#include "engine.hpp"

#include "clip_library.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

const char* kCompositeFragment = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform float uOpacity;
uniform int uMode;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    if (uMode == 2) {
        fragColor = vec4(c * uOpacity, 1.0);
    } else if (uMode == 3) {
        fragColor = vec4(mix(vec3(1.0), c, uOpacity), 1.0);
    } else {
        fragColor = vec4(c, uOpacity);
    }
}
)glsl";

const char* kFinalFragment = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform float uMaster;
uniform float uHue;
uniform float uInvert;
uniform vec2 uMirror;
uniform float uPixel;
uniform float uSplit;
uniform float uAspect;

vec3 hueShift(vec3 c, float h) {
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 toRgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c;
    float a = h * 6.28318;
    float cs = cos(a);
    float sn = sin(a);
    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return clamp(toRgb * yiq, 0.0, 1.0);
}

void main() {
    vec2 uv = vUv;
    if (uMirror.x > 0.5 && uv.x > 0.5) uv.x = 1.0 - uv.x;
    if (uMirror.y > 0.5 && uv.y > 0.5) uv.y = 1.0 - uv.y;
    if (uPixel > 0.001) {
        float n = mix(512.0, 16.0, uPixel);
        vec2 cells = vec2(n * uAspect, n);
        uv = (floor(uv * cells) + 0.5) / cells;
    }
    vec3 c;
    c.r = texture(uTex, uv + vec2(uSplit, 0.0)).r;
    c.g = texture(uTex, uv).g;
    c.b = texture(uTex, uv - vec2(uSplit, 0.0)).b;
    c = hueShift(c, uHue);
    c = mix(c, 1.0 - c, uInvert);
    fragColor = vec4(c * uMaster, 1.0);
}
)glsl";

void applyBlend(int mode) {
    switch (mode) {
    case BlendAdd:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case BlendScreen:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
        break;
    case BlendMultiply:
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        break;
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

}

bool Engine::init(int width, int height, int layerCount, int columnCount) {
    triangle_.create();

    std::string error;
    composite_ = buildProgram(clipVertexSource(), kCompositeFragment, &error);
    if (!composite_) {
        logLine("Composite shader failed: " + error);
        return false;
    }
    error.clear();
    final_ = buildProgram(clipVertexSource(), kFinalFragment, &error);
    if (!final_) {
        logLine("Final shader failed: " + error);
        return false;
    }

    const std::vector<ClipDef>& library = clipLibrary();
    clips_.assign(library.size(), ClipProgram{});
    for (size_t i = 0; i < library.size(); ++i) {
        error.clear();
        GLuint program = buildProgram(clipVertexSource(), clipFragmentSource(library[i]), &error);
        if (!program) {
            logLine("Clip shader '" + library[i].name + "' failed: " + error);
            continue;
        }
        ClipProgram& clip = clips_[i];
        clip.id = program;
        clip.time = glGetUniformLocation(program, "uTime");
        clip.beat = glGetUniformLocation(program, "uBeat");
        clip.bpm = glGetUniformLocation(program, "uBpm");
        clip.res = glGetUniformLocation(program, "uRes");
    }

    if (!createTargets(width, height)) {
        return false;
    }

    columns = columnCount;
    layers.assign(static_cast<size_t>(layerCount), Layer{});
    const int libSize = static_cast<int>(library.size());
    for (int l = 0; l < layerCount; ++l) {
        Layer& layer = layers[static_cast<size_t>(l)];
        layer.cells.assign(static_cast<size_t>(columnCount), -1);
        for (int c = 0; c < std::min(columnCount, 6); ++c) {
            layer.cells[static_cast<size_t>(c)] = (c + l * 2) % libSize;
        }
    }
    if (layerCount > 1) {
        layers[1].blend = BlendAdd;
        layers[1].opacity = 0.6f;
    }
    if (layerCount > 2) {
        layers[2].blend = BlendScreen;
        layers[2].opacity = 0.5f;
    }
    if (layerCount > 3) {
        layers[3].blend = BlendAdd;
        layers[3].opacity = 0.4f;
    }
    trigger(0, 0);
    trigger(1, 0);
    return true;
}

void Engine::shutdown() {
    for (ClipProgram& clip : clips_) {
        if (clip.id) {
            glDeleteProgram(clip.id);
        }
    }
    clips_.clear();
    if (composite_) {
        glDeleteProgram(composite_);
        composite_ = 0;
    }
    if (final_) {
        glDeleteProgram(final_);
        final_ = 0;
    }
    destroyTargets();
    triangle_.destroy();
}

bool Engine::createTargets(int width, int height) {
    return layerTarget_.create(width, height) && comp_.create(width, height) && out_.create(width, height);
}

void Engine::destroyTargets() {
    layerTarget_.destroy();
    comp_.destroy();
    out_.destroy();
}

bool Engine::setResolution(int width, int height) {
    if (width == out_.width && height == out_.height) {
        return true;
    }
    int oldWidth = out_.width;
    int oldHeight = out_.height;
    if (createTargets(width, height)) {
        return true;
    }
    logLine("Resolution change failed, restoring " + std::to_string(oldWidth) + "x" + std::to_string(oldHeight));
    createTargets(oldWidth, oldHeight);
    return false;
}

void Engine::update(double dt) {
    time_ += dt;
    beat_ += dt * static_cast<double>(bpm) / 60.0;
}

void Engine::trigger(int layer, int column) {
    if (layer < 0 || layer >= static_cast<int>(layers.size()) || column < 0 || column >= columns) {
        return;
    }
    Layer& l = layers[static_cast<size_t>(layer)];
    if (l.cells[static_cast<size_t>(column)] < 0) {
        l.activeColumn = -1;
        return;
    }
    l.activeColumn = column;
    l.triggerTime = time_;
}

void Engine::triggerColumn(int column) {
    if (column < 0 || column >= columns) {
        return;
    }
    for (int l = 0; l < static_cast<int>(layers.size()); ++l) {
        if (layers[static_cast<size_t>(l)].cells[static_cast<size_t>(column)] >= 0) {
            trigger(l, column);
        }
    }
}

void Engine::clearLayer(int layer) {
    if (layer < 0 || layer >= static_cast<int>(layers.size())) {
        return;
    }
    layers[static_cast<size_t>(layer)].activeColumn = -1;
}

void Engine::tap() {
    if (!taps_.empty() && time_ - taps_.back() > 2.0) {
        taps_.clear();
    }
    taps_.push_back(time_);
    while (taps_.size() > 5) {
        taps_.pop_front();
    }
    if (taps_.size() >= 2) {
        double interval = (taps_.back() - taps_.front()) / static_cast<double>(taps_.size() - 1);
        if (interval > 0.0) {
            bpm = static_cast<float>(std::clamp(60.0 / interval, 40.0, 240.0));
        }
    }
    beat_ = std::floor(beat_ + 0.5);
}

void Engine::resync() {
    beat_ = 0.0;
}

void Engine::render() {
    if (!out_.fbo) {
        return;
    }

    const float w = static_cast<float>(out_.width);
    const float h = static_cast<float>(out_.height);

    comp_.bind();
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool anySolo = false;
    for (const Layer& layer : layers) {
        anySolo = anySolo || layer.solo;
    }

    for (const Layer& layer : layers) {
        if (layer.bypass || (anySolo && !layer.solo) || layer.activeColumn < 0) {
            continue;
        }
        int clipIndex = layer.cells[static_cast<size_t>(layer.activeColumn)];
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips_.size()) || !clips_[static_cast<size_t>(clipIndex)].id) {
            continue;
        }
        const ClipProgram& clip = clips_[static_cast<size_t>(clipIndex)];

        layerTarget_.bind();
        glDisable(GL_BLEND);
        glUseProgram(clip.id);
        glUniform1f(clip.time, static_cast<float>(time_ - layer.triggerTime));
        glUniform1f(clip.beat, static_cast<float>(beat_));
        glUniform1f(clip.bpm, bpm);
        glUniform2f(clip.res, w, h);
        triangle_.draw();

        comp_.bind();
        glEnable(GL_BLEND);
        applyBlend(layer.blend);
        glUseProgram(composite_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, layerTarget_.texture);
        glUniform1i(glGetUniformLocation(composite_, "uTex"), 0);
        glUniform1f(glGetUniformLocation(composite_, "uOpacity"), layer.opacity);
        glUniform1i(glGetUniformLocation(composite_, "uMode"), layer.blend);
        triangle_.draw();
    }

    glDisable(GL_BLEND);
    out_.bind();
    glUseProgram(final_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, comp_.texture);
    glUniform1i(glGetUniformLocation(final_, "uTex"), 0);
    glUniform1f(glGetUniformLocation(final_, "uMaster"), master);
    glUniform1f(glGetUniformLocation(final_, "uHue"), fx.hue);
    glUniform1f(glGetUniformLocation(final_, "uInvert"), fx.invert ? 1.0f : 0.0f);
    glUniform2f(glGetUniformLocation(final_, "uMirror"), fx.mirrorX ? 1.0f : 0.0f, fx.mirrorY ? 1.0f : 0.0f);
    glUniform1f(glGetUniformLocation(final_, "uPixel"), fx.pixelate);
    glUniform1f(glGetUniformLocation(final_, "uSplit"), fx.rgbSplit);
    glUniform1f(glGetUniformLocation(final_, "uAspect"), w / h);
    triangle_.draw();

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}

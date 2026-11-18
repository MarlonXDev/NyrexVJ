#include "engine.hpp"

#include "clip_library.hpp"
#include "effect_library.hpp"
#include "log.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr int kThumbWidth = 160;
constexpr int kThumbHeight = 90;

const char* kPassHeader = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
)glsl";

const char* kCompositeBody = R"glsl(
uniform sampler2D uTex;
uniform float uOpacity;
uniform int uMode;
uniform vec4 uGrade;
void main() {
    vec3 c = applyGrade(texture(uTex, vUv).rgb, uGrade);
    if (uMode == 2) {
        fragColor = vec4(c * uOpacity, 1.0);
    } else if (uMode == 3) {
        fragColor = vec4(mix(vec3(1.0), c, uOpacity), 1.0);
    } else {
        fragColor = vec4(c, uOpacity);
    }
}
)glsl";

const char* kFinalBody = R"glsl(
uniform sampler2D uTex;
uniform float uLevel;
uniform vec4 uGrade;
void main() {
    vec3 c = applyGrade(texture(uTex, vUv).rgb, uGrade);
    fragColor = vec4(clamp(c, 0.0, 1.0) * uLevel, 1.0);
}
)glsl";

const char* kThumbBody = R"glsl(
uniform sampler2D uTex;
uniform vec2 uTexel;
void main() {
    vec3 acc = vec3(0.0);
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            vec2 o = (vec2(float(i), float(j)) - 1.5) * 3.0 * uTexel;
            acc += texture(uTex, vUv + o).rgb;
        }
    }
    fragColor = vec4(acc / 16.0, 1.0);
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

void clearTarget(const RenderTarget& target) {
    target.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

}

bool Engine::buildPrograms() {
    std::string error;

    const std::string grade = gradeGlsl();
    composite_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + grade + kCompositeBody, &error);
    if (!composite_.id) {
        logLine("Composite shader failed: " + error);
        return false;
    }
    composite_.tex = glGetUniformLocation(composite_.id, "uTex");
    composite_.opacity = glGetUniformLocation(composite_.id, "uOpacity");
    composite_.mode = glGetUniformLocation(composite_.id, "uMode");
    composite_.grade = glGetUniformLocation(composite_.id, "uGrade");

    error.clear();
    final_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + grade + kFinalBody, &error);
    if (!final_.id) {
        logLine("Final shader failed: " + error);
        return false;
    }
    final_.tex = glGetUniformLocation(final_.id, "uTex");
    final_.level = glGetUniformLocation(final_.id, "uLevel");
    final_.grade = glGetUniformLocation(final_.id, "uGrade");

    error.clear();
    thumb_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + kThumbBody, &error);
    if (!thumb_.id) {
        logLine("Thumbnail shader failed: " + error);
        return false;
    }
    thumb_.tex = glGetUniformLocation(thumb_.id, "uTex");
    thumb_.texel = glGetUniformLocation(thumb_.id, "uTexel");

    const std::vector<ClipDef>& clipDefs = clipLibrary();
    clips_.assign(clipDefs.size(), ClipProgram{});
    for (size_t i = 0; i < clipDefs.size(); ++i) {
        error.clear();
        GLuint program = buildProgram(clipVertexSource(), clipFragmentSource(clipDefs[i]), &error);
        if (!program) {
            logLine("Clip shader '" + clipDefs[i].name + "' failed: " + error);
            continue;
        }
        ClipProgram& clip = clips_[i];
        clip.id = program;
        clip.time = glGetUniformLocation(program, "uTime");
        clip.beat = glGetUniformLocation(program, "uBeat");
        clip.bpm = glGetUniformLocation(program, "uBpm");
        clip.res = glGetUniformLocation(program, "uRes");
        clip.colorA = glGetUniformLocation(program, "uColorA");
        clip.colorB = glGetUniformLocation(program, "uColorB");
        clip.rainbow = glGetUniformLocation(program, "uRainbow");
        clip.scale = glGetUniformLocation(program, "uScale");
    }

    const std::vector<EffectDef>& effectDefs = effectLibrary();
    effects_.assign(effectDefs.size(), EffectProgram{});
    for (size_t i = 0; i < effectDefs.size(); ++i) {
        error.clear();
        GLuint program = buildProgram(clipVertexSource(), effectFragmentSource(static_cast<int>(i)), &error);
        if (!program) {
            logLine("Effect shader '" + effectDefs[i].name + "' failed: " + error);
            continue;
        }
        EffectProgram& effect = effects_[i];
        effect.id = program;
        effect.tex = glGetUniformLocation(program, "uTex");
        effect.prev = glGetUniformLocation(program, "uPrev");
        effect.params = glGetUniformLocation(program, "uParams");
        effect.time = glGetUniformLocation(program, "uTime");
        effect.beat = glGetUniformLocation(program, "uBeat");
        effect.aspect = glGetUniformLocation(program, "uAspect");
        effect.res = glGetUniformLocation(program, "uRes");
        effect.usesPrevious = effectDefs[i].usesPrevious;
    }
    return true;
}

bool Engine::init() {
    triangle_.create();
    if (!buildPrograms()) {
        return false;
    }

    comp = Composition{};
    const Resolution& initial = kResolutions[kDefaultResolution];
    if (!createTargets(initial.width, initial.height)) {
        return false;
    }
    currentResolution_ = kDefaultResolution;
    comp.resolution = kDefaultResolution;

    clipThumbs_.assign(clips_.size(), RenderTarget{});
    for (RenderTarget& thumb : clipThumbs_) {
        thumb.create(kThumbWidth, kThumbHeight);
    }
    for (RenderTarget& preview : previews_) {
        preview.create(kThumbWidth, kThumbHeight);
        clearTarget(preview);
    }
    renderClipThumbnails();
    return true;
}

void Engine::shutdown() {
    for (ClipProgram& clip : clips_) {
        if (clip.id) {
            glDeleteProgram(clip.id);
        }
    }
    clips_.clear();
    for (EffectProgram& effect : effects_) {
        if (effect.id) {
            glDeleteProgram(effect.id);
        }
    }
    effects_.clear();
    for (GLuint program : {composite_.id, final_.id, thumb_.id}) {
        if (program) {
            glDeleteProgram(program);
        }
    }
    composite_ = CompositeProgram{};
    final_ = FinalProgram{};
    thumb_ = ThumbProgram{};
    for (RenderTarget& thumb : clipThumbs_) {
        thumb.destroy();
    }
    clipThumbs_.clear();
    for (RenderTarget& preview : previews_) {
        preview.destroy();
    }
    destroyFeedback();
    destroyTargets();
    triangle_.destroy();
}

bool Engine::createTargets(int width, int height) {
    return layerA_.create(width, height) && layerB_.create(width, height) && compA_.create(width, height) && compB_.create(width, height) && out_.create(width, height);
}

void Engine::destroyTargets() {
    layerA_.destroy();
    layerB_.destroy();
    compA_.destroy();
    compB_.destroy();
    out_.destroy();
}

void Engine::destroyFeedback() {
    for (RenderTarget& target : feedback_) {
        target.destroy();
    }
}

bool Engine::setResolutionIndex(int index) {
    if (index < 0 || index >= kResolutionCount) {
        return false;
    }
    if (index == currentResolution_ && out_.fbo) {
        comp.resolution = index;
        return true;
    }
    const Resolution& wanted = kResolutions[index];
    destroyFeedback();
    if (createTargets(wanted.width, wanted.height)) {
        currentResolution_ = index;
        comp.resolution = index;
        return true;
    }
    logLine(std::string("Resolution change to ") + wanted.label + " failed");
    if (currentResolution_ >= 0) {
        const Resolution& previous = kResolutions[currentResolution_];
        createTargets(previous.width, previous.height);
    }
    comp.resolution = currentResolution_;
    return false;
}

void Engine::applyComposition(const Composition& source) {
    int wanted = std::clamp(source.resolution, 0, kResolutionCount - 1);
    comp = source;
    comp.bpm = std::clamp(comp.bpm, 40.0f, 240.0f);
    comp.resolution = currentResolution_ >= 0 ? currentResolution_ : wanted;
    setResolutionIndex(wanted);
    for (int l = 0; l < kLayerCount; ++l) {
        Layer& layer = comp.layers[static_cast<size_t>(l)];
        if (layer.activeColumn < 0 || layer.activeColumn >= kColumnCount || layer.cells[static_cast<size_t>(layer.activeColumn)] < 0) {
            layer.activeColumn = -1;
        }
        layerClock_[static_cast<size_t>(l)] = 0.0;
    }
}

void Engine::update(double dt) {
    time_ += dt;
    beat_ += dt * static_cast<double>(comp.bpm) / 60.0;
    for (int l = 0; l < kLayerCount; ++l) {
        layerClock_[static_cast<size_t>(l)] += dt * static_cast<double>(comp.layers[static_cast<size_t>(l)].speed);
    }
}

void Engine::trigger(int layer, int column) {
    if (layer < 0 || layer >= kLayerCount || column < 0 || column >= kColumnCount) {
        return;
    }
    Layer& l = comp.layers[static_cast<size_t>(layer)];
    if (l.cells[static_cast<size_t>(column)] < 0) {
        l.activeColumn = -1;
        return;
    }
    l.activeColumn = column;
    layerClock_[static_cast<size_t>(layer)] = 0.0;
}

void Engine::triggerColumn(int column) {
    if (column < 0 || column >= kColumnCount) {
        return;
    }
    for (int l = 0; l < kLayerCount; ++l) {
        if (comp.layers[static_cast<size_t>(l)].cells[static_cast<size_t>(column)] >= 0) {
            trigger(l, column);
        }
    }
}

void Engine::clearLayer(int layer) {
    if (layer < 0 || layer >= kLayerCount) {
        return;
    }
    comp.layers[static_cast<size_t>(layer)].activeColumn = -1;
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
            comp.bpm = static_cast<float>(std::clamp(60.0 / interval, 40.0, 240.0));
        }
    }
    beat_ = std::floor(beat_ + 0.5);
}

void Engine::resync() {
    beat_ = 0.0;
}

GLuint Engine::clipThumbnail(int clip) const {
    if (clip < 0 || clip >= static_cast<int>(clipThumbs_.size())) {
        return 0;
    }
    return clipThumbs_[static_cast<size_t>(clip)].texture;
}

GLuint Engine::layerPreview(int layer) const {
    if (layer < 0 || layer >= kLayerCount) {
        return 0;
    }
    return previews_[static_cast<size_t>(layer)].texture;
}

bool Engine::layerLive(int layer) const {
    if (layer < 0 || layer >= kLayerCount) {
        return false;
    }
    return live_[static_cast<size_t>(layer)];
}

bool Engine::readOutput(std::vector<unsigned char>& rgba, int& width, int& height) {
    if (!out_.fbo) {
        return false;
    }
    width = out_.width;
    height = out_.height;
    rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, out_.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Engine::drawClip(const ClipProgram& clip, float time, float beat, float bpm, float width, float height, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale) {
    glUseProgram(clip.id);
    glUniform1f(clip.time, time);
    glUniform1f(clip.beat, beat);
    glUniform1f(clip.bpm, bpm);
    glUniform2f(clip.res, width, height);
    glUniform3f(clip.colorA, colorA[0], colorA[1], colorA[2]);
    glUniform3f(clip.colorB, colorB[0], colorB[1], colorB[2]);
    glUniform1f(clip.rainbow, rainbow ? 1.0f : 0.0f);
    glUniform1f(clip.scale, scale);
    triangle_.draw();
}

void Engine::renderClipThumbnails() {
    const Layer defaults;
    glDisable(GL_BLEND);
    for (size_t i = 0; i < clips_.size(); ++i) {
        if (!clips_[i].id || !clipThumbs_[i].fbo) {
            continue;
        }
        clipThumbs_[i].bind();
        drawClip(clips_[i], 2.6f, 2.3f, 128.0f, static_cast<float>(kThumbWidth), static_cast<float>(kThumbHeight), defaults.colorA, defaults.colorB, false, 1.0f);
    }
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Engine::downsample(const RenderTarget& source, const RenderTarget& target) {
    target.bind();
    glDisable(GL_BLEND);
    glUseProgram(thumb_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glUniform1i(thumb_.tex, 0);
    glUniform2f(thumb_.texel, 1.0f / static_cast<float>(source.width), 1.0f / static_cast<float>(source.height));
    triangle_.draw();
}

void Engine::blit(const RenderTarget& source, const RenderTarget& target) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo);
    glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, target.width, target.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

RenderTarget* Engine::runChain(const std::array<EffectSlot, kEffectSlots>& slots, int chain, RenderTarget* source, RenderTarget* scratch) {
    RenderTarget* in = source;
    RenderTarget* out = scratch;
    const float width = static_cast<float>(source->width);
    const float height = static_cast<float>(source->height);

    for (int s = 0; s < kEffectSlots; ++s) {
        const EffectSlot& slot = slots[static_cast<size_t>(s)];
        if (!slot.enabled || slot.effect < 0 || slot.effect >= static_cast<int>(effects_.size())) {
            continue;
        }
        const EffectProgram& effect = effects_[static_cast<size_t>(slot.effect)];
        if (!effect.id) {
            continue;
        }

        const size_t key = static_cast<size_t>(chain * kEffectSlots + s);
        if (effect.usesPrevious && !feedback_[key].fbo) {
            if (feedback_[key].create(source->width, source->height)) {
                clearTarget(feedback_[key]);
            }
        }

        out->bind();
        glDisable(GL_BLEND);
        glUseProgram(effect.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, in->texture);
        glUniform1i(effect.tex, 0);
        if (effect.usesPrevious && feedback_[key].texture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, feedback_[key].texture);
            glUniform1i(effect.prev, 1);
            glActiveTexture(GL_TEXTURE0);
        }
        glUniform4f(effect.params, slot.params[0], slot.params[1], slot.params[2], slot.params[3]);
        glUniform1f(effect.time, static_cast<float>(time_));
        glUniform1f(effect.beat, static_cast<float>(beat_));
        glUniform1f(effect.aspect, width / height);
        glUniform2f(effect.res, width, height);
        triangle_.draw();

        if (effect.usesPrevious && feedback_[key].fbo) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            blit(*out, feedback_[key]);
            feedbackUsed_[key] = true;
        }

        std::swap(in, out);
    }
    return in;
}

void Engine::render() {
    if (!out_.fbo) {
        return;
    }

    const float w = static_cast<float>(out_.width);
    const float h = static_cast<float>(out_.height);
    feedbackUsed_.fill(false);

    compA_.bind();
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool anySolo = false;
    for (const Layer& layer : comp.layers) {
        anySolo = anySolo || layer.solo;
    }

    for (int l = 0; l < kLayerCount; ++l) {
        const Layer& layer = comp.layers[static_cast<size_t>(l)];
        const size_t li = static_cast<size_t>(l);
        int clipIndex = -1;
        if (!layer.bypass && !(anySolo && !layer.solo) && layer.activeColumn >= 0) {
            clipIndex = layer.cells[static_cast<size_t>(layer.activeColumn)];
        }
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips_.size()) || !clips_[static_cast<size_t>(clipIndex)].id) {
            if (live_[li]) {
                clearTarget(previews_[li]);
            }
            live_[li] = false;
            continue;
        }
        live_[li] = true;

        layerA_.bind();
        glDisable(GL_BLEND);
        drawClip(clips_[static_cast<size_t>(clipIndex)], static_cast<float>(layerClock_[li]), static_cast<float>(beat_), comp.bpm, w, h, layer.colorA, layer.colorB, layer.rainbow, layer.scale);

        RenderTarget* result = runChain(layer.fx, l, &layerA_, &layerB_);
        downsample(*result, previews_[li]);

        compA_.bind();
        glEnable(GL_BLEND);
        applyBlend(layer.blend);
        glUseProgram(composite_.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, result->texture);
        glUniform1i(composite_.tex, 0);
        glUniform1f(composite_.opacity, layer.opacity);
        glUniform1i(composite_.mode, layer.blend);
        glUniform4f(composite_.grade, layer.grade.hue, layer.grade.saturation, layer.grade.brightness, layer.grade.contrast);
        triangle_.draw();
    }

    glDisable(GL_BLEND);
    RenderTarget* master = runChain(comp.master.fx, kLayerCount, &compA_, &compB_);

    out_.bind();
    glUseProgram(final_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, master->texture);
    glUniform1i(final_.tex, 0);
    glUniform1f(final_.level, blackout ? 0.0f : comp.master.level);
    const ColorGrade& grade = comp.master.grade;
    glUniform4f(final_.grade, grade.hue, grade.saturation, grade.brightness, grade.contrast);
    triangle_.draw();

    for (size_t k = 0; k < feedback_.size(); ++k) {
        if (feedback_[k].fbo && !feedbackUsed_[k]) {
            feedback_[k].destroy();
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}

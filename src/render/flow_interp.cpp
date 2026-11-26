#include "render/flow_interp.hpp"

#include "core/flow_synth.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

namespace {

const char* kVertex = R"glsl(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char* kCommon = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uA;
uniform sampler2D uB;
uniform sampler2D uFlow;

vec4 pmTexel(sampler2D tex, ivec2 p, ivec2 size) {
    p = clamp(p, ivec2(0), size - ivec2(1));
    vec4 c = texelFetch(tex, p, 0);
    return vec4(c.rgb * c.a, c.a);
}

vec4 pmSample(sampler2D tex, vec2 uv) {
    ivec2 size = textureSize(tex, 0);
    vec2 x = uv * vec2(size) - vec2(0.5);
    vec2 f = floor(x);
    vec2 t = x - f;
    ivec2 p = ivec2(f);
    vec4 a = pmTexel(tex, p, size);
    vec4 b = pmTexel(tex, p + ivec2(1, 0), size);
    vec4 c = pmTexel(tex, p + ivec2(0, 1), size);
    vec4 d = pmTexel(tex, p + ivec2(1, 1), size);
    return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

vec4 straight(vec4 pm) {
    return pm.a > 1.0e-5 ? vec4(pm.rgb / pm.a, pm.a) : vec4(0.0);
}
)glsl";

const char* kCopyBody = R"glsl(
void main() {
    fragColor = texelFetch(uA, ivec2(gl_FragCoord.xy), 0);
}
)glsl";

const char* kInterpolateBody = R"glsl(
uniform float uT;

void main() {
    vec4 f = texture(uFlow, vUv);
    vec2 d = f.xy;
    float confidence = clamp(f.z, 0.0, 1.0);
    vec4 holdA = pmSample(uA, vUv);
    vec4 holdB = pmSample(uB, vUv);
    vec4 nearest = uT < 0.5 ? holdA : holdB;
    vec4 fromA = pmSample(uA, vUv - uT * d);
    vec4 fromB = pmSample(uB, vUv + (1.0 - uT) * d);
    vec4 blended = mix(fromA, fromB, uT);
    vec4 diff = abs(fromA - fromB);
    float worst = max(max(diff.r, diff.g), max(diff.b, diff.a));
    float consistency = 1.0 - smoothstep(0.06, 0.25, worst);
    fragColor = straight(mix(nearest, blended, confidence * consistency));
}
)glsl";

const char* kBlurBody = R"glsl(
uniform float uShutter;

void main() {
    vec4 f = texture(uFlow, vUv);
    float scale = uShutter * clamp(f.z, 0.0, 1.0);
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 9; ++k) {
        float s = float(k) / 8.0 - 0.5;
        sum += pmSample(uA, vUv + s * scale * f.xy);
    }
    fragColor = straight(sum / 9.0);
}
)glsl";

const char* kVectorsBody = R"glsl(
void main() {
    vec4 f = texture(uFlow, vUv);
    vec2 v = clamp(f.xy / 0.06, vec2(-1.0), vec2(1.0));
    fragColor = vec4(0.5 + 0.5 * v, clamp(f.z, 0.0, 1.0), 1.0);
}
)glsl";

const char* programName(int index) {
    static const char* names[] = {"copy", "interpolate", "blur", "vectors"};
    return names[index];
}

GLuint makeFlowTexture() {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

}

FlowRenderer::~FlowRenderer() {
    destroy();
}

bool FlowRenderer::create(const VideoGpu& gpu, std::string* error) {
    destroy();
    gpu_ = &gpu;
    const std::string common = kCommon;
    const std::string bodies[ProgramCount] = {kCopyBody, kInterpolateBody, kBlurBody, kVectorsBody};
    for (int i = 0; i < ProgramCount; ++i) {
        std::string log;
        program_[i] = buildProgram(kVertex, common + bodies[i], &log);
        if (program_[i] == 0) {
            if (error != nullptr) {
                *error = std::string(programName(i)) + " shader: " + log;
            }
            logLine(std::string("Flow shader '") + programName(i) + "' failed: " + log);
            destroy();
            return false;
        }
        locTex_[i][0] = glGetUniformLocation(program_[i], "uA");
        locTex_[i][1] = glGetUniformLocation(program_[i], "uB");
        locTex_[i][2] = glGetUniformLocation(program_[i], "uFlow");
    }
    locT_ = glGetUniformLocation(program_[Interpolate], "uT");
    locShutter_ = glGetUniformLocation(program_[Blur], "uShutter");
    triangle_.create();
    flowTexture_ = makeFlowTexture();
    setNeutralField();
    return true;
}

void FlowRenderer::destroy() {
    releaseTargets();
    for (GLuint& program : program_) {
        if (program != 0) {
            glDeleteProgram(program);
            program = 0;
        }
    }
    triangle_.destroy();
    if (flowTexture_ != 0) {
        glDeleteTextures(1, &flowTexture_);
        flowTexture_ = 0;
    }
    flowCols_ = 0;
    flowRows_ = 0;
    gpu_ = nullptr;
}

void FlowRenderer::releaseTargets() {
    slot_[0].destroy();
    slot_[1].destroy();
    out_.destroy();
    width_ = 0;
    height_ = 0;
    slots_ = 0;
}

bool FlowRenderer::resize(int width, int height, int slots) {
    if (!ready() || width <= 0 || height <= 0 || slots < 1 || slots > 2) {
        return false;
    }
    if (width == width_ && height == height_ && slots == slots_) {
        return true;
    }
    releaseTargets();
    bool ok = out_.create(width, height);
    for (int i = 0; ok && i < slots; ++i) {
        ok = slot_[i].create(width, height);
    }
    if (!ok) {
        releaseTargets();
        return false;
    }
    width_ = width;
    height_ = height;
    slots_ = slots;
    return true;
}

void FlowRenderer::prepareState() const {
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void FlowRenderer::drawInto(Program program, const RenderTarget& target) {
    target.bind();
    glUseProgram(program_[program]);
    glUniform1i(locTex_[program][0], 0);
    glUniform1i(locTex_[program][1], 1);
    glUniform1i(locTex_[program][2], 2);
    triangle_.draw();
}

void FlowRenderer::store(int slot, GLuint sourceTexture) {
    if (!ready() || slot < 0 || slot >= slots_ || sourceTexture == 0) {
        return;
    }
    const VideoGpu::Saved saved = gpu_->save();
    prepareState();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    drawInto(Copy, slot_[slot]);
    glBindTexture(GL_TEXTURE_2D, 0);
    gpu_->restore(saved);
}

void FlowRenderer::swapSlots() {
    if (slots_ == 2) {
        std::swap(slot_[0], slot_[1]);
    }
}

GLuint FlowRenderer::slotTexture(int slot) const {
    if (slot < 0 || slot >= slots_) {
        return 0;
    }
    return slot_[slot].texture;
}

bool FlowRenderer::setField(const flow::Field& field) {
    if (!ready() || !field.valid()) {
        return false;
    }
    const std::vector<float> packed = flow::packField(field);
    glBindTexture(GL_TEXTURE_2D, flowTexture_);
    if (field.cols == flowCols_ && field.rows == flowRows_) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, field.cols, field.rows, GL_RGBA, GL_FLOAT, packed.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, field.cols, field.rows, 0, GL_RGBA, GL_FLOAT, packed.data());
        flowCols_ = field.cols;
        flowRows_ = field.rows;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void FlowRenderer::setNeutralField() {
    if (flowTexture_ == 0) {
        return;
    }
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glBindTexture(GL_TEXTURE_2D, flowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, zero);
    glBindTexture(GL_TEXTURE_2D, 0);
    flowCols_ = 1;
    flowRows_ = 1;
}

GLuint FlowRenderer::drawInterpolated(float t) {
    if (!ready() || slots_ != 2) {
        return 0;
    }
    const auto begin = std::chrono::steady_clock::now();
    const VideoGpu::Saved saved = gpu_->save();
    prepareState();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, slot_[0].texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, slot_[1].texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, flowTexture_);
    glUseProgram(program_[Interpolate]);
    glUniform1f(locT_, std::clamp(t, 0.0f, 1.0f));
    drawInto(Interpolate, out_);
    for (int unit = 2; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    gpu_->restore(saved);
    lastDrawMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return out_.texture;
}

GLuint FlowRenderer::drawBlur(float shutter) {
    if (!ready() || slots_ < 1) {
        return 0;
    }
    const auto begin = std::chrono::steady_clock::now();
    const VideoGpu::Saved saved = gpu_->save();
    prepareState();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, slot_[0].texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, flowTexture_);
    glUseProgram(program_[Blur]);
    glUniform1f(locShutter_, std::clamp(shutter, 0.0f, 1.0f));
    drawInto(Blur, out_);
    for (int unit = 2; unit >= 0; unit -= 2) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    gpu_->restore(saved);
    lastDrawMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return out_.texture;
}

GLuint FlowRenderer::drawVectors() {
    if (!ready() || slots_ < 1) {
        return 0;
    }
    const auto begin = std::chrono::steady_clock::now();
    const VideoGpu::Saved saved = gpu_->save();
    prepareState();
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, flowTexture_);
    drawInto(Vectors, out_);
    glBindTexture(GL_TEXTURE_2D, 0);
    gpu_->restore(saved);
    lastDrawMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return out_.texture;
}

unsigned long long FlowRenderer::bytes() const {
    unsigned long long total = out_.bytes();
    for (int i = 0; i < slots_; ++i) {
        total += slot_[i].bytes();
    }
    total += static_cast<unsigned long long>(flowCols_) * static_cast<unsigned long long>(flowRows_) * 8ull;
    return total;
}

}

#include "render/color_gpu.hpp"

namespace nvj {

std::string colorGlslCommon() {
    return R"glsl(
float cmSrgbToLinear(float v) {
    v = clamp(v, 0.0, 1.0);
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

vec3 cmSrgbToLinear(vec3 v) {
    return vec3(cmSrgbToLinear(v.r), cmSrgbToLinear(v.g), cmSrgbToLinear(v.b));
}

float cmLinearToDisplay(float l, int mode) {
    if (l <= 0.0) {
        return 0.0;
    }
    l = min(l, 1.0);
    if (mode == 1) {
        return pow(l, 1.0 / 2.2);
    }
    if (mode == 2) {
        return pow(l, 1.0 / 2.4);
    }
    if (mode == 3) {
        return pow(l, 1.0 / 2.6);
    }
    return l <= 0.0031308 ? 12.92 * l : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}

vec3 cmLinearToDisplay(vec3 l, int mode) {
    return vec3(cmLinearToDisplay(l.r, mode), cmLinearToDisplay(l.g, mode), cmLinearToDisplay(l.b, mode));
}
)glsl";
}

std::string colorGlslHdr() {
    return R"glsl(
uniform int uHdrMode;
uniform vec4 uHdrParams;
uniform int uHdrTone;
uniform mat3 uHdrMatrix;
uniform vec3 uHdrLumaSource;
uniform vec3 uHdrLuma709;

float cmPqToNits(float e) {
    e = clamp(e, 0.0, 1.0);
    if (e <= 0.0) {
        return 0.0;
    }
    float p = pow(e, 1.0 / 78.84375);
    float numerator = max(p - 0.8359375, 0.0);
    float denominator = 18.8515625 - 18.6875 * p;
    return 10000.0 * pow(numerator / denominator, 1.0 / 0.1593017578125);
}

float cmHlgInverse(float e) {
    e = clamp(e, 0.0, 1.0);
    return e <= 0.5 ? e * e / 3.0 : (exp((e - 0.55991073) / 0.17883277) + 0.28466892) / 12.0;
}

float cmToneShoulder(float v, float w) {
    if (!(v > 0.0)) {
        return 0.0;
    }
    if (v <= 0.8) {
        return v;
    }
    if (w <= 0.800001) {
        return min(v, 1.0);
    }
    float span = 0.2;
    float xw = (w - 0.8) / span;
    float scale = (1.0 + xw) / xw;
    float x = (v - 0.8) / span;
    return min(1.0, 0.8 + span * scale * x / (1.0 + x));
}

float cmHable(float x) {
    return ((x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.5) + 0.06)) - 0.02 / 0.30;
}

float cmToneFilmic(float v, float w) {
    if (!(v > 0.0)) {
        return 0.0;
    }
    float white = max(w, 1.0);
    return min(1.0, cmHable(v * 2.0) / cmHable(white * 2.0));
}

vec3 cmGamutMap(vec3 c) {
    float y = dot(c, uHdrLuma709);
    if (!(y > 0.0)) {
        return vec3(0.0);
    }
    float t = 1.0;
    if (c.r < 0.0) {
        t = min(t, y / (y - c.r));
    }
    if (c.g < 0.0) {
        t = min(t, y / (y - c.g));
    }
    if (c.b < 0.0) {
        t = min(t, y / (y - c.b));
    }
    if (t < 1.0) {
        c = vec3(y) + t * (c - vec3(y));
    }
    c = max(c, vec3(0.0));
    float peak = max(c.r, max(c.g, c.b));
    if (peak > 1.0) {
        c /= peak;
    }
    return c;
}

vec3 cmHdrToSdr(vec3 e) {
    vec3 relative;
    if (uHdrMode == 2) {
        vec3 scene = vec3(cmHlgInverse(e.r), cmHlgInverse(e.g), cmHlgInverse(e.b));
        float ys = dot(scene, uHdrLumaSource);
        float gain = ys > 0.0 ? pow(ys, uHdrParams.w) : 0.0;
        relative = uHdrParams.y * gain * scene / uHdrParams.x;
    } else {
        relative = vec3(cmPqToNits(e.r), cmPqToNits(e.g), cmPqToNits(e.b)) / uHdrParams.x;
    }
    float luminance = dot(relative, uHdrLumaSource);
    if (!(luminance > 0.0)) {
        return vec3(0.0);
    }
    float mapped = uHdrTone == 1 ? cmToneFilmic(luminance, uHdrParams.z) : cmToneShoulder(luminance, uHdrParams.z);
    vec3 linear709 = cmGamutMap(uHdrMatrix * (relative * (mapped / luminance)));
    return cmLinearToDisplay(linear709, 0);
}
)glsl";
}

void uploadMatrix3(GLint location, const Matrix3& rowMajor) {
    if (location < 0) {
        return;
    }
    float values[9];
    for (size_t i = 0; i < 9; ++i) {
        values[i] = static_cast<float>(rowMajor[i]);
    }
    glUniformMatrix3fv(location, 1, GL_TRUE, values);
}

void HdrUniforms::bind(GLuint program) {
    mode = glGetUniformLocation(program, "uHdrMode");
    params = glGetUniformLocation(program, "uHdrParams");
    tone = glGetUniformLocation(program, "uHdrTone");
    matrix = glGetUniformLocation(program, "uHdrMatrix");
    lumaSource = glGetUniformLocation(program, "uHdrLumaSource");
    luma709 = glGetUniformLocation(program, "uHdrLuma709");
}

void HdrUniforms::upload(const HdrToSdr* input) const {
    if (mode < 0) {
        return;
    }
    if (input == nullptr) {
        glUniform1i(mode, 0);
        return;
    }
    const HdrToSdr p = sanitized(*input);
    glUniform1i(mode, p.source == Transfer::Hlg ? 2 : 1);
    glUniform4f(params, static_cast<float>(p.referenceWhite), static_cast<float>(p.peak), static_cast<float>(p.peak / p.referenceWhite), static_cast<float>(hlgSystemGamma(p.peak) - 1.0));
    glUniform1i(tone, p.mapper == ToneMapper::Filmic ? 1 : 0);
    uploadMatrix3(matrix, primariesMatrix(p.primaries, Primaries::Rec709));
    const Color3 lSource = lumaCoefficients(p.primaries);
    const Color3 l709 = lumaCoefficients(Primaries::Rec709);
    glUniform3f(lumaSource, static_cast<float>(lSource[0]), static_cast<float>(lSource[1]), static_cast<float>(lSource[2]));
    glUniform3f(luma709, static_cast<float>(l709[0]), static_cast<float>(l709[1]), static_cast<float>(l709[2]));
}

void DisplayUniforms::bind(GLuint program) {
    on = glGetUniformLocation(program, "uDisplayOn");
    matrix = glGetUniformLocation(program, "uDisplayMatrix");
    transfer = glGetUniformLocation(program, "uDisplayTransfer");
}

void DisplayUniforms::upload(const DisplayColor& input) const {
    if (on < 0) {
        return;
    }
    const DisplayColor display = sanitized(input);
    glUniform1i(on, display.isDefault() ? 0 : 1);
    if (display.isDefault()) {
        return;
    }
    uploadMatrix3(matrix, displayMatrix(display));
    glUniform1i(transfer, displayTransferIndex(display.transfer));
}

namespace {

FramebufferDepth readDepth(GLenum target, GLenum attachment) {
    FramebufferDepth depth;
    GLint red = 0;
    GLint green = 0;
    GLint blue = 0;
    GLint alpha = 0;
    GLint type = 0;
    while (glGetError() != GL_NO_ERROR) {
    }
    glGetFramebufferAttachmentParameteriv(target, attachment, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &red);
    glGetFramebufferAttachmentParameteriv(target, attachment, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &green);
    glGetFramebufferAttachmentParameteriv(target, attachment, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &blue);
    glGetFramebufferAttachmentParameteriv(target, attachment, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &alpha);
    glGetFramebufferAttachmentParameteriv(target, attachment, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &type);
    if (glGetError() != GL_NO_ERROR) {
        return depth;
    }
    depth.ok = red > 0;
    depth.floating = type == static_cast<GLint>(GL_FLOAT);
    depth.red = red;
    depth.green = green;
    depth.blue = blue;
    depth.alpha = alpha;
    return depth;
}

}

FramebufferDepth measureDefaultFramebuffer() {
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    FramebufferDepth depth = readDepth(GL_FRAMEBUFFER, GL_BACK_LEFT);
    if (!depth.ok) {
        depth = readDepth(GL_FRAMEBUFFER, GL_FRONT_LEFT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    return depth;
}

FramebufferDepth measureFramebuffer(GLuint fbo) {
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    const FramebufferDepth depth = readDepth(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    return depth;
}

std::string describeDepth(const FramebufferDepth& depth) {
    if (!depth.ok) {
        return "unknown";
    }
    std::string text = std::to_string(depth.red) + "/" + std::to_string(depth.green) + "/" + std::to_string(depth.blue);
    if (depth.alpha > 0) {
        text += "/" + std::to_string(depth.alpha);
    }
    text += depth.floating ? " bit float" : " bit";
    return text;
}

}

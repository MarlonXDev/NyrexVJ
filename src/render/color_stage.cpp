#include "render/color_stage.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <vector>

namespace nvj {

namespace {

const char* kStageVertex = R"glsl(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char* kStageFragment = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform sampler3D uLut;
uniform float uLutSize;
uniform vec3 uDomainMin;
uniform vec3 uDomainScale;
uniform float uStrength;

void main() {
    vec4 source = texture(uTex, vUv);
    vec3 t = clamp((source.rgb - uDomainMin) * uDomainScale, 0.0, 1.0);
    vec3 coordinate = (t * (uLutSize - 1.0) + 0.5) / uLutSize;
    vec3 graded = texture(uLut, coordinate).rgb;
    fragColor = vec4(mix(source.rgb, graded, uStrength), source.a);
}
)glsl";

}

bool ColorStage::create(std::string* error) {
    destroy();
    std::string log;
    program_ = buildProgram(kStageVertex, kStageFragment, &log);
    if (program_ == 0) {
        if (error != nullptr) {
            *error = log;
        }
        logLine("Colour stage shader failed: " + log);
        return false;
    }
    locTex_ = glGetUniformLocation(program_, "uTex");
    locLut_ = glGetUniformLocation(program_, "uLut");
    locSize_ = glGetUniformLocation(program_, "uLutSize");
    locDomainMin_ = glGetUniformLocation(program_, "uDomainMin");
    locDomainScale_ = glGetUniformLocation(program_, "uDomainScale");
    locStrength_ = glGetUniformLocation(program_, "uStrength");
    return true;
}

void ColorStage::destroy() {
    clearLut();
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

void ColorStage::clearLut() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    size_ = 0;
}

bool ColorStage::setLut(const CubeLut& lut) {
    clearLut();
    if (!lut.valid()) {
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, lut.size, lut.size, lut.size, 0, GL_RGB, GL_FLOAT, lut.rgb.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_3D, 0);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        logLine("Colour stage: the LUT texture could not be created");
        return false;
    }
    texture_ = texture;
    size_ = lut.size;
    for (size_t i = 0; i < 3; ++i) {
        domainMin_[i] = lut.domainMin[i];
        domainScale_[i] = 1.0f / (lut.domainMax[i] - lut.domainMin[i]);
    }
    return true;
}

void ColorStage::apply(const RenderTarget& source, const RenderTarget& destination, float strength, const FullscreenTriangle& triangle) const {
    if (program_ == 0 || !hasLut()) {
        return;
    }
    destination.bind();
    glDisable(GL_BLEND);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glUniform1i(locTex_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, texture_);
    glUniform1i(locLut_, 1);
    glUniform1f(locSize_, static_cast<float>(size_));
    glUniform3f(locDomainMin_, domainMin_[0], domainMin_[1], domainMin_[2]);
    glUniform3f(locDomainScale_, domainScale_[0], domainScale_[1], domainScale_[2]);
    glUniform1f(locStrength_, std::clamp(strength, 0.0f, 1.0f));
    triangle.draw();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

}

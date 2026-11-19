#include "render/dmx_strip.hpp"

#include "core/stage.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <string>

namespace nvj {

namespace {

const char* kStripFragment = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uGrid;
void main() {
    ivec2 cell = ivec2(int(vUv.x * 120.0), int((1.0 - vUv.y) * 13.0));
    cell = clamp(cell, ivec2(0), ivec2(119, 12));
    float v = texelFetch(uGrid, cell, 0).r;
    fragColor = vec4(v, v, v, 1.0);
}
)glsl";

}

bool DmxStrip::init() {
    if (program_) {
        return true;
    }
    program_ = buildProgram(clipVertexSource(), kStripFragment);
    if (!program_) {
        return false;
    }
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uGrid"), 0);
    glUseProgram(0);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kVrslColumns, kVrslRows, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void DmxStrip::shutdown() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
}

void DmxStrip::upload(const std::uint8_t* grid) {
    if (!texture_ || !grid) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kVrslColumns, kVrslRows, GL_RED, GL_UNSIGNED_BYTE, grid);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int DmxStrip::stripHeight(int width, int height) {
    const int wanted = static_cast<int>((static_cast<long long>(width) * kVrslRows + kVrslColumns / 2) / kVrslColumns);
    return std::clamp(wanted, 1, std::max(height, 1));
}

void DmxStrip::draw(const RenderTarget& target, bool atTop, const FullscreenTriangle& triangle) const {
    if (!program_ || !texture_ || !target.fbo || target.width < kVrslColumns) {
        return;
    }
    const int height = stripHeight(target.width, target.height);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, atTop ? target.height - height : 0, target.width, height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    triangle.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glViewport(0, 0, target.width, target.height);
}

}

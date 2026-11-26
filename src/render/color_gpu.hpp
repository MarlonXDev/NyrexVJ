#pragma once

#include "core/color_model.hpp"
#include "render/gl_util.hpp"

#include <string>

namespace nvj {

std::string colorGlslCommon();
std::string colorGlslHdr();

struct HdrUniforms {
    GLint mode = -1;
    GLint params = -1;
    GLint tone = -1;
    GLint matrix = -1;
    GLint lumaSource = -1;
    GLint luma709 = -1;

    void bind(GLuint program);
    void upload(const HdrToSdr* params) const;
};

struct DisplayUniforms {
    GLint on = -1;
    GLint matrix = -1;
    GLint transfer = -1;

    void bind(GLuint program);
    void upload(const DisplayColor& display) const;
};

void uploadMatrix3(GLint location, const Matrix3& rowMajor);

struct FramebufferDepth {
    bool ok = false;
    bool floating = false;
    int red = 0;
    int green = 0;
    int blue = 0;
    int alpha = 0;
};

FramebufferDepth measureDefaultFramebuffer();
FramebufferDepth measureFramebuffer(GLuint fbo);
std::string describeDepth(const FramebufferDepth& depth);

}

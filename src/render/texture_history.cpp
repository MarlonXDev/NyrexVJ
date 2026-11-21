#include "render/texture_history.hpp"

namespace nvj {

namespace {

size_t bytesPerPixel(GLint format) {
    switch (format) {
    case GL_RGBA32F:
        return 16;
    case GL_RGBA16F:
    case GL_RGBA16:
        return 8;
    case GL_RGB16F:
        return 6;
    case GL_R11F_G11F_B10F:
    case GL_RGB10_A2:
        return 4;
    default:
        return 4;
    }
}

}

TextureHistory::~TextureHistory() {
    release();
}

void TextureHistory::release() {
    for (GLuint& texture : textures_) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
    }
    if (readFramebuffer_ != 0) {
        glDeleteFramebuffers(1, &readFramebuffer_);
        readFramebuffer_ = 0;
    }
    if (drawFramebuffer_ != 0) {
        glDeleteFramebuffers(1, &drawFramebuffer_);
        drawFramebuffer_ = 0;
    }
    history_.configure(0);
    history_.clear();
    width_ = 0;
    height_ = 0;
    format_ = 0;
    formatSource_ = 0;
    bytes_ = 0;
}

GLint TextureHistory::queryFormat(GLuint source, int width, int height) {
    if (source == formatSource_ && width == formatWidth_ && height == formatHeight_ && sourceFormat_ != 0) {
        return sourceFormat_;
    }
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, source);
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (format == 0) {
        format = GL_RGBA8;
    }
    formatSource_ = source;
    formatWidth_ = width;
    formatHeight_ = height;
    sourceFormat_ = format;
    return format;
}

bool TextureHistory::allocate(int width, int height, GLint format, int maxDelay) {
    history_.configure(maxDelay);
    const int count = history_.slots();
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    for (int i = 0; i < count; ++i) {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        textures_[static_cast<size_t>(i)] = texture;
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    glGenFramebuffers(1, &readFramebuffer_);
    glGenFramebuffers(1, &drawFramebuffer_);
    width_ = width;
    height_ = height;
    format_ = format;
    bytes_ = static_cast<size_t>(count) * static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerPixel(format);
    return glGetError() == GL_NO_ERROR;
}

void TextureHistory::update(GLuint source, int width, int height, int maxDelay) {
    maxDelay = clampDelay(maxDelay);
    if (maxDelay == 0 || source == 0 || width <= 0 || height <= 0) {
        if (active()) {
            release();
        }
        return;
    }
    const GLint format = queryFormat(source, width, height);
    if (!active() || maxDelay != history_.maxDelay() || width != width_ || height != height_ || format != format_) {
        release();
        formatSource_ = source;
        formatWidth_ = width;
        formatHeight_ = height;
        sourceFormat_ = format;
        if (!allocate(width, height, format, maxDelay)) {
            release();
            return;
        }
    }
    const int slot = history_.push();
    if (slot >= 0) {
        copy(source, textures_[static_cast<size_t>(slot)]);
    }
}

void TextureHistory::copy(GLuint source, GLuint target) {
    GLint previousRead = 0;
    GLint previousDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) {
        glDisable(GL_SCISSOR_TEST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer_);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
}

GLuint TextureHistory::texture(int delay) const {
    const int slot = history_.slotFor(delay);
    if (slot < 0) {
        return 0;
    }
    return textures_[static_cast<size_t>(slot)];
}

}

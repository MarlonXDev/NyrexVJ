#include "render/ndi_output.hpp"

namespace nvj {

NdiOutput::~NdiOutput() {
    stop();
}

bool NdiOutput::start(const std::string& name) {
    releaseBuffers();
    return sender_.start(name);
}

void NdiOutput::stop() {
    sender_.stop();
    releaseBuffers();
    receivers_ = 0;
}

void NdiOutput::releaseBuffers() {
    if (framebuffer_ != 0) {
        glDeleteFramebuffers(1, &framebuffer_);
        framebuffer_ = 0;
    }
    if (buffers_[0] != 0) {
        glDeleteBuffers(2, buffers_);
        buffers_[0] = 0;
        buffers_[1] = 0;
    }
    width_ = 0;
    height_ = 0;
    index_ = 0;
    primed_ = false;
}

bool NdiOutput::active() const {
    return sender_.active();
}

int NdiOutput::connections() const {
    return receivers_;
}

const std::string& NdiOutput::name() const {
    return sender_.name();
}

const std::string& NdiOutput::error() const {
    return sender_.error();
}

void NdiOutput::send(unsigned int texture, int width, int height) {
    if (!sender_.active() || texture == 0 || width <= 0 || height <= 0) {
        return;
    }
    receivers_ = sender_.connections();
    if (receivers_ <= 0) {
        primed_ = false;
        return;
    }

    GLint previousRead = 0;
    GLint previousPack = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPack);

    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (framebuffer_ == 0) {
        glGenFramebuffers(1, &framebuffer_);
    }
    if (buffers_[0] == 0 || width != width_ || height != height_) {
        if (buffers_[0] == 0) {
            glGenBuffers(2, buffers_);
        }
        for (GLuint buffer : buffers_) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
        }
        width_ = width;
        height_ = height;
        index_ = 0;
        primed_ = false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    GLint previousAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers_[index_]);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (primed_) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers_[1 - index_]);
        const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT);
        if (mapped) {
            sender_.sendRgbaBottomUp(static_cast<const uint8_t*>(mapped), width, height);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }
    index_ = 1 - index_;
    primed_ = true;

    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousPack));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
}

}

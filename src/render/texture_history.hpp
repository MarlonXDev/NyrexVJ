#pragma once

#include "core/frame_lock.hpp"

#include <glad/gl.h>

#include <array>
#include <cstddef>

namespace nvj {

class TextureHistory {
public:
    TextureHistory() = default;
    TextureHistory(const TextureHistory&) = delete;
    TextureHistory& operator=(const TextureHistory&) = delete;
    ~TextureHistory();

    void update(GLuint source, int width, int height, int maxDelay);
    void release();
    GLuint texture(int delay) const;
    bool active() const { return history_.slots() > 0; }
    int filled() const { return history_.filled(); }
    size_t gpuBytes() const { return bytes_; }

private:
    bool allocate(int width, int height, GLint format, int maxDelay);
    GLint queryFormat(GLuint source, int width, int height);
    void copy(GLuint source, GLuint target);

    FrameHistory history_;
    std::array<GLuint, kMaxDelayFrames + 1> textures_{};
    GLuint readFramebuffer_ = 0;
    GLuint drawFramebuffer_ = 0;
    int width_ = 0;
    int height_ = 0;
    GLint format_ = 0;
    GLuint formatSource_ = 0;
    int formatWidth_ = 0;
    int formatHeight_ = 0;
    GLint sourceFormat_ = 0;
    size_t bytes_ = 0;
};

}

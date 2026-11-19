#pragma once

#include "core/ndi_sender.hpp"

#include <glad/gl.h>

#include <string>

namespace nvj {

class NdiOutput {
public:
    NdiOutput() = default;
    ~NdiOutput();
    NdiOutput(const NdiOutput&) = delete;
    NdiOutput& operator=(const NdiOutput&) = delete;

    bool start(const std::string& name);
    void stop();
    void send(unsigned int texture, int width, int height);
    bool active() const;
    int connections() const;
    const std::string& name() const;
    const std::string& error() const;

private:
    void releaseBuffers();

    NdiSender sender_;
    GLuint framebuffer_ = 0;
    GLuint buffers_[2] = {0, 0};
    int width_ = 0;
    int height_ = 0;
    int index_ = 0;
    bool primed_ = false;
    int receivers_ = 0;
};

}

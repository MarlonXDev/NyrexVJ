#include "render/shader_passes.hpp"

namespace nvj {

ShaderPassState::ShaderPassState() {
    latest.fill(0);
    scale.fill(1.0f);
    used.fill(false);
    mediaClip.fill(-1);
}

size_t ShaderPassState::bytes() const {
    size_t total = 0;
    for (const std::array<RenderTarget, 2>& pair : buffers) {
        total += pair[0].bytes() + pair[1].bytes();
    }
    for (const RenderTarget& target : media) {
        total += target.bytes();
    }
    return total;
}

void ShaderPassState::destroy() {
    for (std::array<RenderTarget, 2>& pair : buffers) {
        pair[0].destroy();
        pair[1].destroy();
    }
    for (RenderTarget& target : media) {
        target.destroy();
    }
    latest.fill(0);
    scale.fill(1.0f);
    used.fill(false);
    mediaClip.fill(-1);
    clipIndex = -1;
    width = 0;
    height = 0;
    frame = 0;
    allocated = false;
    restart = false;
}

void ShaderPassState::clear() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    for (std::array<RenderTarget, 2>& pair : buffers) {
        for (RenderTarget& target : pair) {
            if (target.fbo) {
                target.bind();
                glClear(GL_COLOR_BUFFER_BIT);
            }
        }
    }
    for (RenderTarget& target : media) {
        if (target.fbo) {
            target.bind();
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    latest.fill(0);
    frame = 0;
    restart = false;
}

bool ShaderPassState::matches(size_t clip, int w, int h) const {
    return allocated && clipIndex == static_cast<int>(clip) && width == w && height == h;
}

}

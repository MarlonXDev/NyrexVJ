#pragma once

#include "render/gl_util.hpp"

#include <cstdint>

namespace nvj {

class DmxStrip {
public:
    bool init();
    void shutdown();
    void upload(const std::uint8_t* grid);
    void draw(const RenderTarget& target, bool atTop, const FullscreenTriangle& triangle) const;
    static int stripHeight(int width, int height);

private:
    GLuint program_ = 0;
    GLuint texture_ = 0;
};

}

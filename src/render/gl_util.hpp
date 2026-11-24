#pragma once

#include <glad/gl.h>

#include <cstddef>
#include <string>

namespace nvj {

GLuint buildProgram(const std::string& vertexSource, const std::string& fragmentSource, std::string* error = nullptr);

struct RenderTarget {
    GLuint fbo = 0;
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    int bytesPerPixel = 4;

    bool create(int w, int h);
    bool createFloat(int w, int h);
    void destroy();
    void bind() const;
    size_t bytes() const { return fbo ? static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(bytesPerPixel) : 0; }
};

class FullscreenTriangle {
public:
    void create();
    void destroy();
    void draw() const;

private:
    GLuint vao_ = 0;
};

}

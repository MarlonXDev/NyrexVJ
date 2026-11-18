#pragma once

#include <glad/gl.h>

#include <string>

namespace nvj {

GLuint buildProgram(const std::string& vertexSource, const std::string& fragmentSource, std::string* error = nullptr);

struct RenderTarget {
    GLuint fbo = 0;
    GLuint texture = 0;
    int width = 0;
    int height = 0;

    bool create(int w, int h);
    void destroy();
    void bind() const;
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

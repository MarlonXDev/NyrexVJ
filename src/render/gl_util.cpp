#include "render/gl_util.hpp"

#include "core/log.hpp"

#include <vector>

namespace nvj {

namespace {

GLuint compileStage(GLenum type, const std::string& source, std::string& error) {
    GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        error += log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}

GLuint buildProgram(const std::string& vertexSource, const std::string& fragmentSource, std::string* error) {
    std::string localError;
    std::string& sink = error ? *error : localError;
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, sink);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource, sink);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        sink += log.data();
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool RenderTarget::create(int w, int h) {
    destroy();
    width = w;
    height = h;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        logLine("Framebuffer incomplete, status " + std::to_string(status));
        destroy();
        return false;
    }
    return true;
}

void RenderTarget::destroy() {
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    if (texture) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    width = 0;
    height = 0;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void FullscreenTriangle::create() {
    if (!vao_) {
        glGenVertexArrays(1, &vao_);
    }
}

void FullscreenTriangle::destroy() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void FullscreenTriangle::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

}

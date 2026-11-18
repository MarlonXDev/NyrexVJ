#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace nvj {

class OutputWindow {
public:
    OutputWindow() = default;
    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;
    ~OutputWindow();

    bool open(GLFWwindow* share, int monitorIndex, bool fullscreen);
    void close();
    bool isOpen() const { return window_ != nullptr; }
    bool isFullscreen() const { return fullscreen_; }
    void setFullscreen(bool fullscreen, int monitorIndex);
    void render(GLuint texture, int textureWidth, int textureHeight);

    static std::vector<std::string> monitorNames();

private:
    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    bool createResources();
    void destroyResources();

    GLFWwindow* window_ = nullptr;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLint textureLocation_ = -1;
    bool fullscreen_ = false;
    int monitorIndex_ = 0;
    int windowedX_ = 100;
    int windowedY_ = 100;
    int windowedWidth_ = 1280;
    int windowedHeight_ = 720;
};

}

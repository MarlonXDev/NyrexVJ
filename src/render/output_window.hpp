#pragma once

#include "core/output_map.hpp"

#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace nvj {

struct OutputCapture {
    std::vector<uint8_t> rgb;
    int width = 0;
    int height = 0;
};

class OutputWindow {
public:
    OutputWindow() = default;
    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;
    ~OutputWindow();

    bool open(GLFWwindow* share, int index, int monitorIndex, bool fullscreen);
    void close();
    bool isOpen() const { return window_ != nullptr; }
    bool isFullscreen() const { return fullscreen_; }
    int monitorIndex() const { return monitorIndex_; }
    void setFullscreen(bool fullscreen, int monitorIndex);
    void render(GLuint texture, int textureWidth, int textureHeight, const OutputMapping& map, bool testPattern, OutputCapture* capture = nullptr);

    static std::vector<std::string> monitorNames();
    static int monitorCount();

private:
    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    bool createResources();
    void destroyResources();
    void uploadMesh(const OutputMapping& map);

    GLFWwindow* window_ = nullptr;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei indexCount_ = 0;
    OutputMapping meshMap_;
    bool meshValid_ = false;
    GLint textureLocation_ = -1;
    GLint rectLocation_ = -1;
    GLint blendLocation_ = -1;
    GLint shapeLocation_ = -1;
    GLint brightnessLocation_ = -1;
    GLint patternLocation_ = -1;
    bool fullscreen_ = false;
    int monitorIndex_ = 0;
    int windowedX_ = 100;
    int windowedY_ = 100;
    int windowedWidth_ = 1280;
    int windowedHeight_ = 720;
};

}

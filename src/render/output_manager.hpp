#pragma once

#include "core/output_map.hpp"
#include "render/output_window.hpp"

#include <array>

struct GLFWwindow;

namespace nvj {

class OutputManager {
public:
    void attach(std::array<OutputSpec, kMaxOutputs>* specs) { specs_ = specs; }
    std::array<OutputSpec, kMaxOutputs>& specs() { return *specs_; }
    const std::array<OutputSpec, kMaxOutputs>& specs() const { return *specs_; }

    bool open(int index, GLFWwindow* share);
    int openEnabled(GLFWwindow* share, bool fullscreen);
    void close(int index);
    void closeAll();
    bool isOpen(int index) const;
    bool anyOpen() const;
    int openCount() const;
    bool isFullscreen(int index) const;
    bool anyFullscreen() const;
    void setFullscreen(int index, bool fullscreen);
    void toggleFullscreen(GLFWwindow* share);
    void setTestPattern(int index, bool on);
    bool testPattern(int index) const;
    void render(GLuint texture, int width, int height);
    bool capture(int index, GLuint texture, int width, int height, OutputCapture& out);

private:
    static bool valid(int index) { return index >= 0 && index < kMaxOutputs; }

    std::array<OutputSpec, kMaxOutputs>* specs_ = nullptr;
    std::array<OutputWindow, kMaxOutputs> windows_;
    std::array<bool, kMaxOutputs> patterns_{};
};

}

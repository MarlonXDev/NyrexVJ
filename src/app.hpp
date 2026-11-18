#pragma once

#include "engine.hpp"

#include <imgui.h>

#include <string>

struct GLFWwindow;

namespace nvj {

class App {
public:
    bool init(GLFWwindow* window);
    void frame(double dt);
    void shutdown();
    Engine& engine() { return engine_; }

private:
    void buildLayout(ImGuiID dock);
    void handleShortcuts();
    void drawDeck();
    void drawOutput();
    void drawMaster();
    void drawLibrary();

    Engine engine_;
    GLFWwindow* window_ = nullptr;
    std::string iniPath_;
    bool needsLayout_ = false;
    int resolutionIndex_ = 1;
    int selectedClip_ = -1;
};

}

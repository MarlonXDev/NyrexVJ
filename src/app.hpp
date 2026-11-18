#pragma once

#include "engine.hpp"
#include "output_window.hpp"
#include "settings.hpp"
#include "updater.hpp"

#include <imgui.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace nvj {

struct StartOptions {
    std::string preset;
    std::string clip;
    std::string effect;
    int column = -1;
    int resolution = -1;
    int selectLayer = -1;
    int tab = -1;
    bool openOutput = false;
    bool fresh = false;
    bool skipUpdateCheck = false;
};

class App {
public:
    bool init(GLFWwindow* window, const StartOptions& options);
    void frame(double dt);
    void shutdown();
    Engine& engine() { return engine_; }
    bool wantsRestart() const { return restart_; }

private:
    void buildLayout(ImGuiID dock);
    void handleShortcuts();
    void drawMenuBar();
    void drawDeck();
    void drawOutput();
    void drawLayer();
    void drawMaster();
    void drawBrowser();
    void drawPresetsTab();
    void drawLibraryTab();
    void drawSettingsTab();
    void drawGrade(ColorGrade& grade);
    void drawEffectRack(std::array<EffectSlot, kEffectSlots>& slots);
    void drawOutputControls();
    void loadComposition(const Composition& comp, const std::string& message);
    void saveCurrentPreset();
    void refreshUserPresets();
    void toast(const std::string& message);
    void openOutputWindow(bool fullscreen);

    Engine engine_;
    Settings settings_;
    Updater updater_;
    OutputWindow output_;
    GLFWwindow* window_ = nullptr;
    std::string iniPath_;
    bool needsLayout_ = false;
    bool restart_ = false;
    int selectedLayer_ = 0;
    int focusTab_ = -1;
    int outputMonitor_ = 0;
    char presetName_[64] = {};
    std::string selectedUserPreset_;
    std::vector<std::string> userPresets_;
    std::string toastText_;
    double toastUntil_ = 0.0;
    double autosaveTimer_ = 0.0;
    bool shutDown_ = false;
};

}

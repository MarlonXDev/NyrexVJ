#pragma once

#include "app/engine_host.hpp"
#include "audio/audio_system.hpp"
#include "control/control_system.hpp"
#include "core/i18n.hpp"
#include "core/settings.hpp"
#include "render/engine.hpp"
#include "render/output_window.hpp"
#include "render/spout_output.hpp"
#include "update/updater.hpp"

#include <imgui.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace nvj {

enum SettingsPage {
    SettingsGeneral = 0,
    SettingsAudio,
    SettingsOutput,
    SettingsControl,
    SettingsUpdates,
    SettingsInterface,
    SettingsShortcuts,
    SettingsAbout,
    SettingsPageCount
};

struct StartOptions {
    std::string preset;
    std::string clip;
    std::string effect;
    std::string language;
    std::string decks;
    std::string transition;
    std::string demoUpdate;
    float transitionSeconds = -1.0f;
    int column = -1;
    int resolution = -1;
    int selectLayer = -1;
    int tab = -1;
    int settingsPage = -1;
    int oscPort = 0;
    bool openOutput = false;
    bool fresh = false;
    bool skipUpdateCheck = false;
    bool audioDemo = false;
    bool forceWelcome = false;
    bool skipWelcome = false;
};

class App {
public:
    bool init(GLFWwindow* window, const StartOptions& options);
    void frame(double dt);
    void shutdown();
    Engine& engine() { return engine_; }
    ControlSystem& control() { return control_; }
    bool wantsRestart() const { return restart_; }
    bool wantsVsync() const { return settings_.vsync; }

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
    void drawAudioTab();
    void drawDeckBar();
    void drawSettingsWindow();
    void drawSettingsPage(int page);
    void drawUpdateSection();
    void drawControlPage();
    void drawOscSection();
    void drawMidiSection();
    void drawBindingsSection();
    void drawAudioSource();
    void drawUpdateCard();
    void drawStatusBar();
    void openSettings(int page);
    std::string audioSourceLabel() const;
    void loadFactoryBank();
    void switchDeckTo(int index);
    void fillDeckFromLook(int index, const Composition& look);
    void drawWelcome();
    void drawGrade(ColorGrade& grade);
    void drawMods(std::array<AudioMod, kModSlots>& mods, bool master);
    void drawEffectRack(std::array<EffectSlot, kEffectSlots>& slots);
    void drawOutputControls();
    void drawSpoutControls();
    void loadComposition(const Composition& comp, const std::string& message);
    void saveCurrentPreset();
    void refreshUserPresets();
    void toast(const std::string& message);
    void openOutputWindow(bool fullscreen);
    void updateAudio(double dt);
    void setLanguageAndSave(Language language);
    void openWelcome(bool firstRun);

    Engine engine_;
    EngineControlHost controlHost_{engine_};
    ControlSystem control_{engine_.comp, controlHost_};
    Settings settings_;
    Updater updater_;
    OutputWindow output_;
    AudioSystem audio_;
    SpoutOutput spout_;
    GLFWwindow* window_ = nullptr;
    std::string iniPath_;
    bool needsLayout_ = false;
    bool restart_ = false;
    int selectedLayer_ = 0;
    int focusTab_ = -1;
    int outputMonitor_ = 0;
    char presetName_[64] = {};
    char spoutName_[64] = {};
    std::string selectedUserPreset_;
    std::vector<std::string> userPresets_;
    std::string toastText_;
    double toastUntil_ = 0.0;
    double autosaveTimer_ = 0.0;
    bool shutDown_ = false;
    bool welcomeOpen_ = false;
    bool welcomeRequested_ = false;
    bool welcomeFirstRun_ = false;
    std::string welcomeSince_;
    bool settingsOpen_ = false;
    bool settingsFocus_ = false;
    int settingsPage_ = 0;
    float uiScaleEdit_ = 1.0f;
    bool updateCardCollapsed_ = false;
    std::string autoInstallKey_;
    int renameDeck_ = -1;
    bool renameRequested_ = false;
    char deckNameBuffer_[48] = {};
    int appliedVsync_ = -1;
    int oscOverridePort_ = 0;
    char targetFilter_[64] = {};
};

}

#include "app/app.hpp"

#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "scene3d/builtin_docs.hpp"
#include "update/changelog.hpp"

#include <GLFW/glfw3.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace nvj {

namespace {

Composition emptyComposition() {
    Composition comp;
    comp.name = "Empty";
    return comp;
}

}

bool App::init(GLFWwindow* window, const StartOptions& options) {
    window_ = window;
    ImGuiIO& io = ImGui::GetIO();
    iniPath_ = (std::filesystem::path(userDataDir()) / "layout_v6.ini").string();
    io.IniFilename = iniPath_.c_str();
    needsLayout_ = !std::filesystem::exists(iniPath_);

    loadSettings(settings_);
    applyTheme(themeIndex(settings_.theme));
    outputMonitor_ = settings_.outputMonitor;
    uiScaleEdit_ = settings_.uiScale;
    appliedVsync_ = settings_.vsync ? 1 : 0;
    oscOverridePort_ = options.oscPort;
    control_.loadBindings();
    control_.refreshMidiDevices();
    setLanguage(languageFromCode(options.language.empty() ? settings_.language : options.language));

    if (!engine_.init()) {
        logLine("Engine initialisation failed");
        return false;
    }

    Composition start = defaultComposition();
    bool chosen = false;
    if (!options.preset.empty()) {
        int factory = factoryPresetIndex(options.preset);
        std::string error;
        if (factory >= 0) {
            start = factoryPresets()[static_cast<size_t>(factory)];
            chosen = true;
        } else if (loadUserPreset(options.preset, start, error)) {
            chosen = true;
        } else {
            logLine("Preset '" + options.preset + "' not found");
        }
    }
    if (!chosen && settings_.restoreSession && !options.fresh && options.clip.empty()) {
        Composition session;
        if (loadSession(session)) {
            start = session;
        }
    }
    if (!options.clip.empty()) {
        int clip = clipIndexByName(options.clip);
        if (clip >= 0) {
            start = emptyComposition();
            start.name = options.clip;
            start.layers[0].cells[0] = clip;
            start.layers[0].activeColumn = 0;
        } else {
            logLine("Clip '" + options.clip + "' not found");
        }
    }
    if (!options.effect.empty()) {
        int effect = effectIndexByName(options.effect);
        if (effect >= 0) {
            setEffect(start.master.fx[0], effect);
        } else {
            logLine("Effect '" + options.effect + "' not found");
        }
    }
    if (options.resolution >= 0 && options.resolution < kResolutionCount) {
        start.resolution = options.resolution;
    }
    if (!options.decks.empty()) {
        std::string list = options.decks;
        size_t slot = 0;
        while (!list.empty() && slot < static_cast<size_t>(kDeckCount)) {
            const size_t comma = list.find(',');
            const std::string name = list.substr(0, comma);
            list = comma == std::string::npos ? std::string() : list.substr(comma + 1);
            const int factory = factoryPresetIndex(name);
            if (factory < 0) {
                logLine("Deck preset '" + name + "' not found");
                continue;
            }
            const Composition& look = factoryPresets()[static_cast<size_t>(factory)];
            if (slot == 0) {
                start.layers = look.layers;
                start.master = look.master;
                start.deckNames[0] = look.name;
            } else {
                start.decks[slot] = captureDeck(look);
                start.deckUsed[slot] = true;
                start.deckNames[slot] = look.name;
            }
            ++slot;
        }
    }
    if (!options.transition.empty()) {
        start.deckSettings.transition = transitionFromName(options.transition);
    }
    if (options.transitionSeconds >= 0.0f) {
        start.deckSettings.syncBeats = false;
        start.deckSettings.seconds = options.transitionSeconds;
    }
    engine_.applyComposition(start);
    std::snprintf(presetName_, sizeof(presetName_), "%s", engine_.comp.name.c_str());

    if (options.column >= 0) {
        engine_.triggerColumn(options.column);
    }
    if (options.selectLayer >= 0 && options.selectLayer < kLayerCount) {
        selectedLayer_ = options.selectLayer;
    }
    focusTab_ = options.tab;
    if (options.libraryMode >= 0) {
        libraryMode_ = std::clamp(options.libraryMode, 0, 2);
    }
    if (options.libraryKind >= 0) {
        libraryKind_ = std::clamp(options.libraryKind, 0, 4);
    }

    refreshUserPresets();

    audio_.refreshDevices();
    if (options.audioDemo) {
        audio_.select(AudioSystem::demoId());
    } else if (!settings_.audioDevice.empty()) {
        if (!audio_.select(settings_.audioDevice)) {
            logLine("Audio device could not be restored: " + audio_.error());
        }
    }

    std::snprintf(spoutName_, sizeof(spoutName_), "%s", settings_.spoutName.c_str());
    if (settings_.spoutEnabled && SpoutOutput::supported()) {
        spout_.start(spoutName_);
    }

    updater_.setChannel(settings_.channel);
    if (settings_.checkUpdates && !options.skipUpdateCheck) {
        updater_.checkAsync();
    }

    if (!options.demoUpdate.empty()) {
        UpdateStatus demo;
        demo.release.tag = "nightly";
        demo.release.name = "NyrexVJ 0.4.0";
        demo.release.version = parseVersion("0.4.0");
        demo.release.buildNumber = 57;
        autoInstallKey_ = demo.release.tag + "#" + std::to_string(demo.release.buildNumber);
        if (options.demoUpdate == "ready") {
            demo.state = UpdateState::ReadyToRestart;
        } else if (options.demoUpdate == "downloading") {
            demo.state = UpdateState::Downloading;
            demo.progress = 0.55f;
        } else {
            demo.state = UpdateState::Available;
        }
        updater_.debugSetStatus(demo);
        if (options.demoUpdate == "later") {
            demo.state = UpdateState::ReadyToRestart;
            updater_.debugSetStatus(demo);
            updateCardCollapsed_ = true;
        }
    }

    if (options.openOutput || (settings_.openOutputOnStart && options.skipUpdateCheck == false)) {
        openOutputWindow(false);
    }
    if (options.settingsPage >= 0) {
        openSettings(options.settingsPage);
    }
    if (options.openCreator) {
        if (options.creator.empty()) {
            openCreator();
        } else {
            openCreatorWith(options.creator);
        }
    }

    const std::string current = baseVersionString(NYREXVJ_VERSION);
    welcomeSince_ = settings_.lastSeenVersion;
    if (options.forceWelcome) {
        openWelcome(settings_.lastSeenVersion.empty());
    } else if (!options.skipWelcome) {
        const WelcomeKind kind = welcomeKind(settings_.lastSeenVersion, current);
        if (kind != WelcomeKind::None) {
            openWelcome(kind == WelcomeKind::FirstRun);
        }
    }
    return true;
}

void App::openWelcome(bool firstRun) {
    welcomeRequested_ = true;
    welcomeFirstRun_ = firstRun;
}

void App::shutdown() {
    if (shutDown_) {
        return;
    }
    shutDown_ = true;
    saveSession(engine_.comp);
    settings_.outputMonitor = outputMonitor_;
    settings_.audioDevice = audio_.selected() == AudioSystem::demoId() ? std::string() : audio_.selected();
    settings_.spoutEnabled = spout_.active();
    settings_.spoutName = spoutName_;
    saveSettings(settings_);
    spout_.stop();
    oscOut_.stop();
    audio_.select("");
    output_.close();
    shutdownCreator();
    engine_.shutdown();
}

void App::toast(const std::string& message) {
    toastText_ = message;
    toastUntil_ = ImGui::GetTime() + 4.0;
}

void App::openOutputWindow(bool fullscreen) {
    if (!output_.open(window_, outputMonitor_, fullscreen)) {
        toast(tr("Could not open the output window"));
    }
}

void App::refreshUserPresets() {
    userPresets_ = listUserPresets();
}

void App::loadComposition(const Composition& comp, const std::string& message) {
    engine_.applyComposition(comp);
    std::snprintf(presetName_, sizeof(presetName_), "%s", engine_.comp.name.c_str());
    toast(message);
}

void App::saveCurrentPreset() {
    std::string error;
    std::string name = sanitizePresetName(presetName_);
    if (!saveUserPreset(engine_.comp, name, error)) {
        toast(trMessage(error));
        return;
    }
    engine_.comp.name = name;
    selectedUserPreset_ = name;
    refreshUserPresets();
    toast(std::string(tr("Saved preset: ")) + name);
}

void App::setLanguageAndSave(Language language) {
    setLanguage(language);
    settings_.language = languageCode(language);
    saveSettings(settings_);
}

void App::updateAudio(double dt) {
    audio_.setGain(settings_.audioGain);
    audio_.setSmoothing(settings_.audioSmoothing);
    audio_.setBeatSensitivity(settings_.audioBeatSensitivity);
    audio_.update(dt, engine_.comp.bpm);
    const AudioFeatures& features = audio_.features();
    engine_.setAudio(features);
    if (settings_.audioSyncBpm && features.active) {
        if (features.bpm > 40.0f && features.bpmConfidence > 0.55f) {
            float blended = engine_.comp.bpm + (features.bpm - engine_.comp.bpm) * 0.08f;
            engine_.comp.bpm = std::clamp(blended, 40.0f, 240.0f);
        }
        if (features.beatNow) {
            engine_.nudgeBeat(0.35f);
        }
    }
}

void App::frame(double dt) {
    const int vsync = settings_.vsync ? 1 : 0;
    if (vsync != appliedVsync_) {
        glfwSwapInterval(vsync);
        appliedVsync_ = vsync;
    }
    ImGui::GetStyle().FontScaleMain = settings_.uiScale;
    ControlConfig control;
    control.oscEnabled = settings_.oscEnabled || oscOverridePort_ > 0;
    control.oscPort = oscOverridePort_ > 0 ? oscOverridePort_ : settings_.oscPort;
    control.oscAllowRemote = settings_.oscAllowRemote;
    control.midiDevice = settings_.midiDevice;
    control_.configure(control);
    control_.update(dt);
    updateAudio(dt);
    engine_.update(dt);
    engine_.configureSpout(settings_.spoutInputs, spout_.active() ? std::string(spoutName_) : std::string(), settings_.spoutInputFlip);
    OscOutConfig oscOut;
    oscOut.enabled = settings_.oscOutEnabled;
    oscOut.host = settings_.oscOutHost;
    oscOut.port = settings_.oscOutPort;
    oscOut.rate = settings_.oscOutRate;
    oscOut.spectrum = settings_.oscOutSpectrum;
    oscOut.inputPort = control.oscEnabled ? control.oscPort : 0;
    oscOut.version = NYREXVJ_VERSION;
    oscOut.params = settings_.oscOutParams;
    oscOut_.configure(oscOut);
    OscOutFrame oscFrame;
    oscFrame.audio = engine_.audio();
    oscFrame.bpm = engine_.comp.bpm;
    oscFrame.beat = engine_.beat();
    oscOut_.update(dt, oscFrame, control_.router());
    engine_.render();
    if (spout_.active()) {
        spout_.send(engine_.outputTexture(), engine_.width(), engine_.height());
    }
    output_.render(engine_.outputTexture(), engine_.width(), engine_.height());

    if (rescanModels_) {
        rescanModels_ = false;
        const int added = rescanLibrary();
        engine_.syncLibrary();
        assetsDirty_ = true;
        toast(added > 0 ? std::string(tr("New 3D models found: ")) + std::to_string(added) : std::string(tr("No new 3D models found")));
    }

    drawMenuBar();
    drawStatusBar();
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (needsLayout_) {
        buildLayout(dock);
        needsLayout_ = false;
    }
    handleShortcuts();
    drawDeck();
    drawOutput();
    drawPreview();
    drawMaster();
    drawLayer();
    drawClipPanel();
    drawBrowser();
    drawCreator();
    drawSettingsWindow();
    drawUpdateCard();
    drawWelcome();

    autosaveTimer_ += dt;
    if (autosaveTimer_ > 30.0) {
        autosaveTimer_ = 0.0;
        saveSession(engine_.comp);
    }
}

void App::buildLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID bottom = 0;
    ImGuiID top = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Up, 0.52f, nullptr, &bottom);
    ImGuiID rest = 0;
    ImGuiID monitors = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.25f, nullptr, &rest);
    ImGuiID preview = 0;
    ImGuiID output = ImGui::DockBuilderSplitNode(monitors, ImGuiDir_Up, 0.56f, nullptr, &preview);
    ImGuiID rest2 = 0;
    ImGuiID master = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.34f, nullptr, &rest2);
    ImGuiID browser = 0;
    ImGuiID clip = ImGui::DockBuilderSplitNode(rest2, ImGuiDir_Left, 0.42f, nullptr, &browser);
    ImGui::DockBuilderDockWindow("###deck", top);
    ImGui::DockBuilderDockWindow("###output", output);
    ImGui::DockBuilderDockWindow("###preview", preview);
    ImGui::DockBuilderDockWindow("###master", master);
    ImGui::DockBuilderDockWindow("###layer", master);
    ImGui::DockBuilderDockWindow("###clip", clip);
    ImGui::DockBuilderDockWindow("###browser", browser);
    ImGui::DockBuilderFinish(dock);
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    for (int i = 0; i < kColumnCount; ++i) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
            if (io.KeyCtrl) {
                switchDeckTo(i + (io.KeyShift ? 8 : 0));
            } else {
                engine_.triggerColumn(i);
            }
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) {
        openSettings(-1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
        switchDeckTo(nextUsedDeck(engine_.comp, engine_.comp.activeDeck, 1));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
        switchDeckTo(nextUsedDeck(engine_.comp, engine_.comp.activeDeck, -1));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        engine_.tap();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        engine_.blackout = !engine_.blackout;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
        if (output_.isOpen()) {
            output_.setFullscreen(!output_.isFullscreen(), outputMonitor_);
        } else {
            openOutputWindow(true);
        }
    }
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu(trId("File", "menuFile"))) {
        if (ImGui::MenuItem(tr("New empty composition"))) {
            loadComposition(emptyComposition(), tr("New composition"));
        }
        if (ImGui::MenuItem(tr("Save as preset..."))) {
            focusTab_ = 0;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Open data folder"))) {
            platform::openFolder(userDataDir());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Quit"))) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(trId("Output", "menuOutput"))) {
        if (ImGui::MenuItem(output_.isOpen() ? tr("Close output window") : tr("Open output window"))) {
            if (output_.isOpen()) {
                output_.close();
            } else {
                openOutputWindow(false);
            }
        }
        if (ImGui::MenuItem(tr("Fullscreen"), "F11", output_.isFullscreen(), output_.isOpen())) {
            output_.setFullscreen(!output_.isFullscreen(), outputMonitor_);
        }
        std::vector<std::string> monitors = OutputWindow::monitorNames();
        if (!monitors.empty() && ImGui::BeginMenu(tr("Display"))) {
            for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                if (ImGui::MenuItem(monitors[static_cast<size_t>(i)].c_str(), nullptr, i == outputMonitor_)) {
                    outputMonitor_ = i;
                    if (output_.isFullscreen()) {
                        output_.setFullscreen(true, outputMonitor_);
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        bool spoutActive = spout_.active();
        if (ImGui::MenuItem(tr("Send to OBS via Spout"), nullptr, spoutActive, SpoutOutput::supported())) {
            if (spoutActive) {
                spout_.stop();
            } else {
                spout_.start(spoutName_);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(trId("Audio", "menuAudio"))) {
        if (ImGui::MenuItem(tr("Off"), nullptr, audio_.selected().empty())) {
            audio_.select("");
        }
        if (ImGui::MenuItem(tr("Demo signal"), nullptr, audio_.selected() == AudioSystem::demoId())) {
            audio_.select(AudioSystem::demoId());
        }
        for (const AudioDevice& device : audio_.devices()) {
            std::string label = std::string(device.loopback ? tr("System audio: ") : tr("Input: ")) + device.name;
            if (ImGui::MenuItem(label.c_str(), nullptr, audio_.selected() == device.id)) {
                if (!audio_.select(device.id)) {
                    toast(trMessage(audio_.error()));
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Refresh devices"))) {
            audio_.refreshDevices();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(trId("Settings", "menuSettings"))) {
        openSettings(-1);
    }
    if (ImGui::BeginMenu(trId("Creator", "menuCreator"))) {
        if (ImGui::MenuItem(tr("Open Clip Creator"))) {
            openCreator();
        }
        if (ImGui::MenuItem(tr("New 3D scene"))) {
            openCreatorWith(std::string());
        }
        if (ImGui::BeginMenu(tr("Start from a template"))) {
            for (const BuiltinDocInfo& info : builtinDocs()) {
                if (ImGui::MenuItem(info.name)) {
                    openCreatorWith(info.name);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Open scenes folder"))) {
            platform::openFolder(clipsDirectory());
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(trId("Help", "menuHelp"))) {
        if (ImGui::MenuItem(tr("What's new"))) {
            openWelcome(false);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("NyrexVJ on GitHub"))) {
            platform::openUrl(ui::kRepoUrl);
        }
        if (ImGui::MenuItem(tr("Releases"))) {
            platform::openUrl(ui::kReleasesUrl);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(tr("Language"))) {
            for (int i = 0; i < kLanguageCount; ++i) {
                Language language = static_cast<Language>(i);
                if (ImGui::MenuItem(languageLabel(language), nullptr, currentLanguage() == language)) {
                    setLanguageAndSave(language);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("Check for updates"), nullptr, false, !updater_.busy())) {
            updater_.checkAsync();
            updateCardCollapsed_ = false;
            openSettings(SettingsUpdates);
        }
        ImGui::EndMenu();
    }

    if (!toastText_.empty() && ImGui::GetTime() < toastUntil_) {
        ImGui::TextDisabled("%s", toastText_.c_str());
    }
    if (engine_.blackout) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", tr("BLACKOUT"));
    }
    ImGui::EndMainMenuBar();
}

}

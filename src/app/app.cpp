#include "app/app.hpp"

#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/crash_handler.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "scene3d/builtin_docs.hpp"
#include "update/changelog.hpp"

#include <GLFW/glfw3.h>

#include <imgui_internal.h>

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <ctime>
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
    static App* dropTarget = nullptr;
    dropTarget = this;
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        if (dropTarget == nullptr || count <= 0 || paths == nullptr) {
            return;
        }
        std::vector<std::string> list;
        for (int i = 0; i < count; ++i) {
            if (paths[i] != nullptr) {
                list.emplace_back(paths[i]);
            }
        }
        dropTarget->queueDroppedFiles(list);
    });
    ImGuiIO& io = ImGui::GetIO();
    iniPath_ = (std::filesystem::path(userDataDir()) / "layout_v6.ini").string();
    io.IniFilename = iniPath_.c_str();
    needsLayout_ = !std::filesystem::exists(iniPath_);

    loadSettings(settings_);
    applyTheme(themeIndex(settings_.theme));
    outputs_.attach(&settings_.outputs);
    savedOutputs_ = settings_.outputs;
    uiScaleEdit_ = settings_.uiScale;
    appliedVsync_ = settings_.vsync ? 1 : 0;
    oscOverridePort_ = options.oscPort;
    control_.loadBindings();
    control_.refreshMidiDevices();
    setLanguage(languageFromCode(options.language.empty() ? settings_.language : options.language));

    {
        const std::filesystem::path dataDir = userDataDir();
        processId_ = platform::currentProcessId();
        previous_ = inspectPreviousSession(dataDir, processId_, platform::processAlive);
        if (!previous_.otherInstance) {
            ownsSession_ = beginSession(dataDir, processId_, NYREXVJ_VERSION, static_cast<long long>(std::time(nullptr)));
            installCrashHandler(crashReportPath(dataDir).string(), NYREXVJ_VERSION);
        }
        if (options.crashTest) {
            crashTestAt_ = 1.0;
        }
    }

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
    const bool recoverInstead = previous_.crashed && !options.fresh;
    if (!chosen && settings_.restoreSession && !options.fresh && options.clip.empty() && !recoverInstead) {
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

    if (recoverInstead) {
        const std::filesystem::path dataDir = userDataDir();
        recoveryEntries_ = listAutosaves(dataDir);
        recoveryCreator_ = hasCreatorRecovery(dataDir);
        recoveryRequested_ = true;
    }

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

    std::snprintf(ndiName_, sizeof(ndiName_), "%s", settings_.ndiName.c_str());
    if (!options.ndiName.empty()) {
        std::snprintf(ndiName_, sizeof(ndiName_), "%s", options.ndiName.c_str());
        if (!ndi_.start(ndiName_)) {
            logLine("NDI could not be started: " + ndi_.error());
        }
    } else if (settings_.ndiEnabled && NdiSender::available()) {
        ndi_.start(ndiName_);
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

    if (options.selectOutput >= 0 && options.selectOutput < kMaxOutputs) {
        selectedOutput_ = options.selectOutput;
        outputTabForce_ = 4;
    }
    if (options.openOutput || (settings_.openOutputOnStart && options.skipUpdateCheck == false)) {
        openOutputWindow(false);
    }
    if (options.outputPattern) {
        for (int i = 0; i < kMaxOutputs; ++i) {
            outputs_.setTestPattern(i, true);
        }
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
    if (!recoveryPending()) {
        saveSession(engine_.comp);
    }
    settings_.audioDevice = audio_.selected() == AudioSystem::demoId() ? std::string() : audio_.selected();
    settings_.spoutEnabled = spout_.active();
    settings_.spoutName = spoutName_;
    settings_.ndiEnabled = ndi_.active();
    settings_.ndiName = ndiName_;
    saveSettings(settings_);
    spout_.stop();
    ndi_.stop();
    oscOut_.stop();
    stage_.stop();
    audio_.select("");
    outputs_.closeAll();
    shutdownCreator();
    engine_.shutdown();
    if (ownsSession_) {
        if (!recoveryPending()) {
            clearCreatorRecovery(userDataDir());
        }
        endSession(userDataDir(), processId_);
    }
}

void App::autosaveTick(double dt) {
    if (recoveryPending() || !ownsSession_) {
        return;
    }
    autosaveTimer_ += dt;
    rotatingTimer_ += dt;
    if (autosaveTimer_ > 30.0) {
        autosaveTimer_ = 0.0;
        const std::string text = compositionToJson(engine_.comp);
        if (text != lastSessionText_ && saveSession(engine_.comp)) {
            lastSessionText_ = text;
        }
        const std::filesystem::path dataDir = userDataDir();
        if (creatorUnsaved()) {
            writeCreatorRecovery(dataDir, creatorDocJson());
        } else {
            clearCreatorRecovery(dataDir);
        }
        if (rotatingTimer_ > 120.0 && text != lastRotatingText_) {
            rotatingTimer_ = 0.0;
            if (writeRotatingAutosave(dataDir, text, static_cast<long long>(std::time(nullptr)))) {
                lastRotatingText_ = text;
            }
        }
    }
}

void App::queueDroppedFiles(const std::vector<std::string>& paths) {
    pendingImports_.insert(pendingImports_.end(), paths.begin(), paths.end());
}

void App::serviceImports() {
    if (importJob_.valid() && importJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const std::vector<ImportResult> results = importJob_.get();
        int added = 0;
        int existing = 0;
        int rejected = 0;
        std::string firstName;
        for (const ImportResult& result : results) {
            if (result.kind == ImportKind::Media || result.kind == ImportKind::Model || result.kind == ImportKind::Shader) {
                if (result.added) {
                    ++added;
                    if (firstName.empty()) {
                        firstName = result.name;
                    }
                } else {
                    ++existing;
                }
            } else {
                ++rejected;
            }
        }
        if (added > 0) {
            rescanLibrary();
            engine_.syncLibrary();
            assetsDirty_ = true;
        }
        std::string message;
        if (added == 1) {
            message = std::string(tr("Added: ")) + firstName;
        } else if (added > 1) {
            message = std::to_string(added) + " " + tr("files added");
        } else if (existing > 0) {
            message = tr("Already in the library");
        }
        if (rejected > 0) {
            if (!message.empty()) {
                message += "  |  ";
            }
            message += std::to_string(rejected) + " " + tr("files skipped (images, videos, 3D models up to 256 MiB and shader files up to 256 KiB only)");
        }
        if (!message.empty()) {
            toast(message);
        }
    }
    if (!importJob_.valid() && !pendingImports_.empty()) {
        std::vector<std::string> paths;
        paths.swap(pendingImports_);
        importJob_ = std::async(std::launch::async, [paths]() {
            std::vector<ImportResult> out;
            for (const std::string& path : paths) {
                out.push_back(importAsset(path));
            }
            return out;
        });
        toast(tr("Importing files..."));
    }
}

void App::toast(const std::string& message) {
    toastText_ = message;
    toastUntil_ = ImGui::GetTime() + 4.0;
}

void App::openOutputWindow(bool fullscreen) {
    if (outputs_.openEnabled(window_, fullscreen) == 0) {
        toast(tr("Could not open the output window"));
    }
}

int App::dumpOutputs(const std::string& prefix) {
    int written = 0;
    for (int i = 0; i < kMaxOutputs; ++i) {
        OutputCapture capture;
        if (!captureOutput(i, capture)) {
            continue;
        }
        stbi_flip_vertically_on_write(1);
        const std::string path = prefix + std::to_string(i + 1) + ".png";
        if (stbi_write_png(path.c_str(), capture.width, capture.height, 3, capture.rgb.data(), capture.width * 3) != 0) {
            logLine("Output " + std::to_string(i + 1) + " saved to " + path);
            ++written;
        }
    }
    return written;
}

bool App::captureOutput(int index, OutputCapture& out) {
    return outputs_.capture(index, engine_.outputTexture(), engine_.width(), engine_.height(), out);
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
    const auto frameStart = std::chrono::steady_clock::now();
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
    stage_.configure(settings_.dmx, settings_.vrchat);
    stage_.update(dt, [this](int id) { return engine_.sourceValue(id); });
    engine_.setDmxStrip(settings_.dmx.stripEnabled, settings_.dmx.stripAtTop, stage_.vrslGrid());
    engine_.render();
    if (spout_.active()) {
        spout_.send(engine_.outputTexture(), engine_.width(), engine_.height());
    }
    if (ndi_.active()) {
        ndi_.send(engine_.outputTexture(), engine_.width(), engine_.height());
    }
    outputs_.render(engine_.outputTexture(), engine_.width(), engine_.height());

    if (rescanModels_) {
        rescanModels_ = false;
        const int added = rescanLibrary();
        engine_.syncLibrary();
        assetsDirty_ = true;
        toast(added > 0 ? std::string(tr("New files found: ")) + std::to_string(added) : std::string(tr("No new files found")));
    }
    serviceImports();
    shaderPoll_ += dt;
    if (shaderPoll_ >= 1.0) {
        shaderPoll_ = 0.0;
        const ShaderScan scan = refreshShaderClips();
        if (scan.added > 0) {
            engine_.syncLibrary();
            assetsDirty_ = true;
        }
        if (!scan.changed.empty()) {
            engine_.reloadShaderClips(scan.changed);
        }
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
    creatorHasFocus_ = false;
    drawCreator();
    drawSettingsWindow();
    drawUpdateCard();
    drawWelcome();
    drawRecovery();

    history_.observe(engine_.comp, dt, ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive());

    autosaveTick(dt);
    if (crashTestAt_ > 0.0) {
        crashTestAt_ -= dt;
        if (crashTestAt_ <= 0.0) {
            std::raise(SIGSEGV);
        }
    }

    perfCpu_.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
    perfFrame_.add(dt * 1000.0);
    if (engine_.gpuTimingAvailable()) {
        perfGpu_.add(engine_.gpuFrameMs());
    }
    perfRefresh_ += dt;
    if (perfRefresh_ >= 0.25) {
        perfRefresh_ = 0.0;
        perfFrameSummary_ = perfFrame_.summary();
        perfCpuSummary_ = perfCpu_.summary();
        perfGpuSummary_ = perfGpu_.summary();
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
    if (io.KeyCtrl && !creatorHasFocus_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                redoEdit();
            } else {
                undoEdit();
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            redoEdit();
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
        outputs_.toggleFullscreen(window_);
    }
}

void App::undoEdit() {
    if (history_.undo(engine_.comp, static_cast<int>(clipLibrary().size()))) {
        toast(tr("Undone"));
    } else {
        toast(tr("Nothing to undo"));
    }
}

void App::redoEdit() {
    if (history_.redo(engine_.comp, static_cast<int>(clipLibrary().size()))) {
        toast(tr("Redone"));
    } else {
        toast(tr("Nothing to redo"));
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
    if (ImGui::BeginMenu(tr("Edit"))) {
        if (ImGui::MenuItem(tr("Undo"), "Ctrl+Z", false, history_.canUndo())) {
            undoEdit();
        }
        if (ImGui::MenuItem(tr("Redo"), "Ctrl+Y", false, history_.canRedo())) {
            redoEdit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(trId("Output", "menuOutput"))) {
        if (ImGui::MenuItem(outputs_.anyOpen() ? tr("Close output windows") : tr("Open output windows"))) {
            if (outputs_.anyOpen()) {
                outputs_.closeAll();
            } else {
                openOutputWindow(false);
            }
        }
        if (ImGui::MenuItem(tr("Fullscreen"), "F11", outputs_.anyFullscreen(), outputs_.anyOpen())) {
            outputs_.toggleFullscreen(window_);
        }
        if (ImGui::MenuItem(tr("Outputs and mapping..."))) {
            openSettings(SettingsOutput);
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
        const bool ndiActive = ndi_.active();
        if (ImGui::MenuItem(tr("Send via NDI"), nullptr, ndiActive)) {
            if (ndiActive) {
                ndi_.stop();
            } else if (!ndi_.start(ndiName_)) {
                toast(tr("NDI runtime not found. See Settings, Output."));
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

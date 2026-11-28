#include "app/app.hpp"

#include "app/fonts.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/crash_handler.hpp"
#include "core/modulation.hpp"
#include "core/log.hpp"
#include "core/node_files.hpp"
#include "core/preset.hpp"
#include "core/venue_setup.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "scene3d/builtin_docs.hpp"
#include "scene3d/scene_pack.hpp"
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
    iniPath_ = (std::filesystem::path(userDataDir()) / "layout_v8.ini").string();
    io.IniFilename = iniPath_.c_str();
    recoverInterfaceFiles(options);

    loadSettings(settings_);
    if (options.resetInterface) {
        settings_.uiScale = 1.0f;
        settings_.uiAutoScale = false;
        settings_.uiContrast = "standard";
        settings_.uiKeyboardNav = false;
        saveSettings(settings_);
    }
    loadShortcuts();
    if (settings_.design < 1) {
        settings_.design = 1;
        if (settings_.theme == "orange") {
            settings_.theme = "studio";
        }
        saveSettings(settings_);
    }
    if (settings_.design < 2) {
        settings_.design = 2;
        if (settings_.theme == "studio") {
            settings_.theme = "arena";
        }
        saveSettings(settings_);
    }
    applyTheme(themeIndex(settings_.theme));
    outputs_.attach(&settings_.outputs);
    savedOutputs_ = settings_.outputs;
    uiScaleEdit_ = settings_.uiScale;
    appliedVsync_ = settings_.vsync ? 1 : 0;
    oscOverridePort_ = options.oscPort;
    control_.loadBindings();
    loadLibraryMeta();
    sync_.loadCues();
    timeline_.load();
    control_.refreshMidiDevices();
    loadUserLanguages(userLanguageDirectory());
    setLanguage(languageFromCode(options.language.empty() ? settings_.language : options.language));
    fontCheckPending_ = true;

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
        recordPluginCrashes(dataDir, previous_.crashReport, static_cast<long long>(std::time(nullptr)));
        plugins_ = std::make_unique<PluginHost>(dataDir, !options.noPlugins);
        engine_.setPluginHost(plugins_.get());
    }

    setGeneratorClipsEnabled(settings_.generatorClips);
    engine_.setParticleLimit(settings_.particleLimit);
    engine_.setGpuParticles(settings_.gpuParticles, settings_.gpuParticleBudgetMb);
    initConversion();
    refreshNodeEffects();
    refreshIsfEffects();
    refreshCustomTransitions();
    if (!engine_.init()) {
        logLine("Engine initialisation failed");
        return false;
    }

    if (options.quality >= 0) {
        settings_.renderQuality = clampQualityMode(options.quality);
    }
    engine_.setQuality(settings_.renderQuality, budgetMilliseconds(settings_.perfBudgetFps));

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
    if (options.transition.rfind("Transition: ", 0) == 0) {
        start.deckSettings.customTransition = options.transition;
    } else if (!options.transition.empty()) {
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
    startFocusTrigger_ = options.triggerTab;
    if (options.selectLayer >= 0 && options.selectLayer < kLayerCount) {
        selectedLayer_ = options.selectLayer;
    }
    focusTab_ = options.tab;
    if (options.libraryMode >= 0) {
        libraryMode_ = std::clamp(options.libraryMode, 0, 2);
    }
    if (options.libraryKind >= 0) {
        libraryKind_ = std::clamp(options.libraryKind, 0, 10);
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

    std::snprintf(recordFolder_, sizeof(recordFolder_), "%s", settings_.recordFolder.c_str());
    audio_.setSink(&recorder_);

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

    if (!options.venue.empty()) {
        std::array<OutputSpec, kMaxOutputs> set;
        std::string name;
        std::string error;
        std::error_code ec;
        const std::filesystem::path asPath = pathFromUtf8(options.venue);
        const bool loaded = std::filesystem::is_regular_file(asPath, ec) ? loadVenueFile(asPath, name, set, error) : loadVenue(venuesDirectory(), options.venue, set, error);
        if (loaded) {
            applyVenue(set);
            logLine("Venue setup loaded: " + options.venue);
        } else {
            logLine("Venue setup could not be loaded: " + error);
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
    if (options.openNodes) {
        openNodeEditor(options.nodeEffect);
    }
    if (options.openTimeline) {
        timelineOpen_ = true;
        timelineFocus_ = true;
    }
    if (options.openManual) {
        openManual(options.manualChapter);
    }
    if (options.showPackageTab >= 0) {
        openShowPackages(options.showPackageTab);
    }
    if (options.gpuParticles >= 0) {
        settings_.gpuParticles = options.gpuParticles != 0;
        engine_.setGpuParticles(settings_.gpuParticles, settings_.gpuParticleBudgetMb);
    }
    if (options.generatorTab >= 0) {
        openGeneratorStudio(options.generatorTab, options.generatorSelect);
    }
    if (options.shaderEditor) {
        openShaderEditor(options.shaderEditorFile);
    }
    if (options.chainTarget >= 0) {
        openChainLibrary(options.chainTarget);
        chainDialog_.selected = options.chainSelect;
    }
    if (options.packageTab >= 0) {
        openCommunityPackages(options.packageTab);
        if (!options.packageFile.empty()) {
            importCommunityPackage(options.packageFile);
        }
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

void App::updatePlugins() {
    if (!plugins_) {
        return;
    }
    const AudioFeatures& audio = engine_.audio();
    PluginHostSnapshot snapshot;
    snapshot.bass = audio.bass;
    snapshot.mid = audio.mid;
    snapshot.high = audio.high;
    snapshot.level = audio.level;
    snapshot.kick = audio.kick;
    snapshot.beat = engine_.beat();
    snapshot.bpm = engine_.comp.bpm;
    snapshot.beatsPerBar = static_cast<unsigned>(clampBeatsPerBar(engine_.comp.beatsPerBar));
    plugins_->publish(snapshot);
    plugins_->update();
}

void App::shutdown() {
    if (shutDown_) {
        return;
    }
    shutDown_ = true;
    failover_.shutdown();
    netSync_.shutdown(steadyNowSeconds());
    api_.shutdown();
    if (!recoveryPending()) {
        saveSession(engine_.comp);
    }
    settings_.audioDevice = audio_.selected() == AudioSystem::demoId() ? std::string() : audio_.selected();
    settings_.spoutEnabled = spout_.active();
    settings_.spoutName = spoutName_;
    settings_.ndiEnabled = ndi_.active();
    settings_.ndiName = ndiName_;
    settings_.recordFolder = recordFolder_;
    saveSettings(settings_);
    audio_.setSink(nullptr);
    recorder_.shutdown(10.0);
    if (recordProbe_.valid()) {
        recordProbe_.wait();
    }
    spout_.stop();
    ndi_.stop();
    oscOut_.stop();
    stage_.stop();
    audio_.select("");
    outputs_.closeAll();
    shutdownCreator();
    engine_.shutdown();
    shutdownConversion();
    if (plugins_) {
        plugins_->stopAll();
        plugins_->waitForThreads(plugins_->abandonedInstances() > 0 ? 0.1 : 1.0);
    }
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
            snapshotAt_ = steadyNowSeconds();
        } else if (text == lastSessionText_) {
            snapshotAt_ = steadyNowSeconds();
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

void App::syncNodeEffects(const std::string& forceName) {
    NodeScan scan = refreshNodeEffects(forceName);
    const TransitionScan transitions = refreshCustomTransitions();
    if (!transitions.changed.empty()) {
        assetsDirty_ = true;
    }
    const IsfScan isf = refreshIsfEffects();
    scan.added += isf.added;
    scan.changed.insert(scan.changed.end(), isf.changed.begin(), isf.changed.end());
    if (scan.added > 0) {
        engine_.syncEffects();
        assetsDirty_ = true;
    }
    if (!scan.changed.empty()) {
        engine_.reloadNodeEffects(scan.changed);
        assetsDirty_ = true;
    }
}

void App::queueDroppedFiles(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        std::string extension = pathFromUtf8(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".nyxshow") {
            importShowPackage(path);
        } else if (extension == ".nyxpack") {
            importCommunityPackage(path);
        } else if (extension == ".json") {
            importDroppedVenue(path);
        } else if (extension == ".nyxfx") {
            importNodeGraph(path);
        } else if (extension == ".avc") {
            openResoPort(path);
        } else if (extension == ".nyxlayout") {
            importWorkspaceFile(path);
        } else {
            pendingImports_.push_back(path);
        }
    }
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
            convertScanPending_ = true;
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

void App::loadFromApi(const Composition& comp, const std::string& name) {
    loadComposition(comp, std::string(tr("Loaded ")) + name);
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

void App::applyPendingFonts() {
    if (!fontCheckPending_) {
        return;
    }
    fontCheckPending_ = false;
    if (!languageNeedsCjkFont(currentLanguage()) || cjkTried_) {
        return;
    }
    cjkTried_ = true;
    const std::string path = findCjkFontPath();
    if (!path.empty() && mergeCjkFont(path)) {
        setNativeLabelsAvailable(true);
        logLine("Japanese font merged: " + path);
    } else {
        logLine("No Japanese system font found");
    }
}

void App::setLanguageAndSave(Language language) {
    setLanguage(language);
    fontCheckPending_ = true;
    settings_.language = languageCode(language);
    saveSettings(settings_);
}

void App::updateAudio(double dt) {
    audio_.setGain(settings_.audioGain);
    audio_.setSmoothing(settings_.audioSmoothing);
    audio_.setBeatSensitivity(settings_.audioBeatSensitivity);
    audio_.setImprovedTracking(settings_.audioTracking == "improved");
    audio_.setCrossovers(settings_.audioCrossLow, settings_.audioCrossHigh);
    audio_.setLatency(static_cast<double>(settings_.audioLatencyMs) * 0.001);
    audio_.setChannel(audioChannelFromName(settings_.audioChannel.c_str()));
    audio_.update(dt, engine_.comp.bpm);
    const AudioFeatures& features = audio_.features();
    engine_.setAudio(features);
    const SyncMode syncMode = netSync_.effectiveSyncMode(syncModeFromName(settings_.syncMode.c_str()));
    const bool audioFollow = settings_.audioSyncBpm || syncMode == SyncMode::Audio;
    if (audioFollow && features.active) {
        SyncObservation presence;
        presence.source = SyncSource::Audio;
        engine_.beatClock().report(presence);
    }
    const SyncSource activeSource = engine_.beatClock().active();
    const bool audioDrives = syncMode == SyncMode::Manual || activeSource == SyncSource::Audio || activeSource == SyncSource::Manual;
    if (audioFollow && features.active && audioDrives) {
        if (features.bpm > 40.0f && features.bpmConfidence > 0.55f) {
            float blended = engine_.comp.bpm + (features.bpm - engine_.comp.bpm) * 0.08f;
            engine_.comp.bpm = std::clamp(blended, 40.0f, 240.0f);
        }
        if (features.beatNow) {
            engine_.nudgeBeat(0.35f);
        }
    }
}

void App::updateSync(double dt) {
    engine_.setQuantizeGrid(quantizeGridFromName(settings_.launchQuantize.c_str()));
    SyncConfig config;
    config.mode = netSync_.effectiveSyncMode(syncModeFromName(settings_.syncMode.c_str()));
    config.latencyMs = settings_.syncLatencyMs;
    config.barAlign = settings_.syncBarAlign;
    config.djLink = settings_.djLinkEnabled;
    config.djPlayer = settings_.djLinkPlayer;
    config.ltcDevice = settings_.ltcDevice;
    config.cues = settings_.cuesEnabled && !netSync_.suppressesAutomation();
    sync_.configure(config);
    std::vector<MidiMessage> midi;
    control_.takeSyncMessages(midi);
    sync_.update(dt, midi, engine_.beatClock());
    engine_.setTimecode(sync_.timecodeActive(), sync_.timecodeSeconds());
    for (const Cue& cue : sync_.firedCues()) {
        control_.fireTarget(cue.target, cue.value);
    }
    netSync_.noteCues(sync_.firedCues().size());
}

void App::frame(double dt, bool drawUi) {
    const auto frameStart = std::chrono::steady_clock::now();
    const int vsync = settings_.vsync ? 1 : 0;
    if (vsync != appliedVsync_) {
        glfwSwapInterval(vsync);
        appliedVsync_ = vsync;
    }
    applyPendingWorkspace();
    updateInterfaceStyle();
    ControlConfig control;
    control.oscEnabled = settings_.oscEnabled || oscOverridePort_ > 0;
    control.oscPort = oscOverridePort_ > 0 ? oscOverridePort_ : settings_.oscPort;
    control.oscAllowRemote = settings_.oscAllowRemote;
    control.midiDevice = settings_.midiDevice;
    control.midiFeedback = settings_.midiFeedback;
    control.midiOutDevice = settings_.midiOutDevice;
    control_.configure(control);
    control_.update(dt);
    ApiConfig api;
    api.enabled = settings_.apiEnabled;
    api.port = settings_.apiPort;
    api.allowRemote = settings_.apiAllowRemote;
    api_.configure(api);
    api_.update(dt, ImGui::GetIO().Framerate);
    NetSyncSettings netSync;
    netSync.role = netsync::roleFromName(settings_.netSyncRole);
    netSync.port = settings_.netSyncPort;
    netSync.allowRemote = settings_.netSyncAllowRemote;
    netSync.host = settings_.netSyncHost;
    netSync.name = settings_.netSyncName;
    netSync.follow.controls = settings_.netSyncFollowControls;
    netSync.follow.clips = settings_.netSyncFollowClips;
    netSync.follow.clock = settings_.netSyncFollowClock;
    netSync_.configure(netSync);
    FailoverSettings failoverSettings;
    failoverSettings.enabled = settings_.failoverEnabled;
    failoverSettings.autoTakeover = settings_.failoverAutoTakeover;
    failoverSettings.lostAfter = settings_.failoverLostAfter;
    failoverSettings.holdOutputs = settings_.failoverHoldOutputs;
    failoverSettings.leaderPort = settings_.failoverLeaderPort;
    failover_.configure(failoverSettings);
    FailoverEnv failoverEnv;
    for (const OutputSpec& spec : outputs_.specs()) {
        failoverEnv.outputsWanted += spec.enabled ? 1 : 0;
    }
    failoverEnv.outputsOpen = outputs_.openCount();
    failoverEnv.snapshotAge = snapshotAt_ >= 0.0 ? std::max(steadyNowSeconds() - snapshotAt_, 0.0) : -1.0;
    failover_.update(steadyNowSeconds(), dt, failoverEnv);
    netSync_.update(dt, steadyNowSeconds());
    updateSync(dt);
    updateTimeline(dt);
    updateAudio(dt);
    serviceLiveBrowse();
    engine_.setVideoSettings(videoDecodeModeFromName(settings_.videoDecoder), settings_.hapDirect, settings_.videoGpuConvert);
    applyColorSettings();
    {
        flow::Settings flowSettings;
        flowSettings.mode = flow::modeFromName(settings_.flowMode, flow::Mode::Off);
        flowSettings.tier = flow::tierFromName(settings_.flowQuality, flow::Tier::Balanced);
        flowSettings.budgetMb = settings_.flowBudgetMb;
        flowSettings.shutter = static_cast<float>(settings_.flowShutterPercent) / 100.0f;
        engine_.setFlowSettings(flowSettings);
    }
    {
        person::Settings personSettings;
        personSettings.mode = person::modeFromName(settings_.personMode, person::Mode::Off);
        person::parseInput(settings_.personInput, personSettings.input, personSettings.slot);
        personSettings.tier = person::tierFromName(settings_.personQuality, person::Tier::Balanced);
        personSettings.rate = settings_.personRate;
        personSettings.sensitivity = settings_.personSensitivity;
        personSettings.softness = settings_.personSoftness;
        personSettings.stability = settings_.personStability;
        engine_.setPersonSettings(personSettings);
    }
    engine_.prewarmNext = settings_.prewarmNext;
    engine_.setQuality(settings_.renderQuality, budgetMilliseconds(settings_.perfBudgetFps));
    MemoryPolicy memoryPolicy;
    memoryPolicy.budgetBytes = static_cast<std::uint64_t>(std::max(settings_.gpuMemoryBudgetMb, 0)) * gpudiag::kMiB;
    memoryPolicy.trim = settings_.gpuMemoryTrim;
    memoryPolicy.lowerQuality = settings_.gpuMemoryLowerQuality;
    engine_.setMemoryPolicy(memoryPolicy);
    const unsigned long long feedbackBudget = static_cast<unsigned long long>(std::max(settings_.feedbackBudgetMb, 0)) * 1024ull * 1024ull;
    if (engine_.feedbackBudget() != feedbackBudget) {
        engine_.setFeedbackBudget(feedbackBudget);
    }
    engine_.setFeedbackReuse(settings_.feedbackReuse);
    engine_.setGraphOptimizer(settings_.graphOptimizer);
    engine_.setSceneMaterialBindings(settings_.sceneMaterialBindings);
    engine_.setSceneCameraMoves(settings_.sceneCameraMoves);
    engine_.update(dt);
    netSync_.afterEngineUpdate(steadyNowSeconds(), dt);
    updatePlugins();
    engine_.configureSpout(settings_.spoutInputs, spout_.active() ? std::string(spoutName_) : std::string(), settings_.spoutInputFlip);
    CameraMode cameraMode;
    cameraMode.width = settings_.cameraWidth;
    cameraMode.height = settings_.cameraHeight;
    cameraMode.fps = settings_.cameraFps;
    std::array<std::string, kLiveInputSlots> streams;
    if (settings_.streamInputEnabled) {
        streams = settings_.streamInputs;
    }
    engine_.configureLive(settings_.ndiInputs, settings_.cameraInputs, settings_.ndiEnabled ? settings_.ndiName : std::string(), cameraMode, streams, stream::profileFromInt(settings_.streamProfile));
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
    std::vector<std::int32_t> pings;
    control_.takePings(pings);
    for (const std::int32_t id : pings) {
        oscOut_.queuePong(id);
    }
    oscOut_.update(dt, oscFrame, control_.router());
    stage_.configure(settings_.dmx, settings_.vrchat);
    stage_.update(dt, [this](int id) { return engine_.sourceValue(id); });
    engine_.setDmxStrip(settings_.dmx.stripEnabled, settings_.dmx.stripAtTop, stage_.vrslGrid());
    applySenderSources();
    engine_.render();
    if (spout_.active()) {
        spout_.send(engine_.routedTexture(routing::SenderKind::Spout), engine_.width(), engine_.height());
    }
    if (ndi_.active()) {
        ndi_.send(engine_.routedTexture(routing::SenderKind::Ndi), engine_.width(), engine_.height());
    }
    if (recorder_.recording()) {
        recorder_.capture(engine_.routedTexture(routing::SenderKind::Recording), engine_.width(), engine_.height(), steadyNowSeconds());
    }
    serviceRecording();
    outputs_.setFrameLock(settings_.outputFrameLock);
    outputs_.render(engine_.outputTexture(), engine_.width(), engine_.height());

    if (rescanModels_) {
        rescanModels_ = false;
        const int added = rescanLibrary();
        engine_.syncLibrary();
        assetsDirty_ = true;
        convertScanPending_ = true;
        toast(added > 0 ? std::string(tr("New files found: ")) + std::to_string(added) : std::string(tr("No new files found")));
    }
    serviceImports();
    serviceConversion();
    shaderPoll_ += dt;
    if (shaderPoll_ >= 1.0) {
        shaderPoll_ = 0.0;
        const ShaderScan scan = refreshShaderClips();
        const ShaderScan generatorScan = refreshGeneratorClips();
        if (scan.added > 0 || generatorScan.added > 0) {
            engine_.syncLibrary();
            assetsDirty_ = true;
        }
        if (!scan.changed.empty()) {
            engine_.reloadShaderClips(scan.changed);
        }
        if (!generatorScan.changed.empty()) {
            engine_.reloadGeneratorClips(generatorScan.changed);
            assetsDirty_ = true;
        }
        syncNodeEffects();
    }

    if (!drawUi) {
        autosaveTick(dt);
        return;
    }

    drawMenuBar();
    drawStatusBar();
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    rootDock_ = dock;
    if (needsLayout_) {
        buildLayout(dock);
        needsLayout_ = false;
    }
    handleShortcuts();
    drawDeck();
    drawOutput();
    drawPreview();
    drawClipPanel();
    drawMaster();
    drawLayer();
    drawAudioWindow();
    drawBrowser();
    drawTrigger();
    drawStripMaster();
    drawStripAudio();
    drawStripNetwork();
    drawStripMidi();
    drawStripOsc();
    creatorHasFocus_ = false;
    drawCreator();
    drawNodeEditor();
    drawTimeline();
    drawManual();
    drawShowPackages();
    drawResoPort();
    drawCommunityPackages();
    drawGeneratorStudio();
    drawShaderEditor();
    drawRoutingWindow();
    drawFailoverWindow();
    drawChainLibrary();
    drawSettingsWindow();
    drawUpdateCard();
    drawConversionCards();
    drawWelcome();
    drawRecovery();

    if (tabFocusFrames_ > 0 && --tabFocusFrames_ == 0) {
        ImGui::SetWindowFocus("###clip");
        ImGui::SetWindowFocus("###layer");
        ImGui::SetWindowFocus("###browser");
        if (startFocusTrigger_) {
            ImGui::SetWindowFocus("###trigger");
        }
    }

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
    diagMonitor_->addFrame(dt * 1000.0);
    if (engine_.gpuTimingAvailable()) {
        perfGpu_.add(engine_.gpuFrameMs());
    }
    perfRefresh_ += dt;
    if (perfRefresh_ >= 0.25) {
        gpudiag::Thresholds thresholds;
        thresholds.p99Factor = static_cast<double>(settings_.diagP99Percent) / 100.0;
        thresholds.droppedPerMinute = settings_.diagDroppedPerMinute;
        thresholds.memoryFraction = static_cast<double>(settings_.diagMemoryPercent) / 100.0;
        diagMonitor_->setThresholds(thresholds);
        const Diagnostics& diagnostics = engine_.diagnostics();
        gpudiag::Sample sample;
        sample.budgetMs = budgetMilliseconds(settings_.perfBudgetFps);
        sample.estimateBytes = diagnostics.estimateBytes;
        sample.budgetBytes = diagnostics.budgetBytes;
        sample.residentBytes = diagnostics.residentBytes;
        sample.driver = diagnostics.driver;
        sample.uploadBytesPerSecond = diagnostics.uploadBytesPerSecond;
        diagMonitor_->update(perfRefresh_, sample);
        perfRefresh_ = 0.0;
        perfFrameSummary_ = perfFrame_.summary();
        perfCpuSummary_ = perfCpu_.summary();
        perfGpuSummary_ = perfGpu_.summary();
    }
}

void App::buildLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, static_cast<ImGuiViewportP*>(ImGui::GetMainViewport())->GetBuildWorkRect().GetSize());
    ImGuiID lower = 0;
    ImGuiID upper = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Up, 0.52f, nullptr, &lower);
    ImGuiID rest = 0;
    ImGuiID monitors = ImGui::DockBuilderSplitNode(lower, ImGuiDir_Left, 0.31f, nullptr, &rest);
    ImGuiID preview = 0;
    ImGuiID output = ImGui::DockBuilderSplitNode(monitors, ImGuiDir_Up, 0.58f, nullptr, &preview);
    ImGuiID rest2 = 0;
    ImGuiID layer = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.35f, nullptr, &rest2);
    ImGuiID browser = 0;
    ImGuiID clip = ImGui::DockBuilderSplitNode(rest2, ImGuiDir_Left, 0.51f, nullptr, &browser);

    ImGui::DockBuilderDockWindow("###deck", upper);
    ImGui::DockBuilderDockWindow("###output", output);
    ImGui::DockBuilderDockWindow("###preview", preview);
    ImGui::DockBuilderDockWindow("###master", layer);
    ImGui::DockBuilderDockWindow("###layer", layer);
    ImGui::DockBuilderDockWindow("###clip", clip);
    ImGui::DockBuilderDockWindow("###trigger", clip);
    ImGui::DockBuilderDockWindow("###browser", browser);
    ImGui::DockBuilderDockWindow("###audio", browser);
    ImGui::DockBuilderDockWindow("###stripMaster", browser);
    ImGui::DockBuilderDockWindow("###stripAudio", browser);
    ImGui::DockBuilderDockWindow("###stripNetwork", browser);
    ImGui::DockBuilderDockWindow("###stripMidi", browser);
    ImGui::DockBuilderDockWindow("###stripOsc", browser);
    tabFocusFrames_ = 3;
    ImGui::DockBuilderFinish(dock);
}

namespace {

void scaleDockSizeRef(ImGuiDockNode* node, int axis, float factor) {
    if (node == nullptr) {
        return;
    }
    node->SizeRef[axis] *= factor;
    scaleDockSizeRef(node->ChildNodes[0], axis, factor);
    scaleDockSizeRef(node->ChildNodes[1], axis, factor);
}

void releaseDockedWindow(const ImGuiWindow* window) {
    ImGuiDockNode* node = window ? window->DockNode : nullptr;
    if (node == nullptr || !node->IsLeafNode() || node->Windows.Size != 1 || node->ParentNode == nullptr) {
        return;
    }
    ImGuiDockNode* parent = node->ParentNode;
    ImGuiDockNode* sibling = parent->ChildNodes[0] == node ? parent->ChildNodes[1] : parent->ChildNodes[0];
    const int axis = parent->SplitAxis;
    if (sibling == nullptr || (axis != 0 && axis != 1) || sibling->Size[axis] < 1.0f || parent->Size[axis] < 1.0f) {
        return;
    }
    scaleDockSizeRef(sibling, axis, parent->Size[axis] / sibling->Size[axis]);
}

}

void App::applyTimelineDock() {
    const ImGuiWindow* existing = ImGui::FindWindowByName("###timeline");
    const bool wasActive = existing != nullptr && existing->LastFrameActive + 1 >= ImGui::GetFrameCount();
    if (!timelineOpen_) {
        if (wasActive) {
            releaseDockedWindow(existing);
        }
        timelineDockRequest_ = 0;
        return;
    }
    const int request = timelineDockRequest_;
    if (request == 0 || rootDock_ == 0) {
        timelineDockRequest_ = 0;
        return;
    }
    if (request != 3 && !wasActive) {
        return;
    }
    timelineDockRequest_ = 0;
    releaseDockedWindow(existing);
    if (request == 3) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::min(1180.0f, viewport->WorkSize.x * 0.9f), std::min(560.0f, viewport->WorkSize.y * 0.7f)), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.62f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        return;
    }
    const int axis = request == 1 ? 1 : 0;
    const ImGuiDockNode* rootNode = ImGui::DockBuilderGetNode(rootDock_);
    const float before = rootNode ? rootNode->Size[axis] : 0.0f;
    ImGuiID rest = 0;
    ImGuiID target = ImGui::DockBuilderSplitNode(rootDock_, request == 1 ? ImGuiDir_Down : ImGuiDir_Right, 0.34f, nullptr, &rest);
    ImGuiDockNode* restNode = ImGui::DockBuilderGetNode(rest);
    if (restNode && before > 1.0f && restNode->Size[axis] > 1.0f) {
        const float factor = restNode->Size[axis] / before;
        restNode->SizeRef[axis] = restNode->Size[axis];
        scaleDockSizeRef(restNode->ChildNodes[0], axis, factor);
        scaleDockSizeRef(restNode->ChildNodes[1], axis, factor);
    }
    ImGui::DockBuilderFinish(rootDock_);
    ImGui::SetNextWindowDockID(target, ImGuiCond_Always);
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || shortcutCapture_ >= 0) {
        return;
    }
    for (int i = 0; i < kColumnCount; ++i) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
            if (io.KeyCtrl) {
                switchDeckTo(i + (io.KeyShift ? 8 : 0));
            } else {
                engine_.requestTriggerColumn(i);
            }
        }
    }
    using shortcuts::Action;
    if (!creatorHasFocus_ && !nodesHasFocus_ && !timelineHasFocus_) {
        if (shortcutPressed(Action::Undo)) {
            undoEdit();
        } else if (shortcutPressed(Action::Redo)) {
            redoEdit();
        } else if (io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            redoEdit();
        }
    }
    if (shortcutPressed(Action::Settings)) {
        openSettings(-1);
    }
    if (shortcutPressed(Action::Record)) {
        if (recorder_.active()) {
            stopRecording();
        } else {
            startRecording();
        }
    }
    if (shortcutPressed(Action::NextDeck)) {
        switchDeckTo(nextUsedDeck(engine_.comp, engine_.comp.activeDeck, 1));
    }
    if (shortcutPressed(Action::PreviousDeck)) {
        switchDeckTo(nextUsedDeck(engine_.comp, engine_.comp.activeDeck, -1));
    }
    if (!timelineHasFocus_ && shortcutPressed(Action::Tap)) {
        engine_.tap();
    }
    if (shortcutPressed(Action::Blackout)) {
        engine_.blackout = !engine_.blackout;
    }
    if (shortcutPressed(Action::Fullscreen)) {
        outputs_.toggleFullscreen(window_);
    }
    if (shortcutPressed(Action::Manual)) {
        if (manualOpen_) {
            manualOpen_ = false;
        } else {
            openManual(std::string());
        }
    }
    if (shortcutPressed(Action::InterfaceLarger)) {
        changeInterfaceScale(1);
    } else if (shortcutPressed(Action::InterfaceSmaller)) {
        changeInterfaceScale(-1);
    } else if (shortcutPressed(Action::InterfaceReset)) {
        changeInterfaceScale(0);
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
        if (ImGui::MenuItem(tr("Export show package..."))) {
            openShowPackages(0);
        }
        if (ImGui::MenuItem(tr("Import show package..."))) {
            openShowPackages(1);
        }
        if (ImGui::MenuItem(tr("Import Resolume composition (ResoPort)..."))) {
            openResoPort(std::string());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Install community package..."))) {
            openCommunityPackages(1);
        }
        if (ImGui::MenuItem(tr("Community packages..."))) {
            openCommunityPackages(0);
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
        if (ImGui::MenuItem(tr("Undo"), shortcuts::format(shortcuts_.get(shortcuts::Action::Undo)).c_str(), false, history_.canUndo())) {
            undoEdit();
        }
        if (ImGui::MenuItem(tr("Redo"), shortcuts::format(shortcuts_.get(shortcuts::Action::Redo)).c_str(), false, history_.canRedo())) {
            redoEdit();
        }
        ImGui::EndMenu();
    }
    drawViewMenu();
    if (ImGui::BeginMenu(trId("Output", "menuOutput"))) {
        if (ImGui::MenuItem(outputs_.anyOpen() ? tr("Close output windows") : tr("Open output windows"))) {
            if (outputs_.anyOpen()) {
                outputs_.closeAll();
            } else {
                openOutputWindow(false);
            }
        }
        if (ImGui::MenuItem(tr("Fullscreen"), shortcuts::format(shortcuts_.get(shortcuts::Action::Fullscreen)).c_str(), outputs_.anyFullscreen(), outputs_.anyOpen())) {
            outputs_.toggleFullscreen(window_);
        }
        if (ImGui::MenuItem(tr("Outputs and mapping..."))) {
            openSettings(SettingsOutput);
        }
        if (ImGui::MenuItem(tr("Video routing..."), nullptr, routingOpen_)) {
            routingOpen_ = !routingOpen_;
        }
        if (ImGui::MenuItem(tr("Hot standby..."), nullptr, failoverOpen_)) {
            failoverOpen_ = !failoverOpen_;
        }
        ImGui::Separator();
        if (recorder_.active()) {
            if (ImGui::MenuItem(tr("Stop recording"), shortcuts::format(shortcuts_.get(shortcuts::Action::Record)).c_str())) {
                stopRecording();
            }
        } else if (ImGui::MenuItem(tr("Start recording"), shortcuts::format(shortcuts_.get(shortcuts::Action::Record)).c_str())) {
            startRecording();
        }
        if (ImGui::MenuItem(tr("Recording settings..."))) {
            openSettings(SettingsRecording);
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
    if (ImGui::BeginMenu(trId("Timeline", "menuTimeline"))) {
        if (ImGui::MenuItem(tr("Run the timeline"), nullptr, settings_.timelineEnabled)) {
            settings_.timelineEnabled = !settings_.timelineEnabled;
            saveSettings(settings_);
        }
        if (ImGui::MenuItem(tr("Show timeline window"), nullptr, timelineOpen_)) {
            timelineOpen_ = !timelineOpen_;
            timelineFocus_ = timelineOpen_;
        }
        if (ImGui::BeginMenu(tr("Place the timeline"))) {
            if (ImGui::MenuItem(tr("Dock at the bottom"))) {
                timelineOpen_ = true;
                timelineFocus_ = true;
                timelineDockRequest_ = 1;
            }
            if (ImGui::MenuItem(tr("Dock on the right"))) {
                timelineOpen_ = true;
                timelineFocus_ = true;
                timelineDockRequest_ = 2;
            }
            if (ImGui::MenuItem(tr("Float as a window"))) {
                timelineOpen_ = true;
                timelineFocus_ = true;
                timelineDockRequest_ = 3;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const bool canDrive = settings_.timelineEnabled;
        const bool playingNow = timeline_.runner().state() == TimelineState::Playing;
        if (ImGui::MenuItem(playingNow ? tr("Pause the timeline") : tr("Play the timeline"), nullptr, false, canDrive)) {
            timeline_.runner().toggle();
        }
        if (ImGui::MenuItem(tr("Stop the timeline"), nullptr, false, canDrive)) {
            timeline_.runner().stop();
        }
        if (ImGui::MenuItem(tr("Release hand moves"), nullptr, false, canDrive && timeline_.runner().overriddenCount() > 0)) {
            timeline_.runner().releaseAll();
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
        if (ImGui::MenuItem(tr("Shader editor..."), nullptr, shaderEditor_.open)) {
            if (shaderEditor_.open) {
                shaderEditor_.open = false;
            } else {
                openShaderEditor(std::string());
            }
        }
        if (ImGui::MenuItem(tr("Text and particles..."), nullptr, generatorDialog_.open)) {
            if (generatorDialog_.open) {
                generatorDialog_.open = false;
            } else {
                openGeneratorStudio(-1, std::string());
            }
        }
        if (ImGui::MenuItem(tr("Effect chains..."), nullptr, chainDialog_.open)) {
            if (chainDialog_.open) {
                chainDialog_.open = false;
            } else {
                openChainLibrary(selectedLayer_);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Open scenes folder"))) {
            platform::openFolder(clipsDirectory());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Node effect editor"), nullptr, nodesOpen_)) {
            if (nodesOpen_) {
                nodesOpen_ = false;
            } else {
                openNodeEditor(std::string());
            }
        }
        if (ImGui::MenuItem(tr("Open node effects folder"))) {
            platform::openFolder(graphsFolder().string());
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(trId("Help", "menuHelp"))) {
        if (ImGui::MenuItem(tr("User manual"), shortcuts::format(shortcuts_.get(shortcuts::Action::Manual)).c_str(), manualOpen_)) {
            if (manualOpen_) {
                manualOpen_ = false;
            } else {
                openManual(std::string());
            }
        }
        ImGui::Separator();
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
            for (int i = 0; i < languageCount(); ++i) {
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

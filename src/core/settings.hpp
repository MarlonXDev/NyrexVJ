#pragma once

#include "core/color_model.hpp"
#include "core/output_map.hpp"
#include "core/stage.hpp"
#include "update/update_logic.hpp"

#include <array>
#include <string>
#include <vector>

namespace nvj {

constexpr int kSpoutInputSlots = 4;
constexpr int kLiveInputSlots = 4;

struct OscOutParam {
    std::string name;
    std::string target;
};

struct Settings {
    bool checkUpdates = true;
    bool restoreSession = true;
    bool autoInstall = true;
    bool vsync = true;
    int fpsLimit = 60;
    std::string videoDecoder = "auto";
    bool hapDirect = true;
    bool videoGpuConvert = true;
    bool prewarmNext = true;
    bool generatorClips = true;
    int particleLimit = 2048;
    bool gpuParticles = false;
    int gpuParticleBudgetMb = 64;
    std::string convertMode = "ask";
    std::string convertFormat = "auto";
    int convertCacheMb = 4096;
    bool ffmpegHintDismissed = false;
    bool openOutputOnStart = false;
    bool showStatusBar = true;
    int perfBudgetFps = 60;
    int gpuMemoryBudgetMb = 0;
    bool gpuMemoryTrim = true;
    bool gpuMemoryLowerQuality = false;
    bool diagWarnings = true;
    int diagP99Percent = 150;
    int diagDroppedPerMinute = 5;
    int diagMemoryPercent = 85;
    int renderQuality = 0;
    bool showTimings = true;
    float uiScale = 1.0f;
    bool uiAutoScale = false;
    std::string uiContrast = "standard";
    bool uiKeyboardNav = false;
    std::string flowMode = "off";
    std::string flowQuality = "balanced";
    int flowBudgetMb = 256;
    int flowShutterPercent = 50;
    bool colorStage = false;
    std::string colorLut;
    float colorLutStrength = 1.0f;
    bool hdrToSdr = false;
    int hdrWhiteNits = 203;
    int hdrPeakNits = 1000;
    std::string hdrTone = "shoulder";
    UpdateChannel channel = UpdateChannel::Nightly;
    std::array<OutputSpec, kMaxOutputs> outputs = defaultOutputs();
    std::string language = "en";
    std::string theme = "studio";
    int design = 1;
    std::string lastSeenVersion;
    std::string audioDevice;
    float audioGain = 1.0f;
    float audioSmoothing = 0.5f;
    float audioBeatSensitivity = 0.5f;
    bool audioSyncBpm = false;
    bool spoutEnabled = false;
    std::string spoutName = "NyrexVJ";
    bool ndiEnabled = false;
    std::string ndiName = "NyrexVJ";
    bool oscEnabled = false;
    int oscPort = 9010;
    bool oscAllowRemote = false;
    bool apiEnabled = false;
    int apiPort = 9020;
    bool apiAllowRemote = false;
    std::string midiDevice;
    std::string launchQuantize = "off";
    bool midiFeedback = false;
    std::string midiOutDevice;
    bool oscOutEnabled = false;
    std::string oscOutHost = "127.0.0.1";
    int oscOutPort = 9011;
    int oscOutRate = 60;
    bool oscOutSpectrum = true;
    std::vector<OscOutParam> oscOutParams;
    DmxConfig dmx;
    VrchatConfig vrchat;
    std::array<std::string, kSpoutInputSlots> spoutInputs;
    bool spoutInputFlip = false;
    std::array<std::string, kLiveInputSlots> ndiInputs;
    std::array<std::string, kLiveInputSlots> cameraInputs;
    int cameraWidth = 1280;
    int cameraHeight = 720;
    int cameraFps = 30;
    std::string syncMode = "manual";
    float syncLatencyMs = 0.0f;
    bool syncBarAlign = true;
    bool djLinkEnabled = false;
    int djLinkPlayer = 0;
    std::string ltcDevice;
    std::string netSyncRole = "off";
    int netSyncPort = 9030;
    bool netSyncAllowRemote = false;
    std::string netSyncHost = "127.0.0.1";
    std::string netSyncName;
    bool netSyncFollowControls = true;
    bool netSyncFollowClips = true;
    bool netSyncFollowClock = true;
    bool failoverEnabled = false;
    bool failoverAutoTakeover = false;
    float failoverLostAfter = 5.0f;
    bool failoverHoldOutputs = true;
    int failoverLeaderPort = 0;
    bool cuesEnabled = true;
    bool timelineEnabled = false;
    std::string timelineSource = "auto";
    std::string timelineLost = "hold";
    std::string timelineOverride = "segment";
    bool outputFrameLock = true;
    std::string senderSpout = "master";
    std::string senderNdi = "master";
    std::string senderRecord = "master";
    std::string recordProfile = "h264";
    int recordWidth = 0;
    int recordHeight = 0;
    int recordFps = 60;
    int recordQuality = 70;
    bool recordAudio = true;
    bool recordCrashSafe = true;
    std::string recordFolder;
};

bool loadSettings(Settings& settings);
bool saveSettings(const Settings& settings);

}

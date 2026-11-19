#pragma once

#include "core/output_map.hpp"
#include "core/stage.hpp"
#include "update/update_logic.hpp"

#include <array>
#include <string>
#include <vector>

namespace nvj {

constexpr int kSpoutInputSlots = 4;

struct OscOutParam {
    std::string name;
    std::string target;
};

struct Settings {
    bool checkUpdates = true;
    bool restoreSession = true;
    bool autoInstall = true;
    bool vsync = true;
    bool openOutputOnStart = false;
    bool showStatusBar = true;
    int perfBudgetFps = 60;
    bool showTimings = true;
    float uiScale = 1.0f;
    UpdateChannel channel = UpdateChannel::Nightly;
    std::array<OutputSpec, kMaxOutputs> outputs = defaultOutputs();
    std::string language = "en";
    std::string theme = "orange";
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
    std::string midiDevice;
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
};

bool loadSettings(Settings& settings);
bool saveSettings(const Settings& settings);

}

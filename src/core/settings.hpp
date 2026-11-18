#pragma once

#include "update/update_logic.hpp"

#include <string>

namespace nvj {

struct Settings {
    bool checkUpdates = true;
    bool restoreSession = true;
    UpdateChannel channel = UpdateChannel::Nightly;
    int outputMonitor = 0;
    std::string language = "en";
    std::string lastSeenVersion;
    std::string audioDevice;
    float audioGain = 1.0f;
    float audioSmoothing = 0.5f;
    float audioBeatSensitivity = 0.5f;
    bool audioSyncBpm = false;
    bool spoutEnabled = false;
    std::string spoutName = "NyrexVJ";
};

bool loadSettings(Settings& settings);
bool saveSettings(const Settings& settings);

}

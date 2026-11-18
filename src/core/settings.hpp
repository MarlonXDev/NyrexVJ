#pragma once

#include "update/update_logic.hpp"

#include <string>

namespace nvj {

struct Settings {
    bool checkUpdates = true;
    bool restoreSession = true;
    bool autoInstall = true;
    bool vsync = true;
    bool openOutputOnStart = false;
    bool showStatusBar = true;
    float uiScale = 1.0f;
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
    bool oscEnabled = false;
    int oscPort = 9010;
    bool oscAllowRemote = false;
    std::string midiDevice;
};

bool loadSettings(Settings& settings);
bool saveSettings(const Settings& settings);

}

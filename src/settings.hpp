#pragma once

#include "update_logic.hpp"

namespace nvj {

struct Settings {
    bool checkUpdates = true;
    bool restoreSession = true;
    UpdateChannel channel = UpdateChannel::Nightly;
    int outputMonitor = 0;
};

bool loadSettings(Settings& settings);
bool saveSettings(const Settings& settings);

}

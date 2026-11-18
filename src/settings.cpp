#include "settings.hpp"

#include "log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nvj {

namespace {

std::filesystem::path settingsPath() {
    return std::filesystem::path(userDataDir()) / "settings.json";
}

}

bool loadSettings(Settings& settings) {
    std::ifstream file(settingsPath(), std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    nlohmann::json doc = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return false;
    }
    Settings result;
    auto check = doc.find("checkUpdates");
    if (check != doc.end() && check->is_boolean()) {
        result.checkUpdates = check->get<bool>();
    }
    auto restore = doc.find("restoreSession");
    if (restore != doc.end() && restore->is_boolean()) {
        result.restoreSession = restore->get<bool>();
    }
    auto channel = doc.find("channel");
    if (channel != doc.end() && channel->is_string()) {
        result.channel = channelFromName(channel->get<std::string>());
    }
    auto monitor = doc.find("outputMonitor");
    if (monitor != doc.end() && monitor->is_number_integer()) {
        result.outputMonitor = std::max(0, monitor->get<int>());
    }
    settings = result;
    return true;
}

bool saveSettings(const Settings& settings) {
    nlohmann::json doc;
    doc["checkUpdates"] = settings.checkUpdates;
    doc["restoreSession"] = settings.restoreSession;
    doc["channel"] = channelName(settings.channel);
    doc["outputMonitor"] = settings.outputMonitor;
    std::ofstream file(settingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << doc.dump(2);
    return static_cast<bool>(file);
}

}

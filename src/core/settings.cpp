#include "core/settings.hpp"

#include "core/log.hpp"

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
    auto language = doc.find("language");
    if (language != doc.end() && language->is_string()) {
        result.language = language->get<std::string>();
    }
    auto seen = doc.find("lastSeenVersion");
    if (seen != doc.end() && seen->is_string()) {
        result.lastSeenVersion = seen->get<std::string>();
    }
    auto device = doc.find("audioDevice");
    if (device != doc.end() && device->is_string()) {
        result.audioDevice = device->get<std::string>();
    }
    auto number = [&doc](const char* key, float fallback, float low, float high) {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_number()) {
            return fallback;
        }
        return std::clamp(it->get<float>(), low, high);
    };
    result.audioGain = number("audioGain", 1.0f, 0.1f, 8.0f);
    result.audioSmoothing = number("audioSmoothing", 0.5f, 0.0f, 1.0f);
    result.audioBeatSensitivity = number("audioBeatSensitivity", 0.5f, 0.0f, 1.0f);
    auto sync = doc.find("audioSyncBpm");
    if (sync != doc.end() && sync->is_boolean()) {
        result.audioSyncBpm = sync->get<bool>();
    }
    auto spout = doc.find("spoutEnabled");
    if (spout != doc.end() && spout->is_boolean()) {
        result.spoutEnabled = spout->get<bool>();
    }
    auto spoutName = doc.find("spoutName");
    if (spoutName != doc.end() && spoutName->is_string() && !spoutName->get<std::string>().empty()) {
        result.spoutName = spoutName->get<std::string>().substr(0, 60);
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
    doc["language"] = settings.language;
    doc["lastSeenVersion"] = settings.lastSeenVersion;
    doc["audioDevice"] = settings.audioDevice;
    doc["audioGain"] = settings.audioGain;
    doc["audioSmoothing"] = settings.audioSmoothing;
    doc["audioBeatSensitivity"] = settings.audioBeatSensitivity;
    doc["audioSyncBpm"] = settings.audioSyncBpm;
    doc["spoutEnabled"] = settings.spoutEnabled;
    doc["spoutName"] = settings.spoutName;
    std::ofstream file(settingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << doc.dump(2);
    return static_cast<bool>(file);
}

}

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
    auto boolean = [&doc](const char* key, bool fallback) {
        auto it = doc.find(key);
        return (it != doc.end() && it->is_boolean()) ? it->get<bool>() : fallback;
    };
    result.autoInstall = boolean("autoInstall", true);
    result.vsync = boolean("vsync", true);
    result.openOutputOnStart = boolean("openOutputOnStart", false);
    result.showStatusBar = boolean("showStatusBar", true);
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
    auto theme = doc.find("theme");
    if (theme != doc.end() && theme->is_string()) {
        result.theme = theme->get<std::string>();
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
    result.uiScale = number("uiScale", 1.0f, 0.7f, 1.8f);
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
    result.oscEnabled = boolean("oscEnabled", false);
    result.oscAllowRemote = boolean("oscAllowRemote", false);
    auto oscPort = doc.find("oscPort");
    if (oscPort != doc.end() && oscPort->is_number_integer()) {
        result.oscPort = std::clamp(oscPort->get<int>(), 1, 65535);
    }
    auto midiDevice = doc.find("midiDevice");
    if (midiDevice != doc.end() && midiDevice->is_string()) {
        result.midiDevice = midiDevice->get<std::string>();
    }
    result.oscOutEnabled = boolean("oscOutEnabled", false);
    result.oscOutSpectrum = boolean("oscOutSpectrum", true);
    auto oscOutHost = doc.find("oscOutHost");
    if (oscOutHost != doc.end() && oscOutHost->is_string() && !oscOutHost->get<std::string>().empty()) {
        result.oscOutHost = oscOutHost->get<std::string>().substr(0, 64);
    }
    auto oscOutPort = doc.find("oscOutPort");
    if (oscOutPort != doc.end() && oscOutPort->is_number_integer()) {
        result.oscOutPort = std::clamp(oscOutPort->get<int>(), 1, 65535);
    }
    auto oscOutRate = doc.find("oscOutRate");
    if (oscOutRate != doc.end() && oscOutRate->is_number_integer()) {
        result.oscOutRate = std::clamp(oscOutRate->get<int>(), 10, 120);
    }
    auto oscOutParams = doc.find("oscOutParams");
    if (oscOutParams != doc.end() && oscOutParams->is_array()) {
        for (const nlohmann::json& entry : *oscOutParams) {
            if (result.oscOutParams.size() >= 16 || !entry.is_object()) {
                continue;
            }
            auto name = entry.find("name");
            auto target = entry.find("target");
            if (name != entry.end() && name->is_string() && target != entry.end() && target->is_string()) {
                result.oscOutParams.push_back({name->get<std::string>().substr(0, 32), target->get<std::string>().substr(0, 80)});
            }
        }
    }
    auto spoutInputs = doc.find("spoutInputs");
    if (spoutInputs != doc.end() && spoutInputs->is_array()) {
        for (size_t i = 0; i < spoutInputs->size() && i < result.spoutInputs.size(); ++i) {
            if ((*spoutInputs)[i].is_string()) {
                result.spoutInputs[i] = (*spoutInputs)[i].get<std::string>().substr(0, 256);
            }
        }
    }
    result.spoutInputFlip = boolean("spoutInputFlip", false);
    settings = result;
    return true;
}

bool saveSettings(const Settings& settings) {
    nlohmann::json doc;
    doc["checkUpdates"] = settings.checkUpdates;
    doc["restoreSession"] = settings.restoreSession;
    doc["autoInstall"] = settings.autoInstall;
    doc["vsync"] = settings.vsync;
    doc["openOutputOnStart"] = settings.openOutputOnStart;
    doc["showStatusBar"] = settings.showStatusBar;
    doc["uiScale"] = settings.uiScale;
    doc["theme"] = settings.theme;
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
    doc["oscEnabled"] = settings.oscEnabled;
    doc["oscPort"] = settings.oscPort;
    doc["oscAllowRemote"] = settings.oscAllowRemote;
    doc["midiDevice"] = settings.midiDevice;
    doc["oscOutEnabled"] = settings.oscOutEnabled;
    doc["oscOutHost"] = settings.oscOutHost;
    doc["oscOutPort"] = settings.oscOutPort;
    doc["oscOutRate"] = settings.oscOutRate;
    doc["oscOutSpectrum"] = settings.oscOutSpectrum;
    nlohmann::json params = nlohmann::json::array();
    for (const OscOutParam& param : settings.oscOutParams) {
        params.push_back({{"name", param.name}, {"target", param.target}});
    }
    doc["oscOutParams"] = params;
    doc["spoutInputs"] = nlohmann::json(settings.spoutInputs);
    doc["spoutInputFlip"] = settings.spoutInputFlip;
    std::ofstream file(settingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << doc.dump(2);
    return static_cast<bool>(file);
}

}

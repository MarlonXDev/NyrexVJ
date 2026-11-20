#include "core/settings.hpp"

#include "core/frame_pacer.hpp"
#include "core/log.hpp"
#include "core/output_map_json.hpp"
#include "core/stage_json.hpp"
#include "media/video_types.hpp"

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
    auto fpsLimit = doc.find("fpsLimit");
    if (fpsLimit != doc.end() && fpsLimit->is_number_integer()) {
        result.fpsLimit = clampFpsLimit(fpsLimit->get<int>());
    }
    auto videoDecoder = doc.find("videoDecoder");
    if (videoDecoder != doc.end() && videoDecoder->is_string()) {
        result.videoDecoder = videoDecodeModeName(videoDecodeModeFromName(videoDecoder->get<std::string>()));
    }
    result.hapDirect = boolean("hapDirect", true);
    result.videoGpuConvert = boolean("videoGpuConvert", true);
    result.openOutputOnStart = boolean("openOutputOnStart", false);
    result.showStatusBar = boolean("showStatusBar", true);
    result.showTimings = boolean("showTimings", true);
    auto perfBudget = doc.find("perfBudgetFps");
    if (perfBudget != doc.end() && perfBudget->is_number_integer()) {
        result.perfBudgetFps = std::clamp(perfBudget->get<int>(), 24, 360);
    }
    auto channel = doc.find("channel");
    if (channel != doc.end() && channel->is_string()) {
        result.channel = channelFromName(channel->get<std::string>());
    }
    int legacyMonitor = 0;
    auto monitor = doc.find("outputMonitor");
    if (monitor != doc.end() && monitor->is_number_integer()) {
        legacyMonitor = std::max(0, monitor->get<int>());
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
    result.ndiEnabled = boolean("ndiEnabled", false);
    auto ndiName = doc.find("ndiName");
    if (ndiName != doc.end() && ndiName->is_string() && !ndiName->get<std::string>().empty()) {
        result.ndiName = ndiName->get<std::string>().substr(0, 60);
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
    result.outputs = defaultOutputs();
    result.outputs[0].monitor = legacyMonitor;
    auto outputs = doc.find("outputs");
    if (outputs != doc.end() && outputs->is_array()) {
        for (size_t i = 0; i < outputs->size() && i < result.outputs.size(); ++i) {
            outputFromJson((*outputs)[i], result.outputs[i]);
        }
    }
    result.outputs[0].enabled = true;
    auto dmx = doc.find("dmx");
    if (dmx != doc.end()) {
        dmxFromJson(*dmx, result.dmx);
    }
    auto vrchat = doc.find("vrchat");
    if (vrchat != doc.end()) {
        vrchatFromJson(*vrchat, result.vrchat);
    }
    settings = result;
    return true;
}

bool saveSettings(const Settings& settings) {
    nlohmann::json doc;
    doc["checkUpdates"] = settings.checkUpdates;
    doc["restoreSession"] = settings.restoreSession;
    doc["autoInstall"] = settings.autoInstall;
    doc["vsync"] = settings.vsync;
    doc["fpsLimit"] = settings.fpsLimit;
    doc["videoDecoder"] = settings.videoDecoder;
    doc["hapDirect"] = settings.hapDirect;
    doc["videoGpuConvert"] = settings.videoGpuConvert;
    doc["openOutputOnStart"] = settings.openOutputOnStart;
    doc["showStatusBar"] = settings.showStatusBar;
    doc["showTimings"] = settings.showTimings;
    doc["perfBudgetFps"] = settings.perfBudgetFps;
    doc["uiScale"] = settings.uiScale;
    doc["theme"] = settings.theme;
    doc["channel"] = channelName(settings.channel);
    doc["outputMonitor"] = settings.outputs[0].monitor;
    doc["language"] = settings.language;
    doc["lastSeenVersion"] = settings.lastSeenVersion;
    doc["audioDevice"] = settings.audioDevice;
    doc["audioGain"] = settings.audioGain;
    doc["audioSmoothing"] = settings.audioSmoothing;
    doc["audioBeatSensitivity"] = settings.audioBeatSensitivity;
    doc["audioSyncBpm"] = settings.audioSyncBpm;
    doc["spoutEnabled"] = settings.spoutEnabled;
    doc["spoutName"] = settings.spoutName;
    doc["ndiEnabled"] = settings.ndiEnabled;
    doc["ndiName"] = settings.ndiName;
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
    nlohmann::json outputs = nlohmann::json::array();
    for (const OutputSpec& spec : settings.outputs) {
        outputs.push_back(outputToJson(spec));
    }
    doc["outputs"] = outputs;
    doc["dmx"] = dmxToJson(settings.dmx);
    doc["vrchat"] = vrchatToJson(settings.vrchat);
    std::ofstream file(settingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << doc.dump(2);
    return static_cast<bool>(file);
}

}

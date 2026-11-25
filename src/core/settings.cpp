#include "core/settings.hpp"

#include "core/frame_pacer.hpp"
#include "core/log.hpp"
#include "core/output_map_json.hpp"
#include "core/record_plan.hpp"
#include "core/render_policy.hpp"
#include "core/routing.hpp"
#include "core/stage_json.hpp"
#include "media/video_types.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
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
    result.prewarmNext = boolean("prewarmNext", true);
    result.generatorClips = boolean("generatorClips", true);
    result.gpuParticles = boolean("gpuParticles", false);
    auto gpuBudget = doc.find("gpuParticleBudgetMb");
    if (gpuBudget != doc.end() && gpuBudget->is_number_integer()) {
        result.gpuParticleBudgetMb = std::clamp(gpuBudget->get<int>(), 8, 512);
    }
    auto particleLimit = doc.find("particleLimit");
    if (particleLimit != doc.end() && particleLimit->is_number_integer()) {
        result.particleLimit = std::clamp(particleLimit->get<int>(), 64, 16384);
    }
    auto convertMode = doc.find("convertMode");
    if (convertMode != doc.end() && convertMode->is_string()) {
        const std::string value = convertMode->get<std::string>();
        result.convertMode = (value == "off" || value == "ask" || value == "automatic") ? value : "ask";
    }
    auto convertFormat = doc.find("convertFormat");
    if (convertFormat != doc.end() && convertFormat->is_string()) {
        const std::string value = convertFormat->get<std::string>();
        result.convertFormat = (value == "auto" || value == "hap" || value == "hap_alpha" || value == "hap_q") ? value : "auto";
    }
    auto convertCache = doc.find("convertCacheMb");
    if (convertCache != doc.end() && convertCache->is_number_integer()) {
        result.convertCacheMb = std::clamp(convertCache->get<int>(), 256, 204800);
    }
    result.ffmpegHintDismissed = boolean("ffmpegHintDismissed", false);
    result.openOutputOnStart = boolean("openOutputOnStart", false);
    result.showStatusBar = boolean("showStatusBar", true);
    result.showTimings = boolean("showTimings", true);
    auto perfBudget = doc.find("perfBudgetFps");
    if (perfBudget != doc.end() && perfBudget->is_number_integer()) {
        result.perfBudgetFps = std::clamp(perfBudget->get<int>(), 24, 360);
    }
    auto memoryBudget = doc.find("gpuMemoryBudgetMb");
    if (memoryBudget != doc.end() && memoryBudget->is_number_integer()) {
        const int value = memoryBudget->get<int>();
        result.gpuMemoryBudgetMb = value <= 0 ? 0 : std::clamp(value, 256, 65536);
    }
    result.gpuMemoryTrim = boolean("gpuMemoryTrim", true);
    result.gpuMemoryLowerQuality = boolean("gpuMemoryLowerQuality", false);
    result.diagWarnings = boolean("diagWarnings", true);
    auto diagP99 = doc.find("diagP99Percent");
    if (diagP99 != doc.end() && diagP99->is_number_integer()) {
        result.diagP99Percent = std::clamp(diagP99->get<int>(), 110, 600);
    }
    auto diagDropped = doc.find("diagDroppedPerMinute");
    if (diagDropped != doc.end() && diagDropped->is_number_integer()) {
        result.diagDroppedPerMinute = std::clamp(diagDropped->get<int>(), 1, 600);
    }
    auto diagMemory = doc.find("diagMemoryPercent");
    if (diagMemory != doc.end() && diagMemory->is_number_integer()) {
        result.diagMemoryPercent = std::clamp(diagMemory->get<int>(), 50, 99);
    }
    auto renderQuality = doc.find("renderQuality");
    if (renderQuality != doc.end() && renderQuality->is_number_integer()) {
        result.renderQuality = clampQualityMode(renderQuality->get<int>());
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
    auto design = doc.find("design");
    result.design = design != doc.end() && design->is_number_integer() ? design->get<int>() : 0;
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
    result.apiEnabled = boolean("apiEnabled", false);
    result.apiAllowRemote = boolean("apiAllowRemote", false);
    auto apiPort = doc.find("apiPort");
    if (apiPort != doc.end() && apiPort->is_number_integer()) {
        result.apiPort = std::clamp(apiPort->get<int>(), 1024, 65535);
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
    const auto textArray = [&doc](const char* key, auto& target) {
        auto it = doc.find(key);
        if (it != doc.end() && it->is_array()) {
            for (size_t i = 0; i < it->size() && i < target.size(); ++i) {
                if ((*it)[i].is_string()) {
                    target[i] = (*it)[i].get<std::string>().substr(0, 256);
                }
            }
        }
    };
    textArray("ndiInputs", result.ndiInputs);
    textArray("cameraInputs", result.cameraInputs);
    const auto cameraValue = [&doc](const char* key, int fallback, int low, int high) {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_number_integer()) {
            return fallback;
        }
        const int value = it->get<int>();
        return value == 0 ? 0 : std::clamp(value, low, high);
    };
    result.cameraWidth = cameraValue("cameraWidth", 1280, 160, 7680);
    result.cameraHeight = cameraValue("cameraHeight", 720, 120, 4320);
    result.cameraFps = cameraValue("cameraFps", 30, 1, 240);
    auto syncMode = doc.find("syncMode");
    if (syncMode != doc.end() && syncMode->is_string()) {
        const std::string mode = syncMode->get<std::string>();
        if (mode == "auto" || mode == "manual" || mode == "audio" || mode == "midiclock" || mode == "timecode" || mode == "djlink") {
            result.syncMode = mode;
        }
    }
    result.syncLatencyMs = number("syncLatencyMs", 0.0f, -500.0f, 500.0f);
    result.syncBarAlign = boolean("syncBarAlign", true);
    result.djLinkEnabled = boolean("djLinkEnabled", false);
    auto djPlayer = doc.find("djLinkPlayer");
    if (djPlayer != doc.end() && djPlayer->is_number_integer()) {
        result.djLinkPlayer = std::clamp(djPlayer->get<int>(), 0, 6);
    }
    auto ltcDevice = doc.find("ltcDevice");
    if (ltcDevice != doc.end() && ltcDevice->is_string()) {
        result.ltcDevice = ltcDevice->get<std::string>().substr(0, 256);
    }
    result.cuesEnabled = boolean("cuesEnabled", true);
    result.timelineEnabled = boolean("timelineEnabled", false);
    result.midiFeedback = boolean("midiFeedback", false);
    auto midiOut = doc.find("midiOutDevice");
    if (midiOut != doc.end() && midiOut->is_string()) {
        result.midiOutDevice = midiOut->get<std::string>().substr(0, 256);
    }
    const auto choice = [&doc](const char* key, std::initializer_list<const char*> allowed, const char* fallback) {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_string()) {
            return std::string(fallback);
        }
        const std::string value = it->get<std::string>();
        for (const char* name : allowed) {
            if (value == name) {
                return value;
            }
        }
        return std::string(fallback);
    };
    result.launchQuantize = choice("launchQuantize", {"off", "beat", "bar", "2bars", "4bars"}, "off");
    result.timelineSource = choice("timelineSource", {"auto", "timecode", "transport"}, "auto");
    result.timelineLost = choice("timelineLost", {"hold", "continue"}, "hold");
    result.timelineOverride = choice("timelineOverride", {"segment", "released"}, "segment");
    result.outputFrameLock = boolean("outputFrameLock", true);
    const auto senderChoice = [&doc](const char* key) {
        auto it = doc.find(key);
        routing::Source parsed;
        if (it != doc.end() && it->is_string() && routing::parseSenderKey(it->get<std::string>(), parsed)) {
            return routing::senderKey(parsed);
        }
        return std::string("master");
    };
    result.senderSpout = senderChoice("senderSpout");
    result.senderNdi = senderChoice("senderNdi");
    result.senderRecord = senderChoice("senderRecord");
    auto recordProfile = doc.find("recordProfile");
    if (recordProfile != doc.end() && recordProfile->is_string() && findRecordProfile(recordProfile->get<std::string>()) != nullptr) {
        result.recordProfile = recordProfile->get<std::string>();
    }
    auto recordInt = [&doc](const char* key, int fallback, int low, int high) {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_number_integer()) {
            return fallback;
        }
        return std::clamp(it->get<int>(), low, high);
    };
    result.recordWidth = recordInt("recordWidth", 0, 0, kMaxRecordDimension);
    result.recordHeight = recordInt("recordHeight", 0, 0, kMaxRecordDimension);
    if (result.recordWidth == 0 || result.recordHeight == 0) {
        result.recordWidth = 0;
        result.recordHeight = 0;
    }
    result.recordFps = recordInt("recordFps", 60, 24, 120);
    result.recordQuality = recordInt("recordQuality", 70, 0, 100);
    result.recordAudio = boolean("recordAudio", true);
    result.recordCrashSafe = boolean("recordCrashSafe", true);
    auto recordFolder = doc.find("recordFolder");
    if (recordFolder != doc.end() && recordFolder->is_string()) {
        result.recordFolder = recordFolder->get<std::string>().substr(0, 1024);
    }
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
    doc["prewarmNext"] = settings.prewarmNext;
    doc["generatorClips"] = settings.generatorClips;
    doc["particleLimit"] = std::clamp(settings.particleLimit, 64, 16384);
    doc["gpuParticles"] = settings.gpuParticles;
    doc["gpuParticleBudgetMb"] = std::clamp(settings.gpuParticleBudgetMb, 8, 512);
    doc["convertMode"] = settings.convertMode;
    doc["convertFormat"] = settings.convertFormat;
    doc["convertCacheMb"] = settings.convertCacheMb;
    doc["ffmpegHintDismissed"] = settings.ffmpegHintDismissed;
    doc["openOutputOnStart"] = settings.openOutputOnStart;
    doc["showStatusBar"] = settings.showStatusBar;
    doc["showTimings"] = settings.showTimings;
    doc["perfBudgetFps"] = settings.perfBudgetFps;
    doc["gpuMemoryBudgetMb"] = settings.gpuMemoryBudgetMb <= 0 ? 0 : std::clamp(settings.gpuMemoryBudgetMb, 256, 65536);
    doc["gpuMemoryTrim"] = settings.gpuMemoryTrim;
    doc["gpuMemoryLowerQuality"] = settings.gpuMemoryLowerQuality;
    doc["diagWarnings"] = settings.diagWarnings;
    doc["diagP99Percent"] = std::clamp(settings.diagP99Percent, 110, 600);
    doc["diagDroppedPerMinute"] = std::clamp(settings.diagDroppedPerMinute, 1, 600);
    doc["diagMemoryPercent"] = std::clamp(settings.diagMemoryPercent, 50, 99);
    doc["renderQuality"] = settings.renderQuality;
    doc["uiScale"] = settings.uiScale;
    doc["theme"] = settings.theme;
    doc["design"] = settings.design;
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
    doc["apiEnabled"] = settings.apiEnabled;
    doc["apiPort"] = settings.apiPort;
    doc["apiAllowRemote"] = settings.apiAllowRemote;
    doc["midiDevice"] = settings.midiDevice;
    doc["launchQuantize"] = settings.launchQuantize;
    doc["midiFeedback"] = settings.midiFeedback;
    doc["midiOutDevice"] = settings.midiOutDevice;
    doc["syncMode"] = settings.syncMode;
    doc["syncLatencyMs"] = settings.syncLatencyMs;
    doc["syncBarAlign"] = settings.syncBarAlign;
    doc["djLinkEnabled"] = settings.djLinkEnabled;
    doc["djLinkPlayer"] = settings.djLinkPlayer;
    doc["ltcDevice"] = settings.ltcDevice;
    doc["cuesEnabled"] = settings.cuesEnabled;
    doc["timelineEnabled"] = settings.timelineEnabled;
    doc["timelineSource"] = settings.timelineSource;
    doc["timelineLost"] = settings.timelineLost;
    doc["timelineOverride"] = settings.timelineOverride;
    doc["outputFrameLock"] = settings.outputFrameLock;
    if (settings.senderSpout != "master") {
        doc["senderSpout"] = settings.senderSpout;
    }
    if (settings.senderNdi != "master") {
        doc["senderNdi"] = settings.senderNdi;
    }
    if (settings.senderRecord != "master") {
        doc["senderRecord"] = settings.senderRecord;
    }
    doc["recordProfile"] = settings.recordProfile;
    doc["recordWidth"] = settings.recordWidth;
    doc["recordHeight"] = settings.recordHeight;
    doc["recordFps"] = settings.recordFps;
    doc["recordQuality"] = settings.recordQuality;
    doc["recordAudio"] = settings.recordAudio;
    doc["recordCrashSafe"] = settings.recordCrashSafe;
    doc["recordFolder"] = settings.recordFolder;
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
    doc["ndiInputs"] = nlohmann::json(settings.ndiInputs);
    doc["cameraInputs"] = nlohmann::json(settings.cameraInputs);
    doc["cameraWidth"] = settings.cameraWidth;
    doc["cameraHeight"] = settings.cameraHeight;
    doc["cameraFps"] = settings.cameraFps;
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

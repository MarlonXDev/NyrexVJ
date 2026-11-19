#include "core/stage_json.hpp"

#include <algorithm>

namespace nvj {

namespace {

nlohmann::json bindingToJson(const SourceBinding& binding) {
    nlohmann::json doc;
    doc["source"] = binding.source;
    doc["fixed"] = binding.fixed;
    doc["low"] = binding.low;
    doc["high"] = binding.high;
    doc["invert"] = binding.invert;
    return doc;
}

void bindingFromJson(const nlohmann::json& doc, SourceBinding& binding) {
    if (!doc.is_object()) {
        return;
    }
    auto integer = [&doc](const char* key, int& target) {
        auto it = doc.find(key);
        if (it != doc.end() && it->is_number_integer()) {
            target = it->get<int>();
        }
    };
    auto number = [&doc](const char* key, float& target) {
        auto it = doc.find(key);
        if (it != doc.end() && it->is_number()) {
            target = it->get<float>();
        }
    };
    integer("source", binding.source);
    number("fixed", binding.fixed);
    number("low", binding.low);
    number("high", binding.high);
    auto invert = doc.find("invert");
    if (invert != doc.end() && invert->is_boolean()) {
        binding.invert = invert->get<bool>();
    }
}

bool boolean(const nlohmann::json& doc, const char* key, bool& target) {
    auto it = doc.find(key);
    if (it != doc.end() && it->is_boolean()) {
        target = it->get<bool>();
        return true;
    }
    return false;
}

bool integer(const nlohmann::json& doc, const char* key, int& target) {
    auto it = doc.find(key);
    if (it != doc.end() && it->is_number_integer()) {
        target = it->get<int>();
        return true;
    }
    return false;
}

bool text(const nlohmann::json& doc, const char* key, std::string& target, size_t limit) {
    auto it = doc.find(key);
    if (it != doc.end() && it->is_string()) {
        target = it->get<std::string>().substr(0, limit);
        return true;
    }
    return false;
}

}

nlohmann::json dmxToJson(const DmxConfig& config) {
    nlohmann::json doc;
    doc["artnetEnabled"] = config.artnetEnabled;
    doc["artnetHost"] = config.artnetHost;
    doc["artnetPort"] = config.artnetPort;
    doc["artnetNet"] = config.artnetNet;
    doc["artnetFirstUniverse"] = config.artnetFirstUniverse;
    doc["artnetRate"] = config.artnetRate;
    doc["stripEnabled"] = config.stripEnabled;
    doc["stripAtTop"] = config.stripAtTop;
    doc["stripPadUniverses"] = config.stripPadUniverses;
    nlohmann::json lanes = nlohmann::json::array();
    for (const DmxLane& lane : config.lanes) {
        nlohmann::json entry = bindingToJson(lane.binding);
        entry["name"] = lane.name;
        entry["universe"] = lane.universe;
        entry["channel"] = lane.channel;
        entry["fine"] = lane.fine;
        lanes.push_back(entry);
    }
    doc["lanes"] = lanes;
    return doc;
}

void dmxFromJson(const nlohmann::json& doc, DmxConfig& config) {
    config = DmxConfig{};
    if (!doc.is_object()) {
        return;
    }
    boolean(doc, "artnetEnabled", config.artnetEnabled);
    text(doc, "artnetHost", config.artnetHost, 64);
    integer(doc, "artnetPort", config.artnetPort);
    integer(doc, "artnetNet", config.artnetNet);
    integer(doc, "artnetFirstUniverse", config.artnetFirstUniverse);
    integer(doc, "artnetRate", config.artnetRate);
    boolean(doc, "stripEnabled", config.stripEnabled);
    boolean(doc, "stripAtTop", config.stripAtTop);
    boolean(doc, "stripPadUniverses", config.stripPadUniverses);
    auto lanes = doc.find("lanes");
    if (lanes != doc.end() && lanes->is_array()) {
        for (const nlohmann::json& entry : *lanes) {
            if (config.lanes.size() >= kMaxDmxLanes || !entry.is_object()) {
                continue;
            }
            DmxLane lane;
            text(entry, "name", lane.name, 48);
            integer(entry, "universe", lane.universe);
            integer(entry, "channel", lane.channel);
            boolean(entry, "fine", lane.fine);
            bindingFromJson(entry, lane.binding);
            config.lanes.push_back(lane);
        }
    }
    normalizeDmx(config);
}

nlohmann::json vrchatToJson(const VrchatConfig& config) {
    nlohmann::json doc;
    doc["enabled"] = config.enabled;
    doc["host"] = config.host;
    doc["port"] = config.port;
    doc["rate"] = config.rate;
    nlohmann::json params = nlohmann::json::array();
    for (const VrchatParam& param : config.params) {
        nlohmann::json entry = bindingToJson(param.binding);
        entry["name"] = param.name;
        entry["type"] = static_cast<int>(param.type);
        params.push_back(entry);
    }
    doc["params"] = params;
    return doc;
}

void vrchatFromJson(const nlohmann::json& doc, VrchatConfig& config) {
    config = VrchatConfig{};
    if (!doc.is_object()) {
        return;
    }
    boolean(doc, "enabled", config.enabled);
    text(doc, "host", config.host, 64);
    integer(doc, "port", config.port);
    integer(doc, "rate", config.rate);
    auto params = doc.find("params");
    if (params != doc.end() && params->is_array()) {
        for (const nlohmann::json& entry : *params) {
            if (config.params.size() >= kMaxVrchatParams * 2 || !entry.is_object()) {
                continue;
            }
            VrchatParam param;
            text(entry, "name", param.name, 64);
            int type = 0;
            integer(entry, "type", type);
            param.type = static_cast<VrchatType>(std::clamp(type, 0, 2));
            bindingFromJson(entry, param.binding);
            config.params.push_back(param);
        }
    }
    normalizeVrchat(config);
}

}

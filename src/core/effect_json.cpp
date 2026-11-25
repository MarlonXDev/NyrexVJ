#include "core/effect_json.hpp"

#include "render/effect_library.hpp"

#include <algorithm>

namespace nvj {

namespace {

using json = nlohmann::json;

float readNumber(const json& node, const char* key, float fallback, float low, float high) {
    if (!node.is_object()) {
        return fallback;
    }
    auto it = node.find(key);
    if (it == node.end() || !it->is_number()) {
        return fallback;
    }
    return std::clamp(it->get<float>(), low, high);
}

bool readBool(const json& node, const char* key, bool fallback) {
    if (!node.is_object()) {
        return fallback;
    }
    auto it = node.find(key);
    if (it == node.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::string readString(const json& node, const char* key) {
    if (!node.is_object()) {
        return std::string();
    }
    auto it = node.find(key);
    if (it == node.end() || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

}

nlohmann::json effectsToJson(const std::array<EffectSlot, kEffectSlots>& slots) {
    nlohmann::json list = json::array();
    const std::vector<EffectDef>& library = effectLibrary();
    for (const EffectSlot& slot : slots) {
        nlohmann::json entry;
        if (slot.effect >= 0 && slot.effect < static_cast<int>(library.size())) {
            entry["effect"] = library[static_cast<size_t>(slot.effect)].name;
        } else {
            entry["effect"] = nullptr;
        }
        entry["enabled"] = slot.enabled;
        entry["params"] = json::array({slot.params[0], slot.params[1], slot.params[2], slot.params[3]});
        entry["react"] = nlohmann::json{{"band", audioBandName(slot.reactBand)}, {"param", slot.reactParam}, {"amount", slot.reactAmount}, {"mode", reactModeName(slot.reactMode)}};
        list.push_back(entry);
    }
    return list;
}

void effectsFromJson(const nlohmann::json& node, std::array<EffectSlot, kEffectSlots>& slots, std::vector<std::string>* warnings) {
    slots = std::array<EffectSlot, kEffectSlots>{};
    if (!node.is_array()) {
        return;
    }
    for (size_t i = 0; i < node.size() && i < static_cast<size_t>(kEffectSlots); ++i) {
        const json& entry = node[i];
        EffectSlot& slot = slots[i];
        std::string name = readString(entry, "effect");
        if (name.empty()) {
            continue;
        }
        int index = effectIndexByName(name);
        if (index < 0) {
            if (warnings) {
                warnings->push_back("Unknown effect '" + name + "'");
            }
            continue;
        }
        setEffect(slot, index);
        slot.enabled = readBool(entry, "enabled", true);
        auto params = entry.find("params");
        if (params != entry.end() && params->is_array()) {
            for (size_t p = 0; p < params->size() && p < static_cast<size_t>(kParamCount); ++p) {
                if ((*params)[p].is_number()) {
                    slot.params[p] = std::clamp((*params)[p].get<float>(), 0.0f, 1.0f);
                }
            }
        }
        auto react = entry.find("react");
        if (react != entry.end() && react->is_object()) {
            slot.reactBand = audioBandFromName(readString(*react, "band"));
            slot.reactParam = static_cast<int>(readNumber(*react, "param", 0.0f, 0.0f, static_cast<float>(kParamCount - 1)));
            slot.reactAmount = readNumber(*react, "amount", 0.5f, 0.0f, 1.0f);
            slot.reactMode = reactModeFromName(readString(*react, "mode"));
        }
    }
}

}

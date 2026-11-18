#include "preset.hpp"

#include "clip_library.hpp"
#include "effect_library.hpp"
#include "log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nvj {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

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

json gradeToJson(const ColorGrade& grade) {
    return json{{"hue", grade.hue}, {"saturation", grade.saturation}, {"brightness", grade.brightness}, {"contrast", grade.contrast}};
}

ColorGrade gradeFromJson(const json& node) {
    ColorGrade grade;
    grade.hue = readNumber(node, "hue", 0.0f, -0.5f, 0.5f);
    grade.saturation = readNumber(node, "saturation", 1.0f, 0.0f, 2.0f);
    grade.brightness = readNumber(node, "brightness", 0.0f, -0.5f, 0.5f);
    grade.contrast = readNumber(node, "contrast", 1.0f, 0.5f, 2.0f);
    return grade;
}

json effectsToJson(const std::array<EffectSlot, kEffectSlots>& slots) {
    json list = json::array();
    const std::vector<EffectDef>& library = effectLibrary();
    for (const EffectSlot& slot : slots) {
        json entry;
        if (slot.effect >= 0 && slot.effect < static_cast<int>(library.size())) {
            entry["effect"] = library[static_cast<size_t>(slot.effect)].name;
        } else {
            entry["effect"] = nullptr;
        }
        entry["enabled"] = slot.enabled;
        entry["params"] = json::array({slot.params[0], slot.params[1], slot.params[2], slot.params[3]});
        list.push_back(entry);
    }
    return list;
}

void effectsFromJson(const json& node, std::array<EffectSlot, kEffectSlots>& slots, std::vector<std::string>* warnings) {
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
    }
}

std::array<float, 3> colorFromJson(const json& node, const char* key, const std::array<float, 3>& fallback) {
    std::array<float, 3> color = fallback;
    if (!node.is_object()) {
        return color;
    }
    auto it = node.find(key);
    if (it == node.end() || !it->is_array() || it->size() < 3) {
        return color;
    }
    for (size_t i = 0; i < 3; ++i) {
        if ((*it)[i].is_number()) {
            color[i] = std::clamp((*it)[i].get<float>(), 0.0f, 1.0f);
        }
    }
    return color;
}

bool writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    file.flush();
    return static_cast<bool>(file);
}

bool readTextFile(const fs::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

fs::path presetPath(const std::string& name) {
    return fs::path(presetsDirectory()) / (sanitizePresetName(name) + ".json");
}

}

std::string compositionToJson(const Composition& comp) {
    json doc;
    doc["format"] = 1;
    doc["app"] = "NyrexVJ";
    doc["name"] = comp.name;
    doc["bpm"] = comp.bpm;
    int resolution = std::clamp(comp.resolution, 0, kResolutionCount - 1);
    doc["resolution"] = json::array({kResolutions[resolution].width, kResolutions[resolution].height});

    json master;
    master["level"] = comp.master.level;
    master["grade"] = gradeToJson(comp.master.grade);
    master["fx"] = effectsToJson(comp.master.fx);
    doc["master"] = master;

    const std::vector<ClipDef>& clips = clipLibrary();
    json layers = json::array();
    for (const Layer& layer : comp.layers) {
        json entry;
        json cells = json::array();
        for (int cell : layer.cells) {
            if (cell >= 0 && cell < static_cast<int>(clips.size())) {
                cells.push_back(clips[static_cast<size_t>(cell)].name);
            } else {
                cells.push_back(nullptr);
            }
        }
        entry["cells"] = cells;
        entry["active"] = layer.activeColumn;
        entry["opacity"] = layer.opacity;
        entry["blend"] = blendName(layer.blend);
        entry["bypass"] = layer.bypass;
        entry["solo"] = layer.solo;
        entry["colorA"] = json::array({layer.colorA[0], layer.colorA[1], layer.colorA[2]});
        entry["colorB"] = json::array({layer.colorB[0], layer.colorB[1], layer.colorB[2]});
        entry["rainbow"] = layer.rainbow;
        entry["speed"] = layer.speed;
        entry["scale"] = layer.scale;
        entry["grade"] = gradeToJson(layer.grade);
        entry["fx"] = effectsToJson(layer.fx);
        layers.push_back(entry);
    }
    doc["layers"] = layers;
    return doc.dump(2);
}

bool compositionFromJson(const std::string& text, Composition& comp, std::string& error, std::vector<std::string>* warnings) {
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "This file is not a valid preset";
        return false;
    }
    if (!doc.contains("layers") || !doc["layers"].is_array()) {
        error = "This file has no layers";
        return false;
    }

    Composition result;
    std::string name = readString(doc, "name");
    if (!name.empty()) {
        result.name = name;
    }
    result.bpm = readNumber(doc, "bpm", 128.0f, 40.0f, 240.0f);
    auto resolution = doc.find("resolution");
    if (resolution != doc.end() && resolution->is_array() && resolution->size() >= 2 && (*resolution)[0].is_number() && (*resolution)[1].is_number()) {
        result.resolution = resolutionIndexFor((*resolution)[0].get<int>(), (*resolution)[1].get<int>());
    }

    auto master = doc.find("master");
    if (master != doc.end() && master->is_object()) {
        result.master.level = readNumber(*master, "level", 1.0f, 0.0f, 1.0f);
        if (master->contains("grade")) {
            result.master.grade = gradeFromJson((*master)["grade"]);
        }
        if (master->contains("fx")) {
            effectsFromJson((*master)["fx"], result.master.fx, warnings);
        }
    }

    const json& layers = doc["layers"];
    for (size_t l = 0; l < layers.size() && l < static_cast<size_t>(kLayerCount); ++l) {
        const json& entry = layers[l];
        Layer& layer = result.layers[l];
        if (!entry.is_object()) {
            continue;
        }
        auto cells = entry.find("cells");
        if (cells != entry.end() && cells->is_array()) {
            for (size_t c = 0; c < cells->size() && c < static_cast<size_t>(kColumnCount); ++c) {
                const json& cell = (*cells)[c];
                if (!cell.is_string()) {
                    continue;
                }
                int index = clipIndexByName(cell.get<std::string>());
                if (index < 0 && warnings) {
                    warnings->push_back("Unknown clip '" + cell.get<std::string>() + "'");
                }
                layer.cells[c] = index;
            }
        }
        int active = -1;
        auto activeIt = entry.find("active");
        if (activeIt != entry.end() && activeIt->is_number_integer()) {
            active = activeIt->get<int>();
        }
        layer.activeColumn = (active >= 0 && active < kColumnCount && layer.cells[static_cast<size_t>(active)] >= 0) ? active : -1;
        layer.opacity = readNumber(entry, "opacity", 1.0f, 0.0f, 1.0f);
        layer.blend = blendFromName(readString(entry, "blend"));
        layer.bypass = readBool(entry, "bypass", false);
        layer.solo = readBool(entry, "solo", false);
        layer.colorA = colorFromJson(entry, "colorA", layer.colorA);
        layer.colorB = colorFromJson(entry, "colorB", layer.colorB);
        layer.rainbow = readBool(entry, "rainbow", false);
        layer.speed = readNumber(entry, "speed", 1.0f, 0.1f, 4.0f);
        layer.scale = readNumber(entry, "scale", 1.0f, 0.25f, 4.0f);
        if (entry.contains("grade")) {
            layer.grade = gradeFromJson(entry["grade"]);
        }
        if (entry.contains("fx")) {
            effectsFromJson(entry["fx"], layer.fx, warnings);
        }
    }

    comp = result;
    return true;
}

std::string sanitizePresetName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 32 || std::string("\\/:*?\"<>|").find(c) != std::string::npos) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    size_t start = out.find_first_not_of(" .");
    if (start == std::string::npos) {
        return std::string();
    }
    size_t end = out.find_last_not_of(" .");
    out = out.substr(start, end - start + 1);
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

std::string presetsDirectory() {
    fs::path dir = fs::path(userDataDir()) / "presets";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

std::vector<std::string> listUserPresets() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(presetsDirectory(), ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        std::string la = a;
        std::string lb = b;
        std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return la < lb;
    });
    return names;
}

bool saveUserPreset(const Composition& comp, const std::string& name, std::string& error) {
    std::string clean = sanitizePresetName(name);
    if (clean.empty()) {
        error = "Please enter a name";
        return false;
    }
    Composition copy = comp;
    copy.name = clean;
    if (!writeTextFile(presetPath(clean), compositionToJson(copy))) {
        error = "Could not write the preset file";
        return false;
    }
    return true;
}

bool loadUserPreset(const std::string& name, Composition& comp, std::string& error) {
    std::string text;
    if (!readTextFile(presetPath(name), text)) {
        error = "Could not read the preset file";
        return false;
    }
    if (!compositionFromJson(text, comp, error)) {
        return false;
    }
    comp.name = sanitizePresetName(name);
    return true;
}

bool deleteUserPreset(const std::string& name) {
    std::error_code ec;
    return fs::remove(presetPath(name), ec);
}

bool saveSession(const Composition& comp) {
    return writeTextFile(fs::path(userDataDir()) / "autosave.json", compositionToJson(comp));
}

bool loadSession(Composition& comp) {
    std::string text;
    if (!readTextFile(fs::path(userDataDir()) / "autosave.json", text)) {
        return false;
    }
    std::string error;
    return compositionFromJson(text, comp, error);
}

}

#include "core/preset.hpp"

#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "core/log.hpp"
#include "core/recovery.hpp"

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

json modsToJson(const std::array<AudioMod, kModSlots>& mods) {
    json list = json::array();
    for (const AudioMod& mod : mods) {
        list.push_back(json{{"band", audioBandName(mod.band)}, {"target", modTargetName(mod.target)}, {"amount", mod.amount}});
    }
    return list;
}

void modsFromJson(const json& node, std::array<AudioMod, kModSlots>& mods) {
    mods = std::array<AudioMod, kModSlots>{};
    if (!node.is_array()) {
        return;
    }
    for (size_t i = 0; i < node.size() && i < static_cast<size_t>(kModSlots); ++i) {
        const json& entry = node[i];
        mods[i].band = audioBandFromName(readString(entry, "band"));
        mods[i].target = modTargetFromName(readString(entry, "target"));
        mods[i].amount = readNumber(entry, "amount", 0.5f, 0.0f, 1.0f);
    }
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
        entry["react"] = json{{"band", audioBandName(slot.reactBand)}, {"param", slot.reactParam}, {"amount", slot.reactAmount}, {"mode", reactModeName(slot.reactMode)}};
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
        auto react = entry.find("react");
        if (react != entry.end() && react->is_object()) {
            slot.reactBand = audioBandFromName(readString(*react, "band"));
            slot.reactParam = static_cast<int>(readNumber(*react, "param", 0.0f, 0.0f, static_cast<float>(kParamCount - 1)));
            slot.reactAmount = readNumber(*react, "amount", 0.5f, 0.0f, 1.0f);
            slot.reactMode = reactModeFromName(readString(*react, "mode"));
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

json masterToJson(const Master& master) {
    json node;
    node["level"] = master.level;
    node["grade"] = gradeToJson(master.grade);
    node["mods"] = modsToJson(master.mods);
    node["fx"] = effectsToJson(master.fx);
    return node;
}

json playbackToJson(const ClipPlayback& playback) {
    json node;
    node["in"] = playback.in;
    node["out"] = playback.out;
    node["loop"] = loopModeName(playback.loop);
    node["speed"] = playback.speed;
    node["beats"] = playback.syncBeats;
    if (std::any_of(playback.values.begin(), playback.values.end(), [](float value) { return value != kClipValueUnset; })) {
        json values = json::array();
        for (const float value : playback.values) {
            values.push_back(value == kClipValueUnset ? json(nullptr) : json(value));
        }
        node["values"] = values;
    }
    return node;
}

ClipPlayback playbackFromJson(const json& node) {
    ClipPlayback playback;
    if (!node.is_object()) {
        return playback;
    }
    playback.in = readNumber(node, "in", 0.0f, 0.0f, 1.0e7f);
    playback.out = readNumber(node, "out", 0.0f, 0.0f, 1.0e7f);
    playback.loop = loopModeFromName(readString(node, "loop"));
    playback.speed = readNumber(node, "speed", 1.0f, kMinPlaySpeed, kMaxPlaySpeed);
    playback.syncBeats = readNumber(node, "beats", 0.0f, 0.0f, 128.0f);
    if (node.contains("values") && node["values"].is_array()) {
        const json& values = node["values"];
        for (size_t i = 0; i < values.size() && i < playback.values.size(); ++i) {
            if (values[i].is_number()) {
                const double value = values[i].get<double>();
                playback.values[i] = std::isfinite(value) && std::fabs(value) <= 1.0e6 ? static_cast<float>(value) : kClipValueUnset;
            }
        }
    }
    return clampedPlayback(playback);
}

json layersToJson(const std::array<Layer, kLayerCount>& source) {
    const std::vector<ClipDef>& clips = clipLibrary();
    json layers = json::array();
    for (const Layer& layer : source) {
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
        bool anyPlayback = false;
        json playback = json::array();
        for (const ClipPlayback& item : layer.playback) {
            anyPlayback = anyPlayback || !item.isDefault();
            playback.push_back(item.isDefault() ? json(nullptr) : playbackToJson(item));
        }
        if (anyPlayback) {
            entry["playback"] = playback;
        }
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
        entry["mods"] = modsToJson(layer.mods);
        entry["fx"] = effectsToJson(layer.fx);
        layers.push_back(entry);
    }
    return layers;
}

void masterFromJson(const json& node, Master& master, std::vector<std::string>* warnings) {
    master = Master{};
    if (!node.is_object()) {
        return;
    }
    master.level = readNumber(node, "level", 1.0f, 0.0f, 1.0f);
    if (node.contains("grade")) {
        master.grade = gradeFromJson(node["grade"]);
    }
    if (node.contains("mods")) {
        modsFromJson(node["mods"], master.mods);
    }
    if (node.contains("fx")) {
        effectsFromJson(node["fx"], master.fx, warnings);
    }
}

void layersFromJson(const json& layers, std::array<Layer, kLayerCount>& result, std::vector<std::string>* warnings) {
    result = std::array<Layer, kLayerCount>{};
    if (!layers.is_array()) {
        return;
    }
    for (size_t l = 0; l < layers.size() && l < static_cast<size_t>(kLayerCount); ++l) {
        const json& entry = layers[l];
        Layer& layer = result[l];
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
        auto playback = entry.find("playback");
        if (playback != entry.end() && playback->is_array()) {
            for (size_t c = 0; c < playback->size() && c < static_cast<size_t>(kColumnCount); ++c) {
                layer.playback[c] = playbackFromJson((*playback)[c]);
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
        if (entry.contains("mods")) {
            modsFromJson(entry["mods"], layer.mods);
        }
        if (entry.contains("fx")) {
            effectsFromJson(entry["fx"], layer.fx, warnings);
        }
    }
}

json modulatorsToJson(const Modulators& modulators) {
    json lfos = json::array();
    for (const Lfo& lfo : modulators.lfos) {
        lfos.push_back({{"shape", lfoShapeName(lfo.shape)}, {"rate", lfo.rate}, {"phase", lfo.phase}});
    }
    json macros = json::array();
    for (const float value : modulators.macros) {
        macros.push_back(value);
    }
    return json{{"lfos", lfos}, {"macros", macros}};
}

void modulatorsFromJson(const json& node, Modulators& modulators) {
    if (!node.is_object()) {
        return;
    }
    const auto lfos = node.find("lfos");
    if (lfos != node.end() && lfos->is_array()) {
        for (size_t i = 0; i < lfos->size() && i < static_cast<size_t>(kLfoCount); ++i) {
            const json& entry = (*lfos)[i];
            if (!entry.is_object()) {
                continue;
            }
            Lfo& lfo = modulators.lfos[i];
            lfo.shape = lfoShapeFromName(readString(entry, "shape"));
            lfo.rate = static_cast<int>(readNumber(entry, "rate", 4.0f, 0.0f, static_cast<float>(kLfoRateCount - 1)));
            lfo.phase = readNumber(entry, "phase", 0.0f, 0.0f, 1.0f);
        }
    }
    const auto macros = node.find("macros");
    if (macros != node.end() && macros->is_array()) {
        for (size_t i = 0; i < macros->size() && i < static_cast<size_t>(kMacroCount); ++i) {
            if ((*macros)[i].is_number()) {
                modulators.macros[i] = std::clamp((*macros)[i].get<float>(), 0.0f, 1.0f);
            }
        }
    }
}

json deckSettingsToJson(const DeckSettings& settings) {
    return json{{"transition", transitionName(settings.transition)}, {"seconds", settings.seconds}, {"beats", settings.beats}, {"syncBeats", settings.syncBeats}, {"autoCycle", settings.autoCycle}, {"cycleBeats", settings.cycleBeats}};
}

DeckSettings deckSettingsFromJson(const json& node) {
    DeckSettings settings;
    settings.transition = transitionFromName(readString(node, "transition"));
    settings.seconds = readNumber(node, "seconds", 1.5f, 0.0f, 10.0f);
    settings.beats = static_cast<int>(readNumber(node, "beats", 4.0f, 1.0f, 32.0f));
    settings.syncBeats = readBool(node, "syncBeats", false);
    settings.autoCycle = readBool(node, "autoCycle", false);
    settings.cycleBeats = static_cast<int>(readNumber(node, "cycleBeats", 16.0f, 4.0f, 128.0f));
    return settings;
}

}

std::string compositionToJson(const Composition& comp) {
    json doc;
    doc["format"] = 2;
    doc["app"] = "NyrexVJ";
    doc["name"] = comp.name;
    doc["bpm"] = comp.bpm;
    doc["beatsPerBar"] = comp.beatsPerBar;
    doc["modulators"] = modulatorsToJson(comp.modulators);
    int resolution = std::clamp(comp.resolution, 0, kResolutionCount - 1);
    doc["resolution"] = json::array({kResolutions[resolution].width, kResolutions[resolution].height});
    doc["master"] = masterToJson(comp.master);
    doc["layers"] = layersToJson(comp.layers);

    json decks;
    decks["active"] = comp.activeDeck;
    decks["settings"] = deckSettingsToJson(comp.deckSettings);
    json slots = json::array();
    for (int i = 0; i < kDeckCount; ++i) {
        const size_t index = static_cast<size_t>(i);
        if (!comp.deckUsed[index] && i != comp.activeDeck) {
            continue;
        }
        json slot;
        slot["slot"] = i;
        slot["name"] = comp.deckNames[index];
        if (i != comp.activeDeck) {
            slot["master"] = masterToJson(comp.decks[index].master);
            slot["layers"] = layersToJson(comp.decks[index].layers);
        }
        slots.push_back(slot);
    }
    decks["slots"] = slots;
    doc["decks"] = decks;
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
    result.beatsPerBar = static_cast<int>(readNumber(doc, "beatsPerBar", 4.0f, static_cast<float>(kMinBeatsPerBar), static_cast<float>(kMaxBeatsPerBar)));
    if (doc.contains("modulators")) {
        modulatorsFromJson(doc["modulators"], result.modulators);
    }
    auto resolution = doc.find("resolution");
    if (resolution != doc.end() && resolution->is_array() && resolution->size() >= 2 && (*resolution)[0].is_number() && (*resolution)[1].is_number()) {
        result.resolution = resolutionIndexFor((*resolution)[0].get<int>(), (*resolution)[1].get<int>());
    }

    auto master = doc.find("master");
    if (master != doc.end()) {
        masterFromJson(*master, result.master, warnings);
    }
    layersFromJson(doc["layers"], result.layers, warnings);

    auto decks = doc.find("decks");
    if (decks != doc.end() && decks->is_object()) {
        result.activeDeck = static_cast<int>(readNumber(*decks, "active", 0.0f, 0.0f, static_cast<float>(kDeckCount - 1)));
        if (decks->contains("settings")) {
            result.deckSettings = deckSettingsFromJson((*decks)["settings"]);
        }
        auto slots = decks->find("slots");
        if (slots != decks->end() && slots->is_array()) {
            for (const json& slot : *slots) {
                if (!slot.is_object()) {
                    continue;
                }
                const int index = static_cast<int>(readNumber(slot, "slot", -1.0f, -1.0f, static_cast<float>(kDeckCount - 1)));
                if (index < 0) {
                    continue;
                }
                const size_t i = static_cast<size_t>(index);
                result.deckUsed[i] = true;
                result.deckNames[i] = readString(slot, "name").substr(0, 40);
                if (index != result.activeDeck && slot.contains("layers")) {
                    DeckData data;
                    if (slot.contains("master")) {
                        masterFromJson(slot["master"], data.master, warnings);
                    }
                    layersFromJson(slot["layers"], data.layers, warnings);
                    result.decks[i] = data;
                }
            }
        }
    }
    result.deckUsed[0] = result.deckUsed[0] || result.activeDeck == 0;
    normalizeDecks(result);

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

bool readUserPresetText(const std::string& name, std::string& text, std::string& error) {
    if (!readTextFile(presetPath(name), text)) {
        error = "Could not read the preset file";
        return false;
    }
    return true;
}

bool parseUserPreset(const std::string& name, const std::string& text, Composition& comp, std::string& error) {
    if (!compositionFromJson(text, comp, error)) {
        return false;
    }
    comp.name = sanitizePresetName(name);
    return true;
}

bool loadUserPreset(const std::string& name, Composition& comp, std::string& error) {
    std::string text;
    if (!readUserPresetText(name, text, error)) {
        return false;
    }
    return parseUserPreset(name, text, comp, error);
}

bool deleteUserPreset(const std::string& name) {
    std::error_code ec;
    return fs::remove(presetPath(name), ec);
}

bool saveSession(const Composition& comp) {
    return writeFileAtomic(fs::path(userDataDir()) / "autosave.json", compositionToJson(comp));
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

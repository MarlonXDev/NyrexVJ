#include "core/plugin_config.hpp"

#include "core/recovery.hpp"

#include <algorithm>
#include <cmath>
#include <system_error>

#include <nlohmann/json.hpp>

namespace nvj {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxConfigBytes = 256 * 1024;

float clampUnit(double value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

std::string textField(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) {
        return std::string();
    }
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return std::string();
    }
    return found->get<std::string>();
}

bool validSourceKey(const std::string& key) {
    std::string plugin;
    std::string source;
    return splitPluginSourceKey(key, plugin, source);
}

bool readLimited(const fs::path& path, std::string& text) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return false;
    }
    const std::uintmax_t bytes = fs::file_size(path, ec);
    if (ec || bytes > kMaxConfigBytes) {
        return false;
    }
    return readWholeFile(path, text);
}

}

bool PluginConfig::isEnabled(const std::string& id) const {
    return std::find(enabled.begin(), enabled.end(), id) != enabled.end();
}

void PluginConfig::setEnabled(const std::string& id, bool on) {
    const auto found = std::find(enabled.begin(), enabled.end(), id);
    if (on && found == enabled.end() && enabled.size() < kMaxPluginConfigEntries) {
        enabled.push_back(id);
    } else if (!on && found != enabled.end()) {
        enabled.erase(found);
    }
}

std::array<float, kPluginParameters> PluginConfig::parametersFor(const std::string& key) const {
    const auto found = parameters.find(key);
    if (found != parameters.end()) {
        return found->second;
    }
    std::array<float, kPluginParameters> defaults{};
    defaults.fill(0.5f);
    return defaults;
}

std::string pluginSourceKey(const std::string& pluginId, const std::string& sourceId) {
    return pluginId + "/" + sourceId;
}

bool splitPluginSourceKey(const std::string& key, std::string& pluginId, std::string& sourceId) {
    const size_t slash = key.find('/');
    if (slash == std::string::npos) {
        return false;
    }
    const std::string plugin = key.substr(0, slash);
    const std::string source = key.substr(slash + 1);
    if (!validPluginId(plugin) || !validPluginId(source)) {
        return false;
    }
    pluginId = plugin;
    sourceId = source;
    return true;
}

fs::path pluginConfigPath(const fs::path& dataDir) {
    return pluginsFolder(dataDir) / "config.json";
}

fs::path pluginSafeListPath(const fs::path& dataDir) {
    return pluginsFolder(dataDir) / "disabled.json";
}

PluginConfig pluginConfigFromJson(const std::string& text) {
    PluginConfig config;
    if (text.size() > kMaxConfigBytes) {
        return config;
    }
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || textField(doc, "format") != "nyrexvj-plugin-settings") {
        return config;
    }
    if (!doc.contains("version") || !doc["version"].is_number_integer() || doc["version"].get<long long>() != 1) {
        return config;
    }
    if (doc.contains("enabled") && doc["enabled"].is_array()) {
        for (const nlohmann::json& item : doc["enabled"]) {
            if (item.is_string() && validPluginId(item.get<std::string>())) {
                config.setEnabled(item.get<std::string>(), true);
            }
        }
    }
    if (doc.contains("modulation") && doc["modulation"].is_array()) {
        for (size_t i = 0; i < config.modulation.size() && i < doc["modulation"].size(); ++i) {
            const nlohmann::json& item = doc["modulation"][i];
            if (item.is_string() && validSourceKey(item.get<std::string>())) {
                config.modulation[i] = item.get<std::string>();
            }
        }
    }
    if (doc.contains("frames") && doc["frames"].is_array()) {
        for (size_t i = 0; i < config.frames.size() && i < doc["frames"].size(); ++i) {
            const nlohmann::json& item = doc["frames"][i];
            if (item.is_string() && validSourceKey(item.get<std::string>())) {
                config.frames[i] = item.get<std::string>();
            }
        }
    }
    if (doc.contains("parameters") && doc["parameters"].is_object()) {
        for (auto it = doc["parameters"].begin(); it != doc["parameters"].end() && config.parameters.size() < kMaxPluginConfigEntries; ++it) {
            if (!validSourceKey(it.key()) || !it.value().is_array()) {
                continue;
            }
            std::array<float, kPluginParameters> values{};
            values.fill(0.5f);
            for (size_t i = 0; i < values.size() && i < it.value().size(); ++i) {
                if (it.value()[i].is_number()) {
                    values[i] = clampUnit(it.value()[i].get<double>());
                }
            }
            config.parameters[it.key()] = values;
        }
    }
    return config;
}

std::string pluginConfigToJson(const PluginConfig& config) {
    nlohmann::json doc;
    doc["format"] = "nyrexvj-plugin-settings";
    doc["version"] = 1;
    doc["enabled"] = config.enabled;
    doc["modulation"] = nlohmann::json::array();
    for (const std::string& key : config.modulation) {
        doc["modulation"].push_back(key);
    }
    doc["frames"] = nlohmann::json::array();
    for (const std::string& key : config.frames) {
        doc["frames"].push_back(key);
    }
    doc["parameters"] = nlohmann::json::object();
    for (const auto& item : config.parameters) {
        doc["parameters"][item.first] = std::vector<float>(item.second.begin(), item.second.end());
    }
    return doc.dump(2) + "\n";
}

PluginConfig readPluginConfig(const fs::path& dataDir) {
    std::string text;
    if (!readLimited(pluginConfigPath(dataDir), text)) {
        return PluginConfig();
    }
    try {
        return pluginConfigFromJson(text);
    } catch (...) {
        return PluginConfig();
    }
}

bool writePluginConfig(const fs::path& dataDir, const PluginConfig& config) {
    std::error_code ec;
    fs::create_directories(pluginsFolder(dataDir), ec);
    return writeFileAtomic(pluginConfigPath(dataDir), pluginConfigToJson(config));
}

std::vector<PluginSafeEntry> pluginSafeListFromJson(const std::string& text) {
    std::vector<PluginSafeEntry> entries;
    if (text.size() > kMaxConfigBytes) {
        return entries;
    }
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || textField(doc, "format") != "nyrexvj-plugin-safe-list") {
        return entries;
    }
    if (!doc.contains("disabled") || !doc["disabled"].is_array()) {
        return entries;
    }
    for (const nlohmann::json& item : doc["disabled"]) {
        if (!item.is_object() || entries.size() >= kMaxPluginConfigEntries) {
            continue;
        }
        PluginSafeEntry entry;
        entry.id = textField(item, "id");
        entry.reason = textField(item, "reason");
        entry.time = item.contains("time") && item["time"].is_number_integer() ? item["time"].get<long long>() : 0;
        if (!validPluginId(entry.id) || inPluginSafeList(entries, entry.id)) {
            continue;
        }
        if (entry.reason.size() > 120) {
            entry.reason.resize(120);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string pluginSafeListToJson(const std::vector<PluginSafeEntry>& entries) {
    nlohmann::json doc;
    doc["format"] = "nyrexvj-plugin-safe-list";
    doc["version"] = 1;
    doc["disabled"] = nlohmann::json::array();
    for (const PluginSafeEntry& entry : entries) {
        nlohmann::json item;
        item["id"] = entry.id;
        item["reason"] = entry.reason;
        item["time"] = entry.time;
        doc["disabled"].push_back(item);
    }
    return doc.dump(2) + "\n";
}

std::vector<PluginSafeEntry> readPluginSafeList(const fs::path& dataDir) {
    std::string text;
    if (!readLimited(pluginSafeListPath(dataDir), text)) {
        return {};
    }
    try {
        return pluginSafeListFromJson(text);
    } catch (...) {
        return {};
    }
}

bool inPluginSafeList(const std::vector<PluginSafeEntry>& entries, const std::string& id) {
    return std::any_of(entries.begin(), entries.end(), [&](const PluginSafeEntry& entry) { return entry.id == id; });
}

bool addToPluginSafeList(const fs::path& dataDir, const std::vector<std::string>& ids, const std::string& reason, long long time) {
    std::vector<PluginSafeEntry> entries = readPluginSafeList(dataDir);
    bool changed = false;
    for (const std::string& id : ids) {
        if (!validPluginId(id) || inPluginSafeList(entries, id) || entries.size() >= kMaxPluginConfigEntries) {
            continue;
        }
        PluginSafeEntry entry;
        entry.id = id;
        entry.reason = reason;
        entry.time = time;
        entries.push_back(std::move(entry));
        changed = true;
    }
    if (!changed) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(pluginsFolder(dataDir), ec);
    return writeFileAtomic(pluginSafeListPath(dataDir), pluginSafeListToJson(entries));
}

bool removeFromPluginSafeList(const fs::path& dataDir, const std::string& id) {
    std::vector<PluginSafeEntry> entries = readPluginSafeList(dataDir);
    const auto found = std::remove_if(entries.begin(), entries.end(), [&](const PluginSafeEntry& entry) { return entry.id == id; });
    if (found == entries.end()) {
        return true;
    }
    entries.erase(found, entries.end());
    return writeFileAtomic(pluginSafeListPath(dataDir), pluginSafeListToJson(entries));
}

}

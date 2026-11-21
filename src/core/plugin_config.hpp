#pragma once

#include "core/plugin_manifest.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nvj {

constexpr size_t kMaxPluginConfigEntries = 128;

struct PluginConfig {
    std::vector<std::string> enabled;
    std::array<std::string, kPluginModulationSlots> modulation;
    std::array<std::string, kPluginFrameSlots> frames;
    std::map<std::string, std::array<float, kPluginParameters>> parameters;

    bool isEnabled(const std::string& id) const;
    void setEnabled(const std::string& id, bool on);
    std::array<float, kPluginParameters> parametersFor(const std::string& key) const;
};

struct PluginSafeEntry {
    std::string id;
    std::string reason;
    long long time = 0;
};

std::string pluginSourceKey(const std::string& pluginId, const std::string& sourceId);
bool splitPluginSourceKey(const std::string& key, std::string& pluginId, std::string& sourceId);

std::filesystem::path pluginConfigPath(const std::filesystem::path& dataDir);
std::filesystem::path pluginSafeListPath(const std::filesystem::path& dataDir);

PluginConfig pluginConfigFromJson(const std::string& text);
std::string pluginConfigToJson(const PluginConfig& config);
PluginConfig readPluginConfig(const std::filesystem::path& dataDir);
bool writePluginConfig(const std::filesystem::path& dataDir, const PluginConfig& config);

std::vector<PluginSafeEntry> pluginSafeListFromJson(const std::string& text);
std::string pluginSafeListToJson(const std::vector<PluginSafeEntry>& entries);
std::vector<PluginSafeEntry> readPluginSafeList(const std::filesystem::path& dataDir);
bool addToPluginSafeList(const std::filesystem::path& dataDir, const std::vector<std::string>& ids, const std::string& reason, long long time);
bool removeFromPluginSafeList(const std::filesystem::path& dataDir, const std::string& id);
bool inPluginSafeList(const std::vector<PluginSafeEntry>& entries, const std::string& id);

}

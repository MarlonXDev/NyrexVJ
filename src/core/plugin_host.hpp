#pragma once

#include "core/live_source.hpp"
#include "core/plugin_config.hpp"
#include "core/plugin_manifest.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

constexpr double kPluginModulationBudget = 0.020;
constexpr double kPluginFrameBudget = 0.100;
constexpr double kPluginCreateBudget = 0.500;
constexpr double kPluginLoadBudget = 1.000;
constexpr double kPluginIdleSeconds = 3.0;
constexpr double kModulationRate = 60.0;
constexpr int kPluginFrameFps = 30;
constexpr int kPluginFrameWidth = 1280;
constexpr int kPluginFrameHeight = 720;
constexpr int kPluginLogsPerSecond = 20;
constexpr int kPluginMaxInstancesPerPlugin = 12;
constexpr int kPluginMaxInstancesTotal = 24;
constexpr int kPluginMaxAbandoned = 16;
constexpr double kPluginMaxDuty = 0.8;
constexpr size_t kPluginMaxErrorText = 200;
constexpr int kPluginSourceKindModulation = 1;
constexpr int kPluginSourceKindFrame = 2;

enum class PluginState { NotEnabled, SafeMode, Incompatible, Loading, Loaded, Failed };

enum class PluginErrorKind { None, Load, Incompatible, CallFailed, Exception, Timeout, BadOutput, Guard, Limit };

const char* pluginErrorKindName(PluginErrorKind kind);

struct PluginLimits {
    int instancesPerPlugin = kPluginMaxInstancesPerPlugin;
    int instancesTotal = kPluginMaxInstancesTotal;
    int abandoned = kPluginMaxAbandoned;
    double maxDuty = kPluginMaxDuty;
};

struct PluginSourceDesc {
    std::string id;
    std::string name;
    std::string key;
    int kind = 0;
    int parameterCount = 0;
    std::array<std::string, kPluginParameters> parameterNames;
};

struct PluginInfo {
    PluginEntry entry;
    PluginState state = PluginState::NotEnabled;
    PluginErrorKind errorKind = PluginErrorKind::None;
    std::string message;
    std::string safeReason;
    std::vector<PluginSourceDesc> sources;
};

struct PluginRunStats {
    bool running = false;
    bool failed = false;
    bool throttled = false;
    PluginErrorKind errorKind = PluginErrorKind::None;
    std::string error;
    double duty = 0.0;
    unsigned long long calls = 0;
    unsigned long long frames = 0;
    double lastCallMs = 0.0;
    double maxCallMs = 0.0;
    double fps = 0.0;
    float value = 0.0f;
};

struct PluginHostSnapshot {
    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float level = 0.0f;
    float kick = 0.0f;
    double beat = 0.0;
    float bpm = 120.0f;
    unsigned beatsPerBar = 4;
};

class PluginHost {
public:
    PluginHost(std::filesystem::path dataDir, bool allowLoading = true, PluginLimits limits = PluginLimits());
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    void rescan();
    void markCrashed(const std::vector<std::string>& ids, long long time);
    void setEnabled(const std::string& id, bool on);
    void clearSafeMode(const std::string& id);

    const std::vector<PluginInfo>& plugins() const;
    const PluginInfo* find(const std::string& id) const;
    const PluginSourceDesc* findSource(const std::string& key) const;
    bool allowLoading() const;
    const PluginLimits& limits() const;
    bool loading() const;

    PluginConfig& config();
    const PluginConfig& config() const;
    bool saveConfig();
    void assignModulation(int slot, const std::string& key);
    void assignFrame(int slot, const std::string& key);
    void setParameter(const std::string& key, int index, float value);

    void publish(const PluginHostSnapshot& snapshot);
    void update();
    float modulation(int slot);
    PluginRunStats modulationStats(int slot) const;
    PluginRunStats runnerStats(const std::string& key) const;
    int liveInstancesOf(const std::string& id) const;
    std::unique_ptr<LiveSource> openFrameSource(const std::string& key, std::string& error);

    int runningInstances() const;
    int abandonedInstances() const;
    int waitForThreads(double seconds);
    void stopAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int pluginThreadsAlive();
std::vector<std::string> recordPluginCrashes(const std::filesystem::path& dataDir, const std::string& report, long long time);

}

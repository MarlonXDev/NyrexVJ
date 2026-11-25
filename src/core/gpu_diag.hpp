#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace nvj::gpudiag {

constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr int kMinBudgetMb = 256;
constexpr int kMaxBudgetMb = 65536;
constexpr int kMinSamplesForPercentile = 120;
constexpr int kMaxPressureLevel = 4;

enum class MemorySource { None, NvidiaNvx, AmdAti };

const char* memorySourceName(MemorySource source);

struct NvxRaw {
    long long dedicatedKb = -1;
    long long availableKb = -1;
    long long evictedKb = -1;
    long long evictionCount = -1;
};

struct DriverMemory {
    MemorySource source = MemorySource::None;
    bool hasTotal = false;
    bool hasEvictions = false;
    std::uint64_t totalBytes = 0;
    std::uint64_t freeBytes = 0;
    std::uint64_t evictedBytes = 0;
    std::uint64_t evictionCount = 0;

    bool measured() const { return source != MemorySource::None; }
    std::uint64_t usedBytes() const { return hasTotal && totalBytes > freeBytes ? totalBytes - freeBytes : 0; }
    double freeFraction() const { return hasTotal && totalBytes > 0 ? static_cast<double>(freeBytes) / static_cast<double>(totalBytes) : 1.0; }
};

DriverMemory fromNvx(const NvxRaw& raw);
DriverMemory fromAti(const std::array<long long, 4>& kilobytes);

class EvictionWatch {
public:
    static constexpr double kWindowSeconds = 30.0;

    void update(const DriverMemory& memory, double nowSeconds);
    std::uint64_t recentCount() const { return recentCount_; }
    std::uint64_t recentBytes() const { return recentBytes_; }
    void reset();

private:
    struct Mark {
        double time = 0.0;
        std::uint64_t count = 0;
        std::uint64_t bytes = 0;
    };

    std::deque<Mark> marks_;
    std::uint64_t recentCount_ = 0;
    std::uint64_t recentBytes_ = 0;
};

class TrafficMeter {
public:
    static constexpr int kSeconds = 12;

    void add(std::uint64_t bytes);
    void advance(double nowSeconds);
    double bytesPerSecond() const;
    double peakBytesPerSecond() const;
    std::uint64_t total() const { return total_; }
    void reset();

private:
    std::array<std::uint64_t, kSeconds> buckets_{};
    long long second_ = -1;
    std::uint64_t total_ = 0;
};

class WindowStats {
public:
    static constexpr int kSeconds = 60;
    static constexpr int kBuckets = 256;
    static constexpr double kStepMs = 0.25;

    WindowStats();
    void add(double milliseconds);
    double percentile(double fraction) const;
    double maxMs() const;
    double averageMs() const;
    std::uint64_t count() const;
    double coveredSeconds() const;
    void reset();

private:
    struct Slot {
        std::array<std::uint32_t, kBuckets + 1> counts{};
        double maxMs = 0.0;
        double overflowMaxMs = 0.0;
        double sumMs = 0.0;
        std::uint32_t total = 0;
    };

    void advanceTo(long long second);

    std::array<Slot, kSeconds> slots_;
    double clock_ = 0.0;
    long long second_ = 0;
};

class FrameWatch {
public:
    static constexpr int kSeconds = 60;
    static constexpr double kStallMs = 2000.0;
    static constexpr double kLateFactor = 1.5;

    static int droppedIn(double intervalMs, double budgetMs);

    void setBudgetMs(double milliseconds);
    double budgetMs() const { return budgetMs_; }
    void add(double intervalMs);
    std::uint64_t frames() const { return frames_; }
    std::uint64_t late() const { return late_; }
    std::uint64_t dropped() const { return dropped_; }
    std::uint64_t stalls() const { return stalls_; }
    std::uint64_t lateRecent() const;
    std::uint64_t droppedRecent() const;
    void reset();

private:
    void advanceTo(long long second);

    double budgetMs_ = 1000.0 / 60.0;
    double clock_ = 0.0;
    long long second_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t late_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t stalls_ = 0;
    std::array<std::uint32_t, kSeconds> lateBuckets_{};
    std::array<std::uint32_t, kSeconds> droppedBuckets_{};
};

enum class WarningCode {
    FrameTime,
    DroppedFrames,
    MemoryNearBudget,
    MemoryOverBudget,
    ResidentOverBudget,
    DriverMemoryLow,
    DriverEvictions,
    UploadTraffic
};

constexpr int kWarningCodeCount = 8;

enum class Severity { Info, Warning, Critical };

const char* warningId(WarningCode code);

struct Thresholds {
    double p99Factor = 1.5;
    int droppedPerMinute = 5;
    double memoryFraction = 0.85;
    double driverFreeFraction = 0.10;
    std::uint64_t driverFreeCriticalBytes = 256 * kMiB;
    std::uint64_t driverEvictionsRecent = 1;
    double uploadBytesPerSecond = 1.5e9;
};

Thresholds clampThresholds(Thresholds thresholds);

struct Inputs {
    double budgetMs = 1000.0 / 60.0;
    double p99Ms = 0.0;
    std::uint64_t frames = 0;
    std::uint64_t droppedRecent = 0;
    std::uint64_t estimateBytes = 0;
    std::uint64_t budgetBytes = 0;
    std::uint64_t residentBytes = 0;
    DriverMemory driver;
    std::uint64_t driverEvictionsRecent = 0;
    double uploadBytesPerSecond = 0.0;
};

struct Warning {
    WarningCode code = WarningCode::FrameTime;
    Severity severity = Severity::Info;
    double value = 0.0;
    double limit = 0.0;
};

std::vector<Warning> evaluate(const Inputs& inputs, const Thresholds& thresholds);

class WarningTracker {
public:
    static constexpr double kOnSeconds = 2.0;
    static constexpr double kOffSeconds = 6.0;

    void update(const std::vector<Warning>& raw, double dtSeconds);
    const std::vector<Warning>& active() const { return active_; }
    std::uint64_t raised(WarningCode code) const;
    bool has(WarningCode code) const;
    Severity worst() const;
    void reset();

private:
    struct Entry {
        double on = 0.0;
        double off = 0.0;
        bool active = false;
        Warning last;
        std::uint64_t raised = 0;
    };

    void rebuild();

    std::array<Entry, kWarningCodeCount> entries_{};
    std::vector<Warning> active_;
};

struct Sample {
    double budgetMs = 1000.0 / 60.0;
    std::uint64_t estimateBytes = 0;
    std::uint64_t budgetBytes = 0;
    std::uint64_t residentBytes = 0;
    DriverMemory driver;
    double uploadBytesPerSecond = 0.0;
};

class Monitor {
public:
    void addFrame(double intervalMs);
    void update(double dtSeconds, const Sample& sample);
    void setThresholds(const Thresholds& thresholds) { thresholds_ = clampThresholds(thresholds); }
    const Thresholds& thresholds() const { return thresholds_; }
    const WindowStats& window() const { return window_; }
    const FrameWatch& watch() const { return watch_; }
    const EvictionWatch& evictions() const { return evictions_; }
    const WarningTracker& tracker() const { return tracker_; }
    double p95() const { return window_.percentile(0.95); }
    double p99() const { return window_.percentile(0.99); }
    bool percentilesReliable() const { return window_.count() >= static_cast<std::uint64_t>(kMinSamplesForPercentile); }
    void reset();

private:
    Thresholds thresholds_;
    WindowStats window_;
    FrameWatch watch_;
    EvictionWatch evictions_;
    WarningTracker tracker_;
    double clock_ = 0.0;
};

enum class ResourceClass { PreparedVideo = 0, IdleImage = 1, Scene3d = 2 };

const char* resourceClassName(ResourceClass kind);

struct Candidate {
    ResourceClass kind = ResourceClass::IdleImage;
    std::uint64_t bytes = 0;
    double idleSeconds = 0.0;
    bool inUse = false;
    bool pinned = false;
};

struct EvictionRequest {
    std::uint64_t currentBytes = 0;
    std::uint64_t targetBytes = 0;
    double minIdleSeconds = 5.0;
    std::size_t maxCount = 16;
    std::uint64_t minBytes = kMiB;
};

std::uint64_t lowWater(std::uint64_t budgetBytes);
std::uint64_t recoveryWater(std::uint64_t budgetBytes);
std::uint64_t clampBudgetBytes(std::uint64_t bytes);
std::vector<std::size_t> planEviction(const std::vector<Candidate>& candidates, const EvictionRequest& request);

class PressureLadder {
public:
    static constexpr double kRaiseSeconds = 3.0;
    static constexpr double kLowerSeconds = 12.0;
    static constexpr double kLowerFraction = 0.75;

    int update(double dtSeconds, std::uint64_t estimateBytes, std::uint64_t budgetBytes);
    int level() const { return level_; }
    void reset();

private:
    int level_ = 0;
    double over_ = 0.0;
    double under_ = 0.0;
};

bool preparingAllowed(int level);
double minIdleSecondsFor(int level);
float layerScaleCap(int level);

}

#include "core/gpu_diag.hpp"

#include <algorithm>
#include <cmath>

namespace nvj::gpudiag {

namespace {

constexpr std::uint64_t kKb = 1024ull;

long long wrap(long long value, long long size) {
    const long long remainder = value % size;
    return remainder < 0 ? remainder + size : remainder;
}

double clampValue(double value, double low, double high, double fallback) {
    if (!(value == value)) {
        return fallback;
    }
    return std::clamp(value, low, high);
}

}

const char* memorySourceName(MemorySource source) {
    switch (source) {
    case MemorySource::NvidiaNvx:
        return "GL_NVX_gpu_memory_info";
    case MemorySource::AmdAti:
        return "GL_ATI_meminfo";
    case MemorySource::None:
        break;
    }
    return "none";
}

DriverMemory fromNvx(const NvxRaw& raw) {
    DriverMemory memory;
    if (raw.dedicatedKb <= 0 || raw.availableKb < 0) {
        return memory;
    }
    memory.source = MemorySource::NvidiaNvx;
    memory.hasTotal = true;
    memory.totalBytes = static_cast<std::uint64_t>(raw.dedicatedKb) * kKb;
    memory.freeBytes = static_cast<std::uint64_t>(std::min(raw.availableKb, raw.dedicatedKb)) * kKb;
    if (raw.evictionCount >= 0 && raw.evictedKb >= 0) {
        memory.hasEvictions = true;
        memory.evictedBytes = static_cast<std::uint64_t>(raw.evictedKb) * kKb;
        memory.evictionCount = static_cast<std::uint64_t>(raw.evictionCount);
    }
    return memory;
}

DriverMemory fromAti(const std::array<long long, 4>& kilobytes) {
    DriverMemory memory;
    if (kilobytes[0] < 0) {
        return memory;
    }
    memory.source = MemorySource::AmdAti;
    memory.freeBytes = static_cast<std::uint64_t>(kilobytes[0]) * kKb;
    return memory;
}

void EvictionWatch::reset() {
    marks_.clear();
    recentCount_ = 0;
    recentBytes_ = 0;
}

void EvictionWatch::update(const DriverMemory& memory, double nowSeconds) {
    if (!memory.hasEvictions) {
        reset();
        return;
    }
    if (!marks_.empty() && (memory.evictionCount < marks_.back().count || memory.evictedBytes < marks_.back().bytes)) {
        marks_.clear();
    }
    if (!marks_.empty() && nowSeconds - marks_.back().time < 0.5 && memory.evictionCount == marks_.back().count && memory.evictedBytes == marks_.back().bytes) {
        marks_.back().time = nowSeconds;
    } else {
        marks_.push_back({nowSeconds, memory.evictionCount, memory.evictedBytes});
    }
    while (marks_.size() > 1 && nowSeconds - marks_[1].time >= kWindowSeconds) {
        marks_.pop_front();
    }
    recentCount_ = marks_.back().count - marks_.front().count;
    recentBytes_ = marks_.back().bytes - marks_.front().bytes;
}

void TrafficMeter::reset() {
    buckets_.fill(0);
    second_ = -1;
    total_ = 0;
}

void TrafficMeter::advance(double nowSeconds) {
    if (!(nowSeconds >= 0.0)) {
        return;
    }
    const long long second = static_cast<long long>(std::floor(nowSeconds));
    if (second_ < 0) {
        second_ = second;
        return;
    }
    if (second <= second_) {
        return;
    }
    if (second - second_ >= kSeconds) {
        buckets_.fill(0);
    } else {
        for (long long s = second_ + 1; s <= second; ++s) {
            buckets_[static_cast<size_t>(wrap(s, kSeconds))] = 0;
        }
    }
    second_ = second;
}

void TrafficMeter::add(std::uint64_t bytes) {
    if (second_ < 0) {
        second_ = 0;
    }
    buckets_[static_cast<size_t>(wrap(second_, kSeconds))] += bytes;
    total_ += bytes;
}

double TrafficMeter::bytesPerSecond() const {
    if (second_ < 1) {
        return 0.0;
    }
    return static_cast<double>(buckets_[static_cast<size_t>(wrap(second_ - 1, kSeconds))]);
}

double TrafficMeter::peakBytesPerSecond() const {
    if (second_ < 1) {
        return 0.0;
    }
    std::uint64_t peak = 0;
    const long long reach = std::min<long long>(kSeconds - 1, second_);
    for (long long i = 1; i <= reach; ++i) {
        peak = std::max(peak, buckets_[static_cast<size_t>(wrap(second_ - i, kSeconds))]);
    }
    return static_cast<double>(peak);
}

WindowStats::WindowStats() = default;

void WindowStats::reset() {
    for (Slot& slot : slots_) {
        slot = Slot{};
    }
    clock_ = 0.0;
    second_ = 0;
}

void WindowStats::advanceTo(long long second) {
    if (second <= second_) {
        return;
    }
    if (second - second_ >= kSeconds) {
        for (Slot& slot : slots_) {
            slot = Slot{};
        }
    } else {
        for (long long s = second_ + 1; s <= second; ++s) {
            slots_[static_cast<size_t>(wrap(s, kSeconds))] = Slot{};
        }
    }
    second_ = second;
}

void WindowStats::add(double milliseconds) {
    if (!(milliseconds >= 0.0) || !std::isfinite(milliseconds)) {
        return;
    }
    clock_ += milliseconds / 1000.0;
    advanceTo(static_cast<long long>(std::floor(clock_)));
    Slot& slot = slots_[static_cast<size_t>(wrap(second_, kSeconds))];
    const int bucket = std::min(kBuckets, static_cast<int>(milliseconds / kStepMs));
    ++slot.counts[static_cast<size_t>(bucket)];
    if (bucket == kBuckets) {
        slot.overflowMaxMs = std::max(slot.overflowMaxMs, milliseconds);
    }
    slot.maxMs = std::max(slot.maxMs, milliseconds);
    slot.sumMs += milliseconds;
    ++slot.total;
}

std::uint64_t WindowStats::count() const {
    std::uint64_t total = 0;
    for (const Slot& slot : slots_) {
        total += slot.total;
    }
    return total;
}

double WindowStats::maxMs() const {
    double result = 0.0;
    for (const Slot& slot : slots_) {
        result = std::max(result, slot.maxMs);
    }
    return result;
}

double WindowStats::averageMs() const {
    double sum = 0.0;
    std::uint64_t total = 0;
    for (const Slot& slot : slots_) {
        sum += slot.sumMs;
        total += slot.total;
    }
    return total == 0 ? 0.0 : sum / static_cast<double>(total);
}

double WindowStats::coveredSeconds() const {
    return std::min(clock_, static_cast<double>(kSeconds));
}

double WindowStats::percentile(double fraction) const {
    const std::uint64_t total = count();
    if (total == 0) {
        return 0.0;
    }
    const double clamped = clampValue(fraction, 0.0, 1.0, 1.0);
    const std::uint64_t rank = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(clamped * static_cast<double>(total))));
    std::array<std::uint64_t, kBuckets + 1> merged{};
    double overflowMax = 0.0;
    for (const Slot& slot : slots_) {
        for (size_t i = 0; i < merged.size(); ++i) {
            merged[i] += slot.counts[i];
        }
        overflowMax = std::max(overflowMax, slot.overflowMaxMs);
    }
    std::uint64_t running = 0;
    for (size_t i = 0; i < merged.size(); ++i) {
        running += merged[i];
        if (running >= rank) {
            if (i == static_cast<size_t>(kBuckets)) {
                return overflowMax;
            }
            return std::min(static_cast<double>(i + 1) * kStepMs, maxMs());
        }
    }
    return maxMs();
}

int FrameWatch::droppedIn(double intervalMs, double budgetMs) {
    if (!(budgetMs > 0.0) || !(intervalMs > budgetMs * kLateFactor)) {
        return 0;
    }
    return std::max(1, static_cast<int>(intervalMs / budgetMs + 0.5) - 1);
}

void FrameWatch::setBudgetMs(double milliseconds) {
    if (milliseconds > 0.0 && std::isfinite(milliseconds)) {
        budgetMs_ = milliseconds;
    }
}

void FrameWatch::reset() {
    clock_ = 0.0;
    second_ = 0;
    frames_ = 0;
    late_ = 0;
    dropped_ = 0;
    stalls_ = 0;
    lateBuckets_.fill(0);
    droppedBuckets_.fill(0);
}

void FrameWatch::advanceTo(long long second) {
    if (second <= second_) {
        return;
    }
    if (second - second_ >= kSeconds) {
        lateBuckets_.fill(0);
        droppedBuckets_.fill(0);
    } else {
        for (long long s = second_ + 1; s <= second; ++s) {
            lateBuckets_[static_cast<size_t>(wrap(s, kSeconds))] = 0;
            droppedBuckets_[static_cast<size_t>(wrap(s, kSeconds))] = 0;
        }
    }
    second_ = second;
}

void FrameWatch::add(double intervalMs) {
    if (!(intervalMs >= 0.0) || !std::isfinite(intervalMs)) {
        return;
    }
    clock_ += intervalMs / 1000.0;
    advanceTo(static_cast<long long>(std::floor(clock_)));
    if (intervalMs >= kStallMs) {
        ++stalls_;
        return;
    }
    ++frames_;
    const int lost = droppedIn(intervalMs, budgetMs_);
    if (lost > 0) {
        ++late_;
        dropped_ += static_cast<std::uint64_t>(lost);
        const size_t slot = static_cast<size_t>(wrap(second_, kSeconds));
        ++lateBuckets_[slot];
        droppedBuckets_[slot] += static_cast<std::uint32_t>(lost);
    }
}

std::uint64_t FrameWatch::lateRecent() const {
    std::uint64_t total = 0;
    for (const std::uint32_t value : lateBuckets_) {
        total += value;
    }
    return total;
}

std::uint64_t FrameWatch::droppedRecent() const {
    std::uint64_t total = 0;
    for (const std::uint32_t value : droppedBuckets_) {
        total += value;
    }
    return total;
}

const char* warningId(WarningCode code) {
    switch (code) {
    case WarningCode::FrameTime:
        return "frame-time";
    case WarningCode::DroppedFrames:
        return "dropped-frames";
    case WarningCode::MemoryNearBudget:
        return "memory-near-budget";
    case WarningCode::MemoryOverBudget:
        return "memory-over-budget";
    case WarningCode::ResidentOverBudget:
        return "resident-over-budget";
    case WarningCode::DriverMemoryLow:
        return "driver-memory-low";
    case WarningCode::DriverEvictions:
        return "driver-evictions";
    case WarningCode::UploadTraffic:
        return "upload-traffic";
    }
    return "unknown";
}

Thresholds clampThresholds(Thresholds thresholds) {
    const Thresholds defaults;
    thresholds.p99Factor = clampValue(thresholds.p99Factor, 1.1, 6.0, defaults.p99Factor);
    thresholds.droppedPerMinute = std::clamp(thresholds.droppedPerMinute, 1, 600);
    thresholds.memoryFraction = clampValue(thresholds.memoryFraction, 0.5, 0.99, defaults.memoryFraction);
    thresholds.driverFreeFraction = clampValue(thresholds.driverFreeFraction, 0.01, 0.5, defaults.driverFreeFraction);
    thresholds.driverFreeCriticalBytes = std::clamp<std::uint64_t>(thresholds.driverFreeCriticalBytes, 16 * kMiB, 8192 * kMiB);
    thresholds.driverEvictionsRecent = std::clamp<std::uint64_t>(thresholds.driverEvictionsRecent, 1, 1000);
    thresholds.uploadBytesPerSecond = clampValue(thresholds.uploadBytesPerSecond, 1.0e8, 1.0e11, defaults.uploadBytesPerSecond);
    return thresholds;
}

std::vector<Warning> evaluate(const Inputs& inputs, const Thresholds& rawThresholds) {
    const Thresholds thresholds = clampThresholds(rawThresholds);
    std::vector<Warning> result;
    const auto add = [&](WarningCode code, Severity severity, double value, double limit) {
        Warning warning;
        warning.code = code;
        warning.severity = severity;
        warning.value = value;
        warning.limit = limit;
        result.push_back(warning);
    };

    if (inputs.budgetMs > 0.0 && inputs.frames >= static_cast<std::uint64_t>(kMinSamplesForPercentile)) {
        const double limit = thresholds.p99Factor * inputs.budgetMs;
        if (inputs.p99Ms > limit) {
            add(WarningCode::FrameTime, inputs.p99Ms > limit * 2.0 ? Severity::Critical : Severity::Warning, inputs.p99Ms, limit);
        }
    }
    const std::uint64_t dropLimit = static_cast<std::uint64_t>(thresholds.droppedPerMinute);
    if (inputs.droppedRecent >= dropLimit) {
        add(WarningCode::DroppedFrames, inputs.droppedRecent >= dropLimit * 6 ? Severity::Critical : Severity::Warning, static_cast<double>(inputs.droppedRecent), static_cast<double>(dropLimit));
    }
    if (inputs.budgetBytes > 0) {
        const double estimate = static_cast<double>(inputs.estimateBytes);
        const double budget = static_cast<double>(inputs.budgetBytes);
        if (inputs.estimateBytes > inputs.budgetBytes) {
            add(WarningCode::MemoryOverBudget, estimate > budget * 1.25 ? Severity::Critical : Severity::Warning, estimate, budget);
        } else if (estimate >= budget * thresholds.memoryFraction) {
            add(WarningCode::MemoryNearBudget, Severity::Info, estimate, budget * thresholds.memoryFraction);
        }
        if (inputs.residentBytes > inputs.budgetBytes) {
            add(WarningCode::ResidentOverBudget, Severity::Critical, static_cast<double>(inputs.residentBytes), budget);
        }
    }
    if (inputs.driver.measured()) {
        const double freeBytes = static_cast<double>(inputs.driver.freeBytes);
        const bool critical = inputs.driver.freeBytes < thresholds.driverFreeCriticalBytes;
        if (inputs.driver.hasTotal) {
            const double limit = thresholds.driverFreeFraction * static_cast<double>(inputs.driver.totalBytes);
            if (freeBytes < limit) {
                add(WarningCode::DriverMemoryLow, critical ? Severity::Critical : Severity::Warning, freeBytes, limit);
            }
        } else if (critical) {
            add(WarningCode::DriverMemoryLow, Severity::Warning, freeBytes, static_cast<double>(thresholds.driverFreeCriticalBytes));
        }
    }
    if (inputs.driver.hasEvictions && inputs.driverEvictionsRecent >= thresholds.driverEvictionsRecent) {
        add(WarningCode::DriverEvictions, inputs.driverEvictionsRecent >= 20 ? Severity::Critical : Severity::Warning, static_cast<double>(inputs.driverEvictionsRecent), static_cast<double>(thresholds.driverEvictionsRecent));
    }
    if (inputs.uploadBytesPerSecond > thresholds.uploadBytesPerSecond) {
        add(WarningCode::UploadTraffic, Severity::Info, inputs.uploadBytesPerSecond, thresholds.uploadBytesPerSecond);
    }
    return result;
}

void WarningTracker::reset() {
    entries_ = {};
    active_.clear();
}

void WarningTracker::update(const std::vector<Warning>& raw, double dtSeconds) {
    const double dt = std::max(dtSeconds, 0.0);
    std::array<const Warning*, kWarningCodeCount> present{};
    for (const Warning& warning : raw) {
        present[static_cast<size_t>(warning.code)] = &warning;
    }
    for (size_t i = 0; i < entries_.size(); ++i) {
        Entry& entry = entries_[i];
        if (present[i] != nullptr) {
            entry.on += dt;
            entry.off = 0.0;
            if (entry.active) {
                entry.last = *present[i];
            } else if (entry.on >= kOnSeconds) {
                entry.active = true;
                entry.last = *present[i];
                ++entry.raised;
            }
        } else {
            entry.on = 0.0;
            if (entry.active) {
                entry.off += dt;
                if (entry.off >= kOffSeconds) {
                    entry.active = false;
                    entry.off = 0.0;
                }
            }
        }
    }
    rebuild();
}

void WarningTracker::rebuild() {
    active_.clear();
    for (const Entry& entry : entries_) {
        if (entry.active) {
            active_.push_back(entry.last);
        }
    }
    std::stable_sort(active_.begin(), active_.end(), [](const Warning& a, const Warning& b) {
        if (a.severity != b.severity) {
            return static_cast<int>(a.severity) > static_cast<int>(b.severity);
        }
        return static_cast<int>(a.code) < static_cast<int>(b.code);
    });
}

std::uint64_t WarningTracker::raised(WarningCode code) const {
    return entries_[static_cast<size_t>(code)].raised;
}

bool WarningTracker::has(WarningCode code) const {
    return entries_[static_cast<size_t>(code)].active;
}

Severity WarningTracker::worst() const {
    Severity result = Severity::Info;
    for (const Warning& warning : active_) {
        if (static_cast<int>(warning.severity) > static_cast<int>(result)) {
            result = warning.severity;
        }
    }
    return result;
}

void Monitor::reset() {
    window_.reset();
    watch_.reset();
    evictions_.reset();
    tracker_.reset();
    clock_ = 0.0;
}

void Monitor::addFrame(double intervalMs) {
    window_.add(intervalMs);
    watch_.add(intervalMs);
}

void Monitor::update(double dtSeconds, const Sample& sample) {
    clock_ += std::max(dtSeconds, 0.0);
    watch_.setBudgetMs(sample.budgetMs);
    evictions_.update(sample.driver, clock_);
    Inputs inputs;
    inputs.budgetMs = sample.budgetMs;
    inputs.p99Ms = window_.percentile(0.99);
    inputs.frames = window_.count();
    inputs.droppedRecent = watch_.droppedRecent();
    inputs.estimateBytes = sample.estimateBytes;
    inputs.budgetBytes = sample.budgetBytes;
    inputs.residentBytes = sample.residentBytes;
    inputs.driver = sample.driver;
    inputs.driverEvictionsRecent = evictions_.recentCount();
    inputs.uploadBytesPerSecond = sample.uploadBytesPerSecond;
    tracker_.update(evaluate(inputs, thresholds_), dtSeconds);
}

const char* resourceClassName(ResourceClass kind) {
    switch (kind) {
    case ResourceClass::PreparedVideo:
        return "prepared video";
    case ResourceClass::IdleImage:
        return "idle image";
    case ResourceClass::Scene3d:
        return "3D scene";
    }
    return "unknown";
}

std::uint64_t lowWater(std::uint64_t budgetBytes) {
    return budgetBytes / 100 * 85;
}

std::uint64_t recoveryWater(std::uint64_t budgetBytes) {
    return budgetBytes / 100 * 70;
}

std::uint64_t clampBudgetBytes(std::uint64_t bytes) {
    if (bytes == 0) {
        return 0;
    }
    return std::clamp<std::uint64_t>(bytes, static_cast<std::uint64_t>(kMinBudgetMb) * kMiB, static_cast<std::uint64_t>(kMaxBudgetMb) * kMiB);
}

std::vector<std::size_t> planEviction(const std::vector<Candidate>& candidates, const EvictionRequest& request) {
    std::vector<std::size_t> chosen;
    if (request.currentBytes <= request.targetBytes) {
        return chosen;
    }
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& candidate = candidates[i];
        if (candidate.inUse || candidate.pinned || candidate.bytes == 0 || candidate.bytes < request.minBytes || !(candidate.idleSeconds >= request.minIdleSeconds)) {
            continue;
        }
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const Candidate& x = candidates[a];
        const Candidate& y = candidates[b];
        if (x.kind != y.kind) {
            return static_cast<int>(x.kind) < static_cast<int>(y.kind);
        }
        if (x.idleSeconds != y.idleSeconds) {
            return x.idleSeconds > y.idleSeconds;
        }
        if (x.bytes != y.bytes) {
            return x.bytes > y.bytes;
        }
        return a < b;
    });
    std::uint64_t remaining = request.currentBytes;
    for (const std::size_t index : order) {
        if (remaining <= request.targetBytes || chosen.size() >= request.maxCount) {
            break;
        }
        chosen.push_back(index);
        remaining -= std::min(remaining, candidates[index].bytes);
    }
    return chosen;
}

void PressureLadder::reset() {
    level_ = 0;
    over_ = 0.0;
    under_ = 0.0;
}

int PressureLadder::update(double dtSeconds, std::uint64_t estimateBytes, std::uint64_t budgetBytes) {
    if (budgetBytes == 0) {
        reset();
        return 0;
    }
    const double dt = std::max(dtSeconds, 0.0);
    if (estimateBytes > budgetBytes) {
        over_ += dt;
        under_ = 0.0;
        if (over_ >= kRaiseSeconds) {
            over_ = 0.0;
            level_ = std::min(level_ + 1, kMaxPressureLevel);
        }
    } else if (static_cast<double>(estimateBytes) < kLowerFraction * static_cast<double>(budgetBytes)) {
        under_ += dt;
        over_ = 0.0;
        if (under_ >= kLowerSeconds) {
            under_ = 0.0;
            level_ = std::max(level_ - 1, 0);
        }
    } else {
        over_ = 0.0;
        under_ = 0.0;
    }
    return level_;
}

bool preparingAllowed(int level) {
    return level < 1;
}

double minIdleSecondsFor(int level) {
    return level >= 2 ? 1.0 : 5.0;
}

float layerScaleCap(int level) {
    if (level >= 4) {
        return 0.5f;
    }
    if (level >= 3) {
        return 0.75f;
    }
    return 1.0f;
}

}

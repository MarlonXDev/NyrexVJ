#include "check.hpp"

#include "core/gpu_diag.hpp"
#include "core/settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace nvj::gpudiag;

bool near(double a, double b, double tolerance = 1e-9) {
    return std::fabs(a - b) <= tolerance;
}

void testDriverMemory() {
    NvxRaw raw;
    raw.dedicatedKb = 8 * 1024 * 1024;
    raw.availableKb = 3 * 1024 * 1024;
    raw.evictedKb = 2048;
    raw.evictionCount = 3;
    const DriverMemory nvx = fromNvx(raw);
    CHECK(nvx.measured());
    CHECK(nvx.source == MemorySource::NvidiaNvx);
    CHECK(nvx.hasTotal);
    CHECK(nvx.totalBytes == 8ull * 1024 * 1024 * 1024);
    CHECK(nvx.freeBytes == 3ull * 1024 * 1024 * 1024);
    CHECK(nvx.usedBytes() == 5ull * 1024 * 1024 * 1024);
    CHECK(near(nvx.freeFraction(), 0.375));
    CHECK(nvx.hasEvictions);
    CHECK(nvx.evictedBytes == 2048ull * 1024);
    CHECK(nvx.evictionCount == 3);

    NvxRaw over = raw;
    over.availableKb = raw.dedicatedKb + 5000;
    const DriverMemory clamped = fromNvx(over);
    CHECK(clamped.freeBytes == clamped.totalBytes);
    CHECK(clamped.usedBytes() == 0);

    NvxRaw noEvictions = raw;
    noEvictions.evictionCount = -1;
    CHECK(!fromNvx(noEvictions).hasEvictions);
    CHECK(fromNvx(noEvictions).measured());

    CHECK(!fromNvx(NvxRaw{}).measured());
    NvxRaw zero = raw;
    zero.dedicatedKb = 0;
    CHECK(!fromNvx(zero).measured());
    NvxRaw negative = raw;
    negative.availableKb = -1;
    CHECK(!fromNvx(negative).measured());
    CHECK(fromNvx(NvxRaw{}).source == MemorySource::None);

    const DriverMemory ati = fromAti({1024 * 1024, 512, 0, 0});
    CHECK(ati.measured());
    CHECK(ati.source == MemorySource::AmdAti);
    CHECK(!ati.hasTotal);
    CHECK(ati.freeBytes == 1024ull * 1024 * 1024);
    CHECK(ati.usedBytes() == 0);
    CHECK(near(ati.freeFraction(), 1.0));
    CHECK(!ati.hasEvictions);
    CHECK(!fromAti({-1, -1, -1, -1}).measured());
    CHECK(fromAti({0, 0, 0, 0}).measured());
    CHECK(std::string(memorySourceName(MemorySource::None)) == "none");
    CHECK(std::string(memorySourceName(MemorySource::NvidiaNvx)) == "GL_NVX_gpu_memory_info");
    CHECK(std::string(memorySourceName(MemorySource::AmdAti)) == "GL_ATI_meminfo");
}

DriverMemory evictionReading(std::uint64_t count, std::uint64_t kb) {
    NvxRaw raw;
    raw.dedicatedKb = 4 * 1024 * 1024;
    raw.availableKb = 2 * 1024 * 1024;
    raw.evictionCount = static_cast<long long>(count);
    raw.evictedKb = static_cast<long long>(kb);
    return fromNvx(raw);
}

void testEvictionWatch() {
    EvictionWatch watch;
    CHECK(watch.recentCount() == 0);
    watch.update(evictionReading(10, 100), 0.0);
    CHECK(watch.recentCount() == 0);
    watch.update(evictionReading(14, 500), 1.0);
    CHECK(watch.recentCount() == 4);
    CHECK(watch.recentBytes() == 400ull * 1024);
    watch.update(evictionReading(14, 500), 20.0);
    CHECK(watch.recentCount() == 4);
    watch.update(evictionReading(14, 500), 35.0);
    CHECK(watch.recentCount() == 0);
    watch.update(evictionReading(20, 900), 36.0);
    CHECK(watch.recentCount() == 6);
    watch.update(evictionReading(2, 10), 37.0);
    CHECK(watch.recentCount() == 0);
    watch.update(evictionReading(5, 40), 38.0);
    CHECK(watch.recentCount() == 3);

    DriverMemory none;
    watch.update(none, 39.0);
    CHECK(watch.recentCount() == 0);
    watch.reset();
    CHECK(watch.recentBytes() == 0);

    EvictionWatch dense;
    for (int i = 0; i < 6000; ++i) {
        dense.update(evictionReading(static_cast<std::uint64_t>(i / 60), 0), i / 60.0);
    }
    CHECK(dense.recentCount() >= 28 && dense.recentCount() <= 31);
}

void testTraffic() {
    TrafficMeter meter;
    CHECK(near(meter.bytesPerSecond(), 0.0));
    meter.advance(0.0);
    meter.add(1000);
    meter.add(500);
    CHECK(near(meter.bytesPerSecond(), 0.0));
    meter.advance(1.2);
    CHECK(near(meter.bytesPerSecond(), 1500.0));
    meter.add(200);
    meter.advance(2.5);
    CHECK(near(meter.bytesPerSecond(), 200.0));
    CHECK(near(meter.peakBytesPerSecond(), 1500.0));
    CHECK(meter.total() == 1700);
    meter.advance(4.0);
    CHECK(near(meter.bytesPerSecond(), 0.0));
    CHECK(near(meter.peakBytesPerSecond(), 1500.0));
    meter.advance(12.0);
    CHECK(near(meter.peakBytesPerSecond(), 200.0));
    meter.advance(13.0);
    CHECK(near(meter.peakBytesPerSecond(), 0.0));
    meter.advance(30.0);
    CHECK(near(meter.peakBytesPerSecond(), 0.0));
    CHECK(meter.total() == 1700);
    meter.advance(10.0);
    meter.add(7);
    CHECK(meter.total() == 1707);
    meter.reset();
    CHECK(meter.total() == 0);

    TrafficMeter steady;
    for (int frame = 0; frame < 60 * 5; ++frame) {
        steady.advance(frame / 60.0);
        steady.add(1000);
    }
    CHECK(near(steady.bytesPerSecond(), 60000.0));
    CHECK(near(steady.peakBytesPerSecond(), 60000.0));
    steady.advance(std::numeric_limits<double>::quiet_NaN());
    steady.advance(-1.0);
    CHECK(near(steady.bytesPerSecond(), 60000.0));

    TrafficMeter early;
    early.add(10);
    early.advance(1.0);
    CHECK(near(early.bytesPerSecond(), 10.0));
}

void testWindow() {
    WindowStats window;
    CHECK(window.count() == 0);
    CHECK(near(window.percentile(0.99), 0.0));
    CHECK(near(window.maxMs(), 0.0));
    for (int i = 0; i < 600; ++i) {
        window.add(1000.0 / 60.0);
    }
    CHECK(window.count() == 600);
    CHECK(near(window.percentile(0.5), 1000.0 / 60.0, 1e-6));
    CHECK(near(window.percentile(0.99), 1000.0 / 60.0, 1e-6));
    CHECK(near(window.maxMs(), 1000.0 / 60.0, 1e-6));
    CHECK(near(window.averageMs(), 1000.0 / 60.0, 1e-6));
    CHECK(window.coveredSeconds() > 9.9 && window.coveredSeconds() < 10.1);

    WindowStats spiky;
    for (int i = 0; i < 1000; ++i) {
        spiky.add(i % 100 == 99 ? 50.0 : 10.0);
    }
    CHECK(spiky.count() == 1000);
    CHECK(near(spiky.percentile(0.5), 10.0, 0.26));
    CHECK(near(spiky.percentile(0.95), 10.0, 0.26));
    CHECK(near(spiky.percentile(0.995), 50.0, 0.26));
    CHECK(near(spiky.maxMs(), 50.0));
    CHECK(spiky.percentile(0.99) <= spiky.percentile(1.0));
    CHECK(near(spiky.percentile(1.0), 50.0, 0.26));
    CHECK(near(spiky.percentile(2.0), spiky.percentile(1.0)));
    CHECK(near(spiky.percentile(-1.0), spiky.percentile(0.0)));

    WindowStats overflow;
    for (int i = 0; i < 99; ++i) {
        overflow.add(10.0);
    }
    overflow.add(250.0);
    CHECK(near(overflow.percentile(1.0), 250.0));
    CHECK(near(overflow.percentile(0.5), 10.0, 0.26));

    WindowStats aging;
    for (int i = 0; i < 200; ++i) {
        aging.add(30.0);
    }
    CHECK(near(aging.percentile(0.99), 30.0, 0.26));
    for (int i = 0; i < 7000; ++i) {
        aging.add(10.0);
    }
    CHECK(aging.count() >= 5900 && aging.count() <= 6100);
    CHECK(near(aging.percentile(0.99), 10.0, 0.26));
    CHECK(near(aging.maxMs(), 10.0, 1e-9));

    WindowStats jump;
    jump.add(10.0);
    jump.add(1900.0);
    jump.add(10.0);
    CHECK(near(jump.maxMs(), 1900.0));
    for (int i = 0; i < 70; ++i) {
        jump.add(1000.0);
    }
    CHECK(jump.count() <= 61);

    WindowStats bad;
    bad.add(-1.0);
    bad.add(std::numeric_limits<double>::infinity());
    bad.add(std::numeric_limits<double>::quiet_NaN());
    CHECK(bad.count() == 0);
    bad.add(0.0);
    CHECK(bad.count() == 1);
    bad.reset();
    CHECK(bad.count() == 0);
}

void testFrameWatch() {
    const double budget = 1000.0 / 60.0;
    CHECK(FrameWatch::droppedIn(budget, budget) == 0);
    CHECK(FrameWatch::droppedIn(budget * 1.5, budget) == 0);
    CHECK(FrameWatch::droppedIn(budget * 1.6, budget) == 1);
    CHECK(FrameWatch::droppedIn(budget * 2.0, budget) == 1);
    CHECK(FrameWatch::droppedIn(budget * 3.0, budget) == 2);
    CHECK(FrameWatch::droppedIn(budget * 10.0, budget) == 9);
    CHECK(FrameWatch::droppedIn(50.0, 0.0) == 0);
    CHECK(FrameWatch::droppedIn(0.0, budget) == 0);

    FrameWatch watch;
    watch.setBudgetMs(budget);
    for (int i = 0; i < 300; ++i) {
        watch.add(budget);
    }
    CHECK(watch.frames() == 300);
    CHECK(watch.late() == 0);
    CHECK(watch.dropped() == 0);
    watch.add(budget * 3.0);
    watch.add(budget * 1.2);
    watch.add(budget * 2.0);
    CHECK(watch.frames() == 303);
    CHECK(watch.late() == 2);
    CHECK(watch.dropped() == 3);
    CHECK(watch.droppedRecent() == 3);
    CHECK(watch.lateRecent() == 2);
    CHECK(watch.stalls() == 0);

    watch.add(5000.0);
    CHECK(watch.stalls() == 1);
    CHECK(watch.frames() == 303);
    CHECK(watch.dropped() == 3);

    for (int i = 0; i < 6000; ++i) {
        watch.add(budget);
    }
    CHECK(watch.dropped() == 3);
    CHECK(watch.droppedRecent() == 0);
    CHECK(watch.lateRecent() == 0);

    watch.setBudgetMs(-5.0);
    CHECK(near(watch.budgetMs(), budget));
    watch.setBudgetMs(1000.0 / 30.0);
    watch.add(1000.0 / 30.0 * 2.5);
    CHECK(watch.dropped() == 3 + 2);
    watch.add(std::numeric_limits<double>::quiet_NaN());
    watch.add(-3.0);
    const std::uint64_t frames = watch.frames();
    watch.reset();
    CHECK(watch.frames() == 0 && watch.dropped() == 0 && frames > 0);

    FrameWatch burst;
    burst.setBudgetMs(budget);
    for (int i = 0; i < 10; ++i) {
        burst.add(budget * 2.0);
    }
    CHECK(burst.droppedRecent() == 10);
    for (int i = 0; i < 4200; ++i) {
        burst.add(budget);
    }
    CHECK(burst.droppedRecent() == 0);
    CHECK(burst.dropped() == 10);
}

Inputs calm() {
    Inputs inputs;
    inputs.budgetMs = 1000.0 / 60.0;
    inputs.p99Ms = 17.0;
    inputs.frames = 600;
    return inputs;
}

bool has(const std::vector<Warning>& list, WarningCode code) {
    return std::any_of(list.begin(), list.end(), [&](const Warning& warning) { return warning.code == code; });
}

Severity severityOf(const std::vector<Warning>& list, WarningCode code) {
    for (const Warning& warning : list) {
        if (warning.code == code) {
            return warning.severity;
        }
    }
    return Severity::Info;
}

void testEvaluate() {
    const Thresholds defaults;
    CHECK(evaluate(calm(), defaults).empty());

    Inputs slow = calm();
    slow.p99Ms = 30.0;
    std::vector<Warning> list = evaluate(slow, defaults);
    CHECK(list.size() == 1 && list[0].code == WarningCode::FrameTime);
    CHECK(list[0].severity == Severity::Warning);
    CHECK(near(list[0].limit, 1.5 * 1000.0 / 60.0));
    slow.p99Ms = 60.0;
    CHECK(severityOf(evaluate(slow, defaults), WarningCode::FrameTime) == Severity::Critical);
    slow.frames = 119;
    CHECK(!has(evaluate(slow, defaults), WarningCode::FrameTime));
    slow.frames = 120;
    CHECK(has(evaluate(slow, defaults), WarningCode::FrameTime));
    slow.budgetMs = 0.0;
    CHECK(!has(evaluate(slow, defaults), WarningCode::FrameTime));

    Inputs drops = calm();
    drops.droppedRecent = 4;
    CHECK(evaluate(drops, defaults).empty());
    drops.droppedRecent = 5;
    CHECK(severityOf(evaluate(drops, defaults), WarningCode::DroppedFrames) == Severity::Warning);
    drops.droppedRecent = 30;
    CHECK(severityOf(evaluate(drops, defaults), WarningCode::DroppedFrames) == Severity::Critical);
    Thresholds strict = defaults;
    strict.droppedPerMinute = 1;
    drops.droppedRecent = 1;
    CHECK(has(evaluate(drops, strict), WarningCode::DroppedFrames));

    Inputs memory = calm();
    memory.budgetBytes = 1000 * kMiB;
    memory.estimateBytes = 700 * kMiB;
    CHECK(evaluate(memory, defaults).empty());
    memory.estimateBytes = 860 * kMiB;
    list = evaluate(memory, defaults);
    CHECK(list.size() == 1 && list[0].code == WarningCode::MemoryNearBudget && list[0].severity == Severity::Info);
    memory.estimateBytes = 1000 * kMiB;
    CHECK(severityOf(evaluate(memory, defaults), WarningCode::MemoryNearBudget) == Severity::Info);
    memory.estimateBytes = 1001 * kMiB;
    list = evaluate(memory, defaults);
    CHECK(has(list, WarningCode::MemoryOverBudget) && !has(list, WarningCode::MemoryNearBudget));
    CHECK(severityOf(list, WarningCode::MemoryOverBudget) == Severity::Warning);
    memory.estimateBytes = 1300 * kMiB;
    CHECK(severityOf(evaluate(memory, defaults), WarningCode::MemoryOverBudget) == Severity::Critical);
    memory.estimateBytes = 900 * kMiB;
    memory.residentBytes = 1200 * kMiB;
    list = evaluate(memory, defaults);
    CHECK(has(list, WarningCode::ResidentOverBudget));
    CHECK(severityOf(list, WarningCode::ResidentOverBudget) == Severity::Critical);
    memory.budgetBytes = 0;
    CHECK(evaluate(memory, defaults).empty());

    Inputs driver = calm();
    NvxRaw raw;
    raw.dedicatedKb = 8 * 1024 * 1024;
    raw.availableKb = 4 * 1024 * 1024;
    raw.evictionCount = 0;
    raw.evictedKb = 0;
    driver.driver = fromNvx(raw);
    CHECK(evaluate(driver, defaults).empty());
    raw.availableKb = 600 * 1024;
    driver.driver = fromNvx(raw);
    list = evaluate(driver, defaults);
    CHECK(list.size() == 1 && list[0].code == WarningCode::DriverMemoryLow && list[0].severity == Severity::Warning);
    raw.availableKb = 100 * 1024;
    driver.driver = fromNvx(raw);
    CHECK(severityOf(evaluate(driver, defaults), WarningCode::DriverMemoryLow) == Severity::Critical);
    driver.driver = fromAti({2 * 1024 * 1024, 0, 0, 0});
    CHECK(evaluate(driver, defaults).empty());
    driver.driver = fromAti({100 * 1024, 0, 0, 0});
    CHECK(has(evaluate(driver, defaults), WarningCode::DriverMemoryLow));
    driver.driver = DriverMemory{};
    CHECK(evaluate(driver, defaults).empty());

    Inputs evictions = calm();
    raw.availableKb = 4 * 1024 * 1024;
    evictions.driver = fromNvx(raw);
    evictions.driverEvictionsRecent = 0;
    CHECK(evaluate(evictions, defaults).empty());
    evictions.driverEvictionsRecent = 1;
    CHECK(severityOf(evaluate(evictions, defaults), WarningCode::DriverEvictions) == Severity::Warning);
    evictions.driverEvictionsRecent = 25;
    CHECK(severityOf(evaluate(evictions, defaults), WarningCode::DriverEvictions) == Severity::Critical);
    evictions.driver = DriverMemory{};
    CHECK(evaluate(evictions, defaults).empty());

    Inputs upload = calm();
    upload.uploadBytesPerSecond = 1.0e9;
    CHECK(evaluate(upload, defaults).empty());
    upload.uploadBytesPerSecond = 2.0e9;
    CHECK(severityOf(evaluate(upload, defaults), WarningCode::UploadTraffic) == Severity::Info);

    Thresholds wild;
    wild.p99Factor = std::numeric_limits<double>::quiet_NaN();
    wild.droppedPerMinute = -4;
    wild.memoryFraction = 7.0;
    wild.driverFreeFraction = 0.0;
    wild.driverFreeCriticalBytes = 1;
    wild.driverEvictionsRecent = 0;
    wild.uploadBytesPerSecond = -1.0;
    const Thresholds fixed = clampThresholds(wild);
    CHECK(near(fixed.p99Factor, defaults.p99Factor));
    CHECK(fixed.droppedPerMinute == 1);
    CHECK(near(fixed.memoryFraction, 0.99));
    CHECK(near(fixed.driverFreeFraction, 0.01));
    CHECK(fixed.driverFreeCriticalBytes == 16 * kMiB);
    CHECK(fixed.driverEvictionsRecent == 1);
    CHECK(near(fixed.uploadBytesPerSecond, 1.0e8));
    CHECK(evaluate(calm(), wild).empty());

    std::set<std::string> ids;
    for (int i = 0; i < kWarningCodeCount; ++i) {
        ids.insert(warningId(static_cast<WarningCode>(i)));
    }
    CHECK(ids.size() == static_cast<size_t>(kWarningCodeCount));
    CHECK(ids.count("unknown") == 0);
}

void testTracker() {
    WarningTracker tracker;
    std::vector<Warning> raw;
    Warning warning;
    warning.code = WarningCode::FrameTime;
    warning.severity = Severity::Warning;
    warning.value = 30.0;
    warning.limit = 25.0;
    raw.push_back(warning);

    tracker.update(raw, 1.0);
    CHECK(tracker.active().empty());
    tracker.update(raw, 0.9);
    CHECK(tracker.active().empty());
    tracker.update(raw, 0.2);
    CHECK(tracker.active().size() == 1);
    CHECK(tracker.has(WarningCode::FrameTime));
    CHECK(tracker.raised(WarningCode::FrameTime) == 1);

    tracker.update({}, 3.0);
    CHECK(tracker.has(WarningCode::FrameTime));
    tracker.update({}, 3.1);
    CHECK(!tracker.has(WarningCode::FrameTime));
    CHECK(tracker.active().empty());

    for (int i = 0; i < 10; ++i) {
        tracker.update(raw, 0.5);
        tracker.update({}, 0.5);
    }
    CHECK(tracker.active().empty());
    CHECK(tracker.raised(WarningCode::FrameTime) == 1);

    tracker.update(raw, 2.0);
    CHECK(tracker.raised(WarningCode::FrameTime) == 2);
    tracker.update({}, 5.0);
    tracker.update(raw, 0.1);
    CHECK(tracker.has(WarningCode::FrameTime));
    tracker.update({}, 5.0);
    tracker.update({}, 0.0);
    CHECK(tracker.has(WarningCode::FrameTime));

    Warning critical;
    critical.code = WarningCode::DriverMemoryLow;
    critical.severity = Severity::Critical;
    Warning info;
    info.code = WarningCode::UploadTraffic;
    info.severity = Severity::Info;
    Warning second = warning;
    second.code = WarningCode::DroppedFrames;
    tracker.reset();
    CHECK(tracker.active().empty() && tracker.raised(WarningCode::FrameTime) == 0);
    tracker.update({info, second, critical, warning}, 2.5);
    CHECK(tracker.active().size() == 4);
    CHECK(tracker.active()[0].code == WarningCode::DriverMemoryLow);
    CHECK(tracker.active()[1].code == WarningCode::FrameTime);
    CHECK(tracker.active()[2].code == WarningCode::DroppedFrames);
    CHECK(tracker.active()[3].code == WarningCode::UploadTraffic);
    CHECK(tracker.worst() == Severity::Critical);

    tracker.update({info}, 6.5);
    CHECK(tracker.active().size() == 1);
    CHECK(tracker.worst() == Severity::Info);

    Warning worse = info;
    worse.severity = Severity::Warning;
    worse.value = 5.0e9;
    tracker.update({worse}, 0.1);
    CHECK(tracker.active()[0].severity == Severity::Warning);
    CHECK(near(tracker.active()[0].value, 5.0e9));
}

void testMonitor() {
    Monitor monitor;
    Sample sample;
    sample.budgetMs = 1000.0 / 60.0;
    for (int i = 0; i < 600; ++i) {
        monitor.addFrame(1000.0 / 60.0);
        if (i % 15 == 0) {
            monitor.update(0.25, sample);
        }
    }
    CHECK(monitor.percentilesReliable());
    CHECK(monitor.tracker().active().empty());
    CHECK(near(monitor.p99(), 1000.0 / 60.0, 1e-6));
    CHECK(near(monitor.p95(), 1000.0 / 60.0, 1e-6));
    CHECK(monitor.watch().dropped() == 0);

    for (int i = 0; i < 300; ++i) {
        monitor.addFrame(i % 4 == 0 ? 60.0 : 1000.0 / 60.0);
        if (i % 15 == 0) {
            monitor.update(0.25, sample);
        }
    }
    CHECK(monitor.p99() > 25.0);
    CHECK(monitor.watch().droppedRecent() >= 5);
    for (int i = 0; i < 8; ++i) {
        monitor.update(0.25, sample);
    }
    CHECK(monitor.tracker().has(WarningCode::FrameTime));
    CHECK(monitor.tracker().has(WarningCode::DroppedFrames));

    Sample memory = sample;
    memory.budgetBytes = 512 * kMiB;
    memory.estimateBytes = 700 * kMiB;
    memory.residentBytes = 100 * kMiB;
    for (int i = 0; i < 12; ++i) {
        monitor.update(0.25, memory);
    }
    CHECK(monitor.tracker().has(WarningCode::MemoryOverBudget));
    CHECK(monitor.tracker().raised(WarningCode::MemoryOverBudget) == 1);

    Monitor driver;
    Sample reading = sample;
    NvxRaw raw;
    raw.dedicatedKb = 4 * 1024 * 1024;
    raw.availableKb = 3 * 1024 * 1024;
    raw.evictionCount = 0;
    raw.evictedKb = 0;
    reading.driver = fromNvx(raw);
    for (int i = 0; i < 8; ++i) {
        driver.update(0.5, reading);
    }
    CHECK(driver.tracker().active().empty());
    raw.evictionCount = 3;
    raw.evictedKb = 4096;
    reading.driver = fromNvx(raw);
    for (int i = 0; i < 8; ++i) {
        driver.update(0.5, reading);
    }
    CHECK(driver.tracker().has(WarningCode::DriverEvictions));
    CHECK(driver.evictions().recentCount() == 3);

    Thresholds loose;
    loose.p99Factor = 6.0;
    loose.droppedPerMinute = 600;
    monitor.setThresholds(loose);
    CHECK(near(monitor.thresholds().p99Factor, 6.0));
    for (int i = 0; i < 40; ++i) {
        monitor.update(0.25, sample);
    }
    CHECK(!monitor.tracker().has(WarningCode::FrameTime));
    CHECK(!monitor.tracker().has(WarningCode::DroppedFrames));

    monitor.reset();
    CHECK(monitor.window().count() == 0 && monitor.watch().frames() == 0);
    CHECK(!monitor.percentilesReliable());
    CHECK(monitor.tracker().active().empty());

    Monitor few;
    for (int i = 0; i < 50; ++i) {
        few.addFrame(100.0);
    }
    for (int i = 0; i < 12; ++i) {
        few.update(0.25, sample);
    }
    CHECK(!few.tracker().has(WarningCode::FrameTime));
}

std::uint64_t mb(std::uint64_t value) {
    return value * kMiB;
}

void testEviction() {
    std::vector<Candidate> list;
    list.push_back({ResourceClass::Scene3d, mb(100), 50.0, false, false});
    list.push_back({ResourceClass::IdleImage, mb(40), 20.0, false, false});
    list.push_back({ResourceClass::PreparedVideo, mb(15), 9.0, false, false});
    list.push_back({ResourceClass::IdleImage, mb(60), 90.0, false, false});
    list.push_back({ResourceClass::IdleImage, mb(10), 200.0, true, false});
    list.push_back({ResourceClass::IdleImage, mb(30), 300.0, false, true});
    list.push_back({ResourceClass::IdleImage, mb(25), 1.0, false, false});
    list.push_back({ResourceClass::PreparedVideo, mb(0), 99.0, false, false});

    EvictionRequest request;
    request.currentBytes = mb(1000);
    request.targetBytes = mb(1000);
    CHECK(planEviction(list, request).empty());
    request.targetBytes = mb(2000);
    CHECK(planEviction(list, request).empty());

    request.currentBytes = mb(1000);
    request.targetBytes = mb(990);
    std::vector<std::size_t> plan = planEviction(list, request);
    CHECK(plan.size() == 1 && plan[0] == 2);

    request.targetBytes = mb(930);
    plan = planEviction(list, request);
    CHECK(plan.size() == 2);
    CHECK(plan[0] == 2 && plan[1] == 3);

    request.targetBytes = mb(885);
    plan = planEviction(list, request);
    CHECK(plan.size() == 3);
    CHECK(plan[0] == 2 && plan[1] == 3 && plan[2] == 1);

    request.targetBytes = 0;
    plan = planEviction(list, request);
    std::set<std::size_t> chosen(plan.begin(), plan.end());
    CHECK(chosen.count(4) == 0);
    CHECK(chosen.count(5) == 0);
    CHECK(chosen.count(6) == 0);
    CHECK(chosen.count(7) == 0);
    CHECK(chosen.count(0) == 1 && chosen.count(1) == 1 && chosen.count(2) == 1 && chosen.count(3) == 1);
    CHECK(plan.size() == 4);
    CHECK(plan.back() == 0);

    request.minIdleSeconds = 60.0;
    plan = planEviction(list, request);
    chosen = std::set<std::size_t>(plan.begin(), plan.end());
    CHECK(chosen.size() == 1 && chosen.count(3) == 1);

    request.minIdleSeconds = 5.0;
    request.maxCount = 2;
    plan = planEviction(list, request);
    CHECK(plan.size() == 2);

    request.maxCount = 0;
    CHECK(planEviction(list, request).empty());

    std::vector<Candidate> ties;
    ties.push_back({ResourceClass::IdleImage, mb(10), 30.0, false, false});
    ties.push_back({ResourceClass::IdleImage, mb(20), 30.0, false, false});
    ties.push_back({ResourceClass::IdleImage, mb(20), 30.0, false, false});
    EvictionRequest tie;
    tie.currentBytes = mb(100);
    tie.targetBytes = mb(90);
    tie.maxCount = 8;
    plan = planEviction(ties, tie);
    CHECK(plan.size() == 1 && plan[0] == 1);
    tie.targetBytes = mb(50);
    plan = planEviction(ties, tie);
    CHECK(plan.size() == 3 && plan[0] == 1 && plan[1] == 2 && plan[2] == 0);

    std::vector<Candidate> locked;
    for (int i = 0; i < 10; ++i) {
        locked.push_back({ResourceClass::IdleImage, mb(50), 500.0, true, false});
    }
    EvictionRequest all;
    all.currentBytes = mb(1000);
    all.targetBytes = mb(10);
    CHECK(planEviction(locked, all).empty());
    for (Candidate& candidate : locked) {
        candidate.inUse = false;
        candidate.pinned = true;
    }
    CHECK(planEviction(locked, all).empty());

    std::vector<Candidate> nan;
    nan.push_back({ResourceClass::IdleImage, mb(50), std::numeric_limits<double>::quiet_NaN(), false, false});
    CHECK(planEviction(nan, all).empty());

    std::vector<Candidate> small;
    small.push_back({ResourceClass::Scene3d, 40ull * 1024ull, 60.0, false, false});
    small.push_back({ResourceClass::IdleImage, kMiB - 1, 60.0, false, false});
    small.push_back({ResourceClass::IdleImage, kMiB, 60.0, false, false});
    small.push_back({ResourceClass::PreparedVideo, mb(5), 60.0, false, false});
    EvictionRequest everything;
    everything.currentBytes = mb(1000);
    everything.targetBytes = 0;
    std::vector<std::size_t> worthwhile = planEviction(small, everything);
    CHECK(worthwhile.size() == 2);
    CHECK(worthwhile.size() == 2 && worthwhile[0] == 3 && worthwhile[1] == 2);
    everything.minBytes = 0;
    CHECK(planEviction(small, everything).size() == 4);
    everything.minBytes = mb(5) + 1;
    CHECK(planEviction(small, everything).empty());
    CHECK(EvictionRequest{}.minBytes == kMiB);

    CHECK(lowWater(mb(1000)) == mb(850));
    CHECK(lowWater(0) == 0);
    CHECK(recoveryWater(mb(1000)) == mb(700));
    CHECK(recoveryWater(0) == 0);
    CHECK(recoveryWater(mb(256)) < static_cast<std::uint64_t>(static_cast<double>(mb(256)) * PressureLadder::kLowerFraction));
    CHECK(recoveryWater(mb(4096)) < lowWater(mb(4096)));
    CHECK(clampBudgetBytes(0) == 0);
    CHECK(clampBudgetBytes(mb(10)) == mb(256));
    CHECK(clampBudgetBytes(mb(4096)) == mb(4096));
    CHECK(clampBudgetBytes(mb(1000000)) == mb(65536));
    CHECK(std::string(resourceClassName(ResourceClass::PreparedVideo)) == "prepared video");
    CHECK(std::string(resourceClassName(ResourceClass::IdleImage)) == "idle image");
    CHECK(std::string(resourceClassName(ResourceClass::Scene3d)) == "3D scene");
}

void testLadder() {
    PressureLadder ladder;
    CHECK(ladder.level() == 0);
    CHECK(ladder.update(1.0, mb(2000), 0) == 0);
    for (int i = 0; i < 20; ++i) {
        ladder.update(0.1, mb(900), mb(1000));
    }
    CHECK(ladder.level() == 0);
    ladder.update(2.0, mb(1200), mb(1000));
    CHECK(ladder.level() == 0);
    ladder.update(1.1, mb(1200), mb(1000));
    CHECK(ladder.level() == 1);
    ladder.update(3.0, mb(1200), mb(1000));
    CHECK(ladder.level() == 2);
    ladder.update(1.0, mb(900), mb(1000));
    ladder.update(1.0, mb(1200), mb(1000));
    ladder.update(1.0, mb(900), mb(1000));
    ladder.update(1.0, mb(1200), mb(1000));
    CHECK(ladder.level() == 2);
    for (int i = 0; i < 5; ++i) {
        ladder.update(3.0, mb(1200), mb(1000));
    }
    CHECK(ladder.level() == kMaxPressureLevel);
    ladder.update(11.0, mb(500), mb(1000));
    CHECK(ladder.level() == kMaxPressureLevel);
    ladder.update(1.1, mb(500), mb(1000));
    CHECK(ladder.level() == kMaxPressureLevel - 1);
    ladder.update(11.0, mb(800), mb(1000));
    CHECK(ladder.level() == kMaxPressureLevel - 1);
    ladder.update(13.0, mb(700), mb(1000));
    CHECK(ladder.level() == kMaxPressureLevel - 2);
    for (int i = 0; i < 10; ++i) {
        ladder.update(13.0, mb(100), mb(1000));
    }
    CHECK(ladder.level() == 0);
    ladder.update(3.0, mb(1200), mb(1000));
    CHECK(ladder.level() == 1);
    ladder.update(1.0, mb(1200), 0);
    CHECK(ladder.level() == 0);
    ladder.update(3.0, mb(1200), mb(1000));
    ladder.reset();
    CHECK(ladder.level() == 0);
    ladder.update(-5.0, mb(1200), mb(1000));
    CHECK(ladder.level() == 0);

    CHECK(preparingAllowed(0));
    CHECK(!preparingAllowed(1));
    CHECK(near(minIdleSecondsFor(0), 5.0));
    CHECK(near(minIdleSecondsFor(1), 5.0));
    CHECK(near(minIdleSecondsFor(2), 1.0));
    CHECK(near(minIdleSecondsFor(4), 1.0));
    CHECK(layerScaleCap(0) == 1.0f);
    CHECK(layerScaleCap(2) == 1.0f);
    CHECK(layerScaleCap(3) == 0.75f);
    CHECK(layerScaleCap(4) == 0.5f);
    CHECK(layerScaleCap(9) == 0.5f);
}

void testDeterminism() {
    const auto run = []() {
        Monitor monitor;
        Sample sample;
        NvxRaw raw;
        raw.dedicatedKb = 4 * 1024 * 1024;
        raw.availableKb = 300 * 1024;
        raw.evictionCount = 0;
        raw.evictedKb = 0;
        sample.driver = fromNvx(raw);
        sample.budgetBytes = mb(512);
        sample.estimateBytes = mb(600);
        std::string log;
        for (int i = 0; i < 2000; ++i) {
            monitor.addFrame(i % 37 == 0 ? 45.0 : 16.0);
            if (i % 15 == 0) {
                monitor.update(0.25, sample);
                for (const Warning& warning : monitor.tracker().active()) {
                    log += warningId(warning.code);
                    log += ',';
                }
                log += ';';
            }
        }
        log += std::to_string(monitor.p99()) + "/" + std::to_string(monitor.watch().dropped());
        return log;
    };
    CHECK(run() == run());
    CHECK(run().find("driver-memory-low") != std::string::npos);
    CHECK(run().find("memory-over-budget") != std::string::npos);
}

}

void useDataDir(const std::string& path) {
    std::filesystem::create_directories(path);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void writeSettingsFile(const std::string& dir, const std::string& json) {
    std::ofstream file(std::filesystem::path(dir) / "settings.json", std::ios::trunc);
    file << json;
}

void testSettings() {
    const std::string dir = (std::filesystem::temp_directory_path() / ("nvj_gpudiag_settings_" + std::to_string(std::random_device{}()))).string();
    useDataDir(dir);

    nvj::Settings defaults;
    CHECK(defaults.gpuMemoryBudgetMb == 0);
    CHECK(defaults.gpuMemoryTrim);
    CHECK(!defaults.gpuMemoryLowerQuality);
    CHECK(defaults.diagWarnings);
    CHECK(defaults.diagP99Percent == 150);
    CHECK(defaults.diagDroppedPerMinute == 5);
    CHECK(defaults.diagMemoryPercent == 85);

    nvj::Settings wanted;
    wanted.gpuMemoryBudgetMb = 2048;
    wanted.gpuMemoryTrim = false;
    wanted.gpuMemoryLowerQuality = true;
    wanted.diagWarnings = false;
    wanted.diagP99Percent = 200;
    wanted.diagDroppedPerMinute = 12;
    wanted.diagMemoryPercent = 70;
    CHECK(nvj::saveSettings(wanted));
    nvj::Settings back;
    CHECK(nvj::loadSettings(back));
    CHECK(back.gpuMemoryBudgetMb == 2048);
    CHECK(!back.gpuMemoryTrim);
    CHECK(back.gpuMemoryLowerQuality);
    CHECK(!back.diagWarnings);
    CHECK(back.diagP99Percent == 200);
    CHECK(back.diagDroppedPerMinute == 12);
    CHECK(back.diagMemoryPercent == 70);

    for (const auto& [written, expected] : std::vector<std::pair<int, int>>{{0, 0}, {-40, 0}, {1, 256}, {255, 256}, {256, 256}, {4096, 4096}, {65536, 65536}, {900000, 65536}}) {
        writeSettingsFile(dir, "{\"gpuMemoryBudgetMb\":" + std::to_string(written) + "}");
        nvj::Settings clamped;
        CHECK(nvj::loadSettings(clamped));
        CHECK(clamped.gpuMemoryBudgetMb == expected);
    }
    for (const auto& [written, expected] : std::vector<std::pair<int, int>>{{0, 110}, {109, 110}, {110, 110}, {300, 300}, {600, 600}, {9000, 600}}) {
        writeSettingsFile(dir, "{\"diagP99Percent\":" + std::to_string(written) + "}");
        nvj::Settings clamped;
        CHECK(nvj::loadSettings(clamped));
        CHECK(clamped.diagP99Percent == expected);
    }
    for (const auto& [written, expected] : std::vector<std::pair<int, int>>{{-3, 1}, {0, 1}, {1, 1}, {77, 77}, {600, 600}, {601, 600}}) {
        writeSettingsFile(dir, "{\"diagDroppedPerMinute\":" + std::to_string(written) + "}");
        nvj::Settings clamped;
        CHECK(nvj::loadSettings(clamped));
        CHECK(clamped.diagDroppedPerMinute == expected);
    }
    for (const auto& [written, expected] : std::vector<std::pair<int, int>>{{0, 50}, {49, 50}, {50, 50}, {85, 85}, {99, 99}, {100, 99}, {5000, 99}}) {
        writeSettingsFile(dir, "{\"diagMemoryPercent\":" + std::to_string(written) + "}");
        nvj::Settings clamped;
        CHECK(nvj::loadSettings(clamped));
        CHECK(clamped.diagMemoryPercent == expected);
    }

    writeSettingsFile(dir, "{\"gpuMemoryBudgetMb\":\"big\",\"gpuMemoryTrim\":\"no\",\"diagP99Percent\":1.5,\"diagMemoryPercent\":null}");
    nvj::Settings broken;
    CHECK(nvj::loadSettings(broken));
    CHECK(broken.gpuMemoryBudgetMb == 0);
    CHECK(broken.gpuMemoryTrim);
    CHECK(broken.diagP99Percent == 150);
    CHECK(broken.diagMemoryPercent == 85);

    writeSettingsFile(dir, "{}");
    nvj::Settings empty;
    CHECK(nvj::loadSettings(empty));
    CHECK(empty.gpuMemoryBudgetMb == 0);
    CHECK(empty.gpuMemoryTrim);
    CHECK(!empty.gpuMemoryLowerQuality);
    CHECK(empty.diagWarnings);

    nvj::Settings wild;
    wild.gpuMemoryBudgetMb = 7;
    wild.diagP99Percent = -1;
    wild.diagDroppedPerMinute = 0;
    wild.diagMemoryPercent = 400;
    CHECK(nvj::saveSettings(wild));
    nvj::Settings tamed;
    CHECK(nvj::loadSettings(tamed));
    CHECK(tamed.gpuMemoryBudgetMb == 256);
    CHECK(tamed.diagP99Percent == 110);
    CHECK(tamed.diagDroppedPerMinute == 1);
    CHECK(tamed.diagMemoryPercent == 99);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

int main() {
    testDriverMemory();
    testEvictionWatch();
    testTraffic();
    testWindow();
    testFrameWatch();
    testEvaluate();
    testTracker();
    testMonitor();
    testEviction();
    testLadder();
    testDeterminism();
    testSettings();
    return testing::finish();
}

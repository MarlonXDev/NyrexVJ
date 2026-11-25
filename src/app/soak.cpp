#include "app/bench.hpp"

#include "audio/audio_features.hpp"
#include "core/frame_pacer.hpp"
#include "core/frame_stats.hpp"
#include "core/growth_check.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "core/soak_plan.hpp"
#include "platform/platform.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr double kMegabyte = 1024.0 * 1024.0;
constexpr size_t kRunFrameCapacity = 400000;

struct Series {
    std::vector<double> rss;
    std::vector<double> gpu;
    std::vector<double> threads;
    std::vector<double> handles;
};

json summaryJson(const FrameSummary& summary) {
    json item;
    item["count"] = summary.count;
    item["avg"] = summary.avg;
    item["min"] = summary.min;
    item["p50"] = summary.p50;
    item["p95"] = summary.p95;
    item["p99"] = summary.p99;
    item["max"] = summary.max;
    return item;
}

json growthJson(const GrowthResult& result, const std::string& unit, bool judge) {
    json item;
    item["unit"] = unit;
    item["judged"] = result.enough && judge;
    if (result.enough) {
        item["first"] = result.first;
        item["last"] = result.last;
        item["growth"] = result.growth;
        item["limit"] = result.limit;
        if (judge) {
            item["leak"] = result.leak;
        }
    }
    return item;
}

void writeReport(const json& report, const std::string& outputPath) {
    if (outputPath.empty()) {
        return;
    }
    std::ofstream out(outputPath, std::ios::trunc);
    out << report.dump(2) << '\n';
}

}

int runSoakTest(Engine& engine, const std::string& spec, const std::string& outputPath) {
    SoakPlan plan;
    std::string error;
    if (!parseSoakSpec(spec, plan, error)) {
        logLine("Soak test: " + error + " (use SECONDS or SECONDS,FPS)");
        return 1;
    }
    const std::vector<Composition>& looks = factoryPresets();
    if (looks.empty()) {
        logLine("Soak test: no factory looks are available");
        return 1;
    }
    engine.setGpuTiming(false);

    FramePacer pacer;
    pacer.setTargetFps(plan.fps);
    const double period = pacer.period();
    const double periodMs = period * 1000.0;

    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["plannedSeconds"] = plan.seconds;
    report["targetFps"] = pacer.targetFps();
    report["sampleSeconds"] = plan.sampleSeconds;
    report["finished"] = false;
    json samples = json::array();

    size_t lookIndex = 0;
    int lookChanges = 0;
    int deckSwitches = 0;
    int qualityChanges = 0;
    int qualityMode = 0;

    const auto prepareLook = [&]() {
        engine.applyComposition(looks[lookIndex % looks.size()]);
        engine.duplicateActiveDeckTo(1);
        engine.comp.deckSettings.syncBeats = false;
        engine.comp.deckSettings.seconds = 0.5f;
        ++lookIndex;
    };
    prepareLook();
    engine.setQuality(0, periodMs);

    FrameStats windowStats(static_cast<size_t>(plan.fps * plan.sampleSeconds) + 64u);
    FrameStats runStats(kRunFrameCapacity);
    Series series;
    long long frames = 0;
    long long windowFrames = 0;
    long long overBudget = 0;
    long long windowOverBudget = 0;
    long long longFrames = 0;
    long long missedFrames = 0;
    long long droppedFrames = 0;
    double worstIntervalMs = 0.0;
    FrameStats intervalStats(kRunFrameCapacity);
    bool haveLastStart = false;
    Clock::time_point lastFrameStart;
    int lastSecond = 0;

    const auto start = Clock::now();
    const auto seconds = [&](Clock::time_point stamp) { return std::chrono::duration<double>(stamp - start).count(); };
    double windowStart = 0.0;
    double windowCpuStart = platform::processCpuSeconds();
    const size_t rssStart = platform::processMemoryBytes();
    const size_t gpuStart = engine.gpuMemory().total();
    const int threadsStart = platform::processThreadCount();
    const int handlesStart = platform::processHandleCount();
    report["start"] = {{"rssBytes", rssStart}, {"gpuEstimateBytes", gpuStart}, {"threads", threadsStart}, {"handles", handlesStart}};

    for (;;) {
        const double elapsed = seconds(Clock::now());
        if (elapsed >= plan.seconds) {
            break;
        }
        const int second = static_cast<int>(elapsed);
        while (lastSecond < second) {
            ++lastSecond;
            const SoakEvents events = soakEventsAt(plan, lastSecond);
            if (events.look) {
                prepareLook();
                ++lookChanges;
            }
            if (events.deck) {
                engine.switchDeck(engine.comp.activeDeck == 0 ? 1 : 0);
                ++deckSwitches;
            }
            if (events.quality) {
                qualityMode = (qualityMode + 1) % 3;
                engine.setQuality(qualityMode, periodMs);
                ++qualityChanges;
            }
        }

        const auto frameStart = Clock::now();
        if (haveLastStart) {
            const double intervalMs = std::chrono::duration<double, std::milli>(frameStart - lastFrameStart).count();
            intervalStats.add(intervalMs);
            worstIntervalMs = std::max(worstIntervalMs, intervalMs);
            if (intervalMs > periodMs * 1.5) {
                ++missedFrames;
                droppedFrames += static_cast<long long>(intervalMs / periodMs) - 1;
            }
        }
        lastFrameStart = frameStart;
        haveLastStart = true;
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(period);
        engine.render();
        glFinish();
        const auto frameEnd = Clock::now();
        const double workMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        windowStats.add(workMs);
        runStats.add(workMs);
        ++frames;
        ++windowFrames;
        if (workMs > periodMs) {
            ++overBudget;
            ++windowOverBudget;
        }
        if (workMs > periodMs * 2.0) {
            ++longFrames;
        }

        const double now = seconds(frameEnd);
        const double deadline = pacer.nextDeadline(now);
        if (deadline > now) {
            platform::sleepPrecise(deadline - now);
        }

        const double sinceWindow = seconds(Clock::now()) - windowStart;
        if (sinceWindow >= plan.sampleSeconds) {
            const double cpu = platform::processCpuSeconds();
            const size_t rss = platform::processMemoryBytes();
            const size_t gpu = engine.gpuMemory().total();
            const int threads = platform::processThreadCount();
            const int handles = platform::processHandleCount();
            json sample;
            sample["t"] = seconds(Clock::now());
            sample["frames"] = windowFrames;
            sample["workMs"] = summaryJson(windowStats.summary());
            sample["overBudget"] = windowOverBudget;
            sample["cpuPercentOfOneCore"] = sinceWindow > 0.0 ? (cpu - windowCpuStart) / sinceWindow * 100.0 : 0.0;
            sample["rssBytes"] = rss;
            sample["gpuEstimateBytes"] = gpu;
            sample["threads"] = threads;
            sample["handles"] = handles;
            sample["look"] = looks[(lookIndex + looks.size() - 1) % looks.size()].name;
            samples.push_back(sample);
            series.rss.push_back(static_cast<double>(rss) / kMegabyte);
            series.gpu.push_back(static_cast<double>(gpu) / kMegabyte);
            series.threads.push_back(threads);
            series.handles.push_back(handles);
            logLine("Soak " + std::to_string(static_cast<int>(sample["t"].get<double>())) + " s: work p95 " + std::to_string(sample["workMs"]["p95"].get<double>()) + " ms, over budget " + std::to_string(windowOverBudget) + ", RSS " + std::to_string(rss / (1024 * 1024)) + " MiB, GPU estimate " + std::to_string(gpu / (1024 * 1024)) + " MiB, threads " + std::to_string(threads) + ", handles " + std::to_string(handles));
            windowStats.clear();
            windowFrames = 0;
            windowOverBudget = 0;
            windowStart = seconds(Clock::now());
            windowCpuStart = cpu;
            json partial = report;
            partial["samples"] = samples;
            partial["frames"] = frames;
            writeReport(partial, outputPath);
        }
    }

    const double wall = seconds(Clock::now());
    const double cpuTotal = platform::processCpuSeconds();
    report["wallSeconds"] = wall;
    report["frames"] = frames;
    report["achievedFps"] = wall > 0.0 ? static_cast<double>(frames) / wall : 0.0;
    report["workMs"] = summaryJson(runStats.summary());
    report["overBudgetFrames"] = overBudget;
    report["overBudgetFraction"] = frames > 0 ? static_cast<double>(overBudget) / static_cast<double>(frames) : 0.0;
    report["longFrames"] = longFrames;
    report["intervalMs"] = summaryJson(intervalStats.summary());
    report["missedFrames"] = missedFrames;
    report["estimatedDroppedFrames"] = droppedFrames;
    report["worstIntervalMs"] = worstIntervalMs;
    report["events"] = {{"lookChanges", lookChanges}, {"deckSwitches", deckSwitches}, {"qualityChanges", qualityChanges}};
    report["cpuSecondsTotal"] = cpuTotal;
    report["samples"] = samples;

    const bool judge = plan.seconds >= kMinJudgedSoakSeconds;
    const GrowthResult rss = assessGrowth(series.rss, 0.25, GrowthLimits{64.0, 0.10});
    const GrowthResult gpu = assessGrowth(series.gpu, 0.25, GrowthLimits{16.0, 0.05});
    const GrowthResult threads = assessGrowth(series.threads, 0.25, GrowthLimits{2.0, 0.0});
    const GrowthResult handles = assessGrowth(series.handles, 0.25, GrowthLimits{16.0, 0.0});
    json growth;
    growth["rss"] = growthJson(rss, "MiB", judge);
    growth["gpuEstimate"] = growthJson(gpu, "MiB", judge);
    growth["threads"] = growthJson(threads, "count", judge);
    growth["handles"] = growthJson(handles, "count", judge);
    report["growth"] = std::move(growth);
    report["judged"] = judge && rss.enough && gpu.enough && threads.enough && handles.enough;

    GLenum glError = GL_NO_ERROR;
    int glErrors = 0;
    for (GLenum value = glGetError(); value != GL_NO_ERROR && glErrors < 16; value = glGetError()) {
        glError = value;
        ++glErrors;
    }
    report["glErrors"] = glErrors;
    if (glErrors > 0) {
        report["lastGlError"] = static_cast<int>(glError);
    }

    const bool leak = judge && (rss.leak || gpu.leak || threads.leak || handles.leak);
    report["leak"] = leak;
    report["passed"] = !leak && glErrors == 0 && frames > 0;
    report["finished"] = true;
    logLine("Soak test: " + std::to_string(frames) + " frames in " + std::to_string(wall) + " s, " + std::to_string(overBudget) + " over budget, " + (judge ? (leak ? "growth detected" : "no growth detected") : "too short to judge growth"));
    writeReport(report, outputPath);
    return report["passed"].get<bool>() ? 0 : 2;
}

}

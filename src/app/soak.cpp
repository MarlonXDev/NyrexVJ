#include "app/bench.hpp"

#include "audio/audio_features.hpp"
#include "core/frame_pacer.hpp"
#include "core/frame_stats.hpp"
#include "core/growth_check.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "core/run_conditions.hpp"
#include "core/soak_plan.hpp"
#include "platform/platform.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
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

void noteUsage(const Composition& comp, std::set<std::string>& clips, std::set<std::string>& effects) {
    const std::vector<ClipDef>& library = clipLibrary();
    const std::vector<EffectDef>& fx = effectLibrary();
    const auto noteEffects = [&](const auto& slots) {
        for (const EffectSlot& slot : slots) {
            if (slot.effect >= 0 && static_cast<size_t>(slot.effect) < fx.size()) {
                effects.insert(fx[static_cast<size_t>(slot.effect)].name);
            }
        }
    };
    for (const Layer& layer : comp.layers) {
        for (const int cell : layer.cells) {
            if (cell >= 0 && static_cast<size_t>(cell) < library.size()) {
                clips.insert(library[static_cast<size_t>(cell)].name);
            }
        }
        noteEffects(layer.fx);
    }
    noteEffects(comp.master.fx);
}

void writeReport(const json& report, const std::string& outputPath) {
    if (outputPath.empty()) {
        return;
    }
    std::ofstream out(outputPath, std::ios::trunc);
    out << report.dump(2) << '\n';
}

}

int runSoakTest(Engine& engine, const std::string& spec, const std::string& outputPath, const std::string& mediaDirectory) {
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
    plan.media = !mediaDirectory.empty();
    if (plan.media) {
        const char* dataOverride = std::getenv("NYREXVJ_DATA_DIR");
        if (dataOverride == nullptr || *dataOverride == '\0') {
            logLine("Soak test: --soak-media copies the files of the folder into the media and model folders of the data folder, so it needs NYREXVJ_DATA_DIR to point at a scratch folder and refuses to touch the real data folder");
            return 1;
        }
        logLine(std::string("Soak test: the files of '") + mediaDirectory + "' are copied into the data folder '" + dataOverride + "'");
    }

    std::vector<int> mediaClips;
    json mediaReport;
    if (plan.media) {
        namespace fs = std::filesystem;
        std::set<std::string> importedNames;
        int skipped = 0;
        std::vector<fs::path> files;
        std::error_code listError;
        for (const auto& entry : fs::directory_iterator(fs::path(std::u8string(mediaDirectory.begin(), mediaDirectory.end())), listError)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            const std::u8string text = file.u8string();
            const ImportResult result = importAsset(std::string(text.begin(), text.end()));
            if (result.kind == ImportKind::Media || result.kind == ImportKind::Model) {
                importedNames.insert(result.name);
            } else {
                ++skipped;
            }
        }
        rescanLibrary();
        engine.syncLibrary();
        const std::vector<ClipDef>& library = clipLibrary();
        for (size_t i = 0; i < library.size(); ++i) {
            const ClipDef& clip = library[i];
            if ((clip.kind == ClipKind::ImageFile || clip.kind == ClipKind::VideoFile || clip.kind == ClipKind::ModelFile) && !clip.path.empty()) {
                const std::u8string name = fs::path(std::u8string(clip.path.begin(), clip.path.end())).filename().u8string();
                if (importedNames.count(std::string(name.begin(), name.end())) > 0) {
                    mediaClips.push_back(static_cast<int>(i));
                }
            }
        }
        if (mediaClips.size() < 2) {
            logLine("Soak test: the media folder '" + mediaDirectory + "' gave " + std::to_string(mediaClips.size()) + " usable clips, at least two are needed");
            return 1;
        }
        mediaReport["directory"] = mediaDirectory;
        mediaReport["filesSkipped"] = skipped;
        mediaReport["clipCount"] = mediaClips.size();
    }

    FramePacer pacer;
    pacer.setTargetFps(plan.fps);
    const double period = pacer.period();
    const double periodMs = period * 1000.0;

    json report;
    const auto glText = [](GLenum name) {
        const GLubyte* value = glGetString(name);
        return value != nullptr ? std::string(reinterpret_cast<const char*>(value)) : std::string();
    };
    RunContext context;
    context.graphics = GraphicsInfo{glText(GL_VENDOR), glText(GL_RENDERER), glText(GL_VERSION), glText(GL_SHADING_LANGUAGE_VERSION)};
    context.width = engine.width();
    context.height = engine.height();
    context.quality = "cycled: automatic, high, low";
    context.targetFps = pacer.targetFps();
    context.scenario = plan.media ? "media churn: a clip launch every " + std::to_string(plan.mediaEvery) + " s on a rotating layer, a layer cleared every " + std::to_string(plan.clearEvery) + " s, deck switch every " + std::to_string(plan.deckEvery) + " s, quality change every " + std::to_string(plan.qualityEvery) + " s" : "factory looks: a look change every " + std::to_string(plan.lookEvery) + " s, deck switch every " + std::to_string(plan.deckEvery) + " s, quality change every " + std::to_string(plan.qualityEvery) + " s";
    context.durationSeconds = plan.seconds;
    report["schema"] = kRunReportSchema;
    report["schemaVersion"] = kRunReportSchemaVersion;
    report["kind"] = plan.media ? "soak-media" : "soak";
    report["conditions"] = runConditionsJson(context);
    report["renderer"] = context.graphics.renderer;
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

    std::set<std::string> usedClips;
    std::set<std::string> usedEffects;
    long long mediaLaunches = 0;
    long long mediaClears = 0;
    size_t mediaCursor = 0;
    size_t clearCursor = 0;
    int faultsMax = 0;

    const auto prepareLook = [&]() {
        Composition emptyLook;
        emptyLook.name = "Empty";
        engine.applyComposition(plan.media ? emptyLook : looks[lookIndex % looks.size()]);
        engine.duplicateActiveDeckTo(1);
        engine.comp.deckSettings.syncBeats = false;
        engine.comp.deckSettings.seconds = 0.5f;
        noteUsage(engine.comp, usedClips, usedEffects);
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
            if (events.media) {
                const int layer = static_cast<int>(mediaCursor % static_cast<size_t>(kLayerCount));
                const int clip = mediaClips[mediaCursor % mediaClips.size()];
                engine.comp.layers[static_cast<size_t>(layer)].setCell(0, clip);
                engine.trigger(layer, 0);
                noteUsage(engine.comp, usedClips, usedEffects);
                ++mediaCursor;
                ++mediaLaunches;
            }
            if (events.clear) {
                engine.clearLayer(static_cast<int>(clearCursor % static_cast<size_t>(kLayerCount)));
                ++clearCursor;
                ++mediaClears;
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
            const int faults = engine.playingFaults();
            faultsMax = std::max(faultsMax, faults);
            sample["playingFaults"] = faults;
            sample["look"] = plan.media ? std::string("media churn") : looks[(lookIndex + looks.size() - 1) % looks.size()].name;
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
    report["playingFaultsMax"] = faultsMax;
    if (plan.media) {
        mediaReport["launches"] = mediaLaunches;
        mediaReport["clears"] = mediaClears;
        report["media"] = std::move(mediaReport);
    }
    context.durationSeconds = wall;
    context.clips.assign(usedClips.begin(), usedClips.end());
    context.effects.assign(usedEffects.begin(), usedEffects.end());
    report["conditions"] = runConditionsJson(context);
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
    report["passed"] = !leak && glErrors == 0 && frames > 0 && (!plan.media || faultsMax == 0);
    report["finished"] = true;
    logLine("Soak test: " + std::to_string(frames) + " frames in " + std::to_string(wall) + " s, " + std::to_string(overBudget) + " over budget, " + (judge ? (leak ? "growth detected" : "no growth detected") : "too short to judge growth"));
    writeReport(report, outputPath);
    return report["passed"].get<bool>() ? 0 : 2;
}

}

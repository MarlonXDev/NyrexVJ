#include "app/bench.hpp"

#include "audio/audio_features.hpp"
#include "core/frame_stats.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "platform/platform.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

namespace nvj {

namespace {

using json = nlohmann::json;

double rasterizerCpuSeconds() {
#ifdef __linux__
    double total = 0.0;
    const double ticks = static_cast<double>(sysconf(_SC_CLK_TCK));
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", error)) {
        std::ifstream nameFile(entry.path() / "comm");
        std::string name;
        std::getline(nameFile, name);
        if (name.rfind("llvmpipe", 0) != 0) {
            continue;
        }
        std::ifstream statFile(entry.path() / "stat");
        std::string content;
        std::getline(statFile, content);
        const size_t close = content.rfind(')');
        if (close == std::string::npos) {
            continue;
        }
        std::istringstream rest(content.substr(close + 1));
        std::string skip;
        for (int i = 0; i < 11; ++i) {
            rest >> skip;
        }
        unsigned long long user = 0;
        unsigned long long system = 0;
        rest >> user >> system;
        total += static_cast<double>(user + system) / ticks;
    }
    return total;
#else
    return 0.0;
#endif
}

json summarize(std::vector<double> values) {
    json result;
    if (values.empty()) {
        return result;
    }
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    const auto pick = [&](double fraction) { return values[static_cast<size_t>(fraction * static_cast<double>(values.size() - 1))]; };
    result["avg"] = sum / static_cast<double>(values.size());
    result["min"] = values.front();
    result["p50"] = pick(0.5);
    result["p95"] = pick(0.95);
    result["p99"] = pick(0.99);
    result["max"] = values.back();
    return result;
}

}

int runSceneBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath) {
    const size_t first = spec.find(':');
    const size_t second = first == std::string::npos ? std::string::npos : spec.find(':', first + 1);
    int layers = std::atoi(spec.substr(0, first).c_str());
    int frames = first == std::string::npos ? 0 : std::atoi(spec.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1).c_str());
    std::string clipName = second == std::string::npos ? std::string("Torus Knot Mesh") : spec.substr(second + 1);
    layers = std::clamp(layers, 1, kLayerCount);
    frames = std::clamp(frames, 10, 100000);
    const int clip = clipIndexByName(clipName);
    if (clip < 0) {
        logLine("Benchmark: unknown clip '" + clipName + "'");
        return 1;
    }

    for (int l = 0; l < layers; ++l) {
        engine.comp.layers[static_cast<size_t>(l)].cells[0] = clip;
        engine.trigger(l, 0);
    }

    ScenePlayer* player = engine.scenePlayer(clip);
    const auto step = [&]() {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
    };
    const auto waitStart = std::chrono::steady_clock::now();
    while (player != nullptr && (player->state() == SceneState::Idle || player->state() == SceneState::Loading)) {
        step();
        if (std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(120)) {
            logLine("Benchmark: loading timed out");
            return 1;
        }
    }
    if (player != nullptr && player->state() == SceneState::Failed) {
        logLine("Benchmark: scene failed to load: " + player->error());
        return 1;
    }
    for (int i = 0; i < 30; ++i) {
        step();
    }
    glFinish();

    engine.setGpuTiming(false);
    GLuint query = 0;
    glGenQueries(1, &query);
    std::vector<double> cpu;
    std::vector<double> gpu;
    cpu.reserve(static_cast<size_t>(frames));
    gpu.reserve(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        const auto begin = std::chrono::steady_clock::now();
        glBeginQuery(GL_TIME_ELAPSED, query);
        engine.render();
        glEndQuery(GL_TIME_ELAPSED);
        const auto submitted = std::chrono::steady_clock::now();
        glFinish();
        const auto finished = std::chrono::steady_clock::now();
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
        cpu.push_back(std::chrono::duration<double, std::milli>(submitted - begin).count());
        gpu.push_back(static_cast<double>(nanoseconds) / 1.0e6);
        (void)finished;
    }
    glDeleteQueries(1, &query);

    json report;
    report["clip"] = clipName;
    report["layers"] = layers;
    report["frames"] = frames;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["cpuSubmitMs"] = summarize(cpu);
    report["gpuMs"] = summarize(gpu);
    if (player != nullptr) {
        const SceneStats& stats = player->stats();
        report["scene"] = {{"vertices", stats.vertices}, {"triangles", stats.triangles}, {"meshes", stats.meshes}, {"nodes", stats.nodes}, {"images", stats.images}, {"geometryGpuBytes", stats.gpuBytes}, {"loadMs", player->loadMilliseconds()}, {"uploadMs", player->uploadMilliseconds()}};
    }
    const GpuMemory memory = engine.gpuMemory();
    report["memory"] = {{"estimatedGpuBytes", memory.total()}, {"renderTargetBytes", memory.targets}, {"thumbnailBytes", memory.thumbnails}, {"sceneDepthBytes", memory.sceneDepth}, {"sceneGeometryBytes", memory.sceneGeometry}, {"mediaBytes", memory.media}, {"processResidentBytes", platform::processMemoryBytes()}};

    const std::string text = report.dump(2);
    logLine("Benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
}

int runVideoBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath) {
    const size_t colon = spec.find(':');
    const double seconds = std::clamp(std::atof(spec.substr(0, colon).c_str()), 2.0, 600.0);
    std::vector<std::string> stems;
    if (colon != std::string::npos) {
        std::stringstream list(spec.substr(colon + 1));
        std::string item;
        while (std::getline(list, item, ',')) {
            if (!item.empty()) {
                stems.push_back(item);
            }
        }
    }
    if (stems.empty()) {
        logLine("Benchmark: no video clips given, use SECONDS:name1,name2");
        return 1;
    }
    if (stems.size() > static_cast<size_t>(kLayerCount)) {
        stems.resize(static_cast<size_t>(kLayerCount));
    }
    std::vector<int> clips;
    for (const std::string& stem : stems) {
        const int clip = clipIndexByName(videoClipName(stem));
        if (clip < 0) {
            logLine("Benchmark: unknown video clip '" + stem + "'");
            return 1;
        }
        clips.push_back(clip);
    }
    for (size_t l = 0; l < clips.size(); ++l) {
        engine.comp.layers[l].cells[0] = clips[l];
        engine.trigger(static_cast<int>(l), 0);
    }

    const auto step = [&](double dt) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(dt);
        engine.render();
    };
    const auto allReady = [&]() {
        for (const int clip : clips) {
            MediaPlayer* player = engine.mediaPlayer(clip);
            if (player == nullptr || !player->ready()) {
                return false;
            }
        }
        return true;
    };
    const auto waitStart = std::chrono::steady_clock::now();
    while (!allReady()) {
        step(1.0 / 60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        if (std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(60)) {
            logLine("Benchmark: videos did not become ready");
            return 1;
        }
    }
    const double period = 1.0 / 60.0;
    {
        auto next = std::chrono::steady_clock::now();
        auto last = next;
        for (int i = 0; i < 60; ++i) {
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(period));
            const auto now = std::chrono::steady_clock::now();
            step(std::chrono::duration<double>(now - last).count());
            last = now;
            glFinish();
            std::this_thread::sleep_until(next);
        }
    }

    engine.setGpuTiming(false);
    GLuint query = 0;
    glGenQueries(1, &query);
    std::vector<double> work;
    std::vector<double> gpu;
    std::vector<double> interval;
    int late = 0;
    int frames = 0;
    const double cpuStart = platform::processCpuSeconds();
    const double rasterStart = rasterizerCpuSeconds();
    const auto wallStart = std::chrono::steady_clock::now();
    const auto step_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(period));
    auto next = wallStart;
    auto previous = wallStart;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count() < seconds) {
        next += step_duration;
        const auto begin = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(begin - previous).count();
        if (frames > 0) {
            interval.push_back(dt * 1000.0);
            if (dt > period * 1.5) {
                ++late;
            }
        }
        previous = begin;
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(frames == 0 ? period : dt);
        glBeginQuery(GL_TIME_ELAPSED, query);
        engine.render();
        glEndQuery(GL_TIME_ELAPSED);
        glFinish();
        const auto finished = std::chrono::steady_clock::now();
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
        work.push_back(std::chrono::duration<double, std::milli>(finished - begin).count());
        gpu.push_back(static_cast<double>(nanoseconds) / 1.0e6);
        ++frames;
        if (finished > next) {
            next = finished;
        }
        std::this_thread::sleep_until(next);
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    const double cpu = platform::processCpuSeconds() - cpuStart;
    const double raster = rasterizerCpuSeconds() - rasterStart;
    glDeleteQueries(1, &query);

    json report;
    report["clips"] = stems;
    report["seconds"] = wall;
    report["frames"] = frames;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["processCpuSeconds"] = cpu;
    report["processCpuPercentOfOneCore"] = wall > 0.0 ? cpu / wall * 100.0 : 0.0;
    report["softwareRasterizerCpuSeconds"] = raster;
    report["cpuWithoutRasterizerSeconds"] = cpu - raster;
    report["cpuWithoutRasterizerPercentOfOneCore"] = wall > 0.0 ? (cpu - raster) / wall * 100.0 : 0.0;
    report["lateFrames"] = late;
    report["workMs"] = summarize(work);
    report["gpuMs"] = summarize(gpu);
    report["frameIntervalMs"] = summarize(interval);
    json videos = json::array();
    for (size_t l = 0; l < clips.size(); ++l) {
        MediaPlayer* player = engine.mediaPlayer(clips[l]);
        json item;
        item["clip"] = stems[l];
        if (player != nullptr) {
            const VideoInfo& info = player->videoInfo();
            item["codec"] = info.codec;
            item["size"] = std::to_string(info.width) + "x" + std::to_string(info.height);
            item["textureSize"] = std::to_string(player->width()) + "x" + std::to_string(player->height());
            item["gpuBytes"] = player->gpuBytes();
            const VideoStatus status = player->status();
            const UploadStats upload = player->uploadStats();
            const VideoMemory memory = player->videoMemory();
            item["path"] = decodePathName(status.path);
            item["decoder"] = status.decoder;
            item["backend"] = status.backend;
            item["pixelFormat"] = status.pixelFormat;
            item["variant"] = status.variant;
            item["fallbackReason"] = status.fallbackReason;
            item["threads"] = status.threads;
            item["decodeMs"] = status.decodeMs;
            item["decoded"] = status.decoded;
            item["dropped"] = status.dropped;
            item["underruns"] = status.underruns;
            item["uploadRoute"] = uploadRouteName(player->uploadRoute());
            item["uploadMs"] = upload.averageMs;
            item["uploadFrames"] = upload.frames;
            item["memory"] = {{"output", memory.output}, {"planes", memory.planes}, {"compressed", memory.compressed}, {"staging", memory.staging}};
        }
        videos.push_back(std::move(item));
    }
    report["videos"] = std::move(videos);
    const GpuMemory memory = engine.gpuMemory();
    report["memory"] = {{"estimatedGpuBytes", memory.total()}, {"mediaBytes", memory.media}, {"processResidentBytes", platform::processMemoryBytes()}};
    report["videoSettings"] = {{"decoder", videoDecodeModeName(engine.videoDecodeMode())}, {"hapDirect", engine.hapDirect()}, {"gpuConversion", engine.gpuConversion()}, {"compressedTextures", engine.videoCaps().compressedTextures}, {"pixelBuffers", engine.videoCaps().pixelBuffers}};

    const std::string text = report.dump(2);
    logLine("Benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
}


namespace {

struct SelfVariant {
    VideoDecodeMode mode;
    bool hapDirect;
    bool gpuConversion;
};

struct SelfShot {
    bool ok = false;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    std::string route;
    std::string path;
    std::string decoder;
    std::string error;
};

SelfShot captureClip(Engine& engine, int clip, const SelfVariant& variant) {
    SelfShot shot;
    engine.setVideoSettings(variant.mode, variant.hapDirect, variant.gpuConversion);
    engine.clearLayer(0);
    engine.comp.layers[0].cells[0] = clip;
    engine.paused = true;
    engine.trigger(0, 0);
    const auto start = std::chrono::steady_clock::now();
    MediaPlayer* player = engine.mediaPlayer(clip);
    const auto step = [&]() {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
        glFinish();
    };
    while (player != nullptr && !player->ready()) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        if (player->state() == MediaState::Failed) {
            shot.error = player->error();
            return shot;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30)) {
            shot.error = "the video did not become ready";
            return shot;
        }
    }
    for (int i = 0; i < 4; ++i) {
        step();
    }
    if (player == nullptr || !engine.readOutput(shot.pixels, shot.width, shot.height)) {
        shot.error = "the output could not be read";
        return shot;
    }
    shot.route = uploadRouteName(player->uploadRoute());
    shot.path = decodePathName(player->status().path);
    shot.decoder = player->status().decoder;
    shot.ok = true;
    return shot;
}

}

int runVideoSelfTest(Engine& engine, const std::string& directory, const std::string& outputPath) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(fs::path(std::u8string(directory.begin(), directory.end())), error)) {
        const std::string extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".mp4" || extension == ".mov" || extension == ".mkv" || extension == ".webm")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        logLine("Video self test: no video files in '" + directory + "'");
        return 1;
    }
    for (const fs::path& file : files) {
        const std::u8string text = file.u8string();
        importAsset(std::string(text.begin(), text.end()));
    }
    rescanLibrary();
    engine.syncLibrary();

    const VideoGpuCaps& caps = engine.videoCaps();
    const SelfVariant gpu{VideoDecodeMode::Auto, true, true};
    const SelfVariant cpu{VideoDecodeMode::ForceSoftware, false, false};
    json report;
    report["renderer"] = caps.renderer;
    report["conversion"] = caps.conversion;
    report["compressedTextures"] = caps.compressedTextures;
    report["pixelBuffers"] = caps.pixelBuffers;
    json items = json::array();
    bool passed = true;
    for (const fs::path& file : files) {
        const std::u8string stemText = file.stem().u8string();
        const std::string stem(stemText.begin(), stemText.end());
        const int clip = clipIndexByName(videoClipName(stem));
        json item;
        item["file"] = file.filename().string();
        if (clip < 0) {
            item["error"] = "clip not found";
            item["passed"] = false;
            passed = false;
            items.push_back(std::move(item));
            continue;
        }
        const SelfShot reference = captureClip(engine, clip, cpu);
        const SelfShot fast = captureClip(engine, clip, gpu);
        bool itemPassed = reference.ok && fast.ok && reference.pixels.size() == fast.pixels.size();
        item["referenceRoute"] = reference.route;
        item["route"] = fast.route;
        item["path"] = fast.path;
        item["decoder"] = fast.decoder;
        if (!reference.ok || !fast.ok) {
            item["error"] = !reference.ok ? reference.error : fast.error;
        } else if (!itemPassed) {
            item["error"] = "the output sizes differ";
        } else {
            double sum = 0.0;
            int worst = 0;
            size_t count = 0;
            for (size_t i = 0; i + 3 < fast.pixels.size(); i += 4) {
                for (size_t c = 0; c < 3; ++c) {
                    const int delta = std::abs(static_cast<int>(fast.pixels[i + c]) - static_cast<int>(reference.pixels[i + c]));
                    sum += delta;
                    worst = std::max(worst, delta);
                    ++count;
                }
            }
            const double mean = count == 0 ? 0.0 : sum / static_cast<double>(count);
            item["meanDifference"] = mean;
            item["worstDifference"] = worst;
            const bool compressed = file.filename().string().find("hap") != std::string::npos;
            const std::string expected = compressed ? "compressed direct" : "gpu convert";
            const bool routeOk = (compressed && !caps.compressedTextures) || (!compressed && !caps.conversion) || fast.route == expected;
            const bool lit = mean >= 0.0 && sum > 0.0;
            item["expectedRoute"] = expected;
            itemPassed = mean < 4.0 && routeOk && lit && reference.route == "cpu rgba";
        }
        item["passed"] = itemPassed;
        passed = passed && itemPassed;
        logLine("Video self test " + item["file"].get<std::string>() + ": " + (itemPassed ? "passed" : "FAILED") + " (" + item.value("route", std::string()) + ", difference " + std::to_string(item.value("meanDifference", -1.0)) + ")");
        items.push_back(std::move(item));
    }
    engine.paused = false;
    report["clips"] = std::move(items);
    report["passed"] = passed;
    const std::string text = report.dump(2);
    logLine("Video self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return passed ? 0 : 1;
}

namespace {

bool waitForScenes(Engine& engine) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        bool loading = false;
        for (const Layer& layer : engine.comp.layers) {
            if (layer.activeColumn < 0) {
                continue;
            }
            const int clip = layer.cells[static_cast<size_t>(layer.activeColumn)];
            ScenePlayer* player = clip >= 0 ? engine.scenePlayer(clip) : nullptr;
            if (player != nullptr && (player->state() == SceneState::Idle || player->state() == SceneState::Loading)) {
                loading = true;
            }
        }
        if (!loading) {
            return true;
        }
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(120)) {
            return false;
        }
    }
}

json measureComposition(Engine& engine, int frames, double budgetMs) {
    json report;
    report["name"] = engine.comp.name;
    const RenderCost cost = estimateRenderCost(engine.comp);
    report["cost"] = {{"layers", cost.layers()}, {"effectPasses", cost.layerEffects + cost.masterEffects}, {"passes", cost.total()}};
    if (!waitForScenes(engine)) {
        report["error"] = "loading timed out";
        report["passed"] = false;
        return report;
    }
    for (int i = 0; i < 20; ++i) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
    }
    glFinish();
    GLuint query = 0;
    glGenQueries(1, &query);
    FrameStats cpu(static_cast<size_t>(frames));
    FrameStats gpu(static_cast<size_t>(frames));
    FrameStats total(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        const auto begin = std::chrono::steady_clock::now();
        glBeginQuery(GL_TIME_ELAPSED, query);
        engine.render();
        glEndQuery(GL_TIME_ELAPSED);
        const auto submitted = std::chrono::steady_clock::now();
        glFinish();
        const auto finished = std::chrono::steady_clock::now();
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
        cpu.add(std::chrono::duration<double, std::milli>(submitted - begin).count());
        gpu.add(static_cast<double>(nanoseconds) / 1.0e6);
        total.add(std::chrono::duration<double, std::milli>(finished - begin).count());
    }
    glDeleteQueries(1, &query);
    const auto toJson = [](const FrameSummary& s) {
        return json{{"avg", s.avg}, {"min", s.min}, {"p50", s.p50}, {"p95", s.p95}, {"p99", s.p99}, {"max", s.max}};
    };
    report["frames"] = frames;
    report["cpuSubmitMs"] = toJson(cpu.summary());
    report["gpuMs"] = toJson(gpu.summary());
    report["totalMs"] = toJson(total.summary());
    const GpuMemory memory = engine.gpuMemory();
    report["estimatedGpuBytes"] = memory.total();
    report["passed"] = budgetMs <= 0.0 || total.summary().avg <= budgetMs;
    return report;
}

}

int runCompositionBenchmark(Engine& engine, int frames, double budgetMs, bool allLooks, const std::string& outputPath) {
    frames = std::clamp(frames, 10, 100000);
    engine.setGpuTiming(false);
    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["budgetMs"] = budgetMs;
    bool passed = true;
    if (allLooks) {
        json items = json::array();
        for (const Composition& look : factoryPresets()) {
            engine.applyComposition(look);
            json item = measureComposition(engine, frames, budgetMs);
            passed = passed && item.value("passed", false);
            logLine("Benchmark " + look.name + ": " + (item.contains("totalMs") ? std::to_string(item["totalMs"]["avg"].get<double>()) + " ms" : std::string("failed")));
            items.push_back(std::move(item));
        }
        report["looks"] = std::move(items);
    } else {
        json item = measureComposition(engine, frames, budgetMs);
        passed = item.value("passed", false);
        report["composition"] = std::move(item);
    }
    report["passed"] = passed;
    const std::string text = report.dump(2);
    logLine("Benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return passed ? 0 : 2;
}

}

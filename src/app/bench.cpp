#include "app/bench.hpp"

#include "audio/audio_features.hpp"
#include "core/frame_stats.hpp"
#include "core/live_source.hpp"
#include "core/log.hpp"
#include "core/node_compile.hpp"
#include "core/node_graph.hpp"
#include "core/preset.hpp"
#include "media/camera_capture.hpp"
#include "platform/platform.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
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

int runSwitchBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath) {
    const size_t colon = spec.find(':');
    const double seconds = std::clamp(std::atof(spec.substr(0, colon).c_str()), 0.3, 30.0);
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
    if (stems.size() < 2) {
        logLine("Benchmark: give SECONDS:name1,name2,... with at least two video clips");
        return 1;
    }
    if (stems.size() > static_cast<size_t>(kColumnCount)) {
        stems.resize(static_cast<size_t>(kColumnCount));
    }
    std::vector<int> clips;
    for (size_t i = 0; i < stems.size(); ++i) {
        const int clip = clipIndexByName(videoClipName(stems[i]));
        if (clip < 0) {
            logLine("Benchmark: unknown video clip '" + stems[i] + "'");
            return 1;
        }
        clips.push_back(clip);
        engine.comp.layers[0].cells[i] = clip;
    }
    const double period = 1.0 / 60.0;
    const auto step = [&](double dt) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(dt);
        engine.render();
        glFinish();
    };
    const auto pace = [&](auto&& body) {
        const auto start = std::chrono::steady_clock::now();
        auto next = start;
        auto previous = start;
        const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(period));
        while (body(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), std::chrono::duration<double>(std::chrono::steady_clock::now() - previous).count())) {
            previous = std::chrono::steady_clock::now();
            next += interval;
            if (previous > next) {
                next = previous;
            }
            std::this_thread::sleep_until(next);
        }
    };

    engine.trigger(0, 0);
    bool firstReady = false;
    pace([&](double elapsed, double dt) {
        step(std::min(dt, 0.1));
        MediaPlayer* player = engine.mediaPlayer(clips[0]);
        firstReady = player != nullptr && player->ready();
        return !firstReady && elapsed < 60.0;
    });
    if (!firstReady) {
        logLine("Benchmark: the first video did not become ready");
        return 1;
    }

    const int cycles = 2;
    const int switches = static_cast<int>(clips.size()) * cycles;
    std::vector<double> blankFrames;
    std::vector<double> readyMs;
    int current = 0;
    for (int s = 0; s < switches; ++s) {
        current = (current + 1) % static_cast<int>(clips.size());
        engine.trigger(0, current);
        int blank = 0;
        double ready = -1.0;
        pace([&](double elapsed, double dt) {
            step(std::min(dt, 0.1));
            MediaPlayer* player = engine.mediaPlayer(clips[static_cast<size_t>(current)]);
            const bool shown = player != nullptr && player->ready();
            if (!shown && ready < 0.0) {
                ++blank;
            } else if (shown && ready < 0.0) {
                ready = elapsed * 1000.0;
            }
            return elapsed < seconds;
        });
        blankFrames.push_back(static_cast<double>(blank));
        readyMs.push_back(ready < 0.0 ? seconds * 1000.0 : ready);
    }

    json report;
    report["clips"] = stems;
    report["secondsPerClip"] = seconds;
    report["switches"] = switches;
    report["prewarm"] = engine.prewarmNext;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["blankFramesPerSwitch"] = summarize(blankFrames);
    report["millisecondsToFirstPicture"] = summarize(readyMs);
    report["switchesWithBlankFrames"] = static_cast<int>(std::count_if(blankFrames.begin(), blankFrames.end(), [](double value) { return value > 0.0; }));
    report["loadedClips"] = engine.loadedMediaCount();
    const GpuMemory memory = engine.gpuMemory();
    report["memory"] = {{"estimatedGpuBytes", memory.total()}, {"mediaBytes", memory.media}, {"processResidentBytes", platform::processMemoryBytes()}};
    const std::string text = report.dump(2);
    logLine("Benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
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

const char* passKindName(PassKind kind) {
    switch (kind) {
    case PassKind::Housekeeping: return "housekeeping";
    case PassKind::Source: return "source";
    case PassKind::Effect: return "effect";
    case PassKind::Composite: return "composite";
    case PassKind::Preview: return "preview";
    case PassKind::Final: return "final";
    case PassKind::Transition: return "transition";
    default: return "other";
    }
}

class PassAverages {
public:
    void add(const std::vector<ProfileSample>& samples) {
        ++frames_;
        for (const ProfileSample& sample : samples) {
            Item* found = nullptr;
            for (Item& item : items_) {
                if (item.sample.kind == sample.kind && item.sample.deck == sample.deck && item.sample.layer == sample.layer && item.sample.slot == sample.slot && item.sample.name == sample.name) {
                    found = &item;
                    break;
                }
            }
            if (found == nullptr) {
                items_.push_back(Item{sample, 0.0});
                found = &items_.back();
            }
            found->sum += sample.ms;
        }
    }

    double averageMs(PassKind kind) const {
        double sum = 0.0;
        for (const Item& item : items_) {
            if (item.sample.kind == kind) {
                sum += item.sum;
            }
        }
        return frames_ > 0 ? sum / static_cast<double>(frames_) : 0.0;
    }

    double totalMs() const {
        double sum = 0.0;
        for (const Item& item : items_) {
            sum += item.sum;
        }
        return frames_ > 0 ? sum / static_cast<double>(frames_) : 0.0;
    }

    json toJson() const {
        std::vector<const Item*> order;
        for (const Item& item : items_) {
            order.push_back(&item);
        }
        std::sort(order.begin(), order.end(), [](const Item* a, const Item* b) { return a->sum > b->sum; });
        const double total = totalMs() * static_cast<double>(std::max(frames_, 1));
        json rows = json::array();
        for (const Item* item : order) {
            json row;
            row["kind"] = passKindName(item->sample.kind);
            row["deck"] = item->sample.deck;
            row["layer"] = item->sample.layer;
            row["slot"] = item->sample.slot;
            row["name"] = item->sample.name;
            row["avgMs"] = frames_ > 0 ? item->sum / static_cast<double>(frames_) : 0.0;
            row["share"] = total > 0.0 ? item->sum / total : 0.0;
            rows.push_back(std::move(row));
        }
        return rows;
    }

private:
    struct Item {
        ProfileSample sample;
        double sum = 0.0;
    };

    std::vector<Item> items_;
    int frames_ = 0;
};

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
    const bool profiling = engine.profiling();
    GLuint query = 0;
    if (!profiling) {
        glGenQueries(1, &query);
    }
    PassAverages passes;
    FrameStats cpu(static_cast<size_t>(frames));
    FrameStats gpu(static_cast<size_t>(frames));
    FrameStats total(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        const auto begin = std::chrono::steady_clock::now();
        if (!profiling) {
            glBeginQuery(GL_TIME_ELAPSED, query);
        }
        engine.render();
        if (!profiling) {
            glEndQuery(GL_TIME_ELAPSED);
        }
        const auto submitted = std::chrono::steady_clock::now();
        glFinish();
        const auto finished = std::chrono::steady_clock::now();
        double gpuMs = 0.0;
        if (profiling) {
            engine.profileFlush();
            passes.add(engine.profiler().lastSamples());
            gpuMs = engine.profiler().lastFrameMs();
        } else {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
            gpuMs = static_cast<double>(nanoseconds) / 1.0e6;
        }
        cpu.add(std::chrono::duration<double, std::milli>(submitted - begin).count());
        gpu.add(gpuMs);
        total.add(std::chrono::duration<double, std::milli>(finished - begin).count());
    }
    if (query != 0) {
        glDeleteQueries(1, &query);
    }
    if (profiling) {
        report["passes"] = passes.toJson();
    }
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


int runShaderBenchmark(Engine& engine, int frames, const std::string& outputPath) {
    frames = std::clamp(frames, 5, 100000);
    engine.setGpuTiming(false);
    engine.setProfiling(true);

    Composition blank;
    blank.resolution = engine.comp.resolution;
    engine.applyComposition(blank);

    const auto step = [&]() {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
    };
    const auto measure = [&](PassKind kind, double& kindMs, double& totalMs) {
        for (int i = 0; i < 6; ++i) {
            step();
        }
        glFinish();
        PassAverages passes;
        for (int i = 0; i < frames; ++i) {
            step();
            glFinish();
            engine.profileFlush();
            passes.add(engine.profiler().lastSamples());
        }
        kindMs = passes.averageMs(kind);
        totalMs = passes.totalMs();
    };

    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["frames"] = frames;

    const std::vector<ClipDef>& library = clipLibrary();
    json clips = json::array();
    int reference = -1;
    double referenceMs = 0.0;
    for (size_t c = 0; c < library.size(); ++c) {
        const ClipDef& clip = library[c];
        if (clip.isScene() || clip.isMedia() || clip.kind == ClipKind::SpoutSource || !clip.error.empty()) {
            continue;
        }
        Layer& layer = engine.comp.layers[0];
        layer.fx = {};
        layer.cells[0] = static_cast<int>(c);
        layer.activeColumn = -1;
        engine.trigger(0, 0);
        double sourceMs = 0.0;
        double totalMs = 0.0;
        measure(PassKind::Source, sourceMs, totalMs);
        json item;
        item["name"] = clip.name;
        item["sourceMs"] = sourceMs;
        item["frameMs"] = totalMs;
        clips.push_back(std::move(item));
        logLine("Clip " + clip.name + ": " + std::to_string(sourceMs) + " ms");
        if (reference < 0 || sourceMs < referenceMs) {
            reference = static_cast<int>(c);
            referenceMs = sourceMs;
        }
    }
    std::sort(clips.begin(), clips.end(), [](const json& a, const json& b) { return a["sourceMs"].get<double>() > b["sourceMs"].get<double>(); });
    report["clips"] = std::move(clips);

    json effects = json::array();
    if (reference >= 0) {
        report["effectReferenceClip"] = library[static_cast<size_t>(reference)].name;
        Layer& layer = engine.comp.layers[0];
        layer.cells[0] = reference;
        layer.activeColumn = -1;
        engine.trigger(0, 0);
        const std::vector<EffectDef>& defs = effectLibrary();
        for (size_t e = 0; e < defs.size(); ++e) {
            layer.fx = {};
            setEffect(layer.fx[0], static_cast<int>(e));
            layer.fx[0].enabled = true;
            double effectMs = 0.0;
            double totalMs = 0.0;
            measure(PassKind::Effect, effectMs, totalMs);
            json item;
            item["name"] = defs[e].name;
            item["feedback"] = defs[e].usesPrevious;
            item["effectMs"] = effectMs;
            item["frameMs"] = totalMs;
            effects.push_back(std::move(item));
            logLine("Effect " + defs[e].name + ": " + std::to_string(effectMs) + " ms");
        }
        layer.fx = {};
    }
    std::sort(effects.begin(), effects.end(), [](const json& a, const json& b) { return a["effectMs"].get<double>() > b["effectMs"].get<double>(); });
    report["effects"] = std::move(effects);

    const auto top = [](const json& list, const char* key, size_t count) {
        std::string text;
        for (size_t i = 0; i < std::min(count, list.size()); ++i) {
            text += "  " + list[i]["name"].get<std::string>() + ": " + std::to_string(list[i][key].get<double>()) + " ms\n";
        }
        return text;
    };
    logLine("Most expensive clips:\n" + top(report["clips"], "sourceMs", 10));
    logLine("Most expensive effects:\n" + top(report["effects"], "effectMs", 10));

    engine.setProfiling(false);
    const std::string text = report.dump(2);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
}


namespace {

struct NodePair {
    const char* example;
    bool feedback;
    const char* body;
};

const char* const kHandWavy = R"glsl(
vec3 handHue(vec3 c, float h) {
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 toRgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c;
    float a = h * 6.28318;
    float cs = cos(a);
    float sn = sin(a);
    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return clamp(toRgb * yiq, 0.0, 1.0);
}

vec4 effect(vec2 uv, vec4 p) {
    float amount = clamp(p.x, 0.0, 1.0) * 0.08;
    float density = clamp(p.y, 0.0, 1.0) * 16.0;
    vec2 q = uv + vec2(sin(uv.y * density + uTime), cos(uv.x * density + uTime)) * amount;
    vec4 c = texture(uTex, clamp(q, 0.0, 1.0));
    float f = fract(uBeat * 0.25);
    float h = (0.5 - 0.5 * cos(f * 6.2831853)) * p.z;
    return vec4(handHue(c.rgb, h), 1.0);
}
)glsl";

const char* const kHandSwirl = R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 m = uv;
    m.x = 0.5 - abs(0.5 - m.x);
    float strength = mix(-1.0, 1.0, clamp(p.x, 0.0, 1.0));
    float radius = mix(0.05, 1.0, clamp(p.y, 0.0, 1.0));
    vec2 c = (m - 0.5) * vec2(uAspect, 1.0);
    float k = (1.0 - smoothstep(0.0, max(radius, 0.01), length(c))) * strength * 6.2831853;
    c = rot(k) * c;
    return texture(uTex, clamp(c / vec2(uAspect, 1.0) + 0.5, 0.0, 1.0));
}
)glsl";

const char* const kHandKick = R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float k = uKick * p.x + 1.0;
    vec2 q = (uv - 0.5) / max(k, 0.01) + 0.5;
    return texture(uTex, clamp(q, 0.0, 1.0));
}
)glsl";

const char* const kHandTrails = R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float decay = mix(0.7, 0.98, clamp(p.x, 0.0, 1.0));
    return max(texture(uPrev, uv) * decay, texture(uTex, uv));
}
)glsl";

const NodePair kNodePairs[] = {
    {"Wavy Rainbow", false, kHandWavy},
    {"Mirror Swirl", false, kHandSwirl},
    {"Kick Pulse", false, kHandKick},
    {"Trails", true, kHandTrails},
};

std::vector<unsigned char> readOutput(Engine& engine) {
    std::vector<unsigned char> pixels(static_cast<size_t>(engine.width()) * static_cast<size_t>(engine.height()) * 3);
    glFinish();
    glBindTexture(GL_TEXTURE_2D, engine.outputTexture());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return pixels;
}

}

int runNodeBenchmark(Engine& engine, int frames, const std::string& outputPath) {
    frames = std::clamp(frames, 5, 100000);
    engine.setGpuTiming(false);
    engine.setProfiling(true);

    Composition blank;
    blank.resolution = engine.comp.resolution;
    engine.applyComposition(blank);

    const auto step = [&]() {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
    };
    const auto measure = [&](double& effectMs, double& totalMs) {
        for (int i = 0; i < 6; ++i) {
            step();
        }
        glFinish();
        PassAverages passes;
        for (int i = 0; i < frames; ++i) {
            step();
            glFinish();
            engine.profileFlush();
            passes.add(engine.profiler().lastSamples());
        }
        effectMs = passes.averageMs(PassKind::Effect);
        totalMs = passes.totalMs();
    };

    int clipIndex = -1;
    const std::vector<ClipDef>& clips = clipLibrary();
    for (size_t c = 0; c < clips.size(); ++c) {
        const ClipDef& clip = clips[c];
        if (clip.isScene() || clip.isMedia() || clip.kind == ClipKind::SpoutSource || !clip.error.empty()) {
            continue;
        }
        if (clip.name == "Plasma") {
            clipIndex = static_cast<int>(c);
            break;
        }
        if (clipIndex < 0) {
            clipIndex = static_cast<int>(c);
        }
    }
    if (clipIndex < 0) {
        logLine("Node benchmark: no clip available");
        return 1;
    }
    Layer& layer = engine.comp.layers[0];
    layer.fx = {};
    layer.cells[0] = clipIndex;
    layer.activeColumn = -1;
    engine.trigger(0, 0);

    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["frames"] = frames;
    report["clip"] = clips[static_cast<size_t>(clipIndex)].name;

    json compile = json::array();
    for (const ExampleGraph& example : exampleGraphs()) {
        const GraphDocument graph = example.build();
        const int rounds = 300;
        const auto start = std::chrono::steady_clock::now();
        const unsigned long long hash = compileGraph(graph).hash;
        bool stable = true;
        for (int i = 0; i < rounds; ++i) {
            stable = stable && compileGraph(graph).hash == hash;
        }
        const double micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / rounds;
        json item;
        item["name"] = example.name;
        item["nodes"] = graph.nodes.size();
        item["compileMicroseconds"] = micros;
        item["hashStable"] = stable;
        compile.push_back(std::move(item));
        logLine(std::string("Graph ") + example.name + ": compile " + std::to_string(micros) + " us");
    }
    report["graphCompile"] = std::move(compile);

    json pairs = json::array();
    int failed = 0;
    for (const NodePair& pair : kNodePairs) {
        GraphDocument graph;
        bool found = false;
        for (const ExampleGraph& example : exampleGraphs()) {
            if (std::string(example.name) == pair.example) {
                graph = example.build();
                found = true;
                break;
            }
        }
        if (!found) {
            ++failed;
            continue;
        }
        const CompiledGraph compiled = compileGraph(graph);
        if (!compiled.ok) {
            ++failed;
            logLine(std::string("Node benchmark: ") + pair.example + " does not compile");
            continue;
        }
        std::array<float, kParamCount> defaults{};
        for (int i = 0; i < kParamCount; ++i) {
            defaults[static_cast<size_t>(i)] = graph.controls[static_cast<size_t>(i)].value;
        }
        const int graphEffect = registerEffect(std::string("Bench graph ") + pair.example, compiled.glsl, compiled.usesPrevious, defaults);
        const int handEffect = registerEffect(std::string("Bench hand ") + pair.example, pair.body, pair.feedback, defaults);
        engine.syncEffects();

        json item;
        item["name"] = pair.example;
        item["feedback"] = pair.feedback;
        item["textureReads"] = compiled.textureReads;
        double graphMs = 0.0;
        double handMs = 0.0;
        double graphTotal = 0.0;
        double handTotal = 0.0;
        for (int round = 0; round < 2; ++round) {
            layer.fx = {};
            setEffect(layer.fx[0], graphEffect);
            measure(graphMs, graphTotal);
            layer.fx = {};
            setEffect(layer.fx[0], handEffect);
            measure(handMs, handTotal);
        }
        item["graphEffectMs"] = graphMs;
        item["handEffectMs"] = handMs;
        item["graphFrameMs"] = graphTotal;
        item["handFrameMs"] = handTotal;
        item["ratio"] = handMs > 0.0 ? graphMs / handMs : 0.0;

        if (!pair.feedback) {
            layer.fx = {};
            setEffect(layer.fx[0], graphEffect);
            step();
            const std::vector<unsigned char> first = readOutput(engine);
            layer.fx = {};
            setEffect(layer.fx[0], handEffect);
            engine.render();
            const std::vector<unsigned char> second = readOutput(engine);
            int maxDiff = 0;
            double sum = 0.0;
            for (size_t i = 0; i < first.size() && i < second.size(); ++i) {
                const int diff = std::abs(static_cast<int>(first[i]) - static_cast<int>(second[i]));
                maxDiff = std::max(maxDiff, diff);
                sum += diff;
            }
            item["maxPixelDifference"] = maxDiff;
            item["meanPixelDifference"] = first.empty() ? 0.0 : sum / static_cast<double>(first.size());
            if (maxDiff > 2) {
                ++failed;
                logLine(std::string("Node benchmark: ") + pair.example + " differs from the hand written effect by " + std::to_string(maxDiff));
            }
        }
        logLine(std::string("Node benchmark ") + pair.example + ": graph " + std::to_string(graphMs) + " ms, hand written " + std::to_string(handMs) + " ms");
        pairs.push_back(std::move(item));
    }
    layer.fx = {};
    report["pairs"] = std::move(pairs);

    {
        GraphDocument graph = newGraph("Macro check");
        int pictureId = 0;
        int outputId = 0;
        for (const GraphNode& node : graph.nodes) {
            pictureId = node.type == "picture" ? node.id : pictureId;
            outputId = node.type == "output" ? node.id : outputId;
        }
        const int hueId = graph.addNode("hue", 0.0f, 0.0f);
        const int macroId = graph.addNode("macro", 0.0f, 0.0f);
        graph.disconnect(outputId, 0);
        const std::string linkError = graph.connect(pictureId, 0, hueId, 0) + graph.connect(macroId, 0, hueId, 1) + graph.connect(hueId, 0, outputId, 0);
        const CompiledGraph compiled = compileGraph(graph);
        json check;
        if (!linkError.empty() || !compiled.ok || !compiled.usesMacros) {
            ++failed;
            check["ok"] = false;
            logLine("Node benchmark: the macro check graph is not valid");
        } else {
            const int effect = registerEffect("Bench macro check", compiled.glsl, false, {0.5f, 0.5f, 0.5f, 0.5f}, true);
            engine.syncEffects();
            const float savedMacro = engine.comp.modulators.macros[0];
            const auto difference = [](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
                int most = 0;
                for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
                    most = std::max(most, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
                }
                return most;
            };
            layer.fx = {};
            step();
            const std::vector<unsigned char> plain = readOutput(engine);
            setEffect(layer.fx[0], effect);
            engine.comp.modulators.macros[0] = 0.0f;
            engine.render();
            const std::vector<unsigned char> atZero = readOutput(engine);
            engine.comp.modulators.macros[0] = 0.5f;
            engine.render();
            const std::vector<unsigned char> atHalf = readOutput(engine);
            engine.comp.modulators.macros[0] = savedMacro;
            layer.fx = {};
            const int baseline = difference(plain, atZero);
            const int shifted = difference(atZero, atHalf);
            check["ok"] = baseline <= 3 && shifted >= 20;
            check["differenceAtZero"] = baseline;
            check["differenceAtHalf"] = shifted;
            logLine("Node benchmark macro check: " + std::to_string(baseline) + " at 0, " + std::to_string(shifted) + " at 0.5");
            if (baseline > 3 || shifted < 20) {
                ++failed;
            }
        }
        report["macroCheck"] = std::move(check);
    }
    engine.setProfiling(false);
    const std::string text = report.dump(2);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return failed > 0 ? 1 : 0;
}


int runMemoryBenchmark(Engine& engine, const std::string& outputPath) {
    engine.setGpuTiming(false);
    const auto step = [&]() {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(1.0 / 60.0);
        engine.render();
    };
    const auto snapshot = [&](const std::string& phase) {
        glFinish();
        const GpuMemory memory = engine.gpuMemory();
        json item;
        item["phase"] = phase;
        item["targetsBytes"] = memory.targets;
        item["totalBytes"] = memory.total();
        item["layerWidth"] = engine.layerWidth();
        item["layerHeight"] = engine.layerHeight();
        logLine("Memory " + phase + ": targets " + std::to_string(memory.targets / 1024) + " KiB, total " + std::to_string(memory.total() / 1024) + " KiB");
        return item;
    };

    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    json phases = json::array();

    for (int i = 0; i < 10; ++i) {
        step();
    }
    phases.push_back(snapshot("idle"));

    engine.duplicateActiveDeckTo(1);
    engine.comp.deckSettings.syncBeats = false;
    engine.comp.deckSettings.seconds = 0.25f;
    engine.switchDeck(1);
    for (int i = 0; i < 6; ++i) {
        step();
    }
    phases.push_back(snapshot("duringTransition"));
    for (int i = 0; i < 20; ++i) {
        step();
    }
    phases.push_back(snapshot("afterTransition"));
    for (int i = 0; i < 320; ++i) {
        step();
    }
    phases.push_back(snapshot("fiveSecondsLater"));

    const char* names[] = {"quality100", "quality75", "quality50"};
    for (int mode = 0; mode < 3; ++mode) {
        engine.setQuality(mode, 1000.0 / 60.0);
        for (int i = 0; i < 4; ++i) {
            step();
        }
        phases.push_back(snapshot(names[mode]));
    }
    engine.setQuality(0, 1000.0 / 60.0);
    for (int i = 0; i < 4; ++i) {
        step();
    }
    phases.push_back(snapshot("quality100Again"));

    report["phases"] = std::move(phases);
    const std::string text = report.dump(2);
    logLine("Memory benchmark:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
}

}

namespace nvj {

namespace {

struct Rgb8 {
    int r;
    int g;
    int b;
};

Rgb8 referenceUyvy(int y, int u, int v, bool hd) {
    const double kr = hd ? 0.2126 : 0.299;
    const double kb = hd ? 0.0722 : 0.114;
    const double kg = 1.0 - kr - kb;
    const double yn = (y - 16) / 219.0;
    const double cb = (u - 128) / 224.0;
    const double cr = (v - 128) / 224.0;
    const double r = yn + 2.0 * (1.0 - kr) * cr;
    const double b = yn + 2.0 * (1.0 - kb) * cb;
    const double g = (yn - kr * r - kb * b) / kg;
    const auto pack = [](double value) { return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0)); };
    return {pack(r), pack(g), pack(b)};
}

struct UyvyCase {
    std::string name;
    int width;
    int height;
    int outWidth;
    int outHeight;
    bool gradient;
    int y;
    int u;
    int v;
};

json runUyvyCase(VideoUploader& uploader, const UyvyCase& testCase) {
    json item;
    item["name"] = testCase.name;
    const int width = testCase.width;
    const int height = testCase.height;
    std::vector<uint8_t> data(static_cast<size_t>(width) * 2u * static_cast<size_t>(height));
    std::vector<Rgb8> expected(static_cast<size_t>(width) * static_cast<size_t>(height));
    const bool hd = height >= 720;
    for (int row = 0; row < height; ++row) {
        for (int pair = 0; pair < width / 2; ++pair) {
            int y0 = testCase.y;
            int y1 = testCase.y;
            int u = testCase.u;
            int v = testCase.v;
            if (testCase.gradient) {
                y0 = 16 + (pair * 2 * 219) / std::max(width - 1, 1);
                y1 = 16 + ((pair * 2 + 1) * 219) / std::max(width - 1, 1);
                u = 16 + (row * 224) / std::max(height - 1, 1);
                v = 240 - (pair * 224) / std::max(width / 2 - 1, 1);
            }
            const size_t at = (static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(pair) * 2u) * 2u;
            data[at] = static_cast<uint8_t>(u);
            data[at + 1] = static_cast<uint8_t>(y0);
            data[at + 2] = static_cast<uint8_t>(v);
            data[at + 3] = static_cast<uint8_t>(y1);
            expected[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(pair) * 2u] = referenceUyvy(y0, u, v, hd);
            expected[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(pair) * 2u + 1u] = referenceUyvy(y1, u, v, hd);
        }
    }
    VideoFrame frame;
    frame.layout = VideoLayout::PackedUyvy;
    frame.width = width;
    frame.height = height;
    frame.outWidth = testCase.outWidth;
    frame.outHeight = testCase.outHeight;
    frame.matrix = hd ? 1 : 0;
    frame.planeCount = 1;
    frame.planes[0].data = data.data();
    frame.planes[0].width = width / 2;
    frame.planes[0].height = height;
    frame.planes[0].stride = width * 2;
    frame.planes[0].channels = 4;
    frame.planes[0].bytes = 1;
    const bool presented = uploader.present(frame);
    item["presented"] = presented;
    if (!presented || uploader.texture() == 0) {
        item["passed"] = false;
        return item;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(uploader.width()) * static_cast<size_t>(uploader.height()) * 4u);
    glBindTexture(GL_TEXTURE_2D, uploader.texture());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    item["outputSize"] = std::to_string(uploader.width()) + "x" + std::to_string(uploader.height());
    item["route"] = uploadRouteName(uploader.route());
    bool passed = uploader.width() == testCase.outWidth && uploader.height() == testCase.outHeight && uploader.route() == UploadRoute::GpuConvert;
    if (testCase.outWidth == width && testCase.outHeight == height) {
        int worst = 0;
        double sum = 0.0;
        for (size_t i = 0; i < expected.size(); ++i) {
            const int dr = std::abs(static_cast<int>(pixels[i * 4]) - expected[i].r);
            const int dg = std::abs(static_cast<int>(pixels[i * 4 + 1]) - expected[i].g);
            const int db = std::abs(static_cast<int>(pixels[i * 4 + 2]) - expected[i].b);
            worst = std::max({worst, dr, dg, db});
            sum += dr + dg + db;
        }
        item["worstDifference"] = worst;
        item["meanDifference"] = sum / (static_cast<double>(expected.size()) * 3.0);
        passed = passed && worst <= 2;
    } else {
        double reference[3] = {0.0, 0.0, 0.0};
        double actual[3] = {0.0, 0.0, 0.0};
        for (const Rgb8& pixel : expected) {
            reference[0] += pixel.r;
            reference[1] += pixel.g;
            reference[2] += pixel.b;
        }
        const size_t outPixels = static_cast<size_t>(uploader.width()) * static_cast<size_t>(uploader.height());
        for (size_t i = 0; i < outPixels; ++i) {
            actual[0] += pixels[i * 4];
            actual[1] += pixels[i * 4 + 1];
            actual[2] += pixels[i * 4 + 2];
        }
        double worstMean = 0.0;
        for (int c = 0; c < 3; ++c) {
            worstMean = std::max(worstMean, std::abs(reference[c] / static_cast<double>(expected.size()) - actual[c] / static_cast<double>(outPixels)));
        }
        item["meanChannelDifference"] = worstMean;
        passed = passed && worstMean <= 4.0;
    }
    item["passed"] = passed;
    return item;
}

int threadCount() {
#ifdef __linux__
    std::error_code ec;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", ec)) {
        (void)entry;
        ++count;
    }
    return count;
#else
    return -1;
#endif
}

}

int runLiveSelfTest(Engine& engine, const std::string& outputPath) {
    json report;
    bool passed = true;
    const VideoGpuCaps& caps = engine.videoCaps();
    report["renderer"] = caps.renderer;
    report["conversion"] = caps.conversion;
    if (!caps.conversion) {
        report["error"] = "the picture conversion shader is not available";
        report["passed"] = false;
        logLine("Live self test: " + report["error"].get<std::string>());
        return 1;
    }

    json shader = json::array();
    {
        VideoGpu gpu;
        gpu.create();
        VideoUploader uploader(gpu);
        const std::vector<UyvyCase> cases = {
            {"red 601 (480p)", 64, 48, 64, 48, false, 82, 90, 240},
            {"green 601 (480p)", 64, 48, 64, 48, false, 145, 54, 34},
            {"blue 601 (480p)", 64, 48, 64, 48, false, 41, 240, 110},
            {"white", 64, 48, 64, 48, false, 235, 128, 128},
            {"black", 64, 48, 64, 48, false, 16, 128, 128},
            {"red 709 (720p)", 1280, 720, 1280, 720, false, 63, 102, 240},
            {"gradient 601", 256, 144, 256, 144, true, 0, 0, 0},
            {"gradient 709", 1280, 720, 1280, 720, true, 0, 0, 0},
            {"gradient 1080p", 1920, 1080, 1920, 1080, true, 0, 0, 0},
            {"downscale 4k to 1080p", 3840, 2160, 1920, 1080, true, 0, 0, 0},
        };
        for (const UyvyCase& testCase : cases) {
            json item = runUyvyCase(uploader, testCase);
            passed = passed && item.value("passed", false);
            logLine("Live self test shader " + testCase.name + ": " + (item.value("passed", false) ? "passed" : "FAILED"));
            shader.push_back(std::move(item));
        }
        VideoFrame bad;
        bad.layout = VideoLayout::PackedUyvy;
        bad.width = 63;
        bad.height = 4;
        bad.outWidth = 63;
        bad.outHeight = 4;
        bad.planeCount = 1;
        uint8_t dummy[512] = {};
        bad.planes[0].data = dummy;
        bad.planes[0].width = 31;
        bad.planes[0].height = 4;
        bad.planes[0].stride = 126;
        bad.planes[0].channels = 4;
        bad.planes[0].bytes = 1;
        const bool rejected = !uploader.present(bad);
        report["oddWidthRejected"] = rejected;
        passed = passed && rejected;
        uploader.release();
        gpu.destroy();
    }
    report["shader"] = std::move(shader);

    json camera;
    const CameraSupport support = cameraSupport();
    camera["supported"] = support.ffmpeg && support.devices;
    if (!(support.ffmpeg && support.devices)) {
        camera["skipped"] = support.problem;
    } else {
        const int baselineThreads = threadCount();
        engine.setVideoSettings(VideoDecodeMode::Auto, true, true);
        {
            const std::unique_ptr<Composition> neutral = std::make_unique<Composition>();
            engine.comp = *neutral;
        }
        engine.blackout = false;
        engine.clearLayer(0);
        std::array<std::string, kLiveInputSlots> none;
        std::array<std::string, kLiveInputSlots> cameras;
        cameras[0] = "lavfi:color=c=red:size=640x360:rate=30,format=rgb24";
        engine.configureLive(none, cameras, std::string(), CameraMode{});
        const int clip = clipIndexByName("Camera 1");
        camera["clipFound"] = clip >= 0;
        passed = passed && clip >= 0;
        if (clip >= 0) {
            engine.comp.layers[0].cells[0] = clip;
            engine.paused = true;
            engine.trigger(0, 0);
            const auto start = std::chrono::steady_clock::now();
            const auto step = [&](double dt) {
                engine.setAudio(virtualAudio(engine.beat(), engine.time()));
                engine.update(dt);
                engine.render();
                glFinish();
            };
            while (engine.liveInfo(LiveKind::Camera, 0).uploaded < 3 && std::chrono::steady_clock::now() - start < std::chrono::seconds(15)) {
                step(1.0 / 60.0);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            for (int i = 0; i < 3; ++i) {
                step(1.0 / 60.0);
            }
            const LiveInputInfo info = engine.liveInfo(LiveKind::Camera, 0);
            camera["uploaded"] = info.uploaded;
            camera["route"] = uploadRouteName(info.route);
            camera["state"] = liveStateName(info.status.state);
            camera["size"] = std::to_string(info.status.width) + "x" + std::to_string(info.status.height);
            camera["latencyMs"] = info.latencyMs;
            std::vector<unsigned char> pixels;
            int width = 0;
            int height = 0;
            bool colorOk = false;
            if (engine.readOutput(pixels, width, height) && width > 0 && height > 0) {
                const size_t centre = (static_cast<size_t>(height / 2) * static_cast<size_t>(width) + static_cast<size_t>(width / 2)) * 4u;
                camera["centerPixel"] = {pixels[centre], pixels[centre + 1], pixels[centre + 2]};
                colorOk = pixels[centre] >= 240 && pixels[centre + 1] <= 12 && pixels[centre + 2] <= 12;
            }
            camera["colorOk"] = colorOk;
            const bool live = info.status.state == LiveState::Live && info.route == UploadRoute::GpuConvert && info.uploaded >= 3;
            const size_t liveBytes = engine.gpuMemory().live;
            camera["gpuBytesWhileActive"] = liveBytes;
            passed = passed && live && colorOk && liveBytes > 0;

            engine.clearLayer(0);
            double elapsed = 0.0;
            const auto releaseStart = std::chrono::steady_clock::now();
            bool released = false;
            while (std::chrono::steady_clock::now() - releaseStart < std::chrono::seconds(20)) {
                engine.update(0.5);
                engine.render();
                glFinish();
                elapsed += 0.5;
                const LiveInputInfo now = engine.liveInfo(LiveKind::Camera, 0);
                if (!now.active && engine.gpuMemory().live == 0) {
                    released = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            camera["releasedWhenUnused"] = released;
            camera["releaseAfterEngineSeconds"] = elapsed;
            passed = passed && released && elapsed >= kLiveIdleSeconds;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const int threadsAfter = threadCount();
            camera["threadsBefore"] = baselineThreads;
            camera["threadsAfter"] = threadsAfter;
            if (baselineThreads >= 0) {
                passed = passed && threadsAfter <= baselineThreads;
            }
            camera["gpuBytesAfterRelease"] = engine.gpuMemory().live;
        }
    }
    report["camera"] = std::move(camera);
    report["passed"] = passed;
    const std::string text = report.dump(2);
    logLine("Live self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return passed ? 0 : 1;
}

int runLiveBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath) {
    const size_t first = spec.find(':');
    const size_t second = first == std::string::npos ? std::string::npos : spec.find(':', first + 1);
    if (second == std::string::npos) {
        logLine("Benchmark: use SECONDS:camera:SOURCE or SECONDS:ndi:NAME");
        return 1;
    }
    const double seconds = std::clamp(std::atof(spec.substr(0, first).c_str()), 2.0, 600.0);
    const std::string kindText = spec.substr(first + 1, second - first - 1);
    const std::string source = spec.substr(second + 1);
    const bool ndi = kindText == "ndi";
    if (!ndi && kindText != "camera") {
        logLine("Benchmark: the kind must be camera or ndi");
        return 1;
    }
    std::array<std::string, kLiveInputSlots> ndiNames;
    std::array<std::string, kLiveInputSlots> cameraNames;
    (ndi ? ndiNames : cameraNames)[0] = source;
    engine.configureLive(ndiNames, cameraNames, "NyrexVJ", CameraMode{});
    const int clip = clipIndexByName(ndi ? "NDI Input 1" : "Camera 1");
    if (clip < 0) {
        return 1;
    }
    engine.clearLayer(0);
    engine.comp.layers[0].cells[0] = clip;
    engine.trigger(0, 0);
    const LiveKind kind = ndi ? LiveKind::Ndi : LiveKind::Camera;
    const double period = 1.0 / 60.0;
    const auto step = [&](double dt) {
        engine.setAudio(virtualAudio(engine.beat(), engine.time()));
        engine.update(dt);
        engine.render();
    };
    const auto waitStart = std::chrono::steady_clock::now();
    while (engine.liveInfo(kind, 0).uploaded < 10) {
        step(period);
        glFinish();
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        if (std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(30)) {
            logLine("Benchmark: the live source did not deliver pictures: " + engine.liveInfo(kind, 0).startError + " " + engine.liveInfo(kind, 0).status.error);
            return 1;
        }
    }
    engine.resetLiveLatency();
    engine.setGpuTiming(false);
    GLuint query = 0;
    glGenQueries(1, &query);
    std::vector<double> work;
    std::vector<double> gpu;
    std::vector<double> interval;
    std::vector<double> latency;
    int frames = 0;
    int late = 0;
    unsigned long long uploadedStart = engine.liveInfo(kind, 0).uploaded;
    const double cpuStart = platform::processCpuSeconds();
    const double rasterStart = rasterizerCpuSeconds();
    const auto wallStart = std::chrono::steady_clock::now();
    const auto stepDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(period));
    auto next = wallStart;
    auto previous = wallStart;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count() < seconds) {
        next += stepDuration;
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
        latency.push_back(engine.liveInfo(kind, 0).latencyMs);
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
    const LiveInputInfo info = engine.liveInfo(kind, 0);

    json report;
    report["kind"] = kindText;
    report["source"] = source;
    report["seconds"] = wall;
    report["frames"] = frames;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["processCpuSeconds"] = cpu;
    report["processCpuPercentOfOneCore"] = wall > 0.0 ? cpu / wall * 100.0 : 0.0;
    report["softwareRasterizerCpuSeconds"] = raster;
    report["cpuWithoutRasterizerPercentOfOneCore"] = wall > 0.0 ? (cpu - raster) / wall * 100.0 : 0.0;
    report["lateFrames"] = late;
    report["workMs"] = summarize(work);
    report["gpuMs"] = summarize(gpu);
    report["frameIntervalMs"] = summarize(interval);
    report["captureToUploadMs"] = summarize(latency);
    report["captureToUploadMaxMs"] = info.latencyMaxMs;
    report["uploadMs"] = info.uploadMs;
    report["uploadRoute"] = uploadRouteName(info.route);
    report["sourceSize"] = std::to_string(info.status.width) + "x" + std::to_string(info.status.height);
    report["sourceFps"] = info.status.fps;
    report["picturesUploaded"] = info.uploaded - uploadedStart;
    report["picturesDropped"] = info.status.dropped;
    report["state"] = liveStateName(info.status.state);
    report["memory"] = {{"output", info.memory.output}, {"planes", info.memory.planes}, {"staging", info.memory.staging}, {"liveTotal", info.memory.total()}, {"processResidentBytes", platform::processMemoryBytes()}};
    const std::string text = report.dump(2);
    logLine("Benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
}

}

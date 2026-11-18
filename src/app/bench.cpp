#include "app/bench.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;

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
    report["memory"] = {{"estimatedGpuBytes", memory.total()}, {"renderTargetBytes", memory.targets}, {"thumbnailBytes", memory.thumbnails}, {"sceneDepthBytes", memory.sceneDepth}, {"sceneGeometryBytes", memory.sceneGeometry}, {"processResidentBytes", platform::processMemoryBytes()}};

    const std::string text = report.dump(2);
    logLine("Benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return 0;
}

}

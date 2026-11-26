#include "app/gpu_diag_selftest.hpp"

#include "core/gpu_diag.hpp"
#include "core/hardware_sync.hpp"
#include "core/log.hpp"
#include "media/video_types.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/media_player.hpp"
#include "render/video_gpu.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

constexpr int kProbeImages = kColumnCount;
constexpr int kProbeSize = 3584;
constexpr std::uint64_t kBudget = 256ull * gpudiag::kMiB;
constexpr const char* kPrefix = "gd_selftest_";

class Report {
public:
    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("GPU diagnostics self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void skip(const std::string& name, const std::string& detail) {
        json item;
        item["name"] = name;
        item["skipped"] = true;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        logLine("GPU diagnostics self test " + name + ": skipped (" + detail + ")");
    }

    int failed() const { return failed_; }
    json& items() { return items_; }

private:
    json items_ = json::array();
    int failed_ = 0;
};

std::string fixed(double value, int digits = 1) {
    char text[40];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

std::string megabytes(std::uint64_t bytes) {
    return fixed(static_cast<double>(bytes) / static_cast<double>(gpudiag::kMiB)) + " MB";
}

double since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void step(Engine& engine, int frames, double dt) {
    for (int i = 0; i < frames; ++i) {
        engine.update(dt);
        engine.render();
    }
    glFinish();
}

template <class Done>
int stepUntil(Engine& engine, int maxFrames, double dt, Done done) {
    for (int i = 0; i < maxFrames; ++i) {
        engine.update(dt);
        engine.render();
        if (done()) {
            glFinish();
            return i + 1;
        }
    }
    glFinish();
    return -1;
}

template <class Done>
double stepForSeconds(Engine& engine, double seconds, double dt, Done done) {
    const auto start = Clock::now();
    while (since(start) < seconds * 1000.0) {
        engine.update(dt);
        engine.render();
        if (done()) {
            break;
        }
    }
    glFinish();
    return since(start) / 1000.0;
}

int litPixels(Engine& engine) {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!engine.readOutput(pixels, width, height)) {
        return -1;
    }
    int total = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        total += static_cast<int>(pixels[i]) + static_cast<int>(pixels[i + 1]) + static_cast<int>(pixels[i + 2]) >= 30 ? 1 : 0;
    }
    return total;
}

bool hasExtension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* text = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (text != nullptr && std::strcmp(text, name) == 0) {
            return true;
        }
    }
    return false;
}

bool writeProbeImage(const fs::path& path, int size, int variant) {
    std::vector<unsigned char> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 3u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            unsigned char* p = &pixels[(static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 3u];
            p[0] = static_cast<unsigned char>(40 + (x * 180) / size);
            p[1] = static_cast<unsigned char>(40 + (y * 180) / size);
            p[2] = static_cast<unsigned char>(60 + variant * 20 + (((x / 256) + (y / 256)) & 1) * 40);
        }
    }
    const std::string text = path.string();
    return stbi_write_bmp(text.c_str(), size, size, 3, pixels.data()) != 0;
}

struct Probe {
    std::vector<std::string> stems;
    std::vector<int> clips;
};

void removeProbeFiles() {
    std::error_code ec;
    std::vector<fs::path> doomed;
    for (fs::directory_iterator it(fs::path(mediaDirectory()), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(kPrefix, 0) == 0) {
            doomed.push_back(it->path());
        }
    }
    for (const fs::path& path : doomed) {
        fs::remove(path, ec);
    }
}

bool prepareProbe(Engine& engine, Report& report, Probe& probe) {
    removeProbeFiles();
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path() / ("nyrexvj-gpudiag-" + std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(temp, ec);
    int imported = 0;
    for (int i = 0; i < kProbeImages; ++i) {
        const std::string stem = std::string(kPrefix) + std::to_string(i);
        const fs::path path = temp / (stem + ".bmp");
        if (!writeProbeImage(path, kProbeSize, i)) {
            continue;
        }
        const std::u8string text = path.u8string();
        const ImportResult result = importAsset(std::string(text.begin(), text.end()));
        imported += result.kind == ImportKind::Media ? 1 : 0;
        probe.stems.push_back(stem);
    }
    fs::remove_all(temp, ec);
    rescanLibrary();
    engine.syncLibrary();
    for (const std::string& stem : probe.stems) {
        probe.clips.push_back(clipIndexByName(imageClipName(stem)));
    }
    const bool found = static_cast<int>(probe.clips.size()) == kProbeImages && std::all_of(probe.clips.begin(), probe.clips.end(), [](int clip) { return clip >= 0; });
    report.record("the probe images can be imported", imported == kProbeImages && found, std::to_string(imported) + " images of " + std::to_string(kProbeSize) + " x " + std::to_string(kProbeSize) + " pixels imported, all found in the clip library: " + (found ? "yes" : "no"));
    return found;
}

bool ready(Engine& engine, int clip) {
    MediaPlayer* player = engine.mediaPlayer(clip);
    return player != nullptr && player->state() == MediaState::Ready;
}

int readyCount(Engine& engine, const Probe& probe) {
    int count = 0;
    for (const int clip : probe.clips) {
        count += ready(engine, clip) ? 1 : 0;
    }
    return count;
}

void placeRow(Engine& engine, const Probe& probe) {
    for (int c = 0; c < kColumnCount; ++c) {
        engine.comp.layers[0].cells[static_cast<size_t>(c)] = probe.clips[static_cast<size_t>(c)];
    }
}

void setPolicy(Engine& engine, std::uint64_t budget, bool trim, bool lowerQuality) {
    MemoryPolicy policy;
    policy.budgetBytes = budget;
    policy.trim = trim;
    policy.lowerQuality = lowerQuality;
    engine.setMemoryPolicy(policy);
}

bool stateIsClean() {
    GLint program = 0;
    GLint vao = 0;
    GLint buffer = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
    return program == 0 && vao == 0 && buffer == 0;
}

void testHardwareSync(Report& report, Engine& engine, json& summary) {
    const HardwareSyncStatus& status = engine.hardwareSync();
    const std::string renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const bool softwareByName = renderer.find("llvmpipe") != std::string::npos || renderer.find("softpipe") != std::string::npos || renderer.find("swrast") != std::string::npos;
    json entry = json::object();
    entry["state"] = hardwareSyncStateName(status.state);
    entry["graphics"] = graphicsVendorName(status.vendor);
    entry["driverLists"] = joinExtensionNames(status.names);
    summary["hardwareFrameLock"] = entry;
    report.record("the hardware frame lock status was read from the driver", status.state != HardwareSyncState::Unknown, std::string("state: ") + hardwareSyncStateName(status.state) + ", graphics: " + graphicsVendorName(status.vendor) + ", driver lists: " + (status.names.empty() ? std::string("nothing") : joinExtensionNames(status.names)));
    report.record("a software renderer is never reported as offering hardware frame lock", !status.softwareRenderer || status.state == HardwareSyncState::NotOffered, renderer);
    report.record("the software renderer is recognised by the status", !softwareByName || status.softwareRenderer, renderer);
    report.record("the state agrees with the names the driver lists", (status.state == HardwareSyncState::OfferedByDriver) == (!status.names.empty() && !status.softwareRenderer), "names: " + std::to_string(status.names.size()));
}

void testDriver(Report& report, Engine& engine, json& summary) {
    step(engine, 40, 0.05);
    const Diagnostics& d = engine.diagnostics();
    const bool nvx = hasExtension("GL_NVX_gpu_memory_info");
    const bool ati = hasExtension("GL_ATI_meminfo");
    summary["driverExtensions"] = std::string(nvx ? "GL_NVX_gpu_memory_info " : "") + (ati ? "GL_ATI_meminfo" : "");
    if (!nvx && !ati) {
        report.record("a driver without a memory extension reports nothing measured", !d.driver.measured() && d.driver.source == gpudiag::MemorySource::None && d.driver.usedBytes() == 0, std::string("renderer ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)) + " has neither GL_NVX_gpu_memory_info nor GL_ATI_meminfo, so only the estimate exists");
        return;
    }
    const std::string renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const bool software = renderer.find("llvmpipe") != std::string::npos || renderer.find("softpipe") != std::string::npos || renderer.find("swrast") != std::string::npos || renderer.find("SwiftShader") != std::string::npos || renderer.find("Software") != std::string::npos;
    if (!d.driver.measured()) {
        report.record("an unusable driver readout is rejected", software, renderer + " offers the extension but the numbers were not usable and the readout stays off" + (software ? " (a software renderer, expected)" : " (on a real graphics card this is a failure)"));
        return;
    }
    const bool plausible = d.driver.measured() && d.driver.freeBytes > 0 && (!d.driver.hasTotal || (d.driver.totalBytes >= d.driver.freeBytes && d.driver.usedBytes() <= d.driver.totalBytes));
    report.record("the driver readout is plausible", plausible, std::string(gpudiag::memorySourceName(d.driver.source)) + ": free " + megabytes(d.driver.freeBytes) + ", total " + (d.driver.hasTotal ? megabytes(d.driver.totalBytes) : std::string("not reported")));
    summary["driverFreeMegabytes"] = static_cast<double>(d.driver.freeBytes) / static_cast<double>(gpudiag::kMiB);
}

void testUpload(Report& report, Engine& engine) {
    step(engine, 4, 0.75);
    const Diagnostics before = engine.diagnostics();
    const unsigned long long counterBefore = streamedUploadBytes();
    VideoGpu gpu;
    gpu.create();
    VideoUploader uploader(gpu);
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;
    constexpr int kFrames = 10;
    std::vector<std::uint8_t> data(static_cast<size_t>(kWidth) * kHeight * 4u, 128);
    bool all = true;
    for (int i = 0; i < kFrames; ++i) {
        VideoFrame frame;
        frame.layout = VideoLayout::Rgba;
        frame.width = kWidth;
        frame.height = kHeight;
        frame.outWidth = kWidth;
        frame.outHeight = kHeight;
        frame.planeCount = 1;
        frame.planes[0].data = data.data();
        frame.planes[0].width = kWidth;
        frame.planes[0].height = kHeight;
        frame.planes[0].stride = kWidth * 4;
        frame.planes[0].channels = 4;
        frame.planes[0].bytes = 1;
        all = uploader.present(frame) && all;
    }
    const unsigned long long counted = streamedUploadBytes() - counterBefore;
    constexpr unsigned long long expected = static_cast<unsigned long long>(kWidth) * kHeight * 4ull * kFrames;
    report.record("the upload counter counts the bytes handed to the driver", all && counted == expected, std::to_string(counted) + " bytes counted for " + std::to_string(kFrames) + " frames of " + std::to_string(kWidth) + " x " + std::to_string(kHeight) + " RGBA, expected " + std::to_string(expected));
    uploader.release();
    step(engine, 4, 0.75);
    const Diagnostics& after = engine.diagnostics();
    report.record("the engine reports the upload traffic", after.uploadTotalBytes - before.uploadTotalBytes == expected && after.uploadPeakBytesPerSecond > 0.0, "total grew by " + std::to_string(after.uploadTotalBytes - before.uploadTotalBytes) + " bytes, highest second " + fixed(after.uploadPeakBytesPerSecond / 1024.0) + " KB/s");
}

void testOff(Report& report, Engine& engine, const Probe& probe, json& summary) {
    setPolicy(engine, 0, true, false);
    placeRow(engine, probe);
    engine.trigger(0, 0);
    const int frames = stepUntil(engine, 900, 1.0 / 60.0, [&]() { return readyCount(engine, probe) == kProbeImages; });
    step(engine, 4, 0.25);
    engine.refreshDiagnostics();
    const Diagnostics& d = engine.diagnostics();
    summary["estimateWithEveryImageLoadedMegabytes"] = static_cast<double>(d.estimateBytes) / static_cast<double>(gpudiag::kMiB);
    report.record("without a budget every image of the grid is loaded as before", frames > 0 && !engine.memoryTrimming() && d.budgetBytes == 0 && d.releasedCount == 0 && d.pressureLevel == 0 && !d.trimming, std::to_string(readyCount(engine, probe)) + " of " + std::to_string(kProbeImages) + " images loaded after " + std::to_string(frames) + " frames, estimate " + megabytes(d.estimateBytes) + ", nothing released");
}

void testTrim(Report& report, Engine& engine, const Probe& probe, json& summary) {
    engine.refreshDiagnostics();
    const std::uint64_t start = engine.diagnostics().estimateBytes;
    if (start <= kBudget) {
        report.skip("images that are not in use are released over the budget", "the estimate of " + megabytes(start) + " is already under the smallest budget of " + megabytes(kBudget));
        return;
    }
    setPolicy(engine, kBudget, true, false);
    const std::uint64_t releasedBefore = engine.diagnostics().releasedCount;
    bool activeLost = false;
    const double seconds = stepForSeconds(engine, 25.0, 1.0 / 60.0, [&]() {
        activeLost = activeLost || !ready(engine, probe.clips[0]);
        return engine.diagnostics().estimateBytes <= kBudget && engine.diagnostics().releasedCount > releasedBefore;
    });
    const Diagnostics& d = engine.diagnostics();
    summary["trimSeconds"] = seconds;
    summary["releasedAfterTrim"] = d.releasedCount - releasedBefore;
    summary["estimateAfterTrimMegabytes"] = static_cast<double>(d.estimateBytes) / static_cast<double>(gpudiag::kMiB);
    report.record("images that are not in use are released over the budget", d.estimateBytes <= kBudget && d.releasedCount > releasedBefore && d.releasedBytes > 0, "estimate " + megabytes(start) + " before and " + megabytes(d.estimateBytes) + " after against " + megabytes(kBudget) + ", " + std::to_string(d.releasedCount - releasedBefore) + " images released in " + fixed(seconds) + " seconds, " + std::to_string(readyCount(engine, probe)) + " of " + std::to_string(kProbeImages) + " still loaded");
    report.record("the playing image is never released", !activeLost && ready(engine, probe.clips[0]) && litPixels(engine) > 1000, std::string("the image in the playing cell stayed loaded during the whole run, ") + std::to_string(litPixels(engine)) + " pixels lit");
    report.record("the next image stays prepared", ready(engine, probe.clips[1]), "the cell after the playing one was not released");
}

void testReload(Report& report, Engine& engine, const Probe& probe, json& summary) {
    int column = -1;
    for (int i = 1; i < kProbeImages; ++i) {
        if (!ready(engine, probe.clips[static_cast<size_t>(i)])) {
            column = i;
        }
    }
    if (column < 0) {
        report.skip("a released image loads again when it is launched", "no image was released");
        return;
    }
    const int clip = probe.clips[static_cast<size_t>(column)];
    engine.trigger(0, column);
    const auto start = Clock::now();
    const int frames = stepUntil(engine, 1200, 1.0 / 60.0, [&]() { return ready(engine, clip); });
    const double milliseconds = since(start);
    summary["reloadMilliseconds"] = milliseconds;
    summary["reloadFrames"] = frames;
    step(engine, 10, 1.0 / 60.0);
    report.record("a released image loads again when it is launched", frames > 0 && ready(engine, clip) && litPixels(engine) > 1000, "column " + std::to_string(column + 1) + " was ready after " + std::to_string(frames) + " frames (" + fixed(milliseconds, 0) + " ms on this machine), " + std::to_string(litPixels(engine)) + " pixels lit");
}

void testBudgetOff(Report& report, Engine& engine, const Probe& probe) {
    const std::uint64_t releasedBefore = engine.diagnostics().releasedCount;
    setPolicy(engine, 0, true, false);
    const int frames = stepUntil(engine, 900, 1.0 / 60.0, [&]() { return readyCount(engine, probe) == kProbeImages; });
    const Diagnostics& d = engine.diagnostics();
    report.record("turning the budget off restores the old behaviour", frames > 0 && d.releasedCount == releasedBefore && d.budgetBytes == 0 && d.pressureLevel == 0, std::to_string(readyCount(engine, probe)) + " of " + std::to_string(kProbeImages) + " images loaded again, nothing released after the switch");
}

void testPressure(Report& report, Engine& engine, const Probe& probe, json& summary) {
    setPolicy(engine, kBudget, true, true);
    for (int l = 1; l < kLayerCount; ++l) {
        engine.comp.layers[static_cast<size_t>(l)].cells[0] = probe.clips[static_cast<size_t>(l + 3)];
        engine.trigger(l, 0);
    }
    engine.trigger(0, 0);
    const auto fourReady = [&]() {
        int count = 0;
        for (int l = 0; l < kLayerCount; ++l) {
            const Layer& layer = engine.comp.layers[static_cast<size_t>(l)];
            count += layer.activeColumn >= 0 && ready(engine, layer.cells[static_cast<size_t>(layer.activeColumn)]) ? 1 : 0;
        }
        return count == kLayerCount;
    };
    stepUntil(engine, 900, 1.0 / 60.0, fourReady);
    step(engine, 8, 1.0 / 60.0);
    engine.refreshDiagnostics();
    const Diagnostics& start = engine.diagnostics();
    summary["residentWithFourLayersMegabytes"] = static_cast<double>(start.residentBytes) / static_cast<double>(gpudiag::kMiB);
    if (start.residentBytes <= kBudget) {
        report.skip("the pressure rises when nothing can be released", "what cannot be released (" + megabytes(start.residentBytes) + ") is under the smallest budget of " + megabytes(kBudget));
        return;
    }
    const int frames = stepUntil(engine, 200, 0.5, [&]() { return engine.diagnostics().pressureLevel >= gpudiag::kMaxPressureLevel; });
    const Diagnostics& d = engine.diagnostics();
    summary["pressureFrames"] = frames;
    report.record("the pressure rises step by step when nothing can be released", frames > 0 && d.pressureLevel == gpudiag::kMaxPressureLevel, "step " + std::to_string(d.pressureLevel) + " of " + std::to_string(gpudiag::kMaxPressureLevel) + " after " + std::to_string(frames) + " frames of half a second, resident " + megabytes(d.residentBytes) + " against " + megabytes(kBudget));
    step(engine, 4, 0.5);
    report.record("the layers are drawn at half resolution at the highest step", engine.layerScale() <= 0.51f && engine.layerScale() > 0.0f, "layer scale " + fixed(engine.layerScale() * 100.0f, 0) + " percent, " + std::to_string(engine.layerWidth()) + " x " + std::to_string(engine.layerHeight()));
    bool allReady = true;
    for (int l = 0; l < kLayerCount; ++l) {
        const Layer& layer = engine.comp.layers[static_cast<size_t>(l)];
        allReady = allReady && layer.activeColumn >= 0 && ready(engine, layer.cells[static_cast<size_t>(layer.activeColumn)]);
    }
    report.record("the playing images survive the pressure", allReady && litPixels(engine) > 1000, std::string("all four playing images are loaded, ") + std::to_string(litPixels(engine)) + " pixels lit");

    gpudiag::Monitor monitor;
    gpudiag::Sample sample;
    sample.estimateBytes = d.estimateBytes;
    sample.budgetBytes = d.budgetBytes;
    sample.residentBytes = d.residentBytes;
    sample.driver = d.driver;
    for (int i = 0; i < 12; ++i) {
        monitor.addFrame(16.7);
        monitor.update(0.5, sample);
    }
    report.record("the warning logic reports resident memory over the budget", monitor.tracker().has(gpudiag::WarningCode::ResidentOverBudget) && monitor.tracker().worst() == gpudiag::Severity::Critical, "resident memory " + megabytes(d.residentBytes) + " is above the budget of " + megabytes(kBudget));
}

void testRecovery(Report& report, Engine& engine, const Probe& probe, json& summary) {
    if (engine.diagnostics().pressureLevel < gpudiag::kMaxPressureLevel) {
        report.skip("the pressure steps back when there is room again", "the pressure never rose");
        return;
    }
    for (int l = 1; l < kLayerCount; ++l) {
        engine.clearLayer(l);
    }
    for (int c = 1; c < kColumnCount; ++c) {
        engine.comp.layers[0].cells[static_cast<size_t>(c)] = -1;
    }
    const std::uint64_t held = engine.diagnostics().estimateBytes;
    stepForSeconds(engine, 20.0, 1.0, [&]() { return engine.diagnostics().estimateBytes <= gpudiag::recoveryWater(kBudget); });
    engine.refreshDiagnostics();
    const std::uint64_t settled = engine.diagnostics().estimateBytes;
    summary["estimateBeforeRecoveryMegabytes"] = static_cast<double>(held) / static_cast<double>(gpudiag::kMiB);
    summary["estimateAfterRecoveryReleaseMegabytes"] = static_cast<double>(settled) / static_cast<double>(gpudiag::kMiB);
    report.record("idle images are also released below the budget while the pressure is raised", settled <= gpudiag::recoveryWater(kBudget), "estimate " + megabytes(held) + " before and " + megabytes(settled) + " after, the target is " + megabytes(gpudiag::recoveryWater(kBudget)));
    if (settled > gpudiag::recoveryWater(kBudget)) {
        return;
    }
    const int frames = stepUntil(engine, 300, 1.0, [&]() { return engine.diagnostics().pressureLevel == 0; });
    step(engine, 4, 0.5);
    summary["recoveryFrames"] = frames;
    report.record("the pressure steps back when there is room again", frames > 0 && engine.diagnostics().pressureLevel == 0 && engine.layerScale() >= 0.999f, "back at step 0 after " + std::to_string(frames) + " frames of one second, layer scale " + fixed(engine.layerScale() * 100.0f, 0) + " percent, estimate " + megabytes(engine.diagnostics().estimateBytes));
    (void)probe;
}

json benchmark(Engine& engine, const Probe& probe, int frames) {
    json result = json::object();
    for (int l = 1; l < kLayerCount; ++l) {
        engine.clearLayer(l);
    }
    placeRow(engine, probe);
    engine.trigger(0, 0);
    setPolicy(engine, 0, true, false);
    stepUntil(engine, 900, 1.0 / 60.0, [&]() { return readyCount(engine, probe) == kProbeImages; });
    step(engine, 30, 1.0 / 60.0);
    auto time = [&]() {
        const auto start = Clock::now();
        for (int i = 0; i < frames; ++i) {
            engine.update(1.0 / 60.0);
            engine.render();
            glFinish();
        }
        return since(start) / frames;
    };
    json off = json::object();
    off["averageFrameMs"] = time();
    off["imagesLoaded"] = readyCount(engine, probe);
    off["estimateMegabytes"] = static_cast<double>(engine.diagnostics().estimateBytes) / static_cast<double>(gpudiag::kMiB);
    result["budgetOff"] = off;
    setPolicy(engine, kBudget, true, false);
    stepForSeconds(engine, 25.0, 1.0 / 60.0, [&]() { return engine.diagnostics().estimateBytes <= kBudget; });
    step(engine, 30, 1.0 / 60.0);
    json on = json::object();
    on["averageFrameMs"] = time();
    on["imagesLoaded"] = readyCount(engine, probe);
    on["estimateMegabytes"] = static_cast<double>(engine.diagnostics().estimateBytes) / static_cast<double>(gpudiag::kMiB);
    result["budgetOn"] = on;
    result["frames"] = frames;
    result["note"] = "wall clock milliseconds per frame with glFinish after every frame, one playing image of " + std::to_string(kProbeSize) + " x " + std::to_string(kProbeSize) + " pixels, measured on whatever renderer ran the test, with a software renderer this only shows relative cost";
    return result;
}

}

int runGpuDiagSelfTest(Engine& engine, int benchFrames, const std::string& outputPath) {
    json report = json::object();
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["version"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    json summary = json::object();
    Report checks;
    engine.setGpuTiming(false);
    const Composition savedComposition = engine.comp;
    const MemoryPolicy savedPolicy = engine.memoryPolicy();
    engine.comp = Composition{};
    engine.setResolutionIndex(2);
    for (int l = 0; l < kLayerCount; ++l) {
        engine.clearLayer(l);
    }
    setPolicy(engine, 0, true, false);
    Probe probe;
    if (prepareProbe(engine, checks, probe)) {
        testHardwareSync(checks, engine, summary);
        testDriver(checks, engine, summary);
        testUpload(checks, engine);
        testOff(checks, engine, probe, summary);
        testTrim(checks, engine, probe, summary);
        testReload(checks, engine, probe, summary);
        testBudgetOff(checks, engine, probe);
        testPressure(checks, engine, probe, summary);
        testRecovery(checks, engine, probe, summary);
        if (benchFrames > 0) {
            report["benchmark"] = benchmark(engine, probe, benchFrames);
        }
        checks.record("the graphics state is left clean", stateIsClean() && glGetError() == GL_NO_ERROR, "no program, vertex array or pixel buffer left bound and no pending error");
    }
    setPolicy(engine, savedPolicy.budgetBytes, savedPolicy.trim, savedPolicy.lowerQuality);
    for (int l = 0; l < kLayerCount; ++l) {
        engine.clearLayer(l);
    }
    engine.applyComposition(savedComposition);
    removeProbeFiles();
    rescanLibrary();
    engine.syncLibrary();
    step(engine, 5, 1.0 / 60.0);
    report["summary"] = summary;
    report["checks"] = checks.items();
    report["failed"] = checks.failed();
    report["passed"] = checks.failed() == 0;
    const std::string text = report.dump(2);
    logLine("GPU diagnostics self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return checks.failed() == 0 ? 0 : 1;
}

}

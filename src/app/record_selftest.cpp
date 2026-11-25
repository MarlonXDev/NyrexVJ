#include "app/record_selftest.hpp"

#include "core/composition.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "core/record_plan.hpp"
#include "media/video_recorder.hpp"
#include "render/gl_util.hpp"
#include "render/recorder.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr int kSourceWidth = 256;
constexpr int kSourceHeight = 144;

struct TapFrame {
    int64_t index = 0;
    int width = 0;
    int height = 0;
    std::array<int, 16> samples{};
};

struct Tap {
    std::mutex mutex;
    std::vector<TapFrame> frames;
};

void tapFrame(void* user, const uint8_t* rgba, int width, int height, int64_t index) {
    Tap* tap = static_cast<Tap*>(user);
    TapFrame frame;
    frame.index = index;
    frame.width = width;
    frame.height = height;
    const int xs[2] = {width / 4, 3 * width / 4};
    const int ys[2] = {height / 4, 3 * height / 4};
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const size_t offset = (static_cast<size_t>(ys[quadrant / 2]) * static_cast<size_t>(width) + static_cast<size_t>(xs[quadrant % 2])) * 4u;
        for (int channel = 0; channel < 4; ++channel) {
            frame.samples[static_cast<size_t>(quadrant * 4 + channel)] = rgba[offset + static_cast<size_t>(channel)];
        }
    }
    std::lock_guard<std::mutex> lock(tap->mutex);
    tap->frames.push_back(frame);
}

bool videoRequired() {
    const char* value = std::getenv("NYREXVJ_REQUIRE_VIDEO");
    return value != nullptr && *value != '\0';
}

struct Report {
    json items = json::array();
    int failed = 0;
    int passed = 0;

    void check(bool ok, const std::string& name, const std::string& detail = std::string()) {
        json item;
        item["name"] = name;
        item["passed"] = ok;
        if (!detail.empty()) {
            item["detail"] = detail;
        }
        items.push_back(std::move(item));
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        logLine(std::string("Record self test ") + (ok ? "ok: " : "FAILED: ") + name + (detail.empty() ? std::string() : " (" + detail + ")"));
    }
};

void fillRect(int x, int y, int w, int h, int r, int g, int b) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glClearColor(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

int markerFor(int frame) {
    return 10 + frame * 9;
}

void paintPattern(const RenderTarget& target, int frame) {
    target.bind();
    const int halfWidth = target.width / 2;
    const int halfHeight = target.height / 2;
    fillRect(0, halfHeight, halfWidth, target.height - halfHeight, 200, 40, markerFor(frame));
    fillRect(halfWidth, halfHeight, target.width - halfWidth, target.height - halfHeight, 0, 200, 0);
    fillRect(0, 0, halfWidth, halfHeight, 0, 0, 200);
    fillRect(halfWidth, 0, target.width - halfWidth, halfHeight, 240, 240, 240);
}

bool near(int value, int expected, int tolerance) {
    return std::abs(value - expected) <= tolerance;
}

bool quadrantsMatch(const TapFrame& frame, int frameNumber, int tolerance) {
    const int expected[16] = {200, 40, markerFor(frameNumber), 255, 0, 200, 0, 255, 0, 0, 200, 255, 240, 240, 240, 255};
    for (int i = 0; i < 16; ++i) {
        if (!near(frame.samples[static_cast<size_t>(i)], expected[i], tolerance)) {
            return false;
        }
    }
    return true;
}

struct ClipSpec {
    RecordProfile profile = RecordProfile::Ffv1;
    int width = 0;
    int height = 0;
    int fps = 30;
    std::vector<double> times;
    std::vector<const RenderTarget*> sources;
    RecorderTestHooks hooks;
    bool cancel = false;
    int sleepMs = 4;
};

struct Clip {
    bool started = false;
    std::string error;
    RecordStatus status;
    std::vector<TapFrame> taps;
    double loopSeconds = 0.0;
    double maxCaptureMs = 0.0;
    std::string path;
    bool fileExists = false;
    uint64_t fileBytes = 0;
    RecordEvent event = RecordEvent::None;
};

Clip runClip(const ClipSpec& spec, const fs::path& directory, const std::string& name) {
    Clip clip;
    Tap tap;
    RecorderTestHooks hooks = spec.hooks;
    hooks.frameTap = &tapFrame;
    hooks.tapUser = &tap;
    Recorder recorder;
    recorder.setTestHooks(hooks);

    RecordSettings settings;
    settings.profile = spec.profile;
    settings.width = spec.width;
    settings.height = spec.height;
    settings.fps = spec.fps;
    settings.quality = 100;
    settings.audio = false;
    settings.crashSafe = false;
    clip.path = (directory / (name + "." + recordProfileInfo(spec.profile).extension)).string();
    std::string detail;
    const RenderTarget* first = spec.sources.empty() ? nullptr : spec.sources.front();
    if (first == nullptr || !recorder.start(settings, first->width, first->height, 0, clip.path, clip.error, detail)) {
        clip.error += detail.empty() ? std::string() : ": " + detail;
        return clip;
    }
    clip.started = true;
    const auto begin = Clock::now();
    for (size_t i = 0; i < spec.times.size(); ++i) {
        const RenderTarget* source = spec.sources[std::min(i, spec.sources.size() - 1)];
        paintPattern(*source, static_cast<int>(i));
        const auto before = Clock::now();
        recorder.capture(source->texture, source->width, source->height, spec.times[i]);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
        clip.maxCaptureMs = std::max(clip.maxCaptureMs, ms);
        recorder.service();
        if (spec.sleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(spec.sleepMs));
        }
    }
    clip.loopSeconds = std::chrono::duration<double>(Clock::now() - begin).count();
    if (spec.cancel) {
        recorder.cancel();
    } else {
        recorder.stop(spec.times.empty() ? 0.0 : spec.times.back() + 1.0 / static_cast<double>(spec.fps));
    }
    const auto deadline = Clock::now() + std::chrono::seconds(20);
    while (Clock::now() < deadline) {
        const RecordEvent event = recorder.service();
        if (event != RecordEvent::None) {
            clip.event = event;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    clip.status = recorder.status();
    recorder.shutdown(5.0);
    std::error_code ec;
    clip.fileExists = fs::exists(clip.path, ec);
    clip.fileBytes = clip.fileExists ? static_cast<uint64_t>(fs::file_size(clip.path, ec)) : 0;
    {
        std::lock_guard<std::mutex> lock(tap.mutex);
        clip.taps = tap.frames;
    }
    return clip;
}

std::vector<double> steadyTimes(int count, int fps, double start = 0.5) {
    std::vector<double> times;
    for (int i = 0; i < count; ++i) {
        times.push_back(start + static_cast<double>(i) / static_cast<double>(fps));
    }
    return times;
}

bool pickProfile(const RecordCapabilities& caps, RecordProfile& profile) {
    const RecordProfile order[] = {RecordProfile::Ffv1, RecordProfile::MotionJpeg, RecordProfile::ProResHq, RecordProfile::H264Software, RecordProfile::Hap};
    for (RecordProfile candidate : order) {
        if (caps.available(candidate)) {
            profile = candidate;
            return true;
        }
    }
    return false;
}

struct GlState {
    GLint read = 0;
    GLint draw = 0;
    GLint pack = 0;
    GLint alignment = 0;
    GLboolean scissor = GL_FALSE;
    GLint program = 0;
    GLint texture = 0;
};

GlState captureState() {
    GlState state;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.draw);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &state.pack);
    glGetIntegerv(GL_PACK_ALIGNMENT, &state.alignment);
    state.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture);
    return state;
}

bool sameState(const GlState& a, const GlState& b) {
    return a.read == b.read && a.draw == b.draw && a.pack == b.pack && a.alignment == b.alignment && a.scissor == b.scissor && a.program == b.program && a.texture == b.texture;
}

void testOrientationAndOrder(Report& report, RecordProfile profile, const fs::path& directory, const RenderTarget& source) {
    ClipSpec spec;
    spec.profile = profile;
    spec.times = steadyTimes(24, 30);
    spec.sources = {&source};
    const Clip clip = runClip(spec, directory, "orientation");
    report.check(clip.started, "orientation: recording starts", clip.error);
    if (!clip.started) {
        return;
    }
    report.check(clip.event == RecordEvent::Finished && clip.fileExists && clip.fileBytes > 0, "orientation: file is written", "bytes " + std::to_string(clip.fileBytes));
    report.check(clip.taps.size() == 24 && clip.status.dropped() == 0, "orientation: all 24 frames reach the encoder", "frames " + std::to_string(clip.taps.size()) + ", dropped " + std::to_string(clip.status.dropped()));
    bool ordered = true;
    bool exact = true;
    for (size_t i = 0; i < clip.taps.size(); ++i) {
        ordered = ordered && clip.taps[i].index == static_cast<int64_t>(i);
        exact = exact && clip.taps[i].width == kSourceWidth && clip.taps[i].height == kSourceHeight && quadrantsMatch(clip.taps[i], static_cast<int>(i), 0);
    }
    report.check(ordered, "orientation: frame indexes follow each other without gaps");
    report.check(exact, "orientation: top is top, left is left, every frame has its own marker, colors are exact");
    report.check(glGetError() == GL_NO_ERROR, "orientation: no GL error");
}

void testScaling(Report& report, RecordProfile profile, const fs::path& directory, const RenderTarget& source) {
    const int sizes[2][2] = {{128, 72}, {512, 288}};
    for (const auto& size : sizes) {
        ClipSpec spec;
        spec.profile = profile;
        spec.width = size[0];
        spec.height = size[1];
        spec.times = steadyTimes(6, 30);
        spec.sources = {&source};
        const Clip clip = runClip(spec, directory, "scale" + std::to_string(size[0]));
        const std::string label = "scaling to " + std::to_string(size[0]) + " x " + std::to_string(size[1]);
        report.check(clip.started && clip.taps.size() == 6, label + ": all frames arrive", clip.error);
        bool good = !clip.taps.empty();
        for (size_t i = 0; i < clip.taps.size(); ++i) {
            good = good && clip.taps[i].width == size[0] && clip.taps[i].height == size[1] && quadrantsMatch(clip.taps[i], static_cast<int>(i), 3);
        }
        report.check(good, label + ": size and quadrant colors are right");
    }
    report.check(glGetError() == GL_NO_ERROR, "scaling: no GL error");
}

void testGlState(Report& report, RecordProfile profile, const fs::path& directory, const RenderTarget& source, const RenderTarget& other) {
    Tap tap;
    RecorderTestHooks hooks;
    hooks.frameTap = &tapFrame;
    hooks.tapUser = &tap;
    Recorder recorder;
    recorder.setTestHooks(hooks);
    RecordSettings settings;
    settings.profile = profile;
    settings.fps = 30;
    settings.audio = false;
    settings.crashSafe = false;
    std::string error;
    std::string detail;
    const std::string path = (directory / ("state." + std::string(recordProfileInfo(profile).extension))).string();
    if (!recorder.start(settings, source.width, source.height, 0, path, error, detail)) {
        report.check(false, "GL state: recording starts", error);
        return;
    }
    GLuint dummyPack = 0;
    glGenBuffers(1, &dummyPack);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, dummyPack);
    glBufferData(GL_PIXEL_PACK_BUFFER, 64, nullptr, GL_STREAM_READ);
    bool unchanged = true;
    for (int i = 0; i < 6; ++i) {
        paintPattern(source, i);
        other.bind();
        glEnable(GL_SCISSOR_TEST);
        glScissor(1, 1, 3, 3);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, dummyPack);
        glPixelStorei(GL_PACK_ALIGNMENT, 2);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, other.texture);
        const GlState prepared = captureState();
        recorder.capture(source.texture, source.width, source.height, 0.5 + static_cast<double>(i) / 30.0);
        const GlState after = captureState();
        unchanged = unchanged && sameState(prepared, after);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    glDisable(GL_SCISSOR_TEST);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glDeleteBuffers(1, &dummyPack);
    report.check(unchanged, "GL state: framebuffer bindings, pack buffer, pack alignment, scissor, program and texture are restored after every capture");
    recorder.cancel();
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (Clock::now() < deadline && recorder.service() == RecordEvent::None) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    recorder.shutdown(5.0);
    report.check(glGetError() == GL_NO_ERROR, "GL state: no GL error");
}

void testBackpressure(Report& report, RecordProfile profile, const fs::path& directory, const RenderTarget& source) {
    ClipSpec spec;
    spec.profile = profile;
    spec.fps = 60;
    spec.times = steadyTimes(90, 60);
    spec.sources = {&source};
    spec.hooks.encodeDelayMs = 250;
    spec.cancel = true;
    spec.sleepMs = 0;
    const Clip clip = runClip(spec, directory, "backpressure");
    report.check(clip.started, "backpressure: recording starts", clip.error);
    if (!clip.started) {
        return;
    }
    report.check(clip.loopSeconds < 3.0, "backpressure: 90 frames against an encoder that needs 250 ms each never block the render loop", "loop " + std::to_string(clip.loopSeconds) + " s, slowest capture " + std::to_string(clip.maxCaptureMs) + " ms");
    report.check(clip.status.dropped() > 0, "backpressure: frames that do not fit are dropped and counted", "dropped " + std::to_string(clip.status.dropped()));
    report.check(clip.status.queueMax <= std::max(clip.status.pool, 1), "backpressure: the queue never grows beyond the frame pool", "queue " + std::to_string(clip.status.queueMax) + ", pool " + std::to_string(clip.status.pool));
    report.check(clip.event == RecordEvent::Cancelled && !clip.fileExists, "cancel: the file is deleted");
    report.check(glGetError() == GL_NO_ERROR, "backpressure: no GL error");
}

void testSizeChange(Report& report, RecordProfile profile, const fs::path& directory, const RenderTarget& large, const RenderTarget& small) {
    ClipSpec spec;
    spec.profile = profile;
    spec.times = steadyTimes(16, 30);
    for (int i = 0; i < 16; ++i) {
        spec.sources.push_back(i < 8 ? &large : &small);
    }
    const Clip clip = runClip(spec, directory, "sizechange");
    report.check(clip.started && clip.taps.size() == 16, "size change: all 16 frames arrive", "frames " + std::to_string(clip.taps.size()) + " " + clip.error);
    bool same = !clip.taps.empty();
    bool colors = !clip.taps.empty();
    for (size_t i = 0; i < clip.taps.size(); ++i) {
        same = same && clip.taps[i].width == kSourceWidth && clip.taps[i].height == kSourceHeight;
        colors = colors && quadrantsMatch(clip.taps[i], static_cast<int>(i), 3);
    }
    report.check(same, "size change: the file keeps its picture size when the output size changes");
    report.check(colors, "size change: the picture before and after the change is complete and upright");
    report.check(clip.status.sizeChanges == 1, "size change: the change is counted", "changes " + std::to_string(clip.status.sizeChanges));
    report.check(glGetError() == GL_NO_ERROR, "size change: no GL error");
}

void testStall(Report& report, RecordProfile profile, const fs::path& directory, const RenderTarget& source) {
    ClipSpec spec;
    spec.profile = profile;
    spec.times = steadyTimes(6, 30);
    const double resume = spec.times.back() + 4.0 / 30.0;
    for (int i = 0; i < 6; ++i) {
        spec.times.push_back(resume + static_cast<double>(i) / 30.0);
    }
    spec.sources = {&source};
    const Clip clip = runClip(spec, directory, "stall");
    report.check(clip.started && clip.taps.size() == 12, "stall: every frame that was rendered is recorded", "frames " + std::to_string(clip.taps.size()) + " " + clip.error);
    bool gap = clip.taps.size() == 12 && clip.taps[6].index - clip.taps[5].index == 4;
    report.check(gap, "stall: a stall of the show stays a gap in the timeline instead of being squeezed away");
    report.check(clip.status.missedSlots == 3, "stall: the skipped slots are counted", "missed " + std::to_string(clip.status.missedSlots));
    report.check(glGetError() == GL_NO_ERROR, "stall: no GL error");
}

struct Spread {
    double avg = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

Spread spread(std::vector<double> values) {
    Spread result;
    if (values.empty()) {
        return result;
    }
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    const auto at = [&values](double fraction) {
        const size_t index = std::min(values.size() - 1, static_cast<size_t>(fraction * static_cast<double>(values.size())));
        return values[index];
    };
    result.avg = sum / static_cast<double>(values.size());
    result.p50 = at(0.50);
    result.p95 = at(0.95);
    result.p99 = at(0.99);
    result.max = values.back();
    return result;
}

json spreadJson(const Spread& value) {
    json item;
    item["avg"] = value.avg;
    item["p50"] = value.p50;
    item["p95"] = value.p95;
    item["p99"] = value.p99;
    item["max"] = value.max;
    return item;
}

struct PacedLoop {
    Clock::time_point next = Clock::now();
    double period = 1.0 / 60.0;

    void wait() {
        next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(period));
        const auto now = Clock::now();
        if (next > now) {
            std::this_thread::sleep_until(next);
        } else if (now - next > std::chrono::milliseconds(100)) {
            next = now;
        }
    }
};

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            if (!current.empty()) {
                parts.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

}

int runRecordSelfTest(Engine& engine, const std::string& outputPath) {
    (void)engine;
    Report report;
    json out;
    out["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const RecordCapabilities caps = probeRecordCapabilities();
    out["ffmpegLoaded"] = caps.ffmpegLoaded;
    json profiles = json::object();
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        json entry;
        entry["works"] = caps.available(profile);
        entry["encoder"] = caps.profiles[static_cast<size_t>(i)].encoder;
        entry["detail"] = caps.profiles[static_cast<size_t>(i)].detail;
        profiles[recordProfileInfo(profile).id] = entry;
        logLine(std::string("Record self test: ") + recordProfileInfo(profile).id + (caps.available(profile) ? " works with " + caps.profiles[static_cast<size_t>(i)].encoder : " is not available: " + caps.profiles[static_cast<size_t>(i)].detail));
    }
    out["profiles"] = profiles;

    RecordProfile profile = RecordProfile::Ffv1;
    if (!caps.ffmpegLoaded || !pickProfile(caps, profile)) {
        const bool required = videoRequired();
        report.check(!required, "FFmpeg with a working encoder is present", required ? "missing but required" : "missing, the test is skipped");
        out["skipped"] = !required;
        out["checks"] = report.items;
        out["passed"] = report.failed == 0;
        if (!outputPath.empty()) {
            std::ofstream file(outputPath);
            file << out.dump(2) << '\n';
        }
        return report.failed == 0 ? 0 : 2;
    }
    out["profileUnderTest"] = recordProfileInfo(profile).id;

    const fs::path directory = fs::temp_directory_path() / "nyrexvj_record_selftest";
    std::error_code ec;
    fs::remove_all(directory, ec);
    fs::create_directories(directory, ec);

    while (glGetError() != GL_NO_ERROR) {
    }
    RenderTarget source;
    RenderTarget other;
    RenderTarget small;
    const bool created = source.create(kSourceWidth, kSourceHeight) && other.create(64, 64) && small.create(192, 108);
    report.check(created, "test targets are created");
    if (created) {
        testOrientationAndOrder(report, profile, directory, source);
        testScaling(report, profile, directory, source);
        testGlState(report, profile, directory, source, other);
        testBackpressure(report, profile, directory, source);
        testSizeChange(report, profile, directory, source, small);
        testStall(report, profile, directory, source);
    }
    source.destroy();
    other.destroy();
    small.destroy();
    fs::remove_all(directory, ec);

    out["checks"] = report.items;
    out["passed"] = report.failed == 0;
    out["failed"] = report.failed;
    logLine("Record self test: " + std::to_string(report.passed) + " checks passed, " + std::to_string(report.failed) + " failed");
    if (!outputPath.empty()) {
        std::ofstream file(outputPath);
        file << out.dump(2) << '\n';
    }
    return report.failed == 0 ? 0 : 2;
}

}

namespace nvj {

int runRecordBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath) {
    std::string listText = spec;
    int frames = 300;
    const size_t colon = spec.find(':');
    if (colon != std::string::npos) {
        listText = spec.substr(0, colon);
        frames = std::clamp(std::atoi(spec.c_str() + colon + 1), 60, 20000);
    }
    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["frames"] = frames;
    const RecordCapabilities caps = probeRecordCapabilities();
    report["ffmpegLoaded"] = caps.ffmpegLoaded;
    if (!caps.ffmpegLoaded) {
        report["problem"] = caps.problem;
        logLine("Record benchmark: FFmpeg is not available: " + caps.problem);
        if (!outputPath.empty()) {
            std::ofstream file(outputPath);
            file << report.dump(2) << '\n';
        }
        return videoRequired() ? 2 : 0;
    }

    std::vector<RecordProfile> wanted;
    if (listText.empty() || listText == "all") {
        for (int i = 0; i < kRecordProfileCount; ++i) {
            wanted.push_back(static_cast<RecordProfile>(i));
        }
    } else {
        for (const std::string& id : splitList(listText)) {
            const RecordProfileInfo* info = findRecordProfile(id);
            if (info != nullptr) {
                wanted.push_back(info->profile);
            } else {
                logLine("Record benchmark: unknown profile " + id);
            }
        }
    }

    engine.setGpuTiming(false);
    const std::vector<Composition>& looks = factoryPresets();
    if (!looks.empty()) {
        Composition look = looks.front();
        look.resolution = 2;
        engine.applyComposition(look);
        report["composition"] = look.name;
    }
    engine.setResolutionIndex(2);
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());

    const fs::path directory = fs::temp_directory_path() / "nyrexvj_record_bench";
    std::error_code ec;
    fs::remove_all(directory, ec);
    fs::create_directories(directory, ec);

    const auto frameStep = [&engine]() {
        engine.update(1.0 / 60.0);
        engine.render();
    };
    for (int i = 0; i < 20; ++i) {
        frameStep();
    }
    glFinish();

    std::vector<double> baseline;
    {
        PacedLoop pace;
        for (int i = 0; i < 180; ++i) {
            const auto begin = Clock::now();
            frameStep();
            glFinish();
            baseline.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
            pace.wait();
        }
    }
    const Spread baseSpread = spread(baseline);
    report["baselineFrameMs"] = spreadJson(baseSpread);
    logLine("Record benchmark baseline: " + std::to_string(baseSpread.avg) + " ms per frame without recording");

    int failures = 0;
    json results = json::array();
    for (RecordProfile profile : wanted) {
        const RecordProfileInfo& info = recordProfileInfo(profile);
        json item;
        item["profile"] = info.id;
        if (!caps.available(profile)) {
            item["skipped"] = true;
            item["reason"] = caps.profiles[static_cast<size_t>(profile)].detail;
            results.push_back(std::move(item));
            continue;
        }
        item["encoder"] = caps.profiles[static_cast<size_t>(profile)].encoder;
        Recorder recorder;
        RecordSettings settings;
        settings.profile = profile;
        settings.fps = 60;
        settings.quality = 70;
        settings.audio = false;
        settings.crashSafe = true;
        const std::string path = (directory / (std::string("bench_") + info.id + "." + info.extension)).string();
        std::string error;
        std::string detail;
        if (!recorder.start(settings, engine.width(), engine.height(), 0, path, error, detail)) {
            item["error"] = error + (detail.empty() ? std::string() : ": " + detail);
            ++failures;
            results.push_back(std::move(item));
            continue;
        }
        std::vector<double> frameMs;
        std::vector<double> captureMs;
        PacedLoop pace;
        const auto began = Clock::now();
        for (int i = 0; i < frames; ++i) {
            const auto begin = Clock::now();
            frameStep();
            const auto before = Clock::now();
            recorder.capture(engine.outputTexture(), engine.width(), engine.height(), steadyNowSeconds());
            captureMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - before).count());
            recorder.service();
            glFinish();
            frameMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
            pace.wait();
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - began).count();
        recorder.stop(steadyNowSeconds());
        const auto deadline = Clock::now() + std::chrono::seconds(60);
        RecordEvent event = RecordEvent::None;
        while (Clock::now() < deadline && event == RecordEvent::None) {
            event = recorder.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const RecordStatus status = recorder.status();
        recorder.shutdown(10.0);
        const Spread frameSpread = spread(frameMs);
        const Spread captureSpread = spread(captureMs);
        item["frameMs"] = spreadJson(frameSpread);
        item["captureCallMs"] = spreadJson(captureSpread);
        item["overheadAvgMs"] = frameSpread.avg - baseSpread.avg;
        item["overheadP99Ms"] = frameSpread.p99 - baseSpread.p99;
        item["wallSeconds"] = seconds;
        item["framesRecorded"] = status.frames;
        item["framesDropped"] = status.dropped();
        item["droppedPool"] = status.droppedPool;
        item["droppedReadback"] = status.droppedReadback;
        item["missedSlots"] = status.missedSlots;
        item["encodeMsAvg"] = status.encodeMs;
        item["encodeMsMax"] = status.encodeMsMax;
        item["queueMax"] = status.queueMax;
        item["pool"] = status.pool;
        item["fileBytes"] = status.bytes;
        item["megabitPerSecond"] = status.seconds > 0.0 ? static_cast<double>(status.bytes) * 8.0 / status.seconds / 1.0e6 : 0.0;
        item["finished"] = event == RecordEvent::Finished;
        if (event != RecordEvent::Finished) {
            item["error"] = status.error;
            ++failures;
        }
        logLine(std::string("Record benchmark ") + info.id + ": frame " + std::to_string(frameSpread.avg) + " ms (baseline " + std::to_string(baseSpread.avg) + "), capture call " + std::to_string(captureSpread.avg) + " ms avg " + std::to_string(captureSpread.max) + " ms max, dropped " + std::to_string(status.dropped()) + ", encode " + std::to_string(status.encodeMs) + " ms, queue max " + std::to_string(status.queueMax));
        results.push_back(std::move(item));
        fs::remove(path, ec);
    }
    report["results"] = results;
    report["passed"] = failures == 0;
    fs::remove_all(directory, ec);
    const std::string text = report.dump(2);
    logLine("Record benchmark result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream file(outputPath);
        file << text << '\n';
    }
    return failures == 0 ? 0 : 2;
}

}

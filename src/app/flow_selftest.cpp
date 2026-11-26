#include "app/flow_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "core/optical_flow.hpp"
#include "media/video_recorder.hpp"
#include "media/video_types.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/engine.hpp"
#include "render/media_player.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

constexpr int kSampleStride = 4;

void setDataDirectory(const std::string& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void clearDataDirectory() {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", "");
#else
    unsetenv("NYREXVJ_DATA_DIR");
#endif
}

std::string number(double value, int digits = 2) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

struct Shot {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::uint64_t hash = 0;
    std::vector<unsigned char> sampled;
    int sampledWidth = 0;
    int sampledHeight = 0;
    double position = 0.0;
    int index = 0;
    bool engaged = false;
    std::string state;
    std::string reason;
};

std::uint64_t hashPixels(const std::vector<unsigned char>& pixels) {
    std::uint64_t value = 1469598103934665603ull;
    for (const unsigned char byte : pixels) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    return value;
}

Shot makeShot(const std::vector<unsigned char>& pixels, int width, int height) {
    Shot shot;
    shot.width = width;
    shot.height = height;
    shot.hash = hashPixels(pixels);
    shot.sampledWidth = width / kSampleStride;
    shot.sampledHeight = height / kSampleStride;
    shot.sampled.resize(static_cast<std::size_t>(shot.sampledWidth) * static_cast<std::size_t>(shot.sampledHeight) * 4u);
    for (int y = 0; y < shot.sampledHeight; ++y) {
        for (int x = 0; x < shot.sampledWidth; ++x) {
            const std::size_t from = (static_cast<std::size_t>(y * kSampleStride) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x * kSampleStride)) * 4u;
            const std::size_t to = (static_cast<std::size_t>(y) * static_cast<std::size_t>(shot.sampledWidth) + static_cast<std::size_t>(x)) * 4u;
            for (int c = 0; c < 4; ++c) {
                shot.sampled[to + static_cast<std::size_t>(c)] = pixels[from + static_cast<std::size_t>(c)];
            }
        }
    }
    shot.ok = true;
    return shot;
}

double psnr(const Shot& a, const Shot& b) {
    if (!a.ok || !b.ok || a.sampled.size() != b.sampled.size() || a.sampled.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.sampled.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            const double d = (static_cast<double>(a.sampled[i + c]) - static_cast<double>(b.sampled[i + c])) / 255.0;
            sum += d * d;
            ++count;
        }
    }
    const double mse = sum / static_cast<double>(count);
    return mse < 1.0e-12 ? 99.0 : 10.0 * std::log10(1.0 / mse);
}

struct Run {
    std::vector<Shot> shots;
    FlowStatus status;
    std::string error;
    double stepMs = 0.0;
    double fps = 30.0;
    bool ok = false;
};

class Probe {
public:
    explicit Probe(Engine& engine) : engine_(engine) {}

    Engine& engine() { return engine_; }
    json& summary() { return summary_; }
    json& items() { return items_; }
    int failed() const { return failed_; }
    long long frames() const { return frames_; }
    const fs::path& root() const { return root_; }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Flow self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void prepare() {
        root_ = fs::temp_directory_path() / "nyrexvj_flow_selftest";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        setDataDirectory(root_.string());
        engine_.comp = Composition{};
        engine_.blackout = false;
        engine_.paused = false;
        engine_.setGpuTiming(false);
        engine_.setResolutionIndex(0);
        engine_.setFlowSettings(flow::Settings{});
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    void finish() {
        engine_.setFlowSettings(flow::Settings{});
        engine_.paused = false;
        reset();
        step(4);
        clearDataDirectory();
        std::error_code ec;
        fs::remove_all(root_, ec);
        rescanLibrary();
        engine_.syncLibrary();
    }

    void step(int count) {
        for (int i = 0; i < count; ++i) {
            engine_.setAudio(virtualAudio(engine_.beat(), engine_.time()));
            engine_.update(1.0 / 60.0);
            engine_.render();
            glFinish();
            ++frames_;
        }
    }

    void reset() {
        engine_.comp = Composition{};
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    int importClip(const std::string& directory, const std::string& file) {
        const fs::path path = fs::path(directory) / file;
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            return -1;
        }
        importAsset(path.string());
        rescanLibrary();
        engine_.syncLibrary();
        return clipIndexByName(videoClipName(path.stem().string()));
    }

    bool capture(Shot& shot) {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        if (!engine_.readOutput(pixels, width, height) || width <= 0 || height <= 0) {
            return false;
        }
        shot = makeShot(pixels, width, height);
        return true;
    }

    Run play(int clip, const flow::Settings& settings, int steps, float speed, int jumpEvery, int waitMs) {
        Run run;
        Engine& e = engine_;
        e.setFlowSettings(settings);
        reset();
        e.comp.layers[0].setCell(0, clip);
        e.comp.layers[0].playback[0].speed = speed;
        e.trigger(0, 0);
        MediaPlayer* player = e.mediaPlayer(clip);
        const auto start = Clock::now();
        while (player != nullptr && !player->ready()) {
            step(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            if (player->state() == MediaState::Failed) {
                run.error = player->error();
                return run;
            }
            if (Clock::now() - start > std::chrono::seconds(30)) {
                run.error = "the video did not become ready";
                return run;
            }
        }
        if (player == nullptr) {
            run.error = "no player";
            return run;
        }
        const auto begin = Clock::now();
        for (int i = 0; i < steps; ++i) {
            const int count = jumpEvery > 0 && i % jumpEvery == jumpEvery - 1 ? 3 : 1;
            step(count);
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            Shot shot;
            if (!capture(shot)) {
                run.error = "the output could not be read";
                return run;
            }
            shot.position = player->position();
            shot.index = static_cast<int>(std::lround(shot.position * 60.0));
            shot.engaged = player->flowEngaged();
            const FlowStatus status = player->flowStatus();
            shot.state = status.state;
            shot.reason = status.reason;
            run.shots.push_back(std::move(shot));
        }
        run.stepMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count() / std::max(steps, 1);
        run.status = player->flowStatus();
        run.fps = player->videoInfo().fps > 1.0 ? player->videoInfo().fps : 30.0;
        run.ok = true;
        return run;
    }

private:
    Engine& engine_;
    json summary_ = json::object();
    json items_ = json::array();
    int failed_ = 0;
    long long frames_ = 0;
    fs::path root_;
};

const Shot* byIndex(const Run& run, int index) {
    for (const Shot& shot : run.shots) {
        if (shot.index == index) {
            return &shot;
        }
    }
    return nullptr;
}

const Shot* byPosition(const Run& run, double position) {
    const Shot* best = nullptr;
    double distance = 1.0e-4;
    for (const Shot& shot : run.shots) {
        const double gap = std::abs(shot.position - position);
        if (gap < distance) {
            distance = gap;
            best = &shot;
        }
    }
    return best;
}

double phaseOf(const Shot& shot, double fps) {
    const double cell = shot.position * fps;
    return cell - std::floor(cell + 1.0e-6);
}

flow::Settings makeSettings(flow::Mode mode, flow::Tier tier = flow::Tier::Balanced) {
    flow::Settings settings;
    settings.mode = mode;
    settings.tier = tier;
    return settings;
}

void testOffState(Probe& p, int clip) {
    const Run run = p.play(clip, flow::Settings{}, 20, 1.0f, 0, 16);
    if (!run.ok) {
        p.record("the video plays with the feature off", false, run.error);
        return;
    }
    bool never = true;
    for (const Shot& shot : run.shots) {
        never = never && !shot.engaged;
    }
    p.record("with the feature off no picture goes through the flow path", never && run.status.state == "off" && run.status.pairsSubmitted == 0 && run.status.gpuBytes == 0, "state '" + run.status.state + "', pairs submitted " + std::to_string(run.status.pairsSubmitted) + ", flow memory " + std::to_string(run.status.gpuBytes) + " bytes");
}

void testInterpolation(Probe& p, int clip30, int clip60) {
    const int steps = 44;
    const Run off = p.play(clip30, flow::Settings{}, steps, 1.0f, 0, 16);
    const Run truth = p.play(clip60, flow::Settings{}, steps, 1.0f, 0, 16);
    const Run flowRun = p.play(clip30, makeSettings(flow::Mode::Interpolate), steps, 1.0f, 0, 16);
    if (!off.ok || !truth.ok || !flowRun.ok) {
        p.record("the 30 to 60 frame test could be played", false, off.error + truth.error + flowRun.error);
        return;
    }
    double flowSum = 0.0;
    double holdSum = 0.0;
    int between = 0;
    int exact = 0;
    int exactSame = 0;
    int exactChecked = 0;
    for (const Shot& shot : flowRun.shots) {
        if (shot.index < 6) {
            continue;
        }
        const Shot* reference = byIndex(truth, shot.index);
        const Shot* hold = byIndex(off, shot.index);
        if (reference == nullptr || hold == nullptr) {
            continue;
        }
        const double phase = phaseOf(shot, 30.0);
        if (phase > 0.35 && phase < 0.65) {
            flowSum += psnr(shot, *reference);
            holdSum += psnr(*hold, *reference);
            ++between;
        } else if (phase < 0.02 || phase > 0.98) {
            ++exact;
            ++exactChecked;
            if (shot.hash == hold->hash) {
                ++exactSame;
            }
        }
    }
    const double flowMean = between > 0 ? flowSum / between : 0.0;
    const double holdMean = between > 0 ? holdSum / between : 0.0;
    json result;
    result["inBetweenFramesCompared"] = between;
    result["psnrInterpolatedDb"] = flowMean;
    result["psnrHeldFrameDb"] = holdMean;
    result["exactFramesIdentical"] = exactSame;
    result["exactFramesChecked"] = exactChecked;
    result["status"] = {{"synthesised", flowRun.status.synthesised}, {"held", flowRun.status.held}, {"pending", flowRun.status.pending}, {"pairsDone", flowRun.status.pairsDone}, {"analysisAverageMs", flowRun.status.analysisAverageMs}, {"analysisP95Ms", flowRun.status.analysisP95Ms}, {"tierDrops", flowRun.status.tierDrops}};
    p.summary()["interpolation30to60"] = result;
    p.record("enough in-between frames were compared against the 60 fps original", between >= 8, std::to_string(between) + " frames");
    p.record("the interpolated frames are closer to the 60 fps original than the held frames", between >= 8 && flowMean > holdMean + 1.5, "interpolated " + number(flowMean) + " dB, held " + number(holdMean) + " dB, difference " + number(flowMean - holdMean) + " dB");
    p.record("frames that sit exactly on a source frame are bit identical to normal playback", exactChecked >= 8 && exactSame == exactChecked, std::to_string(exactSame) + " of " + std::to_string(exactChecked));
    p.record("the flow path produced frames and finished analyses", flowRun.status.synthesised >= 8 && flowRun.status.pairsDone >= 8, "synthesised " + std::to_string(flowRun.status.synthesised) + ", analyses " + std::to_string(flowRun.status.pairsDone) + ", analysis " + number(flowRun.status.analysisAverageMs) + " ms average, " + number(flowRun.status.analysisP95Ms) + " ms at the 95th percentile");
}

void testSlowMotion(Probe& p, int clip30, int clip60) {
    const int steps = 96;
    const Run off = p.play(clip30, flow::Settings{}, steps, 0.5f, 0, 16);
    const Run truth = p.play(clip60, flow::Settings{}, steps, 0.5f, 0, 16);
    const Run flowRun = p.play(clip30, makeSettings(flow::Mode::Interpolate), steps, 0.5f, 0, 16);
    if (!off.ok || !truth.ok || !flowRun.ok) {
        p.record("the slow motion test could be played", false, off.error + truth.error + flowRun.error);
        return;
    }
    double flowSum = 0.0;
    double holdSum = 0.0;
    int between = 0;
    std::set<std::uint64_t> flowPictures;
    std::set<std::uint64_t> holdPictures;
    for (const Shot& shot : flowRun.shots) {
        if (shot.index < 6) {
            continue;
        }
        flowPictures.insert(shot.hash);
        const Shot* reference = byPosition(truth, shot.position);
        const Shot* hold = byPosition(off, shot.position);
        if (hold != nullptr) {
            holdPictures.insert(hold->hash);
        }
        if (reference == nullptr || hold == nullptr) {
            continue;
        }
        const double phase = phaseOf(shot, 30.0);
        if (phase > 0.48 && phase < 0.52) {
            flowSum += psnr(shot, *reference);
            holdSum += psnr(*hold, *reference);
            ++between;
        }
    }
    const double flowMean = between > 0 ? flowSum / between : 0.0;
    const double holdMean = between > 0 ? holdSum / between : 0.0;
    json result;
    result["speed"] = 0.5;
    result["inBetweenFramesCompared"] = between;
    result["psnrInterpolatedDb"] = flowMean;
    result["psnrHeldFrameDb"] = holdMean;
    result["distinctPicturesInterpolated"] = flowPictures.size();
    result["distinctPicturesHeld"] = holdPictures.size();
    result["status"] = {{"synthesised", flowRun.status.synthesised}, {"held", flowRun.status.held}, {"pairsDone", flowRun.status.pairsDone}, {"tierDrops", flowRun.status.tierDrops}};
    p.summary()["slowMotion"] = result;
    p.record("slow motion at half speed has enough frames to compare", between >= 8, std::to_string(between) + " frames");
    p.record("slow motion at half speed is closer to the 60 fps original than held frames", between >= 8 && flowMean > holdMean + 1.5, "interpolated " + number(flowMean) + " dB, held " + number(holdMean) + " dB");
    p.record("slow motion shows more different pictures than held frames", flowPictures.size() >= holdPictures.size() * 2, std::to_string(flowPictures.size()) + " against " + std::to_string(holdPictures.size()));
}

void testHardCut(Probe& p, int cut) {
    const int steps = 56;
    const Run off = p.play(cut, flow::Settings{}, steps, 1.0f, 0, 16);
    const Run flowRun = p.play(cut, makeSettings(flow::Mode::Interpolate), steps, 1.0f, 0, 16);
    if (!off.ok || !flowRun.ok) {
        p.record("the hard cut clip could be played", false, off.error + flowRun.error);
        return;
    }
    int checked = 0;
    int same = 0;
    for (const Shot& shot : flowRun.shots) {
        const double frame = shot.position * 30.0;
        if (frame > 14.2 && frame < 14.8 && std::fabs(frame - std::round(frame)) > 0.3) {
            const Shot* hold = byIndex(off, shot.index);
            if (hold != nullptr) {
                ++checked;
                if (shot.hash == hold->hash) {
                    ++same;
                }
            }
        }
    }
    p.summary()["hardCut"] = {{"stepsAtTheCut", checked}, {"identicalToNormalPlayback", same}, {"cutsDetected", flowRun.status.cuts}};
    p.record("a hard cut is detected and never blended", flowRun.status.cuts >= 1, "cuts detected " + std::to_string(flowRun.status.cuts));
    p.record("the pictures across the hard cut equal normal playback", checked >= 1 && same == checked, std::to_string(same) + " of " + std::to_string(checked) + " steps inside the cut");
}

void testVariableRate(Probe& p, int vfr) {
    const Run flowRun = p.play(vfr, makeSettings(flow::Mode::Interpolate), 56, 1.0f, 0, 16);
    if (!flowRun.ok) {
        p.record("the variable frame rate clip could be played", false, flowRun.error);
        return;
    }
    p.summary()["variableFrameRate"] = {{"synthesised", flowRun.status.synthesised}, {"held", flowRun.status.held}, {"unusablePairs", flowRun.status.unusable}, {"analyses", flowRun.status.pairsDone}};
    p.record("a variable frame rate clip is interpolated where the timing allows", flowRun.status.synthesised >= 4, "synthesised " + std::to_string(flowRun.status.synthesised) + ", analyses " + std::to_string(flowRun.status.pairsDone));
    p.record("a long gap in the frame times falls back to the held picture", flowRun.status.unusable >= 1, "unusable pairs " + std::to_string(flowRun.status.unusable));
}

void testReverseFallback(Probe& p, int clip30) {
    const Run run = p.play(clip30, makeSettings(flow::Mode::Interpolate), 24, -1.0f, 0, 16);
    if (!run.ok) {
        p.record("the reverse playback test could be played", false, run.error);
        return;
    }
    bool never = true;
    for (const Shot& shot : run.shots) {
        never = never && !shot.engaged;
    }
    p.record("reverse playback falls back to plain playback with a reason", never && run.status.reason.find("reverse") != std::string::npos, "reason '" + run.status.reason + "'");
}

void testBudget(Probe& p, int clip30) {
    int owner = 0;
    const std::uint64_t limit = static_cast<std::uint64_t>(flow::kMinBudgetMb) * 1024ull * 1024ull;
    flow::Settings settings = makeSettings(flow::Mode::Interpolate);
    settings.budgetMb = flow::kMinBudgetMb;
    flow::Settings idle;
    idle.budgetMb = flow::kMinBudgetMb;
    p.engine().setFlowSettings(idle);
    const bool reserved = flow::globalBudget().reserve(&owner, limit - flow::globalBudget().used() - 1000ull);
    const Run run = p.play(clip30, settings, 16, 1.0f, 0, 16);
    flow::globalBudget().release(&owner);
    if (!run.ok || !reserved) {
        p.record("the memory budget test could be played", false, run.error + (reserved ? "" : " the budget could not be filled"));
        return;
    }
    bool never = true;
    for (const Shot& shot : run.shots) {
        never = never && !shot.engaged;
    }
    p.record("a full memory budget switches the feature off for the clip with a reason", never && run.status.reason.find("budget") != std::string::npos, "reason '" + run.status.reason + "'");
}

void testFrameDrops(Probe& p, int clip30) {
    const Run run = p.play(clip30, makeSettings(flow::Mode::Interpolate), 48, 1.0f, 5, 16);
    if (!run.ok) {
        p.record("the frame drop test could be played", false, run.error);
        return;
    }
    p.summary()["frameDrops"] = {{"synthesised", run.status.synthesised}, {"held", run.status.held}, {"unusable", run.status.unusable}, {"failures", run.status.failures}};
    p.record("skipped frames neither stop the clip nor produce upload failures", run.status.failures == 0 && !run.shots.empty(), "synthesised " + std::to_string(run.status.synthesised) + ", held " + std::to_string(run.status.held));
}

void testOtherModes(Probe& p, int clip30) {
    const Run off = p.play(clip30, flow::Settings{}, 30, 1.0f, 0, 16);
    const Run blur = p.play(clip30, makeSettings(flow::Mode::MotionBlur), 30, 1.0f, 0, 16);
    const Run vectors = p.play(clip30, makeSettings(flow::Mode::Vectors), 30, 1.0f, 0, 16);
    if (!off.ok || !blur.ok || !vectors.ok) {
        p.record("the motion blur and vector modes could be played", false, off.error + blur.error + vectors.error);
        return;
    }
    int differing = 0;
    int compared = 0;
    for (const Shot& shot : blur.shots) {
        const Shot* hold = byIndex(off, shot.index);
        if (hold != nullptr && shot.index > 6) {
            ++compared;
            differing += shot.hash != hold->hash ? 1 : 0;
        }
    }
    p.record("motion blur changes the picture where there is motion", compared >= 8 && differing >= compared / 2, std::to_string(differing) + " of " + std::to_string(compared) + " pictures differ, synthesised " + std::to_string(blur.status.synthesised));
    int moving = 0;
    int neutral = 0;
    for (const Shot& shot : vectors.shots) {
        if (shot.index < 8 || shot.sampled.empty()) {
            continue;
        }
        const std::size_t centre = (static_cast<std::size_t>(shot.sampledHeight / 2) * static_cast<std::size_t>(shot.sampledWidth) + static_cast<std::size_t>(shot.sampledWidth / 2)) * 4u;
        const int r = shot.sampled[centre];
        const int g = shot.sampled[centre + 1];
        if (std::abs(r - 128) > 6 || std::abs(g - 128) > 6) {
            ++moving;
        } else {
            ++neutral;
        }
    }
    p.record("the vector mode shows the measured motion as a picture", moving >= 5 && vectors.status.synthesised >= 5, std::to_string(moving) + " pictures with motion at the centre, " + std::to_string(neutral) + " neutral, synthesised " + std::to_string(vectors.status.synthesised));
}

void testAlpha(Probe& p, int clip) {
    const Run off = p.play(clip, flow::Settings{}, 24, 1.0f, 0, 16);
    const Run flowRun = p.play(clip, makeSettings(flow::Mode::Interpolate), 24, 1.0f, 0, 16);
    if (!off.ok || !flowRun.ok) {
        p.record("the clip with an alpha channel could be played", false, off.error + flowRun.error);
        return;
    }
    int checked = 0;
    int same = 0;
    for (const Shot& shot : flowRun.shots) {
        const double phase = phaseOf(shot, flowRun.fps);
        const Shot* hold = byIndex(off, shot.index);
        if (hold != nullptr && (phase < 0.02 || phase > 0.98)) {
            ++checked;
            same += shot.hash == hold->hash ? 1 : 0;
        }
    }
    p.summary()["alphaClip"] = {{"fps", flowRun.fps}, {"state", flowRun.status.state}, {"reason", flowRun.status.reason}, {"synthesised", flowRun.status.synthesised}, {"pairsDone", flowRun.status.pairsDone}};
    p.record("a clip with alpha either runs through the flow path or says why not", flowRun.status.synthesised > 0 || flowRun.status.held > 0 || !flowRun.status.reason.empty(), "state '" + flowRun.status.state + "', reason '" + flowRun.status.reason + "', synthesised " + std::to_string(flowRun.status.synthesised));
    p.record("exact frames of the alpha clip equal normal playback", checked == 0 || same == checked, std::to_string(same) + " of " + std::to_string(checked));
}

void testBenchmark(Probe& p, int clip30) {
    const Run off = p.play(clip30, flow::Settings{}, 40, 1.0f, 0, 0);
    const Run on = p.play(clip30, makeSettings(flow::Mode::Interpolate), 40, 1.0f, 0, 0);
    if (!off.ok || !on.ok) {
        p.record("the benchmark could be run", false, off.error + on.error);
        return;
    }
    json result;
    result["steps"] = 40;
    result["picture"] = std::to_string(p.engine().width()) + "x" + std::to_string(p.engine().height());
    result["msPerStepOff"] = off.stepMs;
    result["msPerStepInterpolate"] = on.stepMs;
    result["analysisAverageMs"] = on.status.analysisAverageMs;
    result["analysisP95Ms"] = on.status.analysisP95Ms;
    result["drawMsCpuSubmit"] = on.status.drawMs;
    result["flowMemoryBytes"] = on.status.gpuBytes;
    result["note"] = "measured on the software OpenGL renderer of this machine without any sleep between steps, not a graphics card number; the draw time is the CPU time to submit the pass, not GPU time";
    p.summary()["benchmark"] = result;
    p.record("the cost of the flow path is measured and bounded", on.stepMs > 0.0 && on.stepMs < 2000.0 && on.status.gpuBytes > 0, "off " + number(off.stepMs) + " ms per step, interpolate " + number(on.stepMs) + " ms per step, analysis " + number(on.status.analysisAverageMs) + " ms average, flow memory " + std::to_string(on.status.gpuBytes) + " bytes, software OpenGL");
}

}

int runFlowSelfTest(Engine& engine, const std::string& clipDirectory, const std::string& outputPath) {
    json report = json::object();
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["version"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const Composition savedComposition = engine.comp;
    Probe probe(engine);
    probe.prepare();
    const RecordCapabilities video = probeRecordCapabilities();
    const char* required = std::getenv("NYREXVJ_REQUIRE_VIDEO");
    if (!video.ffmpegLoaded) {
        if (required != nullptr && *required != '\0') {
            probe.record("the FFmpeg libraries are loaded", false, video.problem);
        } else {
            probe.summary()["skipped"] = "FFmpeg is not available: " + video.problem;
            logLine("Flow self test: skipped, FFmpeg is not available: " + video.problem);
        }
    } else {
        const int clip30 = probe.importClip(clipDirectory, "test_flow_30.mp4");
        const int clip60 = probe.importClip(clipDirectory, "test_flow_60.mp4");
        const int cut = probe.importClip(clipDirectory, "test_flow_cut.mp4");
        const int vfr = probe.importClip(clipDirectory, "test_flow_vfr.mp4");
        const int alpha = probe.importClip(clipDirectory, "test_prores_alpha.mov");
        probe.record("the flow test clips are in the library", clip30 >= 0 && clip60 >= 0 && cut >= 0 && vfr >= 0, "30 fps " + std::to_string(clip30) + ", 60 fps " + std::to_string(clip60) + ", cut " + std::to_string(cut) + ", variable " + std::to_string(vfr));
        if (clip30 >= 0 && clip60 >= 0 && cut >= 0 && vfr >= 0) {
            probe.step(6);
            testOffState(probe, clip30);
            testInterpolation(probe, clip30, clip60);
            testSlowMotion(probe, clip30, clip60);
            testHardCut(probe, cut);
            testVariableRate(probe, vfr);
            testReverseFallback(probe, clip30);
            testBudget(probe, clip30);
            testFrameDrops(probe, clip30);
            testOtherModes(probe, clip30);
            if (alpha >= 0) {
                testAlpha(probe, alpha);
            } else {
                probe.summary()["alphaSkipped"] = "test_prores_alpha.mov was not found";
            }
            testBenchmark(probe, clip30);
        }
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        probe.record("the graphics state is left clean", glGetError() == GL_NO_ERROR && program == 0, "no error and no program left bound");
    }
    probe.finish();
    engine.applyComposition(savedComposition);
    report["summary"] = probe.summary();
    report["frames"] = probe.frames();
    report["checks"] = probe.items();
    report["failed"] = probe.failed();
    report["passed"] = probe.failed() == 0;
    const std::string text = report.dump(2);
    logLine("Flow self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return probe.failed() == 0 ? 0 : 1;
}

}

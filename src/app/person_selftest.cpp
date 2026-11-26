#include "app/person_selftest.hpp"

#include "core/composition.hpp"
#include "core/live_source.hpp"
#include "core/log.hpp"
#include "core/person_mask.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "render/engine.hpp"
#include "render/person_session.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Box {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct Region {
    float x0 = 0.0f;
    float x1 = 1.0f;
    float y0 = 0.0f;
    float y1 = 1.0f;
};

struct Shot {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    bool ok = false;
};

std::string number(double value, int digits = 2) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

LiveFrame makeFrame(int width, int height, const Box* person, unsigned long long id, int phase) {
    LiveFrame frame;
    frame.format = LiveFormat::Rgba;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.id = id;
    frame.capturedAt = steadySeconds();
    frame.data.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            unsigned int hash = static_cast<unsigned int>(x) * 73856093u ^ static_cast<unsigned int>(y) * 19349663u ^ static_cast<unsigned int>(phase) * 83492791u;
            hash ^= hash >> 13;
            hash *= 1274126177u;
            const int noise = static_cast<int>((hash >> 16) % 5u) - 2;
            int r = 60 + static_cast<int>(80.0f * fx) + noise;
            int g = 70 + static_cast<int>(60.0f * fy) + noise;
            int b = 90 + noise;
            if (person != nullptr && fx >= person->x0 && fx < person->x1 && fy >= person->y0 && fy < person->y1) {
                r = 230 + noise;
                g = 200 + noise;
                b = 170 + noise;
            }
            const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride) + static_cast<std::size_t>(x) * 4u;
            frame.data[at] = static_cast<std::uint8_t>(std::clamp(r, 0, 255));
            frame.data[at + 1] = static_cast<std::uint8_t>(std::clamp(g, 0, 255));
            frame.data[at + 2] = static_cast<std::uint8_t>(std::clamp(b, 0, 255));
            frame.data[at + 3] = 255;
        }
    }
    return frame;
}

double luminance(const Shot& shot, int x, int fromTop) {
    const int row = shot.height - 1 - fromTop;
    const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(shot.width) + static_cast<std::size_t>(x)) * 4u;
    return 0.299 * shot.pixels[at] + 0.587 * shot.pixels[at + 1] + 0.114 * shot.pixels[at + 2];
}

void walk(const Shot& shot, const Region& region, const std::function<void(int, int)>& visit) {
    const int x0 = std::clamp(static_cast<int>(region.x0 * static_cast<float>(shot.width)), 0, shot.width - 1);
    const int x1 = std::clamp(static_cast<int>(region.x1 * static_cast<float>(shot.width)), x0 + 1, shot.width);
    const int y0 = std::clamp(static_cast<int>(region.y0 * static_cast<float>(shot.height)), 0, shot.height - 1);
    const int y1 = std::clamp(static_cast<int>(region.y1 * static_cast<float>(shot.height)), y0 + 1, shot.height);
    for (int y = y0; y < y1; y += 4) {
        for (int x = x0; x < x1; x += 4) {
            visit(x, y);
        }
    }
}

double meanLuminance(const Shot& shot, const Region& region) {
    double sum = 0.0;
    long long count = 0;
    walk(shot, region, [&](int x, int y) {
        sum += luminance(shot, x, y);
        ++count;
    });
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

double meanDifference(const Shot& a, const Shot& b, const Region& region) {
    double sum = 0.0;
    long long count = 0;
    walk(a, region, [&](int x, int y) {
        sum += std::fabs(luminance(a, x, y) - luminance(b, x, y));
        ++count;
    });
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

int largestDifference(const Shot& a, const Shot& b) {
    if (a.pixels.size() != b.pixels.size()) {
        return 255;
    }
    int largest = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        largest = std::max(largest, std::abs(static_cast<int>(a.pixels[i]) - static_cast<int>(b.pixels[i])));
    }
    return largest;
}

class Probe {
public:
    explicit Probe(Engine& engine) : engine_(engine) {}

    Engine& engine() { return engine_; }
    json& summary() { return summary_; }
    json& items() { return items_; }
    int failed() const { return failed_; }
    long long frames() const { return frames_; }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Person self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void step(int count) {
        for (int i = 0; i < count; ++i) {
            engine_.update(1.0 / 60.0);
            engine_.render();
            glFinish();
            ++frames_;
        }
    }

    Shot capture() {
        Shot shot;
        shot.ok = engine_.readOutput(shot.pixels, shot.width, shot.height) && shot.width > 0 && shot.height > 0;
        return shot;
    }

    void pump(int width, int height, const Box* person, int count) {
        for (int i = 0; i < count; ++i) {
            ++serial_;
            const LiveFrame frame = makeFrame(width, height, person, serial_, static_cast<int>(serial_ % 7));
            if (PersonSession* session = engine_.personSession()) {
                session->offer(frame);
            }
            step(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(36));
        }
    }

    bool pumpUntil(int width, int height, const Box* person, int maxFrames, const std::function<bool(const PersonSessionInfo&)>& done) {
        for (int i = 0; i < maxFrames; ++i) {
            pump(width, height, person, 1);
            PersonSession* session = engine_.personSession();
            if (session != nullptr && done(session->info())) {
                return true;
            }
        }
        return false;
    }

    bool waitUntil(int milliseconds, const std::function<bool(const PersonSessionInfo&)>& done) {
        const auto begin = Clock::now();
        while (Clock::now() - begin < std::chrono::milliseconds(milliseconds)) {
            step(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            PersonSession* session = engine_.personSession();
            if (session != nullptr && done(session->info())) {
                return true;
            }
        }
        return false;
    }

    void reset() {
        engine_.comp = Composition{};
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    void showClip(const std::string& name) {
        reset();
        engine_.paused = true;
        const int clip = clipIndexByName(name);
        engine_.comp.layers[0].setCell(0, clip);
        engine_.trigger(0, 0);
        step(3);
    }

    void setKey(float mode, float softness) {
        EffectSlot& slot = engine_.comp.layers[0].fx[0];
        setEffect(slot, effectIndexByName("Person Key"));
        slot.enabled = true;
        slot.params = {mode, softness, 0.0f, 0.55f};
    }

    void clearKey() { engine_.comp.layers[0].fx = {}; }

private:
    Engine& engine_;
    json summary_ = json::object();
    json items_ = json::array();
    int failed_ = 0;
    long long frames_ = 0;
    unsigned long long serial_ = 0;
};

person::Settings testSettings() {
    person::Settings settings;
    settings.mode = person::Mode::Difference;
    settings.input = person::Input::Camera;
    settings.slot = 0;
    settings.tier = person::Tier::Balanced;
    settings.rate = 30;
    settings.sensitivity = 50;
    settings.softness = 20;
    settings.stability = 20;
    return settings;
}

bool running(const PersonSessionInfo& info) {
    return info.state == person::State::Running;
}

bool seen(const PersonSessionInfo& info) {
    return info.state == person::State::Running && info.persons >= 1;
}

constexpr float kShownThreshold = 25.0f;

void testDefaultsAndPassThrough(Probe& p) {
    Engine& e = p.engine();
    p.showClip("Plasma");
    p.step(2);
    const Shot plain = p.capture();
    p.setKey(0.0f, 0.0f);
    p.step(3);
    const Shot keyed = p.capture();
    p.record("person detection is off by default and nothing is created", e.personSession() == nullptr && e.personSettings().mode == person::Mode::Off, e.personSession() == nullptr ? "no session exists" : "a session exists");
    const int largest = largestDifference(plain, keyed);
    p.record("the Person Key effect passes the picture through while detection is off", plain.ok && keyed.ok && largest <= 2, "largest channel difference " + std::to_string(largest) + " of 255");
    p.clearKey();
}

void testOrientation(Probe& p) {
    Engine& e = p.engine();
    const Box person{0.15f, 0.08f, 0.35f, 0.28f};
    e.setPersonFeedExternal(true);
    e.setPersonSettings(testSettings());
    p.record("switching person detection on creates the session", e.personSession() != nullptr, "mode difference, camera 1, balanced, 30 per second");

    p.showClip("Plasma");
    p.setKey(0.0f, 0.0f);
    p.pump(320, 180, nullptr, 18);
    const bool learned = p.waitUntil(4000, running);
    p.record("the empty scene is learned and the state becomes running", learned, learned ? "running" : "state is " + std::string(person::stateName(e.personSession()->info().state)));
    const PersonSessionInfo empty = e.personSession()->info();
    p.record("an empty scene reports nobody", empty.persons == 0 && e.sourceValue(BandPersonPresent) < 0.2f, "persons " + std::to_string(empty.persons) + ", present " + number(e.sourceValue(BandPersonPresent)));

    const bool found = p.pumpUntil(320, 180, &person, 60, seen);
    const PersonSessionInfo info = e.personSession()->info();
    p.record("a person entering the picture is found", found, "persons " + std::to_string(info.persons) + ", state " + person::stateName(info.state));
    const double x = e.sourceValue(BandPersonX);
    const double y = e.sourceValue(BandPersonY);
    p.record("the position matches the person: left and near the top", std::fabs(x - 0.25) < 0.1 && std::fabs(y - 0.82) < 0.12, "x " + number(x) + " (expected 0.25), y " + number(y) + " (expected 0.82, 0 is the bottom)");
    const size_t liveMemory = e.gpuMemory().live;
    p.record("the mask texture is counted in the graphics memory report", info.gpuBytes > 0 && liveMemory >= info.gpuBytes, "mask " + std::to_string(info.gpuBytes) + " bytes, live inputs in the report " + std::to_string(liveMemory) + " bytes");
    p.record("the modulation sources report a present person", e.sourceValue(BandPersonPresent) > 0.5f && e.sourceValue(BandPersonCount) > 0.0f && e.sourceValue(BandPersonSize) > 0.0f, "present " + number(e.sourceValue(BandPersonPresent)) + ", count " + number(e.sourceValue(BandPersonCount)) + ", size " + number(e.sourceValue(BandPersonSize)));

    p.pump(320, 180, &person, 6);
    p.clearKey();
    p.step(2);
    const Shot plain = p.capture();
    p.setKey(0.0f, 0.0f);
    p.step(2);
    const Shot keyed = p.capture();
    p.setKey(0.3f, 0.0f);
    p.step(2);
    const Shot inverse = p.capture();

    const Region inside{0.18f, 0.32f, 0.11f, 0.25f};
    const Region flipped{0.18f, 0.32f, 0.75f, 0.89f};
    const Region mirrored{0.68f, 0.82f, 0.11f, 0.25f};
    const Region far{0.55f, 0.95f, 0.40f, 0.95f};
    const double plainInside = meanLuminance(plain, inside);
    const double plainFlipped = meanLuminance(plain, flipped);
    const double plainMirrored = meanLuminance(plain, mirrored);
    const double plainFar = meanLuminance(plain, far);
    p.record("the test picture is bright where it is checked", plainInside > kShownThreshold && plainFlipped > kShownThreshold && plainMirrored > kShownThreshold && plainFar > kShownThreshold, "mean brightness " + number(plainInside, 1) + ", " + number(plainFlipped, 1) + ", " + number(plainMirrored, 1) + ", " + number(plainFar, 1));

    const double keptInside = meanDifference(keyed, plain, inside);
    const double blackFlipped = meanLuminance(keyed, flipped);
    const double blackMirrored = meanLuminance(keyed, mirrored);
    const double blackFar = meanLuminance(keyed, far);
    p.record("the mask lies upright: the picture stays where the person is", keptInside <= 6.0, "difference to the plain picture inside the person area " + number(keptInside, 2));
    p.record("the mask is not flipped or mirrored: everything else is the backdrop", blackFlipped <= 4.0 && blackMirrored <= 4.0 && blackFar <= 4.0, "mean brightness vertically flipped " + number(blackFlipped, 2) + ", horizontally mirrored " + number(blackMirrored, 2) + ", elsewhere " + number(blackFar, 2));

    const double goneInside = meanLuminance(inverse, inside);
    const double keptFlipped = meanDifference(inverse, plain, flipped);
    const double keptFar = meanDifference(inverse, plain, far);
    p.record("the inverted mode removes the person and keeps the rest", goneInside <= 4.0 && keptFlipped <= 6.0 && keptFar <= 6.0, "inside brightness " + number(goneInside, 2) + ", difference elsewhere " + number(keptFlipped, 2) + " and " + number(keptFar, 2));

    json item;
    item["personSignals"] = {{"x", x}, {"y", y}};
    item["plainBrightness"] = {{"inside", plainInside}, {"flipped", plainFlipped}, {"mirrored", plainMirrored}, {"far", plainFar}};
    item["keyedBrightness"] = {{"flipped", blackFlipped}, {"mirrored", blackMirrored}, {"far", blackFar}, {"insideDifference", keptInside}};
    p.summary()["orientation"] = item;
}

void testWideSource(Probe& p) {
    Engine& e = p.engine();
    const Box person{0.10f, 0.10f, 0.30f, 0.40f};
    p.setKey(0.0f, 0.0f);
    p.pump(320, 240, nullptr, 18);
    p.waitUntil(4000, running);
    p.pumpUntil(320, 240, &person, 70, seen);
    p.pump(320, 240, &person, 6);
    const bool found = seen(e.personSession()->info());
    p.record("a 4:3 camera picture is found on a 16:9 output", found, "persons " + std::to_string(e.personSession()->info().persons) + ", aspect " + number(e.personSession()->sourceAspect(), 3));
    p.clearKey();
    p.step(2);
    const Shot plain = p.capture();
    p.setKey(0.0f, 0.0f);
    p.step(2);
    const Shot keyed = p.capture();
    const Region inside{0.22f, 0.33f, 0.13f, 0.37f};
    const Region beside{0.02f, 0.12f, 0.10f, 0.90f};
    const Region elsewhere{0.55f, 0.95f, 0.50f, 0.95f};
    const double keptInside = meanDifference(keyed, plain, inside);
    const double blackBeside = meanLuminance(keyed, beside);
    const double blackElsewhere = meanLuminance(keyed, elsewhere);
    const double plainInside = meanLuminance(plain, inside);
    p.record("the mask follows the picture position when the camera is 4:3", plainInside > kShownThreshold && keptInside <= 6.0 && blackBeside <= 4.0 && blackElsewhere <= 4.0, "inside difference " + number(keptInside, 2) + ", outside the camera picture " + number(blackBeside, 2) + ", elsewhere " + number(blackElsewhere, 2));
}

void testCameraLoss(Probe& p) {
    Engine& e = p.engine();
    const Box person{0.10f, 0.10f, 0.30f, 0.40f};
    p.pump(320, 240, &person, 6);
    p.record("the person is still there before the camera is lost", e.sourceValue(BandPersonPresent) > 0.5f, "present " + number(e.sourceValue(BandPersonPresent)));
    const auto begin = Clock::now();
    const bool lost = p.waitUntil(9000, [&](const PersonSessionInfo& info) { return info.state == person::State::SourceLost && e.sourceValue(BandPersonPresent) < 0.1f; });
    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    p.record("losing the camera is reported and the person signal falls to zero", lost, "after " + number(seconds, 1) + " s, state " + person::stateName(e.personSession()->info().state) + ", present " + number(e.sourceValue(BandPersonPresent)));
    p.summary()["cameraLossSeconds"] = seconds;
    p.record("the render path stays clean while the camera is lost", glGetError() == GL_NO_ERROR, "no OpenGL error");

    p.pump(320, 240, &person, 8);
    const bool back = p.pumpUntil(320, 240, &person, 90, seen);
    p.record("the person is found again when the camera comes back", back, "persons " + std::to_string(e.personSession()->info().persons) + ", state " + person::stateName(e.personSession()->info().state));
}

void testLearnNow(Probe& p) {
    Engine& e = p.engine();
    PersonSession* session = e.personSession();
    session->learn();
    p.pump(320, 240, nullptr, 18);
    const bool ok = p.waitUntil(4000, [](const PersonSessionInfo& info) { return running(info) && info.persons == 0; });
    p.record("learning the background again with an empty scene clears the person", ok, "persons " + std::to_string(session->info().persons) + ", state " + person::stateName(session->info().state));
}

void testBenchmark(Probe& p) {
    Engine& e = p.engine();
    const Box person{0.15f, 0.08f, 0.35f, 0.28f};
    p.showClip("Plasma");
    p.setKey(0.0f, 0.4f);
    p.pump(1280, 720, nullptr, 18);
    p.waitUntil(4000, running);
    p.pumpUntil(1280, 720, &person, 60, seen);

    constexpr int kSteps = 40;
    e.setPersonSettings(person::Settings{});
    p.step(4);
    const auto offBegin = Clock::now();
    p.step(kSteps);
    const double offMs = std::chrono::duration<double, std::milli>(Clock::now() - offBegin).count() / kSteps;

    e.setPersonSettings(testSettings());
    p.pump(1280, 720, nullptr, 18);
    p.waitUntil(4000, running);
    p.pumpUntil(1280, 720, &person, 60, seen);
    double stepTotal = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const LiveFrame frame = makeFrame(1280, 720, &person, 100000 + static_cast<unsigned long long>(i), i % 7);
        const auto begin = Clock::now();
        e.personSession()->offer(frame);
        p.step(1);
        stepTotal += std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double onMs = stepTotal / kSteps;
    const PersonSessionInfo info = e.personSession()->info();

    json result;
    result["steps"] = kSteps;
    result["camera"] = "1280x720";
    result["picture"] = std::to_string(e.width()) + "x" + std::to_string(e.height());
    result["msPerStepOff"] = offMs;
    result["msPerStepOnWithOffer"] = onMs;
    result["analysisAverageMs"] = info.worker.averageMs;
    result["analysisP95Ms"] = info.worker.p95Ms;
    result["analysisBudgetMs"] = info.worker.budgetMs;
    result["reduceMs"] = info.reduceMs;
    result["analysisSize"] = std::to_string(info.worker.width) + "x" + std::to_string(info.worker.height);
    result["tier"] = person::tierName(info.tier);
    result["rate"] = info.rate;
    result["maskBytes"] = info.gpuBytes;
    result["lowered"] = info.worker.lowered;
    result["disabled"] = info.worker.disabled;
    result["note"] = "measured on the software OpenGL renderer of this machine with synthetic camera pictures, not a graphics card or a real camera number; msPerStepOnWithOffer includes handing the 1280x720 picture to the module and the main thread work for it";
    p.summary()["benchmark"] = result;
    p.record("the cost of person detection is measured and stays inside its budget or the governor reacts", info.worker.completed > 0 && !info.worker.disabled && (info.worker.averageMs < info.worker.budgetMs || info.worker.lowered > 0), "analysis " + number(info.worker.averageMs, 2) + " ms average, " + number(info.worker.p95Ms, 2) + " ms p95, budget " + number(info.worker.budgetMs, 1) + " ms, reduce " + number(info.reduceMs, 2) + " ms, step " + number(offMs) + " ms off and " + number(onMs) + " ms on, software OpenGL");
}

void testSwitchOff(Probe& p) {
    Engine& e = p.engine();
    p.clearKey();
    p.showClip("Plasma");
    p.step(2);
    const Shot plain = p.capture();
    p.setKey(0.0f, 0.0f);
    e.setPersonSettings(person::Settings{});
    e.setPersonFeedExternal(false);
    p.step(3);
    const Shot keyed = p.capture();
    p.record("switching person detection off removes the session", e.personSession() == nullptr, "no session exists");
    const int largest = largestDifference(plain, keyed);
    p.record("with detection off the mask is no longer counted in the memory report", e.gpuMemory().live == 0, "live inputs in the report " + std::to_string(e.gpuMemory().live) + " bytes");
    p.record("with detection off the Person Key effect is a pass-through again", largest <= 2 && e.sourceValue(BandPersonPresent) == 0.0f, "largest channel difference " + std::to_string(largest) + " of 255, present " + number(e.sourceValue(BandPersonPresent)));
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    p.record("the graphics state is left clean", glGetError() == GL_NO_ERROR && program == 0, "no error and no program left bound");
}

}

int runPersonSelfTest(Engine& engine, const std::string& outputPath) {
    json report = json::object();
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["version"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const Composition savedComposition = engine.comp;
    const bool savedPaused = engine.paused;
    Probe probe(engine);
    engine.blackout = false;
    engine.paused = true;
    engine.setGpuTiming(false);
    engine.setPersonSettings(person::Settings{});
    engine.setPersonFeedExternal(false);
    probe.reset();
    probe.step(4);

    testDefaultsAndPassThrough(probe);
    testOrientation(probe);
    testWideSource(probe);
    testCameraLoss(probe);
    testLearnNow(probe);
    testBenchmark(probe);
    testSwitchOff(probe);

    engine.setPersonSettings(person::Settings{});
    engine.setPersonFeedExternal(false);
    engine.applyComposition(savedComposition);
    engine.paused = savedPaused;
    probe.step(2);

    report["summary"] = probe.summary();
    report["frames"] = probe.frames();
    report["checks"] = probe.items();
    report["failed"] = probe.failed();
    report["passed"] = probe.failed() == 0;
    const std::string text = report.dump(2);
    logLine("Person self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return probe.failed() == 0 ? 0 : 1;
}

}

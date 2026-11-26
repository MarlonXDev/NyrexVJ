#include "check.hpp"

#include "core/composition.hpp"
#include "core/modulation.hpp"
#include "core/person_mask.hpp"
#include "core/settings.hpp"
#include "media/person_worker.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace nvj::person;
using nvj::LiveFormat;
using nvj::LiveFrame;
using nvj::MaskBitmap;
using nvj::PersonStats;
using nvj::PersonWorker;

constexpr int kW = 160;
constexpr int kH = 90;
constexpr double kFps = 15.0;

struct Rng {
    std::uint32_t state = 12345u;

    std::uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }

    float uniform() { return static_cast<float>(next()) / 16777216.0f; }

    float gauss() {
        const float a = std::max(uniform(), 1.0e-6f);
        const float b = uniform();
        return std::sqrt(-2.0f * std::log(a)) * std::cos(6.2831853f * b);
    }
};

struct Actor {
    float cx = 0.0f;
    float cy = 0.0f;
    float rx = 10.0f;
    float ry = 20.0f;
    int r = 220;
    int g = 70;
    int b = 60;
};

float backgroundValue(int x, int y, int c, int seed) {
    const float s = static_cast<float>(seed);
    float v = 70.0f + 35.0f * std::sin(0.19f * static_cast<float>(x) + static_cast<float>(c) + s) + 30.0f * std::cos(0.23f * static_cast<float>(y) + 2.0f * static_cast<float>(c) + 0.5f * s);
    v += ((x / 8 + y / 8) % 2) != 0 ? 10.0f : 0.0f;
    return v;
}

struct Scene {
    int width = kW;
    int height = kH;
    float noise = 2.0f;
    float gain[3] = {1.0f, 1.0f, 1.0f};
    int seed = 0;
    std::vector<Actor> actors;
    Rng rng;
    std::vector<Actor> occluders;

    Picture render(double time, std::vector<std::uint8_t>* truth) {
        Picture p;
        p.width = width;
        p.height = height;
        p.time = time;
        p.id = static_cast<unsigned long long>(time * 1000.0) + 1ull;
        p.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
        if (truth != nullptr) {
            truth->assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                float rgb[3];
                for (int c = 0; c < 3; ++c) {
                    rgb[c] = backgroundValue(x, y, c, seed);
                }
                float coverage = 0.0f;
                int colour[3] = {0, 0, 0};
                for (const Actor& a : actors) {
                    const float nx = (static_cast<float>(x) + 0.5f - a.cx) / a.rx;
                    const float ny = (static_cast<float>(y) + 0.5f - a.cy) / a.ry;
                    const float d = (std::sqrt(nx * nx + ny * ny) - 1.0f) * std::min(a.rx, a.ry);
                    const float c = std::clamp(0.5f - d, 0.0f, 1.0f);
                    if (c > 0.0f) {
                        coverage = c;
                        colour[0] = a.r;
                        colour[1] = a.g;
                        colour[2] = a.b;
                        if (truth != nullptr && c > 0.5f) {
                            (*truth)[i] = 1;
                        }
                    }
                }
                for (int c = 0; c < 3; ++c) {
                    float v = rgb[c] * (1.0f - coverage) + static_cast<float>(colour[c]) * coverage;
                    v *= gain[c];
                    for (const Actor& o : occluders) {
                        const float nx = (static_cast<float>(x) + 0.5f - o.cx) / o.rx;
                        const float ny = (static_cast<float>(y) + 0.5f - o.cy) / o.ry;
                        if (std::abs(nx) <= 1.0f && std::abs(ny) <= 1.0f) {
                            v = backgroundValue(x, y, c, seed) * gain[c];
                            if (truth != nullptr) {
                                (*truth)[i] = 0;
                            }
                        }
                    }
                    v += noise * rng.gauss();
                    p.rgb[i * 3u + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
                }
            }
        }
        return p;
    }
};

struct Harness {
    Scene scene;
    Analyzer analyzer;
    Result result;
    int frame = 0;
    std::vector<std::uint8_t> truth;
    bool lastOk = true;

    explicit Harness(const Settings& settings = Settings(), std::unique_ptr<Backend> backend = makeBackend(Mode::Difference))
        : analyzer(std::move(backend)) {
        Settings s = settings;
        s.mode = Mode::Difference;
        analyzer.configure(s);
    }

    void step() {
        const Picture picture = scene.render(static_cast<double>(frame) / kFps, &truth);
        lastOk = analyzer.process(picture, result);
        ++frame;
    }

    void run(int count) {
        for (int i = 0; i < count; ++i) {
            step();
        }
    }

    void learn() { run(14); }
};

double iou(const MaskBitmap& mask, const std::vector<std::uint8_t>& truth) {
    if (mask.coverage.size() != truth.size() || truth.empty()) {
        return 0.0;
    }
    std::size_t inter = 0;
    std::size_t uni = 0;
    for (std::size_t i = 0; i < truth.size(); ++i) {
        const bool a = mask.coverage[i] >= 128;
        const bool b = truth[i] != 0;
        inter += (a && b) ? 1u : 0u;
        uni += (a || b) ? 1u : 0u;
    }
    return uni == 0 ? 1.0 : static_cast<double>(inter) / static_cast<double>(uni);
}

double falsePositive(const MaskBitmap& mask, const std::vector<std::uint8_t>& truth) {
    std::size_t outside = 0;
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < truth.size() && i < mask.coverage.size(); ++i) {
        if (truth[i] == 0) {
            ++outside;
            wrong += mask.coverage[i] >= 128 ? 1u : 0u;
        }
    }
    return outside == 0 ? 0.0 : static_cast<double>(wrong) / static_cast<double>(outside);
}

double elapsedMs(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

LiveFrame uyvyFrame(int width, int height, int y, int u, int v) {
    LiveFrame f;
    f.format = LiveFormat::Uyvy;
    f.width = width;
    f.height = height;
    f.stride = width * 2;
    f.id = 7;
    f.capturedAt = 1.0;
    f.data.resize(static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i + 3 < f.data.size(); i += 4) {
        f.data[i] = static_cast<std::uint8_t>(u);
        f.data[i + 1] = static_cast<std::uint8_t>(y);
        f.data[i + 2] = static_cast<std::uint8_t>(v);
        f.data[i + 3] = static_cast<std::uint8_t>(y);
    }
    return f;
}

LiveFrame rgbaFrame(int width, int height, int r, int g, int b) {
    LiveFrame f;
    f.format = LiveFormat::Rgba;
    f.width = width;
    f.height = height;
    f.stride = width * 4;
    f.id = 9;
    f.capturedAt = 2.0;
    f.data.resize(static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i + 3 < f.data.size(); i += 4) {
        f.data[i] = static_cast<std::uint8_t>(r);
        f.data[i + 1] = static_cast<std::uint8_t>(g);
        f.data[i + 2] = static_cast<std::uint8_t>(b);
        f.data[i + 3] = 255;
    }
    return f;
}

void testNamesAndLimits() {
    CHECK(std::string(modeName(Mode::Off)) == "off");
    CHECK(std::string(modeName(Mode::Difference)) == "difference");
    CHECK(modeFromName("difference", Mode::Off) == Mode::Difference);
    CHECK(modeFromName("off", Mode::Difference) == Mode::Off);
    CHECK(modeFromName("model", Mode::Off) == Mode::Off);
    CHECK(modeFromName("", Mode::Difference) == Mode::Difference);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        CHECK(tierFromName(tierName(tier), Tier::Balanced) == tier);
    }
    CHECK(tierFromName("huge", Tier::Fast) == Tier::Fast);
    CHECK(clampTier(-4) == Tier::Fast);
    CHECK(clampTier(99) == Tier::Fine);
    CHECK(clampRate(0) == kMinRate);
    CHECK(clampRate(500) == kMaxRate);
    CHECK(clampRate(12) == 12);
    CHECK(clampPercent(-1) == 0);
    CHECK(clampPercent(101) == 100);

    for (int i = 0; i < kInputCount; ++i) {
        for (int slot = 0; slot < 4; ++slot) {
            Input input = Input::Camera;
            int parsed = -1;
            CHECK(parseInput(inputText(static_cast<Input>(i), slot), input, parsed));
            CHECK(input == static_cast<Input>(i));
            CHECK(parsed == slot);
        }
    }
    Input input = Input::Camera;
    int slot = 0;
    CHECK(!parseInput("camera5", input, slot));
    CHECK(!parseInput("camera0", input, slot));
    CHECK(!parseInput("camera", input, slot));
    CHECK(!parseInput("webcam1", input, slot));
    CHECK(!parseInput("", input, slot));
    CHECK(!parseInput("camera11", input, slot));

    Settings wild;
    wild.mode = static_cast<Mode>(9);
    wild.input = static_cast<Input>(-3);
    wild.slot = 77;
    wild.tier = static_cast<Tier>(12);
    wild.rate = -5;
    wild.sensitivity = 400;
    wild.softness = -1;
    wild.stability = 101;
    const Settings clean = sanitised(wild);
    CHECK(clean.mode == Mode::Difference);
    CHECK(clean.input == Input::Camera);
    CHECK(clean.slot == 3);
    CHECK(clean.tier == Tier::Fine);
    CHECK(clean.rate == kMinRate);
    CHECK(clean.sensitivity == 100);
    CHECK(clean.softness == 0);
    CHECK(clean.stability == 100);
    CHECK(Settings() == sanitised(Settings()));
    CHECK(Settings().mode == Mode::Off);
}

void testAnalysisSize() {
    int w = 0;
    int h = 0;
    analysisSize(1920, 1080, Tier::Balanced, w, h);
    CHECK(w == 160 && h == 90);
    analysisSize(1920, 1080, Tier::Fast, w, h);
    CHECK(w == 96 && h == 54);
    analysisSize(1920, 1080, Tier::Fine, w, h);
    CHECK(w == 256 && h == 144);
    analysisSize(64, 48, Tier::Fine, w, h);
    CHECK(w == 64 && h == 48);
    analysisSize(1080, 1920, Tier::Fine, w, h);
    CHECK(w > 0 && h > 0);
    CHECK(static_cast<long long>(w) * h <= kMaxPicturePixels);
    CHECK(std::abs(static_cast<double>(h) / w - 1920.0 / 1080.0) < 0.05);
    analysisSize(0, 100, Tier::Fast, w, h);
    CHECK(w == 0 && h == 0);
    analysisSize(100, -2, Tier::Fast, w, h);
    CHECK(w == 0 && h == 0);
    analysisSize(4000, 1, Tier::Balanced, w, h);
    CHECK(w == 160 && h == 1);
    for (int tier = 0; tier < kTierCount; ++tier) {
        for (int sw : {2, 17, 640, 3840, 7680}) {
            for (int sh : {1, 9, 480, 2160}) {
                analysisSize(sw, sh, static_cast<Tier>(tier), w, h);
                CHECK(w >= 1 && h >= 1);
                CHECK(static_cast<long long>(w) * h <= kMaxPicturePixels);
                CHECK(w <= sw);
            }
        }
    }
}

void testReduce() {
    Picture p;
    LiveFrame white = uyvyFrame(64, 48, 235, 128, 128);
    CHECK(reduce(white, Tier::Balanced, p));
    CHECK(p.valid() && p.width == 64 && p.height == 48);
    CHECK(p.rgb[0] >= 252 && p.rgb[1] >= 252 && p.rgb[2] >= 252);
    CHECK(p.id == 7 && p.time == 1.0);
    LiveFrame black = uyvyFrame(64, 48, 16, 128, 128);
    CHECK(reduce(black, Tier::Balanced, p));
    CHECK(p.rgb[0] <= 3 && p.rgb[1] <= 3 && p.rgb[2] <= 3);
    LiveFrame red = uyvyFrame(64, 48, 81, 90, 240);
    CHECK(reduce(red, Tier::Balanced, p));
    CHECK(p.rgb[0] > 240 && p.rgb[1] < 25 && p.rgb[2] < 25);
    LiveFrame blue = uyvyFrame(64, 48, 41, 240, 110);
    CHECK(reduce(blue, Tier::Balanced, p));
    CHECK(p.rgb[2] > 200 && p.rgb[0] < 60);
    LiveFrame hd = uyvyFrame(1280, 720, 81, 90, 240);
    CHECK(reduce(hd, Tier::Fast, p));
    CHECK(p.width == 96 && p.height == 54);
    CHECK(p.rgb[0] > 220 && p.rgb[1] < 40);
    LiveFrame rgba = rgbaFrame(64, 48, 10, 200, 90);
    CHECK(reduce(rgba, Tier::Balanced, p));
    CHECK(p.rgb[0] == 10 && p.rgb[1] == 200 && p.rgb[2] == 90);
    LiveFrame odd = rgbaFrame(63, 47, 1, 2, 3);
    CHECK(reduce(odd, Tier::Balanced, p));
    CHECK(p.width == 63 && p.height == 47);
    LiveFrame oddYuv = uyvyFrame(62, 47, 120, 128, 128);
    CHECK(reduce(oddYuv, Tier::Balanced, p));

    LiveFrame bad = rgba;
    bad.stride = 10;
    CHECK(!reduce(bad, Tier::Balanced, p));
    CHECK(!p.valid());
    bad = rgba;
    bad.data.resize(bad.data.size() - 8);
    CHECK(!reduce(bad, Tier::Balanced, p));
    bad = rgba;
    bad.format = LiveFormat::None;
    CHECK(!reduce(bad, Tier::Balanced, p));
    bad = rgba;
    bad.width = 1;
    CHECK(!reduce(bad, Tier::Balanced, p));
    bad = rgba;
    bad.width = 70000;
    CHECK(!reduce(bad, Tier::Balanced, p));
    bad = rgba;
    bad.height = 0;
    CHECK(!reduce(bad, Tier::Balanced, p));
    bad = uyvyFrame(64, 48, 100, 128, 128);
    bad.stride = 64 * 2 - 2;
    CHECK(!reduce(bad, Tier::Balanced, p));
    LiveFrame empty;
    CHECK(!reduce(empty, Tier::Balanced, p));

    LiveFrame full = uyvyFrame(1920, 1080, 81, 90, 240);
    const auto begin = std::chrono::steady_clock::now();
    const int rounds = 40;
    for (int i = 0; i < rounds; ++i) {
        CHECK(reduce(full, Tier::Balanced, p));
    }
    const double ms = elapsedMs(begin) / rounds;
    std::printf("reduce 1920x1080 UYVY to %dx%d: %.3f ms per picture\n", p.width, p.height, ms);
    CHECK(ms < 100.0);
}

MaskBitmap bitmap(int w, int h) {
    MaskBitmap m;
    m.width = w;
    m.height = h;
    m.coverage.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    return m;
}

void fillRect(MaskBitmap& m, int x0, int y0, int x1, int y1, std::uint8_t v = 255) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            m.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)] = v;
        }
    }
}

void testBlobs() {
    MaskBitmap m = bitmap(40, 30);
    fillRect(m, 2, 3, 11, 12);
    fillRect(m, 20, 5, 24, 8);
    fillRect(m, 30, 20, 30, 20);
    std::vector<Blob> blobs = findBlobs(m, 128, 1, 8);
    CHECK(blobs.size() == 3);
    CHECK(blobs[0].area == 100);
    CHECK(blobs[0].minX == 2 && blobs[0].maxX == 11 && blobs[0].minY == 3 && blobs[0].maxY == 12);
    CHECK(std::abs(blobs[0].cx - 7.0f) < 0.01f && std::abs(blobs[0].cy - 8.0f) < 0.01f);
    CHECK(blobs[1].area == 20);
    CHECK(blobs[2].area == 1);
    blobs = findBlobs(m, 128, 5, 8);
    CHECK(blobs.size() == 2);
    blobs = findBlobs(m, 128, 1, 1);
    CHECK(blobs.size() == 1 && blobs[0].area == 100);
    blobs = findBlobs(m, 128, 1, 0);
    CHECK(blobs.empty());

    MaskBitmap diagonal = bitmap(10, 10);
    diagonal.coverage[0] = 255;
    diagonal.coverage[11] = 255;
    CHECK(findBlobs(diagonal, 128, 1, 8).size() == 2);

    MaskBitmap ushape = bitmap(20, 20);
    fillRect(ushape, 2, 2, 3, 15);
    fillRect(ushape, 12, 2, 13, 15);
    fillRect(ushape, 2, 14, 13, 15);
    blobs = findBlobs(ushape, 128, 1, 8);
    CHECK(blobs.size() == 1);
    CHECK(blobs[0].area == 2 * 14 + 2 * 14 + 8 * 2);

    MaskBitmap soft = bitmap(10, 10);
    fillRect(soft, 2, 2, 5, 5, 100);
    CHECK(findBlobs(soft, 128, 1, 8).empty());
    CHECK(findBlobs(soft, 64, 1, 8).size() == 1);

    CHECK(findBlobs(MaskBitmap(), 128, 1, 8).empty());
    MaskBitmap broken = bitmap(10, 10);
    broken.coverage.resize(5);
    CHECK(findBlobs(broken, 128, 1, 8).empty());
}

std::vector<Blob> blobAt(float cx, float cy, int w, int h, int size) {
    Blob b;
    b.minX = static_cast<int>(cx - size / 2);
    b.maxX = b.minX + size - 1;
    b.minY = static_cast<int>(cy - size);
    b.maxY = b.minY + size * 2 - 1;
    b.cx = cx;
    b.cy = cy;
    b.area = size * size * 2;
    (void)w;
    (void)h;
    return {b};
}

void testTracker() {
    Tracker tracker;
    const double dt = 1.0 / kFps;
    int id = 0;
    for (int i = 0; i < 30; ++i) {
        tracker.update(blobAt(20.0f + 2.0f * static_cast<float>(i), 45.0f, kW, kH, 14), kW, kH, dt);
        CHECK(tracker.tracks().size() == 1);
        if (i == 0) {
            CHECK(tracker.tracks()[0].state == TrackState::Tentative);
            id = tracker.tracks()[0].id;
        }
        if (i == 3) {
            CHECK(tracker.tracks()[0].state == TrackState::Tracked);
        }
        CHECK(tracker.tracks()[0].id == id);
    }
    const Track moving = tracker.tracks()[0];
    CHECK(moving.vx > 0.1f);
    CHECK(std::abs(moving.vy) < 0.1f);
    CHECK(std::abs(moving.x - (20.0f + 58.0f) / kW) < 0.03f);

    tracker.update({}, kW, kH, dt);
    CHECK(tracker.tracks().size() == 1);
    CHECK(tracker.tracks()[0].state == TrackState::Coasting);
    for (int i = 0; i < 40; ++i) {
        tracker.update({}, kW, kH, dt);
    }
    CHECK(tracker.tracks().empty());

    Tracker flicker;
    flicker.update(blobAt(80.0f, 45.0f, kW, kH, 14), kW, kH, dt);
    flicker.update({}, kW, kH, dt);
    flicker.update({}, kW, kH, dt);
    CHECK(flicker.tracks().empty());

    Tracker two;
    for (int i = 0; i < 6; ++i) {
        std::vector<Blob> both = blobAt(30.0f, 45.0f, kW, kH, 14);
        const std::vector<Blob> other = blobAt(120.0f, 45.0f, kW, kH, 12);
        both.insert(both.end(), other.begin(), other.end());
        two.update(both, kW, kH, dt);
    }
    CHECK(two.tracks().size() == 2);
    CHECK(two.tracks()[0].id != two.tracks()[1].id);
    CHECK(two.tracks()[0].state == TrackState::Tracked && two.tracks()[1].state == TrackState::Tracked);

    Tracker cap;
    std::vector<Blob> many;
    for (int i = 0; i < 12; ++i) {
        Blob b;
        b.minX = i * 12;
        b.maxX = b.minX + 7;
        b.minY = 10;
        b.maxY = 30;
        b.cx = static_cast<float>(b.minX) + 4.0f;
        b.cy = 20.0f;
        b.area = 8 * 21;
        many.push_back(b);
    }
    for (int i = 0; i < 5; ++i) {
        cap.update(many, 160, 90, dt);
    }
    CHECK(static_cast<int>(cap.tracks().size()) <= kMaxTracks);

    Tracker none;
    none.update({}, 0, 0, dt);
    none.update(blobAt(10.0f, 10.0f, 0, 0, 4), 0, 0, dt);
    CHECK(none.tracks().empty());
    none.update(blobAt(10.0f, 10.0f, kW, kH, 4), kW, kH, 0.0);
    none.update(blobAt(10.0f, 10.0f, kW, kH, 4), kW, kH, 1.0e9);
    none.clear();
    CHECK(none.tracks().empty());
}

void testStaticSceneHasNoForeground() {
    for (float noise : {0.0f, 2.0f, 4.0f}) {
        Harness h;
        h.scene.noise = noise;
        h.run(14 + 45);
        CHECK(h.lastOk);
        CHECK(h.result.state == State::Running);
        CHECK(h.result.foregroundPercent == 0);
        CHECK(h.result.signals.count == 0);
        CHECK(!h.result.signals.present);
        CHECK(h.result.tracks.empty());
    }
}

void testDetectionAtAllTiers() {
    struct Case {
        int w;
        int h;
        float scale;
    };
    for (const Case& c : {Case{96, 54, 0.6f}, Case{160, 90, 1.0f}, Case{256, 144, 1.6f}}) {
        Harness h;
        h.scene.width = c.w;
        h.scene.height = c.h;
        h.learn();
        h.scene.actors = {{c.w * 0.5f, c.h * 0.55f, 10.0f * c.scale, 20.0f * c.scale, 220, 70, 60}};
        h.run(12);
        const double quality = iou(h.result.mask, h.truth);
        std::printf("detection %dx%d: IoU %.3f, false positives %.4f\n", c.w, c.h, quality, falsePositive(h.result.mask, h.truth));
        CHECK(quality > 0.85);
        CHECK(falsePositive(h.result.mask, h.truth) < 0.01);
        CHECK(h.result.signals.count == 1);
        CHECK(h.result.state == State::Running);
    }
}

void testSensitivity() {
    Settings low;
    low.sensitivity = 0;
    Settings high;
    high.sensitivity = 100;
    Harness a(low);
    Harness b(high);
    for (Harness* h : {&a, &b}) {
        h->learn();
        h->scene.actors = {{80.0f, 50.0f, 10.0f, 20.0f, 100, 105, 95}};
        h->run(12);
    }
    const double qa = iou(a.result.mask, a.truth);
    const double qb = iou(b.result.mask, b.truth);
    std::printf("faint person (colour close to the background): IoU %.3f at sensitivity 0, %.3f at 100\n", qa, qb);
    CHECK(qb >= qa);
}

std::vector<int> row(const MaskBitmap& m, int y) {
    std::vector<int> values;
    for (int x = 0; x < m.width; ++x) {
        values.push_back(m.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)]);
    }
    return values;
}

int transitionWidth(const MaskBitmap& m, int y) {
    int count = 0;
    for (int v : row(m, y)) {
        count += (v > 16 && v < 240) ? 1 : 0;
    }
    return count;
}

void testAlphaEdges() {
    int widthAtZero = 0;
    int widthAtHalf = 0;
    int widthAtMax = 0;
    for (int softness : {0, 50, 100}) {
        Settings s;
        s.softness = softness;
        Harness h(s);
        h.scene.noise = 1.0f;
        h.learn();
        h.scene.actors = {{80.0f, 45.0f, 18.0f, 30.0f, 220, 70, 60}};
        h.run(14);
        const std::vector<int> values = row(h.result.mask, 45);
        CHECK(values[80] == 255);
        CHECK(values[2] == 0 && values[157] == 0);
        const int width = transitionWidth(h.result.mask, 45);
        std::printf("edge width at softness %d: %d pixels on both sides, IoU %.3f\n", softness, width, iou(h.result.mask, h.truth));
        CHECK(softness == 0 || width >= 2);
        CHECK(width <= 24);
        for (int v : values) {
            CHECK(v >= 0 && v <= 255);
        }
        int previous = 0;
        for (int x = 40; x <= 80; ++x) {
            CHECK(values[static_cast<std::size_t>(x)] + 40 >= previous);
            previous = values[static_cast<std::size_t>(x)];
        }
        if (softness == 0) {
            widthAtZero = width;
        }
        if (softness == 50) {
            widthAtHalf = width;
        }
        if (softness == 100) {
            widthAtMax = width;
        }
        CHECK(iou(h.result.mask, h.truth) > 0.8);
    }
    CHECK(widthAtHalf > widthAtZero);
    CHECK(widthAtMax > widthAtHalf);
}

double flicker(int stability, std::vector<int>* latency = nullptr) {
    Settings s;
    s.stability = stability;
    Harness h(s);
    h.scene.noise = 5.0f;
    h.learn();
    h.scene.actors = {{80.0f, 45.0f, 14.0f, 26.0f, 190, 110, 80}};
    int first = -1;
    for (int i = 0; i < 12; ++i) {
        h.step();
        if (first < 0 && iou(h.result.mask, h.truth) >= 0.5) {
            first = i + 1;
        }
    }
    if (latency != nullptr) {
        latency->push_back(first);
    }
    MaskBitmap last = h.result.mask;
    double change = 0.0;
    const int frames = 60;
    for (int i = 0; i < frames; ++i) {
        h.step();
        for (std::size_t k = 0; k < last.coverage.size(); ++k) {
            change += std::abs(static_cast<int>(h.result.mask.coverage[k]) - static_cast<int>(last.coverage[k]));
        }
        last = h.result.mask;
    }
    return change / (255.0 * static_cast<double>(last.coverage.size()) * frames);
}

void testTemporalStability() {
    std::vector<int> latencyLow;
    std::vector<int> latencyHigh;
    const double low = flicker(0, &latencyLow);
    const double mid = flicker(50);
    const double high = flicker(100, &latencyHigh);
    std::printf("flicker (mean change per pixel and frame): stability 0 %.6f, 50 %.6f, 100 %.6f; frames until the person is half visible: %d, %d\n", low, mid, high, latencyLow[0], latencyHigh[0]);
    CHECK(high <= low);
    CHECK(high < 0.01);
    CHECK(latencyLow[0] > 0 && latencyLow[0] <= 3);
    CHECK(latencyHigh[0] > 0 && latencyHigh[0] <= 8);
    CHECK(latencyHigh[0] >= latencyLow[0]);
}

void testMultiplePersons() {
    Harness h;
    h.learn();
    h.scene.actors = {{30.0f, 50.0f, 9.0f, 18.0f, 220, 70, 60}, {80.0f, 45.0f, 12.0f, 24.0f, 40, 160, 210}, {128.0f, 52.0f, 8.0f, 16.0f, 230, 200, 40}};
    h.run(14);
    CHECK(h.result.signals.count == 3);
    CHECK(h.result.tracks.size() == 3);
    std::vector<int> ids;
    for (const Track& t : h.result.tracks) {
        ids.push_back(t.id);
        CHECK(t.state == TrackState::Tracked);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::unique(ids.begin(), ids.end()) == ids.end());
    const float expected[3][2] = {{30.0f / kW, 50.0f / kH}, {80.0f / kW, 45.0f / kH}, {128.0f / kW, 52.0f / kH}};
    for (const auto& e : expected) {
        bool found = false;
        for (const Track& t : h.result.tracks) {
            found = found || (std::abs(t.x - e[0]) < 0.04f && std::abs(t.y - e[1]) < 0.05f);
        }
        CHECK(found);
    }
    CHECK(iou(h.result.mask, h.truth) > 0.8);
    CHECK(h.result.signals.primary == h.result.tracks[0].id || h.result.signals.primary > 0);
    CHECK(std::abs(h.result.signals.x - 80.0f / kW) < 0.05f);

    h.scene.actors.pop_back();
    h.run(40);
    CHECK(h.result.signals.count == 2);
    h.scene.actors.clear();
    h.run(60);
    CHECK(h.result.signals.count == 0);
    CHECK(h.result.signals.presence < 0.05f);
}

void testCrossingPersons() {
    Harness h;
    h.learn();
    Actor a{20.0f, 50.0f, 9.0f, 20.0f, 220, 70, 60};
    Actor b{140.0f, 48.0f, 12.0f, 26.0f, 40, 160, 210};
    h.scene.actors = {a, b};
    h.run(8);
    CHECK(h.result.tracks.size() == 2);
    int idA = 0;
    int idB = 0;
    for (const Track& t : h.result.tracks) {
        (t.x < 0.5f ? idA : idB) = t.id;
    }
    CHECK(idA != 0 && idB != 0 && idA != idB);
    bool merged = false;
    for (int i = 0; i < 80; ++i) {
        h.scene.actors[0].cx += 1.5f;
        h.scene.actors[1].cx -= 1.5f;
        h.step();
        if (h.result.signals.count < 2 || h.result.tracks.size() < 2) {
            CHECK(!h.result.tracks.empty());
        }
        std::vector<int> present;
        for (const Track& t : h.result.tracks) {
            present.push_back(t.id);
        }
        merged = merged || std::abs(h.scene.actors[0].cx - h.scene.actors[1].cx) < 10.0f;
    }
    CHECK(merged);
    h.run(10);
    int endA = 0;
    int endB = 0;
    for (const Track& t : h.result.tracks) {
        (t.x < 0.5f ? endA : endB) = t.id;
    }
    std::printf("two persons crossing: start ids %d/%d, end ids on the left/right %d/%d, tracks %zu\n", idA, idB, endA, endB, h.result.tracks.size());
    CHECK(h.result.tracks.size() == 2);
    CHECK(h.result.signals.count == 2);
    CHECK(endA == idB && endB == idA);
}

void testOcclusion() {
    Harness h;
    h.learn();
    h.scene.actors = {{80.0f, 50.0f, 10.0f, 22.0f, 220, 70, 60}};
    h.run(10);
    CHECK(h.result.tracks.size() == 1);
    const int id = h.result.tracks[0].id;
    const float area = h.result.tracks[0].area;
    h.scene.occluders = {{80.0f, 60.0f, 16.0f, 12.0f, 0, 0, 0}};
    h.run(14);
    CHECK(h.result.tracks.size() == 1);
    CHECK(h.result.tracks[0].id == id);
    CHECK(h.result.tracks[0].area < area);
    CHECK(h.result.signals.count == 1);

    h.scene.occluders = {{80.0f, 50.0f, 20.0f, 40.0f, 0, 0, 0}};
    h.run(10);
    CHECK(h.result.signals.count == 1);
    CHECK(h.result.tracks[0].state == TrackState::Coasting);
    h.scene.occluders.clear();
    h.run(6);
    CHECK(h.result.tracks.size() == 1);
    CHECK(h.result.tracks[0].id == id);
    CHECK(h.result.tracks[0].state == TrackState::Tracked);

    h.scene.occluders = {{80.0f, 50.0f, 20.0f, 40.0f, 0, 0, 0}};
    h.run(75);
    CHECK(h.result.signals.count == 0);
    h.scene.occluders.clear();
    h.run(8);
    CHECK(h.result.tracks.size() == 1);
    CHECK(h.result.tracks[0].id != id);
}

void testLightingChanges() {
    Harness h;
    h.learn();
    h.scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    h.run(12);
    const double base = iou(h.result.mask, h.truth);
    CHECK(base > 0.85);

    struct Step {
        float r;
        float g;
        float b;
        const char* name;
    };
    for (const Step& s : {Step{1.8f, 1.8f, 1.8f, "brighter"}, Step{0.5f, 0.5f, 0.5f, "darker"}, Step{1.3f, 1.0f, 0.8f, "warmer"}, Step{0.8f, 1.0f, 1.3f, "cooler"}}) {
        h.scene.gain[0] = s.r;
        h.scene.gain[1] = s.g;
        h.scene.gain[2] = s.b;
        h.step();
        const double first = falsePositive(h.result.mask, h.truth);
        h.run(3);
        const double later = falsePositive(h.result.mask, h.truth);
        const double quality = iou(h.result.mask, h.truth);
        std::printf("light change %s: false positives %.4f in the first picture, %.4f after four; IoU %.3f; persons %d\n", s.name, first, later, quality, h.result.signals.count);
        CHECK(first < 0.06);
        CHECK(later < 0.02);
        CHECK(quality > 0.75);
        CHECK(h.result.signals.count == 1);
    }

    h.scene.gain[0] = h.scene.gain[1] = h.scene.gain[2] = 1.0f;
    h.run(10);
    double worst = 0.0;
    double lowest = 1.0;
    for (int i = 0; i < 200; ++i) {
        const float g = 1.0f + 0.6f * static_cast<float>(i) / 200.0f;
        h.scene.gain[0] = h.scene.gain[1] = h.scene.gain[2] = g;
        h.step();
        worst = std::max(worst, falsePositive(h.result.mask, h.truth));
        lowest = std::min(lowest, iou(h.result.mask, h.truth));
    }
    std::printf("slow drift of the light by 60 percent: worst false positives %.4f, lowest IoU %.3f\n", worst, lowest);
    CHECK(worst < 0.02);
    CHECK(lowest > 0.75);

    Harness strobe;
    strobe.learn();
    strobe.scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    strobe.run(10);
    double strobeWorst = 0.0;
    int lost = 0;
    for (int i = 0; i < 30; ++i) {
        const float g = (i % 2) == 0 ? 2.2f : 1.0f;
        strobe.scene.gain[0] = strobe.scene.gain[1] = strobe.scene.gain[2] = g;
        strobe.step();
        strobeWorst = std::max(strobeWorst, falsePositive(strobe.result.mask, strobe.truth));
        lost += strobe.result.signals.count == 1 ? 0 : 1;
    }
    std::printf("strobe (light doubled every second picture): worst false positives %.4f, pictures without the person %d of 30\n", strobeWorst, lost);
    CHECK(strobeWorst < 0.2);
    CHECK(lost <= 3);
}

void testSceneChange() {
    Harness h;
    h.learn();
    h.scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    h.run(10);
    CHECK(h.result.signals.count == 1);
    h.scene.seed = 5;
    bool noted = false;
    bool learning = false;
    for (int i = 0; i < 8; ++i) {
        h.step();
        noted = noted || h.result.note.find("again") != std::string::npos;
        learning = learning || h.result.state == State::Learning;
        if (h.result.state == State::Learning) {
            CHECK(h.result.signals.count == 0);
            CHECK(h.result.foregroundPercent == 0);
        }
    }
    CHECK(noted);
    CHECK(learning);
    h.run(20);
    CHECK(h.result.state == State::Running);
    CHECK(h.result.signals.count == 0);
    CHECK(iou(h.result.mask, h.truth) < 0.1);

    h.scene.actors.clear();
    h.analyzer.learn();
    h.run(16);
    h.scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    h.run(12);
    CHECK(h.result.state == State::Running);
    CHECK(iou(h.result.mask, h.truth) > 0.8);
    CHECK(h.result.signals.count == 1);

    h.analyzer.learn();
    h.step();
    CHECK(h.result.state == State::Learning);
    h.scene.actors.clear();
    h.run(14);
    CHECK(h.result.state == State::Running);
    CHECK(h.result.signals.count == 0);
}

void testCameraLoss() {
    Harness h;
    Result lost;
    h.analyzer.sourceLost(0.1, lost);
    CHECK(lost.state == State::SourceLost);
    CHECK(lost.mask.coverage.empty());
    h.learn();
    h.scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    h.run(12);
    CHECK(h.result.signals.count == 1);
    const int id = h.result.tracks[0].id;
    CHECK(h.result.foregroundPercent > 5);
    int framesToEmpty = -1;
    int framesToQuiet = -1;
    for (int i = 0; i < 90; ++i) {
        h.analyzer.sourceLost(1.0 / kFps, h.result);
        CHECK(h.result.state == State::SourceLost);
        if (framesToEmpty < 0 && h.result.foregroundPercent == 0) {
            framesToEmpty = i + 1;
        }
        if (framesToQuiet < 0 && h.result.signals.presence < 0.05f) {
            framesToQuiet = i + 1;
        }
    }
    std::printf("camera lost: mask empty after %d pictures, signals quiet after %d, tracks left %zu\n", framesToEmpty, framesToQuiet, h.result.tracks.size());
    CHECK(framesToEmpty > 0 && framesToEmpty <= 25);
    CHECK(framesToQuiet > 0 && framesToQuiet <= 60);
    CHECK(h.result.tracks.empty());
    CHECK(!h.result.signals.present);
    h.run(8);
    CHECK(h.result.state == State::Running);
    CHECK(h.result.signals.count == 1);
    CHECK(h.result.tracks[0].id != id);
    CHECK(iou(h.result.mask, h.truth) > 0.8);
}

void testSignals() {
    Harness h;
    h.learn();
    h.scene.actors = {{40.0f, 27.0f, 8.0f, 16.0f, 220, 70, 60}};
    h.run(25);
    const Signals s = h.result.signals;
    CHECK(s.present && s.count == 1);
    CHECK(std::abs(s.x - 0.25f) < 0.04f);
    CHECK(std::abs(s.y - 0.70f) < 0.05f);
    CHECK(std::abs(s.size - 32.0f / kH) < 0.06f);
    CHECK(s.motion < 0.1f);
    CHECK(s.presence > 0.95f);
    CHECK(std::abs(s.countValue - 1.0f / kMaxTracks) < 0.001f);

    float motion = 0.0f;
    for (int i = 0; i < 40; ++i) {
        h.scene.actors[0].cx += 2.4f;
        h.step();
        motion = std::max(motion, h.result.signals.motion);
    }
    std::printf("person moving at 0.225 picture widths per second: motion signal %.3f, x %.3f\n", motion, h.result.signals.x);
    CHECK(motion > 0.15f && motion < 0.9f);
    CHECK(h.result.signals.x > 0.6f);

    const float lastX = h.result.signals.x;
    const float lastY = h.result.signals.y;
    h.scene.actors.clear();
    h.run(60);
    CHECK(h.result.signals.presence < 0.02f);
    CHECK(h.result.signals.motion < 0.02f);
    CHECK(std::abs(h.result.signals.x - lastX) < 0.05f);
    CHECK(std::abs(h.result.signals.y - lastY) < 0.05f);

    Signals custom;
    custom.presence = 0.5f;
    custom.x = 0.25f;
    custom.y = 0.75f;
    custom.size = 2.0f;
    custom.motion = -1.0f;
    custom.countValue = 0.375f;
    CHECK(signalValue(custom, 0) == 0.5f);
    CHECK(signalValue(custom, 1) == 0.25f);
    CHECK(signalValue(custom, 2) == 0.75f);
    CHECK(signalValue(custom, 3) == 1.0f);
    CHECK(signalValue(custom, 4) == 0.0f);
    CHECK(signalValue(custom, 5) == 0.375f);
    CHECK(signalValue(custom, -1) == 0.0f);
    CHECK(signalValue(custom, kSignalCount) == 0.0f);
    for (int i = 0; i < kSignalCount; ++i) {
        CHECK(std::string(signalName(i)) == nvj::audioBandName(nvj::BandPersonPresent + i));
    }
    CHECK(std::string(signalName(kSignalCount)).empty());
    CHECK(nvj::audioBandFromName("Person X") == nvj::BandPersonX);
    CHECK(nvj::BandPersonPresent == nvj::BandPlugin8 + 1);
    CHECK(nvj::BandPersonCount + 1 == nvj::AudioBandCount);
    nvj::AudioFeatures audio;
    nvj::Modulators modulators;
    for (int band = nvj::BandPersonPresent; band <= nvj::BandPersonCount; ++band) {
        CHECK(nvj::modulationSource(band, audio, modulators, 1.3, 4) == 0.0f);
    }
}

class OracleBackend final : public Backend {
public:
    explicit OracleBackend(const std::vector<std::uint8_t>* truth)
        : truth_(truth) {}

    Provenance provenance() const override {
        Provenance p;
        p.id = "oracle";
        p.title = "ground truth for tests";
        p.licence = "test only";
        p.origin = "tests/test_person.cpp";
        return p;
    }

    bool segment(const Picture& picture, const Params&, std::vector<std::uint8_t>& score, std::string& problem) override {
        ++calls;
        if (fail) {
            problem = "the oracle was told to fail";
            return false;
        }
        score.assign(static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height), 0);
        for (std::size_t i = 0; i < score.size() && i < truth_->size(); ++i) {
            score[i] = (*truth_)[i] != 0 ? 255 : 0;
        }
        if (wrongSize) {
            score.resize(score.size() / 2);
        }
        return true;
    }

    void learn() override { ++learned; }
    void reset() override {}
    bool learning() const override { return false; }
    bool sceneChanged() const override { return false; }

    int calls = 0;
    int learned = 0;
    bool fail = false;
    bool wrongSize = false;

private:
    const std::vector<std::uint8_t>* truth_;
};

void testBackendInterface() {
    const Provenance info = makeBackend(Mode::Difference)->provenance();
    CHECK(info.id == "difference");
    CHECK(!info.needsModel);
    CHECK(!info.usesNetwork);
    CHECK(info.deterministic);
    CHECK(!info.licence.empty() && !info.origin.empty());
    CHECK(makeBackend(Mode::Off) == nullptr);

    Scene scene;
    std::vector<std::uint8_t> truth;
    auto oracle = std::make_unique<OracleBackend>(&truth);
    OracleBackend* raw = oracle.get();
    Analyzer analyzer(std::move(oracle));
    CHECK(analyzer.provenance().id == "oracle");
    Settings s;
    s.mode = Mode::Difference;
    s.softness = 0;
    s.stability = 0;
    analyzer.configure(s);
    scene.actors = {{80.0f, 45.0f, 14.0f, 26.0f, 220, 70, 60}, {30.0f, 50.0f, 8.0f, 16.0f, 40, 160, 210}};
    Result result;
    for (int i = 0; i < 8; ++i) {
        const Picture picture = scene.render(i / kFps, &truth);
        CHECK(analyzer.process(picture, result));
    }
    CHECK(raw->calls == 8);
    CHECK(iou(result.mask, truth) > 0.95);
    CHECK(result.signals.count == 2);
    CHECK(result.state == State::Running);
    analyzer.learn();
    CHECK(raw->learned == 1);

    raw->fail = true;
    const Picture picture = scene.render(9.0, &truth);
    CHECK(!analyzer.process(picture, result));
    CHECK(result.state == State::Failed);
    CHECK(result.note.find("told to fail") != std::string::npos);
    raw->fail = false;
    raw->wrongSize = true;
    CHECK(analyzer.process(picture, result));
    CHECK(result.mask.coverage.empty());
    CHECK(result.signals.count == 0 || result.tracks.size() <= static_cast<std::size_t>(kMaxTracks));

    Analyzer none(nullptr);
    CHECK(none.provenance().id.empty());
    CHECK(!none.process(picture, result));
    CHECK(result.state == State::Failed);
    CHECK(!result.note.empty());
    none.learn();
    none.reset();
    none.sourceLost(0.1, result);
}

Governor::Decision settle(Governor& g, double ms) {
    for (int i = 0; i < 20; ++i) {
        const Governor::Decision d = g.record(ms);
        if (d != Governor::Decision::Keep) {
            return d;
        }
    }
    return Governor::Decision::Keep;
}

void testGovernor() {
    Governor g(Tier::Fine, 20);
    CHECK(g.tier() == Tier::Fine && g.rate() == 20);
    CHECK(std::abs(g.budgetMs() - kCpuShare * 1000.0 / 20.0) < 1.0e-9);
    for (int i = 0; i < 12; ++i) {
        CHECK(g.record(1.0) == Governor::Decision::Keep);
    }
    CHECK(g.tier() == Tier::Fine);
    CHECK(settle(g, 100.0) == Governor::Decision::LowerTier);
    CHECK(g.tier() == Tier::Balanced && g.rate() == 20);
    CHECK(settle(g, 100.0) == Governor::Decision::LowerTier);
    CHECK(g.tier() == Tier::Fast && g.rate() == 20);
    CHECK(settle(g, 100.0) == Governor::Decision::LowerRate);
    CHECK(g.rate() == 10 && g.tier() == Tier::Fast);
    CHECK(settle(g, 500.0) == Governor::Decision::LowerRate);
    CHECK(g.rate() == 5);
    CHECK(settle(g, 500.0) == Governor::Decision::LowerRate);
    CHECK(g.rate() == kMinRate);
    CHECK(!g.disabled());
    CHECK(settle(g, 5000.0) == Governor::Decision::Disable);
    CHECK(g.disabled());
    CHECK(g.record(0.1) == Governor::Decision::Keep);
    CHECK(g.lowered() >= 5);
    CHECK(g.p95() >= g.average() * 0.5);

    g.request(Tier::Balanced, 15);
    CHECK(!g.disabled());
    CHECK(g.tier() == Tier::Balanced && g.rate() == 15);
    for (int i = 0; i < 6; ++i) {
        g.record(100.0);
    }
    CHECK(g.tier() == Tier::Fast);
    int raised = 0;
    for (int i = 0; i < 400; ++i) {
        const Governor::Decision d = g.record(0.2);
        raised += (d == Governor::Decision::RaiseTier || d == Governor::Decision::RaiseRate) ? 1 : 0;
    }
    CHECK(raised >= 1);
    CHECK(g.tier() == Tier::Balanced);
    CHECK(g.rate() == 15);

    Governor bad(Tier::Balanced, 15);
    for (int i = 0; i < 20; ++i) {
        bad.record(std::nan(""));
        bad.record(-5.0);
    }
    CHECK(bad.tier() == Tier::Balanced);
    CHECK(bad.average() == 0.0);
    bad.reset();
    CHECK(bad.average() == 0.0);
    Governor clamped(Tier::Fast, 9999);
    CHECK(clamped.rate() == kMaxRate);
}

void testRobustness() {
    Analyzer analyzer(makeBackend(Mode::Difference));
    Settings s;
    s.mode = Mode::Difference;
    analyzer.configure(s);
    Result result;
    Picture broken;
    broken.width = 100000;
    broken.height = 100000;
    CHECK(!analyzer.process(broken, result));
    broken.width = 160;
    broken.height = 90;
    broken.rgb.resize(100);
    CHECK(!analyzer.process(broken, result));
    CHECK(result.state == State::Failed);
    Picture huge;
    huge.width = 2000;
    huge.height = 2000;
    huge.rgb.assign(static_cast<std::size_t>(huge.width) * huge.height * 3u, 0);
    CHECK(!analyzer.process(huge, result));

    Rng rng;
    for (int w : {1, 2, 3, 17, 64}) {
        for (int h : {1, 2, 5, 33}) {
            Analyzer a(makeBackend(Mode::Difference));
            a.configure(s);
            for (int i = 0; i < 20; ++i) {
                Picture p;
                p.width = w;
                p.height = h;
                p.time = i * 0.05;
                p.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);
                for (std::uint8_t& v : p.rgb) {
                    v = static_cast<std::uint8_t>(rng.next());
                }
                CHECK(a.process(p, result));
                CHECK(result.mask.coverage.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
                CHECK(result.tracks.size() <= static_cast<std::size_t>(kMaxTracks));
            }
        }
    }

    Harness h;
    h.learn();
    h.scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    h.run(10);
    for (int i = 0; i < 20; ++i) {
        Picture p = h.scene.render(1.0, nullptr);
        p.time = (i % 3 == 0) ? std::nan("") : (i % 3 == 1 ? -4.0 : 1.0);
        CHECK(h.analyzer.process(p, h.result));
        CHECK(h.result.mask.coverage.size() == static_cast<std::size_t>(kW) * kH);
    }
    h.scene.width = 120;
    h.scene.height = 70;
    h.run(20);
    CHECK(h.result.mask.width == 120 && h.result.mask.height == 70);
    CHECK(h.lastOk);
    h.scene.width = kW;
    h.scene.height = kH;
    h.run(20);
    CHECK(h.result.mask.width == kW);

    Picture noisy;
    noisy.width = kW;
    noisy.height = kH;
    noisy.rgb.resize(static_cast<std::size_t>(kW) * kH * 3u);
    for (int i = 0; i < 300; ++i) {
        noisy.time = 100.0 + i / kFps;
        for (std::uint8_t& v : noisy.rgb) {
            v = static_cast<std::uint8_t>(rng.next());
        }
        CHECK(analyzer.process(noisy, result));
        CHECK(result.tracks.size() <= static_cast<std::size_t>(kMaxTracks));
        for (const Track& t : result.tracks) {
            CHECK(std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.vx) && std::isfinite(t.vy));
        }
    }
    CHECK(result.signals.count >= 0 && result.signals.count <= kMaxTracks);
}

bool sameResult(const Result& a, const Result& b) {
    return a.mask.coverage == b.mask.coverage && a.signals.count == b.signals.count && a.signals.x == b.signals.x && a.signals.y == b.signals.y && a.tracks.size() == b.tracks.size() && a.state == b.state;
}

void testDeterminism() {
    Harness a;
    Harness b;
    for (Harness* h : {&a, &b}) {
        h->scene.noise = 3.0f;
        h->learn();
        h->scene.actors = {{40.0f, 50.0f, 9.0f, 18.0f, 220, 70, 60}, {110.0f, 45.0f, 12.0f, 24.0f, 40, 160, 210}};
    }
    bool same = true;
    for (int i = 0; i < 40; ++i) {
        for (Harness* h : {&a, &b}) {
            h->scene.actors[0].cx += 1.0f;
            h->step();
        }
        same = same && sameResult(a.result, b.result);
    }
    CHECK(same);
}

void testWorker() {
    PersonWorker worker;
    CHECK(worker.latest() == nullptr);
    Settings s;
    s.mode = Mode::Difference;
    s.rate = 30;
    worker.configure(s);
    Scene scene;
    unsigned long long generation = 0;
    for (int i = 0; i < 14; ++i) {
        CHECK(worker.submit(scene.render(i / kFps, nullptr)));
        CHECK(worker.waitForGeneration(++generation, 5000));
    }
    std::shared_ptr<const Result> first = worker.latest();
    CHECK(first != nullptr);
    CHECK(first->state == State::Running);
    CHECK(first->mask.width == kW && first->mask.height == kH);
    CHECK(worker.stats().provenance == "difference");
    scene.actors = {{80.0f, 50.0f, 12.0f, 24.0f, 220, 70, 60}};
    std::vector<std::uint8_t> truth;
    for (int i = 14; i < 30; ++i) {
        CHECK(worker.submit(scene.render(i / kFps, &truth)));
        CHECK(worker.waitForGeneration(++generation, 5000));
    }
    std::shared_ptr<const Result> shown = worker.latest();
    CHECK(shown->signals.count == 1);
    CHECK(iou(shown->mask, truth) > 0.8);
    CHECK(shown->milliseconds > 0.0);
    PersonStats stats = worker.stats();
    CHECK(stats.completed == generation);
    CHECK(stats.submitted == generation);
    CHECK(stats.failed == 0);
    CHECK(stats.lastMs > 0.0 && stats.averageMs > 0.0 && stats.p95Ms >= stats.averageMs * 0.5);
    CHECK(stats.width == kW && stats.height == kH);
    CHECK(stats.requestedRate == 30 && stats.requestedTier == Tier::Balanced);
    CHECK(!stats.disabled);
    CHECK(worker.idle());

    worker.lost(1.0 / kFps);
    CHECK(worker.waitForGeneration(++generation, 5000));
    CHECK(worker.latest()->state == State::SourceLost);
    for (int i = 0; i < 40; ++i) {
        worker.lost(1.0 / kFps);
        worker.waitForGeneration(++generation, 5000);
    }
    CHECK(worker.latest()->tracks.empty());
    CHECK(worker.latest()->signals.presence < 0.05f);
    worker.lost(-3.0);
    worker.lost(std::nan(""));

    worker.learn();
    CHECK(worker.submit(scene.render(40.0, &truth)));
    CHECK(worker.waitForGeneration(generation + 1, 5000));
    CHECK(worker.latest()->state == State::Learning);

    const unsigned long long before = worker.stats().submitted;
    const unsigned long long beforeDone = worker.stats().completed;
    for (int i = 0; i < 200; ++i) {
        worker.submit(scene.render(50.0 + i / kFps, nullptr));
    }
    const auto begin = std::chrono::steady_clock::now();
    while (!worker.idle() && elapsedMs(begin) < 10000.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    stats = worker.stats();
    std::printf("worker: %llu pictures submitted, %llu analysed, %llu replaced while busy\n", stats.submitted - before, stats.completed - beforeDone, stats.superseded);
    CHECK(worker.idle());
    CHECK(stats.completed - beforeDone >= 1);
    CHECK(stats.completed - beforeDone + stats.superseded >= stats.submitted - before);

    Settings quick = s;
    quick.tier = Tier::Fast;
    quick.rate = 6;
    worker.configure(quick);
    CHECK(worker.submit(scene.render(70.0, nullptr)));
    const auto wait = std::chrono::steady_clock::now();
    while (worker.stats().requestedRate != 6 && elapsedMs(wait) < 5000.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(worker.stats().requestedRate == 6);
    CHECK(worker.stats().requestedTier == Tier::Fast);
    CHECK(worker.tier() == Tier::Fast && worker.rate() == 6);

    PersonWorker off;
    Settings none;
    off.configure(none);
    CHECK(off.submit(scene.render(1.0, nullptr)));
    const auto offWait = std::chrono::steady_clock::now();
    while (!off.idle() && elapsedMs(offWait) < 5000.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(off.idle());

    const auto stopBegin = std::chrono::steady_clock::now();
    {
        PersonWorker busy;
        busy.configure(s);
        for (int i = 0; i < 50; ++i) {
            busy.submit(scene.render(i / kFps, nullptr));
        }
    }
    const double stopMs = elapsedMs(stopBegin);
    std::printf("worker: destroyed with 50 pictures waiting after %.1f ms\n", stopMs);
    CHECK(stopMs < 3000.0);
    {
        PersonWorker unused;
    }
}

void testPerformance() {
    for (int t = 0; t < kTierCount; ++t) {
        const Tier tier = static_cast<Tier>(t);
        Settings s;
        s.tier = tier;
        Harness h(s);
        int w = 0;
        int ht = 0;
        analysisSize(1920, 1080, tier, w, ht);
        h.scene.width = w;
        h.scene.height = ht;
        h.scene.noise = 2.0f;
        h.learn();
        const float scale = static_cast<float>(w) / kW;
        h.scene.actors = {{w * 0.3f, ht * 0.55f, 9.0f * scale, 18.0f * scale, 220, 70, 60}, {w * 0.7f, ht * 0.5f, 12.0f * scale, 24.0f * scale, 40, 160, 210}};
        std::vector<Picture> pictures;
        for (int i = 0; i < 60; ++i) {
            h.scene.actors[0].cx += 0.4f * scale;
            pictures.push_back(h.scene.render((14 + i) / kFps, nullptr));
        }
        std::vector<double> times;
        Result result;
        for (int round = 0; round < 5; ++round) {
            for (const Picture& p : pictures) {
                const auto begin = std::chrono::steady_clock::now();
                h.analyzer.process(p, result);
                times.push_back(elapsedMs(begin));
            }
        }
        std::sort(times.begin(), times.end());
        const double median = times[times.size() / 2];
        const double p95 = times[times.size() * 95 / 100];
        std::printf("analysis %dx%d (%s): median %.3f ms, p95 %.3f ms, share of one core at 15 Hz %.2f percent\n", w, ht, tierName(tier), median, p95, median * 15.0 / 10.0);
        CHECK(median < 100.0);
    }
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void setDataDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

void testSettingsJson() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_person_settings";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    setDataDirectory(dir);

    nvj::Settings defaults;
    CHECK(defaults.personMode == "off");
    CHECK(defaults.personInput == "camera1");
    CHECK(defaults.personQuality == "balanced");
    CHECK(defaults.personRate == 15);
    CHECK(defaults.personSensitivity == 50 && defaults.personSoftness == 50 && defaults.personStability == 50);
    CHECK(nvj::saveSettings(defaults));
    const nlohmann::json plain = nlohmann::json::parse(readText(dir / "settings.json"));
    for (const char* key : {"personMode", "personInput", "personQuality", "personRate", "personSensitivity", "personSoftness", "personStability"}) {
        CHECK(!plain.contains(key));
    }

    nvj::Settings changed;
    changed.personMode = "difference";
    changed.personInput = "ndi3";
    changed.personQuality = "fine";
    changed.personRate = 24;
    changed.personSensitivity = 70;
    changed.personSoftness = 10;
    changed.personStability = 90;
    CHECK(nvj::saveSettings(changed));
    nvj::Settings loaded;
    CHECK(nvj::loadSettings(loaded));
    CHECK(loaded.personMode == "difference");
    CHECK(loaded.personInput == "ndi3");
    CHECK(loaded.personQuality == "fine");
    CHECK(loaded.personRate == 24);
    CHECK(loaded.personSensitivity == 70 && loaded.personSoftness == 10 && loaded.personStability == 90);

    {
        std::ofstream out(dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << R"({"personMode":"magic","personInput":"webcam9","personQuality":4,"personRate":9999,"personSensitivity":-30,"personSoftness":500,"personStability":"high"})";
    }
    nvj::Settings repaired;
    CHECK(nvj::loadSettings(repaired));
    CHECK(repaired.personMode == "off");
    CHECK(repaired.personInput == "camera1");
    CHECK(repaired.personQuality == "balanced");
    CHECK(repaired.personRate == kMaxRate);
    CHECK(repaired.personSensitivity == 0);
    CHECK(repaired.personSoftness == 100);
    CHECK(repaired.personStability == 50);

    {
        std::ofstream out(dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << R"({"personMode":1,"personInput":7,"personRate":"fast","personSensitivity":12.5})";
    }
    nvj::Settings wrongTypes;
    CHECK(nvj::loadSettings(wrongTypes));
    CHECK(wrongTypes.personMode == "off");
    CHECK(wrongTypes.personInput == "camera1");
    CHECK(wrongTypes.personRate == 15);
    CHECK(wrongTypes.personSensitivity == 50);

#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", "");
#else
    unsetenv("NYREXVJ_DATA_DIR");
#endif
    std::filesystem::remove_all(dir, ec);
}

}

int main() {
    testNamesAndLimits();
    testAnalysisSize();
    testReduce();
    testBlobs();
    testTracker();
    testStaticSceneHasNoForeground();
    testDetectionAtAllTiers();
    testSensitivity();
    testAlphaEdges();
    testTemporalStability();
    testMultiplePersons();
    testCrossingPersons();
    testOcclusion();
    testLightingChanges();
    testSceneChange();
    testCameraLoss();
    testSignals();
    testBackendInterface();
    testGovernor();
    testRobustness();
    testDeterminism();
    testWorker();
    testPerformance();
    testSettingsJson();
    return testing::finish();
}

#include "check.hpp"
#include "control/timeline_system.hpp"
#include "core/log.hpp"
#include "ltc_signal.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

struct LaneHost final : nvj::TimelineHost {
    std::map<std::string, float> values;
    std::map<std::string, nvj::LaneKind> kinds;
    std::vector<nvj::Cue> fired;
    int writes = 0;
    int refusedWrites = 0;

    nvj::LaneKind laneKind(const std::string& target) const override {
        const auto it = kinds.find(target);
        return it == kinds.end() ? nvj::LaneKind::Refused : it->second;
    }
    bool read(const std::string& target, float& value) const override {
        const auto it = values.find(target);
        if (it == values.end()) {
            return false;
        }
        value = it->second;
        return true;
    }
    bool write(const std::string& target, float value) override {
        if (laneKind(target) == nvj::LaneKind::Refused) {
            ++refusedWrites;
            return false;
        }
        values[target] = value;
        ++writes;
        return true;
    }
    void fire(const nvj::Cue& cue) override { fired.push_back(cue); }

    int count(double seconds) const {
        int total = 0;
        for (const nvj::Cue& cue : fired) {
            if (cue.seconds == seconds) {
                ++total;
            }
        }
        return total;
    }
};

struct Sim {
    nvj::TimelineRunner runner;
    nvj::ShowTimeline timeline;
    std::vector<nvj::Cue> cues;
    LaneHost host;
    nvj::TimelineInputs inputs;
    nvj::TimelineConfig config;
    double clock = 500.0;
    double dt = 1.0 / 60.0;

    Sim() {
        config.enabled = true;
        runner.configure(config);
        inputs.clock = clock;
        host.kinds["master/level"] = nvj::LaneKind::Continuous;
        host.kinds["layer/1/opacity"] = nvj::LaneKind::Continuous;
        host.kinds["master/blackout"] = nvj::LaneKind::Toggle;
        host.values["master/level"] = 1.0f;
        host.values["layer/1/opacity"] = 1.0f;
        host.values["master/blackout"] = 0.0f;
    }
    void apply() { runner.configure(config); }
    void step() {
        clock += dt;
        inputs.clock = clock;
        runner.update(inputs, timeline, cues, host);
    }
    void run(double seconds) {
        const int steps = static_cast<int>(std::lround(seconds / dt));
        for (int i = 0; i < steps; ++i) {
            step();
        }
    }
    void timecode(double seconds) {
        inputs.timecodeValid = true;
        inputs.timecodeSeconds = seconds;
    }
    void runTimecode(double seconds) {
        const int steps = static_cast<int>(std::lround(seconds / dt));
        for (int i = 0; i < steps; ++i) {
            inputs.timecodeSeconds += dt;
            step();
        }
    }
    int lane(const std::string& target) {
        return nvj::addLane(timeline, target);
    }
    void key(int laneIndex, double seconds, float value, nvj::KeyShape shape = nvj::KeyShape::Linear) {
        nvj::Keyframe k;
        k.seconds = seconds;
        k.value = value;
        k.shape = shape;
        nvj::insertKey(timeline, static_cast<size_t>(laneIndex), k);
    }
    void cue(double seconds, const std::string& target = "deck/next", float value = 1.0f) {
        nvj::Cue c;
        c.seconds = seconds;
        c.target = target;
        c.value = value;
        cues.push_back(c);
        nvj::sortCues(cues);
    }
};

std::uint32_t rngState = 12345u;

std::uint32_t nextRandom() {
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

double randomIn(double low, double high) {
    return low + (high - low) * (static_cast<double>(nextRandom() & 0xFFFFu) / 65535.0);
}

void testEvaluate() {
    std::vector<nvj::Keyframe> none;
    CHECK(nvj::evaluateKeys(none, 5.0, false) == 0.0f);

    std::vector<nvj::Keyframe> one = {{3.0, 0.7f, nvj::KeyShape::Linear}};
    CHECK(near(nvj::evaluateKeys(one, 0.0, false), 0.7, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(one, 100.0, false), 0.7, 1.0e-6));

    std::vector<nvj::Keyframe> ramp = {{0.0, 0.0f, nvj::KeyShape::Linear}, {10.0, 1.0f, nvj::KeyShape::Linear}};
    CHECK(near(nvj::evaluateKeys(ramp, -5.0, false), 0.0, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(ramp, 2.5, false), 0.25, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(ramp, 5.0, false), 0.5, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(ramp, 10.0, false), 1.0, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(ramp, 99.0, false), 1.0, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(ramp, 7.0, true), 0.0, 1.0e-6));

    std::vector<nvj::Keyframe> hold = {{0.0, 0.0f, nvj::KeyShape::Hold}, {10.0, 1.0f, nvj::KeyShape::Linear}};
    CHECK(near(nvj::evaluateKeys(hold, 9.999, false), 0.0, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(hold, 10.0, false), 1.0, 1.0e-6));

    std::vector<nvj::Keyframe> smooth = {{0.0, 0.0f, nvj::KeyShape::Smooth}, {10.0, 1.0f, nvj::KeyShape::Linear}};
    CHECK(near(nvj::evaluateKeys(smooth, 5.0, false), 0.5, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(smooth, 2.5, false), 0.15625, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(smooth, 7.5, false), 0.84375, 1.0e-6));

    std::vector<nvj::Keyframe> step = {{5.0, 0.0f, nvj::KeyShape::Linear}, {5.0, 1.0f, nvj::KeyShape::Linear}};
    CHECK(near(nvj::evaluateKeys(step, 4.999, false), 0.0, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(step, 5.0, false), 1.0, 1.0e-6));
    CHECK(near(nvj::evaluateKeys(ramp, std::nan(""), false), 0.0, 1.0e-6));
    CHECK(nvj::keySegment(ramp, -1.0) == 0);
    CHECK(nvj::keySegment(ramp, 0.0) == 1);
    CHECK(nvj::keySegment(ramp, 5.0) == 1);
    CHECK(nvj::keySegment(ramp, 10.0) == 2);
    CHECK(nvj::keySegment(ramp, std::numeric_limits<double>::infinity()) == 0);
}

void testEdits() {
    nvj::ShowTimeline tl;
    CHECK(nvj::addLane(tl, "") == -1);
    CHECK(nvj::addLane(tl, std::string(200, 'x')) == -1);
    const int a = nvj::addLane(tl, "master/level");
    CHECK(a == 0);
    CHECK(nvj::addLane(tl, "master/level") == 0);
    CHECK(tl.lanes.size() == 1);
    CHECK(nvj::findLane(tl, "master/level") == 0);
    CHECK(nvj::findLane(tl, "nothing") == -1);

    CHECK(nvj::insertKey(tl, 0, {5.0, 0.5f, nvj::KeyShape::Linear}) == 0);
    CHECK(nvj::insertKey(tl, 0, {1.0, 0.1f, nvj::KeyShape::Linear}) == 0);
    CHECK(nvj::insertKey(tl, 0, {9.0, 0.9f, nvj::KeyShape::Linear}) == 2);
    CHECK(nvj::insertKey(tl, 0, {5.0, 0.6f, nvj::KeyShape::Linear}) == 2);
    CHECK(tl.lanes[0].keys.size() == 4);
    CHECK(tl.lanes[0].keys[2].value == 0.6f);
    CHECK(nvj::insertKey(tl, 0, {-5.0, 2.0f, nvj::KeyShape::Linear}) == 0);
    CHECK(tl.lanes[0].keys[0].seconds == 0.0 && tl.lanes[0].keys[0].value == 1.0f);
    CHECK(nvj::insertKey(tl, 0, {1.0e9, -3.0f, nvj::KeyShape::Linear}) == 5);
    CHECK(tl.lanes[0].keys.back().seconds == nvj::kMaxTimelineSeconds && tl.lanes[0].keys.back().value == 0.0f);
    CHECK(nvj::insertKey(tl, 0, {std::nan(""), 0.0f, nvj::KeyShape::Linear}) == -1);
    CHECK(nvj::insertKey(tl, 0, {1.0, std::nanf(""), nvj::KeyShape::Linear}) == -1);
    CHECK(nvj::insertKey(tl, 7, {1.0, 0.0f, nvj::KeyShape::Linear}) == -1);

    const int moved = nvj::moveKey(tl, 0, 0, 20.0, 0.25f);
    CHECK(moved == 4);
    CHECK(tl.lanes[0].keys[static_cast<size_t>(moved)].seconds == 20.0);
    for (size_t i = 1; i < tl.lanes[0].keys.size(); ++i) {
        CHECK(tl.lanes[0].keys[i - 1].seconds <= tl.lanes[0].keys[i].seconds);
    }
    CHECK(nvj::moveKey(tl, 0, 99, 1.0, 0.0f) == -1);
    CHECK(nvj::setKeyShape(tl, 0, 1, nvj::KeyShape::Smooth));
    CHECK(tl.lanes[0].keys[1].shape == nvj::KeyShape::Smooth);
    CHECK(!nvj::setKeyShape(tl, 0, 99, nvj::KeyShape::Hold));
    CHECK(nvj::removeKey(tl, 0, 0));
    CHECK(!nvj::removeKey(tl, 0, 99));
    CHECK(!nvj::removeKey(tl, 3, 0));

    nvj::setLoop(tl, true, 8.0, 2.0);
    CHECK(tl.loop.start == 2.0 && tl.loop.end == 8.0 && tl.loop.enabled);
    CHECK(tl.loop.usable());
    nvj::setLoop(tl, true, 2.0, 2.01);
    CHECK(!tl.loop.usable());
    nvj::setLoop(tl, true, std::nan(""), -4.0);
    CHECK(tl.loop.start == 0.0 && tl.loop.end == 0.0);

    CHECK(nvj::removeLane(tl, 0));
    CHECK(!nvj::removeLane(tl, 0));

    nvj::ShowTimeline full;
    for (size_t i = 0; i < nvj::kMaxTimelineLanes + 5; ++i) {
        const int index = nvj::addLane(full, "lane/" + std::to_string(i));
        CHECK((i < nvj::kMaxTimelineLanes) == (index >= 0));
    }
    CHECK(full.lanes.size() == nvj::kMaxTimelineLanes);
    for (size_t i = 0; i < nvj::kMaxLaneKeys + 10; ++i) {
        const int index = nvj::insertKey(full, 0, {static_cast<double>(i), 0.5f, nvj::KeyShape::Linear});
        CHECK((i < nvj::kMaxLaneKeys) == (index >= 0));
    }
    CHECK(full.lanes[0].keys.size() == nvj::kMaxLaneKeys);
    size_t added = 0;
    for (size_t lane = 1; lane < full.lanes.size(); ++lane) {
        for (size_t i = 0; i < nvj::kMaxLaneKeys; ++i) {
            if (nvj::insertKey(full, lane, {static_cast<double>(i), 0.5f, nvj::KeyShape::Linear}) >= 0) {
                ++added;
            }
        }
    }
    CHECK(nvj::timelineKeyCount(full) == nvj::kMaxTimelineKeys);
    CHECK(added + nvj::kMaxLaneKeys == nvj::kMaxTimelineKeys);
}

nvj::ShowTimeline sampleTimeline() {
    nvj::ShowTimeline tl;
    const int a = nvj::addLane(tl, "master/level");
    nvj::insertKey(tl, static_cast<size_t>(a), {0.0, 0.0f, nvj::KeyShape::Linear});
    nvj::insertKey(tl, static_cast<size_t>(a), {12.345678, 0.4f, nvj::KeyShape::Smooth});
    nvj::insertKey(tl, static_cast<size_t>(a), {60.0, 1.0f, nvj::KeyShape::Hold});
    const int b = nvj::addLane(tl, "master/blackout");
    nvj::insertKey(tl, static_cast<size_t>(b), {5.0, 1.0f, nvj::KeyShape::Hold});
    tl.lanes[static_cast<size_t>(b)].muted = true;
    nvj::addLane(tl, "future/target/unknown");
    nvj::setLoop(tl, true, 4.0, 20.5);
    return tl;
}

void testJson() {
    const nvj::ShowTimeline original = sampleTimeline();
    const std::string text = nvj::timelineToJson(original);
    nvj::ShowTimeline back;
    CHECK(nvj::timelineFromJson(text, back));
    nvj::TimelineDoc x;
    nvj::TimelineDoc y;
    x.timeline = original;
    y.timeline = back;
    CHECK(nvj::sameDoc(x, y));
    CHECK(back.lanes.size() == 3);
    CHECK(back.lanes[2].target == "future/target/unknown");
    CHECK(back.lanes[1].muted);
    CHECK(back.loop.enabled && back.loop.start == 4.0 && back.loop.end == 20.5);
    CHECK(nvj::timelineToJson(back) == text);

    nvj::ShowTimeline keep = sampleTimeline();
    CHECK(!nvj::timelineFromJson("", keep));
    CHECK(!nvj::timelineFromJson("not json", keep));
    CHECK(!nvj::timelineFromJson("[]", keep));
    CHECK(!nvj::timelineFromJson("{}", keep));
    CHECK(!nvj::timelineFromJson("{\"lanes\": 5}", keep));
    CHECK(keep.lanes.size() == 3);
    CHECK(nvj::timelineFromJson("{\"lanes\": []}", keep));
    CHECK(keep.lanes.empty());

    nvj::ShowTimeline parsed;
    const std::string messy = R"({
        "version": 1,
        "loop": {"enabled": true, "start": 9, "end": 3},
        "lanes": [
            5,
            {"target": 7},
            {"target": "", "keys": []},
            {"target": "master/level", "muted": "yes", "keys": [
                {"seconds": 30, "value": 2.5},
                {"seconds": 10, "value": -1, "shape": "smooth"},
                {"seconds": -5, "value": 0.5},
                {"seconds": "x", "value": 0.5},
                {"seconds": 20},
                {"seconds": 100000, "value": 0.5},
                "junk",
                {"seconds": 10, "value": 0.2, "shape": "bogus"}
            ]},
            {"target": "master/level", "keys": [{"seconds": 1, "value": 1}]},
            {"target": "layer/1/opacity", "keys": "none"}
        ],
        "extra": {"anything": [1, 2, 3]}
    })";
    CHECK(nvj::timelineFromJson(messy, parsed));
    CHECK(parsed.lanes.size() == 2);
    CHECK(parsed.lanes[0].target == "master/level");
    CHECK(!parsed.lanes[0].muted);
    CHECK(parsed.lanes[0].keys.size() == 3);
    CHECK(parsed.lanes[0].keys[0].seconds == 10.0 && parsed.lanes[0].keys[0].value == 0.0f && parsed.lanes[0].keys[0].shape == nvj::KeyShape::Smooth);
    CHECK(parsed.lanes[0].keys[1].seconds == 10.0 && parsed.lanes[0].keys[1].shape == nvj::KeyShape::Linear);
    CHECK(parsed.lanes[0].keys[2].seconds == 30.0 && parsed.lanes[0].keys[2].value == 1.0f);
    CHECK(parsed.lanes[1].keys.empty());
    CHECK(parsed.loop.enabled && parsed.loop.start == 3.0 && parsed.loop.end == 9.0);

    std::string many = "{\"lanes\": [";
    for (int lane = 0; lane < 40; ++lane) {
        many += (lane ? "," : "");
        many += "{\"target\": \"t" + std::to_string(lane) + "\", \"keys\": [";
        for (int k = 0; k < 3000; ++k) {
            many += (k ? "," : "");
            many += "{\"seconds\": " + std::to_string(k) + ", \"value\": 0.5}";
        }
        many += "]}";
    }
    many += "]}";
    CHECK(many.size() < nvj::kMaxTimelineText);
    nvj::ShowTimeline limited;
    CHECK(nvj::timelineFromJson(many, limited));
    CHECK(limited.lanes.size() == nvj::kMaxTimelineLanes);
    CHECK(nvj::timelineKeyCount(limited) == nvj::kMaxTimelineKeys);
    for (const nvj::Lane& lane : limited.lanes) {
        CHECK(lane.keys.size() <= nvj::kMaxLaneKeys);
    }

    CHECK(!nvj::timelineFromJson(std::string(nvj::kMaxTimelineText + 1, ' '), keep));
    CHECK(!nvj::timelineFromJson(std::string(20, '[') + std::string(20, ']'), keep));
    CHECK(!nvj::timelineFromJson(std::string(200000, '[') + std::string(200000, ']'), keep));
    CHECK(!nvj::timelineFromJson("{\"lanes\": [{\"target\": \"a\", \"keys\": [{\"seconds\": 1e999, \"value\": 1}]}]}", keep));

    for (size_t cut = 0; cut < text.size(); cut += 7) {
        nvj::ShowTimeline partial;
        nvj::timelineFromJson(text.substr(0, cut), partial);
        CHECK(partial.lanes.size() <= nvj::kMaxTimelineLanes);
    }
}

void testJsonHostile() {
    const std::string text = nvj::timelineToJson(sampleTimeline());
    int accepted = 0;
    for (int round = 0; round < 6000; ++round) {
        std::string mutated = text;
        const int edits = 1 + static_cast<int>(nextRandom() % 6);
        for (int e = 0; e < edits; ++e) {
            const size_t at = nextRandom() % mutated.size();
            switch (nextRandom() % 4) {
            case 0:
                mutated[at] = static_cast<char>(nextRandom() & 0xFF);
                break;
            case 1:
                mutated.erase(at, 1 + nextRandom() % 5);
                break;
            case 2:
                mutated.insert(at, 1, "[]{}\",:-.0e9"[nextRandom() % 12]);
                break;
            default:
                mutated.resize(at);
                break;
            }
            if (mutated.empty()) {
                mutated = "{";
            }
        }
        nvj::ShowTimeline result;
        if (nvj::timelineFromJson(mutated, result)) {
            ++accepted;
            CHECK(result.lanes.size() <= nvj::kMaxTimelineLanes);
            CHECK(nvj::timelineKeyCount(result) <= nvj::kMaxTimelineKeys);
            for (const nvj::Lane& lane : result.lanes) {
                for (size_t i = 0; i < lane.keys.size(); ++i) {
                    CHECK(std::isfinite(lane.keys[i].seconds) && lane.keys[i].seconds >= 0.0 && lane.keys[i].seconds <= nvj::kMaxTimelineSeconds);
                    CHECK(lane.keys[i].value >= 0.0f && lane.keys[i].value <= 1.0f);
                    CHECK(i == 0 || lane.keys[i - 1].seconds <= lane.keys[i].seconds);
                }
            }
            CHECK(result.loop.start <= result.loop.end);
        }
    }
    std::printf("timeline hostile json: %d of 6000 accepted\n", accepted);
    CHECK(accepted > 100);
}

void testOldFilesAndUnchangedCues() {
    const std::string oldCues = "{\n  \"cues\": [\n    {\"seconds\": 72.2, \"target\": \"column/3\", \"value\": 1.0},\n"
                                "    {\"seconds\": 12.0, \"target\": \"layer/2/opacity\", \"value\": 0.25, \"note\": \"kept out\"},\n"
                                "    {\"seconds\": 5.0, \"target\": \"master/blackout\"}\n  ],\n  \"version\": 1\n}";
    std::vector<nvj::Cue> cues;
    CHECK(nvj::cuesFromJson(oldCues, cues));
    CHECK(cues.size() == 3);
    CHECK(cues[0].seconds == 5.0 && cues[0].value == 1.0f);
    CHECK(cues[1].target == "layer/2/opacity" && cues[1].value == 0.25f);
    CHECK(cues[2].target == "column/3");

    const std::string rewritten = nvj::cuesToJson(cues);
    std::vector<nvj::Cue> again;
    CHECK(nvj::cuesFromJson(rewritten, again));
    CHECK(nvj::cuesToJson(again) == rewritten);
    CHECK(rewritten.find("\"version\": 1") != std::string::npos);

    nvj::ShowTimeline unused;
    CHECK(!nvj::timelineFromJson(oldCues, unused));
}

void testTransport() {
    Sim sim;
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 10.0, 1.0f);
    sim.run(1.0);
    CHECK(sim.runner.state() == nvj::TimelineState::Stopped);
    CHECK(!sim.runner.engaged());
    CHECK(sim.host.writes == 0);
    CHECK(sim.host.values["master/level"] == 1.0f);

    sim.runner.play();
    sim.run(5.0);
    CHECK(sim.runner.state() == nvj::TimelineState::Playing);
    CHECK(near(sim.runner.position(), 5.0, 0.05));
    CHECK(near(sim.host.values["master/level"], sim.runner.position() / 10.0, 0.002));
    CHECK(sim.runner.laneStatus("master/level") == nvj::LaneStatus::Following);

    sim.runner.pause();
    const double held = sim.runner.position();
    sim.run(3.0);
    CHECK(sim.runner.state() == nvj::TimelineState::Paused);
    CHECK(sim.runner.position() == held);
    sim.runner.play();
    sim.run(1.0);
    CHECK(near(sim.runner.position(), held + 1.0, 0.05));

    sim.runner.seek(7.0);
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Playing);
    CHECK(near(sim.runner.position(), 7.0, 1.0e-9));
    CHECK(near(sim.host.values["master/level"], 0.7, 0.001));
    sim.runner.pause();
    sim.step();
    sim.runner.seek(7.0);
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Stopped);
    CHECK(near(sim.runner.position(), 7.0, 1.0e-9));
    sim.runner.play();

    sim.runner.stop();
    sim.run(0.2);
    CHECK(sim.runner.position() == 0.0);
    CHECK(near(sim.host.values["master/level"], 0.0, 0.001));
    CHECK(sim.runner.state() == nvj::TimelineState::Stopped);

    sim.runner.play();
    sim.run(30.0);
    CHECK(near(sim.host.values["master/level"], 1.0, 1.0e-6));

    sim.runner.toggle();
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Paused);
    sim.runner.toggle();
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Playing);
}

void testCuesOnTransport() {
    Sim sim;
    sim.cue(0.0);
    sim.cue(1.0);
    sim.cue(2.0);
    sim.cue(3.5);
    sim.runner.play();
    sim.run(5.0);
    CHECK(sim.host.fired.size() == 4);
    CHECK(sim.host.count(0.0) == 1 && sim.host.count(1.0) == 1 && sim.host.count(2.0) == 1 && sim.host.count(3.5) == 1);
    for (size_t i = 1; i < sim.host.fired.size(); ++i) {
        CHECK(sim.host.fired[i - 1].seconds <= sim.host.fired[i].seconds);
    }

    Sim pausing;
    pausing.cue(1.0);
    pausing.cue(2.0);
    pausing.runner.play();
    pausing.run(1.5);
    CHECK(pausing.host.fired.size() == 1);
    pausing.runner.pause();
    pausing.run(1.0);
    pausing.runner.play();
    pausing.run(0.2);
    CHECK(pausing.host.fired.size() == 1);
    pausing.run(1.0);
    CHECK(pausing.host.fired.size() == 2);

    Sim exact;
    exact.cue(1.0);
    exact.runner.seek(1.0);
    exact.step();
    CHECK(exact.host.fired.empty());
    exact.runner.play();
    exact.run(0.1);
    CHECK(exact.host.count(1.0) == 1);
    exact.runner.pause();
    exact.runner.play();
    exact.run(0.5);
    CHECK(exact.host.count(1.0) == 1);

    Sim off;
    off.config.cues = false;
    off.apply();
    off.cue(0.5);
    off.runner.play();
    off.run(2.0);
    CHECK(off.host.fired.empty());
}

void testLoop() {
    Sim sim;
    sim.cue(2.0);
    sim.cue(3.0);
    sim.cue(4.0);
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 10.0, 1.0f);
    nvj::setLoop(sim.timeline, true, 2.0, 4.0);
    sim.runner.seek(1.0);
    sim.runner.play();
    sim.run(9.5);
    const unsigned long long wraps = sim.runner.loopWraps();
    std::printf("timeline loop wraps %llu position %.3f\n", wraps, sim.runner.position());
    CHECK(wraps == 4);
    CHECK(sim.runner.position() >= 2.0 && sim.runner.position() < 4.0);
    CHECK(sim.host.count(4.0) == static_cast<int>(wraps));
    CHECK(sim.host.count(2.0) == static_cast<int>(wraps) + 1);
    CHECK(sim.host.count(3.0) == 4);
    CHECK(sim.runner.seeks() == 0);
    CHECK(sim.runner.cuesSkipped() == 0);

    Sim disabled;
    disabled.cue(2.0);
    nvj::setLoop(disabled.timeline, false, 2.0, 4.0);
    disabled.runner.play();
    disabled.run(6.0);
    CHECK(disabled.runner.loopWraps() == 0);
    CHECK(disabled.runner.position() > 5.0);

    Sim tiny;
    nvj::setLoop(tiny.timeline, true, 2.0, 2.01);
    tiny.runner.play();
    tiny.run(4.0);
    CHECK(tiny.runner.loopWraps() == 0);

    Sim after;
    nvj::setLoop(after.timeline, true, 2.0, 4.0);
    after.runner.seek(6.0);
    after.runner.play();
    after.run(2.0);
    CHECK(after.runner.loopWraps() == 0);
    CHECK(after.runner.position() > 7.0);

    Sim whole;
    whole.cue(0.0);
    nvj::setLoop(whole.timeline, true, 0.0, 1.0);
    whole.runner.play();
    whole.run(3.5);
    CHECK(whole.runner.loopWraps() == 3);
    CHECK(whole.host.count(0.0) == 4);
}

void testSeek() {
    Sim sim;
    sim.cue(5.0);
    sim.cue(6.0);
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 20.0, 1.0f);
    sim.runner.play();
    sim.run(2.0);
    sim.runner.seek(10.0);
    sim.step();
    CHECK(sim.runner.seeks() == 1);
    CHECK(sim.runner.cuesSkipped() == 2);
    CHECK(sim.host.fired.empty());
    CHECK(near(sim.runner.position(), 10.0, 1.0e-9));
    CHECK(near(sim.host.values["master/level"], 0.5, 0.001));
    sim.run(1.0);
    CHECK(sim.host.fired.empty());

    sim.runner.seek(4.5);
    sim.step();
    CHECK(sim.runner.seeks() == 2);
    CHECK(sim.runner.cuesSkipped() == 2);
    CHECK(near(sim.host.values["master/level"], 4.5 / 20.0, 0.001));
    sim.run(2.0);
    CHECK(sim.host.count(5.0) == 1 && sim.host.count(6.0) == 1);

    Sim hitch;
    hitch.cue(1.0);
    hitch.cue(3.0);
    hitch.runner.play();
    hitch.run(0.7);
    hitch.clock += 0.4;
    hitch.step();
    CHECK(hitch.host.count(1.0) == 1);
    CHECK(hitch.runner.seeks() == 0);
    hitch.clock += 3.0;
    hitch.step();
    CHECK(hitch.runner.seeks() == 1);
    CHECK(hitch.runner.cuesSkipped() == 1);
    CHECK(hitch.host.count(3.0) == 0);
    CHECK(hitch.runner.position() > 4.0);

    Sim clamped;
    clamped.runner.seek(-50.0);
    clamped.step();
    CHECK(clamped.runner.position() == 0.0);
    clamped.runner.seek(1.0e12);
    clamped.step();
    CHECK(clamped.runner.position() == nvj::kMaxTimelineSeconds);
    clamped.runner.seek(std::nan(""));
    clamped.step();
    CHECK(clamped.runner.position() == nvj::kMaxTimelineSeconds);
}

void testTimecodeFollow() {
    Sim sim;
    sim.cue(100.5);
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 200.0, 1.0f);
    sim.timecode(100.0);
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Following);
    sim.runTimecode(2.0);
    CHECK(near(sim.runner.position(), sim.inputs.timecodeSeconds, 1.0e-9));
    CHECK(sim.host.fired.empty());
    CHECK(near(sim.host.values["master/level"], sim.runner.position() / 200.0, 0.002));

    sim.runner.play();
    sim.runner.seek(5.0);
    sim.runner.stop();
    sim.runner.pause();
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Following);
    CHECK(near(sim.runner.position(), sim.inputs.timecodeSeconds, 1.0e-9));

    const unsigned long long before = sim.runner.seeks();
    sim.timecode(150.0);
    sim.step();
    CHECK(sim.runner.seeks() == before + 1);
    CHECK(near(sim.host.values["master/level"], 0.75, 0.002));
    CHECK(sim.host.fired.empty());
    CHECK(sim.runner.state() == nvj::TimelineState::Following);

    sim.timecode(10.0);
    sim.step();
    CHECK(sim.runner.seeks() == before + 2);
    CHECK(near(sim.host.values["master/level"], 0.05, 0.002));
}

void testClockChange() {
    Sim sim;
    sim.cue(7.0);
    sim.cue(100.5);
    sim.cue(300.5);
    sim.cue(20.0);
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 400.0, 1.0f);
    sim.runner.seek(5.0);
    sim.runner.play();
    sim.run(2.5);
    CHECK(sim.host.count(7.0) == 1);

    sim.timecode(100.0);
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Following);
    CHECK(sim.runner.clockChanges() == 1);
    CHECK(near(sim.runner.position(), 100.0, 1.0e-9));
    CHECK(sim.runner.seeks() == 1);
    sim.runTimecode(1.0);
    CHECK(sim.host.count(100.5) == 0);

    sim.inputs.timecodeValid = false;
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::LostHolding);
    CHECK(sim.runner.timecodeLosses() == 1);
    const double heldAt = sim.runner.position();
    const float heldValue = sim.host.values["master/level"];
    sim.run(5.0);
    CHECK(sim.runner.position() == heldAt);
    CHECK(sim.host.values["master/level"] == heldValue);
    CHECK(sim.host.count(100.5) == 0);

    sim.timecode(300.0);
    sim.step();
    CHECK(sim.runner.state() == nvj::TimelineState::Following);
    CHECK(sim.runner.clockChanges() == 2);
    CHECK(near(sim.runner.position(), 300.0, 1.0e-9));
    CHECK(sim.host.count(300.5) == 0);
    CHECK(sim.host.count(20.0) == 0);

    Sim running;
    running.config.lost = nvj::LostPolicy::Continue;
    running.apply();
    running.cue(100.5);
    running.cue(101.5);
    running.cue(105.0);
    running.timecode(100.0);
    running.step();
    running.runTimecode(0.2);
    running.inputs.timecodeValid = false;
    running.step();
    CHECK(running.runner.state() == nvj::TimelineState::LostRunning);
    const double lostAt = running.runner.position();
    running.run(2.0);
    CHECK(near(running.runner.position(), lostAt + 2.0, 0.05));
    CHECK(running.host.count(100.5) == 1);
    CHECK(running.host.count(101.5) == 1);
    CHECK(running.host.count(105.0) == 0);
    running.runner.pause();
    running.step();
    CHECK(running.runner.state() == nvj::TimelineState::Paused);

    Sim returning;
    returning.config.lost = nvj::LostPolicy::Continue;
    returning.apply();
    returning.cue(103.0);
    returning.timecode(100.0);
    returning.step();
    returning.inputs.timecodeValid = false;
    returning.run(1.0);
    returning.timecode(500.0);
    returning.step();
    CHECK(returning.runner.state() == nvj::TimelineState::Following);
    CHECK(returning.runner.clockChanges() == 1);
    CHECK(returning.host.fired.empty());
    CHECK(near(returning.runner.position(), 500.0, 1.0e-9));
}

void testSourceChoice() {
    Sim transportOnly;
    transportOnly.config.source = nvj::TimelineSourceChoice::Transport;
    transportOnly.apply();
    transportOnly.timecode(100.0);
    transportOnly.runner.play();
    transportOnly.run(2.0);
    CHECK(transportOnly.runner.state() == nvj::TimelineState::Playing);
    CHECK(near(transportOnly.runner.position(), 2.0, 0.05));
    CHECK(transportOnly.runner.clockChanges() == 0);

    Sim timecodeOnly;
    timecodeOnly.config.source = nvj::TimelineSourceChoice::Timecode;
    timecodeOnly.apply();
    timecodeOnly.cue(0.5);
    timecodeOnly.runner.play();
    timecodeOnly.run(2.0);
    CHECK(timecodeOnly.runner.state() == nvj::TimelineState::Waiting);
    CHECK(timecodeOnly.runner.position() == 0.0);
    CHECK(timecodeOnly.host.fired.empty());
    timecodeOnly.timecode(10.0);
    timecodeOnly.step();
    CHECK(timecodeOnly.runner.state() == nvj::TimelineState::Following);
    timecodeOnly.inputs.timecodeValid = false;
    timecodeOnly.step();
    CHECK(timecodeOnly.runner.state() == nvj::TimelineState::LostHolding);

    Sim switching;
    switching.timecode(50.0);
    switching.step();
    CHECK(switching.runner.state() == nvj::TimelineState::Following);
    switching.config.source = nvj::TimelineSourceChoice::Transport;
    switching.apply();
    switching.step();
    CHECK(switching.runner.state() == nvj::TimelineState::Stopped || switching.runner.state() == nvj::TimelineState::Paused);
    switching.runner.play();
    switching.run(1.0);
    CHECK(switching.runner.state() == nvj::TimelineState::Playing);
    switching.config.source = nvj::TimelineSourceChoice::Timecode;
    switching.apply();
    switching.inputs.timecodeValid = false;
    switching.step();
    CHECK(switching.runner.state() == nvj::TimelineState::Waiting);
}

void testOverride() {
    Sim sim;
    const int lane = sim.lane("layer/1/opacity");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 10.0, 1.0f);
    sim.key(lane, 20.0, 0.0f);
    sim.host.values["layer/1/opacity"] = 0.5f;
    sim.runner.seek(0.0);
    sim.runner.play();
    sim.run(3.0);
    CHECK(sim.runner.overrides() == 0);
    CHECK(sim.runner.laneStatus("layer/1/opacity") == nvj::LaneStatus::Following);
    CHECK(near(sim.host.values["layer/1/opacity"], sim.runner.position() / 10.0, 0.002));

    sim.host.values["layer/1/opacity"] = 0.9f;
    sim.step();
    CHECK(sim.runner.overrides() == 1);
    CHECK(sim.runner.overridden("layer/1/opacity"));
    CHECK(sim.runner.laneStatus("layer/1/opacity") == nvj::LaneStatus::Overridden);
    CHECK(sim.runner.overriddenCount() == 1);
    const int writesBefore = sim.host.writes;
    sim.run(5.0);
    CHECK(sim.host.writes == writesBefore);
    CHECK(sim.host.values["layer/1/opacity"] == 0.9f);
    CHECK(sim.runner.overridden("layer/1/opacity"));

    sim.run(3.0);
    CHECK(sim.runner.position() > 10.0);
    CHECK(!sim.runner.overridden("layer/1/opacity"));
    CHECK(sim.host.writes > writesBefore);
    CHECK(near(sim.host.values["layer/1/opacity"], 1.0 - (sim.runner.position() - 10.0) / 10.0, 0.002));
    CHECK(sim.runner.overrides() == 1);

    sim.host.values["layer/1/opacity"] = 0.05f;
    sim.step();
    CHECK(sim.runner.overridden("layer/1/opacity"));
    sim.runner.seek(2.0);
    sim.step();
    CHECK(!sim.runner.overridden("layer/1/opacity"));
    CHECK(near(sim.host.values["layer/1/opacity"], 0.2, 0.001));

    Sim released;
    released.config.override = nvj::OverridePolicy::UntilReleased;
    released.apply();
    const int l2 = released.lane("master/level");
    released.key(l2, 0.0, 0.0f);
    released.key(l2, 10.0, 1.0f);
    released.runner.play();
    released.run(1.0);
    released.host.values["master/level"] = 0.77f;
    released.step();
    CHECK(released.runner.overridden("master/level"));
    released.run(15.0);
    CHECK(released.runner.overridden("master/level"));
    CHECK(released.host.values["master/level"] == 0.77f);
    released.runner.seek(1.0);
    released.step();
    CHECK(released.runner.overridden("master/level"));
    released.runner.release("master/level");
    released.step();
    CHECK(!released.runner.overridden("master/level"));
    CHECK(near(released.host.values["master/level"], 0.1, 0.01));
    released.host.values["master/level"] = 0.33f;
    released.step();
    CHECK(released.runner.overriddenCount() == 1);
    released.runner.releaseAll();
    CHECK(released.runner.overriddenCount() == 0);
    released.step();
    CHECK(!released.runner.overridden("master/level"));

    Sim toggles;
    const int t = toggles.lane("master/blackout");
    toggles.key(t, 0.0, 0.0f, nvj::KeyShape::Linear);
    toggles.key(t, 5.0, 1.0f, nvj::KeyShape::Linear);
    toggles.key(t, 8.0, 0.0f, nvj::KeyShape::Linear);
    toggles.runner.play();
    toggles.run(4.0);
    CHECK(toggles.host.values["master/blackout"] == 0.0f);
    toggles.run(2.0);
    CHECK(toggles.host.values["master/blackout"] == 1.0f);
    toggles.host.values["master/blackout"] = 0.0f;
    toggles.step();
    CHECK(toggles.runner.overridden("master/blackout"));
    toggles.run(1.0);
    CHECK(toggles.host.values["master/blackout"] == 0.0f);
    toggles.run(2.0);
    CHECK(!toggles.runner.overridden("master/blackout"));
    CHECK(toggles.host.values["master/blackout"] == 0.0f);

    Sim states;
    const int a = states.lane("master/level");
    states.key(a, 0.0, 0.5f);
    states.timeline.lanes[static_cast<size_t>(a)].muted = true;
    const int b = states.lane("master/bpm");
    states.key(b, 0.0, 0.5f);
    const int c = states.lane("layer/1/opacity");
    (void)c;
    states.runner.play();
    states.run(1.0);
    CHECK(states.runner.laneStatus("master/level") == nvj::LaneStatus::Muted);
    CHECK(states.runner.laneStatus("master/bpm") == nvj::LaneStatus::Unavailable);
    CHECK(states.runner.laneStatus("layer/1/opacity") == nvj::LaneStatus::Idle);
    CHECK(states.host.writes == 0);
    CHECK(states.host.refusedWrites == 0);
    states.timeline.lanes[static_cast<size_t>(a)].muted = false;
    states.step();
    CHECK(states.host.writes == 1);
    CHECK(states.host.values["master/level"] == 0.5f);
}

void testOverrideSurvivesStructureChanges() {
    Sim sim;
    const int a = sim.lane("master/level");
    sim.key(a, 0.0, 0.0f);
    sim.key(a, 10.0, 1.0f);
    const int b = sim.lane("layer/1/opacity");
    sim.key(b, 0.0, 1.0f);
    sim.key(b, 10.0, 0.0f);
    sim.runner.play();
    sim.run(1.0);
    sim.host.values["master/level"] = 0.9f;
    sim.step();
    CHECK(sim.runner.overridden("master/level"));
    CHECK(!sim.runner.overridden("layer/1/opacity"));
    nvj::removeLane(sim.timeline, 0);
    sim.step();
    CHECK(!sim.runner.overridden("master/level"));
    CHECK(sim.runner.overriddenCount() == 0);
    CHECK(sim.runner.laneStatus("layer/1/opacity") == nvj::LaneStatus::Following);
}

void testRunningAndClock() {
    Sim sim;
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.key(lane, 10.0, 1.0f);
    sim.runner.play();
    sim.run(2.0);
    const double frozen = sim.runner.position();
    sim.inputs.running = false;
    sim.run(3.0);
    CHECK(sim.runner.position() == frozen);
    CHECK(sim.runner.state() == nvj::TimelineState::Paused);
    sim.inputs.running = true;
    sim.run(1.0);
    CHECK(near(sim.runner.position(), frozen + 1.0, 0.05));
    CHECK(sim.runner.seeks() == 0);

    const double beforeReset = sim.runner.position();
    sim.clock = 3.0;
    sim.run(1.0);
    CHECK(near(sim.runner.position(), beforeReset + 1.0, 0.05));
    CHECK(sim.runner.seeks() == 0);

    sim.clock = std::nan("");
    sim.step();
    CHECK(std::isfinite(sim.runner.position()));
    sim.clock = 10.0;
    sim.run(0.5);
    CHECK(std::isfinite(sim.runner.position()));
}

void testDisabled() {
    Sim sim;
    sim.config.enabled = false;
    sim.apply();
    sim.cue(0.0);
    const int lane = sim.lane("master/level");
    sim.key(lane, 0.0, 0.0f);
    sim.runner.play();
    sim.runner.seek(5.0);
    sim.timecode(10.0);
    sim.run(2.0);
    CHECK(sim.runner.state() == nvj::TimelineState::Off);
    CHECK(sim.host.writes == 0);
    CHECK(sim.host.fired.empty());
    CHECK(sim.runner.position() == 0.0);

    sim.inputs.timecodeValid = false;
    sim.config.enabled = true;
    sim.apply();
    sim.run(1.0);
    CHECK(sim.runner.state() == nvj::TimelineState::Stopped);
    CHECK(sim.host.writes == 0);
    sim.runner.play();
    sim.run(0.5);
    CHECK(sim.host.writes > 0);
    sim.config.enabled = false;
    sim.apply();
    const int writes = sim.host.writes;
    sim.run(1.0);
    CHECK(sim.host.writes == writes);
    CHECK(sim.runner.state() == nvj::TimelineState::Off);
}

void testHistory() {
    nvj::TimelineHistory history;
    nvj::TimelineDoc doc;
    nvj::TimelineDoc before = doc;
    CHECK(!history.commit(before, doc));
    CHECK(!history.canUndo() && !history.canRedo());

    nvj::addLane(doc.timeline, "master/level");
    CHECK(history.commit(before, doc));
    CHECK(history.canUndo() && !history.canRedo());
    before = doc;
    nvj::insertKey(doc.timeline, 0, {1.0, 0.5f, nvj::KeyShape::Linear});
    nvj::Cue cue;
    cue.seconds = 4.0;
    cue.target = "deck/next";
    doc.cues.push_back(cue);
    CHECK(history.commit(before, doc));
    CHECK(history.undoCount() == 2);

    nvj::TimelineDoc current = doc;
    CHECK(history.undo(current));
    CHECK(current.cues.empty() && current.timeline.lanes[0].keys.empty());
    CHECK(history.canRedo());
    CHECK(history.undo(current));
    CHECK(current.timeline.lanes.empty());
    CHECK(!history.undo(current));
    CHECK(history.redo(current));
    CHECK(current.timeline.lanes.size() == 1);
    CHECK(history.redo(current));
    CHECK(nvj::sameDoc(current, doc));
    CHECK(!history.redo(current));

    CHECK(history.undo(current));
    nvj::TimelineDoc branch = current;
    nvj::addLane(branch.timeline, "layer/1/opacity");
    CHECK(history.commit(current, branch));
    CHECK(!history.canRedo());

    nvj::TimelineHistory many;
    nvj::TimelineDoc state;
    for (int i = 0; i < 200; ++i) {
        nvj::TimelineDoc next = state;
        nvj::Cue c;
        c.seconds = i;
        c.target = "deck/next";
        next.cues.push_back(c);
        many.commit(state, next);
        state = next;
    }
    CHECK(many.undoCount() == nvj::TimelineHistory::kLimit);
    nvj::TimelineDoc walk = state;
    size_t steps = 0;
    while (many.undo(walk)) {
        ++steps;
    }
    CHECK(steps == nvj::TimelineHistory::kLimit);
    CHECK(walk.cues.size() == 200 - nvj::TimelineHistory::kLimit);

    nvj::TimelineHistory heavy;
    nvj::TimelineDoc big;
    for (size_t lane = 0; lane < 32; ++lane) {
        const int index = nvj::addLane(big.timeline, "lane/" + std::to_string(lane));
        for (size_t k = 0; k < 512; ++k) {
            nvj::insertKey(big.timeline, static_cast<size_t>(index), {static_cast<double>(k), 0.5f, nvj::KeyShape::Linear});
        }
    }
    nvj::TimelineDoc walking = big;
    for (int i = 0; i < 60; ++i) {
        nvj::TimelineDoc next = walking;
        next.timeline.lanes[0].keys[0].value = static_cast<float>(i % 10) / 10.0f;
        next.timeline.lanes[1].keys[0].value = static_cast<float>(i % 7) / 10.0f + 0.01f;
        heavy.commit(walking, next);
        walking = next;
    }
    std::printf("timeline history heavy: %zu steps, %zu bytes\n", heavy.undoCount(), heavy.bytes());
    CHECK(heavy.bytes() <= nvj::TimelineHistory::kBudgetBytes);
    CHECK(heavy.undoCount() >= 1 && heavy.undoCount() < 60);
    nvj::TimelineDoc probe = walking;
    CHECK(heavy.undo(probe));

    history.reset();
    CHECK(!history.canUndo() && !history.canRedo() && history.bytes() == 0);
}

struct ControlFakeHost final : nvj::ControlHost {
    void triggerClip(int layer, int column) override { clips.emplace_back(layer, column); }
    void triggerColumn(int column) override { columns.push_back(column); }
    void clearLayer(int) override {}
    void switchDeck(int) override {}
    void tap() override {}
    void resync() override {}
    bool blackout() const override { return blackoutOn; }
    void setBlackout(bool on) override { blackoutOn = on; }
    std::vector<std::pair<int, int>> clips;
    std::vector<int> columns;
    bool blackoutOn = false;
};

nvj::MidiMessage midiMessage(int status, int data1, int data2, double time) {
    nvj::MidiMessage m;
    m.status = status;
    m.data1 = data1;
    m.data2 = data2;
    m.time = time;
    return m;
}

struct SystemRig {
    nvj::Composition comp;
    ControlFakeHost host;
    nvj::ControlSystem control{comp, host};
    nvj::SyncSystem sync;
    nvj::BeatClock clock;
    nvj::TimelineSystem timeline;
    double now = 2000.0;
    const double dt = 1.0 / 60.0;
    std::vector<nvj::MidiMessage> scheduled;
    int syncFired = 0;

    SystemRig() {
        nvj::SyncConfig config;
        config.mode = nvj::SyncMode::Auto;
        sync.configure(config);
        nvj::TimelineConfig timelineConfig;
        timelineConfig.enabled = true;
        timeline.configure(timelineConfig);
    }

    void step() {
        now += dt;
        std::vector<nvj::MidiMessage> due;
        std::vector<nvj::MidiMessage> later;
        for (const nvj::MidiMessage& m : scheduled) {
            (m.time <= now ? due : later).push_back(m);
        }
        scheduled.swap(later);
        sync.update(dt, due, clock, now);
        syncFired += static_cast<int>(sync.firedCues().size());
        for (const nvj::Cue& cue : sync.firedCues()) {
            control.fireTarget(cue.target, cue.value);
        }
        clock.advance(dt, true);
        timeline.update(dt, nvj::timelineInputs(sync, clock.time(), true), sync, control);
    }

    void run(double seconds) {
        const double end = now + seconds;
        while (now < end) {
            step();
        }
    }

    void feedMtc(nvj::FrameRate rate, long long firstFrame, int sequences, double startTime) {
        const double fps = nvj::exactFps(rate);
        for (int s = 0; s < sequences; ++s) {
            const nvj::Timecode t = nvj::timecodeFromFrames(firstFrame + 2LL * s, rate);
            const std::array<int, 8> data = ltctest::quarterFrames(t);
            for (int k = 0; k < 8; ++k) {
                const double time = startTime + (static_cast<double>(s) * 8.0 + k) * (1.0 / (4.0 * fps));
                scheduled.push_back(midiMessage(0xF1, data[static_cast<size_t>(k)], 0, time));
            }
        }
    }
};

void testSystemWithSimulatedSources() {
    SystemRig rig;
    nvj::ShowTimeline& tl = rig.timeline.timeline();
    const int lane = nvj::addLane(tl, "layer/1/opacity");
    nvj::insertKey(tl, static_cast<size_t>(lane), {0.0, 0.0f, nvj::KeyShape::Linear});
    nvj::insertKey(tl, static_cast<size_t>(lane), {10.0, 1.0f, nvj::KeyShape::Linear});
    const int blackout = nvj::addLane(tl, "master/blackout");
    nvj::insertKey(tl, static_cast<size_t>(blackout), {0.0, 0.0f, nvj::KeyShape::Hold});
    nvj::insertKey(tl, static_cast<size_t>(blackout), {6.0, 1.0f, nvj::KeyShape::Hold});
    nvj::Cue clip;
    clip.seconds = 2.0;
    clip.target = "layer/1/clip/1";
    rig.sync.cues().push_back(clip);
    nvj::Cue far;
    far.seconds = 52.0;
    far.target = "column/2";
    rig.sync.cues().push_back(far);
    nvj::sortCues(rig.sync.cues());

    rig.run(1.0);
    CHECK(rig.timeline.runner().state() == nvj::TimelineState::Stopped);
    CHECK(rig.host.clips.empty());

    rig.timeline.runner().play();
    rig.run(4.0);
    CHECK(rig.timeline.runner().state() == nvj::TimelineState::Playing);
    CHECK(near(rig.comp.layers[0].opacity, rig.timeline.runner().position() / 10.0, 0.01));
    CHECK(rig.host.clips.size() == 1);
    CHECK(rig.host.clips[0].first == 0 && rig.host.clips[0].second == 0);
    CHECK(rig.syncFired == 0);
    CHECK(!rig.host.blackoutOn);
    rig.run(3.0);
    CHECK(rig.host.blackoutOn);

    const nvj::FrameRate rate = nvj::FrameRate::Fps25;
    const long long first = nvj::timecodeToFrames(nvj::Timecode{0, 0, 49, 0, rate});
    rig.feedMtc(rate, first, 60, rig.now + 0.1);
    rig.run(1.5);
    CHECK(rig.sync.timecodeActive());
    CHECK(rig.timeline.runner().state() == nvj::TimelineState::Following);
    CHECK(near(rig.timeline.runner().position(), rig.sync.timecodeSeconds(), 1.0e-9));
    CHECK(rig.timeline.runner().clockChanges() == 1);
    CHECK(rig.comp.layers[0].opacity == 1.0f);
    const size_t clipsBefore = rig.host.clips.size();
    rig.run(2.5);
    CHECK(rig.syncFired == 1);
    CHECK(rig.host.columns.size() == 1 && rig.host.columns[0] == 1);
    CHECK(rig.host.clips.size() == clipsBefore);
    CHECK(rig.timeline.runner().cuesFired() == 1);

    rig.scheduled.clear();
    rig.run(1.0);
    CHECK(!rig.sync.timecodeActive());
    CHECK(rig.timeline.runner().state() == nvj::TimelineState::LostHolding);
    const double heldAt = rig.timeline.runner().position();
    rig.run(3.0);
    CHECK(rig.timeline.runner().position() == heldAt);
    CHECK(rig.comp.layers[0].opacity == 1.0f);
    CHECK(rig.syncFired == 1);

    rig.comp.layers[0].opacity = 0.25f;
    rig.run(0.5);
    CHECK(rig.timeline.runner().overridden("layer/1/opacity"));
    CHECK(near(rig.comp.layers[0].opacity, 0.25, 1.0e-6));
    rig.timeline.runner().release("layer/1/opacity");
    rig.run(0.2);
    CHECK(rig.comp.layers[0].opacity == 1.0f);

    nvj::TimelineDoc before = rig.timeline.capture(rig.sync);
    nvj::addLane(rig.timeline.timeline(), "master/level");
    nvj::Cue extra;
    extra.seconds = 9.0;
    extra.target = "deck/next";
    rig.sync.cues().push_back(extra);
    CHECK(rig.timeline.commit(before, rig.sync));
    CHECK(rig.timeline.timeline().lanes.size() == 3 && rig.sync.cues().size() == 3);
    CHECK(rig.timeline.undo(rig.sync));
    CHECK(rig.timeline.timeline().lanes.size() == 2 && rig.sync.cues().size() == 2);
    CHECK(rig.timeline.redo(rig.sync));
    CHECK(rig.timeline.timeline().lanes.size() == 3 && rig.sync.cues().size() == 3);
}

void testHostTargets() {
    nvj::Composition comp;
    ControlFakeHost fake;
    nvj::ControlSystem control(comp, fake);
    nvj::ControlTimelineHost host(control);
    CHECK(host.laneKind("layer/1/opacity") == nvj::LaneKind::Continuous);
    CHECK(host.laneKind("master/level") == nvj::LaneKind::Continuous);
    CHECK(host.laneKind("layer/2/fx/1/param/3") == nvj::LaneKind::Continuous);
    CHECK(host.laneKind("layer/1/bypass") == nvj::LaneKind::Toggle);
    CHECK(host.laneKind("master/blackout") == nvj::LaneKind::Toggle);
    CHECK(host.laneKind("master/bpm") == nvj::LaneKind::Refused);
    CHECK(host.laneKind("master/tap") == nvj::LaneKind::Refused);
    CHECK(host.laneKind("layer/1/clip/1") == nvj::LaneKind::Refused);
    CHECK(host.laneKind("column/1") == nvj::LaneKind::Refused);
    CHECK(host.laneKind("deck/next") == nvj::LaneKind::Refused);
    CHECK(host.laneKind("nothing/here") == nvj::LaneKind::Refused);
    CHECK(host.laneKind("") == nvj::LaneKind::Refused);
    float value = -1.0f;
    CHECK(host.write("layer/1/opacity", 0.3f));
    CHECK(host.read("layer/1/opacity", value));
    CHECK(near(value, 0.3, 1.0e-6));
    CHECK(near(comp.layers[0].opacity, 0.3, 1.0e-6));
    CHECK(!host.read("nothing/here", value));
    CHECK(!host.write("nothing/here", 0.5f));
    CHECK(host.write("master/blackout", 1.0f));
    CHECK(host.read("master/blackout", value) && value == 1.0f);
    CHECK(fake.blackoutOn);

    for (const nvj::ControlTarget& target : nvj::controlTargets()) {
        const nvj::LaneKind kind = host.laneKind(target.id);
        if (target.kind == nvj::ControlKind::Trigger || target.item == nvj::ControlItem::Bpm) {
            CHECK(kind == nvj::LaneKind::Refused);
        } else {
            CHECK(kind != nvj::LaneKind::Refused);
            float read = 0.0f;
            CHECK(host.read(target.id, read));
            CHECK(read >= 0.0f && read <= 1.0f);
        }
    }
}

void testPersistence() {
    const std::filesystem::path file = std::filesystem::path(nvj::userDataDir()) / "timeline.json";
    std::filesystem::remove(file);
    {
        nvj::TimelineSystem system;
        CHECK(!system.load());
        system.timeline() = sampleTimeline();
        system.markDirty();
        CHECK(system.save());
        CHECK(std::filesystem::exists(file));
        CHECK(!std::filesystem::exists(std::filesystem::path(file).concat(".tmp")));
    }
    {
        nvj::TimelineSystem system;
        CHECK(system.load());
        CHECK(system.loaded());
        nvj::TimelineDoc a;
        nvj::TimelineDoc b;
        a.timeline = sampleTimeline();
        b.timeline = system.timeline();
        CHECK(nvj::sameDoc(a, b));
    }
    {
        std::ofstream broken(file, std::ios::binary | std::ios::trunc);
        broken << "{ \"lanes\": [ {\"target\": \"master/level\", ";
    }
    {
        nvj::TimelineSystem system;
        CHECK(!system.load());
        CHECK(system.timeline().lanes.empty());
        CHECK(std::filesystem::exists(std::filesystem::path(file).concat(".damaged")));
    }
    {
        nvj::TimelineSystem system;
        system.timeline() = sampleTimeline();
        system.markDirty();
    }
    {
        nvj::TimelineSystem system;
        CHECK(system.load());
        CHECK(system.timeline().lanes.size() == 3);
        CHECK(!system.history().canUndo());
    }
    std::filesystem::remove(file);
}

void testRandomOperations() {
    for (int seed = 0; seed < 4; ++seed) {
        rngState = 777u + static_cast<std::uint32_t>(seed) * 101u;
        Sim sim;
        const char* targets[] = {"master/level", "layer/1/opacity", "master/blackout", "master/bpm", "unknown/id"};
        sim.host.kinds["master/bpm"] = nvj::LaneKind::Refused;
        unsigned long long lastSeeks = 0;
        unsigned long long lastFired = 0;
        unsigned long long lastWraps = 0;
        for (int i = 0; i < 20000; ++i) {
            switch (nextRandom() % 24) {
            case 0:
                sim.runner.play();
                break;
            case 1:
                sim.runner.pause();
                break;
            case 2:
                sim.runner.stop();
                break;
            case 3:
                sim.runner.seek(randomIn(-10.0, 200.0));
                break;
            case 4:
                sim.inputs.timecodeValid = (nextRandom() & 1) != 0;
                sim.inputs.timecodeSeconds = randomIn(-5.0, 400.0);
                break;
            case 5:
                sim.inputs.running = (nextRandom() & 3) != 0;
                break;
            case 6:
                sim.clock += randomIn(-5.0, 5.0);
                break;
            case 7: {
                const int index = nvj::addLane(sim.timeline, targets[nextRandom() % 5]);
                if (index >= 0) {
                    sim.key(index, randomIn(0.0, 120.0), static_cast<float>(randomIn(-0.2, 1.2)), static_cast<nvj::KeyShape>(nextRandom() % 3));
                }
                break;
            }
            case 8:
                if (!sim.timeline.lanes.empty()) {
                    nvj::removeLane(sim.timeline, nextRandom() % sim.timeline.lanes.size());
                }
                break;
            case 9:
                if (!sim.timeline.lanes.empty()) {
                    nvj::Lane& lane = sim.timeline.lanes[nextRandom() % sim.timeline.lanes.size()];
                    lane.muted = !lane.muted;
                }
                break;
            case 10:
                nvj::setLoop(sim.timeline, (nextRandom() & 1) != 0, randomIn(0.0, 60.0), randomIn(0.0, 60.0));
                break;
            case 11:
                sim.config.source = static_cast<nvj::TimelineSourceChoice>(nextRandom() % 3);
                sim.config.lost = static_cast<nvj::LostPolicy>(nextRandom() % 2);
                sim.config.override = static_cast<nvj::OverridePolicy>(nextRandom() % 2);
                sim.config.cues = (nextRandom() & 3) != 0;
                sim.apply();
                break;
            case 12:
                if (sim.cues.size() < 40) {
                    sim.cue(randomIn(0.0, 120.0), "deck/next");
                }
                break;
            case 13:
                if (!sim.cues.empty()) {
                    sim.cues.erase(sim.cues.begin() + static_cast<std::ptrdiff_t>(nextRandom() % sim.cues.size()));
                }
                break;
            case 14:
                sim.host.values[targets[nextRandom() % 3]] = static_cast<float>(randomIn(0.0, 1.0));
                break;
            case 15:
                sim.runner.release(targets[nextRandom() % 5]);
                break;
            case 16:
                if ((nextRandom() % 40) == 0) {
                    sim.runner.releaseAll();
                }
                break;
            case 17:
                if ((nextRandom() % 60) == 0) {
                    sim.config.enabled = !sim.config.enabled;
                    sim.apply();
                }
                break;
            default:
                break;
            }
            sim.step();
            const double position = sim.runner.position();
            CHECK(std::isfinite(position) && position >= 0.0 && position <= nvj::kMaxTimelineSeconds);
            CHECK(sim.runner.seeks() >= lastSeeks);
            CHECK(sim.runner.cuesFired() >= lastFired);
            CHECK(sim.runner.loopWraps() >= lastWraps);
            CHECK(sim.runner.overriddenCount() <= nvj::kMaxTimelineLanes);
            lastSeeks = sim.runner.seeks();
            lastFired = sim.runner.cuesFired();
            lastWraps = sim.runner.loopWraps();
            if (sim.runner.state() == nvj::TimelineState::Following) {
                CHECK(near(position, std::min(sim.inputs.timecodeSeconds, nvj::kMaxTimelineSeconds), 1.0e-9));
            }
        }
        CHECK(sim.host.refusedWrites == 0);
        CHECK(sim.host.fired.size() == sim.runner.cuesFired());
        for (const auto& entry : sim.host.values) {
            CHECK(std::isfinite(entry.second) && entry.second >= 0.0f && entry.second <= 1.0f);
        }
    }
}

}

void testCost() {
    Sim sim;
    for (int laneIndex = 0; laneIndex < static_cast<int>(nvj::kMaxTimelineLanes); ++laneIndex) {
        const std::string target = "test/lane" + std::to_string(laneIndex);
        sim.host.kinds[target] = nvj::LaneKind::Continuous;
        sim.host.values[target] = 0.0f;
        const int lane = sim.lane(target);
        CHECK(lane >= 0);
        for (int k = 0; k < 512; ++k) {
            sim.key(lane, k * 1.5 + laneIndex * 0.01, static_cast<float>((k * 7 + laneIndex) % 11) / 10.0f);
        }
    }
    for (int i = 0; i < 64; ++i) {
        sim.cue(10.0 + i * 11.0, "deck/next", 1.0f);
    }
    sim.apply();
    sim.runner.play();
    sim.run(2.0);
    const int frames = 20000;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        sim.step();
    }
    const auto end = std::chrono::steady_clock::now();
    const double microseconds = std::chrono::duration<double, std::micro>(end - begin).count() / frames;
    std::printf("timeline cost: %zu lanes, 512 keys each, %.2f us per frame\n", sim.timeline.lanes.size(), microseconds);
    CHECK(sim.timeline.lanes.size() == nvj::kMaxTimelineLanes);
    CHECK(sim.host.writes > 0);
    CHECK(microseconds < 5000.0);
}

int main() {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "nvj_timeline_test_data";
    std::error_code ec;
    std::filesystem::remove_all(dataDir, ec);
    std::filesystem::create_directories(dataDir, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dataDir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dataDir.string().c_str(), 1);
#endif
    testEvaluate();
    testEdits();
    testJson();
    testJsonHostile();
    testOldFilesAndUnchangedCues();
    testTransport();
    testCuesOnTransport();
    testLoop();
    testSeek();
    testTimecodeFollow();
    testClockChange();
    testSourceChoice();
    testOverride();
    testOverrideSurvivesStructureChanges();
    testRunningAndClock();
    testDisabled();
    testHistory();
    testHostTargets();
    testSystemWithSimulatedSources();
    testPersistence();
    testCost();
    testRandomOperations();
    return testing::finish();
}

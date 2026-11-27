#include "audio/audio_analyzer.hpp"
#include "audio/audio_channels.hpp"
#include "audio/audio_system.hpp"
#include "audio_signal.hpp"
#include "check.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using nvjtest::Frame;
using nvjtest::Section;
using nvjtest::Track;

double nearestKick(const std::vector<double>& kicks, double time) {
    double best = 1e9;
    for (double kick : kicks) {
        const double delta = time - kick;
        if (std::fabs(delta) < std::fabs(best)) {
            best = delta;
        }
    }
    return best;
}

double medianBpm(const std::vector<Frame>& frames, double from, double to) {
    std::vector<double> values;
    for (const Frame& frame : frames) {
        if (frame.time >= from && frame.time <= to) {
            values.push_back(frame.features.bpm);
        }
    }
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double lockTime(const std::vector<Frame>& frames, double bpm, double tolerance, double from = 0.0) {
    for (const Frame& frame : frames) {
        if (frame.time >= from && frame.features.bpm > 0.0f && std::fabs(frame.features.bpm - bpm) < bpm * tolerance) {
            return frame.time - from;
        }
    }
    return -1.0;
}

struct EventStats {
    int events = 0;
    int near = 0;
    double mean = 0.0;
};

EventStats eventStats(const std::vector<Frame>& frames, const std::vector<double>& kicks, double from, double to, double window) {
    EventStats stats;
    double sum = 0.0;
    for (const Frame& frame : frames) {
        if (frame.features.beatNow && frame.time >= from && frame.time <= to) {
            ++stats.events;
            const double delta = nearestKick(kicks, frame.time);
            if (std::fabs(delta) <= window) {
                ++stats.near;
                sum += delta;
            }
        }
    }
    stats.mean = stats.near > 0 ? sum / stats.near : 0.0;
    return stats;
}

std::vector<Frame> run(const Track& track, bool improved, int fps = 60, double latency = 0.0) {
    nvj::AudioAnalyzer analyzer;
    analyzer.configure(nvjtest::kRate);
    analyzer.setActive(true);
    analyzer.setImproved(improved);
    analyzer.setLatency(latency);
    return nvjtest::runAnalyzer(analyzer, track.samples, fps);
}

Section steady(double bpm, double end, double offset = 0.137) {
    Section section;
    section.end = end;
    section.bpm = bpm;
    section.offset = offset;
    section.hat = 0.3f;
    section.offbeatHats = true;
    return section;
}

void testDefaultsAreClassic() {
    const Track track = nvjtest::renderTrack({steady(128.0, 20.0)}, 20.0);
    nvj::AudioAnalyzer fresh;
    fresh.configure(nvjtest::kRate);
    fresh.setActive(true);
    nvj::AudioAnalyzer toggled;
    toggled.configure(nvjtest::kRate);
    toggled.setActive(true);
    toggled.setImproved(true);
    toggled.setImproved(false);
    toggled.setCrossovers(150.0f, 2000.0f);
    const std::vector<Frame> a = nvjtest::runAnalyzer(fresh, track.samples);
    const std::vector<Frame> b = nvjtest::runAnalyzer(toggled, track.samples);
    CHECK(a.size() == b.size());
    bool same = true;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const nvj::AudioFeatures& x = a[i].features;
        const nvj::AudioFeatures& y = b[i].features;
        same = same && x.bass == y.bass && x.mid == y.mid && x.high == y.high && x.level == y.level && x.kick == y.kick && x.bpm == y.bpm && x.beatNow == y.beatNow && x.beatCount == y.beatCount && x.spectrum == y.spectrum;
        same = same && x.beatConfidence == 0.0f && x.beatPhase < 0.0f && !x.tracking && x.onsetStrength == 0.0f;
    }
    CHECK(same);
    CHECK(a.back().features.beatCount > 20);
    CHECK(!fresh.improved());
}

void testSteadyTempo() {
    for (double bpm : {100.0, 128.0, 150.0}) {
        const Track track = nvjtest::renderTrack({steady(bpm, 30.0)}, 30.0);
        const std::vector<Frame> classic = run(track, false);
        const std::vector<Frame> improved = run(track, true);
        const double lock = lockTime(improved, bpm, 0.02);
        const double lockClassic = lockTime(classic, bpm, 0.02);
        const double medianImproved = medianBpm(improved, 15.0, 30.0);
        const double medianClassic = medianBpm(classic, 15.0, 30.0);
        const EventStats stats = eventStats(improved, track.kicks, 15.0, 30.0, 0.045);
        std::printf("steady %.0f BPM: lock classic %.2f s, improved %.2f s; median BPM %.2f / %.2f; events near kicks %d of %d, mean offset %.1f ms\n", bpm, lockClassic, lock, medianClassic, medianImproved, stats.near, stats.events, stats.mean * 1000.0);
        CHECK(lock > 0.0 && lock < 6.0);
        CHECK(std::fabs(medianImproved - bpm) < bpm * 0.01);
        CHECK(stats.events >= static_cast<int>(15.0 * bpm / 60.0 * 0.9));
        CHECK(stats.near >= stats.events * 9 / 10);
        CHECK(improved.back().features.tracking);
        CHECK(improved.back().features.beatConfidence > 0.5f);
        CHECK(improved.back().features.beatPhase >= 0.0f && improved.back().features.beatPhase < 1.0f);
    }
}

void testFrameRates() {
    const Track track = nvjtest::renderTrack({steady(128.0, 30.0)}, 30.0);
    for (int fps : {30, 60, 144}) {
        const std::vector<Frame> frames = run(track, true, fps);
        const double median = medianBpm(frames, 15.0, 30.0);
        const EventStats stats = eventStats(frames, track.kicks, 15.0, 30.0, 0.06);
        std::printf("frame rate %d: median BPM %.2f, events near kicks %d of %d, mean offset %.1f ms\n", fps, median, stats.near, stats.events, stats.mean * 1000.0);
        CHECK(std::fabs(median - 128.0) < 128.0 * 0.015);
        CHECK(stats.near >= stats.events * 9 / 10);
    }
}

void testTempoChange() {
    Section first = steady(120.0, 14.0);
    Section second = steady(140.0, 34.0, 0.071);
    second.start = 14.0;
    const Track track = nvjtest::renderTrack({first, second}, 34.0);
    const std::vector<Frame> classic = run(track, false);
    const std::vector<Frame> improved = run(track, true);
    const double reacquire = lockTime(improved, 140.0, 0.02, 14.0);
    const double reacquireClassic = lockTime(classic, 140.0, 0.02, 14.0);
    std::printf("tempo change 120 to 140: new tempo reached after classic %.2f s, improved %.2f s\n", reacquireClassic, reacquire);
    CHECK(medianBpm(improved, 10.0, 13.5) > 120.0 * 0.99 && medianBpm(improved, 10.0, 13.5) < 120.0 * 1.01);
    CHECK(reacquire > 0.0 && reacquire < 9.0);
    CHECK(std::fabs(medianBpm(improved, 28.0, 34.0) - 140.0) < 140.0 * 0.01);
}

void testOffbeats() {
    Section hats;
    hats.end = 24.0;
    hats.bpm = 120.0;
    hats.kick = 0.0f;
    hats.hat = 0.6f;
    hats.offbeatHats = true;
    hats.offset = 0.0;
    nvjtest::Noise noise;
    Track track;
    track.samples.assign(static_cast<size_t>(24.0 * nvjtest::kRate), 0.0f);
    for (double t = 0.25; t < 24.0; t += 0.5) {
        nvjtest::addHat(track.samples, t, 0.8f, noise);
    }
    const std::vector<Frame> improved = run(track, true);
    const double median = medianBpm(improved, 12.0, 24.0);
    std::printf("hats only at 120 BPM: improved median %.2f BPM\n", median);
    CHECK(std::fabs(median - 120.0) < 120.0 * 0.02);

    Section loud = steady(128.0, 30.0);
    loud.hat = 0.45f;
    loud.sixteenths = true;
    const Track busy = nvjtest::renderTrack({loud}, 30.0);
    const std::vector<Frame> frames = run(busy, true);
    const EventStats stats = eventStats(frames, busy.kicks, 15.0, 30.0, 0.06);
    std::printf("kick with offbeat and sixteenth hats: events near kicks %d of %d\n", stats.near, stats.events);
    CHECK(std::fabs(medianBpm(frames, 15.0, 30.0) - 128.0) < 128.0 * 0.015);
    CHECK(stats.near >= stats.events * 8 / 10);
}

void testSilence() {
    Section before = steady(128.0, 16.0);
    Section after = steady(128.0, 30.0);
    after.start = 20.0;
    const double period = 60.0 / 128.0;
    after.offset = std::fmod(std::fmod(0.137 - 20.0, period) + period, period);
    const Track track = nvjtest::renderTrack({before, after}, 30.0);
    const std::vector<Frame> improved = run(track, true);
    int duringSilence = 0;
    bool trackingAfterHold = false;
    double bpmAtHold = 0.0;
    for (const Frame& frame : improved) {
        if (frame.time > 17.8 && frame.time < 19.9) {
            if (frame.features.beatNow) {
                ++duringSilence;
            }
            trackingAfterHold = trackingAfterHold || frame.features.tracking;
            bpmAtHold = frame.features.bpm;
        }
    }
    double relock = -1.0;
    for (const Frame& frame : improved) {
        if (frame.time >= 20.0 && frame.features.tracking) {
            relock = frame.time - 20.0;
            break;
        }
    }
    const EventStats resumed = eventStats(improved, track.kicks, 24.0, 30.0, 0.05);
    std::printf("silence 16 to 20 s: beats while silent after the hold time %d, tempo kept %.2f BPM, beats again %.2f s after the music returns, %d of %d on the kicks\n", duringSilence, bpmAtHold, relock, resumed.near, resumed.events);
    CHECK(resumed.events >= 10 && resumed.near >= resumed.events * 9 / 10);
    CHECK(duringSilence == 0);
    CHECK(!trackingAfterHold);
    CHECK(std::fabs(bpmAtHold - 128.0) < 128.0 * 0.02);
    CHECK(relock >= 0.0 && relock < 4.0);
    int ghosts = 0;
    for (const Frame& frame : improved) {
        if (frame.time > 16.0 && frame.time < 17.4 && frame.features.beatNow) {
            ++ghosts;
        }
    }
    CHECK(ghosts >= 1);
}

void testDropout() {
    Section section = steady(128.0, 40.0);
    Track track = nvjtest::renderTrack({section}, 40.0);
    const size_t from = static_cast<size_t>(20.0 * nvjtest::kRate);
    const size_t to = static_cast<size_t>(23.0 * nvjtest::kRate);
    for (size_t i = from; i < to; ++i) {
        track.samples[i] *= 0.02f;
    }
    const std::vector<Frame> frames = run(track, true);
    int filled = 0;
    for (const Frame& frame : frames) {
        if (frame.features.beatNow && frame.time > 20.6 && frame.time < 23.0 && std::fabs(nearestKick(track.kicks, frame.time)) <= 0.05) {
            ++filled;
        }
    }
    std::printf("quiet break of 3 s: grid beats inside it that stayed on the kick grid: %d\n", filled);
    CHECK(filled >= 3);
    const EventStats after = eventStats(frames, track.kicks, 26.0, 40.0, 0.05);
    CHECK(after.near >= after.events * 9 / 10);
}

void testClipping() {
    Track track = nvjtest::renderTrack({steady(128.0, 30.0)}, 30.0);
    for (float& sample : track.samples) {
        sample = std::clamp(sample * 8.0f, -1.0f, 1.0f);
    }
    const std::vector<Frame> frames = run(track, true);
    const double median = medianBpm(frames, 15.0, 30.0);
    std::printf("hard clipped at 8 times gain: median %.2f BPM, tracking %d\n", median, frames.back().features.tracking ? 1 : 0);
    CHECK(std::fabs(median - 128.0) < 128.0 * 0.015);
    CHECK(frames.back().features.tracking);
    for (const Frame& frame : frames) {
        CHECK(frame.features.bass >= 0.0f && frame.features.bass <= 1.0f);
        CHECK(frame.features.beatConfidence >= 0.0f && frame.features.beatConfidence <= 1.0f);
        CHECK(frame.features.onsetStrength >= 0.0f && frame.features.onsetStrength <= 1.0f);
    }
}

void testLatency() {
    const Track track = nvjtest::renderTrack({steady(128.0, 30.0)}, 30.0);
    const std::vector<Frame> zero = run(track, true, 60, 0.0);
    const std::vector<Frame> early = run(track, true, 60, 0.06);
    const std::vector<Frame> late = run(track, true, 60, -0.06);
    const EventStats a = eventStats(zero, track.kicks, 15.0, 30.0, 0.2);
    const EventStats b = eventStats(early, track.kicks, 15.0, 30.0, 0.2);
    const EventStats c = eventStats(late, track.kicks, 15.0, 30.0, 0.2);
    std::printf("latency setting: mean offset of the grid beats to the kicks %.1f ms at 0, %.1f ms at +60 ms, %.1f ms at -60 ms\n", a.mean * 1000.0, b.mean * 1000.0, c.mean * 1000.0);
    CHECK(std::fabs((a.mean - b.mean) - 0.06) < 0.02);
    CHECK(std::fabs((c.mean - a.mean) - 0.06) < 0.02);
}

void testDjMix() {
    Section a = steady(124.0, 40.0, 0.2);
    Section b = steady(128.0, 40.0, 0.11);
    const Track one = nvjtest::renderTrack({a}, 40.0);
    const Track two = nvjtest::renderTrack({b}, 40.0);
    std::vector<float> mixed(one.samples.size(), 0.0f);
    nvjtest::mixInto(mixed, one.samples, 0.0, 20.0, false);
    for (size_t i = static_cast<size_t>(20.0 * nvjtest::kRate); i < mixed.size(); ++i) {
        mixed[i] = 0.0f;
    }
    for (size_t i = 0; i < static_cast<size_t>(20.0 * nvjtest::kRate); ++i) {
        mixed[i] = one.samples[i];
    }
    for (size_t i = static_cast<size_t>(20.0 * nvjtest::kRate); i < static_cast<size_t>(30.0 * nvjtest::kRate); ++i) {
        const float x = static_cast<float>(i - static_cast<size_t>(20.0 * nvjtest::kRate)) / static_cast<float>(10.0 * nvjtest::kRate);
        mixed[i] = one.samples[i] * (1.0f - x) + two.samples[i] * x;
    }
    for (size_t i = static_cast<size_t>(30.0 * nvjtest::kRate); i < mixed.size(); ++i) {
        mixed[i] = two.samples[i];
    }
    Track track;
    track.samples = mixed;
    track.kicks = two.kicks;
    const std::vector<Frame> classic = run(track, false);
    const std::vector<Frame> improved = run(track, true);
    double worst = 0.0;
    for (const Frame& frame : improved) {
        if (frame.time > 33.0 && frame.features.bpm > 0.0f) {
            worst = std::max(worst, std::fabs(static_cast<double>(frame.features.bpm) - 128.0));
        }
    }
    std::printf("DJ mix 124 to 128 BPM over 10 s: median before %.2f / %.2f, after %.2f / %.2f (classic / improved), worst deviation after %.2f BPM\n", medianBpm(classic, 12.0, 19.0), medianBpm(improved, 12.0, 19.0), medianBpm(classic, 34.0, 40.0), medianBpm(improved, 34.0, 40.0), worst);
    CHECK(std::fabs(medianBpm(improved, 12.0, 19.0) - 124.0) < 124.0 * 0.015);
    CHECK(std::fabs(medianBpm(improved, 34.0, 40.0) - 128.0) < 128.0 * 0.015);
    for (const Frame& frame : improved) {
        if (frame.time > 14.0 && frame.features.bpm > 0.0f) {
            CHECK(frame.features.bpm > 118.0f && frame.features.bpm < 134.0f);
        }
    }
}

void testCrossovers() {
    nvj::AudioAnalyzer low;
    low.configure(48000);
    low.setCrossovers(150.0f, 2000.0f);
    std::vector<float> block(800);
    double phase = 0.0;
    auto feed = [&](nvj::AudioAnalyzer& analyzer) {
        phase = 0.0;
        for (int frame = 0; frame < 60; ++frame) {
            for (float& sample : block) {
                sample = static_cast<float>(0.5 * std::sin(phase));
                phase += 2.0 * nvjtest::kPiValue * 300.0 / 48000.0;
            }
            analyzer.push(block.data(), block.size());
            analyzer.advance(1.0 / 60.0);
        }
    };
    feed(low);
    CHECK(low.features().mid > 0.6f && low.features().bass < 0.4f);
    nvj::AudioAnalyzer moved;
    moved.configure(48000);
    moved.setCrossovers(400.0f, 2000.0f);
    feed(moved);
    CHECK(moved.features().bass > 0.6f && moved.features().mid < 0.4f);
    nvj::AudioAnalyzer clamped;
    clamped.configure(48000);
    clamped.setCrossovers(10.0f, 100.0f);
    feed(clamped);
    CHECK(clamped.features().mid > 0.5f);
}

void testChannels() {
    const float stereo[] = {1.0f, 0.0f, 0.5f, -0.5f, 0.0f, 1.0f};
    std::vector<float> out;
    nvj::appendChannel(stereo, 3, 2, nvj::AudioChannel::Mix, out);
    CHECK(out.size() == 3 && out[0] == 0.5f && out[1] == 0.0f && out[2] == 0.5f);
    out.clear();
    nvj::appendChannel(stereo, 3, 2, nvj::AudioChannel::Left, out);
    CHECK(out.size() == 3 && out[0] == 1.0f && out[1] == 0.5f && out[2] == 0.0f);
    out.clear();
    nvj::appendChannel(stereo, 3, 2, nvj::AudioChannel::Right, out);
    CHECK(out.size() == 3 && out[0] == 0.0f && out[1] == -0.5f && out[2] == 1.0f);
    out.clear();
    const float mono[] = {0.25f, -0.25f};
    nvj::appendChannel(mono, 2, 1, nvj::AudioChannel::Right, out);
    CHECK(out.size() == 2 && out[0] == 0.25f && out[1] == -0.25f);
    out.clear();
    const float surround[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    nvj::appendChannel(surround, 1, 6, nvj::AudioChannel::Mix, out);
    CHECK(out.size() == 1 && std::fabs(out[0] - 1.0f / 6.0f) < 1e-6f);
    for (int i = 0; i < nvj::kAudioChannelCount; ++i) {
        const nvj::AudioChannel channel = static_cast<nvj::AudioChannel>(i);
        CHECK(nvj::audioChannelFromName(nvj::audioChannelName(channel)) == channel);
    }
    CHECK(nvj::audioChannelFromName("nonsense") == nvj::AudioChannel::Mix);
    CHECK(nvj::audioChannelFromName(nullptr) == nvj::AudioChannel::Mix);
    nvj::AudioSystem system;
    system.setChannel(nvj::AudioChannel::Left);
    CHECK(system.channel() == nvj::AudioChannel::Left);
    CHECK(system.select(nvj::AudioSystem::demoId()));
    system.setChannel(nvj::AudioChannel::Right);
    CHECK(system.active() && system.selected() == nvj::AudioSystem::demoId());
}

void testCost() {
    Section busy = steady(128.0, 60.0);
    busy.hat = 0.5f;
    busy.sixteenths = true;
    const Track track = nvjtest::renderTrack({busy}, 60.0);
    double perFrame[2] = {0.0, 0.0};
    for (int improved = 0; improved < 2; ++improved) {
        nvj::AudioAnalyzer analyzer;
        analyzer.configure(nvjtest::kRate);
        analyzer.setActive(true);
        analyzer.setImproved(improved == 1);
        const size_t chunk = nvjtest::kRate / 60;
        size_t frames = 0;
        double worst = 0.0;
        const auto start = std::chrono::steady_clock::now();
        for (size_t at = 0; at + chunk <= track.samples.size(); at += chunk) {
            const auto frameStart = std::chrono::steady_clock::now();
            analyzer.push(track.samples.data() + at, chunk);
            analyzer.advance(1.0 / 60.0);
            worst = std::max(worst, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - frameStart).count());
            ++frames;
        }
        const double total = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        perFrame[improved] = total / static_cast<double>(frames);
        std::printf("cost per frame (%s): %.1f us on average, %.1f us worst\n", improved == 1 ? "improved" : "classic", perFrame[improved], worst);
    }
    CHECK(perFrame[1] - perFrame[0] < 1500.0);
}

}

int main() {
    testDefaultsAreClassic();
    testSteadyTempo();
    testFrameRates();
    testTempoChange();
    testOffbeats();
    testSilence();
    testDropout();
    testClipping();
    testLatency();
    testDjMix();
    testCrossovers();
    testChannels();
    testCost();
    return testing::finish();
}

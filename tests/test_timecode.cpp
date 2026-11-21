#include "check.hpp"
#include "core/cue_list.hpp"
#include "core/ltc.hpp"
#include "core/timecode.hpp"
#include "ltc_signal.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using nvj::FrameRate;
using nvj::Timecode;

namespace {

Timecode make(int h, int m, int s, int f, FrameRate rate) {
    Timecode t;
    t.hours = h;
    t.minutes = m;
    t.seconds = s;
    t.frames = f;
    t.rate = rate;
    return t;
}

bool same(const Timecode& a, const Timecode& b) {
    return a.hours == b.hours && a.minutes == b.minutes && a.seconds == b.seconds && a.frames == b.frames && a.rate == b.rate;
}

void testFrameMath() {
    CHECK(nvj::nominalFps(FrameRate::Fps24) == 24);
    CHECK(nvj::nominalFps(FrameRate::Fps25) == 25);
    CHECK(nvj::nominalFps(FrameRate::Fps2997Drop) == 30);
    CHECK(nvj::nominalFps(FrameRate::Fps30) == 30);
    CHECK(std::fabs(nvj::exactFps(FrameRate::Fps2997Drop) - 29.97002997) < 1.0e-6);
    CHECK(nvj::timecodeToFrames(make(0, 0, 59, 29, FrameRate::Fps2997Drop)) == 1799);
    CHECK(nvj::timecodeToFrames(make(0, 1, 0, 2, FrameRate::Fps2997Drop)) == 1800);
    CHECK(nvj::timecodeToFrames(make(0, 10, 0, 0, FrameRate::Fps2997Drop)) == 17982);
    CHECK(nvj::timecodeToFrames(make(1, 0, 0, 0, FrameRate::Fps2997Drop)) == 107892);
    CHECK(nvj::timecodeToFrames(make(1, 0, 0, 0, FrameRate::Fps30)) == 108000);
    CHECK(nvj::timecodeToFrames(make(1, 0, 0, 0, FrameRate::Fps25)) == 90000);
    CHECK(nvj::timecodeToFrames(make(0, 0, 1, 0, FrameRate::Fps24)) == 24);
    CHECK(std::fabs(nvj::timecodeToSeconds(make(1, 0, 0, 0, FrameRate::Fps2997Drop)) - 3600.0) < 0.01);
    CHECK(std::fabs(nvj::timecodeToSeconds(make(0, 0, 10, 0, FrameRate::Fps25)) - 10.0) < 1.0e-9);
    const FrameRate rates[4] = {FrameRate::Fps24, FrameRate::Fps25, FrameRate::Fps2997Drop, FrameRate::Fps30};
    for (FrameRate rate : rates) {
        bool roundTrip = true;
        for (long long frame = 0; frame < 220000; ++frame) {
            const Timecode t = nvj::timecodeFromFrames(frame, rate);
            if (!nvj::timecodeValid(t) || nvj::timecodeToFrames(t) != frame) {
                roundTrip = false;
                break;
            }
        }
        CHECK(roundTrip);
        const long long day = nvj::timecodeToFrames(make(23, 59, 59, nvj::nominalFps(rate) - 1, rate)) + 1;
        CHECK(same(nvj::timecodeFromFrames(day, rate), make(0, 0, 0, 0, rate)));
        CHECK(same(nvj::timecodeFromFrames(day - 1, rate), make(23, 59, 59, nvj::nominalFps(rate) - 1, rate)));
        CHECK(same(nvj::timecodeFromFrames(-1, rate), make(23, 59, 59, nvj::nominalFps(rate) - 1, rate)));
    }
    CHECK(same(nvj::timecodeFromFrames(1800, FrameRate::Fps2997Drop), make(0, 1, 0, 2, FrameRate::Fps2997Drop)));
    CHECK(same(nvj::timecodeFromFrames(1799, FrameRate::Fps2997Drop), make(0, 0, 59, 29, FrameRate::Fps2997Drop)));
    CHECK(same(nvj::timecodeFromSeconds(10.0, FrameRate::Fps25), make(0, 0, 10, 0, FrameRate::Fps25)));
    CHECK(same(nvj::timecodeFromSeconds(-5.0, FrameRate::Fps25), make(0, 0, 0, 0, FrameRate::Fps25)));
    CHECK(same(nvj::timecodeFromSeconds(NAN, FrameRate::Fps30), make(0, 0, 0, 0, FrameRate::Fps30)));
}

void testValidity() {
    CHECK(nvj::timecodeValid(make(0, 0, 0, 0, FrameRate::Fps25)));
    CHECK(nvj::timecodeValid(make(23, 59, 59, 24, FrameRate::Fps25)));
    CHECK(!nvj::timecodeValid(make(23, 59, 59, 25, FrameRate::Fps25)));
    CHECK(!nvj::timecodeValid(make(24, 0, 0, 0, FrameRate::Fps25)));
    CHECK(!nvj::timecodeValid(make(0, 60, 0, 0, FrameRate::Fps25)));
    CHECK(!nvj::timecodeValid(make(0, 0, 60, 0, FrameRate::Fps25)));
    CHECK(!nvj::timecodeValid(make(0, 0, 0, -1, FrameRate::Fps25)));
    CHECK(!nvj::timecodeValid(make(0, 1, 0, 0, FrameRate::Fps2997Drop)));
    CHECK(!nvj::timecodeValid(make(0, 1, 0, 1, FrameRate::Fps2997Drop)));
    CHECK(nvj::timecodeValid(make(0, 1, 0, 2, FrameRate::Fps2997Drop)));
    CHECK(nvj::timecodeValid(make(0, 10, 0, 0, FrameRate::Fps2997Drop)));
    CHECK(nvj::timecodeValid(make(0, 1, 1, 0, FrameRate::Fps2997Drop)));
    CHECK(nvj::timecodeValid(make(0, 1, 0, 0, FrameRate::Fps30)));
}

void testFormatAndParse() {
    CHECK(nvj::formatTimecode(make(1, 2, 3, 4, FrameRate::Fps25)) == "01:02:03:04");
    CHECK(nvj::formatTimecode(make(1, 2, 3, 4, FrameRate::Fps2997Drop)) == "01:02:03;04");
    CHECK(nvj::formatTimecode(make(0, 0, 0, 0, FrameRate::Fps30)) == "00:00:00:00");
    Timecode parsed;
    CHECK(nvj::parseTimecode("01:02:03:04", FrameRate::Fps25, parsed));
    CHECK(same(parsed, make(1, 2, 3, 4, FrameRate::Fps25)));
    CHECK(nvj::parseTimecode("01:02:03;04", FrameRate::Fps30, parsed));
    CHECK(same(parsed, make(1, 2, 3, 4, FrameRate::Fps30)));
    CHECK(nvj::parseTimecode("1:2:3:4", FrameRate::Fps24, parsed));
    CHECK(same(parsed, make(1, 2, 3, 4, FrameRate::Fps24)));
    CHECK(nvj::parseTimecode("00:00:10.12", FrameRate::Fps25, parsed));
    CHECK(parsed.seconds == 10 && parsed.frames == 12);
    const Timecode keep = parsed;
    CHECK(!nvj::parseTimecode("", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("01:02:03", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("01:02:03:04:05", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("aa:bb:cc:dd", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("01:02:03:25", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("01:02:03:04x", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("01-02-03-04", FrameRate::Fps25, parsed));
    CHECK(!nvj::parseTimecode("00:01:00;00", FrameRate::Fps2997Drop, parsed));
    CHECK(!nvj::parseTimecode("0001:02:03:04", FrameRate::Fps25, parsed));
    CHECK(same(parsed, keep));
}

void testMtcSingle() {
    const FrameRate rates[4] = {FrameRate::Fps24, FrameRate::Fps25, FrameRate::Fps2997Drop, FrameRate::Fps30};
    for (FrameRate rate : rates) {
        nvj::MtcDecoder decoder;
        const Timecode t = make(12, 34, 56, 10, rate);
        const std::array<int, 8> data = ltctest::quarterFrames(t);
        nvj::MtcFix fix;
        bool done = false;
        for (int piece = 0; piece < 8; ++piece) {
            const bool ready = decoder.pushQuarterFrame(data[static_cast<size_t>(piece)], fix);
            CHECK(ready == (piece == 7));
            done = done || ready;
        }
        CHECK(done);
        CHECK(same(fix.timecode, t));
        CHECK(std::fabs(fix.frames - (static_cast<double>(nvj::timecodeToFrames(t)) + 1.75)) < 1.0e-9);
        CHECK(std::fabs(fix.seconds - fix.frames / nvj::exactFps(rate)) < 1.0e-9);
        CHECK(decoder.dropped() == 0);
    }
}

void testMtcStream() {
    nvj::MtcDecoder decoder;
    const FrameRate rate = FrameRate::Fps25;
    long long frame = nvj::timecodeToFrames(make(1, 59, 59, 10, rate));
    int completed = 0;
    bool consistent = true;
    nvj::MtcFix fix;
    for (int sequence = 0; sequence < 40; ++sequence) {
        const Timecode t = nvj::timecodeFromFrames(frame, rate);
        const std::array<int, 8> data = ltctest::quarterFrames(t);
        for (int piece = 0; piece < 8; ++piece) {
            if (decoder.pushQuarterFrame(data[static_cast<size_t>(piece)], fix)) {
                ++completed;
                if (nvj::timecodeToFrames(fix.timecode) != frame) {
                    consistent = false;
                }
            }
        }
        frame += 2;
    }
    CHECK(completed == 40);
    CHECK(consistent);
    CHECK(decoder.dropped() == 0);
    CHECK(fix.timecode.hours == 2);
}

void testMtcErrors() {
    nvj::MtcDecoder decoder;
    nvj::MtcFix fix;
    const Timecode t = make(0, 0, 5, 3, FrameRate::Fps25);
    const std::array<int, 8> data = ltctest::quarterFrames(t);
    for (int piece = 3; piece < 8; ++piece) {
        CHECK(!decoder.pushQuarterFrame(data[static_cast<size_t>(piece)], fix));
    }
    CHECK(decoder.dropped() == 0);
    bool completed = false;
    for (int piece = 0; piece < 8; ++piece) {
        if (piece == 4) {
            continue;
        }
        completed = completed || decoder.pushQuarterFrame(data[static_cast<size_t>(piece)], fix);
    }
    CHECK(!completed);
    CHECK(decoder.dropped() >= 1);
    const unsigned long long before = decoder.dropped();
    completed = false;
    for (int piece = 0; piece < 8; ++piece) {
        completed = decoder.pushQuarterFrame(data[static_cast<size_t>(piece)], fix) || completed;
    }
    CHECK(completed);
    CHECK(same(fix.timecode, t));
    CHECK(decoder.dropped() == before);
    Timecode bad = t;
    bad.frames = 31;
    const std::array<int, 8> badData = ltctest::quarterFrames(bad);
    completed = false;
    for (int piece = 0; piece < 8; ++piece) {
        completed = decoder.pushQuarterFrame(badData[static_cast<size_t>(piece)], fix) || completed;
    }
    CHECK(!completed);
    CHECK(decoder.dropped() == before + 1);
    decoder.pushQuarterFrame(data[0], fix);
    decoder.pushQuarterFrame(data[1], fix);
    decoder.reset();
    CHECK(decoder.pieces() == 0);
    completed = false;
    for (int piece = 0; piece < 8; ++piece) {
        completed = decoder.pushQuarterFrame(data[static_cast<size_t>(piece)], fix) || completed;
    }
    CHECK(completed);
    CHECK(!decoder.pushQuarterFrame(0x70, fix));
    CHECK(!decoder.pushQuarterFrame(0x7F, fix));
}

void testTracker() {
    nvj::TimecodeTracker tracker;
    CHECK(!tracker.running());
    CHECK(!tracker.everSeen());
    CHECK(tracker.position() == 0.0);
    double position = 100.0;
    for (int i = 0; i < 50; ++i) {
        tracker.advance(0.04);
        position += 0.04;
        tracker.report(position, 0.0, FrameRate::Fps25);
    }
    CHECK(tracker.everSeen());
    CHECK(tracker.running());
    CHECK(!tracker.takeJump());
    CHECK(std::fabs(tracker.position() - position) < 1.0e-9);
    CHECK(tracker.fixes() == 50);
    tracker.advance(0.1);
    CHECK(std::fabs(tracker.position() - (position + 0.1)) < 1.0e-9);
    tracker.advance(0.2);
    CHECK(std::fabs(tracker.position() - (position + 0.15)) < 1.0e-9);
    CHECK(tracker.running());
    tracker.advance(0.3);
    CHECK(!tracker.running());
    CHECK(std::fabs(tracker.position() - (position + 0.15)) < 1.0e-9);
    tracker.report(position + 0.6 + 7.0, 0.0, FrameRate::Fps25);
    CHECK(!tracker.takeJump());
    CHECK(tracker.running());
    position = position + 0.6 + 7.0;
    tracker.advance(0.04);
    tracker.report(position + 0.04, 0.0, FrameRate::Fps25);
    CHECK(!tracker.takeJump());
    tracker.advance(0.04);
    tracker.report(position + 5.0, 0.0, FrameRate::Fps25);
    CHECK(tracker.takeJump());
    CHECK(!tracker.takeJump());
    CHECK(tracker.jumps() == 1);
    position += 5.0;
    tracker.advance(0.04);
    tracker.report(position + 0.04, 0.0, FrameRate::Fps30);
    CHECK(tracker.takeJump());
    CHECK(tracker.rate() == FrameRate::Fps30);
    position += 0.04;
    tracker.advance(0.1);
    tracker.report(position + 0.1 - 0.06, 0.06, FrameRate::Fps30);
    CHECK(!tracker.takeJump());
    tracker.report(NAN, 0.0, FrameRate::Fps30);
    tracker.report(-3.0, 0.0, FrameRate::Fps30);
    CHECK(tracker.fixes() == 55);
    tracker.reset();
    CHECK(!tracker.everSeen());
    CHECK(!tracker.running());
}

struct Check {
    size_t count = 0;
    int wrong = 0;
    int wrongRate = 0;
    double maxError = 0.0;
    double maxFpsError = 0.0;
    bool ordered = true;
};

Check verify(const std::vector<nvj::LtcFrame>& frames, const Timecode& start, double samplesPerFrame, double realFps) {
    Check result;
    result.count = frames.size();
    double previousEnd = -1.0;
    for (const nvj::LtcFrame& frame : frames) {
        const long long index = std::llround(frame.endSample / samplesPerFrame) - 1;
        const Timecode expected = nvj::timecodeFromFrames(nvj::timecodeToFrames(start) + index, start.rate);
        if (!same(expected, frame.timecode)) {
            ++result.wrong;
        }
        if (frame.timecode.rate != start.rate) {
            ++result.wrongRate;
        }
        result.maxError = std::max(result.maxError, std::fabs(frame.endSample - static_cast<double>(index + 1) * samplesPerFrame));
        result.maxFpsError = std::max(result.maxFpsError, std::fabs(frame.fps - realFps) / realFps);
        if (frame.endSample <= previousEnd) {
            result.ordered = false;
        }
        previousEnd = frame.endSample;
    }
    return result;
}

std::vector<nvj::LtcFrame> decode(const std::vector<float>& signal, int sampleRate, size_t chunk, nvj::LtcDecoder* keep = nullptr) {
    nvj::LtcDecoder local(sampleRate);
    nvj::LtcDecoder& decoder = keep ? *keep : local;
    decoder.setSampleRate(sampleRate);
    std::vector<nvj::LtcFrame> frames;
    for (size_t position = 0; position < signal.size(); position += chunk) {
        const size_t count = std::min(chunk, signal.size() - position);
        decoder.push(signal.data() + position, count, frames);
    }
    return frames;
}

void testLtcRates() {
    const FrameRate rates[4] = {FrameRate::Fps24, FrameRate::Fps25, FrameRate::Fps2997Drop, FrameRate::Fps30};
    const int sampleRates[4] = {44100, 48000, 96000, 22050};
    for (FrameRate rate : rates) {
        for (int sampleRate : sampleRates) {
            ltctest::Options options;
            options.sampleRate = sampleRate;
            const Timecode start = rate == FrameRate::Fps2997Drop ? make(0, 0, 58, 20, rate) : make(10, 20, 30, 5, rate);
            ltctest::Generator generator(options, start);
            const double seconds = 3.0;
            const std::vector<float> signal = generator.render(static_cast<size_t>(seconds * sampleRate));
            const std::vector<nvj::LtcFrame> frames = decode(signal, sampleRate, 480);
            const double fps = nvj::exactFps(rate);
            const double expectedCount = seconds * fps;
            const Check check = verify(frames, start, generator.samplesPerFrame(), fps);
            const double tolerance = sampleRate >= 44100 ? 1.5 : 3.0;
            std::printf("rate %s at %d Hz: %zu frames, error %.2f samples, fps error %.5f\n", nvj::frameRateName(rate), sampleRate, check.count, check.maxError, check.maxFpsError);
            CHECK(static_cast<double>(check.count) >= expectedCount - 4.0);
            CHECK(static_cast<double>(check.count) <= expectedCount);
            CHECK(check.wrong == 0);
            CHECK(check.wrongRate == 0);
            CHECK(check.ordered);
            CHECK(check.maxError <= tolerance);
            CHECK(check.maxFpsError < 0.01);
        }
    }
}

void testLtcDropFrameCrossing() {
    ltctest::Options options;
    const Timecode start = make(0, 0, 59, 0, FrameRate::Fps2997Drop);
    ltctest::Generator generator(options, start);
    const std::vector<float> signal = generator.render(static_cast<size_t>(4.0 * options.sampleRate));
    const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 1024);
    bool sawMinute = false;
    bool sawSkipped = false;
    for (const nvj::LtcFrame& frame : frames) {
        if (frame.timecode.minutes == 1 && frame.timecode.seconds == 0 && frame.timecode.frames == 2) {
            sawMinute = true;
        }
        if (frame.timecode.minutes == 1 && frame.timecode.seconds == 0 && frame.timecode.frames < 2) {
            sawSkipped = true;
        }
    }
    CHECK(sawMinute);
    CHECK(!sawSkipped);
    CHECK(verify(frames, start, generator.samplesPerFrame(), nvj::exactFps(FrameRate::Fps2997Drop)).wrong == 0);
}

void testLtcChunking() {
    ltctest::Options options;
    const Timecode start = make(3, 4, 5, 6, FrameRate::Fps25);
    ltctest::Generator generator(options, start);
    const std::vector<float> signal = generator.render(static_cast<size_t>(2.0 * options.sampleRate));
    const std::vector<nvj::LtcFrame> reference = decode(signal, options.sampleRate, signal.size());
    const size_t chunks[4] = {1, 7, 333, 4096};
    CHECK(reference.size() > 40);
    for (size_t chunk : chunks) {
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, chunk);
        bool equal = frames.size() == reference.size();
        for (size_t i = 0; equal && i < frames.size(); ++i) {
            equal = same(frames[i].timecode, reference[i].timecode) && std::fabs(frames[i].endSample - reference[i].endSample) < 1.0e-6;
        }
        CHECK(equal);
    }
}

void testLtcSignalQuality() {
    const Timecode start = make(0, 15, 0, 0, FrameRate::Fps25);
    {
        ltctest::Options options;
        options.smoothing = 0.35f;
        ltctest::Generator generator(options, start);
        const std::vector<float> signal = generator.render(static_cast<size_t>(3.0 * options.sampleRate));
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 512);
        const Check check = verify(frames, start, generator.samplesPerFrame(), 25.0);
        std::printf("filtered: %zu frames, error %.2f samples\n", check.count, check.maxError);
        CHECK(check.count >= 70);
        CHECK(check.wrong == 0);
        CHECK(check.maxError < 6.0);
    }
    {
        ltctest::Options options;
        options.offset = 0.15f;
        options.noise = 0.08f;
        options.smoothing = 0.5f;
        ltctest::Generator generator(options, start);
        const std::vector<float> signal = generator.render(static_cast<size_t>(3.0 * options.sampleRate));
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 512);
        const Check check = verify(frames, start, generator.samplesPerFrame(), 25.0);
        std::printf("noisy with offset: %zu frames, error %.2f samples\n", check.count, check.maxError);
        CHECK(check.count >= 70);
        CHECK(check.wrong == 0);
    }
    {
        ltctest::Options options;
        options.inverted = true;
        ltctest::Generator generator(options, start);
        const std::vector<float> signal = generator.render(static_cast<size_t>(2.0 * options.sampleRate));
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 512);
        CHECK(frames.size() >= 45);
        CHECK(verify(frames, start, generator.samplesPerFrame(), 25.0).wrong == 0);
    }
    {
        ltctest::Options options;
        options.amplitude = 0.01f;
        ltctest::Generator generator(options, start);
        const std::vector<float> signal = generator.render(static_cast<size_t>(2.0 * options.sampleRate));
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 512);
        CHECK(frames.size() >= 45);
        CHECK(verify(frames, start, generator.samplesPerFrame(), 25.0).wrong == 0);
    }
    {
        ltctest::Options options;
        options.speed = 1.02;
        ltctest::Generator generator(options, start);
        const std::vector<float> signal = generator.render(static_cast<size_t>(2.0 * options.sampleRate));
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 512);
        const Check check = verify(frames, start, generator.samplesPerFrame(), 25.0 * 1.02);
        CHECK(check.count >= 45);
        CHECK(check.wrong == 0);
        CHECK(check.maxFpsError < 0.01);
    }
    {
        ltctest::Options options;
        options.speed = 0.985;
        ltctest::Generator generator(options, start);
        const std::vector<float> signal = generator.render(static_cast<size_t>(2.0 * options.sampleRate));
        const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 512);
        const Check check = verify(frames, start, generator.samplesPerFrame(), 25.0 * 0.985);
        CHECK(check.count >= 42);
        CHECK(check.wrong == 0);
        CHECK(check.wrongRate == 0);
        CHECK(check.maxFpsError < 0.01);
    }
}

void testLtcNoSignal() {
    std::vector<nvj::LtcFrame> frames;
    nvj::LtcDecoder decoder(48000);
    std::vector<float> silence(48000, 0.0f);
    decoder.push(silence.data(), silence.size(), frames);
    CHECK(frames.empty());
    CHECK(!decoder.signalSeen());
    std::vector<float> faint(48000);
    unsigned seed = 7;
    for (float& value : faint) {
        seed = seed * 1664525u + 1013904223u;
        value = 0.002f * (static_cast<float>(seed >> 8) / static_cast<float>(1u << 23) - 1.0f);
    }
    decoder.push(faint.data(), faint.size(), frames);
    CHECK(frames.empty());
    CHECK(!decoder.signalSeen());
    std::vector<float> noise(48000 * 5);
    for (float& value : noise) {
        seed = seed * 1664525u + 1013904223u;
        value = 0.6f * (static_cast<float>(seed >> 8) / static_cast<float>(1u << 23) - 1.0f);
    }
    decoder.push(noise.data(), noise.size(), frames);
    CHECK(frames.empty());
    CHECK(decoder.signalSeen());
    std::vector<float> tone(48000 * 2);
    for (size_t i = 0; i < tone.size(); ++i) {
        tone[i] = 0.5f * std::sin(static_cast<float>(i) * 0.06f);
    }
    decoder.push(tone.data(), tone.size(), frames);
    CHECK(frames.empty());
    std::vector<float> invalid(10, NAN);
    decoder.push(invalid.data(), invalid.size(), frames);
    decoder.push(nullptr, 0, frames);
    CHECK(frames.empty());
}

void testLtcCorruption() {
    ltctest::Options options;
    const Timecode start = make(0, 0, 0, 0, FrameRate::Fps25);
    ltctest::Generator generator(options, start);
    generator.corruptWord(30);
    generator.corruptWord(60);
    const std::vector<float> signal = generator.render(static_cast<size_t>(4.0 * options.sampleRate));
    nvj::LtcDecoder decoder(options.sampleRate);
    const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 777, &decoder);
    const Check check = verify(frames, start, generator.samplesPerFrame(), 25.0);
    CHECK(check.wrong == 0);
    CHECK(check.count >= 90);
    CHECK(decoder.resyncs() >= 4);
    bool recovered = false;
    for (const nvj::LtcFrame& frame : frames) {
        if (std::llround(frame.endSample / generator.samplesPerFrame()) - 1 > 62) {
            recovered = true;
        }
    }
    CHECK(recovered);
}

void testLtcJumpAndGap() {
    ltctest::Options options;
    const Timecode start = make(1, 0, 0, 0, FrameRate::Fps25);
    ltctest::Generator generator(options, start);
    std::vector<float> signal;
    generator.render(static_cast<size_t>(1.0 * options.sampleRate), signal);
    const Timecode target = make(5, 30, 10, 12, FrameRate::Fps25);
    generator.jumpTo(target);
    generator.render(static_cast<size_t>(1.0 * options.sampleRate), signal);
    nvj::LtcDecoder decoder(options.sampleRate);
    const std::vector<nvj::LtcFrame> frames = decode(signal, options.sampleRate, 1000, &decoder);
    CHECK(decoder.resyncs() == 1);
    size_t before = 0;
    size_t after = 0;
    for (const nvj::LtcFrame& frame : frames) {
        if (frame.timecode.hours == 1) {
            ++before;
        }
        if (frame.timecode.hours == 5) {
            ++after;
        }
    }
    CHECK(before >= 20);
    CHECK(after >= 20);
    CHECK(!frames.empty() && frames.back().timecode.hours == 5);

    ltctest::Generator second(options, start);
    std::vector<float> gapped = second.render(static_cast<size_t>(3.0 * options.sampleRate));
    const size_t gapStart = static_cast<size_t>(1.0 * options.sampleRate);
    const size_t gapEnd = static_cast<size_t>(2.0 * options.sampleRate);
    for (size_t i = gapStart; i < gapEnd; ++i) {
        gapped[i] = 0.0f;
    }
    nvj::LtcDecoder gapDecoder(options.sampleRate);
    const std::vector<nvj::LtcFrame> gapFrames = decode(gapped, options.sampleRate, 480, &gapDecoder);
    const Check check = verify(gapFrames, start, second.samplesPerFrame(), 25.0);
    CHECK(check.wrong == 0);
    bool afterGap = false;
    bool insideGap = false;
    for (const nvj::LtcFrame& frame : gapFrames) {
        if (frame.endSample > static_cast<double>(gapStart) + 4000.0 && frame.endSample < static_cast<double>(gapEnd) - 1.0) {
            insideGap = true;
        }
        if (frame.endSample > static_cast<double>(gapEnd) + 4.0 * second.samplesPerFrame()) {
            afterGap = true;
        }
    }
    CHECK(!insideGap);
    CHECK(afterGap);
    CHECK(gapDecoder.frames() == gapFrames.size());
}

void testCues() {
    std::vector<nvj::Cue> cues;
    nvj::Cue a;
    a.seconds = 4.0;
    a.target = "master/level";
    a.value = 0.5f;
    nvj::Cue b;
    b.seconds = 1.0;
    b.target = "tap";
    nvj::Cue c;
    c.seconds = 4.0;
    c.target = "resync";
    cues.push_back(a);
    cues.push_back(b);
    cues.push_back(c);
    nvj::sortCues(cues);
    CHECK(cues[0].target == "tap");
    CHECK(cues[1].target == "master/level" && cues[2].target == "resync");
    CHECK(nvj::dueCues(cues, 0.0, 0.9).empty());
    CHECK(nvj::dueCues(cues, 0.9, 1.0).size() == 1);
    CHECK(nvj::dueCues(cues, 1.0, 1.5).empty());
    CHECK(nvj::dueCues(cues, 3.0, 4.0).size() == 2);
    CHECK(nvj::dueCues(cues, 4.0, 5.0).empty());
    CHECK(nvj::dueCues(cues, 5.0, 4.0).empty());
    CHECK(nvj::dueCues(cues, 2.0, 2.0).empty());

    const std::string json = nvj::cuesToJson(cues);
    std::vector<nvj::Cue> loaded;
    CHECK(nvj::cuesFromJson(json, loaded));
    CHECK(loaded.size() == 3);
    CHECK(loaded.size() == 3 && loaded[1].target == "master/level" && std::fabs(loaded[1].value - 0.5f) < 1.0e-6f);
    std::vector<nvj::Cue> untouched = cues;
    CHECK(!nvj::cuesFromJson("not json", untouched));
    CHECK(!nvj::cuesFromJson("{\"cues\": 5}", untouched));
    CHECK(untouched.size() == 3);
    std::vector<nvj::Cue> filtered;
    CHECK(nvj::cuesFromJson("{\"cues\":[{\"seconds\":2,\"target\":\"tap\",\"value\":7},{\"seconds\":-1,\"target\":\"tap\"},{\"seconds\":1,\"target\":\"\"},{\"target\":\"tap\"},5,{\"seconds\":0.5,\"target\":\"resync\"}]}", filtered));
    CHECK(filtered.size() == 2);
    CHECK(filtered.size() == 2 && filtered[0].target == "resync" && filtered[1].value == 1.0f);
    std::string many = "{\"cues\":[";
    for (int i = 0; i < 600; ++i) {
        many += std::string(i ? "," : "") + "{\"seconds\":" + std::to_string(i) + ",\"target\":\"tap\"}";
    }
    many += "]}";
    CHECK(nvj::cuesFromJson(many, filtered));
    CHECK(filtered.size() == nvj::kMaxCues);

    nvj::CueRunner runner;
    int fired[3] = {0, 0, 0};
    for (int i = 0; i <= 125; ++i) {
        for (size_t index : runner.advance(cues, true, i * 0.04)) {
            ++fired[index];
        }
    }
    CHECK(fired[0] == 1 && fired[1] == 1 && fired[2] == 1);
    CHECK(runner.tracking());
    CHECK(runner.advance(cues, true, 60.0).empty());
    CHECK(runner.skipped() == 1);
    CHECK(runner.advance(cues, true, 0.5).empty());
    CHECK(runner.skipped() == 2);
    for (int i = 0; i <= 30; ++i) {
        for (size_t index : runner.advance(cues, true, 0.5 + i * 0.04)) {
            ++fired[index];
        }
    }
    CHECK(fired[0] == 2);
    CHECK(runner.advance(cues, false, 3.9).empty());
    CHECK(!runner.tracking());
    CHECK(runner.advance(cues, true, 4.2).empty());
    CHECK(runner.advance(cues, true, std::nan("")).empty());
    CHECK(!runner.tracking());
    runner.reset();
    CHECK(!runner.tracking());
}

void testLtcReset() {
    ltctest::Options options;
    const Timecode start = make(0, 0, 0, 0, FrameRate::Fps30);
    ltctest::Generator generator(options, start);
    const std::vector<float> signal = generator.render(static_cast<size_t>(1.0 * options.sampleRate));
    nvj::LtcDecoder decoder(options.sampleRate);
    const std::vector<nvj::LtcFrame> first = decode(signal, options.sampleRate, 480, &decoder);
    CHECK(decoder.frames() == first.size());
    CHECK(decoder.signalSeen());
    decoder.reset();
    CHECK(decoder.samplesPushed() == 0.0);
    CHECK(!decoder.signalSeen());
    const std::vector<nvj::LtcFrame> second = decode(signal, options.sampleRate, 480, &decoder);
    CHECK(second.size() == first.size());
    decoder.setSampleRate(96000);
    CHECK(decoder.sampleRate() == 96000);
    CHECK(decoder.samplesPushed() == 0.0);
    decoder.setSampleRate(100);
    CHECK(decoder.sampleRate() >= 8000);
}

}

int main() {
    testFrameMath();
    testValidity();
    testFormatAndParse();
    testMtcSingle();
    testMtcStream();
    testMtcErrors();
    testTracker();
    testLtcRates();
    testLtcDropFrameCrossing();
    testLtcChunking();
    testLtcSignalQuality();
    testLtcNoSignal();
    testLtcCorruption();
    testLtcJumpAndGap();
    testLtcReset();
    testCues();
    return testing::finish();
}

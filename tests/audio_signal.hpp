#pragma once

#include "audio/audio_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nvjtest {

constexpr int kRate = 48000;
constexpr double kPiValue = 3.14159265358979323846;

struct Section {
    double start = 0.0;
    double end = 0.0;
    double bpm = 128.0;
    double offset = 0.0;
    float kick = 0.8f;
    float hat = 0.0f;
    bool offbeatHats = false;
    bool sixteenths = false;
    float pad = 0.0f;
};

struct Track {
    std::vector<float> samples;
    std::vector<double> kicks;
};

class Noise {
public:
    float next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(state_)) / 2147483648.0f;
    }

private:
    std::uint32_t state_ = 12345u;
};

inline void addKick(std::vector<float>& out, double time, float amplitude) {
    const size_t first = static_cast<size_t>(std::max(0.0, time) * kRate);
    const size_t length = static_cast<size_t>(0.25 * kRate);
    double phase = 0.0;
    for (size_t i = 0; i < length && first + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double frequency = 48.0 + 110.0 * std::exp(-t / 0.025);
        phase += 2.0 * kPiValue * frequency / kRate;
        const double envelope = std::exp(-t / 0.09) * std::min(1.0, t / 0.001);
        out[first + i] += static_cast<float>(amplitude * envelope * std::sin(phase));
    }
}

inline void addHat(std::vector<float>& out, double time, float amplitude, Noise& noise) {
    const size_t first = static_cast<size_t>(std::max(0.0, time) * kRate);
    const size_t length = static_cast<size_t>(0.03 * kRate);
    float previous = 0.0f;
    for (size_t i = 0; i < length && first + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const float raw = noise.next();
        const float high = raw - previous;
        previous = raw;
        out[first + i] += static_cast<float>(amplitude * std::exp(-t / 0.008)) * high * 0.5f;
    }
}

inline Track renderTrack(const std::vector<Section>& sections, double seconds) {
    Track track;
    track.samples.assign(static_cast<size_t>(seconds * kRate), 0.0f);
    Noise noise;
    for (const Section& section : sections) {
        const double period = 60.0 / section.bpm;
        for (double t = section.start + section.offset; t < section.end; t += period) {
            if (section.kick > 0.0f) {
                addKick(track.samples, t, section.kick);
                track.kicks.push_back(t);
            }
            if (section.offbeatHats && section.hat > 0.0f) {
                addHat(track.samples, t + period * 0.5, section.hat, noise);
            }
            if (section.sixteenths && section.hat > 0.0f) {
                for (int s = 1; s < 4; ++s) {
                    addHat(track.samples, t + period * 0.25 * s, section.hat * 0.5f, noise);
                }
            }
        }
        if (section.pad > 0.0f) {
            const size_t from = static_cast<size_t>(section.start * kRate);
            const size_t to = std::min(track.samples.size(), static_cast<size_t>(section.end * kRate));
            for (size_t i = from; i < to; ++i) {
                const double t = static_cast<double>(i) / kRate;
                track.samples[i] += section.pad * static_cast<float>(0.5 * std::sin(2.0 * kPiValue * 220.0 * t) + 0.3 * std::sin(2.0 * kPiValue * 330.0 * t) + 0.2 * std::sin(2.0 * kPiValue * 1320.0 * t));
            }
        }
    }
    return track;
}

inline void mixInto(std::vector<float>& target, const std::vector<float>& source, double from, double to, bool fadeIn) {
    const size_t a = static_cast<size_t>(from * kRate);
    const size_t b = std::min(target.size(), static_cast<size_t>(to * kRate));
    for (size_t i = a; i < b && i < source.size(); ++i) {
        const double x = static_cast<double>(i - a) / static_cast<double>(std::max<size_t>(1, b - a));
        const float gain = static_cast<float>(fadeIn ? x : 1.0 - x);
        target[i] += source[i] * gain;
    }
}

struct Frame {
    double time = 0.0;
    nvj::AudioFeatures features;
};

inline std::vector<Frame> runAnalyzer(nvj::AudioAnalyzer& analyzer, const std::vector<float>& samples, int fps = 60) {
    std::vector<Frame> frames;
    const size_t chunk = static_cast<size_t>(kRate / fps);
    const double dt = 1.0 / fps;
    double clock = 0.0;
    for (size_t at = 0; at + chunk <= samples.size(); at += chunk) {
        analyzer.push(samples.data() + at, chunk);
        analyzer.advance(dt);
        clock += dt;
        frames.push_back({clock, analyzer.features()});
    }
    return frames;
}

}

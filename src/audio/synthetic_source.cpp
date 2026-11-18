#include "audio/synthetic_source.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr double kTwoPi = 6.283185307179586;
const float kBassNotes[8] = {55.0f, 55.0f, 65.41f, 55.0f, 73.42f, 55.0f, 65.41f, 49.0f};

}

void SyntheticSource::setBpm(float bpm) {
    bpm_ = std::clamp(bpm, 40.0f, 240.0f);
    samplesPerBeat_ = 60.0 / static_cast<double>(bpm_) * static_cast<double>(kSampleRate);
}

void SyntheticSource::reset() {
    position_ = 0.0;
    kickPhase_ = 0.0;
    bassPhase_ = 0.0;
    noiseLow_ = 0.0f;
}

float SyntheticSource::next() {
    if (samplesPerBeat_ <= 0.0) {
        setBpm(bpm_);
    }
    const double beatPosition = position_ / samplesPerBeat_;
    const long long beat = static_cast<long long>(std::floor(beatPosition));
    const double inBeat = beatPosition - static_cast<double>(beat);
    const double secondsPerBeat = 60.0 / static_cast<double>(bpm_);
    const double seconds = inBeat * secondsPerBeat;

    noiseState_ = noiseState_ * 1664525u + 1013904223u;
    const float noise = static_cast<float>(static_cast<std::int32_t>(noiseState_ >> 8) - 8388608) / 8388608.0f;
    noiseLow_ += (noise - noiseLow_) * 0.35f;
    const float highPassed = noise - noiseLow_;

    const double kickFrequency = 48.0 + 110.0 * std::exp(-seconds * 38.0);
    kickPhase_ += kTwoPi * kickFrequency / static_cast<double>(kSampleRate);
    if (kickPhase_ > kTwoPi) {
        kickPhase_ -= kTwoPi;
    }
    const float kick = static_cast<float>(std::sin(kickPhase_) * std::exp(-seconds * 11.0)) * 0.8f;

    const double hatSeconds = (inBeat < 0.5 ? inBeat + 0.5 : inBeat - 0.5) * secondsPerBeat;
    const float hat = highPassed * static_cast<float>(std::exp(-hatSeconds * 55.0)) * 0.22f;

    const float clap = (beat % 2 == 1) ? noise * static_cast<float>(std::exp(-seconds * 22.0)) * 0.25f : 0.0f;

    const float note = kBassNotes[static_cast<size_t>(beat % 8)];
    bassPhase_ += kTwoPi * static_cast<double>(note) / static_cast<double>(kSampleRate);
    if (bassPhase_ > kTwoPi) {
        bassPhase_ -= kTwoPi;
    }
    const double bassGate = inBeat > 0.5 ? std::exp(-(inBeat - 0.5) * 3.0) : 0.35 * std::exp(-inBeat * 4.0);
    const float saw = static_cast<float>(bassPhase_ / kTwoPi) * 2.0f - 1.0f;
    const float bass = (saw * 0.5f + static_cast<float>(std::sin(bassPhase_)) * 0.5f) * static_cast<float>(bassGate) * 0.22f;

    const float lead = static_cast<float>(std::sin(bassPhase_ * 8.0)) * 0.04f * static_cast<float>(std::exp(-inBeat * 2.0));

    position_ += 1.0;
    if (position_ >= samplesPerBeat_ * 64.0) {
        position_ -= samplesPerBeat_ * 64.0;
    }
    return std::clamp(kick + hat + clap + bass + lead, -1.0f, 1.0f);
}

void SyntheticSource::generate(float* out, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        out[i] = next();
    }
}

}

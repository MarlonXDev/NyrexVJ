#pragma once

#include <array>
#include <cstdint>

namespace nvj {

constexpr int kSpectrumBins = 128;
constexpr int kWaveSamples = 128;

struct AudioFeatures {
    bool active = false;
    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float level = 0.0f;
    float kick = 0.0f;
    float bpm = 0.0f;
    float bpmConfidence = 0.0f;
    bool beatNow = false;
    std::uint32_t beatCount = 0;
    float beatConfidence = 0.0f;
    float beatPhase = -1.0f;
    float onsetStrength = 0.0f;
    bool tracking = false;
    std::array<float, kSpectrumBins> spectrum{};
    std::array<float, kWaveSamples> wave{};

    float band(int band) const;
};

AudioFeatures virtualAudio(double beat, double time);

}

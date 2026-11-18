#pragma once

#include "audio/audio_features.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace nvj {

class AudioAnalyzer {
public:
    AudioAnalyzer();

    void configure(int sampleRate);
    void reset();
    void push(const float* mono, size_t count);
    void advance(double dt);

    void setGain(float gain) { gain_ = gain; }
    void setSmoothing(float smoothing) { smoothing_ = smoothing; }
    void setBeatSensitivity(float sensitivity) { sensitivity_ = sensitivity; }
    void setActive(bool active) { features_.active = active; }

    const AudioFeatures& features() const { return features_; }

private:
    static constexpr int kFftSize = 2048;

    void computeSpectrum();
    void detectBeat(double dt, float lowEnergy);
    void estimateTempo();

    int sampleRate_ = 48000;
    float gain_ = 1.0f;
    float smoothing_ = 0.5f;
    float sensitivity_ = 0.5f;
    std::vector<float> ring_;
    size_t writeIndex_ = 0;
    size_t pushedSinceAdvance_ = 0;
    std::vector<float> window_;
    std::vector<float> cosTable_;
    std::vector<float> sinTable_;
    std::vector<int> bitReverse_;
    std::vector<float> re_;
    std::vector<float> im_;
    std::vector<float> magnitude_;
    std::array<float, kSpectrumBins> rawSpectrum_{};
    std::array<int, kSpectrumBins + 1> binEdges_{};
    std::array<float, kSpectrumBins> binFrequency_{};
    float agcPeak_ = 0.5f;
    float previousLow_ = 0.0f;
    float fluxMean_ = 0.0f;
    float fluxDeviation_ = 0.0f;
    double clock_ = 0.0;
    double lastBeatTime_ = -10.0;
    std::deque<double> intervals_;
    float bpmSmoothed_ = 0.0f;
    AudioFeatures features_;
};

}

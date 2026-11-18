#pragma once

#include <cstddef>
#include <cstdint>

namespace nvj {

class SyntheticSource {
public:
    static constexpr int kSampleRate = 48000;

    void setBpm(float bpm);
    void reset();
    void generate(float* out, size_t frames);

private:
    float next();

    float bpm_ = 128.0f;
    double samplesPerBeat_ = 0.0;
    double position_ = 0.0;
    double kickPhase_ = 0.0;
    double bassPhase_ = 0.0;
    std::uint32_t noiseState_ = 22222u;
    float noiseLow_ = 0.0f;
};

}

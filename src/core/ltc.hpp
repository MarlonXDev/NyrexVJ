#pragma once

#include "core/timecode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nvj {

struct LtcFrame {
    Timecode timecode;
    double endSample = 0.0;
    double fps = 0.0;
};

class LtcDecoder {
public:
    explicit LtcDecoder(int sampleRate = 48000);

    void setSampleRate(int sampleRate);
    int sampleRate() const { return sampleRate_; }
    void reset();
    void push(const float* samples, size_t count, std::vector<LtcFrame>& out);

    double samplesPushed() const { return static_cast<double>(sampleCount_); }
    double lastFrameEnd() const { return lastEnd_; }
    unsigned long long frames() const { return frames_; }
    unsigned long long rejected() const { return rejected_; }
    unsigned long long resyncs() const { return resyncs_; }
    bool signalSeen() const { return peak_ > 0.003f; }

private:
    void onTransition(double time);
    void onBit(int bit, double time);
    void onWord(double time);
    void resetBits();
    int bitAt(int index) const;
    int field(int start, int length) const;

    int sampleRate_ = 48000;
    long long sampleCount_ = 0;
    float previous_ = 0.0f;
    float dc_ = 0.0f;
    float peak_ = 0.0f;
    float decay_ = 0.9999f;
    bool high_ = false;
    bool stateKnown_ = false;
    double lastZero_ = 0.0;
    bool haveTransition_ = false;
    double lastTransition_ = 0.0;
    double half_ = 0.0;
    bool pendingShort_ = false;
    std::array<std::uint8_t, 80> ring_{};
    int ringHead_ = 0;
    int ringCount_ = 0;
    bool havePrev_ = false;
    Timecode prevWord_;
    bool prevDrop_ = false;
    double prevEnd_ = 0.0;
    double lastEnd_ = -1.0e18;
    unsigned long long frames_ = 0;
    unsigned long long rejected_ = 0;
    unsigned long long resyncs_ = 0;
    std::vector<LtcFrame> pendingFrames_;
};

}

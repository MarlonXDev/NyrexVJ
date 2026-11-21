#include "core/ltc.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr int kWordBits = 80;
constexpr int kSyncStart = 64;
constexpr int kSyncBits[16] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1};
constexpr double kNominalFps = 27.0;
constexpr double kMinFps = 15.0;
constexpr double kMaxFps = 40.0;
constexpr double kPeriodMinFps = 16.0;
constexpr double kPeriodMaxFps = 36.0;
constexpr double kShortLimit = 1.5;
constexpr double kGapLimit = 3.2;
constexpr double kGlitchLimit = 0.4;
constexpr double kHalfGain = 0.1;

}

LtcDecoder::LtcDecoder(int sampleRate) {
    setSampleRate(sampleRate);
}

void LtcDecoder::setSampleRate(int sampleRate) {
    sampleRate_ = std::max(sampleRate, 8000);
    reset();
}

void LtcDecoder::resetBits() {
    ringCount_ = 0;
    ringHead_ = 0;
    pendingShort_ = false;
}

void LtcDecoder::reset() {
    sampleCount_ = 0;
    previous_ = 0.0f;
    dc_ = 0.0f;
    peak_ = 0.0f;
    high_ = false;
    stateKnown_ = false;
    lastZero_ = 0.0;
    haveTransition_ = false;
    lastTransition_ = 0.0;
    half_ = sampleRate_ / (kNominalFps * 160.0);
    resetBits();
    havePrev_ = false;
    prevWord_ = Timecode();
    prevDrop_ = false;
    prevEnd_ = 0.0;
    lastEnd_ = -1.0e18;
}

void LtcDecoder::push(const float* samples, size_t count, std::vector<LtcFrame>& out) {
    for (size_t i = 0; i < count; ++i) {
        const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        const double index = static_cast<double>(sampleCount_);
        const float alpha = std::max(1.0f / static_cast<float>(std::min<long long>(sampleCount_ + 1, 100000)), 0.0005f);
        dc_ += (sample - dc_) * alpha;
        const float value = sample - dc_;
        peak_ = std::max(std::fabs(value), peak_ * decay_);
        const float threshold = std::max(0.3f * peak_, 0.003f);
        if (sampleCount_ > 0 && (value >= 0.0f) != (previous_ >= 0.0f)) {
            const float span = previous_ - value;
            const double fraction = span != 0.0f ? static_cast<double>(previous_ / span) : 0.0;
            lastZero_ = index - 1.0 + fraction;
        }
        if (!stateKnown_) {
            if (value > threshold) {
                high_ = true;
                stateKnown_ = true;
            } else if (value < -threshold) {
                high_ = false;
                stateKnown_ = true;
            }
        } else if (high_ && value < -threshold) {
            high_ = false;
            onTransition(lastZero_);
        } else if (!high_ && value > threshold) {
            high_ = true;
            onTransition(lastZero_);
        }
        previous_ = value;
        ++sampleCount_;
    }
    out.insert(out.end(), pendingFrames_.begin(), pendingFrames_.end());
    pendingFrames_.clear();
}

void LtcDecoder::onTransition(double time) {
    if (!haveTransition_) {
        haveTransition_ = true;
        lastTransition_ = time;
        return;
    }
    const double interval = time - lastTransition_;
    const double ratio = interval / half_;
    if (ratio < kGlitchLimit) {
        return;
    }
    lastTransition_ = time;
    const double minHalf = sampleRate_ / (kMaxFps * 160.0);
    const double maxHalf = sampleRate_ / (kMinFps * 160.0);
    if (ratio > kGapLimit) {
        resetBits();
        return;
    }
    if (ratio < kShortLimit) {
        half_ = std::clamp(half_ + (interval - half_) * kHalfGain, minHalf, maxHalf);
        if (pendingShort_) {
            pendingShort_ = false;
            onBit(1, time);
        } else {
            pendingShort_ = true;
        }
        return;
    }
    half_ = std::clamp(half_ + (interval * 0.5 - half_) * kHalfGain, minHalf, maxHalf);
    if (pendingShort_) {
        pendingShort_ = false;
        ++rejected_;
    }
    onBit(0, time);
}

void LtcDecoder::onBit(int bit, double time) {
    ring_[static_cast<size_t>(ringHead_)] = static_cast<std::uint8_t>(bit);
    ringHead_ = (ringHead_ + 1) % kWordBits;
    if (ringCount_ < kWordBits) {
        ++ringCount_;
    }
    if (ringCount_ < kWordBits) {
        return;
    }
    for (int k = 0; k < 16; ++k) {
        if (bitAt(kSyncStart + k) != kSyncBits[k]) {
            return;
        }
    }
    onWord(time);
}

int LtcDecoder::bitAt(int index) const {
    const int oldest = (ringHead_ + kWordBits - ringCount_) % kWordBits;
    return ring_[static_cast<size_t>((oldest + index) % kWordBits)];
}

int LtcDecoder::field(int start, int length) const {
    int value = 0;
    for (int k = 0; k < length; ++k) {
        value |= bitAt(start + k) << k;
    }
    return value;
}

void LtcDecoder::onWord(double time) {
    const int frameUnits = field(0, 4);
    const int frameTens = field(8, 2);
    const bool drop = bitAt(10) != 0;
    const int secondUnits = field(16, 4);
    const int secondTens = field(24, 3);
    const int minuteUnits = field(32, 4);
    const int minuteTens = field(40, 3);
    const int hourUnits = field(48, 4);
    const int hourTens = field(56, 2);
    if (frameUnits > 9 || secondUnits > 9 || minuteUnits > 9 || hourUnits > 9 || secondTens > 5 || minuteTens > 5) {
        ++rejected_;
        havePrev_ = false;
        return;
    }
    Timecode word;
    word.frames = frameTens * 10 + frameUnits;
    word.seconds = secondTens * 10 + secondUnits;
    word.minutes = minuteTens * 10 + minuteUnits;
    word.hours = hourTens * 10 + hourUnits;
    const double period = time - prevEnd_;
    const bool periodOk = havePrev_ && period >= sampleRate_ / kPeriodMaxFps && period <= sampleRate_ / kPeriodMinFps;
    const bool previousDrop = prevDrop_;
    const Timecode previousWord = prevWord_;
    prevWord_ = word;
    prevDrop_ = drop;
    prevEnd_ = time;
    havePrev_ = true;
    if (!periodOk) {
        return;
    }
    if (drop != previousDrop) {
        ++resyncs_;
        return;
    }
    const double measured = sampleRate_ / period;
    FrameRate rate = FrameRate::Fps30;
    if (drop) {
        rate = FrameRate::Fps2997Drop;
    } else if (measured < 24.5) {
        rate = FrameRate::Fps24;
    } else if (measured < 27.5) {
        rate = FrameRate::Fps25;
    }
    word.rate = rate;
    Timecode before = previousWord;
    before.rate = rate;
    if (!timecodeValid(word) || !timecodeValid(before)) {
        ++rejected_;
        return;
    }
    if (timecodeToFrames(word) != timecodeToFrames(before) + 1) {
        ++resyncs_;
        return;
    }
    LtcFrame frame;
    frame.timecode = word;
    frame.endSample = time;
    frame.fps = measured;
    lastEnd_ = time;
    ++frames_;
    pendingFrames_.push_back(frame);
}

}

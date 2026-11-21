#pragma once

#include "core/timecode.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace ltctest {

struct Options {
    int sampleRate = 48000;
    double speed = 1.0;
    float amplitude = 0.5f;
    float offset = 0.0f;
    float noise = 0.0f;
    float smoothing = 1.0f;
    bool inverted = false;
};

inline std::array<int, 80> encodeWord(const nvj::Timecode& t) {
    std::array<int, 80> bits{};
    auto put = [&bits](int start, int length, int value) {
        for (int k = 0; k < length; ++k) {
            bits[static_cast<size_t>(start + k)] = (value >> k) & 1;
        }
    };
    put(0, 4, t.frames % 10);
    put(8, 2, t.frames / 10);
    bits[10] = t.rate == nvj::FrameRate::Fps2997Drop ? 1 : 0;
    put(16, 4, t.seconds % 10);
    put(24, 3, t.seconds / 10);
    put(32, 4, t.minutes % 10);
    put(40, 3, t.minutes / 10);
    put(48, 4, t.hours % 10);
    put(56, 2, t.hours / 10);
    static const int sync[16] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1};
    for (int k = 0; k < 16; ++k) {
        bits[static_cast<size_t>(64 + k)] = sync[k];
    }
    return bits;
}

class Generator {
public:
    Generator(const Options& options, const nvj::Timecode& start)
        : options_(options), rate_(start.rate), frame_(nvj::timecodeToFrames(start)) {
        samplesPerBit_ = options.sampleRate / (nvj::exactFps(rate_) * options.speed * 80.0);
        loadWord();
    }

    void jumpTo(const nvj::Timecode& target) {
        jumpFrame_ = nvj::timecodeToFrames(target);
        hasJump_ = true;
    }

    void corruptWord(long long wordIndex) {
        corrupt_.insert(wordIndex);
        if (wordIndex == wordIndex_) {
            loadBits();
        }
    }

    double samplesPerFrame() const { return samplesPerBit_ * 80.0; }

    void render(size_t count, std::vector<float>& out) {
        for (size_t i = 0; i < count; ++i) {
            const double position = static_cast<double>(sampleIndex_) / samplesPerBit_;
            const long long bit = static_cast<long long>(std::floor(position));
            while (bitIndex_ < bit) {
                advanceBit();
            }
            const double fraction = position - static_cast<double>(bit);
            int level = level_;
            if (word_[static_cast<size_t>(bitInWord_)] != 0 && fraction >= 0.5) {
                level = -level;
            }
            if (options_.inverted) {
                level = -level;
            }
            float value = static_cast<float>(level) * options_.amplitude + options_.offset;
            if (options_.noise > 0.0f) {
                value += options_.noise * random();
            }
            state_ += options_.smoothing * (value - state_);
            out.push_back(state_);
            ++sampleIndex_;
        }
    }

    std::vector<float> render(size_t count) {
        std::vector<float> out;
        out.reserve(count);
        render(count, out);
        return out;
    }

private:
    float random() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<float>(seed_ >> 8) / static_cast<float>(1u << 23) - 1.0f;
    }

    void loadBits() {
        std::array<int, 80> bits = encodeWord(nvj::timecodeFromFrames(currentFrame_, rate_));
        if (corrupt_.count(wordIndex_) != 0) {
            bits[2] ^= 1;
        }
        word_ = bits;
    }

    void loadWord() {
        if (hasJump_) {
            frame_ = jumpFrame_;
            hasJump_ = false;
        }
        currentFrame_ = frame_;
        ++frame_;
        loadBits();
    }

    void advanceBit() {
        const int finalLevel = word_[static_cast<size_t>(bitInWord_)] != 0 ? -level_ : level_;
        level_ = -finalLevel;
        ++bitIndex_;
        bitInWord_ = (bitInWord_ + 1) % 80;
        if (bitInWord_ == 0) {
            ++wordIndex_;
            loadWord();
        }
    }

    Options options_;
    nvj::FrameRate rate_;
    long long frame_ = 0;
    long long currentFrame_ = 0;
    long long jumpFrame_ = 0;
    bool hasJump_ = false;
    double samplesPerBit_ = 0.0;
    long long sampleIndex_ = 0;
    long long bitIndex_ = 0;
    long long wordIndex_ = 0;
    int bitInWord_ = 0;
    int level_ = 1;
    float state_ = 0.0f;
    std::array<int, 80> word_{};
    std::set<long long> corrupt_;
    std::uint32_t seed_ = 12345u;
};

inline std::array<int, 8> quarterFrames(const nvj::Timecode& t) {
    int rateCode = 1;
    switch (t.rate) {
    case nvj::FrameRate::Fps24:
        rateCode = 0;
        break;
    case nvj::FrameRate::Fps25:
        rateCode = 1;
        break;
    case nvj::FrameRate::Fps2997Drop:
        rateCode = 2;
        break;
    default:
        rateCode = 3;
        break;
    }
    const int nibbles[8] = {t.frames & 0x0F, (t.frames >> 4) & 0x01, t.seconds & 0x0F, (t.seconds >> 4) & 0x03,
                            t.minutes & 0x0F, (t.minutes >> 4) & 0x03, t.hours & 0x0F, ((t.hours >> 4) & 0x01) | (rateCode << 1)};
    std::array<int, 8> data{};
    for (int piece = 0; piece < 8; ++piece) {
        data[static_cast<size_t>(piece)] = (piece << 4) | nibbles[piece];
    }
    return data;
}

}

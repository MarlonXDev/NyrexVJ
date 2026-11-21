#pragma once

#include <array>

namespace nvj {

constexpr int kMaxDelayFrames = 8;
constexpr int kSyncBarBits = 12;
constexpr int kSkewWindow = 120;

int clampDelay(int frames);
int syncBarValue(long long frame, int delayFrames);
bool syncBarBit(int value, int column);

class FrameHistory {
public:
    void configure(int maxDelay);
    void clear();
    int maxDelay() const { return maxDelay_; }
    int slots() const { return maxDelay_ > 0 ? maxDelay_ + 1 : 0; }
    int filled() const { return filled_; }
    int push();
    int slotFor(int delay) const;

private:
    int maxDelay_ = 0;
    int head_ = -1;
    int filled_ = 0;
};

class SkewTracker {
public:
    void add(double milliseconds);
    void reset();
    int count() const { return count_; }
    unsigned long long total() const { return total_; }
    double last() const { return last_; }
    double minimum() const;
    double average() const;
    double maximum() const;

private:
    std::array<double, kSkewWindow> values_{};
    int next_ = 0;
    int count_ = 0;
    unsigned long long total_ = 0;
    double last_ = 0.0;
};

}

#include "core/frame_lock.hpp"

#include <algorithm>

namespace nvj {

int clampDelay(int frames) {
    return std::clamp(frames, 0, kMaxDelayFrames);
}

int syncBarValue(long long frame, int delayFrames) {
    long long value = frame - clampDelay(delayFrames);
    if (value < 0) {
        value = 0;
    }
    return static_cast<int>(value & ((1LL << kSyncBarBits) - 1));
}

bool syncBarBit(int value, int column) {
    if (column < 0 || column >= kSyncBarBits) {
        return false;
    }
    return ((value >> (kSyncBarBits - 1 - column)) & 1) != 0;
}

void FrameHistory::configure(int maxDelay) {
    maxDelay = clampDelay(maxDelay);
    if (maxDelay == maxDelay_) {
        return;
    }
    maxDelay_ = maxDelay;
    clear();
}

void FrameHistory::clear() {
    head_ = -1;
    filled_ = 0;
}

int FrameHistory::push() {
    const int count = slots();
    if (count == 0) {
        return -1;
    }
    head_ = (head_ + 1) % count;
    filled_ = std::min(filled_ + 1, count);
    return head_;
}

int FrameHistory::slotFor(int delay) const {
    const int count = slots();
    if (count == 0 || filled_ == 0) {
        return -1;
    }
    const int age = std::min(clampDelay(delay), filled_ - 1);
    return (head_ - age + count) % count;
}

void SkewTracker::add(double milliseconds) {
    if (milliseconds < 0.0) {
        milliseconds = 0.0;
    }
    values_[static_cast<size_t>(next_)] = milliseconds;
    next_ = (next_ + 1) % kSkewWindow;
    count_ = std::min(count_ + 1, kSkewWindow);
    ++total_;
    last_ = milliseconds;
}

void SkewTracker::reset() {
    next_ = 0;
    count_ = 0;
    total_ = 0;
    last_ = 0.0;
}

double SkewTracker::minimum() const {
    if (count_ == 0) {
        return 0.0;
    }
    return *std::min_element(values_.begin(), values_.begin() + count_);
}

double SkewTracker::average() const {
    if (count_ == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (int i = 0; i < count_; ++i) {
        sum += values_[static_cast<size_t>(i)];
    }
    return sum / static_cast<double>(count_);
}

double SkewTracker::maximum() const {
    if (count_ == 0) {
        return 0.0;
    }
    return *std::max_element(values_.begin(), values_.begin() + count_);
}

}

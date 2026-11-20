#include "core/frame_pacer.hpp"

#include <algorithm>
#include <cstdlib>

namespace nvj {

int clampFpsLimit(int fps) {
    if (fps <= 0) {
        return 0;
    }
    int best = kFpsLimits[0];
    for (const int candidate : kFpsLimits) {
        if (candidate > 0 && std::abs(candidate - fps) < std::abs(best - fps)) {
            best = candidate;
        }
    }
    return best;
}

int fpsLimitIndex(int fps) {
    const int clamped = clampFpsLimit(fps);
    for (size_t i = 0; i < kFpsLimits.size(); ++i) {
        if (kFpsLimits[i] == clamped) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void FramePacer::setTargetFps(int fps) {
    const int clamped = clampFpsLimit(fps);
    if (clamped == fps_) {
        return;
    }
    fps_ = clamped;
    period_ = clamped > 0 ? 1.0 / static_cast<double>(clamped) : 0.0;
    started_ = false;
}

void FramePacer::reset() {
    started_ = false;
}

double FramePacer::nextDeadline(double now) {
    if (fps_ <= 0) {
        return now;
    }
    if (!started_) {
        started_ = true;
        deadline_ = now + period_;
        return deadline_;
    }
    deadline_ += period_;
    if (now > deadline_) {
        deadline_ = now;
    }
    return deadline_;
}

}

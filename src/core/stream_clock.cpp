#include "core/stream_clock.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {
namespace stream {

namespace {

constexpr double kJumpSeconds = 5.0;

}

LagTracker::LagTracker(double limitSeconds, double holdSeconds, double leakPerSecond) : limit_(std::max(limitSeconds, 0.05)), hold_(std::max(holdSeconds, 0.0)), leak_(std::max(leakPerSecond, 0.0)) {}

void LagTracker::reset() {
    started_ = false;
    overSince_ = -1.0;
    lag_ = 0.0;
}

bool LagTracker::observe(double wall, double stream) {
    const double offset = wall - stream;
    if (!started_) {
        started_ = true;
        base_ = offset;
        lastWall_ = wall;
        lastStream_ = stream;
        overSince_ = -1.0;
        lag_ = 0.0;
        return false;
    }
    if (std::fabs(stream - lastStream_) > kJumpSeconds || wall < lastWall_) {
        started_ = false;
        return observe(wall, stream);
    }
    const double elapsed = std::max(wall - lastWall_, 0.0);
    lastWall_ = wall;
    lastStream_ = stream;
    base_ = std::min(base_ + leak_ * elapsed, offset);
    lag_ = std::max(offset - base_, 0.0);
    if (lag_ <= limit_) {
        overSince_ = -1.0;
        return false;
    }
    if (overSince_ < 0.0) {
        overSince_ = wall;
    }
    if (wall - overSince_ < hold_) {
        return false;
    }
    ++resyncs_;
    base_ = offset;
    overSince_ = -1.0;
    lag_ = 0.0;
    return true;
}

void JitterMeter::reset() {
    started_ = false;
    jitter_ = 0.0;
}

void JitterMeter::observe(double wall, double stream) {
    if (!started_) {
        started_ = true;
        lastWall_ = wall;
        lastStream_ = stream;
        return;
    }
    const double wallStep = wall - lastWall_;
    const double streamStep = stream - lastStream_;
    lastWall_ = wall;
    lastStream_ = stream;
    if (std::fabs(streamStep) > kJumpSeconds || wallStep < 0.0) {
        return;
    }
    const double deviation = std::fabs(wallStep - streamStep);
    jitter_ += (deviation - jitter_) / 16.0;
}

}
}

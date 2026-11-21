#include "core/timecode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nvj {

namespace {

constexpr double kJumpSeconds = 0.1;
constexpr double kRunningTimeout = 0.5;
constexpr double kMaxExtrapolation = 0.15;

bool isDrop(FrameRate rate) {
    return rate == FrameRate::Fps2997Drop;
}

}

int nominalFps(FrameRate rate) {
    switch (rate) {
    case FrameRate::Fps24:
        return 24;
    case FrameRate::Fps25:
        return 25;
    default:
        return 30;
    }
}

double exactFps(FrameRate rate) {
    return isDrop(rate) ? 30000.0 / 1001.0 : static_cast<double>(nominalFps(rate));
}

const char* frameRateName(FrameRate rate) {
    switch (rate) {
    case FrameRate::Fps24:
        return "24";
    case FrameRate::Fps25:
        return "25";
    case FrameRate::Fps2997Drop:
        return "29.97 drop";
    default:
        return "30";
    }
}

bool timecodeValid(const Timecode& t) {
    if (t.hours < 0 || t.hours > 23 || t.minutes < 0 || t.minutes > 59 || t.seconds < 0 || t.seconds > 59) {
        return false;
    }
    if (t.frames < 0 || t.frames >= nominalFps(t.rate)) {
        return false;
    }
    if (isDrop(t.rate) && t.seconds == 0 && t.frames < 2 && t.minutes % 10 != 0) {
        return false;
    }
    return true;
}

long long timecodeToFrames(const Timecode& t) {
    const long long fps = nominalFps(t.rate);
    long long frames = ((static_cast<long long>(t.hours) * 60 + t.minutes) * 60 + t.seconds) * fps + t.frames;
    if (isDrop(t.rate)) {
        const long long totalMinutes = static_cast<long long>(t.hours) * 60 + t.minutes;
        frames -= 2 * (totalMinutes - totalMinutes / 10);
    }
    return frames;
}

Timecode timecodeFromFrames(long long frames, FrameRate rate) {
    Timecode t;
    t.rate = rate;
    const long long fps = nominalFps(rate);
    const long long day = fps * 60 * 60 * 24 - (isDrop(rate) ? 2 * (24 * 60 - 24 * 6) : 0);
    frames %= day;
    if (frames < 0) {
        frames += day;
    }
    if (isDrop(rate)) {
        const long long perTenMinutes = 17982;
        const long long tens = frames / perTenMinutes;
        const long long rest = frames % perTenMinutes;
        frames += 18 * tens;
        if (rest > 1) {
            frames += 2 * ((rest - 2) / 1798);
        }
    }
    t.frames = static_cast<int>(frames % fps);
    const long long totalSeconds = frames / fps;
    t.seconds = static_cast<int>(totalSeconds % 60);
    t.minutes = static_cast<int>((totalSeconds / 60) % 60);
    t.hours = static_cast<int>((totalSeconds / 3600) % 24);
    return t;
}

double timecodeToSeconds(const Timecode& t) {
    return static_cast<double>(timecodeToFrames(t)) / exactFps(t.rate);
}

Timecode timecodeFromSeconds(double seconds, FrameRate rate) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const long long frames = static_cast<long long>(std::floor(seconds * exactFps(rate) + 1.0e-6));
    return timecodeFromFrames(frames, rate);
}

std::string formatTimecode(const Timecode& t) {
    char text[32];
    std::snprintf(text, sizeof(text), "%02d:%02d:%02d%c%02d", t.hours, t.minutes, t.seconds, isDrop(t.rate) ? ';' : ':', t.frames);
    return text;
}

bool parseTimecode(const std::string& text, FrameRate rate, Timecode& out) {
    int values[4] = {};
    size_t position = 0;
    for (int field = 0; field < 4; ++field) {
        if (position >= text.size() || text[position] < '0' || text[position] > '9') {
            return false;
        }
        int value = 0;
        int digits = 0;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
            value = value * 10 + (text[position] - '0');
            ++position;
            if (++digits > 3) {
                return false;
            }
        }
        values[field] = value;
        if (field < 3) {
            if (position >= text.size() || (text[position] != ':' && text[position] != ';' && text[position] != '.')) {
                return false;
            }
            ++position;
        }
    }
    if (position != text.size()) {
        return false;
    }
    Timecode result;
    result.hours = values[0];
    result.minutes = values[1];
    result.seconds = values[2];
    result.frames = values[3];
    result.rate = rate;
    if (!timecodeValid(result)) {
        return false;
    }
    out = result;
    return true;
}

void MtcDecoder::reset() {
    count_ = 0;
    next_ = 0;
}

bool MtcDecoder::pushQuarterFrame(int data, MtcFix& out) {
    const int type = (data >> 4) & 0x07;
    const int nibble = data & 0x0F;
    if (type != next_) {
        if (count_ > 0) {
            ++dropped_;
        }
        count_ = 0;
        if (type != 0) {
            next_ = 0;
            return false;
        }
    }
    nibbles_[type] = nibble;
    count_ = type + 1;
    next_ = (type + 1) & 7;
    if (type != 7) {
        return false;
    }
    count_ = 0;
    static constexpr FrameRate kRates[4] = {FrameRate::Fps24, FrameRate::Fps25, FrameRate::Fps2997Drop, FrameRate::Fps30};
    Timecode t;
    t.rate = kRates[(nibbles_[7] >> 1) & 3];
    t.frames = nibbles_[0] | ((nibbles_[1] & 1) << 4);
    t.seconds = nibbles_[2] | ((nibbles_[3] & 3) << 4);
    t.minutes = nibbles_[4] | ((nibbles_[5] & 3) << 4);
    t.hours = nibbles_[6] | ((nibbles_[7] & 1) << 4);
    if (!timecodeValid(t)) {
        ++dropped_;
        return false;
    }
    out.timecode = t;
    out.frames = static_cast<double>(timecodeToFrames(t)) + 1.75;
    out.seconds = out.frames / exactFps(t.rate);
    return true;
}

void TimecodeTracker::reset() {
    seen_ = false;
    jump_ = false;
    lastTime_ = time_;
    lastSeconds_ = 0.0;
}

void TimecodeTracker::advance(double dt) {
    time_ += std::max(dt, 0.0);
}

void TimecodeTracker::report(double seconds, double age, FrameRate rate) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return;
    }
    const double at = time_ - std::max(age, 0.0);
    if (seen_ && at >= lastTime_) {
        const double elapsed = at - lastTime_;
        if (elapsed <= kRunningTimeout) {
            const double expected = lastSeconds_ + elapsed;
            if (rate != rate_ || std::fabs(seconds - expected) > kJumpSeconds) {
                jump_ = true;
                ++jumps_;
            }
        }
    }
    if (seen_ && at < lastTime_) {
        return;
    }
    seen_ = true;
    lastTime_ = at;
    lastSeconds_ = seconds;
    rate_ = rate;
    ++fixes_;
}

bool TimecodeTracker::running() const {
    return seen_ && time_ - lastTime_ <= kRunningTimeout;
}

double TimecodeTracker::position() const {
    if (!seen_) {
        return 0.0;
    }
    return lastSeconds_ + std::min(time_ - lastTime_, kMaxExtrapolation);
}

bool TimecodeTracker::takeJump() {
    const bool value = jump_;
    jump_ = false;
    return value;
}

}

#include "core/playhead.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr float kSyncChoices[kSyncBeatChoices] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f};

}

const char* loopModeName(LoopMode mode) {
    switch (mode) {
    case LoopMode::PingPong:
        return "pingpong";
    case LoopMode::OnceHold:
        return "hold";
    case LoopMode::OnceStop:
        return "stop";
    default:
        return "loop";
    }
}

LoopMode loopModeFromName(const std::string& name) {
    if (name == "pingpong") {
        return LoopMode::PingPong;
    }
    if (name == "hold") {
        return LoopMode::OnceHold;
    }
    if (name == "stop") {
        return LoopMode::OnceStop;
    }
    return LoopMode::Loop;
}

float syncBeatsChoice(int index) {
    return kSyncChoices[std::clamp(index, 0, kSyncBeatChoices - 1)];
}

int syncBeatsIndex(float beats) {
    int best = 0;
    float distance = std::fabs(beats - kSyncChoices[0]);
    for (int i = 1; i < kSyncBeatChoices; ++i) {
        const float candidate = std::fabs(beats - kSyncChoices[i]);
        if (candidate < distance) {
            distance = candidate;
            best = i;
        }
    }
    return best;
}

ClipPlayback clampedPlayback(const ClipPlayback& playback) {
    ClipPlayback result = playback;
    if (!std::isfinite(result.in) || result.in < 0.0) {
        result.in = 0.0;
    }
    if (!std::isfinite(result.out) || result.out < 0.0) {
        result.out = 0.0;
    }
    if (!std::isfinite(result.speed)) {
        result.speed = 1.0f;
    }
    result.speed = std::clamp(result.speed, kMinPlaySpeed, kMaxPlaySpeed);
    if (!std::isfinite(result.syncBeats) || result.syncBeats < 0.0f) {
        result.syncBeats = 0.0f;
    }
    result.syncBeats = syncBeatsChoice(syncBeatsIndex(result.syncBeats));
    for (float& value : result.values) {
        if (!std::isfinite(value) || std::fabs(value) > 1.0e6f) {
            value = kClipValueUnset;
        }
    }
    return result;
}

PlayRange resolveRange(const ClipPlayback& playback, double duration, double framePeriod) {
    PlayRange range;
    if (!(duration > 0.0)) {
        return range;
    }
    const double minimum = std::min(std::max(framePeriod, 0.001), duration);
    double in = std::clamp(playback.in, 0.0, std::max(duration - minimum, 0.0));
    double out = playback.out > 0.0 ? std::min(playback.out, duration) : duration;
    if (out < in + minimum) {
        out = std::min(in + minimum, duration);
        in = std::max(out - minimum, 0.0);
    }
    range.in = in;
    range.out = out;
    return range;
}

void Playhead::configure(const ClipPlayback& playback, double duration, double framePeriod) {
    const ClipPlayback next = clampedPlayback(playback);
    framePeriod_ = framePeriod > 0.0 ? framePeriod : 1.0 / 30.0;
    duration_ = duration;
    const bool modeChanged = next.loop != playback_.loop;
    playback_ = next;
    range_ = resolveRange(next, duration, framePeriod_);
    if (modeChanged) {
        wraps_ = 0;
        finished_ = false;
        leg_ = 1;
    }
    if (!(range_.length() > 0.0)) {
        position_ = 0.0;
        return;
    }
    position_ = std::clamp(position_, range_.in, endPosition());
}

double Playhead::endPosition() const {
    const double length = range_.length();
    if (length <= framePeriod_) {
        return range_.in;
    }
    return range_.out - std::min(framePeriod_ * 0.5, length * 0.5);
}

void Playhead::restart() {
    wraps_ = 0;
    finished_ = false;
    leg_ = 1;
    position_ = playback_.speed < 0.0f ? endPosition() : range_.in;
}

bool Playhead::reverse() const {
    const bool negative = playback_.speed < 0.0f;
    if (playback_.loop != LoopMode::PingPong) {
        return negative;
    }
    return (leg_ > 0) == negative;
}

bool Playhead::visible() const {
    return !(playback_.loop == LoopMode::OnceStop && finished_);
}

bool Playhead::paused() const {
    return playback_.speed == 0.0f || finished_;
}

double Playhead::timeline() const {
    const double relative = position_ - range_.in;
    if (playback_.loop == LoopMode::Loop && !reverse() && wraps_ > 0) {
        return static_cast<double>(wraps_) * range_.length() + relative;
    }
    return relative;
}

void Playhead::seek(double position) {
    if (!(range_.length() > 0.0)) {
        position_ = 0.0;
        return;
    }
    position_ = std::clamp(position, range_.in, endPosition());
    wraps_ = 0;
    finished_ = false;
}

void Playhead::step(int frames) {
    seek(position_ + static_cast<double>(frames) * framePeriod_);
}

void Playhead::advance(double seconds, double beats) {
    if (!(range_.length() > 0.0)) {
        return;
    }
    double delta = 0.0;
    if (playback_.syncBeats > 0.0f) {
        delta = range_.length() / static_cast<double>(playback_.syncBeats) * beats * static_cast<double>(playback_.speed);
    } else {
        delta = seconds * static_cast<double>(playback_.speed);
    }
    if (delta == 0.0 || !std::isfinite(delta)) {
        return;
    }
    move(delta);
}

void Playhead::move(double delta) {
    const double length = range_.length();
    switch (playback_.loop) {
    case LoopMode::Loop: {
        const double q = (position_ - range_.in) + delta;
        const double w = std::floor(q / length);
        wraps_ = delta > 0.0 ? wraps_ + static_cast<long long>(w) : 0;
        position_ = range_.in + (q - w * length);
        break;
    }
    case LoopMode::PingPong: {
        const double span = std::max(endPosition() - range_.in, 0.0);
        if (!(span > 0.0)) {
            position_ = range_.in;
            break;
        }
        const double cycle = 2.0 * span;
        double s = leg_ > 0 ? position_ - range_.in : cycle - (position_ - range_.in);
        s = std::fmod(s + delta, cycle);
        if (s < 0.0) {
            s += cycle;
        }
        if (s <= span) {
            leg_ = 1;
            position_ = range_.in + s;
        } else {
            leg_ = -1;
            position_ = range_.in + (cycle - s);
        }
        break;
    }
    default: {
        if (finished_) {
            break;
        }
        const double next = position_ + delta;
        if (next >= endPosition() && delta > 0.0) {
            position_ = endPosition();
            finished_ = true;
        } else if (next <= range_.in && delta < 0.0) {
            position_ = range_.in;
            finished_ = true;
        } else {
            position_ = next;
        }
        break;
    }
    }
}

}

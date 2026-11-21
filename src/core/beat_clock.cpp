#include "core/beat_clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nvj {

namespace {

constexpr double kSlewFraction = 0.25;
constexpr double kJumpBeats = 1.0;
constexpr double kLockError = 0.06;
constexpr double kLoseError = 0.18;
constexpr int kLockObservations = 3;
constexpr double kTapGap = 2.0;
constexpr size_t kMaxTaps = 5;
constexpr double kErrorBlend = 0.3;
constexpr double kTempoSnap = 0.01;

struct Tuning {
    double timeout;
    double phaseGain;
    double tempoGain;
};

constexpr std::array<Tuning, kSyncSourceCount> kTuning = {{
    {0.0, 1.0, 1.0},
    {3.0, 0.35, 0.08},
    {1.0, 0.1, 0.3},
    {0.5, 0.5, 1.0},
    {2.0, 0.5, 0.5},
}};

constexpr std::array<SyncSource, 4> kAutoOrder = {SyncSource::DjLink, SyncSource::Timecode, SyncSource::MidiClock, SyncSource::Audio};

int index(SyncSource source) {
    return static_cast<int>(source);
}

double wrapTo(double value, double modulus) {
    double r = std::fmod(value, modulus);
    if (r >= modulus * 0.5) {
        r -= modulus;
    } else if (r < -modulus * 0.5) {
        r += modulus;
    }
    return r;
}

}

SyncSource modeSource(SyncMode mode) {
    switch (mode) {
    case SyncMode::Audio:
        return SyncSource::Audio;
    case SyncMode::MidiClock:
        return SyncSource::MidiClock;
    case SyncMode::Timecode:
        return SyncSource::Timecode;
    case SyncMode::DjLink:
        return SyncSource::DjLink;
    default:
        return SyncSource::Manual;
    }
}

const char* syncSourceName(SyncSource source) {
    switch (source) {
    case SyncSource::Audio:
        return "Audio";
    case SyncSource::MidiClock:
        return "MIDI clock";
    case SyncSource::Timecode:
        return "Timecode";
    case SyncSource::DjLink:
        return "Pro DJ Link";
    default:
        return "Manual";
    }
}

const char* syncModeName(SyncMode mode) {
    switch (mode) {
    case SyncMode::Manual:
        return "manual";
    case SyncMode::Audio:
        return "audio";
    case SyncMode::MidiClock:
        return "midiclock";
    case SyncMode::Timecode:
        return "timecode";
    case SyncMode::DjLink:
        return "djlink";
    default:
        return "auto";
    }
}

SyncMode syncModeFromName(const char* name) {
    if (name == nullptr) {
        return SyncMode::Auto;
    }
    for (int i = 0; i <= static_cast<int>(SyncMode::DjLink); ++i) {
        const SyncMode mode = static_cast<SyncMode>(i);
        if (std::strcmp(name, syncModeName(mode)) == 0) {
            return mode;
        }
    }
    return SyncMode::Auto;
}

void BeatClock::setMode(SyncMode mode) {
    mode_ = mode;
    noteSwitch(active());
}

void BeatClock::setLatency(double seconds) {
    latency_ = std::clamp(seconds, -0.5, 0.5);
}

void BeatClock::setTempo(double bpm) {
    if (!std::isfinite(bpm)) {
        return;
    }
    bpm_ = std::clamp(bpm, kMinBpm, kMaxBpm);
}

void BeatClock::tap() {
    if (!taps_.empty() && time_ - taps_.back() > kTapGap) {
        taps_.clear();
    }
    taps_.push_back(time_);
    while (taps_.size() > kMaxTaps) {
        taps_.pop_front();
    }
    if (taps_.size() >= 2) {
        const double interval = (taps_.back() - taps_.front()) / static_cast<double>(taps_.size() - 1);
        if (interval > 0.0) {
            bpm_ = std::clamp(60.0 / interval, kMinBpm, kMaxBpm);
        }
    }
    beat_ = std::floor(beat_ + 0.5);
    pending_ = 0.0;
}

void BeatClock::resync() {
    beat_ = 0.0;
    pending_ = 0.0;
}

void BeatClock::shift(double beats) {
    beat_ += beats;
}

void BeatClock::nudge(double amount) {
    const double fraction = beat_ - std::floor(beat_);
    const double delta = fraction < 0.5 ? -fraction : 1.0 - fraction;
    beat_ += delta * std::clamp(amount, 0.0, 1.0);
}

bool BeatClock::present(SyncSource source) const {
    if (source == SyncSource::Manual) {
        return true;
    }
    const int i = index(source);
    return everSeen_[static_cast<size_t>(i)] && time_ - lastSeen_[static_cast<size_t>(i)] <= kTuning[static_cast<size_t>(i)].timeout;
}

double BeatClock::secondsSinceSeen(SyncSource source) const {
    const int i = index(source);
    if (!everSeen_[static_cast<size_t>(i)]) {
        return -1.0;
    }
    return time_ - lastSeen_[static_cast<size_t>(i)];
}

SyncSource BeatClock::active() const {
    if (mode_ == SyncMode::Auto) {
        for (SyncSource source : kAutoOrder) {
            if (present(source)) {
                return source;
            }
        }
        return SyncSource::Manual;
    }
    const SyncSource forced = modeSource(mode_);
    if (forced == SyncSource::Manual) {
        return SyncSource::Manual;
    }
    return present(forced) ? forced : SyncSource::Manual;
}

LockState BeatClock::state() const {
    const SyncSource now = active();
    if (now == SyncSource::Manual) {
        if (mode_ == SyncMode::Auto || mode_ == SyncMode::Manual) {
            return LockState::Free;
        }
        const int i = index(modeSource(mode_));
        return everSeen_[static_cast<size_t>(i)] ? LockState::Lost : LockState::Searching;
    }
    if (goodObservations_ >= kLockObservations && errorAbs_ < kLockError) {
        return LockState::Locked;
    }
    return LockState::Acquiring;
}

double BeatClock::lockQuality() const {
    if (active() == SyncSource::Manual) {
        return 0.0;
    }
    return std::clamp(1.0 - errorAbs_ / 0.25, 0.0, 1.0);
}

void BeatClock::noteSwitch(SyncSource now) {
    if (now != lastActive_) {
        lastActive_ = now;
        goodObservations_ = 0;
        errorAbs_ = 1.0;
        errorSigned_ = 0.0;
        pending_ = 0.0;
    }
}

void BeatClock::applyTempo(SyncSource source, double bpm) {
    if (!std::isfinite(bpm) || bpm <= 0.0) {
        return;
    }
    const double target = std::clamp(bpm, kMinBpm, kMaxBpm);
    const double gain = kTuning[static_cast<size_t>(index(source))].tempoGain;
    bpm_ += (target - bpm_) * gain;
    if (std::fabs(bpm_ - target) < kTempoSnap) {
        bpm_ = target;
    }
}

void BeatClock::advance(double dt, bool running) {
    dt = std::max(dt, 0.0);
    time_ += dt;
    running_ = running;
    noteSwitch(active());
    if (!running) {
        return;
    }
    double delta = dt * bpm_ / 60.0;
    if (pending_ != 0.0) {
        const double limit = kSlewFraction * delta;
        const double step = std::clamp(pending_, -limit, limit);
        delta += step;
        pending_ -= step;
    }
    beat_ += delta;
}

void BeatClock::report(const SyncObservation& o) {
    if (o.source == SyncSource::Manual) {
        if (o.hasTempo) {
            setTempo(o.bpm);
        }
        return;
    }
    const size_t i = static_cast<size_t>(index(o.source));
    const double age = std::max(o.age, 0.0);
    const double seen = time_ - age;
    lastSeen_[i] = everSeen_[i] ? std::max(lastSeen_[i], seen) : seen;
    everSeen_[i] = true;
    const SyncSource now = active();
    noteSwitch(now);
    if (now != o.source) {
        return;
    }
    if (o.hasTempo) {
        applyTempo(o.source, o.bpm);
    }
    if (!o.hasPhase || !std::isfinite(o.beat)) {
        return;
    }
    const double perBeatSeconds = 60.0 / bpm_;
    const double rate = running_ ? 1.0 / perBeatSeconds : 0.0;
    const double projected = beat_ + pending_ - age * rate;
    const double target = o.beat + latency_ / perBeatSeconds;
    double error = target - projected;
    if (o.modulus > 0) {
        error = wrapTo(error, static_cast<double>(o.modulus));
    }
    const bool fresh = goodObservations_ == 0;
    if (o.jump || (o.modulus <= 0 && std::fabs(error) > kJumpBeats)) {
        beat_ = target + age * rate;
        pending_ = 0.0;
        goodObservations_ = 0;
        errorAbs_ = 1.0;
        errorSigned_ = error;
        ++jumps_;
        return;
    }
    pending_ += kTuning[i].phaseGain * error;
    errorSigned_ = error;
    errorAbs_ = fresh ? std::fabs(error) : errorAbs_ + (std::fabs(error) - errorAbs_) * kErrorBlend;
    if (std::fabs(error) < kLockError) {
        ++goodObservations_;
    } else if (std::fabs(error) > kLoseError) {
        goodObservations_ = 0;
    }
}

}

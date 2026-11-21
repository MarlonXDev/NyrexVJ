#include "core/midi_clock.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr double kGapSeconds = 0.5;
constexpr double kTimeoutSeconds = 1.0;
constexpr int kMinimumTicks = 13;
constexpr long long kTicksPerBeat = 24;

}

void MidiClockTracker::reset() {
    head_ = 0;
    count_ = 0;
    last_ = -1.0e9;
    running_ = true;
    positionKnown_ = false;
    positionTicks_ = 0;
    tempoTicks_ = 0;
    pending_ = false;
    jump_ = false;
}

void MidiClockTracker::onClock(double time) {
    if (!std::isfinite(time) || time < last_) {
        return;
    }
    if (count_ > 0 && time - last_ > kGapSeconds) {
        count_ = 0;
        head_ = 0;
        ++gaps_;
    }
    stamps_[static_cast<size_t>(head_)] = time;
    head_ = (head_ + 1) % kWindow;
    count_ = std::min(count_ + 1, kWindow);
    last_ = time;
    ++ticks_;
    if (!running_) {
        return;
    }
    if (positionKnown_) {
        if (positionTicks_ % kTicksPerBeat == 0) {
            pending_ = true;
            pendingTime_ = time;
            pendingBeats_ = static_cast<double>(positionTicks_ / kTicksPerBeat);
        }
        ++positionTicks_;
    } else {
        if (tempoTicks_ % kTicksPerBeat == 0) {
            pending_ = true;
            pendingTime_ = time;
            pendingBeats_ = 0.0;
        }
        ++tempoTicks_;
    }
}

void MidiClockTracker::onStart(double) {
    running_ = true;
    positionKnown_ = true;
    positionTicks_ = 0;
    pending_ = false;
    jump_ = true;
    ++starts_;
}

void MidiClockTracker::onContinue(double) {
    running_ = true;
    jump_ = true;
}

void MidiClockTracker::onStop(double) {
    running_ = false;
    pending_ = false;
}

void MidiClockTracker::onSongPosition(int sixteenths, double) {
    positionTicks_ = static_cast<long long>(std::clamp(sixteenths, 0, 16383)) * 6;
    positionKnown_ = true;
    pending_ = false;
    jump_ = true;
}

double MidiClockTracker::bpm() const {
    if (count_ < kMinimumTicks) {
        return 0.0;
    }
    const int newest = (head_ + kWindow - 1) % kWindow;
    const int oldest = (head_ + kWindow - count_) % kWindow;
    const double span = stamps_[static_cast<size_t>(newest)] - stamps_[static_cast<size_t>(oldest)];
    if (span <= 0.0) {
        return 0.0;
    }
    const double interval = span / static_cast<double>(count_ - 1);
    return 2.5 / interval;
}

bool MidiClockTracker::active(double now) const {
    return ticks_ > 0 && now - last_ <= kTimeoutSeconds;
}

double MidiClockTracker::position() const {
    return positionKnown_ ? static_cast<double>(positionTicks_) / static_cast<double>(kTicksPerBeat) : 0.0;
}

bool MidiClockTracker::takeObservation(SyncObservation& out, double now, bool barAlign) {
    if (!pending_) {
        return false;
    }
    pending_ = false;
    SyncObservation observation;
    observation.source = SyncSource::MidiClock;
    const double tempo = bpm();
    observation.hasTempo = tempo >= kMinBpm && tempo <= kMaxBpm;
    observation.bpm = observation.hasTempo ? tempo : 0.0;
    observation.age = std::max(now - pendingTime_, 0.0);
    if (positionKnown_) {
        observation.hasPhase = true;
        observation.modulus = barAlign ? 4 : 1;
        observation.beat = barAlign ? std::fmod(pendingBeats_, 4.0) : 0.0;
    }
    observation.jump = jump_;
    jump_ = false;
    out = observation;
    return true;
}

}

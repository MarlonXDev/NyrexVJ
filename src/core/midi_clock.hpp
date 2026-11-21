#pragma once

#include "core/beat_clock.hpp"

#include <array>

namespace nvj {

class MidiClockTracker {
public:
    void onClock(double time);
    void onStart(double time);
    void onContinue(double time);
    void onStop(double time);
    void onSongPosition(int sixteenths, double time);
    void reset();

    bool takeObservation(SyncObservation& out, double now, bool barAlign);
    bool active(double now) const;
    double bpm() const;
    bool running() const { return running_; }
    bool positionKnown() const { return positionKnown_; }
    double position() const;
    unsigned long long ticks() const { return ticks_; }
    unsigned long long gaps() const { return gaps_; }
    unsigned long long starts() const { return starts_; }

private:
    static constexpr int kWindow = 97;

    std::array<double, kWindow> stamps_{};
    int head_ = 0;
    int count_ = 0;
    double last_ = -1.0e9;
    bool running_ = true;
    bool positionKnown_ = false;
    long long positionTicks_ = 0;
    long long tempoTicks_ = 0;
    bool pending_ = false;
    double pendingTime_ = 0.0;
    double pendingBeats_ = 0.0;
    bool jump_ = false;
    unsigned long long ticks_ = 0;
    unsigned long long gaps_ = 0;
    unsigned long long starts_ = 0;
};

}

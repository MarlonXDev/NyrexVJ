#pragma once

#include <array>
#include <deque>

namespace nvj {

enum class SyncSource { Manual = 0, Audio, MidiClock, Timecode, DjLink, Network };

constexpr int kSyncSourceCount = 6;

enum class SyncMode { Auto = 0, Manual, Audio, MidiClock, Timecode, DjLink, Network };

enum class LockState { Free, Searching, Acquiring, Locked, Lost };

constexpr double kMinBpm = 40.0;
constexpr double kMaxBpm = 240.0;

struct SyncObservation {
    SyncSource source = SyncSource::Manual;
    bool hasPhase = false;
    double beat = 0.0;
    int modulus = 0;
    bool hasTempo = false;
    double bpm = 0.0;
    double age = 0.0;
    bool jump = false;
};

SyncSource modeSource(SyncMode mode);
const char* syncSourceName(SyncSource source);
const char* syncModeName(SyncMode mode);
SyncMode syncModeFromName(const char* name);

class BeatClock {
public:
    void setMode(SyncMode mode);
    SyncMode mode() const { return mode_; }
    void setLatency(double seconds);
    double latency() const { return latency_; }

    void setTempo(double bpm);
    void tap();
    void resync();
    void shift(double beats);
    void nudge(double amount);

    void advance(double dt, bool running);
    void report(const SyncObservation& observation);

    double beat() const { return beat_; }
    double bpm() const { return bpm_; }
    double time() const { return time_; }

    bool present(SyncSource source) const;
    SyncSource active() const;
    LockState state() const;
    bool locked() const { return state() == LockState::Locked; }
    double phaseError() const { return errorSigned_; }
    double lockQuality() const;
    double pending() const { return pending_; }
    unsigned long long jumps() const { return jumps_; }
    double secondsSinceSeen(SyncSource source) const;

private:
    void applyTempo(SyncSource source, double bpm);
    void noteSwitch(SyncSource now);

    SyncMode mode_ = SyncMode::Auto;
    double latency_ = 0.0;
    double beat_ = 0.0;
    double bpm_ = 128.0;
    double time_ = 0.0;
    double pending_ = 0.0;
    double errorAbs_ = 1.0;
    double errorSigned_ = 0.0;
    int goodObservations_ = 0;
    bool running_ = true;
    unsigned long long jumps_ = 0;
    SyncSource lastActive_ = SyncSource::Manual;
    std::array<double, kSyncSourceCount> lastSeen_{};
    std::array<bool, kSyncSourceCount> everSeen_{};
    std::deque<double> taps_;
};

}

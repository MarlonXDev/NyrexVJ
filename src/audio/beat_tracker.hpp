#pragma once

#include <array>
#include <deque>

namespace nvj {

struct TrackerState {
    float bpm = 0.0f;
    float tempoConfidence = 0.0f;
    float beatConfidence = 0.0f;
    float phase = -1.0f;
    bool hasTempo = false;
    bool locked = false;
};

class BeatTracker {
public:
    static constexpr double kMinBpm = 80.0;
    static constexpr double kMaxBpm = 180.0;

    void reset();
    void setLatency(double seconds) { latency_ = seconds; }
    void addOnset(double time, float strength);
    int advance(double now, bool active);
    const TrackerState& state() const { return state_; }

private:
    static constexpr double kWindow = 10.0;
    static constexpr double kHalfLife = 3.0;
    static constexpr double kBinStep = 0.25;
    static constexpr int kBins = 401;
    static constexpr size_t kMaxOnsets = 160;

    struct Onset {
        double time = 0.0;
        float strength = 0.0f;
    };

    struct Alignment {
        double strength = 0.0;
        double phase = 0.0;
    };

    void evaluate(double now);
    Alignment align(double bpm, double now, double horizon) const;
    float confidenceFor(double strength, double weight) const;

    std::deque<Onset> onsets_;
    std::array<float, kBins> histogram_{};
    TrackerState state_;
    double bpm_ = 0.0;
    double gridPhase_ = 0.0;
    double latency_ = 0.0;
    double lastEvaluation_ = -10.0;
    double lastAdvance_ = 0.0;
    double lastFire_ = -10.0;
    double lastActive_ = 0.0;
    double previousFraction_ = -1.0;
    double candidateBpm_ = 0.0;
    int candidateCount_ = 0;
    double totalWeight_ = 0.0;
    bool locked_ = false;
};

}

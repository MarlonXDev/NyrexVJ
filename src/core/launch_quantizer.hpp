#pragma once

#include <cstdint>
#include <vector>

namespace nvj {

enum class QuantizeGrid { Off = 0, Beat, Bar, TwoBars, FourBars };

constexpr int kQuantizeGridCount = 5;

const char* quantizeGridName(QuantizeGrid grid);
QuantizeGrid quantizeGridFromName(const char* name);
double quantizeGridBeats(QuantizeGrid grid, int beatsPerBar);

enum class LaunchKind { Clip = 0, Column, Deck };

struct LaunchRequest {
    LaunchKind kind = LaunchKind::Clip;
    int a = 0;
    int b = 0;
};

struct QuantizeClock {
    double beat = 0.0;
    double bpm = 120.0;
    int beatsPerBar = 4;
    bool usable = true;
};

constexpr size_t kMaxPendingLaunches = 32;

class LaunchQuantizer {
public:
    void setGrid(QuantizeGrid grid);
    QuantizeGrid grid() const { return grid_; }

    bool request(const LaunchRequest& request, const QuantizeClock& clock, double now, std::vector<LaunchRequest>& immediate);
    void update(const QuantizeClock& clock, double now, std::vector<LaunchRequest>& due);
    void cancelAll();

    size_t pending() const { return queue_.size(); }
    double secondsToNext(const QuantizeClock& clock) const;
    uint64_t executedOnBeat() const { return onBeat_; }
    uint64_t executedImmediately() const { return immediate_; }
    uint64_t executedAfterClockLoss() const { return afterLoss_; }
    uint64_t executedAfterTimeout() const { return afterTimeout_; }
    uint64_t replaced() const { return replaced_; }

private:
    struct Pending {
        LaunchRequest request;
        double target = 0.0;
        double deadline = 0.0;
    };

    static double nextBoundary(double beat, double grid);
    static bool sameSlot(const LaunchRequest& a, const LaunchRequest& b);

    QuantizeGrid grid_ = QuantizeGrid::Off;
    std::vector<Pending> queue_;
    uint64_t onBeat_ = 0;
    uint64_t immediate_ = 0;
    uint64_t afterLoss_ = 0;
    uint64_t afterTimeout_ = 0;
    uint64_t replaced_ = 0;
};

}

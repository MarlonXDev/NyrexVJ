#include "core/launch_quantizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nvj {

namespace {

constexpr double kEpsilon = 1e-6;
constexpr double kMaxWaitSeconds = 30.0;

const char* const kGridNames[kQuantizeGridCount] = {"off", "beat", "bar", "2bars", "4bars"};

}

const char* quantizeGridName(QuantizeGrid grid) {
    const int index = static_cast<int>(grid);
    return index >= 0 && index < kQuantizeGridCount ? kGridNames[index] : kGridNames[0];
}

QuantizeGrid quantizeGridFromName(const char* name) {
    if (name) {
        for (int i = 0; i < kQuantizeGridCount; ++i) {
            if (std::strcmp(name, kGridNames[i]) == 0) {
                return static_cast<QuantizeGrid>(i);
            }
        }
    }
    return QuantizeGrid::Off;
}

double quantizeGridBeats(QuantizeGrid grid, int beatsPerBar) {
    const int bar = std::clamp(beatsPerBar, 1, 16);
    switch (grid) {
    case QuantizeGrid::Beat:
        return 1.0;
    case QuantizeGrid::Bar:
        return bar;
    case QuantizeGrid::TwoBars:
        return bar * 2.0;
    case QuantizeGrid::FourBars:
        return bar * 4.0;
    case QuantizeGrid::Off:
        break;
    }
    return 0.0;
}

double LaunchQuantizer::nextBoundary(double beat, double grid) {
    const double steps = std::ceil(beat / grid - kEpsilon);
    return steps * grid;
}

bool LaunchQuantizer::sameSlot(const LaunchRequest& a, const LaunchRequest& b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
    case LaunchKind::Clip:
        return a.a == b.a;
    case LaunchKind::Column:
    case LaunchKind::Deck:
        return true;
    }
    return false;
}

void LaunchQuantizer::setGrid(QuantizeGrid grid) {
    grid_ = grid;
}

bool LaunchQuantizer::request(const LaunchRequest& request, const QuantizeClock& clock, double now, std::vector<LaunchRequest>& immediate) {
    const double gridBeats = quantizeGridBeats(grid_, clock.beatsPerBar);
    const bool clockOk = clock.usable && std::isfinite(clock.beat) && std::isfinite(clock.bpm) && clock.bpm >= 1.0;
    if (grid_ == QuantizeGrid::Off || gridBeats <= 0.0) {
        immediate.push_back(request);
        ++immediate_;
        return false;
    }
    if (!clockOk) {
        immediate.push_back(request);
        ++afterLoss_;
        return false;
    }
    const double target = nextBoundary(clock.beat, gridBeats);
    if (target - clock.beat <= kEpsilon) {
        immediate.push_back(request);
        ++onBeat_;
        return false;
    }
    const auto old = std::find_if(queue_.begin(), queue_.end(), [&](const Pending& p) { return sameSlot(p.request, request); });
    if (old != queue_.end()) {
        queue_.erase(old);
        ++replaced_;
    }
    if (queue_.size() >= kMaxPendingLaunches) {
        immediate.push_back(request);
        ++immediate_;
        return false;
    }
    Pending pending;
    pending.request = request;
    pending.target = target;
    const double wait = (target - clock.beat) * 60.0 / clock.bpm;
    pending.deadline = now + std::min(kMaxWaitSeconds, wait * 1.5 + 0.5);
    queue_.push_back(pending);
    return true;
}

void LaunchQuantizer::update(const QuantizeClock& clock, double now, std::vector<LaunchRequest>& due) {
    if (queue_.empty()) {
        return;
    }
    const double gridBeats = quantizeGridBeats(grid_, clock.beatsPerBar);
    const bool clockOk = clock.usable && std::isfinite(clock.beat) && std::isfinite(clock.bpm) && clock.bpm >= 1.0;
    std::vector<Pending> keep;
    keep.reserve(queue_.size());
    for (Pending& pending : queue_) {
        if (grid_ == QuantizeGrid::Off || gridBeats <= 0.0) {
            due.push_back(pending.request);
            ++immediate_;
            continue;
        }
        if (!clockOk) {
            due.push_back(pending.request);
            ++afterLoss_;
            continue;
        }
        if (clock.beat < pending.target - gridBeats - kEpsilon) {
            pending.target = nextBoundary(clock.beat, gridBeats);
            const double wait = (pending.target - clock.beat) * 60.0 / clock.bpm;
            pending.deadline = now + std::min(kMaxWaitSeconds, wait * 1.5 + 0.5);
        }
        if (clock.beat >= pending.target - kEpsilon) {
            due.push_back(pending.request);
            ++onBeat_;
            continue;
        }
        if (now >= pending.deadline) {
            due.push_back(pending.request);
            ++afterTimeout_;
            continue;
        }
        keep.push_back(pending);
    }
    queue_ = std::move(keep);
}

void LaunchQuantizer::cancelAll() {
    queue_.clear();
}

double LaunchQuantizer::secondsToNext(const QuantizeClock& clock) const {
    if (queue_.empty() || clock.bpm < 1.0) {
        return -1.0;
    }
    double best = -1.0;
    for (const Pending& pending : queue_) {
        const double seconds = std::max(0.0, (pending.target - clock.beat) * 60.0 / clock.bpm);
        if (best < 0.0 || seconds < best) {
            best = seconds;
        }
    }
    return best;
}

}

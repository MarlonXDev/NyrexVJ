#pragma once

#include "core/composition.hpp"

#include <cstddef>
#include <vector>

namespace nvj {

struct FrameSummary {
    int count = 0;
    double avg = 0.0;
    double min = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

class FrameStats {
public:
    explicit FrameStats(size_t capacity = 240);

    void add(double milliseconds);
    void clear();
    size_t size() const { return filled_; }
    size_t capacity() const { return values_.size(); }
    double last() const;
    FrameSummary summary() const;
    double overBudgetFraction(double budgetMilliseconds) const;
    std::vector<float> ordered() const;

private:
    std::vector<double> values_;
    size_t next_ = 0;
    size_t filled_ = 0;
};

constexpr int kMinBudgetFps = 24;
constexpr int kMaxBudgetFps = 360;
constexpr int kDefaultBudgetFps = 60;

int clampBudgetFps(int fps);
double budgetMilliseconds(int fps);

struct RenderCost {
    int sources = 0;
    int layerEffects = 0;
    int masterEffects = 0;
    int composites = 0;
    int previews = 0;
    int finals = 1;
    int transitionPasses = 0;

    int layers() const { return sources; }
    int total() const { return sources + layerEffects + masterEffects + composites + previews + finals + transitionPasses; }
};

RenderCost estimateRenderCost(const Composition& comp);

}

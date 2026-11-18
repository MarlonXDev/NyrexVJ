#include "core/frame_stats.hpp"

#include <algorithm>

namespace nvj {

FrameStats::FrameStats(size_t capacity) : values_(std::max<size_t>(capacity, 1), 0.0) {}

void FrameStats::add(double milliseconds) {
    values_[next_] = milliseconds;
    next_ = (next_ + 1) % values_.size();
    filled_ = std::min(filled_ + 1, values_.size());
}

void FrameStats::clear() {
    std::fill(values_.begin(), values_.end(), 0.0);
    next_ = 0;
    filled_ = 0;
}

double FrameStats::last() const {
    if (filled_ == 0) {
        return 0.0;
    }
    return values_[(next_ + values_.size() - 1) % values_.size()];
}

FrameSummary FrameStats::summary() const {
    FrameSummary result;
    if (filled_ == 0) {
        return result;
    }
    std::vector<double> sorted;
    sorted.reserve(filled_);
    double sum = 0.0;
    for (size_t i = 0; i < filled_; ++i) {
        const double value = values_[(next_ + values_.size() - filled_ + i) % values_.size()];
        sorted.push_back(value);
        sum += value;
    }
    std::sort(sorted.begin(), sorted.end());
    const auto pick = [&](double fraction) { return sorted[static_cast<size_t>(fraction * static_cast<double>(sorted.size() - 1))]; };
    result.count = static_cast<int>(filled_);
    result.avg = sum / static_cast<double>(filled_);
    result.min = sorted.front();
    result.p50 = pick(0.5);
    result.p95 = pick(0.95);
    result.p99 = pick(0.99);
    result.max = sorted.back();
    return result;
}

double FrameStats::overBudgetFraction(double budgetMilliseconds) const {
    if (filled_ == 0) {
        return 0.0;
    }
    size_t over = 0;
    for (size_t i = 0; i < filled_; ++i) {
        if (values_[(next_ + values_.size() - filled_ + i) % values_.size()] > budgetMilliseconds) {
            ++over;
        }
    }
    return static_cast<double>(over) / static_cast<double>(filled_);
}

std::vector<float> FrameStats::ordered() const {
    std::vector<float> out;
    out.reserve(filled_);
    for (size_t i = 0; i < filled_; ++i) {
        out.push_back(static_cast<float>(values_[(next_ + values_.size() - filled_ + i) % values_.size()]));
    }
    return out;
}

int clampBudgetFps(int fps) {
    return std::clamp(fps, kMinBudgetFps, kMaxBudgetFps);
}

double budgetMilliseconds(int fps) {
    return 1000.0 / static_cast<double>(clampBudgetFps(fps));
}

RenderCost estimateRenderCost(const Composition& comp) {
    RenderCost cost;
    bool anySolo = false;
    for (const Layer& layer : comp.layers) {
        anySolo = anySolo || layer.solo;
    }
    const auto countEffects = [](const std::array<EffectSlot, kEffectSlots>& slots) {
        int count = 0;
        for (const EffectSlot& slot : slots) {
            if (slot.enabled && slot.effect >= 0) {
                ++count;
            }
        }
        return count;
    };
    for (const Layer& layer : comp.layers) {
        if (layer.bypass || (anySolo && !layer.solo) || layer.activeColumn < 0 || layer.activeColumn >= kColumnCount) {
            continue;
        }
        if (layer.cells[static_cast<size_t>(layer.activeColumn)] < 0) {
            continue;
        }
        ++cost.sources;
        cost.layerEffects += countEffects(layer.fx);
        ++cost.composites;
        ++cost.previews;
    }
    cost.masterEffects = countEffects(comp.master.fx);
    return cost;
}

}

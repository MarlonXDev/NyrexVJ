#include "core/render_policy.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr double kAverageWeight = 0.15;
constexpr int kStaleFrames = 90;

}

int clampQualityMode(int mode) {
    return std::clamp(mode, 0, static_cast<int>(QualityModeCount) - 1);
}

float fixedQualityScale(int mode) {
    switch (clampQualityMode(mode)) {
    case QualityBalanced:
        return 0.75f;
    case QualityPerformance:
        return 0.5f;
    default:
        return 1.0f;
    }
}

int scaledDimension(int full, float scale) {
    if (full <= 0) {
        return 0;
    }
    if (scale >= 0.999f) {
        return full;
    }
    const int scaled = static_cast<int>(std::lround(static_cast<double>(full) * static_cast<double>(scale)));
    return std::clamp(scaled & ~1, std::min(64, full), full);
}

void QualityController::reset() {
    step_ = 0;
    scale_ = 1.0f;
    average_ = 0.0;
    primed_ = false;
    over_ = 0;
    under_ = 0;
    cooldown_ = 0;
}

float QualityController::update(double gpuMs, double budgetMs) {
    if (gpuMs <= 0.0 || budgetMs <= 0.0) {
        return scale_;
    }
    if (!primed_) {
        average_ = gpuMs;
        primed_ = true;
    } else {
        average_ = average_ * 0.9 + gpuMs * 0.1;
    }
    if (cooldown_ > 0) {
        --cooldown_;
        over_ = 0;
        under_ = 0;
        return scale_;
    }
    if (average_ > budgetMs * 0.92) {
        under_ = 0;
        if (++over_ >= 20 && step_ < kMaxStep) {
            ++step_;
            scale_ = 1.0f - 0.125f * static_cast<float>(step_);
            over_ = 0;
            cooldown_ = 60;
            primed_ = false;
        }
    } else if (average_ < budgetMs * 0.6 && step_ > 0) {
        over_ = 0;
        if (++under_ >= 180) {
            const float next = 1.0f - 0.125f * static_cast<float>(step_ - 1);
            const double ratio = static_cast<double>(next) / static_cast<double>(scale_);
            const double predicted = average_ * ratio * ratio;
            if (predicted < budgetMs * 0.85) {
                --step_;
                scale_ = next;
                cooldown_ = 120;
                primed_ = false;
            }
            under_ = 0;
        }
    } else {
        over_ = 0;
        under_ = 0;
    }
    return scale_;
}

bool IdleRelease::update(bool needed) {
    if (needed) {
        idle_ = 0;
        return false;
    }
    if (idle_ < limit_) {
        ++idle_;
    }
    return idle_ >= limit_;
}

void ProfileTable::beginFrame() {
    seen_.assign(rows_.size(), false);
    frameTotal_ = 0.0;
}

void ProfileTable::add(const ProfileSample& sample) {
    frameTotal_ += sample.ms;
    for (size_t i = 0; i < rows_.size(); ++i) {
        ProfileRow& row = rows_[i];
        if (row.kind == sample.kind && row.deck == sample.deck && row.layer == sample.layer && row.slot == sample.slot && row.name == sample.name) {
            if (i < seen_.size() && seen_[i]) {
                row.lastMs += sample.ms;
                row.averageMs += sample.ms * kAverageWeight;
                continue;
            }
            row.lastMs = sample.ms;
            row.averageMs = row.averageMs * (1.0 - kAverageWeight) + sample.ms * kAverageWeight;
            row.missing = 0;
            if (i < seen_.size()) {
                seen_[i] = true;
            }
            return;
        }
    }
    ProfileRow row;
    row.kind = sample.kind;
    row.deck = sample.deck;
    row.layer = sample.layer;
    row.slot = sample.slot;
    row.name = sample.name;
    row.averageMs = sample.ms;
    row.lastMs = sample.ms;
    rows_.push_back(row);
    seen_.push_back(true);
}

void ProfileTable::endFrame() {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (i < seen_.size() && !seen_[i]) {
            ++rows_[i].missing;
            rows_[i].lastMs = 0.0;
        }
    }
    std::vector<ProfileRow> kept;
    kept.reserve(rows_.size());
    for (const ProfileRow& row : rows_) {
        if (row.missing < kStaleFrames) {
            kept.push_back(row);
        }
    }
    rows_ = std::move(kept);
    seen_.clear();
    lastTotal_ = frameTotal_;
    averageTotal_ = frames_ == 0 ? frameTotal_ : averageTotal_ * (1.0 - kAverageWeight) + frameTotal_ * kAverageWeight;
    ++frames_;
    double sum = 0.0;
    for (const ProfileRow& row : rows_) {
        sum += row.averageMs;
    }
    for (ProfileRow& row : rows_) {
        row.share = sum > 0.0 ? row.averageMs / sum : 0.0;
    }
}

void ProfileTable::clear() {
    rows_.clear();
    seen_.clear();
    averageTotal_ = 0.0;
    lastTotal_ = 0.0;
    frameTotal_ = 0.0;
    frames_ = 0;
}

std::vector<ProfileRow> ProfileTable::sorted() const {
    std::vector<ProfileRow> copy = rows_;
    std::stable_sort(copy.begin(), copy.end(), [](const ProfileRow& a, const ProfileRow& b) { return a.averageMs > b.averageMs; });
    return copy;
}

}

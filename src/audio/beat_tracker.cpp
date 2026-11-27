#include "audio/beat_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kPairLimit = 3.05;
constexpr double kMinPair = 0.25;
constexpr double kPairWeights[4] = {1.0, 0.8, 0.6, 0.5};
constexpr double kEnterLock = 0.4;
constexpr double kLeaveLock = 0.25;
constexpr double kSilenceHold = 1.5;

double decayed(double age) {
    return std::exp2(-age / 3.0);
}

}

void BeatTracker::reset() {
    onsets_.clear();
    histogram_.fill(0.0f);
    state_ = TrackerState{};
    bpm_ = 0.0;
    gridPhase_ = 0.0;
    lastEvaluation_ = -10.0;
    lastAdvance_ = 0.0;
    lastFire_ = -10.0;
    lastActive_ = 0.0;
    previousFraction_ = -1.0;
    candidateBpm_ = 0.0;
    candidateCount_ = 0;
    totalWeight_ = 0.0;
    locked_ = false;
}

void BeatTracker::addOnset(double time, float strength) {
    if (!onsets_.empty() && time <= onsets_.back().time) {
        return;
    }
    onsets_.push_back({time, std::clamp(strength, 0.0f, 1.0f)});
    while (onsets_.size() > kMaxOnsets) {
        onsets_.pop_front();
    }
}

float BeatTracker::confidenceFor(double strength, double weight) const {
    const double shaped = std::clamp((strength - 0.3) / 0.7, 0.0, 1.0);
    const double evidence = std::clamp(weight / 2.5, 0.0, 1.0);
    return static_cast<float>(shaped * evidence);
}

BeatTracker::Alignment BeatTracker::align(double bpm, double now, double horizon) const {
    Alignment result;
    if (bpm <= 0.0) {
        return result;
    }
    const double period = 60.0 / bpm;
    double cosSum = 0.0;
    double sinSum = 0.0;
    double weightSum = 0.0;
    for (const Onset& onset : onsets_) {
        const double age = now - onset.time;
        if (age > horizon) {
            continue;
        }
        const double weight = static_cast<double>(onset.strength) * decayed(age);
        const double squared = weight * weight;
        const double angle = kTwoPi * (onset.time / period);
        cosSum += squared * std::cos(angle);
        sinSum += squared * std::sin(angle);
        weightSum += squared;
    }
    if (weightSum <= 1e-9) {
        return result;
    }
    result.strength = std::sqrt(cosSum * cosSum + sinSum * sinSum) / weightSum;
    double cycles = std::atan2(sinSum, cosSum) / kTwoPi;
    cycles -= std::floor(cycles);
    result.phase = cycles;
    return result;
}

void BeatTracker::evaluate(double now) {
    lastEvaluation_ = now;
    while (!onsets_.empty() && now - onsets_.front().time > kWindow) {
        onsets_.pop_front();
    }
    const size_t count = onsets_.size();
    totalWeight_ = 0.0;
    std::array<double, kMaxOnsets> weights{};
    for (size_t i = 0; i < count; ++i) {
        weights[i] = static_cast<double>(onsets_[i].strength) * decayed(now - onsets_[i].time);
        totalWeight_ += weights[i];
    }
    if (count < 4 || totalWeight_ < 0.4) {
        state_.tempoConfidence *= 0.9f;
        state_.beatConfidence *= 0.9f;
        if (locked_ && state_.tempoConfidence < kLeaveLock) {
            locked_ = false;
        }
        return;
    }

    histogram_.fill(0.0f);
    for (size_t j = 1; j < count; ++j) {
        for (size_t i = j; i-- > 0;) {
            const double distance = onsets_[j].time - onsets_[i].time;
            if (distance > kPairLimit) {
                break;
            }
            if (distance < kMinPair) {
                continue;
            }
            const double pairWeight = weights[i] * weights[j];
            for (int k = 1; k <= 4; ++k) {
                const double period = distance / static_cast<double>(k);
                if (period < 60.0 / kMaxBpm || period > 60.0 / kMinBpm) {
                    continue;
                }
                const double bpm = 60.0 / period;
                const double sigma = bpm * 0.012;
                const double centre = (bpm - kMinBpm) / kBinStep;
                const int radius = static_cast<int>(std::ceil(2.5 * sigma / kBinStep));
                const int from = std::max(0, static_cast<int>(std::floor(centre)) - radius);
                const int to = std::min(kBins - 1, static_cast<int>(std::floor(centre)) + radius);
                for (int bin = from; bin <= to; ++bin) {
                    const double offset = (kMinBpm + bin * kBinStep - bpm) / sigma;
                    histogram_[static_cast<size_t>(bin)] += static_cast<float>(pairWeight * kPairWeights[k - 1] * std::exp(-0.5 * offset * offset));
                }
            }
        }
    }
    int best = -1;
    float bestValue = 0.0f;
    for (int bin = 0; bin < kBins; ++bin) {
        const double bpm = kMinBpm + bin * kBinStep;
        const double octave = std::log2(bpm / 125.0) / 0.7;
        const float shaped = histogram_[static_cast<size_t>(bin)] * static_cast<float>(std::exp(-0.5 * octave * octave));
        if (shaped > bestValue) {
            bestValue = shaped;
            best = bin;
        }
    }
    if (best < 0) {
        return;
    }
    double refinedSum = 0.0;
    double refinedWeight = 0.0;
    const double bestBpm = kMinBpm + best * kBinStep;
    for (int bin = 0; bin < kBins; ++bin) {
        const double bpm = kMinBpm + bin * kBinStep;
        if (std::fabs(bpm - bestBpm) <= bestBpm * 0.025) {
            refinedSum += histogram_[static_cast<size_t>(bin)] * bpm;
            refinedWeight += histogram_[static_cast<size_t>(bin)];
        }
    }
    const double candidate = refinedWeight > 0.0 ? refinedSum / refinedWeight : bestBpm;
    const Alignment candidateAlign = align(candidate, now, kWindow);
    const float candidateConfidence = confidenceFor(candidateAlign.strength, totalWeight_);

    if (bpm_ <= 0.0) {
        if (candidateConfidence >= 0.35f) {
            if (candidateBpm_ > 0.0 && std::fabs(candidate - candidateBpm_) < candidateBpm_ * 0.04) {
                candidateCount_ += 1;
            } else {
                candidateCount_ = 1;
            }
            candidateBpm_ = candidate;
            if (candidateCount_ >= 2) {
                bpm_ = candidate;
                candidateCount_ = 0;
            }
        } else {
            candidateCount_ = 0;
        }
    } else {
        const Alignment currentAlign = align(bpm_, now, kWindow);
        const float currentConfidence = confidenceFor(currentAlign.strength, totalWeight_);
        if (std::fabs(candidate - bpm_) < bpm_ * 0.04) {
            bpm_ += (candidate - bpm_) * 0.35;
            candidateCount_ = 0;
        } else if ((candidateConfidence >= 0.35f && candidateConfidence > currentConfidence * 1.25f + 0.05f) || (currentConfidence < 0.2f && candidateConfidence >= 0.3f)) {
            if (candidateBpm_ > 0.0 && std::fabs(candidate - candidateBpm_) < candidateBpm_ * 0.04) {
                candidateCount_ += 1;
            } else {
                candidateCount_ = 1;
            }
            candidateBpm_ = candidate;
            if (candidateCount_ >= 3) {
                bpm_ = candidate;
                candidateCount_ = 0;
            }
        } else {
            candidateCount_ = 0;
        }
    }

    if (bpm_ <= 0.0) {
        state_.tempoConfidence = candidateConfidence;
        state_.beatConfidence = 0.0f;
        return;
    }
    const Alignment current = align(bpm_, now, kWindow);
    const double period = 60.0 / bpm_;
    const Alignment recent = align(bpm_, now, period * 4.5);
    gridPhase_ = current.phase;
    state_.bpm = static_cast<float>(bpm_);
    state_.hasTempo = true;
    state_.tempoConfidence = confidenceFor(current.strength, totalWeight_);
    double recentWeight = 0.0;
    for (const Onset& onset : onsets_) {
        if (now - onset.time <= period * 4.5) {
            recentWeight += static_cast<double>(onset.strength);
        }
    }
    const double shapedRecent = std::clamp((recent.strength - 0.3) / 0.7, 0.0, 1.0);
    state_.beatConfidence = static_cast<float>(shapedRecent * std::clamp(recentWeight / 1.2, 0.0, 1.0));
    if (!locked_ && state_.tempoConfidence >= kEnterLock) {
        locked_ = true;
    } else if (locked_ && state_.tempoConfidence < kLeaveLock) {
        locked_ = false;
    }
}

int BeatTracker::advance(double now, bool active) {
    if (active) {
        lastActive_ = now;
    }
    if (now - lastEvaluation_ >= 0.2) {
        evaluate(now);
    }
    const double previous = lastAdvance_;
    lastAdvance_ = now;
    const bool silent = now - lastActive_ > kSilenceHold;
    state_.locked = locked_ && bpm_ > 0.0 && !silent;
    if (!state_.locked) {
        state_.phase = -1.0f;
        previousFraction_ = -1.0;
        return 0;
    }
    const double period = 60.0 / bpm_;
    const double position = (now + latency_) / period - gridPhase_;
    const double fraction = position - std::floor(position);
    state_.phase = static_cast<float>(fraction);
    int fired = 0;
    if (previousFraction_ >= 0.0 && now > previous) {
        const double forward = fraction - previousFraction_ + (fraction < previousFraction_ ? 1.0 : 0.0);
        if (fraction < previousFraction_ && forward <= 0.5 && now - lastFire_ > period * 0.6) {
            fired = 1;
            lastFire_ = now - fraction * period;
        }
    }
    previousFraction_ = fraction;
    return fired;
}

}

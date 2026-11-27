#include "audio/audio_analyzer.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kLowFrequency = 30.0f;
constexpr float kHighFrequency = 16000.0f;

float normalizeDb(float amplitude) {
    float db = 20.0f * std::log10(amplitude + 1e-7f);
    return std::clamp((db + 80.0f) / 70.0f, 0.0f, 1.0f);
}

}

float AudioFeatures::band(int index) const {
    switch (index) {
    case 1:
        return bass;
    case 2:
        return mid;
    case 3:
        return high;
    case 4:
        return level;
    case 5:
        return kick;
    default:
        return 0.0f;
    }
}

AudioFeatures virtualAudio(double beat, double time) {
    AudioFeatures f;
    f.active = false;
    const float phase = static_cast<float>(beat - std::floor(beat));
    f.kick = std::exp(-phase * 5.0f);
    f.bass = 0.35f + 0.65f * f.kick;
    f.mid = 0.45f + 0.2f * std::sin(static_cast<float>(time) * 1.7f) + 0.15f * f.kick;
    const float offPhase = std::fabs(phase - 0.5f);
    f.high = 0.3f + 0.35f * std::exp(-offPhase * 10.0f);
    f.level = 0.4f + 0.4f * f.kick;
    f.bpm = 0.0f;
    for (int i = 0; i < kSpectrumBins; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(kSpectrumBins - 1);
        float base = std::pow(1.0f - x, 0.7f) * 0.55f;
        float wobble = 0.55f + 0.45f * std::sin(static_cast<float>(time) * 2.3f + static_cast<float>(i) * 0.55f);
        float pulse = f.kick * std::exp(-x * 5.0f) * 0.6f;
        f.spectrum[static_cast<size_t>(i)] = std::clamp(base * wobble + pulse, 0.0f, 1.0f);
    }
    for (int i = 0; i < kWaveSamples; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(kWaveSamples - 1);
        float v = std::sin(x * 6.2831853f * 3.0f + static_cast<float>(time) * 4.0f) * (0.18f + 0.22f * f.kick) + std::sin(x * 6.2831853f * 11.0f - static_cast<float>(time) * 7.0f) * 0.06f * f.mid;
        f.wave[static_cast<size_t>(i)] = std::clamp(0.5f + v, 0.0f, 1.0f);
    }
    return f;
}

AudioAnalyzer::AudioAnalyzer() {
    ring_.assign(kFftSize, 0.0f);
    window_.resize(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1));
    }
    cosTable_.resize(kFftSize / 2);
    sinTable_.resize(kFftSize / 2);
    for (int i = 0; i < kFftSize / 2; ++i) {
        float angle = -2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFftSize);
        cosTable_[static_cast<size_t>(i)] = std::cos(angle);
        sinTable_[static_cast<size_t>(i)] = std::sin(angle);
    }
    bitReverse_.resize(kFftSize);
    int bits = 0;
    while ((1 << bits) < kFftSize) {
        ++bits;
    }
    for (int i = 0; i < kFftSize; ++i) {
        int reversed = 0;
        for (int b = 0; b < bits; ++b) {
            if (i & (1 << b)) {
                reversed |= 1 << (bits - 1 - b);
            }
        }
        bitReverse_[static_cast<size_t>(i)] = reversed;
    }
    re_.assign(kFftSize, 0.0f);
    im_.assign(kFftSize, 0.0f);
    magnitude_.assign(kFftSize / 2 + 1, 0.0f);
    configure(48000);
}

void AudioAnalyzer::configure(int sampleRate) {
    sampleRate_ = std::max(8000, sampleRate);
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
    const float top = std::min(kHighFrequency, nyquist * 0.95f);
    const float ratio = top / kLowFrequency;
    for (int i = 0; i <= kSpectrumBins; ++i) {
        float frequency = kLowFrequency * std::pow(ratio, static_cast<float>(i) / static_cast<float>(kSpectrumBins));
        float position = frequency * static_cast<float>(kFftSize) / static_cast<float>(sampleRate_);
        binEdges_[static_cast<size_t>(i)] = std::clamp(static_cast<int>(std::floor(position)), 1, kFftSize / 2 - 1);
    }
    for (int i = 0; i < kSpectrumBins; ++i) {
        binFrequency_[static_cast<size_t>(i)] = kLowFrequency * std::pow(ratio, (static_cast<float>(i) + 0.5f) / static_cast<float>(kSpectrumBins));
    }
}

void AudioAnalyzer::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    writeIndex_ = 0;
    pushedSinceAdvance_ = 0;
    agcPeak_ = 0.5f;
    previousLow_ = 0.0f;
    fluxMean_ = 0.0f;
    fluxDeviation_ = 0.0f;
    clock_ = 0.0;
    lastBeatTime_ = -10.0;
    intervals_.clear();
    bpmSmoothed_ = 0.0f;
    tracker_.reset();
    previousRaw_.fill(0.0f);
    onsetOlder_ = 0.0f;
    onsetPrevious_ = 0.0f;
    onsetThresholdPrevious_ = 0.0f;
    previousTime_ = 0.0;
    humpStart_ = 0.0;
    inHump_ = false;
    onsetMean_ = 0.0f;
    onsetDeviation_ = 0.0f;
    onsetPeak_ = 0.0f;
    lastOnsetTime_ = -10.0;
    lastOnsetAt_ = 0.0;
    bool active = features_.active;
    features_ = AudioFeatures{};
    features_.active = active;
}

void AudioAnalyzer::push(const float* mono, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        ring_[writeIndex_] = mono[i];
        writeIndex_ = (writeIndex_ + 1) % ring_.size();
    }
    pushedSinceAdvance_ += count;
}

void AudioAnalyzer::computeSpectrum() {
    for (int i = 0; i < kFftSize; ++i) {
        size_t source = (writeIndex_ + static_cast<size_t>(i)) % ring_.size();
        re_[static_cast<size_t>(bitReverse_[static_cast<size_t>(i)])] = ring_[source] * window_[static_cast<size_t>(i)] * gain_;
    }
    std::fill(im_.begin(), im_.end(), 0.0f);
    for (int size = 2; size <= kFftSize; size <<= 1) {
        const int half = size / 2;
        const int step = kFftSize / size;
        for (int start = 0; start < kFftSize; start += size) {
            for (int k = 0; k < half; ++k) {
                const float wr = cosTable_[static_cast<size_t>(k * step)];
                const float wi = sinTable_[static_cast<size_t>(k * step)];
                const size_t a = static_cast<size_t>(start + k);
                const size_t b = a + static_cast<size_t>(half);
                const float tr = re_[b] * wr - im_[b] * wi;
                const float ti = re_[b] * wi + im_[b] * wr;
                re_[b] = re_[a] - tr;
                im_[b] = im_[a] - ti;
                re_[a] += tr;
                im_[a] += ti;
            }
        }
    }
    const float scale = 4.0f / static_cast<float>(kFftSize);
    for (int i = 0; i <= kFftSize / 2; ++i) {
        size_t index = static_cast<size_t>(i);
        magnitude_[index] = std::sqrt(re_[index] * re_[index] + im_[index] * im_[index]) * scale;
    }
    for (int i = 0; i < kSpectrumBins; ++i) {
        int from = binEdges_[static_cast<size_t>(i)];
        int to = std::max(from, binEdges_[static_cast<size_t>(i) + 1]);
        float peak = 0.0f;
        for (int k = from; k <= to; ++k) {
            peak = std::max(peak, magnitude_[static_cast<size_t>(k)]);
        }
        if (to == from) {
            float position = binFrequency_[static_cast<size_t>(i)] * static_cast<float>(kFftSize) / static_cast<float>(sampleRate_);
            int lower = std::clamp(static_cast<int>(std::floor(position)), 0, kFftSize / 2 - 1);
            float frac = position - static_cast<float>(lower);
            peak = magnitude_[static_cast<size_t>(lower)] * (1.0f - frac) + magnitude_[static_cast<size_t>(lower) + 1] * frac;
        }
        rawSpectrum_[static_cast<size_t>(i)] = normalizeDb(peak);
    }
}

void AudioAnalyzer::setImproved(bool improved) {
    if (improved_ == improved) {
        return;
    }
    improved_ = improved;
    tracker_.reset();
    previousRaw_.fill(0.0f);
    onsetOlder_ = 0.0f;
    onsetPrevious_ = 0.0f;
    onsetThresholdPrevious_ = 0.0f;
    onsetMean_ = 0.0f;
    onsetDeviation_ = 0.0f;
    onsetPeak_ = 0.0f;
    lastOnsetTime_ = -10.0;
    features_.beatConfidence = 0.0f;
    features_.beatPhase = -1.0f;
    features_.onsetStrength = 0.0f;
    features_.tracking = false;
}

void AudioAnalyzer::setCrossovers(float low, float high) {
    crossLow_ = std::clamp(low, 60.0f, 400.0f);
    crossHigh_ = std::clamp(high, crossLow_ + 200.0f, 6000.0f);
}

void AudioAnalyzer::setLatency(double seconds) {
    tracker_.setLatency(std::clamp(seconds, -0.5, 0.5));
}

void AudioAnalyzer::detectBeat(double dt, float lowEnergy, bool emit) {
    const float flux = std::max(0.0f, lowEnergy - previousLow_);
    previousLow_ = lowEnergy;
    const float tau = 0.8f;
    const float blend = 1.0f - std::exp(-static_cast<float>(dt) / tau);
    const float deviation = std::fabs(flux - fluxMean_);
    fluxMean_ += (flux - fluxMean_) * blend;
    fluxDeviation_ += (deviation - fluxDeviation_) * blend;

    const float factor = 1.0f + (1.0f - std::clamp(sensitivity_, 0.0f, 1.0f)) * 3.0f;
    const float threshold = fluxMean_ + fluxDeviation_ * factor + 0.015f;
    if (emit) {
        features_.beatNow = false;
    }
    if (flux > threshold && lowEnergy > 0.04f && clock_ - lastBeatTime_ > 0.27) {
        if (lastBeatTime_ > 0.0) {
            double interval = clock_ - lastBeatTime_;
            if (interval < 1.6) {
                intervals_.push_back(interval);
                while (intervals_.size() > 24) {
                    intervals_.pop_front();
                }
            } else {
                intervals_.clear();
            }
        }
        lastBeatTime_ = clock_;
        if (emit) {
            features_.beatNow = true;
            features_.kick = 1.0f;
            features_.beatCount += 1;
        }
        estimateTempo();
    }
}

void AudioAnalyzer::detectOnsets(double dt, bool active) {
    float flux[3] = {0.0f, 0.0f, 0.0f};
    int bins[3] = {0, 0, 0};
    for (int i = 0; i < kSpectrumBins; ++i) {
        const size_t index = static_cast<size_t>(i);
        const float frequency = binFrequency_[index];
        const int band = frequency < crossLow_ ? 0 : (frequency < crossHigh_ ? 1 : 2);
        const float rise = rawSpectrum_[index] - previousRaw_[index];
        if (rise > 0.0f) {
            flux[band] += rise;
        }
        bins[band] += 1;
        previousRaw_[index] = rawSpectrum_[index];
    }
    const float weights[3] = {1.0f, 0.6f, 0.4f};
    float onset = 0.0f;
    for (int b = 0; b < 3; ++b) {
        if (bins[b] > 0) {
            onset += weights[b] * flux[b] / static_cast<float>(bins[b]);
        }
    }
    onset *= static_cast<float>(std::clamp((1.0 / 60.0) / dt, 0.25, 4.0));
    const float blend = 1.0f - std::exp(-static_cast<float>(dt) / 0.8f);
    const float deviation = std::fabs(onset - onsetMean_);
    onsetMean_ += (onset - onsetMean_) * blend;
    onsetDeviation_ += (deviation - onsetDeviation_) * blend;
    onsetPeak_ = std::max(onset, onsetPeak_ * std::exp(-static_cast<float>(dt) / 4.0f));
    const float factor = 1.0f + (1.0f - std::clamp(sensitivity_, 0.0f, 1.0f)) * 3.0f;
    const float threshold = onsetMean_ + onsetDeviation_ * factor + 0.01f;

    features_.onsetStrength *= std::exp(-static_cast<float>(dt) * 8.0f);
    const double now = clock_;
    if (onset > threshold) {
        if (!inHump_) {
            inHump_ = true;
            const float rise = onset - onsetPrevious_;
            const double fraction = rise > 1e-9f ? std::clamp(static_cast<double>((threshold - onsetPrevious_) / rise), 0.0, 1.0) : 1.0;
            humpStart_ = previousTime_ + fraction * (now - previousTime_);
        }
    } else {
        inHump_ = false;
    }
    const bool peak = onsetPrevious_ > onsetThresholdPrevious_ && onsetPrevious_ >= onsetOlder_ && onsetPrevious_ > onset;
    if (active && peak && previousTime_ - lastOnsetTime_ > 0.1) {
        const float strength = std::clamp(onsetPrevious_ / std::max(onsetPeak_, 0.02f), 0.0f, 1.0f);
        if (strength >= 0.15f) {
            lastOnsetTime_ = previousTime_;
            lastOnsetAt_ = humpStart_;
            features_.onsetStrength = std::max(features_.onsetStrength, strength);
            tracker_.addOnset(humpStart_, strength);
        }
    }
    onsetOlder_ = onsetPrevious_;
    onsetPrevious_ = onset;
    onsetThresholdPrevious_ = threshold;
    previousTime_ = now;
}

void AudioAnalyzer::estimateTempo() {
    if (intervals_.size() < 6) {
        return;
    }
    std::vector<double> folded;
    folded.reserve(intervals_.size());
    for (double interval : intervals_) {
        double value = interval;
        while (value < 0.33) {
            value *= 2.0;
        }
        while (value > 0.75) {
            value *= 0.5;
        }
        folded.push_back(value);
    }
    std::vector<double> sorted = folded;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    size_t agree = 0;
    for (double value : folded) {
        if (std::fabs(value - median) < median * 0.07) {
            ++agree;
        }
    }
    const float confidence = static_cast<float>(agree) / static_cast<float>(folded.size());
    const float bpm = static_cast<float>(60.0 / median);
    features_.bpmConfidence = confidence;
    if (confidence < 0.5f) {
        return;
    }
    if (bpmSmoothed_ <= 0.0f || std::fabs(bpm - bpmSmoothed_) > bpmSmoothed_ * 0.08f) {
        bpmSmoothed_ = bpm;
    } else {
        bpmSmoothed_ += (bpm - bpmSmoothed_) * 0.25f;
    }
    features_.bpm = bpmSmoothed_;
}

void AudioAnalyzer::advance(double dt) {
    dt = std::clamp(dt, 0.0001, 0.25);
    clock_ += dt;
    if (pushedSinceAdvance_ == 0) {
        const size_t silent = std::min<size_t>(4096, static_cast<size_t>(dt * static_cast<double>(sampleRate_)));
        std::vector<float> zeros(silent, 0.0f);
        push(zeros.data(), zeros.size());
    }
    pushedSinceAdvance_ = 0;

    computeSpectrum();

    float peak = 0.0f;
    for (float value : rawSpectrum_) {
        peak = std::max(peak, value);
    }
    const float agcRate = peak > agcPeak_ ? 8.0f : 0.15f;
    agcPeak_ += (peak - agcPeak_) * (1.0f - std::exp(-agcRate * static_cast<float>(dt)));
    const float agc = 1.0f / std::max(0.45f, agcPeak_);

    const float release = 0.04f + std::clamp(smoothing_, 0.0f, 1.0f) * 0.3f;
    const float attackBlend = 1.0f - std::exp(-static_cast<float>(dt) / 0.012f);
    const float releaseBlend = 1.0f - std::exp(-static_cast<float>(dt) / release);

    float bandPeak[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < kSpectrumBins; ++i) {
        const size_t index = static_cast<size_t>(i);
        float value = std::clamp(rawSpectrum_[index] * agc, 0.0f, 1.0f);
        float& shown = features_.spectrum[index];
        shown += (value - shown) * (value > shown ? attackBlend : releaseBlend);
        const float frequency = binFrequency_[index];
        const int band = frequency < crossLow_ ? 0 : (frequency < crossHigh_ ? 1 : 2);
        if (frequency < 10000.0f) {
            bandPeak[band] = std::max(bandPeak[band], value);
        }
    }
    float* targets[3] = {&features_.bass, &features_.mid, &features_.high};
    for (int b = 0; b < 3; ++b) {
        *targets[b] += (bandPeak[b] - *targets[b]) * (bandPeak[b] > *targets[b] ? attackBlend : releaseBlend);
    }

    float sumSquares = 0.0f;
    const size_t windowSize = 1024;
    for (size_t i = 0; i < windowSize; ++i) {
        size_t source = (writeIndex_ + ring_.size() - 1 - i) % ring_.size();
        float sample = ring_[source] * gain_;
        sumSquares += sample * sample;
    }
    const float rms = std::sqrt(sumSquares / static_cast<float>(windowSize));
    const float levelValue = std::clamp((20.0f * std::log10(rms + 1e-7f) + 60.0f) / 55.0f, 0.0f, 1.0f);
    features_.level += (levelValue - features_.level) * (levelValue > features_.level ? attackBlend : releaseBlend);

    for (int i = 0; i < kWaveSamples; ++i) {
        size_t source = (writeIndex_ + ring_.size() - static_cast<size_t>(kWaveSamples - i) * 4) % ring_.size();
        features_.wave[static_cast<size_t>(i)] = std::clamp(ring_[source] * gain_ * 0.5f + 0.5f, 0.0f, 1.0f);
    }

    float lowEnergy = 0.0f;
    int lowCount = 0;
    for (int i = 0; i < kSpectrumBins; ++i) {
        if (binFrequency_[static_cast<size_t>(i)] < 200.0f) {
            lowEnergy += rawSpectrum_[static_cast<size_t>(i)];
            ++lowCount;
        }
    }
    lowEnergy = lowCount > 0 ? lowEnergy / static_cast<float>(lowCount) : 0.0f;
    features_.kick *= std::exp(-static_cast<float>(dt) * 5.0f);
    if (!improved_) {
        detectBeat(dt, lowEnergy, true);
        if (clock_ - lastBeatTime_ > 4.0) {
            features_.bpmConfidence *= 0.98f;
        }
        return;
    }
    const bool heard = rms > 0.0015f;
    detectOnsets(dt, heard);
    const int grid = tracker_.advance(clock_, heard);
    const TrackerState& tracked = tracker_.state();
    detectBeat(dt, lowEnergy, !tracked.locked);
    if (tracked.locked && grid > 0) {
        features_.beatNow = true;
        features_.kick = 1.0f;
        features_.beatCount += 1;
    } else if (tracked.locked) {
        features_.beatNow = false;
    }
    features_.tracking = tracked.locked;
    features_.beatPhase = tracked.phase;
    features_.beatConfidence = tracked.beatConfidence;
    if (tracked.hasTempo) {
        features_.bpm = tracked.bpm;
        features_.bpmConfidence = tracked.tempoConfidence;
    } else {
        features_.bpm = 0.0f;
        features_.bpmConfidence = 0.0f;
    }
}

}

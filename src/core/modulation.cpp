#include "core/modulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nvj {

namespace {

constexpr double kTwoPi = 6.283185307179586;

float hash01(long long cell, int seed) {
    std::uint64_t x = static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(seed + 1) * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(x >> 40) / 16777216.0f;
}

}

float lfoCycleBeats(const Lfo& lfo) {
    return kLfoRateBeats[std::clamp(lfo.rate, 0, kLfoRateCount - 1)];
}

float lfoValue(const Lfo& lfo, int index, double beat) {
    if (!std::isfinite(beat)) {
        return 0.0f;
    }
    const double position = beat / static_cast<double>(lfoCycleBeats(lfo)) + static_cast<double>(std::clamp(lfo.phase, 0.0f, 1.0f));
    const double whole = std::floor(position);
    const float p = static_cast<float>(position - whole);
    switch (lfo.shape) {
    case LfoTriangle:
        return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
    case LfoSaw:
        return p;
    case LfoSquare:
        return p < 0.5f ? 1.0f : 0.0f;
    case LfoRandom:
        return hash01(static_cast<long long>(whole), index);
    case LfoSmooth: {
        const float a = hash01(static_cast<long long>(whole), index);
        const float b = hash01(static_cast<long long>(whole) + 1, index);
        const float t = p * p * (3.0f - 2.0f * p);
        return a + (b - a) * t;
    }
    default:
        return static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(p)));
    }
}

int clampBeatsPerBar(int beats) {
    return std::clamp(beats, kMinBeatsPerBar, kMaxBeatsPerBar);
}

bool isAudioSource(int source) {
    return source >= BandBass && source <= BandKick;
}

float modulationSource(int source, const AudioFeatures& audio, const Modulators& modulators, double beat, int beatsPerBar) {
    if (source <= BandOff || source >= AudioBandCount) {
        return 0.0f;
    }
    if (isAudioSource(source)) {
        return audio.band(source);
    }
    if (source >= BandLfo1 && source <= BandLfo4) {
        const int index = source - BandLfo1;
        return lfoValue(modulators.lfos[static_cast<size_t>(index)], index, beat);
    }
    if (source >= BandMacro1 && source <= BandMacro8) {
        return std::clamp(modulators.macros[static_cast<size_t>(source - BandMacro1)], 0.0f, 1.0f);
    }
    if (!std::isfinite(beat)) {
        return 0.0f;
    }
    const double length = source == BandBar ? static_cast<double>(clampBeatsPerBar(beatsPerBar)) : 1.0;
    const double position = beat / length;
    return static_cast<float>(position - std::floor(position));
}

}

#pragma once

#include "audio/audio_features.hpp"
#include "core/composition.hpp"

namespace nvj {

float lfoCycleBeats(const Lfo& lfo);
float lfoValue(const Lfo& lfo, int index, double beat);
float modulationSource(int source, const AudioFeatures& audio, const Modulators& modulators, double beat, int beatsPerBar);
bool isAudioSource(int source);
int clampBeatsPerBar(int beats);

}

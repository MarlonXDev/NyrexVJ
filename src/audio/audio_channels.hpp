#pragma once

#include <cstddef>
#include <vector>

namespace nvj {

enum class AudioChannel { Mix = 0, Left, Right };

constexpr int kAudioChannelCount = 3;

const char* audioChannelName(AudioChannel channel);
AudioChannel audioChannelFromName(const char* name);

void appendChannel(const float* interleaved, size_t frames, int channels, AudioChannel mode, std::vector<float>& out);

}

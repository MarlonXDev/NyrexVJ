#include "audio/audio_channels.hpp"

#include <cstring>

namespace nvj {

const char* audioChannelName(AudioChannel channel) {
    switch (channel) {
    case AudioChannel::Left:
        return "left";
    case AudioChannel::Right:
        return "right";
    default:
        return "mix";
    }
}

AudioChannel audioChannelFromName(const char* name) {
    if (name != nullptr && std::strcmp(name, "left") == 0) {
        return AudioChannel::Left;
    }
    if (name != nullptr && std::strcmp(name, "right") == 0) {
        return AudioChannel::Right;
    }
    return AudioChannel::Mix;
}

void appendChannel(const float* interleaved, size_t frames, int channels, AudioChannel mode, std::vector<float>& out) {
    const size_t count = channels < 1 ? 1 : static_cast<size_t>(channels);
    out.reserve(out.size() + frames);
    for (size_t f = 0; f < frames; ++f) {
        const float* frame = interleaved + f * count;
        if (count == 1) {
            out.push_back(frame[0]);
        } else if (mode == AudioChannel::Left) {
            out.push_back(frame[0]);
        } else if (mode == AudioChannel::Right) {
            out.push_back(frame[1]);
        } else {
            float sum = 0.0f;
            for (size_t c = 0; c < count; ++c) {
                sum += frame[c];
            }
            out.push_back(sum / static_cast<float>(count));
        }
    }
}

}

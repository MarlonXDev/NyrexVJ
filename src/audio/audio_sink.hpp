#pragma once

#include <cstddef>

namespace nvj {

class AudioSink {
public:
    virtual ~AudioSink() = default;
    virtual void pushSamples(const float* samples, size_t count, int sampleRate) = 0;
};

}

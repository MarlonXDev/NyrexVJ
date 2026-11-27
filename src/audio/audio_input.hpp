#pragma once

#include "audio/audio_channels.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct AudioDevice {
    std::string id;
    std::string name;
    bool loopback = false;
};

class AudioInput {
public:
    virtual ~AudioInput() = default;
    virtual std::vector<AudioDevice> listDevices() = 0;
    virtual bool start(const std::string& id, std::string& error) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual int sampleRate() const = 0;
    virtual void drain(std::vector<float>& mono) = 0;
    virtual bool supportsChannel() const { return false; }
    virtual void setChannel(AudioChannel) {}
};

std::unique_ptr<AudioInput> createAudioInput();

}

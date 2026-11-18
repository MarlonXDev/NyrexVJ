#include "audio/audio_input.hpp"

namespace nvj {

namespace {

class NullInput final : public AudioInput {
public:
    std::vector<AudioDevice> listDevices() override { return {}; }
    bool start(const std::string&, std::string& error) override {
        error = "Audio capture is only available on Windows";
        return false;
    }
    void stop() override {}
    bool running() const override { return false; }
    int sampleRate() const override { return 48000; }
    void drain(std::vector<float>& mono) override { mono.clear(); }
};

}

std::unique_ptr<AudioInput> createAudioInput() {
    return std::make_unique<NullInput>();
}

}

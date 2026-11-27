#pragma once

#include "audio/audio_analyzer.hpp"
#include "audio/audio_input.hpp"
#include "audio/audio_sink.hpp"
#include "audio/synthetic_source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    static const char* demoId();

    void refreshDevices();
    const std::vector<AudioDevice>& devices() const { return devices_; }
    bool select(const std::string& id);
    const std::string& selected() const { return selected_; }
    const std::string& error() const { return error_; }
    bool active() const { return mode_ != Mode::Off; }
    bool lost() const;
    bool deviceActive() const { return mode_ == Mode::Device && input_ != nullptr && input_->running(); }
    int sampleRate() const { return mode_ == Mode::Device && input_ != nullptr ? input_->sampleRate() : 0; }
    void setSink(AudioSink* sink) { sink_ = sink; }
    bool noSignal() const { return mode_ == Mode::Device && silentFor_ > 3.0; }
    void update(double dt, float demoBpm);
    void setGain(float gain);
    void setSmoothing(float smoothing);
    void setBeatSensitivity(float sensitivity);
    void setImprovedTracking(bool improved);
    void setCrossovers(float low, float high);
    void setLatency(double seconds);
    void setChannel(AudioChannel channel);
    AudioChannel channel() const { return channel_; }
    bool channelSupported() const { return input_ != nullptr && input_->supportsChannel(); }
    bool improvedTracking() const { return analyzer_.improved(); }
    const AudioFeatures& features() const { return analyzer_.features(); }

private:
    enum class Mode { Off, Demo, Device };

    std::unique_ptr<AudioInput> input_;
    AudioAnalyzer analyzer_;
    SyntheticSource demo_;
    std::vector<AudioDevice> devices_;
    std::vector<float> scratch_;
    std::string selected_;
    std::string error_;
    Mode mode_ = Mode::Off;
    AudioChannel channel_ = AudioChannel::Mix;
    double demoCarry_ = 0.0;
    double silentFor_ = 0.0;
    AudioSink* sink_ = nullptr;
};

}

#include "audio/audio_system.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

AudioSystem::AudioSystem() : input_(createAudioInput()) {}

AudioSystem::~AudioSystem() {
    if (input_) {
        input_->stop();
    }
}

const char* AudioSystem::demoId() {
    return "demo";
}

void AudioSystem::refreshDevices() {
    devices_ = input_->listDevices();
}

bool AudioSystem::select(const std::string& id) {
    input_->stop();
    error_.clear();
    analyzer_.reset();
    demoCarry_ = 0.0;
    silentFor_ = 0.0;
    if (id.empty()) {
        mode_ = Mode::Off;
        selected_.clear();
        analyzer_.setActive(false);
        return true;
    }
    if (id == demoId()) {
        mode_ = Mode::Demo;
        selected_ = id;
        demo_.reset();
        analyzer_.configure(SyntheticSource::kSampleRate);
        analyzer_.setActive(true);
        return true;
    }
    std::string message;
    if (!input_->start(id, message)) {
        mode_ = Mode::Off;
        selected_.clear();
        error_ = message;
        analyzer_.setActive(false);
        return false;
    }
    mode_ = Mode::Device;
    selected_ = id;
    analyzer_.configure(input_->sampleRate());
    analyzer_.setActive(true);
    return true;
}

bool AudioSystem::lost() const {
    return mode_ == Mode::Device && !input_->running();
}

void AudioSystem::update(double dt, float demoBpm) {
    if (mode_ == Mode::Demo) {
        demo_.setBpm(demoBpm);
        demoCarry_ += dt * static_cast<double>(SyntheticSource::kSampleRate);
        size_t frames = static_cast<size_t>(demoCarry_);
        demoCarry_ -= static_cast<double>(frames);
        frames = std::min<size_t>(frames, 8192);
        scratch_.resize(frames);
        demo_.generate(scratch_.data(), frames);
        analyzer_.push(scratch_.data(), frames);
    } else if (mode_ == Mode::Device) {
        input_->drain(scratch_);
        float peak = 0.0f;
        for (float sample : scratch_) {
            peak = std::max(peak, std::fabs(sample));
        }
        silentFor_ = peak > 0.0005f ? 0.0 : silentFor_ + dt;
        if (!scratch_.empty()) {
            analyzer_.push(scratch_.data(), scratch_.size());
        }
    }
    analyzer_.advance(dt);
}

void AudioSystem::setGain(float gain) {
    analyzer_.setGain(gain);
}

void AudioSystem::setSmoothing(float smoothing) {
    analyzer_.setSmoothing(smoothing);
}

void AudioSystem::setBeatSensitivity(float sensitivity) {
    analyzer_.setBeatSensitivity(sensitivity);
}

}

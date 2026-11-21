#include "control/sync_system.hpp"

#include "control/midi_input.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nvj {

namespace {

constexpr double kCueSaveDelay = 0.5;

std::filesystem::path cuesPath() {
    return std::filesystem::path(userDataDir()) / "cues.json";
}

}

SyncSystem::SyncSystem() = default;

SyncSystem::~SyncSystem() {
    if (cuesDirty_) {
        saveCues();
    }
}

void SyncSystem::refreshLtcDevices() {
    std::unique_ptr<AudioInput> probe = createAudioInput();
    ltcDevices_ = probe ? probe->listDevices() : std::vector<AudioDevice>();
}

void SyncSystem::configure(const SyncConfig& config) {
    if (!configured_ || config.ltcDevice != ltcDeviceApplied_) {
        if (ltcInput_) {
            ltcInput_->stop();
            ltcInput_.reset();
        }
        ltcError_.clear();
        ltc_.reset();
        ltcTracker_.reset();
        if (!config.ltcDevice.empty()) {
            ltcInput_ = createAudioInput();
            if (!ltcInput_ || !ltcInput_->start(config.ltcDevice, ltcError_)) {
                if (ltcError_.empty()) {
                    ltcError_ = "The timecode input could not be started";
                }
                ltcInput_.reset();
            }
        }
        ltcDeviceApplied_ = config.ltcDevice;
    }
    const int wantedPort = config.djLink ? config.djPort : 0;
    if (!configured_ || wantedPort != djPortApplied_) {
        djInput_.stop();
        djError_.clear();
        dj_.reset();
        if (wantedPort != 0) {
            djInput_.start(wantedPort, djError_);
        }
        djPortApplied_ = wantedPort;
    }
    dj_.setPlayer(config.djPlayer);
    config_ = config;
    configured_ = true;
}

void SyncSystem::pushLtcSamples(const float* samples, size_t count, int sampleRate) {
    if (samples == nullptr || count == 0 || sampleRate <= 0) {
        return;
    }
    if (sampleRate != ltc_.sampleRate()) {
        ltc_.setSampleRate(sampleRate);
    }
    ltcFrames_.clear();
    ltc_.push(samples, count, ltcFrames_);
    const double pushed = ltc_.samplesPushed();
    for (const LtcFrame& frame : ltcFrames_) {
        const double age = std::max((pushed - frame.endSample) / static_cast<double>(ltc_.sampleRate()), 0.0);
        const double seconds = static_cast<double>(timecodeToFrames(frame.timecode) + 1) / exactFps(frame.timecode.rate);
        ltcTracker_.report(seconds, age, frame.timecode.rate);
    }
}

void SyncSystem::serviceLtc(double dt) {
    ltcTracker_.advance(dt);
    if (!ltcInput_ || !ltcInput_->running()) {
        return;
    }
    ltcScratch_.clear();
    ltcInput_->drain(ltcScratch_);
    pushLtcSamples(ltcScratch_.data(), ltcScratch_.size(), ltcInput_->sampleRate());
}

void SyncSystem::serviceTimecodeSource() {
    TimecodeSource wanted = TimecodeSource::None;
    if (ltcTracker_.running()) {
        wanted = TimecodeSource::Ltc;
    } else if (mtcTracker_.running()) {
        wanted = TimecodeSource::Mtc;
    }
    const TimecodeTracker* tracker = wanted == TimecodeSource::Ltc ? &ltcTracker_ : wanted == TimecodeSource::Mtc ? &mtcTracker_ : nullptr;
    if (tracker == nullptr) {
        source_ = TimecodeSource::None;
        return;
    }
    source_ = wanted;
    seconds_ = tracker->position();
    timecode_ = timecodeFromSeconds(seconds_, tracker->rate());
}

void SyncSystem::update(double dt, const std::vector<MidiMessage>& midi, BeatClock& clock) {
    update(dt, midi, clock, midiNow());
}

void SyncSystem::update(double dt, const std::vector<MidiMessage>& midi, BeatClock& clock, double now) {
    now_ = now;
    for (const MidiMessage& message : midi) {
        const double stamp = message.time > 0.0 && message.time <= now_ ? message.time : now_;
        switch (message.status) {
        case 0xF8:
            midiClock_.onClock(stamp);
            break;
        case 0xFA:
            midiClock_.onStart(stamp);
            break;
        case 0xFB:
            midiClock_.onContinue(stamp);
            break;
        case 0xFC:
            midiClock_.onStop(stamp);
            break;
        case 0xF2:
            midiClock_.onSongPosition((message.data1 & 0x7F) | ((message.data2 & 0x7F) << 7), stamp);
            break;
        case 0xF1: {
            MtcFix fix;
            if (mtc_.pushQuarterFrame(message.data1 & 0x7F, fix)) {
                mtcTracker_.report(fix.seconds, std::max(now_ - stamp, 0.0), fix.timecode.rate);
            }
            break;
        }
        default:
            break;
        }
    }
    mtcTracker_.advance(dt);
    serviceLtc(dt);
    dj_.advance(dt);
    djInput_.poll(dj_);
    serviceTimecodeSource();

    clock.setMode(config_.mode);
    clock.setLatency(config_.latencyMs / 1000.0);

    SyncObservation observation;
    while (midiClock_.takeObservation(observation, now_, config_.barAlign)) {
        clock.report(observation);
    }
    while (dj_.takeObservation(observation, config_.barAlign)) {
        clock.report(observation);
    }
    TimecodeTracker* tracker = source_ == TimecodeSource::Ltc ? &ltcTracker_ : source_ == TimecodeSource::Mtc ? &mtcTracker_ : nullptr;
    if (tracker != nullptr) {
        SyncObservation timecodeObservation;
        timecodeObservation.source = SyncSource::Timecode;
        timecodeObservation.hasPhase = true;
        timecodeObservation.modulus = 0;
        timecodeObservation.beat = seconds_ * clock.bpm() / 60.0;
        timecodeObservation.jump = tracker->takeJump();
        clock.report(timecodeObservation);
    }

    fired_.clear();
    if (config_.cues) {
        for (size_t index : runner_.advance(cues_, source_ != TimecodeSource::None, seconds_)) {
            fired_.push_back(cues_[index]);
        }
    } else {
        runner_.reset();
    }
    if (cuesDirty_) {
        cuesDirtyFor_ += dt;
        if (cuesDirtyFor_ >= kCueSaveDelay) {
            saveCues();
        }
    }
}

bool SyncSystem::loadCues() {
    std::ifstream file(cuesPath(), std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return cuesFromJson(buffer.str(), cues_);
}

bool SyncSystem::saveCues() {
    cuesDirty_ = false;
    cuesDirtyFor_ = 0.0;
    std::ofstream file(cuesPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    sortCues(cues_);
    file << cuesToJson(cues_);
    return static_cast<bool>(file);
}

}

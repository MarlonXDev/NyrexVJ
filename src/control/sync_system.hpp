#pragma once

#include "audio/audio_input.hpp"
#include "control/djlink_input.hpp"
#include "control/midi_map.hpp"
#include "core/beat_clock.hpp"
#include "core/cue_list.hpp"
#include "core/djlink.hpp"
#include "core/ltc.hpp"
#include "core/midi_clock.hpp"
#include "core/timecode.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct SyncConfig {
    SyncMode mode = SyncMode::Manual;
    double latencyMs = 0.0;
    bool barAlign = true;
    bool djLink = false;
    int djPlayer = 0;
    int djPort = kDjLinkBeatPort;
    std::string ltcDevice;
    bool cues = true;
};

enum class TimecodeSource { None, Ltc, Mtc, Network };

class SyncSystem {
public:
    SyncSystem();
    ~SyncSystem();

    void configure(const SyncConfig& config);
    void update(double dt, const std::vector<MidiMessage>& midi, BeatClock& clock);
    void update(double dt, const std::vector<MidiMessage>& midi, BeatClock& clock, double now);
    void pushLtcSamples(const float* samples, size_t count, int sampleRate);
    void reportNetworkTimecode(double seconds, double age, FrameRate rate);
    void clearNetworkTimecode();

    bool timecodeActive() const { return source_ != TimecodeSource::None; }
    TimecodeSource timecodeSource() const { return source_; }
    double timecodeSeconds() const { return seconds_; }
    Timecode timecode() const { return timecode_; }
    const std::vector<Cue>& firedCues() const { return fired_; }

    const MidiClockTracker& midiClock() const { return midiClock_; }
    const TimecodeTracker& mtcTracker() const { return mtcTracker_; }
    const TimecodeTracker& ltcTracker() const { return ltcTracker_; }
    const TimecodeTracker& networkTracker() const { return networkTracker_; }
    const MtcDecoder& mtc() const { return mtc_; }
    const LtcDecoder& ltc() const { return ltc_; }
    const DjLinkTracker& djLink() const { return dj_; }
    bool djLinkListening() const { return djInput_.running(); }
    const std::string& djLinkError() const { return djError_; }
    bool ltcRunning() const { return ltcInput_ && ltcInput_->running(); }
    const std::string& ltcError() const { return ltcError_; }
    const std::vector<AudioDevice>& ltcDevices() const { return ltcDevices_; }
    void refreshLtcDevices();
    double midiNowSeconds() const { return now_; }

    std::vector<Cue>& cues() { return cues_; }
    const std::vector<Cue>& cues() const { return cues_; }
    const CueRunner& cueRunner() const { return runner_; }
    bool loadCues();
    bool saveCues();
    void markCuesDirty() { cuesDirty_ = true; cuesDirtyFor_ = 0.0; }

private:
    void serviceLtc(double dt);
    void serviceTimecodeSource();

    SyncConfig config_;
    bool configured_ = false;
    MidiClockTracker midiClock_;
    MtcDecoder mtc_;
    TimecodeTracker mtcTracker_;
    TimecodeTracker ltcTracker_;
    TimecodeTracker networkTracker_;
    LtcDecoder ltc_;
    DjLinkTracker dj_;
    DjLinkInput djInput_;
    std::string djError_;
    int djPortApplied_ = 0;
    std::unique_ptr<AudioInput> ltcInput_;
    std::string ltcDeviceApplied_;
    std::string ltcError_;
    std::vector<AudioDevice> ltcDevices_;
    std::vector<float> ltcScratch_;
    std::vector<LtcFrame> ltcFrames_;
    TimecodeSource source_ = TimecodeSource::None;
    double seconds_ = 0.0;
    Timecode timecode_;
    double now_ = 0.0;
    std::vector<Cue> cues_;
    std::vector<Cue> fired_;
    CueRunner runner_;
    bool cuesDirty_ = false;
    double cuesDirtyFor_ = 0.0;
};

}

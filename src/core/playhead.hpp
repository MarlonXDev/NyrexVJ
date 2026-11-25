#pragma once

#include <array>
#include <string>

namespace nvj {

constexpr int kClipValueCount = 16;
constexpr float kClipValueUnset = -3.0e38f;

constexpr std::array<float, kClipValueCount> unsetClipValues() {
    std::array<float, kClipValueCount> values{};
    for (float& value : values) {
        value = kClipValueUnset;
    }
    return values;
}

enum class LoopMode { Loop = 0, PingPong = 1, OnceHold = 2, OnceStop = 3 };

constexpr float kMinPlaySpeed = -4.0f;
constexpr float kMaxPlaySpeed = 4.0f;
constexpr int kLoopModeCount = 4;
constexpr int kSyncBeatChoices = 11;

struct ClipPlayback {
    double in = 0.0;
    double out = 0.0;
    LoopMode loop = LoopMode::Loop;
    float speed = 1.0f;
    float syncBeats = 0.0f;
    std::array<float, kClipValueCount> values = unsetClipValues();

    bool operator==(const ClipPlayback&) const = default;
    bool isDefault() const { return *this == ClipPlayback{}; }
};

const char* loopModeName(LoopMode mode);
LoopMode loopModeFromName(const std::string& name);
float syncBeatsChoice(int index);
int syncBeatsIndex(float beats);
ClipPlayback clampedPlayback(const ClipPlayback& playback);

struct PlayRange {
    double in = 0.0;
    double out = 0.0;
    double length() const { return out - in; }
};

PlayRange resolveRange(const ClipPlayback& playback, double duration, double framePeriod);

class Playhead {
public:
    void configure(const ClipPlayback& playback, double duration, double framePeriod);
    void restart();
    void advance(double seconds, double beats);
    void seek(double position);
    void step(int frames);

    double position() const { return position_; }
    double timeline() const;
    double in() const { return range_.in; }
    double out() const { return range_.out; }
    double length() const { return range_.length(); }
    bool reverse() const;
    bool finished() const { return finished_; }
    bool visible() const;
    bool paused() const;
    const ClipPlayback& playback() const { return playback_; }

private:
    double endPosition() const;
    void move(double delta);

    ClipPlayback playback_;
    PlayRange range_;
    double duration_ = 0.0;
    double framePeriod_ = 1.0 / 30.0;
    double position_ = 0.0;
    int leg_ = 1;
    long long wraps_ = 0;
    bool finished_ = false;
};

}

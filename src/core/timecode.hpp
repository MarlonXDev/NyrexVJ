#pragma once

#include <string>

namespace nvj {

enum class FrameRate { Fps24 = 0, Fps25, Fps2997Drop, Fps30 };

struct Timecode {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
    FrameRate rate = FrameRate::Fps25;
};

int nominalFps(FrameRate rate);
double exactFps(FrameRate rate);
const char* frameRateName(FrameRate rate);
bool timecodeValid(const Timecode& timecode);
long long timecodeToFrames(const Timecode& timecode);
Timecode timecodeFromFrames(long long frames, FrameRate rate);
double timecodeToSeconds(const Timecode& timecode);
Timecode timecodeFromSeconds(double seconds, FrameRate rate);
std::string formatTimecode(const Timecode& timecode);
bool parseTimecode(const std::string& text, FrameRate rate, Timecode& out);

struct MtcFix {
    Timecode timecode;
    double frames = 0.0;
    double seconds = 0.0;
};

class MtcDecoder {
public:
    void reset();
    bool pushQuarterFrame(int data, MtcFix& out);
    int pieces() const { return count_; }
    unsigned long long dropped() const { return dropped_; }

private:
    int nibbles_[8] = {};
    int count_ = 0;
    int next_ = 0;
    unsigned long long dropped_ = 0;
};

class TimecodeTracker {
public:
    void advance(double dt);
    void report(double seconds, double age, FrameRate rate);
    void reset();

    bool running() const;
    bool everSeen() const { return seen_; }
    double position() const;
    FrameRate rate() const { return rate_; }
    bool takeJump();
    double secondsSinceFix() const { return time_ - lastTime_; }
    unsigned long long fixes() const { return fixes_; }
    unsigned long long jumps() const { return jumps_; }

private:
    double time_ = 0.0;
    double lastTime_ = 0.0;
    double lastSeconds_ = 0.0;
    FrameRate rate_ = FrameRate::Fps25;
    bool seen_ = false;
    bool jump_ = false;
    unsigned long long fixes_ = 0;
    unsigned long long jumps_ = 0;
};

}

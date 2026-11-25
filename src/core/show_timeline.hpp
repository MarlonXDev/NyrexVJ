#pragma once

#include "core/cue_list.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace nvj {

constexpr size_t kMaxTimelineLanes = 32;
constexpr size_t kMaxLaneKeys = 2048;
constexpr size_t kMaxTimelineKeys = 16384;
constexpr size_t kMaxTimelineText = 4u * 1024u * 1024u;
constexpr size_t kMaxTargetText = 128;
constexpr double kMaxTimelineSeconds = 86400.0;
constexpr double kMinLoopLength = 0.05;
constexpr double kTimelineMaxStep = kCueMaxStep;
constexpr float kOverrideTolerance = 0.004f;
constexpr float kWriteTolerance = 0.0001f;

enum class KeyShape { Hold = 0, Linear = 1, Smooth = 2 };

struct Keyframe {
    double seconds = 0.0;
    float value = 0.0f;
    KeyShape shape = KeyShape::Linear;
};

struct Lane {
    std::string target;
    bool muted = false;
    std::vector<Keyframe> keys;
};

struct TimelineLoop {
    bool enabled = false;
    double start = 0.0;
    double end = 0.0;
    bool usable() const { return enabled && end - start >= kMinLoopLength; }
};

struct ShowTimeline {
    std::vector<Lane> lanes;
    TimelineLoop loop;
};

struct TimelineDoc {
    ShowTimeline timeline;
    std::vector<Cue> cues;
};

const char* keyShapeName(KeyShape shape);
KeyShape keyShapeFromName(const std::string& name);

float evaluateKeys(const std::vector<Keyframe>& keys, double seconds, bool stepsOnly);
size_t keySegment(const std::vector<Keyframe>& keys, double seconds);

size_t timelineKeyCount(const ShowTimeline& timeline);
int findLane(const ShowTimeline& timeline, const std::string& target);
int addLane(ShowTimeline& timeline, const std::string& target);
bool removeLane(ShowTimeline& timeline, size_t lane);
int insertKey(ShowTimeline& timeline, size_t lane, const Keyframe& key);
int moveKey(ShowTimeline& timeline, size_t lane, size_t key, double seconds, float value);
bool removeKey(ShowTimeline& timeline, size_t lane, size_t key);
bool setKeyShape(ShowTimeline& timeline, size_t lane, size_t key, KeyShape shape);
void setLoop(ShowTimeline& timeline, bool enabled, double start, double end);

bool sameDoc(const TimelineDoc& a, const TimelineDoc& b);
size_t docBytes(const TimelineDoc& doc);

std::string timelineToJson(const ShowTimeline& timeline);
bool timelineFromJson(const std::string& text, ShowTimeline& timeline);

enum class TimelineSourceChoice { Auto = 0, Timecode = 1, Transport = 2 };
enum class LostPolicy { Hold = 0, Continue = 1 };
enum class OverridePolicy { NextSegment = 0, UntilReleased = 1 };

const char* timelineSourceName(TimelineSourceChoice choice);
TimelineSourceChoice timelineSourceFromName(const std::string& name);
const char* lostPolicyName(LostPolicy policy);
LostPolicy lostPolicyFromName(const std::string& name);
const char* overridePolicyName(OverridePolicy policy);
OverridePolicy overridePolicyFromName(const std::string& name);

struct TimelineConfig {
    bool enabled = false;
    TimelineSourceChoice source = TimelineSourceChoice::Auto;
    LostPolicy lost = LostPolicy::Hold;
    OverridePolicy override = OverridePolicy::NextSegment;
    bool cues = true;
};

struct TimelineInputs {
    bool timecodeValid = false;
    double timecodeSeconds = 0.0;
    double clock = 0.0;
    bool running = true;
};

enum class LaneKind { Refused, Continuous, Toggle };

class TimelineHost {
public:
    virtual ~TimelineHost() = default;
    virtual LaneKind laneKind(const std::string& target) const = 0;
    virtual bool read(const std::string& target, float& value) const = 0;
    virtual bool write(const std::string& target, float value) = 0;
    virtual void fire(const Cue& cue) = 0;
};

enum class TimelineState { Off, Waiting, Stopped, Paused, Playing, Following, LostHolding, LostRunning };

enum class LaneStatus { Idle, Following, Overridden, Muted, Unavailable };

class TimelineRunner {
public:
    void configure(const TimelineConfig& config);
    const TimelineConfig& config() const { return config_; }

    void play();
    void pause();
    void toggle();
    void stop();
    void seek(double seconds);

    void update(const TimelineInputs& inputs, const ShowTimeline& timeline, const std::vector<Cue>& cues, TimelineHost& host);

    double position() const { return position_; }
    bool advancing() const { return advancing_; }
    bool engaged() const { return engaged_; }
    TimelineState state() const;
    LaneStatus laneStatus(const std::string& target) const;
    bool overridden(const std::string& target) const;
    void release(const std::string& target);
    void releaseAll();
    size_t overriddenCount() const;

    unsigned long long seeks() const { return seeks_; }
    unsigned long long cuesSkipped() const { return cuesSkipped_; }
    unsigned long long cuesFired() const { return cuesFired_; }
    unsigned long long loopWraps() const { return loopWraps_; }
    unsigned long long clockChanges() const { return clockChanges_; }
    unsigned long long timecodeLosses() const { return timecodeLosses_; }
    unsigned long long overrides() const { return overrides_; }
    unsigned long long writes() const { return writes_; }

private:
    struct LaneRuntime {
        bool hasWritten = false;
        float lastWanted = 0.0f;
        float lastRead = 0.0f;
        bool overridden = false;
        size_t segment = 0;
        LaneStatus status = LaneStatus::Idle;
    };

    void anchorAt(double position, double clock);
    void fireRange(const std::vector<Cue>& cues, double from, double to, TimelineHost& host);
    size_t countCues(const std::vector<Cue>& cues, double from, double to) const;
    void driveLanes(const ShowTimeline& timeline, TimelineHost& host);
    void pruneRuntime(const ShowTimeline& timeline);

    TimelineConfig config_;
    double position_ = 0.0;
    double anchorPosition_ = 0.0;
    double anchorClock_ = 0.0;
    bool havePosition_ = false;
    bool wantPlay_ = false;
    bool advancing_ = false;
    bool engaged_ = false;
    bool following_ = false;
    bool lost_ = false;
    bool armStart_ = true;
    bool seekPending_ = false;
    double seekTo_ = 0.0;
    bool needAnchor_ = false;
    unsigned long long seeks_ = 0;
    unsigned long long cuesSkipped_ = 0;
    unsigned long long cuesFired_ = 0;
    unsigned long long loopWraps_ = 0;
    unsigned long long clockChanges_ = 0;
    unsigned long long timecodeLosses_ = 0;
    unsigned long long overrides_ = 0;
    unsigned long long writes_ = 0;
    std::unordered_map<std::string, LaneRuntime> lanes_;
};

class TimelineHistory {
public:
    static constexpr size_t kLimit = 64;
    static constexpr size_t kBudgetBytes = 8u * 1024u * 1024u;

    void reset();
    bool commit(const TimelineDoc& before, const TimelineDoc& after);
    bool undo(TimelineDoc& current);
    bool redo(TimelineDoc& current);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }
    size_t bytes() const { return bytes_; }

private:
    void trim();

    std::vector<TimelineDoc> undo_;
    std::vector<TimelineDoc> redo_;
    size_t bytes_ = 0;
};

}

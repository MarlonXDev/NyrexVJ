#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nvj::failover {

enum class Level : std::uint8_t { Ok = 0, Warn = 1, Fail = 2 };

struct Thresholds {
    double lostAfter = 5.0;
    double unhealthyAfter = 3.0;
    double readyHold = 10.0;
    double confirmWindow = 30.0;
    double minConfirmDelay = 0.5;
    double maxRoundTrip = 0.05;
    double maxJitter = 0.015;
    double maxStateAge = 1.0;
    double maxSnapshotAge = 150.0;
};

constexpr double kMinLostAfter = 3.0;
constexpr double kMaxLostAfter = 60.0;
double clampLostAfter(double seconds);

struct Facts {
    bool linkLive = false;
    bool compatFull = false;
    bool clockReady = false;
    double roundTrip = 0.0;
    double jitter = 0.0;
    bool hasState = false;
    double stateAge = 1e9;
    int missingClips = 0;
    int otherDifferences = 0;
    int localMissingFiles = 0;
    bool showHashKnown = false;
    bool showHashMatches = false;
    int outputsWanted = 0;
    int outputsOpen = 0;
    int decoderFaults = 0;
    std::uint8_t primaryHealth = 0;
    double sessionSnapshotAge = -1.0;
};

struct Check {
    std::string id;
    Level level = Level::Ok;
    double a = 0.0;
    double b = 0.0;
};

struct Report {
    Level overall = Level::Fail;
    std::uint8_t wire = 3;
    int fails = 0;
    int warns = 0;
    std::vector<Check> checks;
    const Check* find(const std::string& id) const;
};

Report evaluateReadiness(const Facts& facts, const Thresholds& thresholds);
std::uint8_t wireReadiness(Level level);
Level levelFromWire(std::uint8_t wire);

std::uint64_t showFingerprint(const std::vector<std::string>& lines);

enum class State : std::uint8_t {
    Idle,
    Connecting,
    Following,
    PrimaryUnhealthy,
    PrimaryLost,
    PrimaryEnded,
    ConfirmPending,
    Leader
};

enum class Action : std::uint8_t { None, TakeOver };

struct Inputs {
    bool enabled = false;
    bool standby = false;
    bool linkLive = false;
    double primaryAge = 1e9;
    bool byeSeen = false;
    std::uint8_t byeReason = 0;
    std::uint8_t primaryHealth = 0;
    Level readiness = Level::Fail;
    bool localOk = true;
    bool autoArmed = false;
};

struct Decision {
    Action action = Action::None;
    bool automatic = false;
    bool primaryAlive = false;
};

struct Measure {
    bool taken = false;
    bool automatic = false;
    bool forced = false;
    double lastHeardAt = 0.0;
    double detectedAt = -1.0;
    double requestedAt = -1.0;
    double confirmedAt = -1.0;
    double takenAt = -1.0;
    double detectionSeconds = 0.0;
    double decisionSeconds = 0.0;
    double stateAgeAtTakeover = 0.0;
    Level readinessBeforeLoss = Level::Fail;
    double readySecondsBeforeLoss = 0.0;
};

class Machine {
public:
    void configure(const Thresholds& thresholds);
    const Thresholds& thresholds() const { return thresholds_; }
    void update(double now, const Inputs& inputs);
    bool requestTakeover(double now);
    bool confirmTakeover(double now);
    void cancelRequest();
    Decision takeDecision();
    void tookOver(double now);
    void steppedDown(double now);
    void rearm();

    State state() const { return state_; }
    bool requestOpen() const { return confirmPending_; }
    bool requestWarnsAlive() const { return warnAlive_; }
    double requestAge(double now) const;
    bool autoUsed() const { return autoUsed_; }
    bool autoBlockedByEnd() const { return blockedByEnd_; }
    double readySeconds(double now) const;
    Level lastLiveReadiness() const { return lastLive_; }
    const Measure& measure() const { return measure_; }

private:
    void markLoss(double now, const Inputs& inputs);

    Thresholds thresholds_;
    State state_ = State::Idle;
    bool everLive_ = false;
    bool confirmPending_ = false;
    bool warnAlive_ = false;
    double requestedAt_ = 0.0;
    bool confirmed_ = false;
    bool lossMarked_ = false;
    bool autoUsed_ = false;
    bool blockedByEnd_ = false;
    double readySince_ = -1.0;
    Level lastLive_ = Level::Fail;
    double lastLiveAt_ = 0.0;
    double lastNow_ = 0.0;
    Decision pending_;
    bool havePending_ = false;
    Measure measure_;
    double unhealthySince_ = -1.0;
};

const char* stateId(State state);

}

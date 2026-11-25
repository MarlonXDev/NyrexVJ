#include "core/failover.hpp"

#include "core/sha256.hpp"

#include <algorithm>

namespace nvj::failover {

double clampLostAfter(double seconds) {
    if (!(seconds >= kMinLostAfter)) {
        return seconds != seconds ? 5.0 : kMinLostAfter;
    }
    return std::min(seconds, kMaxLostAfter);
}

std::uint8_t wireReadiness(Level level) {
    switch (level) {
    case Level::Ok:
        return 1;
    case Level::Warn:
        return 2;
    default:
        return 3;
    }
}

Level levelFromWire(std::uint8_t wire) {
    switch (wire) {
    case 1:
        return Level::Ok;
    case 2:
        return Level::Warn;
    default:
        return Level::Fail;
    }
}

const Check* Report::find(const std::string& id) const {
    for (const Check& check : checks) {
        if (check.id == id) {
            return &check;
        }
    }
    return nullptr;
}

namespace {

void add(Report& report, const char* id, Level level, double a = 0.0, double b = 0.0) {
    Check check;
    check.id = id;
    check.level = level;
    check.a = a;
    check.b = b;
    report.checks.push_back(check);
    if (level == Level::Fail) {
        ++report.fails;
    } else if (level == Level::Warn) {
        ++report.warns;
    }
}

}

Report evaluateReadiness(const Facts& f, const Thresholds& t) {
    Report report;
    add(report, "link", f.linkLive ? Level::Ok : Level::Fail);
    add(report, "version", f.compatFull ? Level::Ok : Level::Fail);
    if (!f.clockReady) {
        add(report, "clock", Level::Fail);
    } else if (f.roundTrip > t.maxRoundTrip || f.jitter > t.maxJitter) {
        add(report, "clock", Level::Warn, f.roundTrip, f.jitter);
    } else {
        add(report, "clock", Level::Ok, f.roundTrip, f.jitter);
    }
    if (!f.hasState || f.stateAge > t.maxStateAge) {
        add(report, "state", Level::Fail, f.stateAge);
    } else {
        add(report, "state", Level::Ok, f.stateAge);
    }
    if (f.missingClips > 0) {
        add(report, "assets", Level::Fail, f.missingClips, f.otherDifferences);
    } else if (f.otherDifferences > 0) {
        add(report, "assets", Level::Warn, 0, f.otherDifferences);
    } else {
        add(report, "assets", Level::Ok);
    }
    add(report, "files", f.localMissingFiles > 0 ? Level::Fail : Level::Ok, f.localMissingFiles);
    if (!f.showHashKnown) {
        add(report, "show", Level::Warn);
    } else {
        add(report, "show", f.showHashMatches ? Level::Ok : Level::Fail);
    }
    if (f.outputsWanted <= 0) {
        add(report, "outputs", Level::Warn, f.outputsOpen, f.outputsWanted);
    } else if (f.outputsOpen <= 0) {
        add(report, "outputs", Level::Fail, f.outputsOpen, f.outputsWanted);
    } else if (f.outputsOpen < f.outputsWanted) {
        add(report, "outputs", Level::Warn, f.outputsOpen, f.outputsWanted);
    } else {
        add(report, "outputs", Level::Ok, f.outputsOpen, f.outputsWanted);
    }
    add(report, "decoders", f.decoderFaults > 0 ? Level::Fail : Level::Ok, f.decoderFaults);
    add(report, "primary", f.primaryHealth != 0 ? Level::Warn : Level::Ok, f.primaryHealth);
    if (f.sessionSnapshotAge < 0.0 || f.sessionSnapshotAge > t.maxSnapshotAge) {
        add(report, "snapshot", Level::Warn, f.sessionSnapshotAge);
    } else {
        add(report, "snapshot", Level::Ok, f.sessionSnapshotAge);
    }
    report.overall = report.fails > 0 ? Level::Fail : report.warns > 0 ? Level::Warn : Level::Ok;
    report.wire = wireReadiness(report.overall);
    return report;
}

std::uint64_t showFingerprint(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line;
        text.push_back('\n');
    }
    const std::string hex = sha256Hex(text);
    std::uint64_t value = 0;
    for (size_t i = 0; i < 16 && i < hex.size(); ++i) {
        const char c = hex[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
        value = (value << 4) | static_cast<std::uint64_t>(digit);
    }
    return value == 0 ? 1 : value;
}

void Machine::configure(const Thresholds& thresholds) {
    thresholds_ = thresholds;
    thresholds_.lostAfter = clampLostAfter(thresholds_.lostAfter);
}

double Machine::requestAge(double now) const {
    return confirmPending_ ? now - requestedAt_ : 0.0;
}

double Machine::readySeconds(double now) const {
    return readySince_ < 0.0 ? 0.0 : std::max(now - readySince_, 0.0);
}

void Machine::markLoss(double now, const Inputs& inputs) {
    if (lossMarked_) {
        return;
    }
    lossMarked_ = true;
    measure_.detectedAt = now;
    measure_.lastHeardAt = now - inputs.primaryAge;
    measure_.detectionSeconds = inputs.primaryAge;
    measure_.readinessBeforeLoss = lastLive_;
    measure_.readySecondsBeforeLoss = readySince_ < 0.0 ? 0.0 : std::max(lastLiveAt_ - readySince_, 0.0);
}

void Machine::update(double now, const Inputs& in) {
    lastNow_ = now;
    if (!in.enabled || !in.standby) {
        state_ = state_ == State::Leader ? State::Leader : State::Idle;
        confirmPending_ = false;
        return;
    }
    if (state_ == State::Leader) {
        return;
    }
    if (in.linkLive) {
        everLive_ = true;
        if (in.primaryAge <= thresholds_.maxStateAge) {
            lastLive_ = in.readiness;
            lastLiveAt_ = now;
            if (in.readiness == Level::Fail) {
                readySince_ = -1.0;
            } else if (readySince_ < 0.0) {
                readySince_ = now;
            }
            if (lossMarked_) {
                lossMarked_ = false;
                measure_ = Measure();
            }
            blockedByEnd_ = false;
        }
    }
    if (confirmPending_ && now - requestedAt_ > thresholds_.confirmWindow) {
        confirmPending_ = false;
        confirmed_ = false;
    }
    const bool primaryHeard = in.linkLive && in.primaryAge < thresholds_.lostAfter;
    if (in.byeSeen && !in.linkLive) {
        blockedByEnd_ = in.byeReason == 0;
    }
    State next;
    if (!everLive_) {
        next = State::Connecting;
    } else if (in.byeSeen && !in.linkLive) {
        next = State::PrimaryEnded;
        markLoss(now, in);
    } else if (!primaryHeard && in.primaryAge >= thresholds_.lostAfter) {
        next = State::PrimaryLost;
        markLoss(now, in);
    } else if (in.primaryHealth != 0) {
        if (unhealthySince_ < 0.0) {
            unhealthySince_ = now;
        }
        next = now - unhealthySince_ >= thresholds_.unhealthyAfter ? State::PrimaryUnhealthy : State::Following;
    } else {
        next = State::Following;
    }
    if (in.primaryHealth == 0) {
        unhealthySince_ = -1.0;
    }
    if (confirmPending_) {
        next = State::ConfirmPending;
    }
    state_ = next;
    if (confirmed_ && confirmPending_) {
        confirmed_ = false;
        confirmPending_ = false;
        pending_.action = Action::TakeOver;
        pending_.automatic = false;
        pending_.primaryAlive = warnAlive_;
        havePending_ = true;
        measure_.automatic = false;
        measure_.forced = warnAlive_;
        measure_.confirmedAt = now;
        measure_.stateAgeAtTakeover = in.primaryAge;
        state_ = State::Following;
        return;
    }
    if (state_ == State::PrimaryLost && in.autoArmed && !autoUsed_ && !blockedByEnd_ && in.localOk && lastLive_ != Level::Fail && !havePending_) {
        const double readyFor = readySince_ < 0.0 ? 0.0 : lastLiveAt_ - readySince_;
        if (readyFor >= thresholds_.readyHold) {
            autoUsed_ = true;
            pending_.action = Action::TakeOver;
            pending_.automatic = true;
            pending_.primaryAlive = false;
            havePending_ = true;
            measure_.automatic = true;
            measure_.forced = false;
            measure_.confirmedAt = now;
            measure_.stateAgeAtTakeover = in.primaryAge;
            measure_.decisionSeconds = now - measure_.detectedAt;
        }
    }
}

bool Machine::requestTakeover(double now) {
    if (state_ == State::Idle || state_ == State::Leader) {
        return false;
    }
    confirmPending_ = true;
    confirmed_ = false;
    requestedAt_ = now;
    warnAlive_ = state_ == State::Following || state_ == State::PrimaryUnhealthy;
    measure_.requestedAt = now;
    state_ = State::ConfirmPending;
    return true;
}

bool Machine::confirmTakeover(double now) {
    if (!confirmPending_ || now - requestedAt_ < thresholds_.minConfirmDelay || now - requestedAt_ > thresholds_.confirmWindow) {
        return false;
    }
    confirmed_ = true;
    return true;
}

void Machine::cancelRequest() {
    confirmPending_ = false;
    confirmed_ = false;
}

Decision Machine::takeDecision() {
    Decision out;
    if (havePending_) {
        out = pending_;
        havePending_ = false;
        pending_ = Decision();
    }
    return out;
}

void Machine::tookOver(double now) {
    state_ = State::Leader;
    measure_.taken = true;
    measure_.takenAt = now;
    confirmPending_ = false;
    confirmed_ = false;
}

void Machine::steppedDown(double now) {
    (void)now;
    state_ = State::Connecting;
    everLive_ = false;
    lossMarked_ = false;
    readySince_ = -1.0;
    lastLive_ = Level::Fail;
    unhealthySince_ = -1.0;
    confirmPending_ = false;
    confirmed_ = false;
    havePending_ = false;
    blockedByEnd_ = false;
}

void Machine::rearm() {
    autoUsed_ = false;
}

const char* stateId(State state) {
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Connecting:
        return "connecting";
    case State::Following:
        return "following";
    case State::PrimaryUnhealthy:
        return "primary-unhealthy";
    case State::PrimaryLost:
        return "primary-lost";
    case State::PrimaryEnded:
        return "primary-ended";
    case State::ConfirmPending:
        return "confirm-pending";
    case State::Leader:
        return "leader";
    }
    return "idle";
}

}

#include "check.hpp"
#include "core/failover.hpp"

#include <cmath>

using namespace nvj::failover;

namespace {

Facts goodFacts() {
    Facts f;
    f.linkLive = true;
    f.compatFull = true;
    f.clockReady = true;
    f.roundTrip = 0.002;
    f.jitter = 0.001;
    f.hasState = true;
    f.stateAge = 0.05;
    f.showHashKnown = true;
    f.showHashMatches = true;
    f.outputsWanted = 2;
    f.outputsOpen = 2;
    f.sessionSnapshotAge = 20.0;
    return f;
}

Level levelOf(const Report& report, const char* id) {
    const Check* check = report.find(id);
    CHECK(check != nullptr);
    return check ? check->level : Level::Fail;
}

void testReadiness() {
    const Thresholds t;
    Report ok = evaluateReadiness(goodFacts(), t);
    CHECK(ok.overall == Level::Ok);
    CHECK(ok.wire == 1);
    CHECK(ok.fails == 0 && ok.warns == 0);
    CHECK(ok.checks.size() == 11);

    Facts f = goodFacts();
    f.linkLive = false;
    Report r = evaluateReadiness(f, t);
    CHECK(r.overall == Level::Fail && r.wire == 3);
    CHECK(levelOf(r, "link") == Level::Fail);

    f = goodFacts();
    f.compatFull = false;
    CHECK(levelOf(evaluateReadiness(f, t), "version") == Level::Fail);

    f = goodFacts();
    f.clockReady = false;
    CHECK(levelOf(evaluateReadiness(f, t), "clock") == Level::Fail);
    f = goodFacts();
    f.roundTrip = 0.2;
    r = evaluateReadiness(f, t);
    CHECK(levelOf(r, "clock") == Level::Warn);
    CHECK(r.overall == Level::Warn && r.wire == 2);
    f = goodFacts();
    f.jitter = 0.05;
    CHECK(levelOf(evaluateReadiness(f, t), "clock") == Level::Warn);

    f = goodFacts();
    f.hasState = false;
    CHECK(levelOf(evaluateReadiness(f, t), "state") == Level::Fail);
    f = goodFacts();
    f.stateAge = 2.5;
    CHECK(levelOf(evaluateReadiness(f, t), "state") == Level::Fail);

    f = goodFacts();
    f.missingClips = 3;
    r = evaluateReadiness(f, t);
    CHECK(levelOf(r, "assets") == Level::Fail);
    CHECK(r.find("assets")->a == 3.0);
    f = goodFacts();
    f.otherDifferences = 2;
    CHECK(levelOf(evaluateReadiness(f, t), "assets") == Level::Warn);

    f = goodFacts();
    f.localMissingFiles = 1;
    CHECK(levelOf(evaluateReadiness(f, t), "files") == Level::Fail);

    f = goodFacts();
    f.showHashKnown = false;
    CHECK(levelOf(evaluateReadiness(f, t), "show") == Level::Warn);
    f = goodFacts();
    f.showHashMatches = false;
    CHECK(levelOf(evaluateReadiness(f, t), "show") == Level::Fail);

    f = goodFacts();
    f.outputsWanted = 0;
    f.outputsOpen = 0;
    CHECK(levelOf(evaluateReadiness(f, t), "outputs") == Level::Warn);
    f = goodFacts();
    f.outputsOpen = 0;
    CHECK(levelOf(evaluateReadiness(f, t), "outputs") == Level::Fail);
    f = goodFacts();
    f.outputsOpen = 1;
    CHECK(levelOf(evaluateReadiness(f, t), "outputs") == Level::Warn);

    f = goodFacts();
    f.decoderFaults = 2;
    CHECK(levelOf(evaluateReadiness(f, t), "decoders") == Level::Fail);

    f = goodFacts();
    f.primaryHealth = 1;
    CHECK(levelOf(evaluateReadiness(f, t), "primary") == Level::Warn);

    f = goodFacts();
    f.sessionSnapshotAge = -1.0;
    CHECK(levelOf(evaluateReadiness(f, t), "snapshot") == Level::Warn);
    f.sessionSnapshotAge = 400.0;
    CHECK(levelOf(evaluateReadiness(f, t), "snapshot") == Level::Warn);

    f = goodFacts();
    f.missingClips = 1;
    f.roundTrip = 1.0;
    r = evaluateReadiness(f, t);
    CHECK(r.fails == 1 && r.warns == 1 && r.overall == Level::Fail);

    CHECK(levelFromWire(1) == Level::Ok);
    CHECK(levelFromWire(2) == Level::Warn);
    CHECK(levelFromWire(3) == Level::Fail);
    CHECK(levelFromWire(0) == Level::Fail);
    CHECK(wireReadiness(Level::Ok) == 1);
}

void testFingerprint() {
    const std::vector<std::string> a = {"deck 1", "clip a", "clip b"};
    const std::vector<std::string> b = {"deck 1", "clip b", "clip a"};
    CHECK(showFingerprint(a) == showFingerprint(a));
    CHECK(showFingerprint(a) != showFingerprint(b));
    CHECK(showFingerprint(a) != 0);
    CHECK(showFingerprint({}) != 0);
    CHECK(showFingerprint({"ab", "c"}) != showFingerprint({"a", "bc"}));
}

void testClamp() {
    CHECK(clampLostAfter(5.0) == 5.0);
    CHECK(clampLostAfter(0.5) == kMinLostAfter);
    CHECK(clampLostAfter(900.0) == kMaxLostAfter);
    CHECK(clampLostAfter(-1.0) == kMinLostAfter);
    CHECK(clampLostAfter(std::nan("")) == 5.0);
}

struct Run {
    Machine machine;
    Inputs in;
    double now = 100.0;
    double lastHeard = 100.0;
    bool alive = true;

    Run() {
        Thresholds t;
        t.lostAfter = 5.0;
        t.readyHold = 10.0;
        machine.configure(t);
        in.enabled = true;
        in.standby = true;
        in.readiness = Level::Ok;
        in.linkLive = true;
        in.primaryAge = 0.0;
    }

    void step(double dt = 0.1) {
        now += dt;
        if (alive) {
            lastHeard = now;
        }
        in.primaryAge = now - lastHeard;
        in.linkLive = (alive || in.primaryAge < 3.0) && !in.byeSeen;
        machine.update(now, in);
    }

    void run(double seconds) {
        const int steps = static_cast<int>(seconds / 0.1 + 0.5);
        for (int i = 0; i < steps; ++i) {
            step();
        }
    }
};

void testConnecting() {
    Run r;
    r.in.linkLive = false;
    r.machine.update(r.now, r.in);
    CHECK(r.machine.state() == State::Connecting);
    r.in.linkLive = true;
    r.machine.update(r.now + 0.1, r.in);
    CHECK(r.machine.state() == State::Following);
    Machine idle;
    Inputs off;
    idle.update(1.0, off);
    CHECK(idle.state() == State::Idle);
    off.enabled = true;
    idle.update(2.0, off);
    CHECK(idle.state() == State::Idle);
}

void testManualTakeoverAfterLoss() {
    Run r;
    r.run(12.0);
    CHECK(r.machine.state() == State::Following);
    r.alive = false;
    r.run(2.0);
    CHECK(r.machine.state() == State::Following);
    r.run(4.0);
    CHECK(r.machine.state() == State::PrimaryLost);
    CHECK(r.machine.measure().detectionSeconds >= 5.0);
    CHECK(r.machine.measure().detectionSeconds < 5.3);
    CHECK(r.machine.takeDecision().action == Action::None);
    CHECK(r.machine.requestTakeover(r.now));
    CHECK(r.machine.state() == State::ConfirmPending);
    CHECK(!r.machine.requestWarnsAlive());
    CHECK(!r.machine.confirmTakeover(r.now + 0.1));
    r.run(1.0);
    CHECK(r.machine.requestOpen());
    CHECK(r.machine.confirmTakeover(r.now));
    r.step();
    const Decision d = r.machine.takeDecision();
    CHECK(d.action == Action::TakeOver);
    CHECK(!d.automatic);
    CHECK(!d.primaryAlive);
    CHECK(r.machine.takeDecision().action == Action::None);
    r.machine.tookOver(r.now);
    CHECK(r.machine.state() == State::Leader);
    CHECK(r.machine.measure().taken);
    CHECK(!r.machine.measure().automatic);
    CHECK(r.machine.measure().stateAgeAtTakeover > 5.0);
    r.step();
    CHECK(r.machine.state() == State::Leader);
    r.machine.steppedDown(r.now);
    CHECK(r.machine.state() == State::Connecting);
}

void testManualWhileAlive() {
    Run r;
    r.run(2.0);
    CHECK(r.machine.requestTakeover(r.now));
    CHECK(r.machine.requestWarnsAlive());
    r.run(1.0);
    CHECK(r.machine.confirmTakeover(r.now));
    r.step();
    const Decision d = r.machine.takeDecision();
    CHECK(d.action == Action::TakeOver);
    CHECK(d.primaryAlive);
    CHECK(r.machine.measure().forced);
    CHECK(r.machine.measure().requestedAt > 0.0);
}

void testConfirmExpires() {
    Run r;
    r.run(2.0);
    CHECK(r.machine.requestTakeover(r.now));
    r.run(31.0);
    CHECK(!r.machine.requestOpen());
    CHECK(!r.machine.confirmTakeover(r.now));
    CHECK(r.machine.state() == State::Following);
    CHECK(r.machine.takeDecision().action == Action::None);
    CHECK(r.machine.requestTakeover(r.now));
    r.machine.cancelRequest();
    CHECK(!r.machine.confirmTakeover(r.now + 1.0));
    Machine idle;
    CHECK(!idle.requestTakeover(1.0));
}

void testAutomatic() {
    Run r;
    r.in.autoArmed = true;
    r.run(12.0);
    r.alive = false;
    r.run(4.0);
    CHECK(r.machine.takeDecision().action == Action::None);
    r.run(1.5);
    const Decision d = r.machine.takeDecision();
    CHECK(d.action == Action::TakeOver);
    CHECK(d.automatic);
    CHECK(r.machine.autoUsed());
    CHECK(r.machine.measure().automatic);
    CHECK(r.machine.measure().readySecondsBeforeLoss >= 10.0);
    CHECK(r.machine.measure().decisionSeconds >= 0.0);
    CHECK(r.machine.measure().decisionSeconds < 0.5);
    r.run(3.0);
    CHECK(r.machine.takeDecision().action == Action::None);
    r.machine.steppedDown(r.now);
    r.machine.rearm();
    CHECK(!r.machine.autoUsed());
}

void testAutomaticNotWhenNotReadyLongEnough() {
    Run r;
    r.in.autoArmed = true;
    r.run(4.0);
    r.alive = false;
    r.run(12.0);
    CHECK(r.machine.state() == State::PrimaryLost);
    CHECK(r.machine.takeDecision().action == Action::None);
}

void testAutomaticNotWhenFailing() {
    Run r;
    r.in.autoArmed = true;
    r.in.readiness = Level::Fail;
    r.run(30.0);
    r.alive = false;
    r.run(12.0);
    CHECK(r.machine.state() == State::PrimaryLost);
    CHECK(r.machine.takeDecision().action == Action::None);
    Run w;
    w.in.autoArmed = true;
    w.in.readiness = Level::Warn;
    w.run(15.0);
    w.alive = false;
    w.run(7.0);
    CHECK(w.machine.takeDecision().action == Action::TakeOver);
}

void testAutomaticNotWhenLocalFault() {
    Run r;
    r.in.autoArmed = true;
    r.in.localOk = false;
    r.run(15.0);
    r.alive = false;
    r.run(8.0);
    CHECK(r.machine.takeDecision().action == Action::None);
    r.in.localOk = true;
    r.run(0.2);
    CHECK(r.machine.takeDecision().action == Action::TakeOver);
}

void testAutomaticNotWhenNotArmed() {
    Run r;
    r.run(15.0);
    r.alive = false;
    r.run(10.0);
    CHECK(r.machine.state() == State::PrimaryLost);
    CHECK(r.machine.takeDecision().action == Action::None);
}

void testNoAutomaticAfterEnd() {
    Run r;
    r.in.autoArmed = true;
    r.run(15.0);
    r.alive = false;
    r.in.byeSeen = true;
    r.in.byeReason = 0;
    r.run(1.0);
    CHECK(r.machine.state() == State::PrimaryEnded);
    r.run(15.0);
    CHECK(r.machine.autoBlockedByEnd());
    CHECK(r.machine.takeDecision().action == Action::None);
    CHECK(r.machine.requestTakeover(r.now));
    r.run(1.0);
    CHECK(r.machine.confirmTakeover(r.now));
    r.step();
    CHECK(r.machine.takeDecision().action == Action::TakeOver);
}

void testShortOutage() {
    Run r;
    r.in.autoArmed = true;
    r.run(15.0);
    r.alive = false;
    r.run(2.0);
    r.alive = true;
    r.run(10.0);
    CHECK(r.machine.state() == State::Following);
    CHECK(r.machine.takeDecision().action == Action::None);
    CHECK(!r.machine.autoUsed());
    r.alive = false;
    r.run(8.0);
    const Decision second = r.machine.takeDecision();
    CHECK(second.action == Action::TakeOver);
    CHECK(second.automatic);
    CHECK(r.machine.measure().detectionSeconds >= 5.0);
}

void testUnhealthy() {
    Run r;
    r.run(2.0);
    r.in.primaryHealth = 1;
    r.run(2.0);
    CHECK(r.machine.state() == State::Following);
    r.run(2.0);
    CHECK(r.machine.state() == State::PrimaryUnhealthy);
    CHECK(r.machine.takeDecision().action == Action::None);
    r.in.primaryHealth = 0;
    r.run(0.2);
    CHECK(r.machine.state() == State::Following);
    r.in.primaryHealth = 3;
    r.run(1.0);
    r.in.primaryHealth = 0;
    r.run(0.5);
    r.in.primaryHealth = 3;
    r.run(2.5);
    CHECK(r.machine.state() == State::Following);
}

void testColdTakeover() {
    Run r;
    r.in.linkLive = false;
    r.in.primaryAge = 1e9;
    r.machine.update(r.now, r.in);
    CHECK(r.machine.state() == State::Connecting);
    CHECK(r.machine.requestTakeover(r.now));
    CHECK(!r.machine.requestWarnsAlive());
    r.now += 1.0;
    r.machine.update(r.now, r.in);
    CHECK(r.machine.confirmTakeover(r.now));
    r.now += 0.1;
    r.machine.update(r.now, r.in);
    CHECK(r.machine.takeDecision().action == Action::TakeOver);
    Run a;
    a.in.autoArmed = true;
    a.in.linkLive = false;
    a.in.primaryAge = 1e9;
    for (int i = 0; i < 200; ++i) {
        a.now += 0.1;
        a.machine.update(a.now, a.in);
    }
    CHECK(a.machine.takeDecision().action == Action::None);
}

}

int main() {
    testReadiness();
    testFingerprint();
    testClamp();
    testConnecting();
    testManualTakeoverAfterLoss();
    testManualWhileAlive();
    testConfirmExpires();
    testAutomatic();
    testAutomaticNotWhenNotReadyLongEnough();
    testAutomaticNotWhenFailing();
    testAutomaticNotWhenLocalFault();
    testAutomaticNotWhenNotArmed();
    testNoAutomaticAfterEnd();
    testShortOutage();
    testUnhealthy();
    testColdTakeover();
    return testing::finish();
}

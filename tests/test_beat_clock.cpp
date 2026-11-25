#include "check.hpp"

#include "core/beat_clock.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

using nvj::BeatClock;
using nvj::LockState;
using nvj::SyncMode;
using nvj::SyncObservation;
using nvj::SyncSource;

constexpr double kFrame = 1.0 / 60.0;

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

double fraction(double beat) {
    return beat - std::floor(beat);
}

double wrapHalf(double value) {
    double r = value - std::floor(value + 0.5);
    return r;
}

struct BeatSource {
    SyncSource source = SyncSource::MidiClock;
    double bpm = 120.0;
    double startOffset = 0.0;
    int modulus = 1;
    bool sendTempo = true;
    double nextBoundary = 0.0;
    double age = 0.0;
    bool silent = false;
};

struct Run {
    double maxStep = 0.0;
    double minStep = 1.0e9;
    double backward = 0.0;
    double lastBeat = 0.0;
};

void drive(BeatClock& clock, BeatSource& source, double seconds, Run* run = nullptr) {
    const int frames = static_cast<int>(std::round(seconds / kFrame));
    if (run) {
        run->lastBeat = clock.beat();
    }
    for (int i = 0; i < frames; ++i) {
        const double before = clock.beat();
        clock.advance(kFrame, true);
        const double now = clock.time();
        while (!source.silent && source.nextBoundary <= now) {
            SyncObservation o;
            o.source = source.source;
            o.hasPhase = true;
            o.beat = 0.0;
            o.modulus = source.modulus;
            o.hasTempo = source.sendTempo;
            o.bpm = source.bpm;
            o.age = now - source.nextBoundary;
            clock.report(o);
            source.nextBoundary += 60.0 / source.bpm;
        }
        if (source.silent) {
            source.nextBoundary = std::max(source.nextBoundary, now);
        }
        if (run) {
            const double step = clock.beat() - before;
            run->maxStep = std::max(run->maxStep, step);
            run->minStep = std::min(run->minStep, step);
            if (step < 0.0) {
                run->backward = std::min(run->backward, step);
            }
        }
    }
}

void testFreeRun() {
    BeatClock clock;
    clock.setTempo(120.0);
    for (int i = 0; i < 60; ++i) {
        clock.advance(kFrame, true);
    }
    CHECK(near(clock.beat(), 2.0, 1.0e-9));
    CHECK(clock.state() == LockState::Free);
    CHECK(clock.active() == SyncSource::Manual);
    for (int i = 0; i < 60; ++i) {
        clock.advance(kFrame, false);
    }
    CHECK(near(clock.beat(), 2.0, 1.0e-9));
    CHECK(near(clock.time(), 2.0, 1.0e-9));
    clock.setTempo(500.0);
    CHECK(near(clock.bpm(), nvj::kMaxBpm, 1.0e-12));
    clock.setTempo(1.0);
    CHECK(near(clock.bpm(), nvj::kMinBpm, 1.0e-12));
    clock.setTempo(std::nan(""));
    CHECK(near(clock.bpm(), nvj::kMinBpm, 1.0e-12));
    clock.resync();
    CHECK(clock.beat() == 0.0);
    clock.shift(2.5);
    CHECK(near(clock.beat(), 2.5, 1.0e-12));
}

void testTap() {
    BeatClock clock;
    clock.setTempo(100.0);
    for (int i = 0; i < 17; ++i) {
        clock.advance(kFrame, true);
    }
    clock.tap();
    CHECK(near(clock.bpm(), 100.0, 1.0e-12));
    CHECK(near(clock.beat(), std::floor(clock.beat()), 1.0e-12));
    for (int tap = 0; tap < 4; ++tap) {
        for (int i = 0; i < 30; ++i) {
            clock.advance(kFrame, true);
        }
        clock.tap();
    }
    CHECK(near(clock.bpm(), 120.0, 0.01));
    CHECK(near(fraction(clock.beat()), 0.0, 1.0e-9));
    for (int i = 0; i < 150; ++i) {
        clock.advance(kFrame, true);
    }
    clock.tap();
    CHECK(near(clock.bpm(), 120.0, 0.01));
    for (int i = 0; i < 60; ++i) {
        clock.advance(kFrame, true);
    }
    clock.tap();
    CHECK(near(clock.bpm(), 60.0, 0.01));
    BeatClock fast;
    fast.tap();
    for (int i = 0; i < 3; ++i) {
        fast.advance(kFrame, true);
    }
    fast.tap();
    CHECK(near(fast.bpm(), nvj::kMaxBpm, 1.0e-9));
}

void testNudge() {
    BeatClock clock;
    clock.shift(3.2);
    clock.nudge(1.0);
    CHECK(near(clock.beat(), 3.0, 1.0e-12));
    clock.shift(0.7);
    clock.nudge(0.5);
    CHECK(near(clock.beat(), 3.85, 1.0e-12));
    clock.nudge(-4.0);
    CHECK(near(clock.beat(), 3.85, 1.0e-12));
}

void testPhaseContinuityAndLock() {
    BeatClock clock;
    clock.setTempo(120.0);
    clock.shift(0.3);
    BeatSource source;
    source.source = SyncSource::MidiClock;
    source.bpm = 120.0;
    source.nextBoundary = 0.0;
    Run run;
    drive(clock, source, 12.0, &run);
    CHECK(clock.active() == SyncSource::MidiClock);
    CHECK(clock.state() == LockState::Locked);
    CHECK(clock.locked());
    CHECK(std::fabs(wrapHalf(clock.beat())) < 0.06);
    CHECK(run.backward == 0.0);
    CHECK(run.minStep > 0.0);
    const double nominal = 120.0 / 60.0 * kFrame;
    CHECK(run.maxStep <= nominal * 1.2501);
    CHECK(run.minStep >= nominal * 0.7499);
    CHECK(clock.lockQuality() > 0.7);
    CHECK(clock.jumps() == 0);
}

void testSlewNeverJumpsBackwards() {
    BeatClock clock;
    clock.setTempo(120.0);
    clock.shift(0.45);
    BeatSource source;
    source.source = SyncSource::Audio;
    source.modulus = 1;
    source.bpm = 120.0;
    source.sendTempo = false;
    Run run;
    drive(clock, source, 12.0, &run);
    CHECK(run.backward == 0.0);
    CHECK(std::fabs(wrapHalf(clock.beat())) < 0.08);
}

void testTempoFollow() {
    BeatClock clock;
    clock.setTempo(100.0);
    BeatSource source;
    source.source = SyncSource::MidiClock;
    source.bpm = 140.0;
    drive(clock, source, 20.0);
    CHECK(near(clock.bpm(), 140.0, 0.5));
    BeatClock exact;
    exact.setTempo(100.0);
    BeatSource link;
    link.source = SyncSource::DjLink;
    link.bpm = 128.0;
    drive(exact, link, 10.0);
    CHECK(near(exact.bpm(), 128.0, 0.01));
    BeatClock clamped;
    BeatSource wild;
    wild.source = SyncSource::DjLink;
    wild.bpm = 400.0;
    drive(clamped, wild, 12.0);
    CHECK(clamped.bpm() <= nvj::kMaxBpm + 1.0e-9);
    CHECK(clamped.bpm() >= nvj::kMinBpm);
}

void testPriorityAuto() {
    BeatClock clock;
    clock.setTempo(120.0);
    BeatSource audio;
    audio.source = SyncSource::Audio;
    audio.bpm = 120.0;
    audio.sendTempo = false;
    BeatSource midi;
    midi.source = SyncSource::MidiClock;
    midi.bpm = 120.0;
    BeatSource link;
    link.source = SyncSource::DjLink;
    link.bpm = 120.0;
    auto driveAll = [&](double seconds) {
        const int frames = static_cast<int>(std::round(seconds / kFrame));
        for (int i = 0; i < frames; ++i) {
            clock.advance(kFrame, true);
            const double now = clock.time();
            for (BeatSource* s : {&audio, &midi, &link}) {
                while (!s->silent && s->nextBoundary <= now) {
                    SyncObservation o;
                    o.source = s->source;
                    o.hasPhase = true;
                    o.beat = 0.0;
                    o.modulus = 1;
                    o.hasTempo = s->sendTempo;
                    o.bpm = s->bpm;
                    o.age = now - s->nextBoundary;
                    clock.report(o);
                    s->nextBoundary += 60.0 / s->bpm;
                }
                if (s->silent) {
                    s->nextBoundary = std::max(s->nextBoundary, now);
                }
            }
        }
    };
    driveAll(2.0);
    CHECK(clock.active() == SyncSource::DjLink);
    CHECK(clock.present(SyncSource::Audio));
    CHECK(clock.present(SyncSource::MidiClock));
    CHECK(!clock.present(SyncSource::Timecode));
    link.silent = true;
    driveAll(0.5);
    CHECK(clock.active() == SyncSource::DjLink);
    driveAll(2.0);
    CHECK(clock.active() == SyncSource::MidiClock);
    midi.silent = true;
    driveAll(1.5);
    CHECK(clock.active() == SyncSource::Audio);
    audio.silent = true;
    driveAll(3.5);
    CHECK(clock.active() == SyncSource::Manual);
    CHECK(clock.state() == LockState::Free);
    link.silent = false;
    driveAll(1.0);
    CHECK(clock.active() == SyncSource::DjLink);
}

void testForcedMode() {
    BeatClock clock;
    clock.setTempo(120.0);
    clock.setMode(SyncMode::Audio);
    CHECK(clock.state() == LockState::Searching);
    BeatSource link;
    link.source = SyncSource::DjLink;
    link.bpm = 150.0;
    drive(clock, link, 3.0);
    CHECK(clock.active() == SyncSource::Manual);
    CHECK(near(clock.bpm(), 120.0, 1.0e-12));
    CHECK(clock.state() == LockState::Searching);
    BeatSource audio;
    audio.source = SyncSource::Audio;
    audio.bpm = 120.0;
    audio.sendTempo = false;
    drive(clock, audio, 6.0);
    CHECK(clock.active() == SyncSource::Audio);
    CHECK(clock.locked());
    audio.silent = true;
    drive(clock, audio, 4.0);
    CHECK(clock.state() == LockState::Lost);
    CHECK(clock.active() == SyncSource::Manual);
    clock.setMode(SyncMode::Manual);
    CHECK(clock.state() == LockState::Free);
    BeatClock manual;
    manual.setMode(SyncMode::Manual);
    BeatSource midi;
    midi.source = SyncSource::MidiClock;
    midi.bpm = 90.0;
    drive(manual, midi, 5.0);
    CHECK(manual.active() == SyncSource::Manual);
    CHECK(near(manual.bpm(), 128.0, 1.0e-12));
}

void testLossAndReturn() {
    BeatClock clock;
    clock.setTempo(120.0);
    BeatSource source;
    source.source = SyncSource::DjLink;
    source.bpm = 120.0;
    drive(clock, source, 8.0);
    CHECK(clock.locked());
    const double lockedBeat = clock.beat();
    source.silent = true;
    Run during;
    drive(clock, source, 4.0, &during);
    CHECK(clock.active() == SyncSource::Manual);
    CHECK(during.backward == 0.0);
    CHECK(near(clock.beat() - lockedBeat, 4.0 * 2.0, 0.02));
    source.silent = false;
    const double reference = clock.time() + 0.1;
    source.nextBoundary = reference;
    Run after;
    drive(clock, source, 8.0, &after);
    CHECK(clock.active() == SyncSource::DjLink);
    CHECK(clock.locked());
    CHECK(after.backward == 0.0);
    CHECK(clock.jumps() == 0);
    CHECK(std::fabs(wrapHalf(clock.beat() - (clock.time() - reference) * 2.0)) < 0.05);
    CHECK(clock.secondsSinceSeen(SyncSource::DjLink) >= 0.0);
    CHECK(clock.secondsSinceSeen(SyncSource::Timecode) < 0.0);
}

void testBarAlignment() {
    BeatClock clock;
    clock.setTempo(120.0);
    clock.shift(1.0);
    const double period = 0.5;
    double next = 0.0;
    int inBar = 0;
    for (int i = 0; i < static_cast<int>(20.0 / kFrame); ++i) {
        clock.advance(kFrame, true);
        const double now = clock.time();
        while (next <= now) {
            SyncObservation o;
            o.source = SyncSource::DjLink;
            o.hasPhase = true;
            o.beat = static_cast<double>(inBar % 4);
            o.modulus = 4;
            o.hasTempo = true;
            o.bpm = 120.0;
            o.age = now - next;
            clock.report(o);
            next += period;
            ++inBar;
        }
    }
    const double barPhase = std::fmod(clock.beat(), 4.0);
    const double expected = std::fmod(clock.time() / period, 4.0);
    double diff = std::fabs(barPhase - expected);
    diff = std::min(diff, 4.0 - diff);
    CHECK(diff < 0.3);
    CHECK(clock.jumps() == 0);
}

void testAbsolutePosition() {
    BeatClock clock;
    clock.setTempo(120.0);
    clock.setMode(SyncMode::Timecode);
    const double offset = 7200.0;
    double seconds = offset;
    for (int i = 0; i < 120; ++i) {
        clock.advance(kFrame, true);
        seconds += kFrame;
        SyncObservation o;
        o.source = SyncSource::Timecode;
        o.hasPhase = true;
        o.beat = seconds * clock.bpm() / 60.0;
        o.modulus = 0;
        clock.report(o);
    }
    CHECK(clock.active() == SyncSource::Timecode);
    CHECK(clock.jumps() == 1);
    CHECK(near(clock.beat(), seconds * 2.0, 0.05));
    CHECK(clock.locked());
    seconds = 5.0;
    for (int i = 0; i < 30; ++i) {
        clock.advance(kFrame, true);
        seconds += kFrame;
        SyncObservation o;
        o.source = SyncSource::Timecode;
        o.hasPhase = true;
        o.beat = seconds * 2.0;
        clock.report(o);
    }
    CHECK(clock.jumps() == 2);
    CHECK(near(clock.beat(), seconds * 2.0, 0.05));
    SyncObservation seek;
    seek.source = SyncSource::Timecode;
    seek.hasPhase = true;
    seek.beat = seconds * 2.0 + 0.3;
    seek.jump = true;
    clock.report(seek);
    CHECK(clock.jumps() == 3);
    CHECK(near(clock.beat(), seconds * 2.0 + 0.3, 1.0e-9));
    CHECK(clock.state() == LockState::Acquiring);
}

void testLatencyOffset() {
    BeatClock plain;
    plain.setTempo(120.0);
    BeatClock shifted;
    shifted.setTempo(120.0);
    shifted.setLatency(0.05);
    CHECK(near(shifted.latency(), 0.05, 1.0e-12));
    BeatSource a;
    a.source = SyncSource::MidiClock;
    BeatSource b = a;
    drive(plain, a, 10.0);
    drive(shifted, b, 10.0);
    const double delta = wrapHalf(shifted.beat() - plain.beat());
    CHECK(near(delta, 0.05 * 2.0, 0.03));
    shifted.setLatency(10.0);
    CHECK(near(shifted.latency(), 0.5, 1.0e-12));
    shifted.setLatency(-10.0);
    CHECK(near(shifted.latency(), -0.5, 1.0e-12));
}

void testObservationAge() {
    BeatClock clock;
    clock.setTempo(120.0);
    clock.advance(1.0, true);
    SyncObservation o;
    o.source = SyncSource::Timecode;
    o.hasPhase = true;
    o.beat = 10.0;
    o.age = 0.25;
    o.jump = true;
    clock.setMode(SyncMode::Timecode);
    clock.report(o);
    CHECK(near(clock.beat(), 10.5, 1.0e-9));
    clock.advance(0.5, true);
    CHECK(near(clock.beat(), 11.5, 1.0e-9));
}

void testManualObservationAndPause() {
    BeatClock clock;
    SyncObservation tempo;
    tempo.source = SyncSource::Manual;
    tempo.hasTempo = true;
    tempo.bpm = 90.0;
    clock.report(tempo);
    CHECK(near(clock.bpm(), 90.0, 1.0e-12));
    BeatClock paused;
    paused.setTempo(120.0);
    paused.setMode(SyncMode::MidiClock);
    SyncObservation o;
    o.source = SyncSource::MidiClock;
    o.hasPhase = true;
    o.beat = 0.0;
    o.modulus = 1;
    paused.advance(0.2, true);
    const double before = paused.beat();
    paused.advance(0.2, false);
    paused.report(o);
    paused.advance(0.2, false);
    CHECK(near(paused.beat(), before, 1.0e-12));
    CHECK(paused.pending() != 0.0);
}

void testNetworkSource() {
    BeatClock clock;
    clock.setTempo(100.0);
    const double masterBpm = 126.0;
    double masterBeat = 40.0;
    auto driveNetwork = [&](double seconds, bool report) {
        const int frames = static_cast<int>(std::round(seconds / kFrame));
        for (int i = 0; i < frames; ++i) {
            clock.advance(kFrame, true);
            masterBeat += kFrame * masterBpm / 60.0;
            if (report && i % 6 == 0) {
                SyncObservation o;
                o.source = SyncSource::Network;
                o.hasPhase = true;
                o.beat = masterBeat;
                o.modulus = 0;
                o.hasTempo = true;
                o.bpm = masterBpm;
                clock.report(o);
            }
        }
    };
    driveNetwork(0.2, true);
    CHECK(clock.active() == SyncSource::Network);
    CHECK(clock.jumps() == 1);
    driveNetwork(6.0, true);
    CHECK(near(clock.bpm(), masterBpm, 0.01));
    CHECK(near(clock.beat(), masterBeat, 0.05));
    CHECK(clock.locked());
    BeatSource link;
    link.source = SyncSource::DjLink;
    link.bpm = 140.0;
    drive(clock, link, 1.0);
    CHECK(clock.present(SyncSource::DjLink));
    driveNetwork(0.5, true);
    CHECK(clock.active() == SyncSource::Network);
    const double bpmBeforeLoss = clock.bpm();
    const double beatBeforeLoss = clock.beat();
    driveNetwork(2.5, false);
    CHECK(clock.active() != SyncSource::Network);
    CHECK(near(clock.bpm(), bpmBeforeLoss, 1.0e-9));
    CHECK(clock.beat() > beatBeforeLoss + 1.0);
    driveNetwork(1.0, true);
    CHECK(clock.active() == SyncSource::Network);
    masterBeat += 25.0;
    driveNetwork(0.2, true);
    CHECK(near(clock.beat(), masterBeat, 0.05));
    CHECK(clock.jumps() >= 2);
    BeatClock forced;
    forced.setMode(SyncMode::Network);
    CHECK(forced.state() == LockState::Searching);
    SyncObservation o;
    o.source = SyncSource::Network;
    o.hasPhase = true;
    o.beat = 8.0;
    o.hasTempo = true;
    o.bpm = 100.0;
    forced.advance(0.01, true);
    forced.report(o);
    CHECK(forced.active() == SyncSource::Network);
}

void testNames() {
    for (int i = 0; i <= static_cast<int>(SyncMode::Network); ++i) {
        const SyncMode mode = static_cast<SyncMode>(i);
        CHECK(nvj::syncModeFromName(nvj::syncModeName(mode)) == mode);
    }
    CHECK(nvj::syncModeFromName("nonsense") == SyncMode::Auto);
    CHECK(nvj::syncModeFromName(nullptr) == SyncMode::Auto);
    CHECK(nvj::modeSource(SyncMode::Auto) == SyncSource::Manual);
    CHECK(nvj::modeSource(SyncMode::DjLink) == SyncSource::DjLink);
    CHECK(nvj::modeSource(SyncMode::Network) == SyncSource::Network);
    CHECK(std::string(nvj::syncModeName(SyncMode::Network)) == "network");
    CHECK(std::string(nvj::syncSourceName(SyncSource::Network)) == "Network");
    CHECK(std::string(nvj::syncSourceName(SyncSource::MidiClock)) == "MIDI clock");
}

}

int main() {
    testFreeRun();
    testTap();
    testNudge();
    testPhaseContinuityAndLock();
    testSlewNeverJumpsBackwards();
    testTempoFollow();
    testPriorityAuto();
    testForcedMode();
    testLossAndReturn();
    testBarAlignment();
    testAbsolutePosition();
    testLatencyOffset();
    testObservationAge();
    testNetworkSource();
    testManualObservationAndPause();
    testNames();
    return testing::finish();
}

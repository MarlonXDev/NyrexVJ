#include "check.hpp"
#include "core/beat_clock.hpp"
#include "core/midi_clock.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

double circular(double a, double b, double modulus) {
    double d = std::fmod(a - b, modulus);
    if (d < 0.0) {
        d += modulus;
    }
    return std::min(d, modulus - d);
}

struct Source {
    nvj::MidiClockTracker tracker;
    double time = 0.0;
    double nextTick = 0.0;
    double bpm = 120.0;
    double jitter = 0.0008;
    unsigned seed = 5;

    double noise() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<double>(seed >> 8) / static_cast<double>(1u << 23) - 1.0) * jitter;
    }

    void ticks(int count) {
        for (int i = 0; i < count; ++i) {
            time = nextTick;
            tracker.onClock(time + noise());
            nextTick += 60.0 / (bpm * 24.0);
        }
    }

    void seconds(double duration) {
        const double end = nextTick + duration;
        while (nextTick < end) {
            ticks(1);
        }
    }
};

void testTempo() {
    Source source;
    CHECK(source.tracker.bpm() == 0.0);
    CHECK(!source.tracker.active(0.0));
    source.ticks(12);
    CHECK(source.tracker.bpm() == 0.0);
    source.ticks(1);
    CHECK(near(source.tracker.bpm(), 120.0, 1.5));
    source.seconds(10.0);
    CHECK(near(source.tracker.bpm(), 120.0, 0.1));
    CHECK(source.tracker.active(source.time));
    CHECK(source.tracker.active(source.time + 0.9));
    CHECK(!source.tracker.active(source.time + 1.1));
    CHECK(source.tracker.running());
    CHECK(!source.tracker.positionKnown());
    source.bpm = 140.0;
    source.seconds(1.0);
    const double mixed = source.tracker.bpm();
    CHECK(mixed > 120.0 && mixed < 140.0);
    source.seconds(3.0);
    CHECK(near(source.tracker.bpm(), 140.0, 0.2));
    source.bpm = 60.0;
    source.seconds(6.0);
    CHECK(near(source.tracker.bpm(), 60.0, 0.2));
}

void testGapAndOrder() {
    Source source;
    source.seconds(3.0);
    CHECK(source.tracker.bpm() > 100.0);
    const unsigned long long ticks = source.tracker.ticks();
    source.tracker.onClock(source.time - 1.0);
    CHECK(source.tracker.ticks() == ticks);
    source.tracker.onClock(NAN);
    CHECK(source.tracker.ticks() == ticks);
    source.nextTick += 1.5;
    source.ticks(5);
    CHECK(source.tracker.gaps() == 1);
    CHECK(source.tracker.bpm() == 0.0);
    source.ticks(10);
    CHECK(near(source.tracker.bpm(), 120.0, 2.0));
    source.tracker.reset();
    CHECK(source.tracker.bpm() == 0.0);
    CHECK(!source.tracker.positionKnown());
}

void testObservationsWithoutStart() {
    Source source;
    nvj::SyncObservation observation;
    int seen = 0;
    for (int i = 0; i < 24 * 8; ++i) {
        source.ticks(1);
        if (source.tracker.takeObservation(observation, source.time, true)) {
            ++seen;
            CHECK(observation.source == nvj::SyncSource::MidiClock);
            CHECK(!observation.hasPhase);
            if (i > 40) {
                CHECK(observation.hasTempo);
                CHECK(near(observation.bpm, 120.0, 0.5));
            }
        }
    }
    CHECK(seen == 8);
    CHECK(!source.tracker.takeObservation(observation, source.time, true));
}

void testStartPositionAndBars() {
    Source source;
    source.seconds(2.0);
    nvj::SyncObservation observation;
    source.tracker.onStart(source.time);
    CHECK(source.tracker.positionKnown());
    CHECK(source.tracker.starts() == 1);
    std::vector<double> beats;
    bool firstJump = false;
    for (int i = 0; i < 24 * 10; ++i) {
        source.ticks(1);
        if (source.tracker.takeObservation(observation, source.time + 0.004, true)) {
            if (beats.empty()) {
                firstJump = observation.jump;
            } else {
                CHECK(!observation.jump);
            }
            CHECK(observation.hasPhase);
            CHECK(observation.modulus == 4);
            CHECK(near(observation.age, 0.004, 0.0015));
            beats.push_back(observation.beat);
        }
    }
    CHECK(firstJump);
    CHECK(beats.size() == 10);
    bool sequence = beats.size() == 10;
    for (size_t i = 0; sequence && i < beats.size(); ++i) {
        sequence = near(beats[i], static_cast<double>(i % 4), 1.0e-9);
    }
    CHECK(sequence);
    CHECK(near(source.tracker.position(), 10.0, 0.05));

    source.tracker.onStart(source.time);
    int count = 0;
    for (int i = 0; i < 24 * 2 + 1; ++i) {
        source.ticks(1);
        if (source.tracker.takeObservation(observation, source.time, false)) {
            ++count;
            CHECK(observation.modulus == 1);
            CHECK(near(observation.beat, 0.0, 1.0e-9));
            CHECK(count > 1 || observation.jump);
        }
    }
    CHECK(count == 3);
}

void testStopContinueAndPosition() {
    Source source;
    source.seconds(1.0);
    source.tracker.onStart(source.time);
    nvj::SyncObservation observation;
    source.ticks(24 * 3 + 5);
    while (source.tracker.takeObservation(observation, source.time, true)) {
    }
    source.tracker.onStop(source.time);
    CHECK(!source.tracker.running());
    const double stopped = source.tracker.position();
    for (int i = 0; i < 24 * 4; ++i) {
        source.ticks(1);
        CHECK(!source.tracker.takeObservation(observation, source.time, true));
    }
    CHECK(near(source.tracker.position(), stopped, 1.0e-9));
    CHECK(source.tracker.active(source.time));
    CHECK(near(source.tracker.bpm(), 120.0, 0.2));

    source.tracker.onContinue(source.time);
    CHECK(source.tracker.running());
    bool jumped = false;
    int observations = 0;
    for (int i = 0; i < 24 * 3; ++i) {
        source.ticks(1);
        if (source.tracker.takeObservation(observation, source.time, true)) {
            ++observations;
            jumped = jumped || observation.jump;
            CHECK(observation.hasPhase);
        }
    }
    CHECK(observations >= 2);
    CHECK(jumped);

    source.tracker.onSongPosition(16, source.time);
    CHECK(near(source.tracker.position(), 4.0, 1.0e-9));
    source.ticks(1);
    CHECK(source.tracker.takeObservation(observation, source.time, true));
    CHECK(near(observation.beat, 0.0, 1.0e-9));
    CHECK(observation.jump);
    source.tracker.onSongPosition(18, source.time);
    CHECK(near(source.tracker.position(), 4.5, 1.0e-9));
    for (int i = 0; i < 12; ++i) {
        source.ticks(1);
        CHECK(!source.tracker.takeObservation(observation, source.time, true) || i == 11);
    }
    source.tracker.onSongPosition(1000000, source.time);
    CHECK(source.tracker.position() <= 16383.0 * 6.0 / 24.0 + 1.0e-9);
    source.tracker.onSongPosition(-5, source.time);
    CHECK(near(source.tracker.position(), 0.0, 1.0e-9));
}

void testSongPositionWithoutStart() {
    Source source;
    source.seconds(1.0);
    source.tracker.onSongPosition(8, source.time);
    CHECK(source.tracker.positionKnown());
    nvj::SyncObservation observation;
    source.ticks(1);
    CHECK(source.tracker.takeObservation(observation, source.time, true));
    CHECK(observation.hasPhase);
    CHECK(near(observation.beat, 2.0, 1.0e-9));
}

void testTempoRange() {
    Source slow;
    slow.bpm = 20.0;
    slow.jitter = 0.0;
    nvj::SyncObservation observation;
    bool anyTempo = false;
    for (int i = 0; i < 24 * 4; ++i) {
        slow.ticks(1);
        if (slow.tracker.takeObservation(observation, slow.time, true)) {
            anyTempo = anyTempo || observation.hasTempo;
        }
    }
    CHECK(!anyTempo);
    Source fast;
    fast.bpm = 300.0;
    fast.jitter = 0.0;
    for (int i = 0; i < 24 * 4; ++i) {
        fast.ticks(1);
        if (fast.tracker.takeObservation(observation, fast.time, true)) {
            anyTempo = anyTempo || observation.hasTempo;
        }
    }
    CHECK(!anyTempo);
}

void testWithBeatClock() {
    Source source;
    source.bpm = 128.0;
    nvj::BeatClock clock;
    clock.setMode(nvj::SyncMode::Auto);
    double time = 0.0;
    const double dt = 1.0 / 60.0;
    auto run = [&](double seconds, bool sendClock) {
        const double end = time + seconds;
        while (time < end) {
            time += dt;
            if (sendClock) {
                while (source.nextTick <= time) {
                    source.time = source.nextTick;
                    source.tracker.onClock(source.nextTick + source.noise());
                    source.nextTick += 60.0 / (source.bpm * 24.0);
                }
            } else {
                source.nextTick = time;
            }
            nvj::SyncObservation observation;
            while (source.tracker.takeObservation(observation, time, true)) {
                clock.report(observation);
            }
            clock.advance(dt, true);
        }
    };
    source.tracker.onStart(0.0);
    run(20.0, true);
    CHECK(clock.active() == nvj::SyncSource::MidiClock);
    CHECK(clock.locked());
    CHECK(near(clock.bpm(), 128.0, 0.8));
    const double expected = std::fmod(source.tracker.position() + (time - source.time) * 128.0 / 60.0, 4.0);
    const double offset = circular(clock.beat(), expected, 4.0);
    std::printf("midi clock phase offset %.3f beats, bpm %.2f\n", offset, clock.bpm());
    CHECK(offset < 0.15);

    run(3.0, false);
    CHECK(clock.active() != nvj::SyncSource::MidiClock);

    source.tracker.onStart(time);
    run(10.0, true);
    CHECK(clock.active() == nvj::SyncSource::MidiClock);
    CHECK(clock.locked());
    const double expectedAfter = std::fmod(source.tracker.position() + (time - source.time) * 128.0 / 60.0, 4.0);
    CHECK(circular(clock.beat(), expectedAfter, 4.0) < 0.2);
    CHECK(clock.jumps() >= 2);
}

}

int main() {
    testTempo();
    testGapAndOrder();
    testObservationsWithoutStart();
    testStartPositionAndBars();
    testStopContinueAndPosition();
    testSongPositionWithoutStart();
    testTempoRange();
    testWithBeatClock();
    return testing::finish();
}

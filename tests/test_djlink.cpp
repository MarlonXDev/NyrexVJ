#include "check.hpp"
#include "control/djlink_input.hpp"
#include "core/beat_clock.hpp"
#include "core/djlink.hpp"
#include "djlink_sim.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
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

void testParse() {
    nvj::DjBeat beat;
    const std::vector<unsigned char> packet = djtest::beatPacket(2, "CDJ-3000", 128.0, 4.0, 3, 450, 1350);
    CHECK(packet.size() == 96);
    CHECK(nvj::parseDjBeat(packet.data(), packet.size(), beat));
    CHECK(beat.player == 2);
    CHECK(beat.name == "CDJ-3000");
    CHECK(near(beat.trackBpm, 128.0, 1.0e-9));
    CHECK(near(beat.pitch, 1.04, 1.0e-6));
    CHECK(near(beat.bpm, 133.12, 1.0e-4));
    CHECK(beat.beatInBar == 3);
    CHECK(near(beat.nextBeatMs, 450.0 / 1.04, 1.0e-3));
    CHECK(near(beat.nextBarMs, 1350.0 / 1.04, 1.0e-3));

    std::vector<unsigned char> copy = packet;
    copy[44] = copy[45] = copy[46] = copy[47] = 0xff;
    CHECK(nvj::parseDjBeat(copy.data(), copy.size(), beat));
    CHECK(beat.nextBarMs < 0.0);

    copy = packet;
    copy.resize(140, 0x55);
    CHECK(nvj::parseDjBeat(copy.data(), copy.size(), beat));

    copy = djtest::beatPacket(1, "XDJ-XZ", 90.5, -8.0, 0);
    CHECK(nvj::parseDjBeat(copy.data(), copy.size(), beat));
    CHECK(beat.beatInBar == 0);
    CHECK(near(beat.pitch, 0.92, 1.0e-6));
    CHECK(near(beat.bpm, 90.5 * 0.92, 1.0e-3));

    nvj::DjBeat untouched;
    untouched.player = 99;
    std::vector<unsigned char> bad = packet;
    bad[0] = 0x00;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[10] = 0x0a;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[31] = 0x00;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[32] = 0x03;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[95] = 3;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[84] = bad[85] = bad[86] = bad[87] = 0;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[84] = 0x01;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[90] = bad[91] = 0;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[90] = bad[91] = 0xff;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    bad = packet;
    bad[92] = 5;
    CHECK(!nvj::parseDjBeat(bad.data(), bad.size(), untouched));
    CHECK(!nvj::parseDjBeat(nullptr, 96, untouched));
    CHECK(untouched.player == 99);
    for (size_t size = 0; size < 96; ++size) {
        CHECK(!nvj::parseDjBeat(packet.data(), size, untouched));
    }
    const std::vector<unsigned char> onAir = djtest::onAirPacket(33);
    CHECK(!nvj::parseDjBeat(onAir.data(), onAir.size(), untouched));
    CHECK(nvj::isDjLinkPacket(onAir.data(), onAir.size()));
    CHECK(nvj::isDjLinkPacket(packet.data(), packet.size()));
    CHECK(!nvj::isDjLinkPacket(bad.data(), 4));
    CHECK(!nvj::isDjLinkPacket(nullptr, 0));
    std::vector<unsigned char> named = packet;
    named[12] = 0x01;
    named[13] = 0xe9;
    CHECK(nvj::parseDjBeat(named.data(), named.size(), beat));
    CHECK(beat.name.size() == 8);
    CHECK(beat.name[1] == '?' && beat.name[2] == '?');
}

void testFuzz() {
    const std::vector<unsigned char> base = djtest::beatPacket(1, "CDJ-2000NXS2", 126.0, 0.0, 2);
    std::uint32_t state = 2463534242u;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    int accepted = 0;
    for (int round = 0; round < 20000; ++round) {
        std::vector<unsigned char> data = base;
        const int flips = 1 + static_cast<int>(next() % 6);
        for (int i = 0; i < flips; ++i) {
            data[next() % data.size()] = static_cast<unsigned char>(next());
        }
        if (round % 7 == 0) {
            data.resize(next() % 130);
        }
        nvj::DjBeat beat;
        if (nvj::parseDjBeat(data.data(), data.size(), beat)) {
            ++accepted;
            CHECK(beat.beatInBar >= 0 && beat.beatInBar <= 4);
            CHECK(beat.bpm > 0.0 && std::isfinite(beat.bpm));
            CHECK(beat.pitch > 0.0 && beat.pitch <= 4.0);
        }
    }
    CHECK(accepted > 100);
}

nvj::DjBeat beatFrom(int player, double bpm, int bar) {
    nvj::DjBeat beat;
    beat.player = player;
    beat.trackBpm = bpm;
    beat.bpm = bpm;
    beat.beatInBar = bar;
    return beat;
}

void testTrackerFollowing() {
    nvj::DjLinkTracker tracker;
    CHECK(!tracker.active());
    CHECK(tracker.followed() == 0);
    nvj::SyncObservation observation;
    CHECK(!tracker.takeObservation(observation, true));

    tracker.onBeat(beatFrom(2, 126.0, 1));
    CHECK(tracker.active());
    CHECK(tracker.followed() == 2);
    CHECK(tracker.takeObservation(observation, true));
    CHECK(observation.source == nvj::SyncSource::DjLink);
    CHECK(observation.hasPhase && observation.hasTempo);
    CHECK(observation.modulus == 4);
    CHECK(near(observation.beat, 0.0, 1.0e-9));
    CHECK(near(observation.bpm, 126.0, 1.0e-9));
    CHECK(observation.jump);
    CHECK(!tracker.takeObservation(observation, true));

    tracker.advance(0.25);
    tracker.onBeat(beatFrom(1, 100.0, 2));
    CHECK(tracker.followed() == 2);
    CHECK(tracker.ignored() == 1);
    CHECK(!tracker.takeObservation(observation, true));
    CHECK(tracker.playerSeen(1));
    CHECK(tracker.playerSeen(2));
    CHECK(!tracker.playerSeen(3));

    tracker.onBeat(beatFrom(2, 126.0, 3));
    tracker.advance(0.125);
    CHECK(tracker.takeObservation(observation, true));
    CHECK(near(observation.beat, 2.0, 1.0e-9));
    CHECK(near(observation.age, 0.125, 1.0e-9));
    CHECK(!observation.jump);
    tracker.onBeat(beatFrom(2, 126.0, 4));
    CHECK(tracker.takeObservation(observation, false));
    CHECK(observation.modulus == 1);
    CHECK(near(observation.beat, 0.0, 1.0e-9));
    tracker.onBeat(beatFrom(2, 126.0, 0));
    CHECK(tracker.takeObservation(observation, true));
    CHECK(observation.modulus == 1);

    tracker.advance(2.5);
    CHECK(!tracker.active());
    CHECK(near(tracker.secondsSinceBeat(), 2.5, 1.0e-9));
    tracker.onBeat(beatFrom(1, 100.0, 2));
    CHECK(tracker.followed() == 1);
    CHECK(tracker.takeObservation(observation, true));
    CHECK(observation.jump);
    CHECK(near(observation.bpm, 100.0, 1.0e-9));

    tracker.onBeat(beatFrom(0, 120.0, 1));
    tracker.onBeat(beatFrom(33, 120.0, 1));
    tracker.onBeat(beatFrom(7, 120.0, 1));
    CHECK(tracker.followed() == 1);
    CHECK(tracker.ignored() == 4);
    tracker.onMalformed();
    tracker.onOther();
    tracker.onOther();
    CHECK(tracker.malformed() == 1);
    CHECK(tracker.other() == 2);
}

void testTrackerSelection() {
    nvj::DjLinkTracker tracker;
    tracker.setPlayer(3);
    CHECK(tracker.player() == 3);
    tracker.onBeat(beatFrom(1, 120.0, 1));
    tracker.onBeat(beatFrom(2, 120.0, 1));
    CHECK(tracker.followed() == 0);
    CHECK(!tracker.active());
    CHECK(tracker.ignored() == 2);
    tracker.onBeat(beatFrom(3, 124.0, 2));
    CHECK(tracker.followed() == 3);
    CHECK(tracker.active());
    nvj::SyncObservation observation;
    CHECK(tracker.takeObservation(observation, true));
    CHECK(near(observation.bpm, 124.0, 1.0e-9));
    tracker.setPlayer(1);
    CHECK(tracker.followed() == 0);
    tracker.onBeat(beatFrom(3, 124.0, 3));
    CHECK(tracker.followed() == 0);
    tracker.onBeat(beatFrom(1, 120.0, 1));
    CHECK(tracker.followed() == 1);
    CHECK(tracker.takeObservation(observation, true));
    CHECK(observation.jump);
    tracker.setPlayer(0);
    tracker.onBeat(beatFrom(4, 120.0, 1));
    CHECK(tracker.followed() == 1);
    tracker.setPlayer(99);
    CHECK(tracker.player() == nvj::kDjLinkMaxPlayer);
    tracker.setPlayer(-4);
    CHECK(tracker.player() == 0);
    tracker.reset();
    CHECK(tracker.followed() == 0);
    CHECK(!tracker.active());
    CHECK(!tracker.playerSeen(1));
    CHECK(!tracker.takeObservation(observation, true));
}

struct Simulation {
    nvj::DjLinkTracker tracker;
    nvj::BeatClock clock;
    double time = 0.0;
    double nextBeat = 0.0;
    long long beatIndex = 0;
    int firstBar = 1;
    double bpm = 128.0;
    double pitch = 0.0;
    bool sending = true;
    bool barAlign = true;
    unsigned seed = 99;

    double jitter() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<double>(seed >> 8) / static_cast<double>(1u << 23) - 1.0) * 0.004;
    }

    void run(double seconds) {
        const double dt = 1.0 / 60.0;
        const double end = time + seconds;
        while (time < end) {
            time += dt;
            const double effective = bpm * (1.0 + pitch / 100.0);
            while (sending && nextBeat + jitter() <= time) {
                const int bar = static_cast<int>((beatIndex + firstBar - 1) % 4) + 1;
                const std::vector<unsigned char> packet = djtest::beatPacket(1, "CDJ-3000", bpm, pitch, bar);
                nvj::DjBeat beat;
                CHECK(nvj::parseDjBeat(packet.data(), packet.size(), beat));
                tracker.onBeat(beat);
                ++beatIndex;
                nextBeat += 60.0 / effective;
            }
            if (!sending) {
                nextBeat = time;
            }
            tracker.advance(dt);
            nvj::SyncObservation observation;
            while (tracker.takeObservation(observation, barAlign)) {
                clock.report(observation);
            }
            clock.advance(dt, true);
        }
    }
};

void testEndToEnd() {
    Simulation sim;
    sim.run(20.0);
    CHECK(sim.clock.active() == nvj::SyncSource::DjLink);
    CHECK(sim.clock.locked());
    CHECK(near(sim.clock.bpm(), 128.0, 0.6));
    const double expected = std::fmod(static_cast<double>(sim.beatIndex - 1) + (sim.firstBar - 1) + (sim.time - (sim.nextBeat - 60.0 / 128.0)) / (60.0 / 128.0), 4.0);
    CHECK(circular(sim.clock.beat(), expected, 4.0) < 0.15);
    std::printf("djlink phase offset %.3f beats, bpm %.2f, state locked %d\n", circular(sim.clock.beat(), expected, 4.0), sim.clock.bpm(), sim.clock.locked() ? 1 : 0);

    sim.pitch = 6.0;
    sim.run(12.0);
    CHECK(near(sim.clock.bpm(), 128.0 * 1.06, 1.0));
    CHECK(sim.clock.active() == nvj::SyncSource::DjLink);

    sim.sending = false;
    sim.run(4.0);
    CHECK(sim.clock.active() != nvj::SyncSource::DjLink);
    CHECK(!sim.tracker.active());

    sim.sending = true;
    sim.firstBar = 3;
    sim.beatIndex = 0;
    sim.run(8.0);
    CHECK(sim.clock.active() == nvj::SyncSource::DjLink);
    CHECK(sim.clock.locked());
    const double perBeat = 60.0 / (128.0 * 1.06);
    const double expectedAfter = std::fmod(static_cast<double>(sim.beatIndex - 1) + 2.0 + (sim.time - (sim.nextBeat - perBeat)) / perBeat, 4.0);
    CHECK(circular(sim.clock.beat(), expectedAfter, 4.0) < 0.2);
    CHECK(sim.clock.jumps() >= 2);
}

void testEndToEndBeatOnly() {
    Simulation sim;
    sim.barAlign = false;
    sim.run(15.0);
    CHECK(sim.clock.locked());
    const double perBeat = 60.0 / 128.0;
    const double sinceBeat = sim.time - (sim.nextBeat - perBeat);
    const double phase = std::fmod(sim.clock.beat(), 1.0);
    CHECK(circular(phase, sinceBeat / perBeat, 1.0) < 0.15);
}

void testSocket() {
    nvj::DjLinkInput input;
    std::string error;
    CHECK(!input.start(0, error));
    CHECK(!error.empty());
    CHECK(!input.start(70000, error));
    CHECK(!input.running());
    nvj::DjLinkTracker tracker;
    input.poll(tracker);
    int port = 0;
    for (int candidate = 52871; candidate < 52890 && port == 0; ++candidate) {
        if (input.start(candidate, error)) {
            port = candidate;
        }
    }
    CHECK(port != 0);
    if (port == 0) {
        return;
    }
    CHECK(input.running());
    CHECK(input.port() == port);
    {
        nvj::DjLinkInput second;
        CHECK(second.start(port, error));
    }
    const std::vector<unsigned char> beat = djtest::beatPacket(4, "CDJ-3000", 132.0, 0.0, 2);
    const std::vector<unsigned char> onAir = djtest::onAirPacket(33);
    const unsigned char garbage[7] = {1, 2, 3, 4, 5, 6, 7};
    CHECK(nvj::UdpSocket::sendLoopback(port, beat.data(), static_cast<int>(beat.size())));
    CHECK(nvj::UdpSocket::sendLoopback(port, onAir.data(), static_cast<int>(onAir.size())));
    CHECK(nvj::UdpSocket::sendLoopback(port, garbage, 7));
    for (int attempt = 0; attempt < 200 && input.datagrams() < 3; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        input.poll(tracker);
    }
    CHECK(input.datagrams() == 3);
    CHECK(tracker.followed() == 4);
    CHECK(tracker.packets() == 1);
    CHECK(tracker.other() == 1);
    CHECK(tracker.malformed() == 1);
    nvj::SyncObservation observation;
    CHECK(tracker.takeObservation(observation, true));
    CHECK(near(observation.bpm, 132.0, 1.0e-9));
    CHECK(near(observation.beat, 1.0, 1.0e-9));
    input.stop();
    CHECK(!input.running());
    input.poll(tracker);
}

}

int main() {
    testParse();
    testFuzz();
    testTrackerFollowing();
    testTrackerSelection();
    testEndToEnd();
    testEndToEndBeatOnly();
    testSocket();
    return testing::finish();
}

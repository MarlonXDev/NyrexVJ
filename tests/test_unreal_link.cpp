#include "check.hpp"

#include "Core/NyrexLink.h"
#include "Core/NyrexState.h"
#include "control/osc.hpp"
#include "control/osc_out.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace NyrexCore;

bool near(double a, double b, double tolerance = 1e-6) {
    return std::fabs(a - b) < tolerance;
}

void testStatesAndReconnect() {
    LinkMonitor link;
    CHECK(link.state() == LinkState::Stopped);
    CHECK(!link.connected());
    link.onHello(0.0, true);
    link.onPacket(0.0);
    CHECK(!link.evaluate(0.0).connected);
    CHECK(link.state() == LinkState::Stopped);

    link.setListening(true, 0.0);
    CHECK(link.state() == LinkState::Waiting);
    LinkChange change = link.evaluate(1.0);
    CHECK(!change.connected && !change.disconnected && !change.incompatible);
    CHECK(link.state() == LinkState::Waiting);

    link.onPacket(2.0);
    link.onHello(2.0, true);
    change = link.evaluate(2.0);
    CHECK(change.connected && !change.disconnected);
    CHECK(link.connected() && link.state() == LinkState::Connected);
    change = link.evaluate(2.5);
    CHECK(!change.connected && !change.disconnected);

    link.onHello(4.0, true);
    CHECK(!link.evaluate(8.9).disconnected);
    CHECK(link.connected());
    change = link.evaluate(9.1);
    CHECK(change.disconnected && !change.connected);
    CHECK(link.state() == LinkState::Lost);
    CHECK(!link.evaluate(20.0).disconnected);
    CHECK(link.state() == LinkState::Lost);

    link.onPacket(21.0);
    link.onHello(21.0, true);
    change = link.evaluate(21.0);
    CHECK(change.connected);
    CHECK(link.state() == LinkState::Connected);

    const LinkStats stats = link.stats(22.0);
    CHECK(stats.connections == 2);
    CHECK(stats.losses == 1);
    CHECK(stats.hellos == 3);
    CHECK(near(stats.connectedSeconds, 1.0));
    CHECK(near(stats.secondsSinceLastHello, 1.0));
    CHECK(near(stats.secondsSinceLastPacket, 1.0));
    CHECK(near(stats.longestGapSeconds, 19.0));
}

void testByeAndStop() {
    LinkMonitor link;
    link.setListening(true, 0.0);
    link.onHello(1.0, true);
    CHECK(link.evaluate(1.0).connected);
    link.onBye(2.0);
    LinkChange change = link.evaluate(2.0);
    CHECK(change.disconnected);
    CHECK(link.state() == LinkState::Ended);
    CHECK(link.stats(2.0).losses == 0);
    CHECK(!link.evaluate(30.0).connected);
    CHECK(link.state() == LinkState::Ended);

    link.onHello(31.0, true);
    CHECK(link.evaluate(31.0).connected);
    CHECK(link.stats(31.0).connections == 2);

    link.setListening(false, 32.0);
    CHECK(link.state() == LinkState::Stopped);
    CHECK(!link.connected());
    link.onHello(33.0, true);
    link.onPacket(33.0);
    CHECK(!link.evaluate(33.0).connected);
    CHECK(link.stats(33.0).packets == 0);

    link.setListening(true, 40.0);
    CHECK(link.state() == LinkState::Waiting);
    CHECK(link.stats(40.0).connections == 0);
    CHECK(link.stats(40.0).secondsSinceLastPacket < 0.0);
}

void testIncompatible() {
    LinkMonitor link;
    link.setListening(true, 0.0);
    link.onPacket(1.0);
    link.onHello(1.0, false);
    LinkChange change = link.evaluate(1.0);
    CHECK(change.incompatible && !change.connected);
    CHECK(link.state() == LinkState::Incompatible);
    link.onHello(2.0, false);
    change = link.evaluate(2.0);
    CHECK(!change.incompatible);
    CHECK(link.state() == LinkState::Incompatible);
    link.evaluate(10.0);
    CHECK(link.state() == LinkState::Waiting);

    link.onHello(11.0, true);
    CHECK(link.evaluate(11.0).connected);
    link.onHello(12.0, false);
    change = link.evaluate(12.0);
    CHECK(change.disconnected && change.incompatible);
    CHECK(link.state() == LinkState::Incompatible);
    CHECK(link.stats(12.0).losses == 0);
    link.evaluate(20.0);
    CHECK(link.state() == LinkState::Lost);
}

void testRateAndGaps() {
    LinkMonitor link;
    link.setListening(true, 0.0);
    link.onHello(0.0, true);
    for (int i = 0; i < 125; ++i) {
        link.onPacket(i / 60.0);
    }
    link.evaluate(2.08);
    LinkStats stats = link.stats(2.08);
    CHECK(stats.packetsPerSecond > 50.0 && stats.packetsPerSecond < 70.0);
    CHECK(stats.packets == 125);
    CHECK(stats.longestGapSeconds < 0.02);
    CHECK(link.stats(10.0).packetsPerSecond == 0.0);

    link.onPacket(12.0);
    CHECK(link.stats(12.0).longestGapSeconds > 9.0);
}

void testBadTimes() {
    LinkMonitor link(-3.0);
    link.setListening(true, 0.0);
    link.onHello(10.0, true);
    CHECK(link.evaluate(10.0).connected);
    CHECK(!link.evaluate(14.9).disconnected);
    CHECK(link.evaluate(15.1).disconnected);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    link.onPacket(nan);
    link.onHello(inf, true);
    link.onBye(nan);
    const LinkChange change = link.evaluate(nan);
    CHECK(!change.connected && !change.disconnected && !change.incompatible);
    CHECK(link.stats(nan).secondsSinceLastPacket < 0.0 || std::isfinite(link.stats(nan).secondsSinceLastPacket));

    LinkMonitor back;
    back.setListening(true, 100.0);
    back.onHello(100.0, true);
    CHECK(back.evaluate(100.0).connected);
    back.onPacket(50.0);
    CHECK(!back.evaluate(60.0).disconnected);
    CHECK(back.stats(60.0).secondsSinceLastHello == 0.0);

    LinkMonitor tiny(0.0);
    tiny.setListening(true, 0.0);
    tiny.onHello(0.0, true);
    CHECK(tiny.evaluate(0.0).connected);
    CHECK(!tiny.evaluate(4.0).disconnected);
    CHECK(tiny.evaluate(5.5).disconnected);

    CHECK(std::string(linkStateName(LinkState::Waiting)) == "Waiting");
    CHECK(std::string(linkStateName(LinkState::Incompatible)) == "Incompatible");
    CHECK(std::string(linkStateName(LinkState::Lost)) == "Lost");
}

void testLatencyBasics() {
    LatencyTracker tracker;
    CHECK(!tracker.stats().valid);
    CHECK(tracker.due(0.0, 1.0));
    const std::int32_t first = tracker.beginPing(10.0);
    CHECK(first == 1);
    CHECK(!tracker.due(10.5, 1.0));
    CHECK(tracker.due(11.0, 1.0));
    CHECK(!tracker.due(11.0, 0.0));
    CHECK(!tracker.due(11.0, -1.0));
    CHECK(tracker.onPong(first, 10.012));
    LatencyStats stats = tracker.stats();
    CHECK(stats.valid && stats.samples == 1);
    CHECK(near(stats.lastMs, 12.0, 1e-6) && near(stats.averageMs, 12.0, 1e-6));
    CHECK(near(stats.minMs, 12.0, 1e-6) && near(stats.maxMs, 12.0, 1e-6) && near(stats.p95Ms, 12.0, 1e-6));
    CHECK(stats.sent == 1 && stats.received == 1 && stats.lost == 0 && stats.ignored == 0);

    CHECK(!tracker.onPong(first, 10.02));
    CHECK(tracker.stats().ignored == 1);
    CHECK(!tracker.onPong(777, 10.02));
    CHECK(tracker.stats().ignored == 2);
    CHECK(tracker.stats().received == 1);
}

void testLatencyStatistics() {
    LatencyTracker tracker;
    for (int i = 1; i <= 20; ++i) {
        const double now = i * 1.0;
        const std::int32_t id = tracker.beginPing(now);
        CHECK(tracker.onPong(id, now + i * 0.001));
    }
    const LatencyStats stats = tracker.stats();
    CHECK(stats.samples == 20 && stats.received == 20);
    CHECK(near(stats.minMs, 1.0, 1e-6));
    CHECK(near(stats.maxMs, 20.0, 1e-6));
    CHECK(near(stats.averageMs, 10.5, 1e-6));
    CHECK(near(stats.lastMs, 20.0, 1e-6));
    CHECK(near(stats.p95Ms, 19.0, 1e-6));
}

void testLatencyWindowAndLoss() {
    LatencyTracker tracker;
    for (int i = 1; i <= 100; ++i) {
        const double now = i * 1.0;
        const std::int32_t id = tracker.beginPing(now);
        tracker.onPong(id, now + (i <= 36 ? 0.100 : 0.010));
    }
    LatencyStats stats = tracker.stats();
    CHECK(stats.samples == kLatencyWindow);
    CHECK(near(stats.maxMs, 10.0, 1e-6) && near(stats.averageMs, 10.0, 1e-6));
    CHECK(stats.received == 100);

    LatencyTracker lossy;
    const std::int32_t a = lossy.beginPing(0.0);
    const std::int32_t b = lossy.beginPing(0.5);
    CHECK(a != b);
    lossy.expire(1.0);
    CHECK(lossy.stats().lost == 0);
    lossy.expire(2.2);
    CHECK(lossy.stats().lost == 1);
    CHECK(!lossy.onPong(a, 2.3));
    CHECK(lossy.stats().ignored == 1);
    CHECK(!lossy.onPong(b, 3.0));
    CHECK(lossy.stats().lost == 1 || lossy.stats().lost == 2);
    CHECK(lossy.stats().received == 0);

    LatencyTracker late;
    const std::int32_t id = late.beginPing(0.0);
    CHECK(!late.onPong(id, 2.5));
    CHECK(late.stats().ignored == 1 && late.stats().received == 0);

    LatencyTracker backwards;
    const std::int32_t back = backwards.beginPing(5.0);
    CHECK(!backwards.onPong(back, 4.0));
    CHECK(!backwards.stats().valid);
}

void testLatencyLimitsAndIds() {
    LatencyTracker tracker;
    for (std::size_t i = 0; i < kMaxPendingPings + 10; ++i) {
        tracker.beginPing(1.0 + i * 0.001);
    }
    CHECK(tracker.stats().sent == kMaxPendingPings + 10);
    CHECK(tracker.stats().lost == 10);
    CHECK(!tracker.onPong(1, 1.1));
    CHECK(tracker.onPong(static_cast<std::int32_t>(kMaxPendingPings + 10), 1.1));

    LatencyTracker wrap;
    std::int32_t last = 0;
    double now = 0.0;
    for (std::int32_t i = 0; i < kMaxPingId + 3; ++i) {
        now += 0.001;
        last = wrap.beginPing(now);
        CHECK(last >= 1 && last <= kMaxPingId);
        wrap.onPong(last, now);
    }
    CHECK(last == 3);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    LatencyTracker bad;
    CHECK(bad.beginPing(nan) == 0);
    CHECK(!bad.due(nan, 1.0));
    CHECK(!bad.onPong(1, nan));
    bad.expire(nan);
    CHECK(bad.stats().sent == 0);

    LatencyTracker reset;
    reset.onPong(reset.beginPing(1.0), 1.01);
    reset.reset();
    CHECK(!reset.stats().valid && reset.stats().sent == 0 && reset.stats().received == 0);
    CHECK(reset.due(1.0, 5.0));

    LatencyTracker odd(-1.0);
    const std::int32_t id = odd.beginPing(0.0);
    CHECK(odd.onPong(id, 1.9));
    LatencyTracker odd2(0.0);
    const std::int32_t id2 = odd2.beginPing(0.0);
    CHECK(odd2.onPong(id2, 1.9));
}

void testPingWire() {
    std::vector<std::uint8_t> bytes;
    CHECK(encodePing(12345, bytes));
    CHECK(bytes.size() == 20);
    std::vector<OscPacketMessage> decoded;
    CHECK(decodePacket(bytes.data(), bytes.size(), decoded));
    CHECK(decoded.size() == 1 && decoded[0].address == "/nyrex/ping");
    CHECK(decoded[0].values.size() == 1 && decoded[0].values[0].kind == OscValue::Kind::Integer && decoded[0].values[0].integer == 12345);

    std::vector<nvj::OscMessage> seen;
    CHECK(nvj::parseOscPacket(bytes.data(), bytes.size(), seen));
    CHECK(seen.size() == 1 && seen[0].address == "/nyrex/ping");
    CHECK(seen[0].arguments.size() == 1 && seen[0].arguments[0].type == nvj::OscArgument::Type::Int);
    CHECK(near(seen[0].arguments[0].number, 12345.0));

    std::vector<std::uint8_t> refused;
    CHECK(!encodePing(-1, refused));
    CHECK(!encodePing(kMaxPingId + 1, refused));
    CHECK(refused.empty());
    CHECK(encodePing(0, refused));
    CHECK(encodePing(kMaxPingId, refused));
    CHECK(validControlAddress("/nyrex/ping"));
}

void testPongWire() {
    nvj::OscSender sender;
    const std::vector<nvj::OscMessage> answer = sender.pong(8675309);
    const std::vector<unsigned char> bundle = nvj::encodeOscBundle(answer);
    CHECK(bundle.size() == 44);

    NyrexState state;
    NyrexEvents events;
    applyPacket(state, bundle.data(), bundle.size(), events);
    CHECK(events.pongs.size() == 1 && events.pongs[0] == 8675309);
    CHECK(!state.helloSeen);
    CHECK(!events.hello && !events.bye);

    NyrexEvents none;
    OscPacketMessage floatPong;
    floatPong.address = "/nyrex/out/pong";
    floatPong.values.push_back(OscValue::fromNumber(4.0f));
    applyMessage(state, floatPong, none);
    OscPacketMessage twoPong;
    twoPong.address = "/nyrex/out/pong";
    twoPong.values.push_back(OscValue::fromInteger(4));
    twoPong.values.push_back(OscValue::fromInteger(5));
    applyMessage(state, twoPong, none);
    OscPacketMessage negative;
    negative.address = "/nyrex/out/pong";
    negative.values.push_back(OscValue::fromInteger(-4));
    applyMessage(state, negative, none);
    OscPacketMessage empty;
    empty.address = "/nyrex/out/pong";
    applyMessage(state, empty, none);
    OscPacketMessage text;
    text.address = "/nyrex/out/pong";
    text.values.push_back(OscValue::fromText("9"));
    applyMessage(state, text, none);
    CHECK(none.pongs.empty());

    NyrexState incompatible;
    incompatible.schemaSupported = false;
    NyrexEvents events2;
    applyPacket(incompatible, bundle.data(), bundle.size(), events2);
    CHECK(events2.pongs.size() == 1);

    NyrexEvents many;
    for (int i = 0; i < 400; ++i) {
        OscPacketMessage item;
        item.address = "/nyrex/out/pong";
        item.values.push_back(OscValue::fromInteger(i));
        applyMessage(state, item, many);
    }
    CHECK(many.pongs.size() == kMaxEventsPerApply);
}

void testRoundTripThroughRealMessages() {
    LatencyTracker tracker;
    LinkMonitor link;
    link.setListening(true, 0.0);
    nvj::OscSender sender;

    NyrexState state;
    double now = 1.0;
    for (int round = 0; round < 5; ++round) {
        const std::int32_t id = tracker.beginPing(now);
        std::vector<std::uint8_t> ping;
        CHECK(encodePing(id, ping));
        std::vector<nvj::OscMessage> received;
        CHECK(nvj::parseOscPacket(ping.data(), ping.size(), received));
        CHECK(received.size() == 1 && received[0].arguments.size() == 1);
        const int echoed = static_cast<int>(received[0].arguments[0].number);
        const std::vector<unsigned char> answer = nvj::encodeOscBundle(sender.pong(echoed));
        NyrexEvents events;
        applyPacket(state, answer.data(), answer.size(), events);
        now += 0.004 + round * 0.001;
        link.onPacket(now);
        for (const std::int32_t pong : events.pongs) {
            CHECK(tracker.onPong(pong, now));
        }
        now += 1.0;
    }
    const LatencyStats stats = tracker.stats();
    CHECK(stats.received == 5 && stats.lost == 0 && stats.ignored == 0);
    CHECK(near(stats.minMs, 4.0, 1e-3) && near(stats.maxMs, 8.0, 1e-3));
    CHECK(link.stats(now).packets == 5);
}

}

int main() {
    testStatesAndReconnect();
    testByeAndStop();
    testIncompatible();
    testRateAndGaps();
    testBadTimes();
    testLatencyBasics();
    testLatencyStatistics();
    testLatencyWindowAndLoss();
    testLatencyLimitsAndIds();
    testPingWire();
    testPongWire();
    testRoundTripThroughRealMessages();
    return testing::finish();
}

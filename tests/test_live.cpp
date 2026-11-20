#include "core/live_source.hpp"

#include "check.hpp"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

namespace {

bool near(double a, double b) {
    return std::fabs(a - b) < 1.0e-9;
}

void testNames() {
    CHECK(std::string(nvj::liveStateName(nvj::LiveState::Idle)) == "idle");
    CHECK(std::string(nvj::liveStateName(nvj::LiveState::Searching)) == "searching");
    CHECK(std::string(nvj::liveStateName(nvj::LiveState::Connecting)) == "connecting");
    CHECK(std::string(nvj::liveStateName(nvj::LiveState::Live)) == "live");
    CHECK(std::string(nvj::liveStateName(nvj::LiveState::SignalLost)) == "signal lost");
    CHECK(std::string(nvj::liveStateName(nvj::LiveState::Reconnecting)) == "reconnecting");
}

void testBackoff() {
    nvj::ReconnectPolicy policy;
    CHECK(near(policy.delayFor(1), 0.5));
    CHECK(near(policy.delayFor(2), 1.0));
    CHECK(near(policy.delayFor(3), 2.0));
    CHECK(near(policy.delayFor(4), 4.0));
    CHECK(near(policy.delayFor(5), 8.0));
    CHECK(near(policy.delayFor(6), 10.0));
    CHECK(near(policy.delayFor(60), 10.0));
    CHECK(near(policy.delayFor(0), 0.5));
    CHECK(near(policy.delayFor(-5), 0.5));
    nvj::ReconnectPolicy odd;
    odd.firstDelay = -1.0;
    odd.growth = 0.1;
    odd.maxDelay = 0.0;
    CHECK(odd.delayFor(3) > 0.0);
    CHECK(odd.delayFor(1000) <= 0.02);
}

void testStartAndStop() {
    nvj::LiveStateMachine machine;
    CHECK(machine.state() == nvj::LiveState::Idle);
    CHECK(!machine.attemptDue(100.0));
    machine.beginAttempt(1.0);
    machine.frame(1.0);
    machine.attemptFailed(1.0);
    machine.connectionLost(1.0);
    CHECK(machine.state() == nvj::LiveState::Idle);
    CHECK(machine.update(50.0) == nvj::LiveStateMachine::Action::None);
    machine.start(10.0);
    CHECK(machine.state() == nvj::LiveState::Searching);
    CHECK(machine.attemptDue(10.0));
    CHECK(machine.attempts() == 0);
    machine.stop();
    CHECK(machine.state() == nvj::LiveState::Idle);
    CHECK(!machine.attemptDue(100.0));
}

void testFirstConnection() {
    nvj::LiveStateMachine machine;
    machine.start(0.0);
    CHECK(machine.attemptDue(0.0));
    machine.beginAttempt(0.0);
    CHECK(machine.state() == nvj::LiveState::Connecting);
    CHECK(machine.attemptInFlight());
    CHECK(!machine.attemptDue(0.1));
    machine.attemptSucceeded(0.2);
    CHECK(!machine.attemptInFlight());
    CHECK(machine.state() == nvj::LiveState::Connecting);
    CHECK(!machine.attemptDue(5.0));
    machine.frame(0.3);
    CHECK(machine.state() == nvj::LiveState::Live);
    CHECK(machine.everLive());
    CHECK(machine.attempts() == 0);
    CHECK(machine.update(0.5) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::Live);
}

void testNothingFoundKeepsSearching() {
    nvj::LiveStateMachine machine;
    machine.start(0.0);
    double now = 0.0;
    std::vector<double> waits;
    for (int i = 0; i < 7; ++i) {
        CHECK(machine.attemptDue(now));
        machine.beginAttempt(now);
        machine.attemptFailed(now);
        CHECK(machine.state() == nvj::LiveState::Searching || machine.state() == nvj::LiveState::Reconnecting);
        CHECK(!machine.everLive());
        const double wait = machine.secondsUntilAttempt(now);
        waits.push_back(wait);
        CHECK(!machine.attemptDue(now + wait - 0.01));
        now += wait;
    }
    CHECK(near(waits[0], 0.5));
    CHECK(near(waits[1], 1.0));
    CHECK(near(waits[2], 2.0));
    CHECK(near(waits[3], 4.0));
    CHECK(near(waits[4], 8.0));
    CHECK(near(waits[5], 10.0));
    CHECK(near(waits[6], 10.0));
    CHECK(machine.attempts() == 7);
}

void testSignalLostAndRecovery() {
    nvj::LiveStateMachine machine;
    machine.start(0.0);
    machine.beginAttempt(0.0);
    machine.attemptSucceeded(0.0);
    machine.frame(0.1);
    machine.frame(0.2);
    CHECK(machine.update(1.0) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::Live);
    CHECK(machine.update(2.0) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::SignalLost);
    CHECK(machine.secondsSinceFrame(2.0) > 1.7);
    CHECK(machine.update(3.0) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::SignalLost);
    machine.frame(3.1);
    CHECK(machine.state() == nvj::LiveState::Live);
    CHECK(machine.update(3.2) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::Live);
}

void testSignalLostThenReset() {
    nvj::LiveStateMachine machine;
    machine.start(0.0);
    machine.beginAttempt(0.0);
    machine.attemptSucceeded(0.0);
    machine.frame(0.1);
    CHECK(machine.update(2.0) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::SignalLost);
    CHECK(machine.update(4.9) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.update(5.1) == nvj::LiveStateMachine::Action::Reset);
    CHECK(machine.state() == nvj::LiveState::Reconnecting);
    CHECK(!machine.attemptDue(5.1));
    CHECK(near(machine.secondsUntilAttempt(5.1), 0.5));
    CHECK(machine.attemptDue(5.7));
    machine.beginAttempt(5.7);
    CHECK(machine.state() == nvj::LiveState::Reconnecting);
    CHECK(machine.attempts() == 1);
    machine.attemptSucceeded(5.8);
    machine.frame(5.9);
    CHECK(machine.state() == nvj::LiveState::Live);
    CHECK(machine.attempts() == 0);
}

void testConnectedWithoutFrames() {
    nvj::LiveStateMachine machine;
    machine.start(0.0);
    machine.beginAttempt(0.0);
    machine.attemptSucceeded(0.1);
    CHECK(machine.update(3.0) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::Connecting);
    CHECK(machine.update(7.0) == nvj::LiveStateMachine::Action::Reset);
    CHECK(machine.state() == nvj::LiveState::Searching);
    CHECK(!machine.everLive());
    CHECK(machine.attemptDue(8.0));
}

void testDisconnectWhileLive() {
    nvj::LiveStateMachine machine;
    machine.start(0.0);
    machine.beginAttempt(0.0);
    machine.attemptSucceeded(0.0);
    machine.frame(0.1);
    machine.connectionLost(0.5);
    CHECK(machine.state() == nvj::LiveState::Reconnecting);
    CHECK(near(machine.secondsUntilAttempt(0.5), 0.5));
    machine.beginAttempt(1.0);
    machine.attemptFailed(1.0);
    CHECK(machine.state() == nvj::LiveState::Reconnecting);
    CHECK(near(machine.secondsUntilAttempt(1.0), 0.5));
    machine.beginAttempt(1.5);
    machine.attemptFailed(1.5);
    CHECK(near(machine.secondsUntilAttempt(1.5), 1.0));
    machine.beginAttempt(2.5);
    machine.attemptSucceeded(2.6);
    machine.frame(2.7);
    CHECK(machine.state() == nvj::LiveState::Live);
    machine.connectionLost(3.0);
    CHECK(near(machine.secondsUntilAttempt(3.0), 0.5));
}

void testClockGoingBackwards() {
    nvj::LiveStateMachine machine;
    machine.start(100.0);
    machine.beginAttempt(100.0);
    machine.attemptSucceeded(100.0);
    machine.frame(100.1);
    CHECK(machine.update(50.0) == nvj::LiveStateMachine::Action::None);
    CHECK(machine.state() == nvj::LiveState::Live);
    CHECK(machine.secondsSinceFrame(50.0) == 0.0);
    CHECK(machine.secondsUntilAttempt(0.0) == 0.0);
}

nvj::LiveFrame makeFrame(unsigned long long id, int width, int height, uint8_t fill) {
    nvj::LiveFrame frame;
    frame.format = nvj::LiveFormat::Uyvy;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 2;
    frame.id = id;
    frame.data.assign(static_cast<size_t>(frame.stride) * static_cast<size_t>(height), fill);
    return frame;
}

void testFrameBoxNewestOnly() {
    nvj::LiveFrameBox box;
    nvj::LiveFrame out;
    CHECK(!box.take(out));
    nvj::LiveFrame first = makeFrame(1, 4, 2, 10);
    box.publish(first);
    nvj::LiveFrame second = makeFrame(2, 4, 2, 20);
    box.publish(second);
    CHECK(box.published() == 2);
    CHECK(box.dropped() == 1);
    CHECK(box.take(out));
    CHECK(out.id == 2);
    CHECK(out.data.size() == 16);
    CHECK(out.data[0] == 20);
    CHECK(!box.take(out));
    nvj::LiveFrame third = makeFrame(3, 4, 2, 30);
    box.publish(third);
    CHECK(box.dropped() == 1);
    CHECK(box.take(out));
    CHECK(out.id == 3);
    box.reset();
    CHECK(box.published() == 0);
    CHECK(box.dropped() == 0);
    CHECK(!box.take(out));
}

void testFrameBoxRecyclesBuffers() {
    nvj::LiveFrameBox box;
    nvj::LiveFrame spare = makeFrame(1, 64, 64, 1);
    const uint8_t* original = spare.data.data();
    box.publish(spare);
    nvj::LiveFrame out;
    CHECK(box.take(out));
    CHECK(out.data.data() == original);
    box.publish(out);
    CHECK(out.data.empty() || out.data.data() != original);
}

void testFrameBoxThreads() {
    nvj::LiveFrameBox box;
    std::atomic<bool> stop{false};
    std::atomic<unsigned long long> lastSeen{0};
    std::atomic<bool> ordered{true};
    std::atomic<bool> intact{true};
    std::thread consumer([&]() {
        nvj::LiveFrame out;
        unsigned long long previous = 0;
        while (!stop.load()) {
            if (box.take(out)) {
                if (out.id <= previous) {
                    ordered = false;
                }
                previous = out.id;
                lastSeen = out.id;
                const uint8_t expected = static_cast<uint8_t>(out.id & 0xff);
                for (uint8_t value : out.data) {
                    if (value != expected) {
                        intact = false;
                        break;
                    }
                }
            }
        }
        if (box.take(out)) {
            lastSeen = out.id;
        }
    });
    nvj::LiveFrame frame;
    const unsigned long long total = 3000;
    for (unsigned long long id = 1; id <= total; ++id) {
        frame.format = nvj::LiveFormat::Uyvy;
        frame.width = 32;
        frame.height = 8;
        frame.stride = 64;
        frame.id = id;
        frame.data.assign(512, static_cast<uint8_t>(id & 0xff));
        box.publish(frame);
        if (id % 64 == 0) {
            std::this_thread::yield();
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop = true;
    consumer.join();
    CHECK(ordered.load());
    CHECK(intact.load());
    CHECK(lastSeen.load() == total);
    CHECK(box.published() == total);
}

void testSteadyClock() {
    const double a = nvj::steadySeconds();
    const double b = nvj::steadySeconds();
    CHECK(b >= a);
}

}

int main() {
    testNames();
    testBackoff();
    testStartAndStop();
    testFirstConnection();
    testNothingFoundKeepsSearching();
    testSignalLostAndRecovery();
    testSignalLostThenReset();
    testConnectedWithoutFrames();
    testDisconnectWhileLive();
    testClockGoingBackwards();
    testFrameBoxNewestOnly();
    testFrameBoxRecyclesBuffers();
    testFrameBoxThreads();
    testSteadyClock();
    return testing::finish();
}

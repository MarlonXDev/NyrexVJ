#include "core/live_source.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace nvj {

const char* liveStateName(LiveState state) {
    switch (state) {
    case LiveState::Searching:
        return "searching";
    case LiveState::Connecting:
        return "connecting";
    case LiveState::Live:
        return "live";
    case LiveState::SignalLost:
        return "signal lost";
    case LiveState::Reconnecting:
        return "reconnecting";
    default:
        return "idle";
    }
}

double ReconnectPolicy::delayFor(int attempt) const {
    const double base = std::max(firstDelay, 0.01);
    const double factor = std::max(growth, 1.0);
    const double limit = std::max(maxDelay, base);
    const int steps = std::clamp(attempt - 1, 0, 30);
    return std::min(base * std::pow(factor, static_cast<double>(steps)), limit);
}

LiveStateMachine::LiveStateMachine(const ReconnectPolicy& policy) : policy_(policy) {}

void LiveStateMachine::start(double now) {
    state_ = LiveState::Searching;
    attempts_ = 0;
    everLive_ = false;
    inFlight_ = false;
    connected_ = false;
    nextAttempt_ = now;
    attemptStart_ = now;
    lastFrame_ = now;
    lostSince_ = now;
}

void LiveStateMachine::stop() {
    state_ = LiveState::Idle;
    attempts_ = 0;
    everLive_ = false;
    inFlight_ = false;
    connected_ = false;
}

bool LiveStateMachine::attemptDue(double now) const {
    const bool waiting = state_ == LiveState::Searching || state_ == LiveState::Reconnecting;
    return waiting && !inFlight_ && !connected_ && now >= nextAttempt_;
}

void LiveStateMachine::beginAttempt(double now) {
    if (state_ == LiveState::Idle) {
        return;
    }
    inFlight_ = true;
    connected_ = false;
    attemptStart_ = now;
    ++attempts_;
    state_ = everLive_ || attempts_ > 1 ? LiveState::Reconnecting : LiveState::Connecting;
}

void LiveStateMachine::attemptFailed(double now) {
    if (state_ == LiveState::Idle) {
        return;
    }
    inFlight_ = false;
    connected_ = false;
    nextAttempt_ = now + policy_.delayFor(std::max(attempts_, 1));
    state_ = everLive_ ? LiveState::Reconnecting : LiveState::Searching;
}

void LiveStateMachine::attemptSucceeded(double now) {
    if (state_ == LiveState::Idle) {
        return;
    }
    inFlight_ = false;
    connected_ = true;
    attemptStart_ = now;
    lastFrame_ = now;
}

void LiveStateMachine::frame(double now) {
    if (state_ == LiveState::Idle) {
        return;
    }
    state_ = LiveState::Live;
    everLive_ = true;
    connected_ = true;
    inFlight_ = false;
    attempts_ = 0;
    lastFrame_ = now;
}

void LiveStateMachine::connectionLost(double now) {
    if (state_ == LiveState::Idle) {
        return;
    }
    inFlight_ = false;
    connected_ = false;
    nextAttempt_ = now + policy_.delayFor(std::max(attempts_, 1));
    state_ = everLive_ ? LiveState::Reconnecting : LiveState::Searching;
}

LiveStateMachine::Action LiveStateMachine::update(double now) {
    if (state_ == LiveState::Live && now - lastFrame_ > policy_.lostAfter) {
        state_ = LiveState::SignalLost;
        lostSince_ = now;
        return Action::None;
    }
    if (state_ == LiveState::SignalLost && now - lostSince_ > policy_.reconnectAfter) {
        connectionLost(now);
        return Action::Reset;
    }
    const bool starting = state_ == LiveState::Connecting || state_ == LiveState::Reconnecting;
    if (starting && connected_ && now - attemptStart_ > policy_.connectTimeout) {
        connectionLost(now);
        return Action::Reset;
    }
    return Action::None;
}

double LiveStateMachine::secondsUntilAttempt(double now) const {
    if (state_ != LiveState::Searching && state_ != LiveState::Reconnecting) {
        return 0.0;
    }
    return std::max(nextAttempt_ - now, 0.0);
}

double LiveStateMachine::secondsSinceFrame(double now) const {
    return std::max(now - lastFrame_, 0.0);
}

void LiveFrameBox::publish(LiveFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fresh_) {
        ++dropped_;
    }
    std::swap(latest_, frame);
    fresh_ = true;
    ++published_;
}

bool LiveFrameBox::take(LiveFrame& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fresh_) {
        return false;
    }
    std::swap(out, latest_);
    fresh_ = false;
    return true;
}

unsigned long long LiveFrameBox::published() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

unsigned long long LiveFrameBox::dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

void LiveFrameBox::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    fresh_ = false;
    published_ = 0;
    dropped_ = 0;
    latest_ = LiveFrame{};
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

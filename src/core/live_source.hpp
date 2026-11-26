#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace nvj {

enum class LiveState { Idle, Searching, Connecting, Live, SignalLost, Reconnecting };

const char* liveStateName(LiveState state);

struct ReconnectPolicy {
    double firstDelay = 0.5;
    double growth = 2.0;
    double maxDelay = 10.0;
    double lostAfter = 1.5;
    double reconnectAfter = 3.0;
    double connectTimeout = 6.0;

    double delayFor(int attempt) const;
};

class LiveStateMachine {
public:
    enum class Action { None, Reset };

    explicit LiveStateMachine(const ReconnectPolicy& policy = ReconnectPolicy());

    void start(double now);
    void stop();
    bool attemptDue(double now) const;
    void beginAttempt(double now);
    void attemptFailed(double now);
    void attemptSucceeded(double now);
    void frame(double now);
    void connectionLost(double now);
    Action update(double now);

    LiveState state() const { return state_; }
    int attempts() const { return attempts_; }
    bool everLive() const { return everLive_; }
    bool attemptInFlight() const { return inFlight_; }
    double secondsUntilAttempt(double now) const;
    double secondsSinceFrame(double now) const;

private:
    ReconnectPolicy policy_;
    LiveState state_ = LiveState::Idle;
    int attempts_ = 0;
    bool everLive_ = false;
    bool inFlight_ = false;
    bool connected_ = false;
    double nextAttempt_ = 0.0;
    double attemptStart_ = 0.0;
    double lastFrame_ = 0.0;
    double lostSince_ = 0.0;
};

enum class LiveFormat { None, Uyvy, Rgba };

struct LiveFrame {
    LiveFormat format = LiveFormat::None;
    int width = 0;
    int height = 0;
    int stride = 0;
    unsigned long long id = 0;
    double capturedAt = 0.0;
    std::vector<uint8_t> data;
};

class LiveFrameBox {
public:
    void publish(LiveFrame& frame);
    bool take(LiveFrame& out);
    unsigned long long published() const;
    unsigned long long dropped() const;
    void reset();

private:
    mutable std::mutex mutex_;
    LiveFrame latest_;
    bool fresh_ = false;
    unsigned long long published_ = 0;
    unsigned long long dropped_ = 0;
};

struct LiveStatus {
    LiveState state = LiveState::Idle;
    std::string source;
    std::string error;
    int width = 0;
    int height = 0;
    int connections = 0;
    double fps = 0.0;
    unsigned long long frames = 0;
    unsigned long long dropped = 0;
    unsigned long long unsupported = 0;
    int attempts = 0;
    double jitterMs = 0.0;
    unsigned long long resyncs = 0;
    std::string detail;
};

class LiveFrameObserver {
public:
    virtual ~LiveFrameObserver() = default;
    virtual void offer(const LiveFrame& frame) = 0;
};

class LiveSource {
public:
    virtual ~LiveSource() = default;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual bool take(LiveFrame& out) = 0;
    virtual LiveStatus status() const = 0;
};

double steadySeconds();

}

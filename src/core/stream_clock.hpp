#pragma once

namespace nvj {
namespace stream {

class LagTracker {
public:
    explicit LagTracker(double limitSeconds = 1.0, double holdSeconds = 0.5, double leakPerSecond = 0.001);

    void reset();
    bool observe(double wall, double stream);
    double lagSeconds() const { return lag_; }
    unsigned long long resyncs() const { return resyncs_; }

private:
    double limit_;
    double hold_;
    double leak_;
    bool started_ = false;
    double base_ = 0.0;
    double lastWall_ = 0.0;
    double lastStream_ = 0.0;
    double overSince_ = -1.0;
    double lag_ = 0.0;
    unsigned long long resyncs_ = 0;
};

class JitterMeter {
public:
    void reset();
    void observe(double wall, double stream);
    double jitterSeconds() const { return jitter_; }

private:
    bool started_ = false;
    double lastWall_ = 0.0;
    double lastStream_ = 0.0;
    double jitter_ = 0.0;
};

}
}

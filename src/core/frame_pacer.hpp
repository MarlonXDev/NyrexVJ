#pragma once

#include <array>

namespace nvj {

constexpr std::array<int, 6> kFpsLimits = {30, 60, 90, 120, 144, 0};
constexpr int kDefaultFpsLimit = 60;

int clampFpsLimit(int fps);
int fpsLimitIndex(int fps);

class FramePacer {
public:
    void setTargetFps(int fps);
    int targetFps() const { return fps_; }
    bool limited() const { return fps_ > 0; }
    double period() const { return period_; }
    double nextDeadline(double now);
    void reset();

private:
    int fps_ = 0;
    double period_ = 0.0;
    double deadline_ = 0.0;
    bool started_ = false;
};

}

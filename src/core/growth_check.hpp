#pragma once

#include <vector>

namespace nvj {

struct GrowthLimits {
    double absolute = 0.0;
    double relative = 0.0;
};

struct GrowthResult {
    bool enough = false;
    double first = 0.0;
    double last = 0.0;
    double growth = 0.0;
    double limit = 0.0;
    bool leak = false;
};

constexpr size_t kMinGrowthSamples = 6;

double medianOf(std::vector<double> values);
GrowthResult assessGrowth(const std::vector<double>& values, double warmupFraction, const GrowthLimits& limits);

}

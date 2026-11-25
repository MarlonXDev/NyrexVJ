#include "core/growth_check.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

double medianOf(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) * 0.5;
}

GrowthResult assessGrowth(const std::vector<double>& values, double warmupFraction, const GrowthLimits& limits) {
    GrowthResult result;
    const double fraction = std::isfinite(warmupFraction) ? std::clamp(warmupFraction, 0.0, 0.9) : 0.0;
    const size_t skip = static_cast<size_t>(std::floor(static_cast<double>(values.size()) * fraction));
    if (values.size() < skip + kMinGrowthSamples) {
        return result;
    }
    const std::vector<double> window(values.begin() + static_cast<std::ptrdiff_t>(skip), values.end());
    const size_t third = window.size() / 3;
    result.enough = true;
    result.first = medianOf(std::vector<double>(window.begin(), window.begin() + static_cast<std::ptrdiff_t>(third)));
    result.last = medianOf(std::vector<double>(window.end() - static_cast<std::ptrdiff_t>(third), window.end()));
    result.growth = result.last - result.first;
    result.limit = std::max(limits.absolute, limits.relative * std::fabs(result.first));
    result.leak = result.growth > result.limit;
    return result;
}

}

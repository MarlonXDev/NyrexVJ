#include "core/contrast.hpp"

#include <algorithm>
#include <cmath>

namespace nvj::contrast {

namespace {

double linear(float channel) {
    const double value = std::clamp(static_cast<double>(channel), 0.0, 1.0);
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

}

double luminance(const Rgb& colour) {
    return 0.2126 * linear(colour.r) + 0.7152 * linear(colour.g) + 0.0722 * linear(colour.b);
}

double ratio(const Rgb& first, const Rgb& second) {
    const double a = luminance(first);
    const double b = luminance(second);
    const double lighter = std::max(a, b);
    const double darker = std::min(a, b);
    return (lighter + 0.05) / (darker + 0.05);
}

Rgb blend(const Rgb& top, float alpha, const Rgb& bottom) {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    Rgb out;
    out.r = top.r * a + bottom.r * (1.0f - a);
    out.g = top.g * a + bottom.g * (1.0f - a);
    out.b = top.b * a + bottom.b * (1.0f - a);
    return out;
}

}

#pragma once

namespace nvj::contrast {

constexpr double kNormalText = 4.5;
constexpr double kEnhancedText = 7.0;
constexpr double kComponent = 3.0;

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

double luminance(const Rgb& colour);
double ratio(const Rgb& first, const Rgb& second);
Rgb blend(const Rgb& top, float alpha, const Rgb& bottom);

}

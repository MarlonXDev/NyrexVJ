#include "composition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace nvj {

namespace {

const char* const kBlendNames[BlendModeCount] = {"Normal", "Add", "Screen", "Multiply"};

}

const char* blendName(int mode) {
    if (mode < 0 || mode >= BlendModeCount) {
        return kBlendNames[0];
    }
    return kBlendNames[mode];
}

int blendFromName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (int i = 0; i < BlendModeCount; ++i) {
        std::string candidate = kBlendNames[i];
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (candidate == lower) {
            return i;
        }
    }
    return BlendNormal;
}

bool ColorGrade::isIdentity() const {
    return std::fabs(hue) < 1e-4f && std::fabs(saturation - 1.0f) < 1e-4f && std::fabs(brightness) < 1e-4f && std::fabs(contrast - 1.0f) < 1e-4f;
}

int resolutionIndexFor(int width, int height) {
    for (int i = 0; i < kResolutionCount; ++i) {
        if (kResolutions[i].width == width && kResolutions[i].height == height) {
            return i;
        }
    }
    return kDefaultResolution;
}

}

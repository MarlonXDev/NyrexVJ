#include "core/composition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace nvj {

namespace {

const char* const kBlendNames[BlendModeCount] = {"Normal", "Add", "Screen", "Multiply"};
const char* const kBandNames[AudioBandCount] = {"Off", "Bass", "Mid", "High", "Level", "Kick"};
const char* const kTargetNames[ModTargetCount] = {"Opacity", "Scale", "Speed", "Hue", "Brightness", "Saturation"};

std::string lowered(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

int indexByName(const char* const* names, int count, const std::string& name, int fallback) {
    const std::string wanted = lowered(name);
    for (int i = 0; i < count; ++i) {
        if (lowered(names[i]) == wanted) {
            return i;
        }
    }
    return fallback;
}

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

const char* audioBandName(int band) {
    if (band < 0 || band >= AudioBandCount) {
        return kBandNames[0];
    }
    return kBandNames[band];
}

int audioBandFromName(const std::string& name) {
    return indexByName(kBandNames, AudioBandCount, name, BandOff);
}

const char* modTargetName(int target) {
    if (target < 0 || target >= ModTargetCount) {
        return kTargetNames[0];
    }
    return kTargetNames[target];
}

int modTargetFromName(const std::string& name) {
    return indexByName(kTargetNames, ModTargetCount, name, TargetOpacity);
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

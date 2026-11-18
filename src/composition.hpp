#pragma once

#include <array>
#include <string>
#include <vector>

namespace nvj {

constexpr int kLayerCount = 4;
constexpr int kColumnCount = 8;
constexpr int kEffectSlots = 4;
constexpr int kParamCount = 4;

enum BlendMode {
    BlendNormal = 0,
    BlendAdd,
    BlendScreen,
    BlendMultiply,
    BlendModeCount
};

struct Resolution {
    const char* label;
    int width;
    int height;
};

constexpr Resolution kResolutions[] = {
    {"960 x 540", 960, 540},
    {"1280 x 720", 1280, 720},
    {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440},
};

constexpr int kResolutionCount = static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0]));
constexpr int kDefaultResolution = 2;

const char* blendName(int mode);
int blendFromName(const std::string& name);

struct ColorGrade {
    float hue = 0.0f;
    float saturation = 1.0f;
    float brightness = 0.0f;
    float contrast = 1.0f;

    bool isIdentity() const;
};

struct EffectSlot {
    int effect = -1;
    bool enabled = true;
    std::array<float, kParamCount> params{0.5f, 0.5f, 0.5f, 0.5f};
};

struct Layer {
    std::array<int, kColumnCount> cells;
    int activeColumn = -1;
    float opacity = 1.0f;
    int blend = BlendNormal;
    bool bypass = false;
    bool solo = false;
    std::array<float, 3> colorA{1.0f, 0.45f, 0.0f};
    std::array<float, 3> colorB{0.0f, 0.8f, 1.0f};
    bool rainbow = false;
    float speed = 1.0f;
    float scale = 1.0f;
    ColorGrade grade;
    std::array<EffectSlot, kEffectSlots> fx;

    Layer() { cells.fill(-1); }
};

struct Master {
    float level = 1.0f;
    ColorGrade grade;
    std::array<EffectSlot, kEffectSlots> fx;
};

struct Composition {
    std::string name = "Untitled";
    float bpm = 128.0f;
    int resolution = kDefaultResolution;
    Master master;
    std::array<Layer, kLayerCount> layers;
};

int resolutionIndexFor(int width, int height);

}

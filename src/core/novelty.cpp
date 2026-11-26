#include "core/novelty.hpp"

#include <cstdio>
#include <cstring>

#ifndef NYREXVJ_VERSION
#define NYREXVJ_VERSION "0.0.0-dev"
#endif

namespace nvj {

namespace {

struct Entry {
    const char* name;
    int major;
    int minor;
};

constexpr Entry kClips[] = {
    {"Neon Sigil", 0, 8},
    {"Neon Lattice", 0, 8},
    {"Neon Corridor", 0, 8},
    {"Chaos Smoke", 0, 8},
    {"Tesseract", 0, 8},
    {"Laser Web", 0, 8},
    {"Mandelbox Fly", 0, 8},
    {"Prism Bloom", 0, 8},
    {"Chrome Orbs Mesh", 0, 9},
    {"Neon Cubes Mesh", 0, 9},
    {"Gold Knot Mesh", 0, 9},
    {"Pulse Columns Mesh", 0, 9},
    {"Spout Input 1", 0, 10},
    {"Spout Input 2", 0, 10},
    {"Spout Input 3", 0, 10},
    {"Spout Input 4", 0, 10},
    {"NDI Input 1", 0, 22},
    {"NDI Input 2", 0, 22},
    {"NDI Input 3", 0, 22},
    {"NDI Input 4", 0, 22},
    {"Camera 1", 0, 22},
    {"Camera 2", 0, 22},
    {"Camera 3", 0, 22},
    {"Camera 4", 0, 22},
    {"Plugin Source 1", 0, 26},
    {"Plugin Source 2", 0, 26},
    {"Plugin Source 3", 0, 26},
    {"Plugin Source 4", 0, 26},
};

constexpr Entry kEffects[] = {
    {"Kaleido Neon", 0, 8},
    {"Mandala Fold", 0, 8},
    {"Hue Feedback", 0, 8},
    {"Star Burst", 0, 8},
    {"Chrome Liquid", 0, 8},
    {"Image Terrain", 0, 8},
    {"Hyperspace", 0, 8},
    {"Shatter", 0, 8},
    {"Prism Glass", 0, 60},
    {"Frosted Glass", 0, 60},
};

constexpr Entry kPresets[] = {
    {"Hyperdrive", 0, 8},
    {"Neon Cathedral", 0, 8},
    {"Chrome Meltdown", 0, 8},
    {"Shatter Storm", 0, 8},
    {"Mandala Machine", 0, 8},
    {"Terrain Flight", 0, 8},
    {"Tesseract Club", 0, 8},
    {"Feedback Galaxy", 0, 8},
};

bool parseMajorMinor(const std::string& version, int& major, int& minor) {
    const char* text = version.c_str();
    if (*text == 'v' || *text == 'V') {
        ++text;
    }
    return std::sscanf(text, "%d.%d", &major, &minor) == 2;
}

template <size_t N>
bool listed(const Entry (&entries)[N], const std::string& name, const std::string& appVersion) {
    int major = 0;
    int minor = 0;
    if (!parseMajorMinor(appVersion, major, minor)) {
        return false;
    }
    for (const Entry& entry : entries) {
        if (name == entry.name) {
            return entry.major == major && entry.minor == minor;
        }
    }
    return false;
}

}

bool isNewClipIn(const std::string& name, const std::string& appVersion) {
    return listed(kClips, name, appVersion);
}

bool isNewEffectIn(const std::string& name, const std::string& appVersion) {
    return listed(kEffects, name, appVersion);
}

bool isNewPresetIn(const std::string& name, const std::string& appVersion) {
    return listed(kPresets, name, appVersion);
}

bool isNewClip(const std::string& name) {
    return isNewClipIn(name, NYREXVJ_VERSION);
}

bool isNewEffect(const std::string& name) {
    return isNewEffectIn(name, NYREXVJ_VERSION);
}

bool isNewPreset(const std::string& name) {
    return isNewPresetIn(name, NYREXVJ_VERSION);
}

}

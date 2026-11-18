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
    BlendSubtract,
    BlendDifference,
    BlendExclusion,
    BlendOverlay,
    BlendSoftLight,
    BlendHardLight,
    BlendLighten,
    BlendDarken,
    BlendColorDodge,
    BlendLinearBurn,
    BlendModeCount
};

enum AudioBand {
    BandOff = 0,
    BandBass,
    BandMid,
    BandHigh,
    BandLevel,
    BandKick,
    AudioBandCount
};

enum ModTarget {
    TargetOpacity = 0,
    TargetScale,
    TargetSpeed,
    TargetHue,
    TargetBrightness,
    TargetSaturation,
    ModTargetCount
};

constexpr int kModSlots = 2;
constexpr int kDeckCount = 16;

enum TransitionType {
    TransitionFade = 0,
    TransitionDissolve,
    TransitionZoom,
    TransitionWipe,
    TransitionRadial,
    TransitionSpin,
    TransitionSlide,
    TransitionTypeCount
};

enum EffectReactMode {
    ReactParameter = 0,
    ReactGate,
    EffectReactModeCount
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
const char* audioBandName(int band);
int audioBandFromName(const std::string& name);
const char* modTargetName(int target);
int modTargetFromName(const std::string& name);
const char* transitionName(int type);
int transitionFromName(const std::string& name);
const char* reactModeName(int mode);
int reactModeFromName(const std::string& name);

struct AudioMod {
    int band = BandOff;
    int target = TargetOpacity;
    float amount = 0.5f;
};

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
    int reactBand = BandOff;
    int reactParam = 0;
    float reactAmount = 0.5f;
    int reactMode = ReactParameter;
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
    std::array<AudioMod, kModSlots> mods;
    std::array<EffectSlot, kEffectSlots> fx;

    Layer() { cells.fill(-1); }
};

struct Master {
    float level = 1.0f;
    ColorGrade grade;
    std::array<AudioMod, kModSlots> mods;
    std::array<EffectSlot, kEffectSlots> fx;
};

struct DeckData {
    Master master;
    std::array<Layer, kLayerCount> layers;
};

struct DeckSettings {
    int transition = TransitionFade;
    float seconds = 1.5f;
    int beats = 4;
    bool syncBeats = false;
    bool autoCycle = false;
    int cycleBeats = 16;
};

struct Composition {
    std::string name = "Untitled";
    float bpm = 128.0f;
    int resolution = kDefaultResolution;
    Master master;
    std::array<Layer, kLayerCount> layers;
    int activeDeck = 0;
    std::array<bool, kDeckCount> deckUsed{true};
    std::array<std::string, kDeckCount> deckNames;
    std::array<DeckData, kDeckCount> decks;
    DeckSettings deckSettings;
};

DeckData captureDeck(const Composition& comp);
void restoreDeck(Composition& comp, const DeckData& deck);
DeckData emptyDeck();
bool selectDeck(Composition& comp, int index);
void normalizeDecks(Composition& comp);
std::string deckDisplayName(const Composition& comp, int index);
int nextUsedDeck(const Composition& comp, int from, int direction);
int usedDeckCount(const Composition& comp);
float transitionSeconds(const DeckSettings& settings, float bpm);

int resolutionIndexFor(int width, int height);

}

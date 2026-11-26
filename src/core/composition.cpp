#include "core/composition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace nvj {

namespace {

const char* const kBlendNames[BlendModeCount] = {"Normal", "Add", "Screen", "Multiply", "Subtract", "Difference", "Exclusion", "Overlay", "Soft Light", "Hard Light", "Lighten", "Darken", "Color Dodge", "Linear Burn"};
const char* const kBandNames[AudioBandCount] = {"Off", "Bass", "Mid", "High", "Level", "Kick", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8", "Beat", "Bar", "TC second", "TC minute", "Plugin 1", "Plugin 2", "Plugin 3", "Plugin 4", "Plugin 5", "Plugin 6", "Plugin 7", "Plugin 8", "Person present", "Person X", "Person Y", "Person size", "Person motion", "Person count"};
const char* const kLfoShapeNames[LfoShapeCount] = {"Sine", "Triangle", "Saw", "Square", "Random", "Smooth random"};
const char* const kLfoRateNames[kLfoRateCount] = {"1/4 beat", "1/2 beat", "1 beat", "2 beats", "4 beats", "8 beats", "16 beats", "32 beats"};
const char* const kTargetNames[ModTargetCount] = {"Opacity", "Scale", "Speed", "Hue", "Brightness", "Saturation"};
const char* const kTransitionNames[TransitionTypeCount] = {"Fade", "Dissolve", "Zoom", "Wipe", "Radial", "Spin", "Slide", "Pixelate", "Glitch", "Blinds", "Ripple", "Flash", "Blur", "Checker", "Frost"};
const char* const kReactModeNames[EffectReactModeCount] = {"Parameter", "Gate"};

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

const char* lfoShapeName(int shape) {
    if (shape < 0 || shape >= LfoShapeCount) {
        return kLfoShapeNames[0];
    }
    return kLfoShapeNames[shape];
}

int lfoShapeFromName(const std::string& name) {
    return indexByName(kLfoShapeNames, LfoShapeCount, name, LfoSine);
}

const char* lfoRateName(int rate) {
    if (rate < 0 || rate >= kLfoRateCount) {
        return kLfoRateNames[4];
    }
    return kLfoRateNames[rate];
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

const char* transitionName(int type) {
    if (type < 0 || type >= TransitionTypeCount) {
        return kTransitionNames[0];
    }
    return kTransitionNames[type];
}

int transitionFromName(const std::string& name) {
    return indexByName(kTransitionNames, TransitionTypeCount, name, TransitionFade);
}

const char* reactModeName(int mode) {
    if (mode < 0 || mode >= EffectReactModeCount) {
        return kReactModeNames[0];
    }
    return kReactModeNames[mode];
}

int reactModeFromName(const std::string& name) {
    return indexByName(kReactModeNames, EffectReactModeCount, name, ReactParameter);
}

DeckData captureDeck(const Composition& comp) {
    DeckData deck;
    deck.master = comp.master;
    deck.layers = comp.layers;
    return deck;
}

void restoreDeck(Composition& comp, const DeckData& deck) {
    const float level = comp.master.level;
    comp.master = deck.master;
    comp.master.level = level;
    comp.layers = deck.layers;
}

DeckData emptyDeck() {
    return DeckData{};
}

void normalizeDecks(Composition& comp) {
    comp.activeDeck = std::clamp(comp.activeDeck, 0, kDeckCount - 1);
    comp.deckUsed[static_cast<size_t>(comp.activeDeck)] = true;
    comp.deckSettings.transition = std::clamp(comp.deckSettings.transition, 0, TransitionTypeCount - 1);
    comp.deckSettings.seconds = std::clamp(comp.deckSettings.seconds, 0.0f, 10.0f);
    comp.deckSettings.beats = std::clamp(comp.deckSettings.beats, 1, 32);
    comp.deckSettings.cycleBeats = std::clamp(comp.deckSettings.cycleBeats, 4, 128);
}

bool selectDeck(Composition& comp, int index) {
    if (index < 0 || index >= kDeckCount || index == comp.activeDeck) {
        return false;
    }
    const size_t from = static_cast<size_t>(comp.activeDeck);
    const size_t to = static_cast<size_t>(index);
    comp.decks[from] = captureDeck(comp);
    comp.deckUsed[from] = true;
    if (!comp.deckUsed[to]) {
        comp.decks[to] = emptyDeck();
        comp.deckUsed[to] = true;
    }
    restoreDeck(comp, comp.decks[to]);
    comp.activeDeck = index;
    return true;
}

std::string deckDisplayName(const Composition& comp, int index) {
    if (index < 0 || index >= kDeckCount) {
        return std::string();
    }
    const std::string& name = comp.deckNames[static_cast<size_t>(index)];
    if (!name.empty()) {
        return name;
    }
    return "Deck " + std::to_string(index + 1);
}

int nextUsedDeck(const Composition& comp, int from, int direction) {
    const int step = direction >= 0 ? 1 : -1;
    for (int i = 1; i <= kDeckCount; ++i) {
        const int candidate = ((from + step * i) % kDeckCount + kDeckCount) % kDeckCount;
        if (comp.deckUsed[static_cast<size_t>(candidate)]) {
            return candidate;
        }
    }
    return from;
}

int usedDeckCount(const Composition& comp) {
    int count = 0;
    for (bool used : comp.deckUsed) {
        count += used ? 1 : 0;
    }
    return count;
}

float transitionSeconds(const DeckSettings& settings, float bpm) {
    if (settings.syncBeats) {
        return static_cast<float>(settings.beats) * 60.0f / std::max(bpm, 20.0f);
    }
    return settings.seconds;
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

int replaceClipEverywhere(Composition& comp, int from, int to, bool keepPlayback) {
    if (from < 0 || to < 0 || from == to) {
        return 0;
    }
    auto rewrite = [&](std::array<Layer, kLayerCount>& layers) {
        int changed = 0;
        for (Layer& layer : layers) {
            for (size_t c = 0; c < kColumnCount; ++c) {
                if (layer.cells[c] != from) {
                    continue;
                }
                if (keepPlayback) {
                    layer.cells[c] = to;
                } else {
                    layer.setCell(c, to);
                }
                ++changed;
            }
        }
        return changed;
    };
    int total = rewrite(comp.layers);
    for (size_t d = 0; d < kDeckCount; ++d) {
        const int inDeck = rewrite(comp.decks[d].layers);
        if (static_cast<int>(d) != comp.activeDeck) {
            total += inDeck;
        }
    }
    return total;
}

}

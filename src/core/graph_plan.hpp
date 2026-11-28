#pragma once

#include "core/composition.hpp"

#include <array>

namespace nvj {

enum class DeckRoute {
    Final,
    Land,
    Direct,
    ViaScratch
};

bool finalIsNeutral(const ColorGrade& grade, float level, bool blackout, bool hold);
bool chainIsEmpty(const std::array<EffectSlot, kEffectSlots>& slots, int effectCount);
bool resultStaysInFirst(int blendSwaps);
DeckRoute planDeckRoute(bool optimizer, bool neutral, bool emptyMaster, bool colour);

struct GraphStats {
    unsigned long long frames = 0;
    unsigned long long finalSkipped = 0;
    unsigned long long copiesSkipped = 0;
};

}

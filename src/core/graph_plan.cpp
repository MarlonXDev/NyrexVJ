#include "core/graph_plan.hpp"

namespace nvj {

bool finalIsNeutral(const ColorGrade& grade, float level, bool blackout, bool hold) {
    return !blackout && !hold && level == 1.0f && grade == ColorGrade{};
}

bool chainIsEmpty(const std::array<EffectSlot, kEffectSlots>& slots, int effectCount) {
    for (const EffectSlot& slot : slots) {
        if (slot.enabled && slot.effect >= 0 && slot.effect < effectCount) {
            return false;
        }
    }
    return true;
}

bool resultStaysInFirst(int blendSwaps) {
    return blendSwaps % 2 == 0;
}

DeckRoute planDeckRoute(bool optimizer, bool neutral, bool emptyMaster, bool colour) {
    if (!optimizer) {
        return DeckRoute::Final;
    }
    const bool plain = neutral && emptyMaster;
    if (colour) {
        return plain ? DeckRoute::Direct : DeckRoute::ViaScratch;
    }
    return plain ? DeckRoute::Land : DeckRoute::Final;
}

}

#pragma once

#include "render/effect_library.hpp"

#include <vector>

namespace nvj {

void appendExtraEffects(std::vector<EffectDef>& fx);
void appendFeedbackEffects(std::vector<EffectDef>& fx);

}

#pragma once

#include "core/composition.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <vector>

namespace nvj {

nlohmann::json effectsToJson(const std::array<EffectSlot, kEffectSlots>& slots);
void effectsFromJson(const nlohmann::json& node, std::array<EffectSlot, kEffectSlots>& slots, std::vector<std::string>* warnings);

}

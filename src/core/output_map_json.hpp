#pragma once

#include "core/output_map.hpp"

#include <nlohmann/json.hpp>

namespace nvj {

nlohmann::json maskToJson(const SliceMask& mask);
bool maskFromJson(const nlohmann::json& doc, SliceMask& mask);
nlohmann::json mappingToJson(const OutputMapping& map);
bool mappingFromJson(const nlohmann::json& doc, OutputMapping& map);
nlohmann::json colorToJson(const OutputColor& color);
bool colorFromJson(const nlohmann::json& doc, OutputColor& color);
nlohmann::json outputToJson(const OutputSpec& spec);
bool outputFromJson(const nlohmann::json& doc, OutputSpec& spec);

}

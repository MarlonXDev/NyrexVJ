#pragma once

#include "core/output_map.hpp"

#include <nlohmann/json.hpp>

namespace nvj {

nlohmann::json outputToJson(const OutputSpec& spec);
bool outputFromJson(const nlohmann::json& doc, OutputSpec& spec);

}

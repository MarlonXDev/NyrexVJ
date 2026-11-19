#pragma once

#include "core/stage.hpp"

#include <nlohmann/json.hpp>

namespace nvj {

nlohmann::json dmxToJson(const DmxConfig& config);
void dmxFromJson(const nlohmann::json& doc, DmxConfig& config);
nlohmann::json vrchatToJson(const VrchatConfig& config);
void vrchatFromJson(const nlohmann::json& doc, VrchatConfig& config);

}

#pragma once

#include "control/control_targets.hpp"

#include <string>

namespace nvj {

std::string targetLabel(const ControlTarget& target);
std::string targetLabelById(const std::string& id);

}

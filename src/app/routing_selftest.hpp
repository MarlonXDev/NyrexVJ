#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runRoutingSelfTest(Engine& engine, int benchFrames, const std::string& outputPath);

}

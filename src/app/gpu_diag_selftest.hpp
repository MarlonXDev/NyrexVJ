#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runGpuDiagSelfTest(Engine& engine, int benchFrames, const std::string& outputPath);

}

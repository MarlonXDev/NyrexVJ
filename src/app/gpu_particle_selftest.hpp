#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runGpuParticleSelfTest(Engine& engine, int benchFrames, const std::string& outputPath);

}

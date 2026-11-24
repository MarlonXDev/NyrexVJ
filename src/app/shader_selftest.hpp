#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runShaderSelfTest(Engine& engine, int benchFrames, const std::string& outputPath);

}

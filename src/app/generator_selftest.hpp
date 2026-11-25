#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runGeneratorSelfTest(Engine& engine, int benchFrames, const std::string& outputPath);

}

#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runShaderEditorSelfTest(Engine& engine, int benchFrames, const std::string& outputPath);

}

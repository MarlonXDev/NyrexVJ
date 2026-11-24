#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runSceneSelfTest(Engine& engine, const std::string& modelDirectory, const std::string& outputPath);

}

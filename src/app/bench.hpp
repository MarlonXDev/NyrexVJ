#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runSceneBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);

}

#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runRecordSelfTest(Engine& engine, const std::string& outputPath);
int runRecordBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);

}

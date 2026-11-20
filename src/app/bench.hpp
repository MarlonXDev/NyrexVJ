#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runSceneBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);
int runVideoSelfTest(Engine& engine, const std::string& directory, const std::string& outputPath);
int runVideoBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);
int runCompositionBenchmark(Engine& engine, int frames, double budgetMs, bool allLooks, const std::string& outputPath);

}

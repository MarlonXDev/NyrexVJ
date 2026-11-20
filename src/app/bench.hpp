#pragma once

#include "render/engine.hpp"

#include <string>

namespace nvj {

int runSceneBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);
int runVideoSelfTest(Engine& engine, const std::string& directory, const std::string& outputPath);
int runVideoBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);
int runLiveSelfTest(Engine& engine, const std::string& outputPath);
int runLiveBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);
int runSwitchBenchmark(Engine& engine, const std::string& spec, const std::string& outputPath);
int runMemoryBenchmark(Engine& engine, const std::string& outputPath);
int runShaderBenchmark(Engine& engine, int frames, const std::string& outputPath);
int runCompositionBenchmark(Engine& engine, int frames, double budgetMs, bool allLooks, const std::string& outputPath);

}

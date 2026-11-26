#pragma once

#include <string>

namespace nvj {

class Engine;

int runFlowSelfTest(Engine& engine, const std::string& clipDirectory, const std::string& outputPath);

}

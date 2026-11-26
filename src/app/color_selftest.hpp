#pragma once

#include <string>

namespace nvj {

class Engine;

int runColorSelfTest(Engine& engine, const std::string& clipDirectory, const std::string& outputPath);

}

#pragma once

#include <string>

namespace nvj {

class App;

int runApiSelfTest(App& app, const std::string& outputPath);
int runApiBenchmark(App& app, const std::string& spec, const std::string& outputPath);

}

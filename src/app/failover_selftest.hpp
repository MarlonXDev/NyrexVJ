#pragma once

#include <string>

namespace nvj {

class App;

int runFailoverSelfTest(App& app, const std::string& outputPath);

}

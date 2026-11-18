#pragma once

#include <string>

namespace nvj {

std::string userDataDir();
void logInit();
void logLine(const std::string& line);

}

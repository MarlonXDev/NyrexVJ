#pragma once

#include <string>

namespace nvj {

std::string findCjkFontPath();
bool mergeCjkFont(const std::string& path);
std::string findSansFontPath();
bool useSansFont(bool enable);

}

#pragma once

#include "render/shader_source.hpp"

#include <cstddef>
#include <string>

namespace nvj {

constexpr size_t kIsfMaxHeaderBytes = 64 * 1024;
constexpr int kIsfMaxDepth = 12;
constexpr int kIsfMaxInputs = 64;
constexpr int kIsfMaxPasses = 16;
constexpr int kIsfMaxImported = 8;
constexpr int kIsfMaxCategories = 8;
constexpr size_t kIsfMaxTextBytes = 512;

struct IsfResult {
    bool isIsf = false;
    bool ok = false;
    std::string error;
    std::string text;
    ShaderInfo info;
};

bool looksLikeIsf(const std::string& text);
IsfResult translateIsf(const std::string& text);

}

#pragma once

#include <cstddef>
#include <string>

namespace nvj {

constexpr size_t kMaxShaderClips = 128;
constexpr unsigned long long kMaxShaderFileBytes = 256ull * 1024ull;

struct ShaderSource {
    bool ok = false;
    bool shadertoy = false;
    std::string body;
    std::string error;
};

bool isShaderFileName(const std::string& name);
std::string shaderClipName(const std::string& stem);
ShaderSource prepareShaderSource(const std::string& text);

}

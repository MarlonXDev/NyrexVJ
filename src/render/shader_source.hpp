#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

constexpr size_t kMaxShaderClips = 128;
constexpr unsigned long long kMaxShaderFileBytes = 256ull * 1024ull;
constexpr int kMaxShaderBuffers = 4;
constexpr int kShaderChannels = 4;
constexpr int kMaxShaderMediaInputs = 4;
constexpr float kMinShaderBufferScale = 0.25f;
constexpr unsigned long long kShaderBufferBudgetBytes = 512ull * 1024ull * 1024ull;

enum class ShaderInputKind {
    None,
    Audio,
    Buffer,
    Media
};

struct ShaderInput {
    ShaderInputKind kind = ShaderInputKind::None;
    int buffer = -1;
    std::string media;
    bool nearest = false;
    bool repeat = false;
};

struct ShaderPassSource {
    bool image = false;
    int buffer = -1;
    int firstLine = 1;
    float scale = 1.0f;
    std::string code;
    std::array<ShaderInput, kShaderChannels> inputs;

    std::string label() const;
};

struct ShaderPlan {
    bool alpha = false;
    bool premultiplied = false;
    int commonLine = 1;
    std::string common;
    std::vector<ShaderPassSource> passes;

    int bufferCount() const;
    std::vector<std::string> mediaNames() const;
};

struct ShaderSource {
    bool ok = false;
    bool shadertoy = false;
    bool advanced = false;
    std::string body;
    std::string error;
    std::shared_ptr<const ShaderPlan> plan;
};

bool isShaderFileName(const std::string& name);
std::string shaderClipName(const std::string& stem);
ShaderSource prepareShaderSource(const std::string& text);
char shaderBufferLetter(int buffer);
float fitShaderScale(float requested, int width, int height, int bytesPerPixel, int copies, unsigned long long available);

}

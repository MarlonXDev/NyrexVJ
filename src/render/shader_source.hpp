#pragma once

#include "core/playhead.hpp"

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
constexpr int kMaxShaderParams = 8;
constexpr int kShaderValueCount = kClipValueCount;
constexpr int kMaxShaderListItems = 16;
constexpr int kShaderFilterControls = 4;
constexpr float kShaderValueUnset = kClipValueUnset;

enum class ShaderInputKind {
    None,
    Audio,
    Buffer,
    Media,
    Picture
};

enum class ShaderParamKind {
    Float,
    Int,
    Bool,
    Point,
    Color,
    List
};

struct ShaderParam {
    std::string name;
    std::string label;
    ShaderParamKind kind = ShaderParamKind::Float;
    int slot = 0;
    int count = 1;
    std::array<float, 4> min{};
    std::array<float, 4> max{};
    std::array<float, 4> def{};
    std::vector<std::string> labels;
    std::vector<float> values;

    bool scalar() const { return kind != ShaderParamKind::Point && kind != ShaderParamKind::Color; }
};

struct ShaderInfo {
    std::string format;
    std::string description;
    std::string credit;
    std::vector<std::string> categories;
    std::vector<std::string> notes;
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
    bool plain = false;
    bool filter = false;
    int commonLine = 1;
    std::string common;
    std::vector<ShaderPassSource> passes;
    std::vector<ShaderParam> params;
    ShaderInfo info;

    int bufferCount() const;
    std::vector<std::string> mediaNames() const;
    int valueCount() const;
    bool usesPicture() const;
};

using ShaderValues = std::array<float, kShaderValueCount>;

ShaderValues unsetShaderValues();
bool shaderValuesUnset(const ShaderValues& values);
float shaderValueClamp(const ShaderParam& param, int component, float value);
ShaderValues resolveShaderValues(const ShaderPlan& plan, const ShaderValues& stored);
std::vector<int> shaderFilterParams(const ShaderPlan& plan);
float shaderParamFromControl(const ShaderParam& param, float control);
float shaderControlFromParam(const ShaderParam& param, float value);
bool isReservedShaderName(const std::string& name);

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

#pragma once

#include "render/gl_util.hpp"
#include "render/shader_source.hpp"

#include <array>
#include <cstddef>

namespace nvj {

constexpr unsigned long long kShaderRestartGapFrames = 3;
constexpr unsigned long long kShaderIdleReleaseFrames = 240;
constexpr double kShaderRestartBackSeconds = 0.25;

struct ShaderPassState {
    int clipIndex = -1;
    int width = 0;
    int height = 0;
    std::array<std::array<RenderTarget, 2>, kMaxShaderBuffers> buffers;
    std::array<int, kMaxShaderBuffers> latest{};
    std::array<float, kMaxShaderBuffers> scale{};
    std::array<bool, kMaxShaderBuffers> used{};
    std::array<RenderTarget, kMaxShaderMediaInputs> media;
    std::array<int, kMaxShaderMediaInputs> mediaClip{};
    unsigned long long frame = 0;
    unsigned long long lastDrawn = 0;
    double lastClock = 0.0;
    bool allocated = false;
    bool restart = false;

    ShaderPassState();
    size_t bytes() const;
    void destroy();
    void clear();
    bool matches(size_t clip, int w, int h) const;
};

}

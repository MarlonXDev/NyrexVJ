#pragma once

#include "core/color_lut.hpp"
#include "render/gl_util.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace nvj {

class ColorStage {
public:
    bool create(std::string* error = nullptr);
    void destroy();
    bool ready() const { return program_ != 0; }

    bool setLut(const CubeLut& lut);
    void clearLut();
    bool hasLut() const { return texture_ != 0 && size_ > 0; }
    int lutSize() const { return size_; }
    std::uint64_t lutBytes() const { return hasLut() ? cubeGpuBytes(size_) : 0; }

    void apply(const RenderTarget& source, const RenderTarget& destination, float strength, const FullscreenTriangle& triangle) const;

private:
    GLuint program_ = 0;
    GLuint texture_ = 0;
    int size_ = 0;
    GLint locTex_ = -1;
    GLint locLut_ = -1;
    GLint locSize_ = -1;
    GLint locDomainMin_ = -1;
    GLint locDomainScale_ = -1;
    GLint locStrength_ = -1;
    std::array<float, 3> domainMin_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainScale_{1.0f, 1.0f, 1.0f};
};

}

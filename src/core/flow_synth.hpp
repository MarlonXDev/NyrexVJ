#pragma once

#include "core/optical_flow.hpp"

#include <cstdint>
#include <vector>

namespace nvj::flow {

constexpr float kConsistencyLow = 0.06f;
constexpr float kConsistencyHigh = 0.25f;
constexpr int kBlurTaps = 9;
constexpr float kVectorRange = 0.06f;

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool valid() const { return width > 0 && height > 0 && rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u; }
};

struct FieldSample {
    float dx = 0.0f;
    float dy = 0.0f;
    float confidence = 0.0f;
};

FieldSample sampleField(const Field& field, float u, float v);
std::vector<float> packField(const Field& field);

void interpolateImage(const Image& a, const Image& b, const Field& field, float t, Image& out);
void motionBlurImage(const Image& a, const Field& field, float shutter, Image& out);
void vectorsImage(const Field& field, int width, int height, Image& out);

}

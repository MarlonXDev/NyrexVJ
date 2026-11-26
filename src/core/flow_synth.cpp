#include "core/flow_synth.hpp"

#include <algorithm>
#include <cmath>

namespace nvj::flow {

namespace {

struct Pixel {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

float smoothStep(float low, float high, float x) {
    const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Pixel fetch(const Image& image, int x, int y) {
    const int cx = std::clamp(x, 0, image.width - 1);
    const int cy = std::clamp(y, 0, image.height - 1);
    const std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(cy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(cx)) * 4u];
    Pixel out;
    out.a = static_cast<float>(p[3]) / 255.0f;
    out.r = static_cast<float>(p[0]) / 255.0f * out.a;
    out.g = static_cast<float>(p[1]) / 255.0f * out.a;
    out.b = static_cast<float>(p[2]) / 255.0f * out.a;
    return out;
}

Pixel sampleBilinear(const Image& image, float u, float v) {
    const float x = u * static_cast<float>(image.width) - 0.5f;
    const float y = v * static_cast<float>(image.height) - 0.5f;
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float tx = x - fx;
    const float ty = y - fy;
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const Pixel p00 = fetch(image, x0, y0);
    const Pixel p10 = fetch(image, x0 + 1, y0);
    const Pixel p01 = fetch(image, x0, y0 + 1);
    const Pixel p11 = fetch(image, x0 + 1, y0 + 1);
    Pixel out;
    out.r = (p00.r * (1.0f - tx) + p10.r * tx) * (1.0f - ty) + (p01.r * (1.0f - tx) + p11.r * tx) * ty;
    out.g = (p00.g * (1.0f - tx) + p10.g * tx) * (1.0f - ty) + (p01.g * (1.0f - tx) + p11.g * tx) * ty;
    out.b = (p00.b * (1.0f - tx) + p10.b * tx) * (1.0f - ty) + (p01.b * (1.0f - tx) + p11.b * tx) * ty;
    out.a = (p00.a * (1.0f - tx) + p10.a * tx) * (1.0f - ty) + (p01.a * (1.0f - tx) + p11.a * tx) * ty;
    return out;
}

Pixel mixPixel(const Pixel& a, const Pixel& b, float t) {
    Pixel out;
    out.r = a.r + (b.r - a.r) * t;
    out.g = a.g + (b.g - a.g) * t;
    out.b = a.b + (b.b - a.b) * t;
    out.a = a.a + (b.a - a.a) * t;
    return out;
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f), 0l, 255l));
}

void store(Image& image, int x, int y, const Pixel& premultiplied) {
    std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4u];
    const float a = std::clamp(premultiplied.a, 0.0f, 1.0f);
    if (a > 1.0e-5f) {
        p[0] = toByte(premultiplied.r / a);
        p[1] = toByte(premultiplied.g / a);
        p[2] = toByte(premultiplied.b / a);
    } else {
        p[0] = 0;
        p[1] = 0;
        p[2] = 0;
    }
    p[3] = toByte(a);
}

void prepare(Image& out, int width, int height) {
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
}

float maxDifference(const Pixel& a, const Pixel& b) {
    return std::max(std::max(std::fabs(a.r - b.r), std::fabs(a.g - b.g)), std::max(std::fabs(a.b - b.b), std::fabs(a.a - b.a)));
}

}

FieldSample sampleField(const Field& field, float u, float v) {
    FieldSample out;
    if (!field.valid()) {
        return out;
    }
    const float gx = std::clamp(u * static_cast<float>(field.imageWidth) / static_cast<float>(field.block) - 0.5f, 0.0f, static_cast<float>(field.cols - 1));
    const float gy = std::clamp(v * static_cast<float>(field.imageHeight) / static_cast<float>(field.block) - 0.5f, 0.0f, static_cast<float>(field.rows - 1));
    const int x0 = static_cast<int>(std::floor(gx));
    const int y0 = static_cast<int>(std::floor(gy));
    const int x1 = std::min(x0 + 1, field.cols - 1);
    const int y1 = std::min(y0 + 1, field.rows - 1);
    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);
    const auto at = [&](const std::vector<float>& data, int x, int y) {
        return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(field.cols) + static_cast<std::size_t>(x)];
    };
    const auto lerp = [&](const std::vector<float>& data) {
        const float top = at(data, x0, y0) * (1.0f - tx) + at(data, x1, y0) * tx;
        const float bottom = at(data, x0, y1) * (1.0f - tx) + at(data, x1, y1) * tx;
        return top * (1.0f - ty) + bottom * ty;
    };
    out.dx = lerp(field.dx) / static_cast<float>(field.imageWidth);
    out.dy = lerp(field.dy) / static_cast<float>(field.imageHeight);
    out.confidence = lerp(field.confidence);
    return out;
}

std::vector<float> packField(const Field& field) {
    std::vector<float> data;
    if (!field.valid()) {
        return data;
    }
    data.resize(field.blocks() * 4u);
    for (std::size_t i = 0; i < field.blocks(); ++i) {
        data[i * 4u + 0u] = field.dx[i] / static_cast<float>(field.imageWidth);
        data[i * 4u + 1u] = field.dy[i] / static_cast<float>(field.imageHeight);
        data[i * 4u + 2u] = field.confidence[i];
        data[i * 4u + 3u] = 0.0f;
    }
    return data;
}

void interpolateImage(const Image& a, const Image& b, const Field& field, float t, Image& out) {
    prepare(out, a.width, a.height);
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(a.width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(a.height);
            const Pixel holdA = sampleBilinear(a, u, v);
            if (field.sceneCut || !field.valid() || b.width != a.width || b.height != a.height) {
                store(out, x, y, holdA);
                continue;
            }
            const Pixel holdB = sampleBilinear(b, u, v);
            const Pixel nearest = clamped < 0.5f ? holdA : holdB;
            const FieldSample f = sampleField(field, u, v);
            const Pixel fromA = sampleBilinear(a, u - clamped * f.dx, v - clamped * f.dy);
            const Pixel fromB = sampleBilinear(b, u + (1.0f - clamped) * f.dx, v + (1.0f - clamped) * f.dy);
            const Pixel blended = mixPixel(fromA, fromB, clamped);
            const float consistency = 1.0f - smoothStep(kConsistencyLow, kConsistencyHigh, maxDifference(fromA, fromB));
            const float weight = std::clamp(f.confidence, 0.0f, 1.0f) * consistency;
            store(out, x, y, mixPixel(nearest, blended, weight));
        }
    }
}

void motionBlurImage(const Image& a, const Field& field, float shutter, Image& out) {
    prepare(out, a.width, a.height);
    const float amount = std::clamp(shutter, 0.0f, 1.0f);
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(a.width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(a.height);
            if (field.sceneCut || !field.valid() || amount <= 0.0f) {
                store(out, x, y, sampleBilinear(a, u, v));
                continue;
            }
            const FieldSample f = sampleField(field, u, v);
            const float scale = amount * std::clamp(f.confidence, 0.0f, 1.0f);
            Pixel sum;
            for (int k = 0; k < kBlurTaps; ++k) {
                const float s = static_cast<float>(k) / static_cast<float>(kBlurTaps - 1) - 0.5f;
                const Pixel tap = sampleBilinear(a, u + s * scale * f.dx, v + s * scale * f.dy);
                sum.r += tap.r;
                sum.g += tap.g;
                sum.b += tap.b;
                sum.a += tap.a;
            }
            const float inverse = 1.0f / static_cast<float>(kBlurTaps);
            Pixel mean;
            mean.r = sum.r * inverse;
            mean.g = sum.g * inverse;
            mean.b = sum.b * inverse;
            mean.a = sum.a * inverse;
            store(out, x, y, mean);
        }
    }
}

void vectorsImage(const Field& field, int width, int height, Image& out) {
    prepare(out, width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            const FieldSample f = sampleField(field, u, v);
            std::uint8_t* p = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u];
            p[0] = toByte(0.5f + 0.5f * std::clamp(f.dx / kVectorRange, -1.0f, 1.0f));
            p[1] = toByte(0.5f + 0.5f * std::clamp(f.dy / kVectorRange, -1.0f, 1.0f));
            p[2] = toByte(std::clamp(f.confidence, 0.0f, 1.0f));
            p[3] = 255;
        }
    }
}

}

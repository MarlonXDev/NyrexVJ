#include "check.hpp"

#include "core/flow_synth.hpp"
#include "core/optical_flow.hpp"
#include "core/settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace nvj::flow;

constexpr int kW = 320;
constexpr int kH = 176;

float sceneValue(float x, float y, int channel) {
    const float c = static_cast<float>(channel);
    float v = 0.5f;
    v += 0.18f * std::sin(0.21f * x + 0.13f * y + c);
    v += 0.15f * std::sin(0.07f * x - 0.19f * y + 2.0f * c);
    v += 0.10f * std::sin(0.33f * x + 0.02f * y);
    v += 0.10f * std::sin(0.05f * x + 0.41f * y + 1.0f);
    v += 0.08f * std::sin(0.50f * x - 0.30f * y);
    v += 0.25f * std::exp(-((x - 90.0f) * (x - 90.0f) + (y - 60.0f) * (y - 60.0f)) / 260.0f);
    v -= 0.2f * std::exp(-((x - 220.0f) * (x - 220.0f) + (y - 110.0f) * (y - 110.0f)) / 420.0f);
    return std::clamp(v, 0.0f, 1.0f);
}

Image makeImage(int w, int h, float vx, float vy, float tau, float gain = 1.0f) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u];
            for (int c = 0; c < 3; ++c) {
                const float v = std::clamp(sceneValue(static_cast<float>(x) + 0.5f - vx * tau, static_cast<float>(y) + 0.5f - vy * tau, c) * gain, 0.0f, 1.0f);
                p[c] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
            }
            p[3] = 255;
        }
    }
    return image;
}

Image makeNoise(int w, int h, std::uint32_t seed) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        state = state * 1664525u + 1013904223u;
        const std::uint8_t value = static_cast<std::uint8_t>(state >> 24);
        image.rgba[i] = value;
        image.rgba[i + 1] = static_cast<std::uint8_t>(value / 2 + 30);
        image.rgba[i + 2] = static_cast<std::uint8_t>(255 - value);
        image.rgba[i + 3] = 255;
    }
    return image;
}

Image makeFlat(int w, int h, std::uint8_t value) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, value);
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        image.rgba[i] = 255;
    }
    return image;
}

Luma lumaOf(const Image& image) {
    return lumaFromRgba(image.rgba.data(), image.width, image.height, image.width * 4, image.width, image.height);
}

double psnr(const Image& a, const Image& b, int margin) {
    double sum = 0.0;
    std::size_t count = 0;
    for (int y = margin; y < a.height - margin; ++y) {
        for (int x = margin; x < a.width - margin; ++x) {
            for (int c = 0; c < 3; ++c) {
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(x)) * 4u + static_cast<std::size_t>(c);
                const double d = (static_cast<double>(a.rgba[i]) - static_cast<double>(b.rgba[i])) / 255.0;
                sum += d * d;
                ++count;
            }
        }
    }
    const double mse = sum / static_cast<double>(count);
    if (mse < 1.0e-12) {
        return 99.0;
    }
    return 10.0 * std::log10(1.0 / mse);
}

Image plainBlend(const Image& a, const Image& b, float t) {
    Image out = a;
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        out.rgba[i] = static_cast<std::uint8_t>(std::lround(static_cast<float>(a.rgba[i]) * (1.0f - t) + static_cast<float>(b.rgba[i]) * t));
    }
    return out;
}

int maxDiff(const Image& a, const Image& b) {
    int worst = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i])));
    }
    return worst;
}

struct Accuracy {
    double meanError = 0.0;
    double worst = 0.0;
    double confident = 0.0;
};

Accuracy flowAccuracy(const Field& field, float vx, float vy, int marginBlocks) {
    Accuracy out;
    double sum = 0.0;
    int count = 0;
    int confident = 0;
    for (int by = marginBlocks; by < field.rows - marginBlocks; ++by) {
        for (int bx = marginBlocks; bx < field.cols - marginBlocks; ++bx) {
            const std::size_t i = static_cast<std::size_t>(by) * static_cast<std::size_t>(field.cols) + static_cast<std::size_t>(bx);
            const double ex = field.dx[i] - vx;
            const double ey = field.dy[i] - vy;
            const double e = std::sqrt(ex * ex + ey * ey);
            sum += e;
            out.worst = std::max(out.worst, e);
            if (field.confidence[i] > 0.5f) {
                ++confident;
            }
            ++count;
        }
    }
    out.meanError = count > 0 ? sum / count : 0.0;
    out.confident = count > 0 ? static_cast<double>(confident) / count : 0.0;
    return out;
}

struct Layers {
    float discX;
    float discY;
    float rectX;
    float rectY;
};

Layers layersAt(float tau) {
    return {80.0f + 9.0f * tau, 70.0f, 230.0f - 5.0f * tau, 60.0f + 4.0f * tau};
}

Image makeLayered(int w, int h, float tau, std::uint32_t noiseSeed) {
    const Layers l = layersAt(tau);
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    std::uint32_t state = noiseSeed;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const bool inDisc = (px - l.discX) * (px - l.discX) + (py - l.discY) * (py - l.discY) < 30.0f * 30.0f;
            const bool inRect = px > l.rectX - 28.0f && px < l.rectX + 28.0f && py > l.rectY - 22.0f && py < l.rectY + 22.0f;
            std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u];
            for (int c = 0; c < 3; ++c) {
                float v;
                if (inDisc) {
                    v = sceneValue(px - l.discX + 400.0f, py - l.discY + 300.0f, c);
                } else if (inRect) {
                    v = sceneValue(px - l.rectX + 700.0f, py - l.rectY + 50.0f, c);
                } else {
                    v = sceneValue(px, py, c) * 0.8f + 0.1f;
                }
                if (noiseSeed != 0) {
                    state = state * 1664525u + 1013904223u;
                    v += (static_cast<float>(state >> 24) / 255.0f - 0.5f) * 0.03f;
                }
                p[c] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            }
            p[3] = 255;
        }
    }
    return image;
}

double regionError(const Field& field, int centreX, int centreY, int halfBlocks, float vx, float vy) {
    const int cx = centreX / field.block;
    const int cy = centreY / field.block;
    double sum = 0.0;
    int count = 0;
    for (int by = cy - halfBlocks; by <= cy + halfBlocks; ++by) {
        for (int bx = cx - halfBlocks; bx <= cx + halfBlocks; ++bx) {
            const std::size_t i = static_cast<std::size_t>(by) * static_cast<std::size_t>(field.cols) + static_cast<std::size_t>(bx);
            sum += std::hypot(field.dx[i] - vx, field.dy[i] - vy);
            ++count;
        }
    }
    return sum / count;
}

Field estimateOrFail(const Image& a, const Image& b, Tier tier) {
    Field field;
    std::string error;
    const bool ok = estimate(lumaOf(a), lumaOf(b), tier, field, &error);
    CHECK(ok);
    if (!ok) {
        std::fprintf(stderr, "estimate failed: %s\n", error.c_str());
    }
    return field;
}

void testNamesAndLimits() {
    for (int i = 0; i < kModeCount; ++i) {
        const Mode mode = static_cast<Mode>(i);
        CHECK(modeFromName(modeName(mode), Mode::Vectors) == mode);
    }
    CHECK(modeFromName("nonsense", Mode::MotionBlur) == Mode::MotionBlur);
    CHECK(modeFromName("", Mode::Off) == Mode::Off);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        CHECK(tierFromName(tierName(tier), Tier::Fine) == tier);
    }
    CHECK(tierFromName("ultra", Tier::Fast) == Tier::Fast);
    CHECK(clampTier(-5) == Tier::Fast);
    CHECK(clampTier(1) == Tier::Balanced);
    CHECK(clampTier(99) == Tier::Fine);
    CHECK(clampBudgetMb(0) == kMinBudgetMb);
    CHECK(clampBudgetMb(100000) == kMaxBudgetMb);
    CHECK(clampBudgetMb(256) == 256);
    for (int i = 0; i < kTierCount; ++i) {
        const TierSpec& spec = tierSpec(static_cast<Tier>(i));
        CHECK(spec.block == 8);
        CHECK(spec.analysisWidth % spec.block == 0);
        CHECK(spec.levels >= 2 && spec.levels <= 5);
        CHECK(spec.radius >= 2);
    }
    CHECK(tierSpec(Tier::Fast).analysisWidth < tierSpec(Tier::Balanced).analysisWidth);
    CHECK(tierSpec(Tier::Balanced).analysisWidth < tierSpec(Tier::Fine).analysisWidth);
    CHECK(!tierSpec(Tier::Fast).bidirectional);
    CHECK(tierSpec(Tier::Fine).bidirectional);
}

void testAnalysisSize() {
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        const int block = tierSpec(tier).block;
        const int sizes[][2] = {{1920, 1080}, {1280, 720}, {640, 360}, {1080, 1920}, {3840, 2160}, {100, 100}, {64, 64}, {20000, 100}, {100, 20000}, {1, 1}};
        for (const auto& size : sizes) {
            int w = 0;
            int h = 0;
            analysisSize(size[0], size[1], tier, w, h);
            CHECK(w % block == 0);
            CHECK(h % block == 0);
            CHECK(w >= block * 2);
            CHECK(h >= block * 2);
            CHECK(w <= std::max(tierSpec(tier).analysisWidth, block * 4));
            CHECK(h <= kMaxFlowRows * block);
            CHECK(w / block <= kMaxFlowCols);
        }
    }
    int w = 0;
    int h = 0;
    analysisSize(1920, 1080, Tier::Balanced, w, h);
    CHECK(w == 240);
    CHECK(h == 136);
    analysisSize(0, 0, Tier::Fast, w, h);
    CHECK(w == tierSpec(Tier::Fast).analysisWidth);
    CHECK(h > 0);
    analysisSize(48, 48, Tier::Fine, w, h);
    CHECK(w == 48);
    CHECK(h == 48);
}

void testLuma() {
    const Image image = makeImage(64, 48, 0.0f, 0.0f, 0.0f);
    const Luma same = lumaOf(image);
    CHECK(same.valid());
    CHECK(same.width == 64 && same.height == 48);
    const std::uint8_t* p = &image.rgba[(5 * 64 + 7) * 4];
    const unsigned expected = (54u * p[0] + 183u * p[1] + 19u * p[2] + 128u) >> 8;
    CHECK(same.pixels[5 * 64 + 7] == expected);

    const Luma half = lumaFromRgba(image.rgba.data(), 64, 48, 64 * 4, 32, 24);
    CHECK(half.valid());
    CHECK(half.width == 32 && half.height == 24);
    double a = 0.0;
    double b = 0.0;
    for (const auto v : same.pixels) {
        a += v;
    }
    for (const auto v : half.pixels) {
        b += v;
    }
    CHECK(std::fabs(a / same.pixels.size() - b / half.pixels.size()) < 1.5);

    CHECK(!lumaFromRgba(nullptr, 64, 48, 256, 32, 24).valid());
    CHECK(!lumaFromRgba(image.rgba.data(), 0, 48, 256, 32, 24).valid());
    CHECK(!lumaFromRgba(image.rgba.data(), 64, 48, 100, 32, 24).valid());
    CHECK(!lumaFromRgba(image.rgba.data(), 64, 48, 256, 0, 24).valid());

    Image transparent = makeFlat(16, 16, 200);
    for (std::size_t i = 3; i < transparent.rgba.size(); i += 4) {
        transparent.rgba[i] = 0;
    }
    const Luma dark = lumaFromRgba(transparent.rgba.data(), 16, 16, 64, 16, 16);
    CHECK(dark.pixels[0] == 0);
    for (std::size_t i = 3; i < transparent.rgba.size(); i += 4) {
        transparent.rgba[i] = 128;
    }
    const Luma halfAlpha = lumaFromRgba(transparent.rgba.data(), 16, 16, 64, 16, 16);
    CHECK(halfAlpha.pixels[0] > 90 && halfAlpha.pixels[0] < 110);

    std::vector<std::uint8_t> plane8(16 * 8, 0);
    for (std::size_t i = 0; i < plane8.size(); ++i) {
        plane8[i] = static_cast<std::uint8_t>(i * 2);
    }
    const Luma l8 = lumaFromPlane(plane8.data(), 16, 8, 16, 1, 8, false, nullptr, 0, 0, 16, 8);
    CHECK(l8.valid());
    CHECK(l8.pixels[10] == 20);

    std::vector<std::uint8_t> plane10(16 * 8 * 2, 0);
    for (int i = 0; i < 16 * 8; ++i) {
        const unsigned value = static_cast<unsigned>((i % 16) * 64);
        plane10[static_cast<std::size_t>(i) * 2] = static_cast<std::uint8_t>(value & 0xff);
        plane10[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
    }
    const Luma l10 = lumaFromPlane(plane10.data(), 16, 8, 32, 2, 10, false, nullptr, 0, 0, 16, 8);
    CHECK(l10.valid());
    CHECK(l10.pixels[15] == (15 * 64) >> 2);
    CHECK(l10.pixels[0] == 0);

    std::vector<std::uint8_t> plane16(16 * 8 * 2, 0);
    for (int i = 0; i < 16 * 8; ++i) {
        const unsigned value = static_cast<unsigned>((i % 16) * 4096);
        plane16[static_cast<std::size_t>(i) * 2] = static_cast<std::uint8_t>(value & 0xff);
        plane16[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
    }
    const Luma l16 = lumaFromPlane(plane16.data(), 16, 8, 32, 2, 16, true, nullptr, 0, 0, 16, 8);
    CHECK(l16.valid());
    CHECK(l16.pixels[15] == 240);
    CHECK(!lumaFromPlane(plane8.data(), 16, 8, 8, 1, 8, false, nullptr, 0, 0, 16, 8).valid());
    CHECK(!lumaFromPlane(plane8.data(), 16, 8, 16, 3, 8, false, nullptr, 0, 0, 16, 8).valid());

    std::vector<std::uint8_t> alphaPlane(16 * 8, 0);
    const Luma masked = lumaFromPlane(plane8.data(), 16, 8, 16, 1, 8, false, alphaPlane.data(), 16, 1, 16, 8);
    CHECK(masked.valid());
    CHECK(masked.pixels[10] == 0);
    std::fill(alphaPlane.begin(), alphaPlane.end(), static_cast<std::uint8_t>(255));
    const Luma opaque = lumaFromPlane(plane8.data(), 16, 8, 16, 1, 8, false, alphaPlane.data(), 16, 1, 16, 8);
    CHECK(opaque.pixels[10] == 20);

    std::vector<std::uint8_t> uyvy(16 * 8 * 2, 0);
    for (int i = 0; i < 16 * 8; ++i) {
        uyvy[static_cast<std::size_t>(i) * 2] = 128;
        uyvy[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::uint8_t>(i % 200);
    }
    const Luma lu = lumaFromUyvy(uyvy.data(), 16, 8, 32, 16, 8);
    CHECK(lu.valid());
    CHECK(lu.pixels[37] == 37);
    CHECK(!lumaFromUyvy(nullptr, 16, 8, 32, 16, 8).valid());
    CHECK(!lumaFromUyvy(uyvy.data(), 16, 8, 16, 16, 8).valid());
}

void testEstimateRejectsBadInput() {
    Field field;
    std::string error;
    Luma empty;
    Luma good = lumaOf(makeImage(kW, kH, 0, 0, 0));
    CHECK(!estimate(empty, good, Tier::Balanced, field, &error));
    CHECK(!error.empty());
    CHECK(!field.valid());
    Luma other = lumaOf(makeImage(kW - 16, kH, 0, 0, 0));
    error.clear();
    CHECK(!estimate(good, other, Tier::Balanced, field, &error));
    CHECK(!error.empty());
    Luma odd;
    odd.width = 100;
    odd.height = 100;
    odd.pixels.assign(10000, 7);
    error.clear();
    CHECK(!estimate(odd, odd, Tier::Balanced, field, &error));
    CHECK(!error.empty());
    Luma tiny;
    tiny.width = 8;
    tiny.height = 8;
    tiny.pixels.assign(64, 1);
    CHECK(!estimate(tiny, tiny, Tier::Fast, field, nullptr));
}

void testEstimateTranslation() {
    struct Case {
        Tier tier;
        float vx;
        float vy;
        double meanLimit;
    };
    const Case cases[] = {
        {Tier::Fast, 5.0f, -3.0f, 0.6},
        {Tier::Balanced, 5.0f, -3.0f, 0.5},
        {Tier::Fine, 5.0f, -3.0f, 0.5},
        {Tier::Balanced, -7.0f, 4.0f, 0.6},
        {Tier::Balanced, 3.5f, 0.0f, 0.6},
        {Tier::Fine, 0.0f, 6.25f, 0.6},
        {Tier::Balanced, 0.0f, 0.0f, 0.1},
    };
    for (const Case& c : cases) {
        const Image a = makeImage(kW, kH, c.vx, c.vy, 0.0f);
        const Image b = makeImage(kW, kH, c.vx, c.vy, 1.0f);
        const Field field = estimateOrFail(a, b, c.tier);
        CHECK(field.valid());
        CHECK(field.cols == kW / 8);
        CHECK(field.rows == kH / 8);
        const Accuracy accuracy = flowAccuracy(field, c.vx, c.vy, 2);
        std::printf("  translation tier=%s v=(%.2f,%.2f) mean error %.3f worst %.3f confident %.2f\n", tierName(c.tier), c.vx, c.vy, accuracy.meanError, accuracy.worst, accuracy.confident);
        CHECK(accuracy.meanError < c.meanLimit);
        CHECK(accuracy.confident > 0.8);
        CHECK(!field.sceneCut);
        CHECK(field.confidentFraction > 0.8f);
        CHECK(field.texturedMatched > 0.8f);
        CHECK(field.matchedCost < field.zeroCost + 1.0e-3f || c.vx == 0.0f && c.vy == 0.0f);
    }
}

void testEstimateFastMotion() {
    struct Case {
        Tier tier;
        float vx;
        float vy;
    };
    const Case cases[] = {
        {Tier::Fast, 12.0f, 0.0f},
        {Tier::Balanced, 18.0f, -6.0f},
        {Tier::Fine, -20.0f, 8.0f},
    };
    for (const Case& c : cases) {
        const Image a = makeImage(kW, kH, c.vx, c.vy, 0.0f);
        const Image b = makeImage(kW, kH, c.vx, c.vy, 1.0f);
        const Field field = estimateOrFail(a, b, c.tier);
        const Accuracy accuracy = flowAccuracy(field, c.vx, c.vy, 3);
        std::printf("  fast tier=%s v=(%.1f,%.1f) mean error %.3f worst %.3f confident %.2f maxMag %.1f\n", tierName(c.tier), c.vx, c.vy, accuracy.meanError, accuracy.worst, accuracy.confident, field.maxMagnitude);
        CHECK(accuracy.meanError < 1.5);
        CHECK(accuracy.confident > 0.6);
        CHECK(!field.sceneCut);
    }
    const Image a = makeImage(kW, kH, 0, 0, 0.0f);
    const Image b = makeImage(kW, kH, 90.0f, 0, 1.0f);
    const Field field = estimateOrFail(a, b, Tier::Fast);
    CHECK(field.valid());
    CHECK(field.maxMagnitude <= static_cast<float>(tierSpec(Tier::Fast).radius * 4 + 16));
    for (std::size_t i = 0; i < field.blocks(); ++i) {
        CHECK(std::isfinite(field.dx[i]) && std::isfinite(field.dy[i]));
        CHECK(field.confidence[i] >= 0.0f && field.confidence[i] <= 1.0f);
    }
}

void testFlatAndStatic() {
    const Image a = makeFlat(kW, kH, 90);
    const Field flat = estimateOrFail(a, a, Tier::Balanced);
    CHECK(flat.valid());
    CHECK(!flat.sceneCut);
    CHECK(flat.maxMagnitude < 0.01f);
    CHECK(flat.matchedCost < 0.01f);

    const Image still = makeImage(kW, kH, 0, 0, 0.0f);
    const Field same = estimateOrFail(still, still, Tier::Fine);
    CHECK(same.maxMagnitude < 0.01f);
    CHECK(!same.sceneCut);
    CHECK(same.confidentFraction > 0.95f);

    const Image dim = makeImage(kW, kH, 0, 0, 0.0f, 0.96f);
    const Field lit = estimateOrFail(still, dim, Tier::Balanced);
    CHECK(!lit.sceneCut);
    CHECK(lit.meanMagnitude < 1.0f);
}

void testSceneCut() {
    const Image a = makeNoise(kW, kH, 11);
    const Image b = makeNoise(kW, kH, 97);
    for (int i = 0; i < kTierCount; ++i) {
        const Field field = estimateOrFail(a, b, static_cast<Tier>(i));
        std::printf("  cut tier=%s cost %.1f confident %.2f cut=%d\n", tierName(static_cast<Tier>(i)), field.matchedCost, field.confidentFraction, field.sceneCut ? 1 : 0);
        CHECK(field.sceneCut);
        CHECK(field.texturedMatched < kCutMatched);
    }
    const Image scene = makeImage(kW, kH, 4.0f, 0.0f, 0.0f);
    const Image other = makeNoise(kW, kH, 5);
    CHECK(estimateOrFail(scene, other, Tier::Balanced).sceneCut);
    const Image moved = makeImage(kW, kH, 4.0f, 0.0f, 1.0f);
    CHECK(!estimateOrFail(scene, moved, Tier::Balanced).sceneCut);
    const Image scenic = makeImage(kW, kH, 0.0f, 0.0f, 0.0f);
    const Field toBlack = estimateOrFail(scenic, makeFlat(kW, kH, 0), Tier::Balanced);
    std::printf("  scene to black: cost %.1f confident %.2f cut=%d\n", toBlack.matchedCost, toBlack.confidentFraction, toBlack.sceneCut ? 1 : 0);
    CHECK(toBlack.sceneCut);
    const Field toWhite = estimateOrFail(scenic, makeFlat(kW, kH, 255), Tier::Fast);
    CHECK(toWhite.sceneCut);
    const Field jumped = estimateOrFail(scenic, makeImage(kW, kH, 0.0f, 0.0f, 0.0f, 0.3f), Tier::Balanced);
    std::printf("  scene to dark copy: cost %.1f confident %.2f cut=%d\n", jumped.matchedCost, jumped.confidentFraction, jumped.sceneCut ? 1 : 0);
    CHECK(jumped.sceneCut);
}

Image makeOtherShot(int w, int h, float tau) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u];
            for (int c = 0; c < 3; ++c) {
                const float v = 1.0f - (sceneValue(static_cast<float>(y) * 1.3f + 40.0f + 3.0f * tau, static_cast<float>(x) * 0.9f + 90.0f, (c + 1) % 3) * 0.8f + 0.1f);
                p[c] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            }
            p[3] = 255;
        }
    }
    return image;
}

void testSceneCutBetweenTexturedShots() {
    const Image before = makeLayered(kW, kH, 14.0f * 0.2f, 5);
    const Image after = makeOtherShot(kW, kH, 0.0f);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        const Field cut = estimateOrFail(before, after, tier);
        const Field same = estimateOrFail(before, makeLayered(kW, kH, 14.0f * 0.2f + 0.2f, 6), tier);
        std::printf("  textured cut tier=%s: matched %.2f (cut), %.2f (same shot)\n", tierName(tier), cut.texturedMatched, same.texturedMatched);
        CHECK(cut.sceneCut);
        CHECK(!same.sceneCut);
        CHECK(cut.texturedMatched < same.texturedMatched);
    }
}

Image mixShots(const Image& a, const Image& b, float weight) {
    Image out = a;
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        out.rgba[i] = static_cast<std::uint8_t>(std::lround(static_cast<float>(a.rgba[i]) * (1.0f - weight) + static_cast<float>(b.rgba[i]) * weight));
    }
    return out;
}

Image dimShot(const Image& a, float gain) {
    Image out = a;
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        out.rgba[i] = static_cast<std::uint8_t>(std::lround(static_cast<float>(a.rgba[i]) * gain));
    }
    return out;
}

int countCuts(const Image& from, const Image& to, bool fade, int steps, Tier tier) {
    int cuts = 0;
    for (int i = 0; i < steps; ++i) {
        const float first = static_cast<float>(i) / static_cast<float>(steps);
        const float second = static_cast<float>(i + 1) / static_cast<float>(steps);
        const Image a = fade ? dimShot(from, 1.0f - first) : mixShots(from, to, first);
        const Image b = fade ? dimShot(from, 1.0f - second) : mixShots(from, to, second);
        cuts += estimateOrFail(a, b, tier).sceneCut ? 1 : 0;
    }
    return cuts;
}

void testFadesAndDissolves() {
    const Image shot = makeLayered(kW, kH, 2.8f, 5);
    const Image other = makeOtherShot(kW, kH, 0.0f);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        const int dissolve = countCuts(shot, other, false, 8, tier);
        const int slowFade = countCuts(shot, other, true, 15, tier);
        const int fastFade = countCuts(shot, other, true, 4, tier);
        std::printf("  tier=%s dissolve over 8 frames: %d cuts, fade over 15 frames: %d cuts, fade over 4 frames: %d cuts\n", tierName(tier), dissolve, slowFade, fastFade);
        CHECK(dissolve == 0);
        CHECK(slowFade == 0);
        CHECK(fastFade == 4);
    }
}

void testPlanPair() {
    const double period = 1.0 / 30.0;
    for (int i = 0; i <= 4; ++i) {
        const double seconds = period * static_cast<double>(i) / 4.0;
        const PairPlan plan = planPair(seconds, 0.0, period, period, false);
        CHECK(plan.usable);
    }
    PairPlan plan = planPair(0.0, 0.0, period, period, false);
    CHECK(plan.usable && plan.exact && plan.t == 0.0f);
    plan = planPair(1.0 / 60.0, 0.0, period, period, false);
    CHECK(plan.usable && !plan.exact);
    CHECK(std::fabs(plan.t - 0.5f) < 1.0e-4f);
    plan = planPair(period, 0.0, period, period, false);
    CHECK(plan.usable && plan.exact && plan.t == 1.0f);
    plan = planPair(period * 0.25, 0.0, period, period, false);
    CHECK(std::fabs(plan.t - 0.25f) < 1.0e-4f);

    plan = planPair(0.02, 0.0, period, period, true);
    CHECK(!plan.usable);
    CHECK(!plan.reason.empty());
    plan = planPair(0.02, period, 0.0, period, false);
    CHECK(!plan.usable);
    plan = planPair(0.02, 0.01, 0.01, period, false);
    CHECK(!plan.usable);
    plan = planPair(0.2, 0.0, period, period, false);
    CHECK(!plan.usable);
    plan = planPair(-0.2, 0.0, period, period, false);
    CHECK(!plan.usable);

    plan = planPair(0.05, 0.0, 0.1, period, false);
    CHECK(!plan.usable);
    CHECK(plan.reason.find("gap") != std::string::npos);
    plan = planPair(0.04, 0.0, 0.0667, period, false);
    CHECK(plan.usable);
    CHECK(std::fabs(plan.t - 0.6f) < 0.01f);
    plan = planPair(0.01, 0.0, 0.01, 0.0, false);
    CHECK(plan.usable && plan.t == 1.0f);
    plan = planPair(0.01, 0.0, 0.0333, 0.0, false);
    CHECK(plan.usable);

    const double vfr[] = {0.0, 0.040, 0.071, 0.100, 0.144, 0.171};
    int usable = 0;
    for (int i = 0; i + 1 < 6; ++i) {
        const double mid = (vfr[i] + vfr[i + 1]) * 0.5;
        const PairPlan p = planPair(mid, vfr[i], vfr[i + 1], period, false);
        if (p.usable) {
            ++usable;
            CHECK(std::fabs(p.t - 0.5f) < 1.0e-3f);
        }
    }
    CHECK(usable == 5);

    const double drops[] = {0.0, 1.0 * period, 3.0 * period};
    plan = planPair(2.0 * period, drops[1], drops[2], period, false);
    CHECK(plan.usable);
    plan = planPair(3.5 * period, 3.0 * period, 7.0 * period, period, false);
    CHECK(!plan.usable);
}

void testGovernor() {
    const double period = 1.0 / 30.0;
    Governor calm(Tier::Balanced);
    CHECK(calm.current() == Tier::Balanced);
    for (int i = 0; i < 400; ++i) {
        const Governor::Decision decision = calm.record(4.0, period);
        CHECK(decision == Governor::Decision::Keep);
    }
    CHECK(calm.current() == Tier::Balanced);
    CHECK(!calm.disabled());
    CHECK(std::fabs(calm.average() - 4.0) < 1.0e-9);
    CHECK(std::fabs(calm.p95() - 4.0) < 1.0e-9);
    CHECK(calm.samples() <= 64);

    Governor loaded(Tier::Fine);
    int lowered = 0;
    int disabled = 0;
    for (int i = 0; i < 200; ++i) {
        const Governor::Decision decision = loaded.record(period * 1000.0 * 0.9, period);
        if (decision == Governor::Decision::Lower) {
            ++lowered;
        }
        if (decision == Governor::Decision::Disable) {
            ++disabled;
        }
    }
    CHECK(lowered == 2);
    CHECK(loaded.current() == Tier::Fast);
    CHECK(loaded.disabled());
    CHECK(disabled >= 1);
    CHECK(loaded.lowered() == 2);
    CHECK(loaded.record(1.0, period) == Governor::Decision::Disable);

    loaded.setRequested(Tier::Balanced);
    CHECK(!loaded.disabled());
    CHECK(loaded.current() == Tier::Balanced);

    Governor spiky(Tier::Balanced);
    for (int i = 0; i < 300; ++i) {
        spiky.record(i % 6 == 0 ? period * 1000.0 * 1.5 : 3.0, period);
    }
    CHECK(spiky.current() == Tier::Balanced);
    CHECK(!spiky.disabled());
    CHECK(spiky.p95() > 3.0);

    Governor recover(Tier::Balanced);
    for (int i = 0; i < 6; ++i) {
        recover.record(period * 1000.0, period);
    }
    CHECK(recover.current() == Tier::Fast);
    CHECK(!recover.disabled());
    Governor::Decision last = Governor::Decision::Keep;
    int raised = 0;
    for (int i = 0; i < 400; ++i) {
        last = recover.record(1.0, period);
        if (last == Governor::Decision::Raise) {
            ++raised;
        }
    }
    CHECK(raised == 1);
    CHECK(recover.current() == Tier::Balanced);
    CHECK(!recover.disabled());

    recover.reset();
    CHECK(recover.samples() == 0);
    CHECK(recover.lowered() == 0);
    CHECK(recover.average() == 0.0);
    CHECK(recover.p95() == 0.0);

    Governor slowSource(Tier::Balanced);
    for (int i = 0; i < 100; ++i) {
        slowSource.record(40.0, 1.0 / 5.0);
    }
    CHECK(slowSource.current() == Tier::Balanced);
}

void testBudget() {
    Budget budget;
    budget.setLimit(1000);
    int a = 0;
    int b = 0;
    int c = 0;
    CHECK(budget.reserve(&a, 400));
    CHECK(budget.reserve(&b, 500));
    CHECK(!budget.reserve(&c, 200));
    CHECK(budget.used() == 900);
    CHECK(budget.owners() == 2);
    CHECK(budget.reserved(&c) == 0);
    CHECK(budget.reserve(&a, 450));
    CHECK(budget.used() == 950);
    CHECK(!budget.reserve(&a, 600));
    CHECK(budget.reserved(&a) == 450);
    budget.release(&b);
    CHECK(budget.reserve(&c, 500));
    CHECK(budget.used() == 950);
    budget.release(&a);
    budget.release(&c);
    budget.release(&c);
    CHECK(budget.used() == 0);
    CHECK(budget.owners() == 0);
    budget.setLimit(10);
    CHECK(!budget.reserve(&a, 11));
    CHECK(budget.reserve(&a, 10));
    CHECK(budget.limit() == 10);
    CHECK(globalBudget().limit() == static_cast<std::uint64_t>(kDefaultBudgetMb) * 1024ull * 1024ull);

    CHECK(estimateBytes(Mode::Off, 1920, 1080) == 0);
    CHECK(estimateBytes(Mode::Interpolate, 0, 1080) == 0);
    const std::uint64_t interpolate = estimateBytes(Mode::Interpolate, 1920, 1080);
    const std::uint64_t blur = estimateBytes(Mode::MotionBlur, 1920, 1080);
    const std::uint64_t vectors = estimateBytes(Mode::Vectors, 1920, 1080);
    CHECK(interpolate > blur);
    CHECK(blur >= vectors);
    CHECK(interpolate >= 3ull * 1920ull * 1080ull * 4ull);
    CHECK(estimateBytes(Mode::Interpolate, 3840, 2160) > interpolate * 3);
    CHECK(interpolate < static_cast<std::uint64_t>(kDefaultBudgetMb) * 1024ull * 1024ull);
    CHECK(estimateBytes(Mode::Interpolate, 7680, 4320) > static_cast<std::uint64_t>(kDefaultBudgetMb) * 1024ull * 1024ull);
}

void testSampling() {
    Field field;
    field.cols = 4;
    field.rows = 2;
    field.block = 8;
    field.imageWidth = 32;
    field.imageHeight = 16;
    field.dx.assign(8, 0.0f);
    field.dy.assign(8, 0.0f);
    field.confidence.assign(8, 1.0f);
    for (int i = 0; i < 8; ++i) {
        field.dx[static_cast<std::size_t>(i)] = static_cast<float>(i % 4) * 4.0f;
        field.dy[static_cast<std::size_t>(i)] = i < 4 ? 0.0f : 8.0f;
    }
    CHECK(field.valid());
    FieldSample s = sampleField(field, 4.0f / 32.0f, 4.0f / 16.0f);
    CHECK(std::fabs(s.dx - 0.0f) < 1.0e-6f);
    s = sampleField(field, 12.0f / 32.0f, 4.0f / 16.0f);
    CHECK(std::fabs(s.dx - 4.0f / 32.0f) < 1.0e-6f);
    s = sampleField(field, 8.0f / 32.0f, 4.0f / 16.0f);
    CHECK(std::fabs(s.dx - 2.0f / 32.0f) < 1.0e-6f);
    s = sampleField(field, 0.0f, 0.0f);
    CHECK(std::fabs(s.dx) < 1.0e-6f);
    s = sampleField(field, 1.0f, 1.0f);
    CHECK(std::fabs(s.dx - 12.0f / 32.0f) < 1.0e-6f);
    CHECK(std::fabs(s.dy - 8.0f / 16.0f) < 1.0e-6f);
    s = sampleField(field, 0.5f, 0.5f);
    CHECK(std::fabs(s.dy - 4.0f / 16.0f) < 1.0e-6f);
    const std::vector<float> packed = packField(field);
    CHECK(packed.size() == 32);
    CHECK(std::fabs(packed[4] - 4.0f / 32.0f) < 1.0e-6f);
    CHECK(std::fabs(packed[4 * 4 + 1] - 8.0f / 16.0f) < 1.0e-6f);
    CHECK(packed[2] == 1.0f);
    Field invalid;
    CHECK(packField(invalid).empty());
    CHECK(sampleField(invalid, 0.5f, 0.5f).confidence == 0.0f);
}

void testInterpolation30To60() {
    const float vx = 5.0f;
    const float vy = -3.0f;
    const Image a = makeImage(kW, kH, vx, vy, 0.0f);
    const Image b = makeImage(kW, kH, vx, vy, 1.0f);
    const Image truth = makeImage(kW, kH, vx, vy, 0.5f);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        const Field field = estimateOrFail(a, b, tier);
        Image out;
        interpolateImage(a, b, field, 0.5f, out);
        CHECK(out.valid());
        const double flow = psnr(out, truth, 24);
        const double blend = psnr(plainBlend(a, b, 0.5f), truth, 24);
        const double hold = psnr(a, truth, 24);
        std::printf("  30->60 tier=%s PSNR interpolated %.2f dB, plain blend %.2f dB, hold %.2f dB\n", tierName(tier), flow, blend, hold);
        CHECK(flow > blend + 3.0);
        CHECK(flow > hold + 3.0);
        CHECK(flow > 24.0);
    }
    const Field field = estimateOrFail(a, b, Tier::Balanced);
    Image at0;
    Image at1;
    interpolateImage(a, b, field, 0.0f, at0);
    interpolateImage(a, b, field, 1.0f, at1);
    CHECK(maxDiff(at0, a) <= 2);
    CHECK(maxDiff(at1, b) <= 2);
    double sum = 0.0;
    int count = 0;
    for (int step = 1; step < 8; ++step) {
        const float t = static_cast<float>(step) / 8.0f;
        Image out;
        interpolateImage(a, b, field, t, out);
        const double value = psnr(out, makeImage(kW, kH, vx, vy, t), 24);
        sum += value;
        ++count;
        CHECK(value > 24.0);
    }
    std::printf("  sub-frame sweep mean PSNR %.2f dB\n", sum / count);
}

void testInterpolationFallbacks() {
    const Image a = makeImage(kW, kH, 5.0f, 0.0f, 0.0f);
    const Image b = makeImage(kW, kH, 5.0f, 0.0f, 1.0f);
    Field field = estimateOrFail(a, b, Tier::Balanced);
    Image out;
    Field cut = field;
    cut.sceneCut = true;
    interpolateImage(a, b, cut, 0.9f, out);
    CHECK(maxDiff(out, a) <= 1);
    interpolateImage(a, b, Field(), 0.5f, out);
    CHECK(maxDiff(out, a) <= 1);
    const Image smaller = makeImage(kW - 16, kH, 5.0f, 0.0f, 1.0f);
    interpolateImage(a, smaller, field, 0.5f, out);
    CHECK(out.width == kW);
    CHECK(maxDiff(out, a) <= 1);

    Field distrust = field;
    std::fill(distrust.confidence.begin(), distrust.confidence.end(), 0.0f);
    interpolateImage(a, b, distrust, 0.25f, out);
    CHECK(maxDiff(out, a) <= 1);
    interpolateImage(a, b, distrust, 0.75f, out);
    CHECK(maxDiff(out, b) <= 1);

    const Image noiseA = makeNoise(kW, kH, 3);
    const Image noiseB = makeNoise(kW, kH, 4);
    Field wrong = estimateOrFail(noiseA, noiseB, Tier::Fast);
    wrong.sceneCut = false;
    std::fill(wrong.confidence.begin(), wrong.confidence.end(), 1.0f);
    interpolateImage(noiseA, noiseB, wrong, 0.5f, out);
    const double nearest = psnr(out, noiseB, 0);
    CHECK(nearest > 0.0);
}

void testInterpolationAlpha() {
    Image a = makeImage(kW, kH, 6.0f, 0.0f, 0.0f);
    Image b = makeImage(kW, kH, 6.0f, 0.0f, 1.0f);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const int ax = x - 40;
            const int bx = x - 46;
            const std::size_t i = (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4u + 3u;
            a.rgba[i] = ax >= 80 && ax < 160 && y > 40 && y < 130 ? 255 : 0;
            b.rgba[i] = bx >= 80 && bx < 160 && y > 40 && y < 130 ? 255 : 0;
        }
    }
    const Field field = estimateOrFail(a, b, Tier::Balanced);
    Image out;
    interpolateImage(a, b, field, 0.5f, out);
    const std::size_t inside = (static_cast<std::size_t>(85) * kW + 163u) * 4u + 3u;
    const std::size_t outside = (static_cast<std::size_t>(85) * kW + 20u) * 4u + 3u;
    CHECK(out.rgba[inside] > 200);
    CHECK(out.rgba[outside] < 20);
}

void testMotionBlurAndVectors() {
    const Image a = makeImage(kW, kH, 8.0f, 0.0f, 0.0f);
    const Image b = makeImage(kW, kH, 8.0f, 0.0f, 1.0f);
    const Field field = estimateOrFail(a, b, Tier::Balanced);
    Image blurred;
    motionBlurImage(a, field, 0.0f, blurred);
    CHECK(maxDiff(blurred, a) <= 1);
    motionBlurImage(a, field, 1.0f, blurred);
    double rough = 0.0;
    double smooth = 0.0;
    for (int y = 30; y < 140; ++y) {
        for (int x = 30; x < 290; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4u + 1u;
            rough += std::abs(static_cast<int>(a.rgba[i + 4]) - static_cast<int>(a.rgba[i]));
            smooth += std::abs(static_cast<int>(blurred.rgba[i + 4]) - static_cast<int>(blurred.rgba[i]));
        }
    }
    std::printf("  blur horizontal gradient energy %.0f -> %.0f\n", rough, smooth);
    CHECK(smooth < rough * 0.8);
    Field cut = field;
    cut.sceneCut = true;
    motionBlurImage(a, cut, 1.0f, blurred);
    CHECK(maxDiff(blurred, a) <= 1);
    const Image still = makeImage(kW, kH, 0.0f, 0.0f, 0.0f);
    const Field idle = estimateOrFail(still, still, Tier::Balanced);
    motionBlurImage(still, idle, 1.0f, blurred);
    CHECK(maxDiff(blurred, still) <= 2);

    Image vectors;
    vectorsImage(field, kW, kH, vectors);
    CHECK(vectors.valid());
    const std::size_t centre = (static_cast<std::size_t>(88) * kW + 160u) * 4u;
    const float expectedR = 0.5f + 0.5f * std::clamp((8.0f / static_cast<float>(kW)) / kVectorRange, -1.0f, 1.0f);
    CHECK(std::abs(static_cast<int>(vectors.rgba[centre]) - static_cast<int>(std::lround(expectedR * 255.0f))) <= 12);
    CHECK(std::abs(static_cast<int>(vectors.rgba[centre + 1]) - 128) <= 8);
    CHECK(vectors.rgba[centre + 2] > 200);
    CHECK(vectors.rgba[centre + 3] == 255);
    Image idleVectors;
    vectorsImage(idle, 64, 32, idleVectors);
    CHECK(idleVectors.rgba[0] >= 127 && idleVectors.rgba[0] <= 129);
    CHECK(idleVectors.rgba[1] >= 127 && idleVectors.rgba[1] <= 129);
    vectorsImage(Field(), 16, 16, idleVectors);
    CHECK(idleVectors.valid());
    CHECK(idleVectors.rgba[2] == 0);
    CHECK(idleVectors.rgba[3] == 255);
}

void testLayeredMotion() {
    const Image a = makeLayered(kW, kH, 0.0f, 7);
    const Image b = makeLayered(kW, kH, 1.0f, 13);
    const Image truth = makeLayered(kW, kH, 0.5f, 0);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        const Field field = estimateOrFail(a, b, tier);
        const double disc = regionError(field, 84, 70, 1, 9.0f, 0.0f);
        const double rect = regionError(field, 228, 62, 1, -5.0f, 4.0f);
        const double background = regionError(field, 160, 150, 1, 0.0f, 0.0f);
        Image out;
        interpolateImage(a, b, field, 0.5f, out);
        const double flow = psnr(out, truth, 8);
        const double blend = psnr(plainBlend(a, b, 0.5f), truth, 8);
        const double hold = psnr(a, truth, 8);
        std::printf("  layers tier=%s disc error %.2f, rectangle error %.2f, background error %.2f, PSNR %.2f dB (blend %.2f, hold %.2f), cut=%d\n", tierName(tier), disc, rect, background, flow, blend, hold, field.sceneCut ? 1 : 0);
        CHECK(disc < 1.0);
        CHECK(rect < 1.0);
        CHECK(background < 0.5);
        CHECK(!field.sceneCut);
        CHECK(flow > blend + 1.0);
        CHECK(flow > hold + 3.0);
    }
}

void testFrameDrops() {
    const float vx = 2.5f;
    const float vy = 1.5f;
    const Image a = makeImage(kW, kH, vx, vy, 0.0f);
    const Image skipped = makeImage(kW, kH, vx, vy, 2.0f);
    const Field field = estimateOrFail(a, skipped, Tier::Balanced);
    const Accuracy accuracy = flowAccuracy(field, 2.0f * vx, 2.0f * vy, 3);
    std::printf("  dropped frame (gap of two periods) mean error %.3f confident %.2f\n", accuracy.meanError, accuracy.confident);
    CHECK(accuracy.meanError < 1.0);
    const double period = 1.0 / 30.0;
    const PairPlan plan = planPair(period, 0.0, 2.0 * period, period, false);
    CHECK(plan.usable);
    CHECK(std::fabs(plan.t - 0.5f) < 1.0e-4f);
    Image out;
    interpolateImage(a, skipped, field, plan.t, out);
    const double value = psnr(out, makeImage(kW, kH, vx, vy, 1.0f), 24);
    std::printf("  dropped frame restored with PSNR %.2f dB\n", value);
    CHECK(value > 22.0);
}

void testSequenceAndCuts() {
    std::vector<Image> frames;
    for (int i = 0; i < 6; ++i) {
        frames.push_back(makeImage(kW, kH, 5.0f, 2.0f, static_cast<float>(i)));
    }
    frames.push_back(makeNoise(kW, kH, 21));
    frames.push_back(makeNoise(kW, kH, 22));
    int cuts = 0;
    int smooth = 0;
    for (std::size_t i = 0; i + 1 < frames.size(); ++i) {
        const Field field = estimateOrFail(frames[i], frames[i + 1], Tier::Balanced);
        if (field.sceneCut) {
            ++cuts;
        } else {
            ++smooth;
        }
    }
    CHECK(smooth == 5);
    CHECK(cuts == 2);
}

void testLoopBoundary() {
    const double period = 1.0 / 30.0;
    const double duration = 10.0 * period;
    const double lastFrame = 9.0 * period;
    PairPlan plan = planPair(lastFrame + 0.01, lastFrame, 0.0, period, false);
    CHECK(!plan.usable);
    plan = planPair(lastFrame + 0.01, lastFrame, duration, period, false);
    CHECK(plan.usable);
    plan = planPair(0.01, 0.0, period, period, false);
    CHECK(plan.usable);
}

void testPerformanceBaseline() {
    const Image a = makeImage(kW, kH, 6.0f, 1.0f, 0.0f);
    const Image b = makeImage(kW, kH, 6.0f, 1.0f, 1.0f);
    const Luma la = lumaOf(a);
    const Luma lb = lumaOf(b);
    for (int i = 0; i < kTierCount; ++i) {
        const Tier tier = static_cast<Tier>(i);
        Field field;
        const auto begin = std::chrono::steady_clock::now();
        const int runs = 5;
        for (int r = 0; r < runs; ++r) {
            CHECK(estimate(la, lb, tier, field, nullptr));
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / runs;
        std::printf("  estimate on %dx%d tier=%s: %.2f ms per pair (single core, this run)\n", kW, kH, tierName(tier), ms);
        CHECK(ms < 2000.0);
    }
}

void testDeterminism() {
    const Image a = makeImage(kW, kH, 4.0f, -1.0f, 0.0f);
    const Image b = makeImage(kW, kH, 4.0f, -1.0f, 1.0f);
    const Field one = estimateOrFail(a, b, Tier::Fine);
    const Field two = estimateOrFail(a, b, Tier::Fine);
    CHECK(one.dx == two.dx);
    CHECK(one.dy == two.dy);
    CHECK(one.confidence == two.confidence);
    CHECK(one.sceneCut == two.sceneCut);
    Image x;
    Image y;
    interpolateImage(a, b, one, 0.37f, x);
    interpolateImage(a, b, two, 0.37f, y);
    CHECK(x.rgba == y.rgba);
}

}

void setDataDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void testSettingsJson() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_flow_settings";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    setDataDirectory(dir);

    nvj::Settings defaults;
    CHECK(defaults.flowMode == "off");
    CHECK(defaults.flowQuality == "balanced");
    CHECK(defaults.flowBudgetMb == 256);
    CHECK(defaults.flowShutterPercent == 50);
    CHECK(nvj::saveSettings(defaults));
    const nlohmann::json plain = nlohmann::json::parse(readText(dir / "settings.json"));
    for (const char* key : {"flowMode", "flowQuality", "flowBudgetMb", "flowShutterPercent"}) {
        CHECK(!plain.contains(key));
    }

    nvj::Settings changed;
    changed.flowMode = "interpolate";
    changed.flowQuality = "fine";
    changed.flowBudgetMb = 512;
    changed.flowShutterPercent = 80;
    CHECK(nvj::saveSettings(changed));
    nvj::Settings loaded;
    CHECK(nvj::loadSettings(loaded));
    CHECK(loaded.flowMode == "interpolate");
    CHECK(loaded.flowQuality == "fine");
    CHECK(loaded.flowBudgetMb == 512);
    CHECK(loaded.flowShutterPercent == 80);

    {
        std::ofstream out(dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << R"({"flowMode":"magic","flowQuality":7,"flowBudgetMb":99999,"flowShutterPercent":-30})";
    }
    nvj::Settings repaired;
    CHECK(nvj::loadSettings(repaired));
    CHECK(repaired.flowMode == "off");
    CHECK(repaired.flowQuality == "balanced");
    CHECK(repaired.flowBudgetMb == kMaxBudgetMb);
    CHECK(repaired.flowShutterPercent == 0);

    {
        std::ofstream out(dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << R"({"flowMode":3,"flowQuality":"fast","flowBudgetMb":"big","flowShutterPercent":12.5})";
    }
    nvj::Settings wrongTypes;
    CHECK(nvj::loadSettings(wrongTypes));
    CHECK(wrongTypes.flowMode == "off");
    CHECK(wrongTypes.flowQuality == "fast");
    CHECK(wrongTypes.flowBudgetMb == 256);
    CHECK(wrongTypes.flowShutterPercent == 50);

    {
        std::ofstream out(dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << R"({"flowBudgetMb":1})";
    }
    nvj::Settings low;
    CHECK(nvj::loadSettings(low));
    CHECK(low.flowBudgetMb == kMinBudgetMb);

#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", "");
#else
    unsetenv("NYREXVJ_DATA_DIR");
#endif
    std::filesystem::remove_all(dir, ec);
}

int main() {
    testNamesAndLimits();
    testAnalysisSize();
    testLuma();
    testEstimateRejectsBadInput();
    testEstimateTranslation();
    testEstimateFastMotion();
    testFlatAndStatic();
    testSceneCut();
    testSceneCutBetweenTexturedShots();
    testFadesAndDissolves();
    testPlanPair();
    testGovernor();
    testBudget();
    testSampling();
    testInterpolation30To60();
    testInterpolationFallbacks();
    testInterpolationAlpha();
    testMotionBlurAndVectors();
    testLayeredMotion();
    testFrameDrops();
    testSequenceAndCuts();
    testLoopBoundary();
    testPerformanceBaseline();
    testDeterminism();
    testSettingsJson();
    return testing::finish();
}

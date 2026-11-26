#include "core/optical_flow.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace nvj::flow {

namespace {

constexpr int kLambda = 3;
constexpr int kMinTopWidth = 56;
constexpr float kFlatTexture = 2.0f;
constexpr float kTexturedBlock = 4.0f;
constexpr float kTexturedGoodCost = 10.0f;
constexpr float kCostLow = 6.0f;
constexpr float kCostHigh = 30.0f;
constexpr double kBudgetShare = 0.75;
constexpr int kRecentWindow = 6;
constexpr int kRecentOver = 4;
constexpr int kCalmNeeded = 90;
constexpr std::size_t kHistoryLimit = 64;

float smoothStep(float low, float high, float x) {
    const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

template <class Fetch>
Luma resample(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight, Fetch fetch) {
    Luma out;
    if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
        return out;
    }
    out.width = targetWidth;
    out.height = targetHeight;
    out.pixels.assign(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight), 0);
    std::vector<int> xs(static_cast<std::size_t>(targetWidth) + 1);
    std::vector<int> ys(static_cast<std::size_t>(targetHeight) + 1);
    for (int i = 0; i <= targetWidth; ++i) {
        xs[static_cast<std::size_t>(i)] = static_cast<int>(static_cast<long long>(i) * sourceWidth / targetWidth);
    }
    for (int i = 0; i <= targetHeight; ++i) {
        ys[static_cast<std::size_t>(i)] = static_cast<int>(static_cast<long long>(i) * sourceHeight / targetHeight);
    }
    std::vector<unsigned long long> sums(static_cast<std::size_t>(targetWidth));
    for (int ty = 0; ty < targetHeight; ++ty) {
        const int ylow = std::min(ys[static_cast<std::size_t>(ty)], sourceHeight - 1);
        const int yhigh = std::min(std::max(ys[static_cast<std::size_t>(ty) + 1], ylow + 1), sourceHeight);
        std::fill(sums.begin(), sums.end(), 0ull);
        for (int sy = ylow; sy < yhigh; ++sy) {
            for (int tx = 0; tx < targetWidth; ++tx) {
                const int xlow = std::min(xs[static_cast<std::size_t>(tx)], sourceWidth - 1);
                const int xhigh = std::min(std::max(xs[static_cast<std::size_t>(tx) + 1], xlow + 1), sourceWidth);
                unsigned sum = 0;
                for (int sx = xlow; sx < xhigh; ++sx) {
                    sum += fetch(sx, sy);
                }
                sums[static_cast<std::size_t>(tx)] += sum;
            }
        }
        for (int tx = 0; tx < targetWidth; ++tx) {
            const int xlow = std::min(xs[static_cast<std::size_t>(tx)], sourceWidth - 1);
            const int xhigh = std::min(std::max(xs[static_cast<std::size_t>(tx) + 1], xlow + 1), sourceWidth);
            const unsigned long long count = static_cast<unsigned long long>(xhigh - xlow) * static_cast<unsigned long long>(yhigh - ylow);
            out.pixels[static_cast<std::size_t>(ty) * static_cast<std::size_t>(targetWidth) + static_cast<std::size_t>(tx)] = static_cast<std::uint8_t>((sums[static_cast<std::size_t>(tx)] + count / 2) / count);
        }
    }
    return out;
}

unsigned sampleValue(const std::uint8_t* p, int bytes, int shift) {
    if (bytes >= 2) {
        const unsigned value = static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
        return std::min<unsigned>(255u, value >> shift);
    }
    return p[0];
}

struct Level {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

Level halve(const Level& source) {
    Level out;
    out.width = std::max(source.width / 2, 1);
    out.height = std::max(source.height / 2, 1);
    out.pixels.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
    for (int y = 0; y < out.height; ++y) {
        const int y0 = std::min(y * 2, source.height - 1);
        const int y1 = std::min(y * 2 + 1, source.height - 1);
        for (int x = 0; x < out.width; ++x) {
            const int x0 = std::min(x * 2, source.width - 1);
            const int x1 = std::min(x * 2 + 1, source.width - 1);
            const int sum = source.pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(x0)] + source.pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(x1)] + source.pixels[static_cast<std::size_t>(y1) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(x0)] + source.pixels[static_cast<std::size_t>(y1) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(x1)];
            out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)] = static_cast<std::uint8_t>((sum + 2) / 4);
        }
    }
    return out;
}

int blockSad(const Level& a, const Level& b, int x0, int y0, int bw, int bh, int dx, int dy, int limit) {
    int sum = 0;
    const bool inside = x0 + dx >= 0 && y0 + dy >= 0 && x0 + dx + bw <= b.width && y0 + dy + bh <= b.height;
    for (int j = 0; j < bh; ++j) {
        const std::uint8_t* pa = &a.pixels[static_cast<std::size_t>(y0 + j) * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(x0)];
        if (inside) {
            const std::uint8_t* pb = &b.pixels[static_cast<std::size_t>(y0 + j + dy) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(x0 + dx)];
            for (int i = 0; i < bw; ++i) {
                sum += std::abs(static_cast<int>(pa[i]) - static_cast<int>(pb[i]));
            }
        } else {
            const int yy = std::clamp(y0 + j + dy, 0, b.height - 1);
            const std::uint8_t* row = &b.pixels[static_cast<std::size_t>(yy) * static_cast<std::size_t>(b.width)];
            for (int i = 0; i < bw; ++i) {
                const int xx = std::clamp(x0 + i + dx, 0, b.width - 1);
                sum += std::abs(static_cast<int>(pa[i]) - static_cast<int>(row[xx]));
            }
        }
        if (sum >= limit) {
            return sum;
        }
    }
    return sum;
}

struct Grid {
    int cols = 0;
    int rows = 0;
    std::vector<int> dx;
    std::vector<int> dy;
    std::vector<int> sad;
    std::vector<float> fx;
    std::vector<float> fy;
};

struct Search {
    const Level& a;
    const Level& b;
    int x0;
    int y0;
    int bw;
    int bh;
    int predictX;
    int predictY;
    int maxShift;
    int bestDx = 0;
    int bestDy = 0;
    int bestSad = std::numeric_limits<int>::max();
    int bestCost = std::numeric_limits<int>::max();

    bool test(int dx, int dy) {
        if (std::abs(dx) > maxShift || std::abs(dy) > maxShift) {
            return false;
        }
        const int penalty = kLambda * (std::abs(dx - predictX) + std::abs(dy - predictY));
        if (bestCost != std::numeric_limits<int>::max() && penalty >= bestCost) {
            return false;
        }
        const int limit = bestCost == std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : bestCost - penalty;
        const int sad = blockSad(a, b, x0, y0, bw, bh, dx, dy, limit);
        const int cost = sad + penalty;
        if (cost < bestCost) {
            bestCost = cost;
            bestSad = sad;
            bestDx = dx;
            bestDy = dy;
            return true;
        }
        return false;
    }

    void refine(int steps) {
        for (int step = 0; step < steps; ++step) {
            const int cx = bestDx;
            const int cy = bestDy;
            bool moved = false;
            for (int j = -1; j <= 1; ++j) {
                for (int i = -1; i <= 1; ++i) {
                    if ((i != 0 || j != 0) && test(cx + i, cy + j)) {
                        moved = true;
                    }
                }
            }
            if (!moved) {
                break;
            }
        }
    }
};

Grid searchLevel(const Level& a, const Level& b, int block, int radius, int refineSteps, const Grid* parent, int maxShift) {
    Grid grid;
    grid.cols = (a.width + block - 1) / block;
    grid.rows = (a.height + block - 1) / block;
    const std::size_t count = static_cast<std::size_t>(grid.cols) * static_cast<std::size_t>(grid.rows);
    grid.dx.assign(count, 0);
    grid.dy.assign(count, 0);
    grid.sad.assign(count, 0);
    for (int by = 0; by < grid.rows; ++by) {
        for (int bx = 0; bx < grid.cols; ++bx) {
            const std::size_t index = static_cast<std::size_t>(by) * static_cast<std::size_t>(grid.cols) + static_cast<std::size_t>(bx);
            const int x0 = bx * block;
            const int y0 = by * block;
            const int bw = std::min(block, a.width - x0);
            const int bh = std::min(block, a.height - y0);
            int predictX = 0;
            int predictY = 0;
            if (parent != nullptr) {
                const int px = std::min(bx / 2, parent->cols - 1);
                const int py = std::min(by / 2, parent->rows - 1);
                const std::size_t pi = static_cast<std::size_t>(py) * static_cast<std::size_t>(parent->cols) + static_cast<std::size_t>(px);
                predictX = parent->dx[pi] * 2;
                predictY = parent->dy[pi] * 2;
            }
            Search search{a, b, x0, y0, bw, bh, predictX, predictY, maxShift};
            if (parent == nullptr) {
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        search.test(dx, dy);
                    }
                }
            } else {
                search.test(predictX, predictY);
                search.test(0, 0);
                if (bx > 0) {
                    search.test(grid.dx[index - 1], grid.dy[index - 1]);
                }
                if (by > 0) {
                    search.test(grid.dx[index - static_cast<std::size_t>(grid.cols)], grid.dy[index - static_cast<std::size_t>(grid.cols)]);
                    if (bx + 1 < grid.cols) {
                        search.test(grid.dx[index - static_cast<std::size_t>(grid.cols) + 1], grid.dy[index - static_cast<std::size_t>(grid.cols) + 1]);
                    }
                }
                search.refine(refineSteps);
            }
            grid.dx[index] = search.bestDx;
            grid.dy[index] = search.bestDy;
            grid.sad[index] = search.bestSad;
        }
    }
    return grid;
}

Grid searchPyramid(const std::vector<Level>& pa, const std::vector<Level>& pb, const TierSpec& spec) {
    const int top = static_cast<int>(pa.size()) - 1;
    Grid grid;
    bool first = true;
    for (int level = top; level >= 0; --level) {
        const int maxShift = std::max(spec.radius * (1 << (top - level)) + spec.refine * 2, spec.radius);
        const Level& a = pa[static_cast<std::size_t>(level)];
        const Level& b = pb[static_cast<std::size_t>(level)];
        Grid next = searchLevel(a, b, spec.block, spec.radius, spec.refine, first ? nullptr : &grid, maxShift);
        grid = std::move(next);
        first = false;
    }
    const Level& a = pa[0];
    const Level& b = pb[0];
    const std::size_t count = grid.dx.size();
    grid.fx.assign(count, 0.0f);
    grid.fy.assign(count, 0.0f);
    for (int by = 0; by < grid.rows; ++by) {
        for (int bx = 0; bx < grid.cols; ++bx) {
            const std::size_t index = static_cast<std::size_t>(by) * static_cast<std::size_t>(grid.cols) + static_cast<std::size_t>(bx);
            const int x0 = bx * spec.block;
            const int y0 = by * spec.block;
            const int bw = std::min(spec.block, a.width - x0);
            const int bh = std::min(spec.block, a.height - y0);
            const int dx = grid.dx[index];
            const int dy = grid.dy[index];
            const int huge = std::numeric_limits<int>::max();
            const float c0 = static_cast<float>(grid.sad[index]);
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            const float left = static_cast<float>(blockSad(a, b, x0, y0, bw, bh, dx - 1, dy, huge));
            const float right = static_cast<float>(blockSad(a, b, x0, y0, bw, bh, dx + 1, dy, huge));
            const float denominatorX = left - 2.0f * c0 + right;
            if (c0 > 0.0f && denominatorX > 1.0e-3f) {
                offsetX = std::clamp(0.5f * (left - right) / denominatorX, -0.5f, 0.5f);
            }
            const float up = static_cast<float>(blockSad(a, b, x0, y0, bw, bh, dx, dy - 1, huge));
            const float down = static_cast<float>(blockSad(a, b, x0, y0, bw, bh, dx, dy + 1, huge));
            const float denominatorY = up - 2.0f * c0 + down;
            if (c0 > 0.0f && denominatorY > 1.0e-3f) {
                offsetY = std::clamp(0.5f * (up - down) / denominatorY, -0.5f, 0.5f);
            }
            grid.fx[index] = static_cast<float>(dx) + offsetX;
            grid.fy[index] = static_cast<float>(dy) + offsetY;
        }
    }
    return grid;
}

std::vector<Level> buildPyramid(const Luma& image, int levels, int block) {
    std::vector<Level> pyramid;
    Level base;
    base.width = image.width;
    base.height = image.height;
    base.pixels = image.pixels;
    pyramid.push_back(std::move(base));
    while (static_cast<int>(pyramid.size()) < levels && pyramid.back().width / 2 >= std::max(block, kMinTopWidth) && pyramid.back().height / 2 >= block) {
        pyramid.push_back(halve(pyramid.back()));
    }
    return pyramid;
}

float blockTexture(const Level& a, int x0, int y0, int bw, int bh) {
    int sum = 0;
    for (int j = 0; j < bh; ++j) {
        for (int i = 0; i < bw; ++i) {
            sum += a.pixels[static_cast<std::size_t>(y0 + j) * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(x0 + i)];
        }
    }
    const float mean = static_cast<float>(sum) / static_cast<float>(bw * bh);
    float deviation = 0.0f;
    for (int j = 0; j < bh; ++j) {
        for (int i = 0; i < bw; ++i) {
            deviation += std::abs(static_cast<float>(a.pixels[static_cast<std::size_t>(y0 + j) * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(x0 + i)]) - mean);
        }
    }
    return deviation / static_cast<float>(bw * bh);
}

}

const char* modeName(Mode mode) {
    switch (mode) {
    case Mode::Interpolate:
        return "interpolate";
    case Mode::MotionBlur:
        return "blur";
    case Mode::Vectors:
        return "vectors";
    default:
        return "off";
    }
}

Mode modeFromName(const std::string& name, Mode fallback) {
    if (name == "off") {
        return Mode::Off;
    }
    if (name == "interpolate") {
        return Mode::Interpolate;
    }
    if (name == "blur") {
        return Mode::MotionBlur;
    }
    if (name == "vectors") {
        return Mode::Vectors;
    }
    return fallback;
}

const char* tierName(Tier tier) {
    switch (tier) {
    case Tier::Fast:
        return "fast";
    case Tier::Fine:
        return "fine";
    default:
        return "balanced";
    }
}

Tier tierFromName(const std::string& name, Tier fallback) {
    if (name == "fast") {
        return Tier::Fast;
    }
    if (name == "balanced") {
        return Tier::Balanced;
    }
    if (name == "fine") {
        return Tier::Fine;
    }
    return fallback;
}

Tier clampTier(int value) {
    return static_cast<Tier>(std::clamp(value, 0, kTierCount - 1));
}

int clampBudgetMb(int value) {
    return std::clamp(value, kMinBudgetMb, kMaxBudgetMb);
}

Settings sanitised(const Settings& settings) {
    Settings out = settings;
    out.mode = static_cast<Mode>(std::clamp(static_cast<int>(settings.mode), 0, kModeCount - 1));
    out.tier = clampTier(static_cast<int>(settings.tier));
    out.budgetMb = clampBudgetMb(settings.budgetMb);
    out.shutter = std::isfinite(settings.shutter) ? std::clamp(settings.shutter, 0.0f, 1.0f) : 0.5f;
    return out;
}

const TierSpec& tierSpec(Tier tier) {
    static const TierSpec specs[kTierCount] = {
        {160, 8, 4, 3, 2, false},
        {240, 8, 5, 3, 3, false},
        {360, 8, 5, 4, 4, true},
    };
    return specs[static_cast<std::size_t>(clampTier(static_cast<int>(tier)))];
}

void analysisSize(int sourceWidth, int sourceHeight, Tier tier, int& width, int& height) {
    const TierSpec& spec = tierSpec(tier);
    const int block = spec.block;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        width = spec.analysisWidth;
        height = block * 2;
        return;
    }
    width = spec.analysisWidth;
    if (sourceWidth < width) {
        width = std::max(block * 4, (sourceWidth / block) * block);
    }
    const double scaled = static_cast<double>(width) * static_cast<double>(sourceHeight) / static_cast<double>(sourceWidth);
    height = (static_cast<int>(std::lround(scaled)) + block / 2) / block * block;
    height = std::clamp(height, block * 2, kMaxFlowRows * block);
}

Luma lumaFromRgba(const std::uint8_t* rgba, int width, int height, int stride, int targetWidth, int targetHeight) {
    if (rgba == nullptr || width <= 0 || height <= 0 || stride < width * 4) {
        return Luma();
    }
    return resample(width, height, targetWidth, targetHeight, [&](int x, int y) -> unsigned {
        const std::uint8_t* p = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x) * 4u;
        const unsigned luma = (54u * p[0] + 183u * p[1] + 19u * p[2] + 128u) >> 8;
        const unsigned alpha = p[3];
        if (alpha == 255u) {
            return luma;
        }
        return (luma * alpha + 127u) / 255u;
    });
}

Luma lumaFromPlane(const std::uint8_t* plane, int width, int height, int stride, int bytesPerSample, int bitDepth, bool msbAligned, const std::uint8_t* alpha, int alphaStride, int alphaBytes, int targetWidth, int targetHeight) {
    if (plane == nullptr || width <= 0 || height <= 0 || bytesPerSample < 1 || bytesPerSample > 2 || stride < width * bytesPerSample) {
        return Luma();
    }
    const int shift = bytesPerSample >= 2 ? (msbAligned ? 8 : std::max(bitDepth - 8, 0)) : 0;
    const bool withAlpha = alpha != nullptr && alphaBytes >= 1 && alphaBytes <= 2 && alphaStride >= width * alphaBytes;
    return resample(width, height, targetWidth, targetHeight, [&](int x, int y) -> unsigned {
        const unsigned luma = sampleValue(plane + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x) * static_cast<std::size_t>(bytesPerSample), bytesPerSample, shift);
        if (!withAlpha) {
            return luma;
        }
        const int alphaShift = alphaBytes >= 2 ? (msbAligned ? 8 : std::max(bitDepth - 8, 0)) : 0;
        const unsigned a = sampleValue(alpha + static_cast<std::size_t>(y) * static_cast<std::size_t>(alphaStride) + static_cast<std::size_t>(x) * static_cast<std::size_t>(alphaBytes), alphaBytes, alphaShift);
        if (a >= 255u) {
            return luma;
        }
        return (luma * a + 127u) / 255u;
    });
}

Luma lumaFromUyvy(const std::uint8_t* data, int width, int height, int stride, int targetWidth, int targetHeight) {
    if (data == nullptr || width <= 0 || height <= 0 || stride < width * 2) {
        return Luma();
    }
    return resample(width, height, targetWidth, targetHeight, [&](int x, int y) -> unsigned {
        return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x) * 2u + 1u];
    });
}

bool estimate(const Luma& a, const Luma& b, Tier tier, Field& field, std::string* error) {
    field = Field();
    const TierSpec& spec = tierSpec(tier);
    if (!a.valid() || !b.valid()) {
        if (error != nullptr) {
            *error = "the analysis pictures are empty";
        }
        return false;
    }
    if (a.width != b.width || a.height != b.height) {
        if (error != nullptr) {
            *error = "the two analysis pictures differ in size";
        }
        return false;
    }
    if (a.width < spec.block * 2 || a.height < spec.block * 2 || a.width % spec.block != 0 || a.height % spec.block != 0) {
        if (error != nullptr) {
            *error = "the analysis picture is too small or not a multiple of the block size";
        }
        return false;
    }
    const std::vector<Level> pa = buildPyramid(a, spec.levels, spec.block);
    const std::vector<Level> pb = buildPyramid(b, spec.levels, spec.block);
    const Grid forward = searchPyramid(pa, pb, spec);
    Grid backward;
    if (spec.bidirectional) {
        backward = searchPyramid(pb, pa, spec);
    }
    const int cols = forward.cols;
    const int rows = forward.rows;
    const std::size_t count = forward.dx.size();
    field.cols = cols;
    field.rows = rows;
    field.block = spec.block;
    field.imageWidth = a.width;
    field.imageHeight = a.height;
    field.dx = forward.fx;
    field.dy = forward.fy;
    field.confidence.assign(count, 1.0f);

    const Level& base = pa[0];
    std::vector<float> texture(count, 0.0f);
    std::vector<float> rawTexture;
    std::vector<float> cost(count, 0.0f);
    double costSum = 0.0;
    double zeroSum = 0.0;
    for (int by = 0; by < rows; ++by) {
        for (int bx = 0; bx < cols; ++bx) {
            const std::size_t index = static_cast<std::size_t>(by) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(bx);
            const int x0 = bx * spec.block;
            const int y0 = by * spec.block;
            const int bw = std::min(spec.block, a.width - x0);
            const int bh = std::min(spec.block, a.height - y0);
            const float pixels = static_cast<float>(bw * bh);
            texture[index] = blockTexture(base, x0, y0, bw, bh);
            cost[index] = static_cast<float>(forward.sad[index]) / pixels;
            const int zero = blockSad(pa[0], pb[0], x0, y0, bw, bh, 0, 0, std::numeric_limits<int>::max());
            zeroSum += static_cast<double>(zero) / static_cast<double>(pixels);
            costSum += cost[index];
        }
    }

    rawTexture = texture;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> nextX = field.dx;
        std::vector<float> nextY = field.dy;
        for (int by = 0; by < rows; ++by) {
            for (int bx = 0; bx < cols; ++bx) {
                const std::size_t index = static_cast<std::size_t>(by) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(bx);
                if (texture[index] >= kFlatTexture) {
                    continue;
                }
                float weight = 0.0f;
                float sumX = 0.0f;
                float sumY = 0.0f;
                for (int j = -1; j <= 1; ++j) {
                    for (int i = -1; i <= 1; ++i) {
                        const int nx = bx + i;
                        const int ny = by + j;
                        if ((i == 0 && j == 0) || nx < 0 || ny < 0 || nx >= cols || ny >= rows) {
                            continue;
                        }
                        const std::size_t ni = static_cast<std::size_t>(ny) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(nx);
                        if (texture[ni] < kFlatTexture) {
                            continue;
                        }
                        weight += texture[ni];
                        sumX += texture[ni] * field.dx[ni];
                        sumY += texture[ni] * field.dy[ni];
                    }
                }
                if (weight > 0.0f) {
                    nextX[index] = sumX / weight;
                    nextY[index] = sumY / weight;
                    texture[index] = std::max(texture[index], 0.0f);
                }
            }
        }
        field.dx = std::move(nextX);
        field.dy = std::move(nextY);
    }

    int confident = 0;
    double magnitudeSum = 0.0;
    float magnitudeMax = 0.0f;
    for (int by = 0; by < rows; ++by) {
        for (int bx = 0; bx < cols; ++bx) {
            const std::size_t index = static_cast<std::size_t>(by) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(bx);
            float confidence = 1.0f - smoothStep(kCostLow, kCostHigh, cost[index]);
            if (spec.bidirectional) {
                const float centerX = (static_cast<float>(bx) + 0.5f) * static_cast<float>(spec.block) + field.dx[index];
                const float centerY = (static_cast<float>(by) + 0.5f) * static_cast<float>(spec.block) + field.dy[index];
                const int tx = std::clamp(static_cast<int>(std::floor(centerX / static_cast<float>(spec.block))), 0, cols - 1);
                const int ty = std::clamp(static_cast<int>(std::floor(centerY / static_cast<float>(spec.block))), 0, rows - 1);
                const std::size_t ti = static_cast<std::size_t>(ty) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(tx);
                const float errorX = field.dx[index] + backward.fx[ti];
                const float errorY = field.dy[index] + backward.fy[ti];
                const float error = std::sqrt(errorX * errorX + errorY * errorY);
                confidence *= 1.0f - smoothStep(1.0f, 3.5f, error);
            }
            field.confidence[index] = std::clamp(confidence, 0.0f, 1.0f);
            if (field.confidence[index] > 0.5f) {
                ++confident;
            }
            const float magnitude = std::sqrt(field.dx[index] * field.dx[index] + field.dy[index] * field.dy[index]);
            magnitudeSum += magnitude;
            magnitudeMax = std::max(magnitudeMax, magnitude);
        }
    }
    field.matchedCost = static_cast<float>(costSum / static_cast<double>(count));
    field.zeroCost = static_cast<float>(zeroSum / static_cast<double>(count));
    field.confidentFraction = static_cast<float>(confident) / static_cast<float>(count);
    field.meanMagnitude = static_cast<float>(magnitudeSum / static_cast<double>(count));
    field.maxMagnitude = magnitudeMax;
    int textured = 0;
    int texturedGood = 0;
    double texturedSum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (rawTexture[i] >= kTexturedBlock) {
            ++textured;
            texturedSum += cost[i];
            if (cost[i] < kTexturedGoodCost) {
                ++texturedGood;
            }
        }
    }
    field.texturedFraction = static_cast<float>(textured) / static_cast<float>(count);
    field.texturedCost = textured > 0 ? static_cast<float>(texturedSum / static_cast<double>(textured)) : 0.0f;
    field.texturedMatched = textured > 0 ? static_cast<float>(texturedGood) / static_cast<float>(textured) : 1.0f;
    const bool unmatched = field.texturedFraction >= kCutTextured && field.texturedMatched < kCutMatched;
    field.sceneCut = unmatched || (field.matchedCost > kCutCost && field.confidentFraction < kCutConfident);
    return true;
}

PairPlan planPair(double seconds, double timeA, double timeB, double nominalPeriod, bool reverse) {
    PairPlan plan;
    if (reverse) {
        plan.reason = "reverse playback is not interpolated";
        return plan;
    }
    const double period = nominalPeriod > 1.0e-4 ? nominalPeriod : 1.0 / 30.0;
    const double gap = timeB - timeA;
    if (!(gap > 1.0e-6)) {
        plan.reason = "the frame times are not in order";
        return plan;
    }
    if (gap > period * 2.5) {
        plan.reason = "there is a gap in the frame times";
        return plan;
    }
    const double tolerance = std::max(period * 0.01, 1.0e-4);
    if (seconds < timeA - tolerance || seconds > timeB + tolerance) {
        plan.reason = "the time lies outside of the two frames";
        return plan;
    }
    const double t = std::clamp((seconds - timeA) / gap, 0.0, 1.0);
    plan.usable = true;
    plan.t = static_cast<float>(t);
    plan.exact = t < 0.004 || t > 0.996;
    return plan;
}

Governor::Governor(Tier requested) {
    setRequested(requested);
}

void Governor::setRequested(Tier requested) {
    requested_ = requested;
    current_ = requested;
    disabled_ = false;
    recent_.clear();
    calm_ = 0;
}

void Governor::reset() {
    history_.clear();
    recent_.clear();
    calm_ = 0;
    lowered_ = 0;
    current_ = requested_;
    disabled_ = false;
}

Governor::Decision Governor::record(double analysisMs, double periodSeconds) {
    history_.push_back(analysisMs);
    if (history_.size() > kHistoryLimit) {
        history_.erase(history_.begin());
    }
    if (disabled_) {
        return Decision::Disable;
    }
    const double period = periodSeconds > 1.0e-4 ? periodSeconds : 1.0 / 30.0;
    const double budget = period * 1000.0 * kBudgetShare;
    recent_.push_back(analysisMs);
    if (static_cast<int>(recent_.size()) > kRecentWindow) {
        recent_.erase(recent_.begin());
    }
    if (static_cast<int>(recent_.size()) == kRecentWindow) {
        int over = 0;
        for (const double value : recent_) {
            if (value > budget) {
                ++over;
            }
        }
        if (over >= kRecentOver) {
            recent_.clear();
            calm_ = 0;
            if (current_ != Tier::Fast) {
                current_ = static_cast<Tier>(static_cast<int>(current_) - 1);
                ++lowered_;
                return Decision::Lower;
            }
            disabled_ = true;
            return Decision::Disable;
        }
    }
    if (analysisMs < budget * 0.35) {
        ++calm_;
    } else {
        calm_ = 0;
    }
    if (calm_ >= kCalmNeeded && static_cast<int>(current_) < static_cast<int>(requested_)) {
        current_ = static_cast<Tier>(static_cast<int>(current_) + 1);
        calm_ = 0;
        recent_.clear();
        return Decision::Raise;
    }
    return Decision::Keep;
}

double Governor::p95() const {
    if (history_.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = history_;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t index = std::min(sorted.size() - 1, static_cast<std::size_t>(std::ceil(static_cast<double>(sorted.size()) * 0.95)) - 1u);
    return sorted[index];
}

double Governor::average() const {
    if (history_.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : history_) {
        sum += value;
    }
    return sum / static_cast<double>(history_.size());
}

std::uint64_t estimateBytes(Mode mode, int width, int height) {
    if (mode == Mode::Off || width <= 0 || height <= 0) {
        return 0;
    }
    const std::uint64_t frame = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
    const std::uint64_t slots = mode == Mode::Interpolate ? 2ull : 1ull;
    const std::uint64_t flowTexture = static_cast<std::uint64_t>(kMaxFlowCols) * static_cast<std::uint64_t>(kMaxFlowRows) * 8ull;
    return (slots + 1ull) * frame + flowTexture;
}

void Budget::setLimit(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    limit_ = bytes;
}

std::uint64_t Budget::limit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return limit_;
}

bool Budget::reserve(const void* owner, std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t others = 0;
    for (const auto& entry : owners_) {
        if (entry.first != owner) {
            others += entry.second;
        }
    }
    if (others + bytes > limit_) {
        return false;
    }
    owners_[owner] = bytes;
    return true;
}

void Budget::release(const void* owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    owners_.erase(owner);
}

std::uint64_t Budget::used() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& entry : owners_) {
        sum += entry.second;
    }
    return sum;
}

std::size_t Budget::owners() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return owners_.size();
}

std::uint64_t Budget::reserved(const void* owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = owners_.find(owner);
    return found == owners_.end() ? 0ull : found->second;
}

Budget& globalBudget() {
    static Budget budget;
    return budget;
}

}

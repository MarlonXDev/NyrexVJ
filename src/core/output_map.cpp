#include "core/output_map.hpp"

#include "core/output_map_json.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace nvj {

namespace {

float clampf(float value, float low, float high) {
    if (!(value >= low)) {
        return low;
    }
    return value > high ? high : value;
}

struct Homography {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 0.0f, e = 1.0f, f = 0.0f, g = 0.0f, h = 0.0f;
};

Homography squareToQuad(const std::array<Vec2f, 4>& q) {
    Homography result;
    const float sx = q[0].x - q[1].x + q[2].x - q[3].x;
    const float sy = q[0].y - q[1].y + q[2].y - q[3].y;
    if (std::fabs(sx) < 1e-7f && std::fabs(sy) < 1e-7f) {
        result.a = q[1].x - q[0].x;
        result.b = q[3].x - q[0].x;
        result.c = q[0].x;
        result.d = q[1].y - q[0].y;
        result.e = q[3].y - q[0].y;
        result.f = q[0].y;
        return result;
    }
    const float dx1 = q[1].x - q[2].x;
    const float dx2 = q[3].x - q[2].x;
    const float dy1 = q[1].y - q[2].y;
    const float dy2 = q[3].y - q[2].y;
    const float den = dx1 * dy2 - dx2 * dy1;
    if (std::fabs(den) < 1e-9f) {
        result.a = q[1].x - q[0].x;
        result.b = q[3].x - q[0].x;
        result.c = q[0].x;
        result.d = q[1].y - q[0].y;
        result.e = q[3].y - q[0].y;
        result.f = q[0].y;
        return result;
    }
    result.g = (sx * dy2 - dx2 * sy) / den;
    result.h = (dx1 * sy - sx * dy1) / den;
    result.a = q[1].x - q[0].x + result.g * q[1].x;
    result.b = q[3].x - q[0].x + result.h * q[3].x;
    result.c = q[0].x;
    result.d = q[1].y - q[0].y + result.g * q[1].y;
    result.e = q[3].y - q[0].y + result.h * q[3].y;
    result.f = q[0].y;
    return result;
}

Vec2f applyHomography(const Homography& m, float s, float t) {
    const float w = m.g * s + m.h * t + 1.0f;
    const float inv = std::fabs(w) < 1e-7f ? 1.0f : 1.0f / w;
    return {(m.a * s + m.b * t + m.c) * inv, (m.d * s + m.e * t + m.f) * inv};
}

Vec2f catmull(const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, const Vec2f& p3, float u) {
    const float u2 = u * u;
    const float u3 = u2 * u;
    const auto channel = [&](float a, float b, float c, float d) {
        return 0.5f * ((2.0f * b) + (-a + c) * u + (2.0f * a - 5.0f * b + 4.0f * c - d) * u2 + (-a + 3.0f * b - 3.0f * c + d) * u3);
    };
    return {channel(p0.x, p1.x, p2.x, p3.x), channel(p0.y, p1.y, p2.y, p3.y)};
}

Vec2f pointAt(const std::vector<Vec2f>& grid, int columns, int rows, int column, int row) {
    const int c = std::clamp(column, 0, columns - 1);
    const int r = std::clamp(row, 0, rows - 1);
    return grid[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c)];
}

Vec2f rowSample(const std::vector<Vec2f>& grid, int columns, int rows, int row, float x) {
    const int base = std::clamp(static_cast<int>(std::floor(x)), 0, columns - 2);
    const float u = x - static_cast<float>(base);
    const auto at = [&](int column) -> Vec2f {
        if (column < 0) {
            const Vec2f a = pointAt(grid, columns, rows, 0, row);
            const Vec2f b = pointAt(grid, columns, rows, 1, row);
            return {2.0f * a.x - b.x, 2.0f * a.y - b.y};
        }
        if (column > columns - 1) {
            const Vec2f a = pointAt(grid, columns, rows, columns - 1, row);
            const Vec2f b = pointAt(grid, columns, rows, columns - 2, row);
            return {2.0f * a.x - b.x, 2.0f * a.y - b.y};
        }
        return pointAt(grid, columns, rows, column, row);
    };
    return catmull(at(base - 1), at(base), at(base + 1), at(base + 2), u);
}

}

void resizeWarp(OutputMapping& map, int columns, int rows) {
    columns = std::clamp(columns, kMinWarpPoints, kMaxWarpPoints);
    rows = std::clamp(rows, kMinWarpPoints, kMaxWarpPoints);
    std::vector<Vec2f> grid(static_cast<size_t>(columns) * static_cast<size_t>(rows));
    const bool same = static_cast<int>(map.warp.size()) == map.warpColumns * map.warpRows && map.warpColumns >= kMinWarpPoints && map.warpRows >= kMinWarpPoints;
    if (same) {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                const float s = static_cast<float>(c) / static_cast<float>(columns - 1);
                const float t = static_cast<float>(r) / static_cast<float>(rows - 1);
                grid[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c)] = warpOffsetAt(map, s, t);
            }
        }
    }
    map.warpColumns = columns;
    map.warpRows = rows;
    map.warp = std::move(grid);
}

void normalizeMask(SliceMask& mask) {
    if (mask.points.size() > static_cast<size_t>(kMaxMaskPoints)) {
        mask.points.resize(static_cast<size_t>(kMaxMaskPoints));
    }
    for (Vec2f& point : mask.points) {
        point.x = clampf(point.x, -0.5f, 1.5f);
        point.y = clampf(point.y, -0.5f, 1.5f);
    }
    mask.feather = clampf(mask.feather, 0.0f, kMaxMaskFeather);
    if (mask.points.size() < static_cast<size_t>(kMinMaskPoints)) {
        mask.enabled = false;
    }
}

void normalizeColor(OutputColor& color) {
    color.contrast = clampf(color.contrast, 0.25f, 2.0f);
    color.saturation = clampf(color.saturation, 0.0f, 2.0f);
    color.gamma = clampf(color.gamma, 0.4f, 2.5f);
    color.red = clampf(color.red, 0.0f, 1.5f);
    color.green = clampf(color.green, 0.0f, 1.5f);
    color.blue = clampf(color.blue, 0.0f, 1.5f);
    color.black = clampf(color.black, 0.0f, 0.3f);
}

bool isDefaultColor(const OutputColor& color) {
    OutputColor copy = color;
    normalizeColor(copy);
    return copy == OutputColor{};
}

void applyColor(const OutputColor& color, float rgb[3]) {
    OutputColor c = color;
    normalizeColor(c);
    float r = rgb[0] * c.red;
    float g = rgb[1] * c.green;
    float b = rgb[2] * c.blue;
    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    r = luma + (r - luma) * c.saturation;
    g = luma + (g - luma) * c.saturation;
    b = luma + (b - luma) * c.saturation;
    r = clampf((r - 0.5f) * c.contrast + 0.5f, 0.0f, 1.0f);
    g = clampf((g - 0.5f) * c.contrast + 0.5f, 0.0f, 1.0f);
    b = clampf((b - 0.5f) * c.contrast + 0.5f, 0.0f, 1.0f);
    const float inverse = 1.0f / c.gamma;
    r = std::pow(r, inverse);
    g = std::pow(g, inverse);
    b = std::pow(b, inverse);
    rgb[0] = c.black + (1.0f - c.black) * r;
    rgb[1] = c.black + (1.0f - c.black) * g;
    rgb[2] = c.black + (1.0f - c.black) * b;
}

int meshDivisionsFor(const OutputMapping& map) {
    const int points = std::max(map.warpColumns, map.warpRows);
    return std::clamp((points - 1) * 4, kMeshDivisions, kMaxMeshDivisions);
}

int sliceCount(const OutputSpec& spec) {
    return 1 + static_cast<int>(spec.slices.size());
}

const OutputMapping& sliceMapping(const OutputSpec& spec, int index) {
    if (index <= 0 || spec.slices.empty()) {
        return spec.map;
    }
    const size_t k = std::min(static_cast<size_t>(index - 1), spec.slices.size() - 1);
    return spec.slices[k].map;
}

OutputMapping& sliceMapping(OutputSpec& spec, int index) {
    if (index <= 0 || spec.slices.empty()) {
        return spec.map;
    }
    const size_t k = std::min(static_cast<size_t>(index - 1), spec.slices.size() - 1);
    return spec.slices[k].map;
}

std::string sliceName(const OutputSpec& spec, int index) {
    if (index <= 0) {
        return "Slice 1";
    }
    if (static_cast<size_t>(index - 1) < spec.slices.size()) {
        const std::string& name = spec.slices[static_cast<size_t>(index - 1)].name;
        if (!name.empty()) {
            return name;
        }
    }
    return "Slice " + std::to_string(index + 1);
}

bool addSlice(OutputSpec& spec) {
    if (sliceCount(spec) >= kMaxSlices) {
        return false;
    }
    OutputSlice slice;
    slice.name = "Slice " + std::to_string(sliceCount(spec) + 1);
    spec.slices.push_back(slice);
    return true;
}

bool removeSlice(OutputSpec& spec, int index) {
    if (spec.slices.empty() || index < 0 || index >= sliceCount(spec)) {
        return false;
    }
    if (index == 0) {
        const bool fill = spec.map.fill;
        spec.map = spec.slices.front().map;
        spec.map.fill = fill;
        spec.slices.erase(spec.slices.begin());
        return true;
    }
    spec.slices.erase(spec.slices.begin() + (index - 1));
    return true;
}

bool duplicateSlice(OutputSpec& spec, int index) {
    if (sliceCount(spec) >= kMaxSlices || index < 0 || index >= sliceCount(spec)) {
        return false;
    }
    OutputSlice copy;
    copy.map = sliceMapping(spec, index);
    copy.map.fill = false;
    copy.name = (sliceName(spec, index) + " copy").substr(0, 40);
    spec.slices.push_back(copy);
    return true;
}

void normalizeSpec(OutputSpec& spec) {
    normalizeMapping(spec.map);
    normalizeColor(spec.color);
    if (spec.slices.size() > static_cast<size_t>(kMaxSlices - 1)) {
        spec.slices.resize(static_cast<size_t>(kMaxSlices - 1));
    }
    for (size_t i = 0; i < spec.slices.size(); ++i) {
        OutputSlice& slice = spec.slices[i];
        if (slice.name.size() > 40) {
            slice.name.resize(40);
        }
        if (slice.name.empty()) {
            slice.name = "Slice " + std::to_string(i + 2);
        }
        normalizeMapping(slice.map);
    }
}

void normalizeMapping(OutputMapping& map) {
    map.slice[2] = clampf(map.slice[2], 0.02f, 1.0f);
    map.slice[3] = clampf(map.slice[3], 0.02f, 1.0f);
    map.slice[0] = clampf(map.slice[0], 0.0f, 1.0f - map.slice[2]);
    map.slice[1] = clampf(map.slice[1], 0.0f, 1.0f - map.slice[3]);
    for (Vec2f& corner : map.corners) {
        corner.x = clampf(corner.x, -0.5f, 1.5f);
        corner.y = clampf(corner.y, -0.5f, 1.5f);
    }
    map.warpColumns = std::clamp(map.warpColumns, kMinWarpPoints, kMaxWarpPoints);
    map.warpRows = std::clamp(map.warpRows, kMinWarpPoints, kMaxWarpPoints);
    const size_t wanted = static_cast<size_t>(map.warpColumns) * static_cast<size_t>(map.warpRows);
    if (map.warp.size() != wanted) {
        map.warp.assign(wanted, Vec2f{});
    }
    for (Vec2f& offset : map.warp) {
        offset.x = clampf(offset.x, -0.5f, 0.5f);
        offset.y = clampf(offset.y, -0.5f, 0.5f);
    }
    map.blend.left = clampf(map.blend.left, 0.0f, 0.5f);
    map.blend.right = clampf(map.blend.right, 0.0f, 0.5f);
    map.blend.top = clampf(map.blend.top, 0.0f, 0.5f);
    map.blend.bottom = clampf(map.blend.bottom, 0.0f, 0.5f);
    map.blend.power = clampf(map.blend.power, 1.0f, 4.0f);
    map.blend.gamma = clampf(map.blend.gamma, 1.0f, 3.0f);
    map.blend.blackLevel = clampf(map.blend.blackLevel, 0.0f, 0.3f);
    map.brightness = clampf(map.brightness, 0.0f, 1.0f);
    normalizeMask(map.mask);
}

bool isDefaultMapping(const OutputMapping& map) {
    OutputMapping fresh;
    OutputMapping copy = map;
    normalizeMapping(copy);
    return copy == fresh;
}

void resetCorners(OutputMapping& map) {
    map.corners = OutputMapping{}.corners;
}

void resetWarp(OutputMapping& map) {
    map.warp.assign(static_cast<size_t>(map.warpColumns) * static_cast<size_t>(map.warpRows), Vec2f{});
}

Vec2f warpOffsetAt(const OutputMapping& map, float s, float t) {
    const int columns = map.warpColumns;
    const int rows = map.warpRows;
    if (columns < kMinWarpPoints || rows < kMinWarpPoints || static_cast<int>(map.warp.size()) != columns * rows) {
        return {};
    }
    const float x = clampf(s, 0.0f, 1.0f) * static_cast<float>(columns - 1);
    const float y = clampf(t, 0.0f, 1.0f) * static_cast<float>(rows - 1);
    const int base = std::clamp(static_cast<int>(std::floor(y)), 0, rows - 2);
    const float v = y - static_cast<float>(base);
    const auto rowAt = [&](int row) -> Vec2f {
        if (row < 0) {
            const Vec2f a = rowSample(map.warp, columns, rows, 0, x);
            const Vec2f b = rowSample(map.warp, columns, rows, 1, x);
            return {2.0f * a.x - b.x, 2.0f * a.y - b.y};
        }
        if (row > rows - 1) {
            const Vec2f a = rowSample(map.warp, columns, rows, rows - 1, x);
            const Vec2f b = rowSample(map.warp, columns, rows, rows - 2, x);
            return {2.0f * a.x - b.x, 2.0f * a.y - b.y};
        }
        return rowSample(map.warp, columns, rows, row, x);
    };
    return catmull(rowAt(base - 1), rowAt(base), rowAt(base + 1), rowAt(base + 2), v);
}

Vec2f mapPoint(const OutputMapping& map, float s, float t) {
    const Homography h = squareToQuad(map.corners);
    const Vec2f base = applyHomography(h, s, t);
    const Vec2f offset = warpOffsetAt(map, s, t);
    return {base.x + offset.x, base.y + offset.y};
}

std::vector<MeshVertex> buildMesh(const OutputMapping& map, int divisions) {
    divisions = std::clamp(divisions, 1, 128);
    const Homography h = squareToQuad(map.corners);
    std::vector<MeshVertex> vertices;
    vertices.reserve(static_cast<size_t>(divisions + 1) * static_cast<size_t>(divisions + 1));
    for (int j = 0; j <= divisions; ++j) {
        const float t = static_cast<float>(j) / static_cast<float>(divisions);
        for (int i = 0; i <= divisions; ++i) {
            const float s = static_cast<float>(i) / static_cast<float>(divisions);
            const Vec2f base = applyHomography(h, s, t);
            const Vec2f offset = warpOffsetAt(map, s, t);
            const float sourceS = map.flipX ? 1.0f - s : s;
            const float sourceT = map.flipY ? 1.0f - t : t;
            MeshVertex vertex;
            vertex.x = base.x + offset.x;
            vertex.y = base.y + offset.y;
            vertex.u = map.slice[0] + map.slice[2] * sourceS;
            vertex.v = 1.0f - (map.slice[1] + map.slice[3] * sourceT);
            vertices.push_back(vertex);
        }
    }
    return vertices;
}

std::vector<uint32_t> buildMeshIndices(int divisions) {
    divisions = std::clamp(divisions, 1, 128);
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(divisions) * static_cast<size_t>(divisions) * 6u);
    const uint32_t stride = static_cast<uint32_t>(divisions + 1);
    for (int j = 0; j < divisions; ++j) {
        for (int i = 0; i < divisions; ++i) {
            const uint32_t a = static_cast<uint32_t>(j) * stride + static_cast<uint32_t>(i);
            const uint32_t b = a + 1;
            const uint32_t c = a + stride;
            const uint32_t d = c + 1;
            indices.insert(indices.end(), {a, b, d, a, d, c});
        }
    }
    return indices;
}

float edgeRamp(float t, float power, float gamma) {
    t = clampf(t, 0.0f, 1.0f);
    power = clampf(power, 1.0f, 4.0f);
    gamma = clampf(gamma, 1.0f, 3.0f);
    const float shaped = t < 0.5f ? 0.5f * std::pow(2.0f * t, power) : 1.0f - 0.5f * std::pow(2.0f * (1.0f - t), power);
    return std::pow(shaped, 1.0f / gamma);
}

float edgeFactor(const EdgeBlend& blend, float x, float y) {
    float factor = 1.0f;
    if (blend.left > 0.0f) {
        factor *= edgeRamp(x / blend.left, blend.power, blend.gamma);
    }
    if (blend.right > 0.0f) {
        factor *= edgeRamp((1.0f - x) / blend.right, blend.power, blend.gamma);
    }
    if (blend.top > 0.0f) {
        factor *= edgeRamp(y / blend.top, blend.power, blend.gamma);
    }
    if (blend.bottom > 0.0f) {
        factor *= edgeRamp((1.0f - y) / blend.bottom, blend.power, blend.gamma);
    }
    return factor;
}

bool hasEdgeBlend(const EdgeBlend& blend) {
    return blend.left > 0.0f || blend.right > 0.0f || blend.top > 0.0f || blend.bottom > 0.0f;
}

float blendZone(const EdgeBlend& blend, float x, float y) {
    if ((blend.left > 0.0f && x < blend.left) || (blend.right > 0.0f && 1.0f - x < blend.right) || (blend.top > 0.0f && y < blend.top) || (blend.bottom > 0.0f && 1.0f - y < blend.bottom)) {
        return 1.0f;
    }
    return 0.0f;
}

float blackLift(const EdgeBlend& blend, float x, float y) {
    if (!hasEdgeBlend(blend)) {
        return 0.0f;
    }
    return clampf(blend.blackLevel, 0.0f, 0.3f) * (1.0f - blendZone(blend, x, y));
}

bool maskUsable(const SliceMask& mask) {
    return mask.enabled && mask.points.size() >= static_cast<size_t>(kMinMaskPoints);
}

SliceMask maskRectangle(float left, float top, float right, float bottom) {
    SliceMask mask;
    mask.enabled = true;
    mask.points = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    normalizeMask(mask);
    return mask;
}

SliceMask maskEllipse(float centerX, float centerY, float radiusX, float radiusY, int segments) {
    segments = std::clamp(segments, 6, kMaxMaskPoints);
    SliceMask mask;
    mask.enabled = true;
    for (int i = 0; i < segments; ++i) {
        const float angle = 6.28318530718f * static_cast<float>(i) / static_cast<float>(segments);
        mask.points.push_back({centerX + radiusX * std::cos(angle), centerY + radiusY * std::sin(angle)});
    }
    normalizeMask(mask);
    return mask;
}

bool maskContains(const SliceMask& mask, float x, float y) {
    if (!maskUsable(mask)) {
        return true;
    }
    bool inside = false;
    const size_t count = mask.points.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vec2f& a = mask.points[i];
        const Vec2f& b = mask.points[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside != mask.invert;
}

int maskInsertPoint(SliceMask& mask) {
    if (mask.points.size() >= static_cast<size_t>(kMaxMaskPoints)) {
        return -1;
    }
    if (mask.points.size() < 2) {
        mask.points.push_back({0.5f, 0.5f});
        return static_cast<int>(mask.points.size()) - 1;
    }
    size_t best = 0;
    float longest = -1.0f;
    const size_t count = mask.points.size();
    for (size_t i = 0; i < count; ++i) {
        const Vec2f& a = mask.points[i];
        const Vec2f& b = mask.points[(i + 1) % count];
        const float length = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
        if (length > longest) {
            longest = length;
            best = i;
        }
    }
    const Vec2f& a = mask.points[best];
    const Vec2f& b = mask.points[(best + 1) % count];
    const Vec2f middle{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    mask.points.insert(mask.points.begin() + static_cast<std::ptrdiff_t>(best + 1), middle);
    return static_cast<int>(best + 1);
}

bool maskRemovePoint(SliceMask& mask, int index) {
    if (index < 0 || static_cast<size_t>(index) >= mask.points.size() || mask.points.size() <= static_cast<size_t>(kMinMaskPoints)) {
        return false;
    }
    mask.points.erase(mask.points.begin() + index);
    return true;
}

namespace {

void blurLine(float* data, int count, int stride, int radius, std::vector<float>& padded) {
    const size_t total = static_cast<size_t>(count) + 2u * static_cast<size_t>(radius);
    padded.resize(total);
    for (int i = 0; i < radius; ++i) {
        padded[static_cast<size_t>(i)] = data[0];
        padded[static_cast<size_t>(radius + count + i)] = data[static_cast<size_t>(count - 1) * static_cast<size_t>(stride)];
    }
    for (int i = 0; i < count; ++i) {
        padded[static_cast<size_t>(radius + i)] = data[static_cast<size_t>(i) * static_cast<size_t>(stride)];
    }
    const int window = 2 * radius + 1;
    float sum = 0.0f;
    for (int i = 0; i < window; ++i) {
        sum += padded[static_cast<size_t>(i)];
    }
    const float scale = 1.0f / static_cast<float>(window);
    for (int i = 0; i < count; ++i) {
        data[static_cast<size_t>(i) * static_cast<size_t>(stride)] = sum * scale;
        if (i + 1 < count) {
            sum += padded[static_cast<size_t>(i + window)] - padded[static_cast<size_t>(i)];
        }
    }
}

void blurImage(std::vector<float>& image, int width, int height, int radius) {
    std::vector<float> padded;
    for (int pass = 0; pass < 3; ++pass) {
        for (int row = 0; row < height; ++row) {
            blurLine(image.data() + static_cast<size_t>(row) * static_cast<size_t>(width), width, 1, radius, padded);
        }
        for (int column = 0; column < width; ++column) {
            blurLine(image.data() + column, height, width, radius, padded);
        }
    }
}

}

std::vector<uint8_t> buildMaskImage(const SliceMask& mask, int width, int height) {
    width = std::clamp(width, 1, 4096);
    height = std::clamp(height, 1, 4096);
    std::vector<uint8_t> result(static_cast<size_t>(width) * static_cast<size_t>(height), 255);
    if (!maskUsable(mask)) {
        return result;
    }
    constexpr int kSub = 4;
    std::vector<float> cover(result.size(), 0.0f);
    std::vector<float> crossings;
    const size_t count = mask.points.size();
    const float widthF = static_cast<float>(width);
    for (int row = 0; row < height; ++row) {
        float* line = cover.data() + static_cast<size_t>(row) * static_cast<size_t>(width);
        for (int sub = 0; sub < kSub; ++sub) {
            const float y = (static_cast<float>(row) + (static_cast<float>(sub) + 0.5f) / static_cast<float>(kSub)) / static_cast<float>(height);
            crossings.clear();
            for (size_t i = 0; i < count; ++i) {
                const Vec2f& a = mask.points[i];
                const Vec2f& b = mask.points[(i + 1) % count];
                if ((a.y <= y) != (b.y <= y)) {
                    const float t = (y - a.y) / (b.y - a.y);
                    crossings.push_back((a.x + t * (b.x - a.x)) * widthF);
                }
            }
            std::sort(crossings.begin(), crossings.end());
            for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
                const float x0 = clampf(crossings[k], 0.0f, widthF);
                const float x1 = clampf(crossings[k + 1], 0.0f, widthF);
                if (x1 <= x0) {
                    continue;
                }
                const int first = std::min(static_cast<int>(std::floor(x0)), width - 1);
                const int last = std::min(static_cast<int>(std::floor(x1)), width - 1);
                for (int ix = first; ix <= last; ++ix) {
                    const float overlap = std::min(x1, static_cast<float>(ix + 1)) - std::max(x0, static_cast<float>(ix));
                    if (overlap > 0.0f) {
                        line[ix] += overlap / static_cast<float>(kSub);
                    }
                }
            }
        }
    }
    if (mask.feather > 0.0f) {
        const int radius = std::max(1, static_cast<int>(std::lround(mask.feather * static_cast<float>(height) * 0.5f)));
        blurImage(cover, width, height, radius);
    }
    for (size_t i = 0; i < result.size(); ++i) {
        const int value = static_cast<int>(std::lround(clampf(cover[i], 0.0f, 1.0f) * 255.0f));
        result[i] = static_cast<uint8_t>(mask.invert ? 255 - value : value);
    }
    return result;
}

void layoutGrid(int columns, int rows, float overlap, std::vector<OutputMapping>& maps) {
    columns = std::clamp(columns, 1, kMaxOutputs);
    rows = std::clamp(rows, 1, kMaxOutputs);
    overlap = clampf(overlap, 0.0f, 0.4f);
    const float width = (1.0f + static_cast<float>(columns - 1) * overlap) / static_cast<float>(columns);
    const float height = (1.0f + static_cast<float>(rows - 1) * overlap) / static_cast<float>(rows);
    const float blendX = columns > 1 ? overlap / width : 0.0f;
    const float blendY = rows > 1 ? overlap / height : 0.0f;
    maps.clear();
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            OutputMapping map;
            map.fill = columns * rows > 1;
            map.slice = {static_cast<float>(c) * (width - overlap), static_cast<float>(r) * (height - overlap), width, height};
            map.blend.left = c > 0 ? blendX : 0.0f;
            map.blend.right = c < columns - 1 ? blendX : 0.0f;
            map.blend.top = r > 0 ? blendY : 0.0f;
            map.blend.bottom = r < rows - 1 ? blendY : 0.0f;
            normalizeMapping(map);
            maps.push_back(map);
        }
    }
}

OutputSpec defaultOutputSpec(int index) {
    OutputSpec spec;
    spec.name = "Output " + std::to_string(index + 1);
    spec.enabled = index == 0;
    spec.monitor = index;
    return spec;
}

std::array<OutputSpec, kMaxOutputs> defaultOutputs() {
    std::array<OutputSpec, kMaxOutputs> outputs;
    for (int i = 0; i < kMaxOutputs; ++i) {
        outputs[static_cast<size_t>(i)] = defaultOutputSpec(i);
    }
    return outputs;
}

nlohmann::json maskToJson(const SliceMask& mask) {
    nlohmann::json doc;
    doc["enabled"] = mask.enabled;
    nlohmann::json points = nlohmann::json::array();
    for (const Vec2f& point : mask.points) {
        points.push_back({point.x, point.y});
    }
    doc["points"] = points;
    doc["feather"] = mask.feather;
    doc["invert"] = mask.invert;
    return doc;
}

nlohmann::json mappingToJson(const OutputMapping& map) {
    nlohmann::json doc;
    doc["slice"] = {map.slice[0], map.slice[1], map.slice[2], map.slice[3]};
    nlohmann::json corners = nlohmann::json::array();
    for (const Vec2f& corner : map.corners) {
        corners.push_back({corner.x, corner.y});
    }
    doc["corners"] = corners;
    doc["warpColumns"] = map.warpColumns;
    doc["warpRows"] = map.warpRows;
    nlohmann::json warp = nlohmann::json::array();
    for (const Vec2f& offset : map.warp) {
        warp.push_back({offset.x, offset.y});
    }
    doc["warp"] = warp;
    doc["blend"] = {{"left", map.blend.left}, {"right", map.blend.right}, {"top", map.blend.top}, {"bottom", map.blend.bottom}, {"power", map.blend.power}, {"gamma", map.blend.gamma}, {"blackLevel", map.blend.blackLevel}};
    doc["brightness"] = map.brightness;
    doc["flipX"] = map.flipX;
    doc["flipY"] = map.flipY;
    doc["fill"] = map.fill;
    doc["mask"] = maskToJson(map.mask);
    return doc;
}

nlohmann::json colorToJson(const OutputColor& color) {
    return {{"contrast", color.contrast}, {"saturation", color.saturation}, {"gamma", color.gamma}, {"red", color.red}, {"green", color.green}, {"blue", color.blue}, {"black", color.black}};
}

nlohmann::json outputToJson(const OutputSpec& spec) {
    nlohmann::json doc = mappingToJson(spec.map);
    doc["name"] = spec.name;
    doc["enabled"] = spec.enabled;
    doc["monitor"] = spec.monitor;
    doc["fullscreen"] = spec.fullscreen;
    doc["color"] = colorToJson(spec.color);
    nlohmann::json slices = nlohmann::json::array();
    for (const OutputSlice& slice : spec.slices) {
        nlohmann::json entry = mappingToJson(slice.map);
        entry["name"] = slice.name;
        entry["enabled"] = slice.enabled;
        slices.push_back(entry);
    }
    doc["slices"] = slices;
    return doc;
}

namespace {

bool readPair(const nlohmann::json& value, Vec2f& out) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
        return false;
    }
    out.x = value[0].get<float>();
    out.y = value[1].get<float>();
    return true;
}

float readFloat(const nlohmann::json& doc, const char* key, float fallback) {
    auto it = doc.find(key);
    return (it != doc.end() && it->is_number()) ? it->get<float>() : fallback;
}

bool readBool(const nlohmann::json& doc, const char* key, bool fallback) {
    auto it = doc.find(key);
    return (it != doc.end() && it->is_boolean()) ? it->get<bool>() : fallback;
}

std::string readName(const nlohmann::json& doc, const std::string& fallback) {
    auto name = doc.find("name");
    if (name != doc.end() && name->is_string() && !name->get<std::string>().empty()) {
        return name->get<std::string>().substr(0, 40);
    }
    return fallback;
}

}

bool maskFromJson(const nlohmann::json& doc, SliceMask& mask) {
    if (!doc.is_object()) {
        return false;
    }
    SliceMask result;
    result.enabled = readBool(doc, "enabled", false);
    result.feather = readFloat(doc, "feather", 0.0f);
    result.invert = readBool(doc, "invert", false);
    auto points = doc.find("points");
    if (points != doc.end() && points->is_array()) {
        for (const nlohmann::json& entry : *points) {
            Vec2f point;
            if (readPair(entry, point) && result.points.size() < static_cast<size_t>(kMaxMaskPoints)) {
                result.points.push_back(point);
            }
        }
    }
    normalizeMask(result);
    mask = result;
    return true;
}

bool mappingFromJson(const nlohmann::json& doc, OutputMapping& out) {
    if (!doc.is_object()) {
        return false;
    }
    OutputMapping map;
    auto slice = doc.find("slice");
    if (slice != doc.end() && slice->is_array() && slice->size() == 4) {
        for (size_t i = 0; i < 4; ++i) {
            if ((*slice)[i].is_number()) {
                map.slice[i] = (*slice)[i].get<float>();
            }
        }
    }
    auto corners = doc.find("corners");
    if (corners != doc.end() && corners->is_array() && corners->size() == 4) {
        for (size_t i = 0; i < 4; ++i) {
            readPair((*corners)[i], map.corners[i]);
        }
    }
    auto columns = doc.find("warpColumns");
    auto rows = doc.find("warpRows");
    if (columns != doc.end() && columns->is_number_integer() && rows != doc.end() && rows->is_number_integer()) {
        map.warpColumns = std::clamp(columns->get<int>(), kMinWarpPoints, kMaxWarpPoints);
        map.warpRows = std::clamp(rows->get<int>(), kMinWarpPoints, kMaxWarpPoints);
    }
    map.warp.assign(static_cast<size_t>(map.warpColumns) * static_cast<size_t>(map.warpRows), Vec2f{});
    auto warp = doc.find("warp");
    if (warp != doc.end() && warp->is_array() && warp->size() == map.warp.size()) {
        for (size_t i = 0; i < map.warp.size(); ++i) {
            readPair((*warp)[i], map.warp[i]);
        }
    }
    auto blend = doc.find("blend");
    if (blend != doc.end() && blend->is_object()) {
        map.blend.left = readFloat(*blend, "left", 0.0f);
        map.blend.right = readFloat(*blend, "right", 0.0f);
        map.blend.top = readFloat(*blend, "top", 0.0f);
        map.blend.bottom = readFloat(*blend, "bottom", 0.0f);
        map.blend.power = readFloat(*blend, "power", 2.0f);
        map.blend.gamma = readFloat(*blend, "gamma", 2.2f);
        map.blend.blackLevel = readFloat(*blend, "blackLevel", 0.0f);
    }
    map.brightness = readFloat(doc, "brightness", 1.0f);
    map.flipX = readBool(doc, "flipX", false);
    map.flipY = readBool(doc, "flipY", false);
    map.fill = readBool(doc, "fill", false);
    auto mask = doc.find("mask");
    if (mask != doc.end()) {
        maskFromJson(*mask, map.mask);
    }
    normalizeMapping(map);
    out = map;
    return true;
}

bool colorFromJson(const nlohmann::json& doc, OutputColor& color) {
    if (!doc.is_object()) {
        return false;
    }
    OutputColor result;
    result.contrast = readFloat(doc, "contrast", 1.0f);
    result.saturation = readFloat(doc, "saturation", 1.0f);
    result.gamma = readFloat(doc, "gamma", 1.0f);
    result.red = readFloat(doc, "red", 1.0f);
    result.green = readFloat(doc, "green", 1.0f);
    result.blue = readFloat(doc, "blue", 1.0f);
    result.black = readFloat(doc, "black", 0.0f);
    normalizeColor(result);
    color = result;
    return true;
}

bool outputFromJson(const nlohmann::json& doc, OutputSpec& spec) {
    if (!doc.is_object()) {
        return false;
    }
    OutputSpec result = spec;
    result.name = readName(doc, result.name);
    result.enabled = readBool(doc, "enabled", result.enabled);
    auto monitor = doc.find("monitor");
    if (monitor != doc.end() && monitor->is_number_integer()) {
        result.monitor = std::clamp(monitor->get<int>(), 0, 15);
    }
    result.fullscreen = readBool(doc, "fullscreen", result.fullscreen);
    mappingFromJson(doc, result.map);
    result.color = OutputColor{};
    auto color = doc.find("color");
    if (color != doc.end()) {
        colorFromJson(*color, result.color);
    }
    result.slices.clear();
    auto slices = doc.find("slices");
    if (slices != doc.end() && slices->is_array()) {
        for (const nlohmann::json& entry : *slices) {
            if (!entry.is_object() || result.slices.size() >= static_cast<size_t>(kMaxSlices - 1)) {
                continue;
            }
            OutputSlice slice;
            slice.name = readName(entry, "Slice " + std::to_string(result.slices.size() + 2));
            slice.enabled = readBool(entry, "enabled", true);
            mappingFromJson(entry, slice.map);
            result.slices.push_back(slice);
        }
    }
    normalizeSpec(result);
    spec = result;
    return true;
}

}

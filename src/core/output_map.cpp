#include "core/output_map.hpp"

#include "core/output_map_json.hpp"

#include <algorithm>
#include <cmath>

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
    map.brightness = clampf(map.brightness, 0.0f, 1.0f);
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

nlohmann::json outputToJson(const OutputSpec& spec) {
    nlohmann::json doc;
    doc["name"] = spec.name;
    doc["enabled"] = spec.enabled;
    doc["monitor"] = spec.monitor;
    doc["fullscreen"] = spec.fullscreen;
    const OutputMapping& map = spec.map;
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
    doc["blend"] = {{"left", map.blend.left}, {"right", map.blend.right}, {"top", map.blend.top}, {"bottom", map.blend.bottom}, {"power", map.blend.power}, {"gamma", map.blend.gamma}};
    doc["brightness"] = map.brightness;
    doc["flipX"] = map.flipX;
    doc["flipY"] = map.flipY;
    doc["fill"] = map.fill;
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

}

bool outputFromJson(const nlohmann::json& doc, OutputSpec& spec) {
    if (!doc.is_object()) {
        return false;
    }
    OutputSpec result = spec;
    auto name = doc.find("name");
    if (name != doc.end() && name->is_string() && !name->get<std::string>().empty()) {
        result.name = name->get<std::string>().substr(0, 40);
    }
    result.enabled = readBool(doc, "enabled", result.enabled);
    auto monitor = doc.find("monitor");
    if (monitor != doc.end() && monitor->is_number_integer()) {
        result.monitor = std::clamp(monitor->get<int>(), 0, 15);
    }
    result.fullscreen = readBool(doc, "fullscreen", result.fullscreen);
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
    }
    map.brightness = readFloat(doc, "brightness", 1.0f);
    map.flipX = readBool(doc, "flipX", false);
    map.flipY = readBool(doc, "flipY", false);
    map.fill = readBool(doc, "fill", false);
    normalizeMapping(map);
    result.map = map;
    spec = result;
    return true;
}

}

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

constexpr int kMaxOutputs = 4;
constexpr int kMinWarpPoints = 2;
constexpr int kMaxWarpPoints = 9;
constexpr int kMeshDivisions = 32;

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
    bool operator==(const Vec2f&) const = default;
};

struct EdgeBlend {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float power = 2.0f;
    float gamma = 2.2f;
    bool operator==(const EdgeBlend&) const = default;
};

struct OutputMapping {
    std::array<float, 4> slice{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<Vec2f, 4> corners{{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    int warpColumns = 2;
    int warpRows = 2;
    std::vector<Vec2f> warp = std::vector<Vec2f>(4);
    EdgeBlend blend;
    float brightness = 1.0f;
    bool flipX = false;
    bool flipY = false;
    bool fill = false;
    bool operator==(const OutputMapping&) const = default;
};

struct OutputSpec {
    std::string name;
    bool enabled = false;
    int monitor = 0;
    bool fullscreen = false;
    OutputMapping map;
    bool operator==(const OutputSpec&) const = default;
};

struct MeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

void normalizeMapping(OutputMapping& map);
void resizeWarp(OutputMapping& map, int columns, int rows);
bool isDefaultMapping(const OutputMapping& map);
void resetCorners(OutputMapping& map);
void resetWarp(OutputMapping& map);

Vec2f warpOffsetAt(const OutputMapping& map, float s, float t);
Vec2f mapPoint(const OutputMapping& map, float s, float t);
std::vector<MeshVertex> buildMesh(const OutputMapping& map, int divisions = kMeshDivisions);
std::vector<uint32_t> buildMeshIndices(int divisions = kMeshDivisions);

float edgeRamp(float t, float power, float gamma);
float edgeFactor(const EdgeBlend& blend, float x, float y);

void layoutGrid(int columns, int rows, float overlap, std::vector<OutputMapping>& maps);

OutputSpec defaultOutputSpec(int index);
std::array<OutputSpec, kMaxOutputs> defaultOutputs();

}

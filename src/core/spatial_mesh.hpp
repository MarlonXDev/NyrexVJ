#pragma once

#include "core/output_map.hpp"
#include "core/spatial_map.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nvj {

struct SpatialVertex {
    float x = 0.0f;
    float y = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
    float depth = 0.0f;
    float w = 1.0f;
    float q = 1.0f;
};

struct SpatialMeshStats {
    int surfaces = 0;
    int trianglesIn = 0;
    int trianglesOut = 0;
    int culled = 0;
    int behind = 0;
    int clipped = 0;
    int degenerate = 0;
    int offscreen = 0;
};

struct SpatialMesh {
    std::vector<SpatialVertex> vertices;
    std::vector<uint32_t> indices;
    SpatialMeshStats stats;
    size_t bytes() const { return vertices.size() * sizeof(SpatialVertex) + indices.size() * sizeof(uint32_t); }
};

struct SpatialHit {
    bool hit = false;
    int surface = -1;
    Vec3f world;
    float distance = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    bool contentValid = false;
};

struct TextureCoord {
    float u = 0.0f;
    float v = 0.0f;
};

constexpr size_t kMaxSpatialMeshBytes = 8u * 1024u * 1024u;

SpatialMesh buildSpatialMesh(const OutputMapping& map);
size_t spatialMeshBytesEstimate(const SpatialMapping& spatial);
TextureCoord contentCoords(const OutputMapping& map, double localX, double localY);
SpatialHit castProjectorRay(const OutputMapping& map, float outputX, float outputY);
bool pointLitByProjector(const OutputMapping& map, const Vec3f& world, float tolerance = 0.002f);
float projectorBlendWeight(const OutputMapping& map, const Vec3f& world);

}

#pragma once

#include "scene3d/primitives.hpp"
#include "scene3d/scene_model.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

enum class GeoNodeType { Primitive, Subdivide, Transform, Noise, Twist, Merge, Instance, Material, Audio, Output };
enum class GeoInstanceKind { Ring, Line, Grid, Scatter };

constexpr int kGeoNodeTypeCount = 10;
constexpr int kGeoInstanceKindCount = 4;
constexpr size_t kGeoMaxNodes = 48;
constexpr size_t kGeoMaxGraphs = 8;
constexpr size_t kGeoMaxVertices = 200000;
constexpr size_t kGeoMaxIndices = 1000000;
constexpr size_t kGeoMaxInstances = 1024;
constexpr size_t kGeoMaxDrawnTriangles = 4000000;
constexpr size_t kGeoMaxBindings = 4;
constexpr size_t kGeoMaxIdBytes = 24;
constexpr size_t kGeoMaxNameBytes = 64;
constexpr size_t kGeoMaxNotes = 16;
constexpr int kGeoAudioTargetCount = 10;
constexpr BindTarget kGeoAudioTargets[kGeoAudioTargetCount] = {BindTarget::Scale, BindTarget::Emissive, BindTarget::Twist, BindTarget::Lift, BindTarget::Hue, BindTarget::Roughness, BindTarget::UvX, BindTarget::UvY, BindTarget::Inflate, BindTarget::Ripple};
constexpr int kGeoMaxLevels = 4;
constexpr int kGeoMaxOctaves = 4;
constexpr size_t kGeoCacheEntries = 8;
constexpr size_t kGeoCacheVertices = 600000;

struct GeoNode {
    std::string id;
    GeoNodeType type = GeoNodeType::Primitive;
    std::array<std::string, 2> inputs;
    PrimitiveKind primitive = PrimitiveKind::Sphere;
    int levels = 1;
    Vec3 move;
    Vec3 rotate;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    float amount = 0.2f;
    float frequency = 2.0f;
    int octaves = 2;
    int seed = 1;
    GeoInstanceKind instance = GeoInstanceKind::Ring;
    int count = 8;
    float radius = 1.5f;
    float spacing = 0.6f;
    float jitter = 0.0f;
    std::array<float, 3> color{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 0.0f;
    bool followPalette = false;
    BindSource source = BindSource::Bass;
    BindTarget target = BindTarget::Scale;
    float bindAmount = 0.3f;
};

struct GeometryGraph {
    std::string name = "Graph";
    std::vector<GeoNode> nodes;
};

struct GeoInstance {
    Vec3 position;
    Vec3 rotation;
    float scale = 1.0f;
};

struct GeoMaterial {
    std::array<float, 3> color{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 0.0f;
    bool followPalette = false;
};

struct GeoBinding {
    BindSource source = BindSource::Bass;
    BindTarget target = BindTarget::Scale;
    float amount = 0.0f;
};

struct GeoLimits {
    size_t maxVertices = kGeoMaxVertices;
    size_t maxIndices = kGeoMaxIndices;
    size_t maxInstances = kGeoMaxInstances;
    size_t maxDrawnTriangles = kGeoMaxDrawnTriangles;
};

struct GeoResult {
    bool ok = false;
    SceneMesh mesh;
    std::vector<GeoInstance> instances;
    bool hasMaterial = false;
    GeoMaterial material;
    std::vector<GeoBinding> bindings;
    std::vector<std::string> errors;
    std::vector<std::string> notes;
    size_t vertices = 0;
    size_t indices = 0;
    size_t nodesEvaluated = 0;
    double milliseconds = 0.0;
    uint64_t hash = 0;
};

const char* geoNodeKey(GeoNodeType type);
const char* geoNodeLabel(GeoNodeType type);
bool geoNodeFromKey(const std::string& key, GeoNodeType& type);
const char* geoInstanceKey(GeoInstanceKind kind);
const char* geoInstanceLabel(GeoInstanceKind kind);
bool geoInstanceFromKey(const std::string& key, GeoInstanceKind& kind);
int geoInputCount(GeoNodeType type);
GeoNode makeGeoNode(GeoNodeType type, const std::string& id);
GeometryGraph makeDefaultGeometryGraph();
std::string geoNewNodeId(const GeometryGraph& graph);
int geoNodeIndex(const GeometryGraph& graph, const std::string& id);
bool geoInputWouldLoop(const GeometryGraph& graph, int node, const std::string& input);
bool geoRemoveNode(GeometryGraph& graph, int index);
void sanitizeGeometryGraph(GeometryGraph& graph, std::vector<std::string>* notes);
uint64_t geometryGraphHash(const GeometryGraph& graph);
GeoResult evaluateGeometryGraph(const GeometryGraph& graph, const GeoLimits& limits = {});
std::shared_ptr<const GeoResult> evaluateGeometryGraphCached(const GeometryGraph& graph, bool* hit = nullptr);

struct GeoCacheStats {
    size_t entries = 0;
    size_t vertices = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
};

GeoCacheStats geometryGraphCacheStats();
void setGeometryGraphsEnabled(bool enabled);
bool geometryGraphsEnabled();
void geometryGraphCacheClear();

}

#include "scene3d/geometry_graph.hpp"

#include "scene3d/geometry_graph_json.hpp"
#include "scene3d/scene_doc.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <list>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>

namespace nvj {

using json = nlohmann::json;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegreesToRadians = 0.0174532925f;

float clampFinite(float value, float low, float high, float fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

int clampInt(int value, int low, int high) {
    return std::clamp(value, low, high);
}

std::string cleanName(const std::string& text, const std::string& fallback) {
    std::string out;
    for (const unsigned char c : text) {
        if (c >= 32 && c != 127) {
            out.push_back(static_cast<char>(c));
        }
        if (out.size() >= kGeoMaxNameBytes) {
            break;
        }
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return fallback;
    }
    return out.substr(first);
}

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > kGeoMaxIdBytes) {
        return false;
    }
    for (const unsigned char c : id) {
        if (!(std::isalnum(c) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

void addNote(std::vector<std::string>& notes, const std::string& text) {
    if (notes.size() < kGeoMaxNotes) {
        notes.push_back(text);
    } else if (notes.size() == kGeoMaxNotes) {
        notes.push_back("More messages were left out");
    }
}

uint32_t mix(uint32_t x) {
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16)) * 0x21f0aaadu;
    x = (x ^ (x >> 15)) * 0x735a2d97u;
    return x ^ (x >> 15);
}

float hash01(int seed, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = mix(static_cast<uint32_t>(seed) * 0x85ebca6bu + 0x1234567u);
    h = mix(h ^ mix(a + 0x27d4eb2fu));
    h = mix(h ^ mix(b + 0x165667b1u));
    h = mix(h ^ mix(c + 0x9e3779b1u));
    return static_cast<float>(h >> 8) / 16777216.0f;
}

float lattice(int x, int y, int z, int seed) {
    return hash01(seed, static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z)) * 2.0f - 1.0f;
}

float smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

float valueNoise(Vec3 p, int seed) {
    const float fx = std::floor(p.x);
    const float fy = std::floor(p.y);
    const float fz = std::floor(p.z);
    const int x = static_cast<int>(fx);
    const int y = static_cast<int>(fy);
    const int z = static_cast<int>(fz);
    const float tx = smooth(p.x - fx);
    const float ty = smooth(p.y - fy);
    const float tz = smooth(p.z - fz);
    const auto at = [&](int dx, int dy, int dz) { return lattice(x + dx, y + dy, z + dz, seed); };
    const auto mixf = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = mixf(at(0, 0, 0), at(1, 0, 0), tx);
    const float c10 = mixf(at(0, 1, 0), at(1, 1, 0), tx);
    const float c01 = mixf(at(0, 0, 1), at(1, 0, 1), tx);
    const float c11 = mixf(at(0, 1, 1), at(1, 1, 1), tx);
    return mixf(mixf(c00, c10, ty), mixf(c01, c11, ty), tz);
}

float fractalNoise(Vec3 p, int seed, int octaves) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float scale = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(p * scale, seed + i * 31) * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        scale *= 2.0f;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

Quat eulerToQuat(Vec3 degrees) {
    const Quat x = axisAngle({1.0f, 0.0f, 0.0f}, degrees.x * kDegreesToRadians);
    const Quat y = axisAngle({0.0f, 1.0f, 0.0f}, degrees.y * kDegreesToRadians);
    const Quat z = axisAngle({0.0f, 0.0f, 1.0f}, degrees.z * kDegreesToRadians);
    return y * x * z;
}

Vec3 rotateVector(const Mat4& rotationMatrix, Vec3 v) {
    return {rotationMatrix.m[0] * v.x + rotationMatrix.m[4] * v.y + rotationMatrix.m[8] * v.z,
            rotationMatrix.m[1] * v.x + rotationMatrix.m[5] * v.y + rotationMatrix.m[9] * v.z,
            rotationMatrix.m[2] * v.x + rotationMatrix.m[6] * v.y + rotationMatrix.m[10] * v.z};
}

void recomputeNormals(SceneMesh& mesh) {
    const auto key = [](Vec3 p) {
        const auto q = [](float v) { return static_cast<int64_t>(std::llround(static_cast<double>(v) * 10000.0)); };
        uint64_t h = 1469598103934665603ull;
        for (const int64_t v : {q(p.x), q(p.y), q(p.z)}) {
            h ^= static_cast<uint64_t>(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h *= 1099511628211ull;
        }
        return h;
    };
    std::unordered_map<uint64_t, Vec3> sums;
    sums.reserve(mesh.positions.size());
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.positions[mesh.indices[i]];
        const Vec3& b = mesh.positions[mesh.indices[i + 1]];
        const Vec3& c = mesh.positions[mesh.indices[i + 2]];
        const Vec3 face = cross(b - a, c - a);
        for (size_t k = 0; k < 3; ++k) {
            Vec3& sum = sums[key(mesh.positions[mesh.indices[i + k]])];
            sum = sum + face;
        }
    }
    mesh.normals.resize(mesh.positions.size());
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3 sum = sums[key(mesh.positions[i])];
        if (length(sum) > 1e-12f) {
            mesh.normals[i] = normalize(sum);
        }
    }
}

float meshExtent(const SceneMesh& mesh) {
    if (mesh.positions.empty()) {
        return 1.0f;
    }
    Vec3 low = mesh.positions[0];
    Vec3 high = mesh.positions[0];
    for (const Vec3& p : mesh.positions) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    return std::max({high.x - low.x, high.y - low.y, high.z - low.z, 1e-4f});
}

struct Flow {
    SceneMesh mesh;
    std::vector<GeoInstance> instances;
    bool hasInstances = false;
    bool hasMaterial = false;
    GeoMaterial material;
    std::vector<GeoBinding> bindings;
};

struct Evaluator {
    const GeometryGraph& graph;
    const GeoLimits& limits;
    GeoResult& result;
    std::vector<int> state;
    std::vector<std::shared_ptr<Flow>> flows;
    bool failed = false;

    void fail(const std::string& text) {
        failed = true;
        result.errors.push_back(text);
    }

    std::shared_ptr<Flow> input(const GeoNode& node, size_t slot, bool required) {
        const std::string& id = node.inputs[slot];
        if (id.empty()) {
            if (required) {
                fail("Missing input: " + node.id);
            }
            return nullptr;
        }
        const int index = geoNodeIndex(graph, id);
        if (index < 0) {
            fail("Input does not exist: " + id);
            return nullptr;
        }
        return visit(index);
    }

    bool sizeOk(const GeoNode& node, size_t vertices, size_t indices) {
        if (vertices > limits.maxVertices) {
            fail("Too many vertices at node: " + node.id + ", " + std::to_string(vertices) + " > " + std::to_string(limits.maxVertices));
            return false;
        }
        if (indices > limits.maxIndices) {
            fail("Too many indices at node: " + node.id + ", " + std::to_string(indices) + " > " + std::to_string(limits.maxIndices));
            return false;
        }
        return true;
    }

    std::shared_ptr<Flow> visit(int index) {
        const size_t slot = static_cast<size_t>(index);
        if (state[slot] == 2) {
            return flows[slot];
        }
        const GeoNode& node = graph.nodes[slot];
        if (state[slot] == 1) {
            fail("Loop through node: " + node.id);
            return nullptr;
        }
        state[slot] = 1;
        std::shared_ptr<Flow> flow = build(node);
        state[slot] = 2;
        flows[slot] = failed ? nullptr : flow;
        if (flows[slot]) {
            ++result.nodesEvaluated;
        }
        return flows[slot];
    }

    std::shared_ptr<Flow> build(const GeoNode& node) {
        switch (node.type) {
        case GeoNodeType::Primitive: {
            auto flow = std::make_shared<Flow>();
            flow->mesh = primitiveMesh(node.primitive);
            flow->mesh.material = -1;
            if (!sizeOk(node, flow->mesh.positions.size(), flow->mesh.indices.size())) {
                return nullptr;
            }
            return flow;
        }
        case GeoNodeType::Merge: {
            std::shared_ptr<Flow> a = input(node, 0, true);
            if (failed || !a) {
                return nullptr;
            }
            std::shared_ptr<Flow> b = input(node, 1, false);
            if (failed) {
                return nullptr;
            }
            if (!b) {
                return a;
            }
            if (a->hasInstances || b->hasInstances) {
                fail("Merge must come before Instance: " + node.id);
                return nullptr;
            }
            if (!sizeOk(node, a->mesh.positions.size() + b->mesh.positions.size(), a->mesh.indices.size() + b->mesh.indices.size())) {
                return nullptr;
            }
            auto flow = std::make_shared<Flow>(*a);
            const uint32_t offset = static_cast<uint32_t>(flow->mesh.positions.size());
            flow->mesh.positions.insert(flow->mesh.positions.end(), b->mesh.positions.begin(), b->mesh.positions.end());
            flow->mesh.normals.insert(flow->mesh.normals.end(), b->mesh.normals.begin(), b->mesh.normals.end());
            flow->mesh.uvs.insert(flow->mesh.uvs.end(), b->mesh.uvs.begin(), b->mesh.uvs.end());
            for (const uint32_t i : b->mesh.indices) {
                flow->mesh.indices.push_back(i + offset);
            }
            if (!flow->hasMaterial && b->hasMaterial) {
                flow->hasMaterial = true;
                flow->material = b->material;
            }
            for (const GeoBinding& binding : b->bindings) {
                if (flow->bindings.size() < kGeoMaxBindings) {
                    flow->bindings.push_back(binding);
                }
            }
            return flow;
        }
        default:
            break;
        }
        std::shared_ptr<Flow> in = input(node, 0, true);
        if (failed || !in) {
            return nullptr;
        }
        switch (node.type) {
        case GeoNodeType::Subdivide:
            return subdivide(node, *in);
        case GeoNodeType::Transform:
            return transform(node, *in);
        case GeoNodeType::Noise:
            return noise(node, *in);
        case GeoNodeType::Twist:
            return twist(node, *in);
        case GeoNodeType::Instance:
            return instance(node, *in);
        case GeoNodeType::Material: {
            auto flow = std::make_shared<Flow>(*in);
            flow->hasMaterial = true;
            flow->material = {node.color, node.metallic, node.roughness, node.emissive, node.emissiveStrength, node.followPalette};
            return flow;
        }
        case GeoNodeType::Audio: {
            auto flow = std::make_shared<Flow>(*in);
            if (flow->bindings.size() >= kGeoMaxBindings) {
                addNote(result.notes, "Audio row left out, four rows fit: " + node.id);
            } else if (node.bindAmount != 0.0f) {
                flow->bindings.push_back({node.source, node.target, node.bindAmount});
            }
            return flow;
        }
        case GeoNodeType::Output:
            return in;
        default:
            return nullptr;
        }
    }

    std::shared_ptr<Flow> subdivide(const GeoNode& node, const Flow& in) {
        auto flow = std::make_shared<Flow>(in);
        SceneMesh& mesh = flow->mesh;
        for (int level = 0; level < node.levels; ++level) {
            const size_t triangles = mesh.indices.size() / 3;
            if (!sizeOk(node, 0, triangles * 12)) {
                return nullptr;
            }
            std::unordered_map<uint64_t, uint32_t> midpoints;
            midpoints.reserve(mesh.indices.size() / 2 + 16);
            bool overflow = false;
            const bool hasUvs = mesh.uvs.size() == mesh.positions.size() * 2;
            const auto midpoint = [&](uint32_t a, uint32_t b) {
                const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
                const auto known = midpoints.find(key);
                if (known != midpoints.end()) {
                    return known->second;
                }
                if (mesh.positions.size() >= limits.maxVertices) {
                    overflow = true;
                    return a;
                }
                const uint32_t created = static_cast<uint32_t>(mesh.positions.size());
                mesh.positions.push_back((mesh.positions[a] + mesh.positions[b]) * 0.5f);
                mesh.normals.push_back(normalize(mesh.normals[a] + mesh.normals[b]));
                if (hasUvs) {
                    mesh.uvs.push_back((mesh.uvs[a * 2] + mesh.uvs[b * 2]) * 0.5f);
                    mesh.uvs.push_back((mesh.uvs[a * 2 + 1] + mesh.uvs[b * 2 + 1]) * 0.5f);
                }
                midpoints[key] = created;
                return created;
            };
            std::vector<uint32_t> indices;
            indices.reserve(triangles * 12);
            for (size_t i = 0; i < triangles; ++i) {
                const uint32_t a = mesh.indices[i * 3];
                const uint32_t b = mesh.indices[i * 3 + 1];
                const uint32_t c = mesh.indices[i * 3 + 2];
                const uint32_t ab = midpoint(a, b);
                const uint32_t bc = midpoint(b, c);
                const uint32_t ca = midpoint(c, a);
                indices.insert(indices.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
            }
            if (overflow) {
                fail("Too many vertices at node: " + node.id + ", over " + std::to_string(limits.maxVertices));
                return nullptr;
            }
            mesh.indices = std::move(indices);
        }
        return flow;
    }

    std::shared_ptr<Flow> transform(const GeoNode& node, const Flow& in) {
        auto flow = std::make_shared<Flow>(in);
        const Mat4 rotationMatrix = rotation(eulerToQuat(node.rotate));
        const Mat4 matrix = composeTrs(node.move, eulerToQuat(node.rotate), node.scale);
        const Vec3 inverseScale{1.0f / node.scale.x, 1.0f / node.scale.y, 1.0f / node.scale.z};
        for (Vec3& p : flow->mesh.positions) {
            p = transformPoint(matrix, p);
        }
        for (Vec3& n : flow->mesh.normals) {
            const Vec3 scaled{n.x * inverseScale.x, n.y * inverseScale.y, n.z * inverseScale.z};
            n = normalize(rotateVector(rotationMatrix, scaled));
        }
        if (node.scale.x * node.scale.y * node.scale.z < 0.0f) {
            for (size_t i = 0; i + 2 < flow->mesh.indices.size(); i += 3) {
                std::swap(flow->mesh.indices[i + 1], flow->mesh.indices[i + 2]);
            }
        }
        return flow;
    }

    std::shared_ptr<Flow> noise(const GeoNode& node, const Flow& in) {
        auto flow = std::make_shared<Flow>(in);
        SceneMesh& mesh = flow->mesh;
        const float strength = node.amount * meshExtent(mesh);
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            const float n = fractalNoise(mesh.positions[i] * node.frequency, node.seed, node.octaves);
            mesh.positions[i] = mesh.positions[i] + mesh.normals[i] * (n * strength);
        }
        recomputeNormals(mesh);
        return flow;
    }

    std::shared_ptr<Flow> twist(const GeoNode& node, const Flow& in) {
        auto flow = std::make_shared<Flow>(in);
        SceneMesh& mesh = flow->mesh;
        const auto turn = [&](Vec3 v, float y) {
            const float angle = node.amount * kDegreesToRadians * y;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            return Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
        };
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            const float y = mesh.positions[i].y;
            mesh.positions[i] = turn(mesh.positions[i], y);
            mesh.normals[i] = normalize(turn(mesh.normals[i], y));
        }
        recomputeNormals(mesh);
        return flow;
    }

    std::shared_ptr<Flow> instance(const GeoNode& node, const Flow& in) {
        if (in.hasInstances) {
            fail("Geometry is already instanced: " + node.id);
            return nullptr;
        }
        const size_t count = std::min<size_t>(static_cast<size_t>(std::max(node.count, 1)), limits.maxInstances);
        if (static_cast<size_t>(node.count) > limits.maxInstances) {
            addNote(result.notes, "Instance count was limited: " + node.id + ", " + std::to_string(limits.maxInstances));
        }
        auto flow = std::make_shared<Flow>(in);
        flow->hasInstances = true;
        flow->instances.reserve(count);
        const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
        const int rows = static_cast<int>((count + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
        for (size_t i = 0; i < count; ++i) {
            GeoInstance item;
            const uint32_t id = static_cast<uint32_t>(i);
            switch (node.instance) {
            case GeoInstanceKind::Ring: {
                const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count);
                item.position = {node.radius * std::cos(angle), 0.0f, node.radius * std::sin(angle)};
                item.rotation = {0.0f, -angle / kDegreesToRadians, 0.0f};
                break;
            }
            case GeoInstanceKind::Line:
                item.position = {(static_cast<float>(i) - 0.5f * static_cast<float>(count - 1)) * node.spacing, 0.0f, 0.0f};
                break;
            case GeoInstanceKind::Grid:
                item.position = {(static_cast<float>(static_cast<int>(i) % cols) - 0.5f * static_cast<float>(cols - 1)) * node.spacing, 0.0f,
                                 (static_cast<float>(static_cast<int>(i) / cols) - 0.5f * static_cast<float>(rows - 1)) * node.spacing};
                break;
            case GeoInstanceKind::Scatter: {
                const float u = hash01(node.seed, id, 1, 0) * 2.0f - 1.0f;
                const float angle = hash01(node.seed, id, 2, 0) * 2.0f * kPi;
                const float ring = std::sqrt(std::max(0.0f, 1.0f - u * u));
                const float reach = std::cbrt(hash01(node.seed, id, 3, 0)) * node.radius;
                item.position = {reach * ring * std::cos(angle), reach * u, reach * ring * std::sin(angle)};
                item.rotation = {0.0f, hash01(node.seed, id, 4, 0) * 360.0f, 0.0f};
                break;
            }
            }
            item.scale = 1.0f + (hash01(node.seed, id, 5, 0) * 2.0f - 1.0f) * 0.6f * node.jitter;
            flow->instances.push_back(item);
        }
        return flow;
    }
};

uint64_t fnv(const std::string& text) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

struct CacheEntry {
    uint64_t hash = 0;
    std::shared_ptr<const GeoResult> result;
};

std::atomic<bool> graphsEnabled{true};
std::mutex cacheMutex;
std::list<CacheEntry> cacheList;
GeoCacheStats cacheStats;

}

const char* geoNodeKey(GeoNodeType type) {
    static const char* const keys[kGeoNodeTypeCount] = {"primitive", "subdivide", "transform", "noise", "twist", "merge", "instance", "material", "audio", "output"};
    const int index = static_cast<int>(type);
    return index >= 0 && index < kGeoNodeTypeCount ? keys[index] : "primitive";
}

const char* geoNodeLabel(GeoNodeType type) {
    static const char* const labels[kGeoNodeTypeCount] = {"Primitive", "Subdivide", "Transform", "Noise", "Twist", "Merge", "Instance", "Material", "Audio", "Output"};
    const int index = static_cast<int>(type);
    return index >= 0 && index < kGeoNodeTypeCount ? labels[index] : "Primitive";
}

bool geoNodeFromKey(const std::string& key, GeoNodeType& type) {
    for (int i = 0; i < kGeoNodeTypeCount; ++i) {
        if (key == geoNodeKey(static_cast<GeoNodeType>(i))) {
            type = static_cast<GeoNodeType>(i);
            return true;
        }
    }
    return false;
}

const char* geoInstanceKey(GeoInstanceKind kind) {
    static const char* const keys[kGeoInstanceKindCount] = {"ring", "line", "grid", "scatter"};
    const int index = static_cast<int>(kind);
    return index >= 0 && index < kGeoInstanceKindCount ? keys[index] : "ring";
}

const char* geoInstanceLabel(GeoInstanceKind kind) {
    static const char* const labels[kGeoInstanceKindCount] = {"Ring", "Line", "Grid", "Scatter"};
    const int index = static_cast<int>(kind);
    return index >= 0 && index < kGeoInstanceKindCount ? labels[index] : "Ring";
}

bool geoInstanceFromKey(const std::string& key, GeoInstanceKind& kind) {
    for (int i = 0; i < kGeoInstanceKindCount; ++i) {
        if (key == geoInstanceKey(static_cast<GeoInstanceKind>(i))) {
            kind = static_cast<GeoInstanceKind>(i);
            return true;
        }
    }
    return false;
}

int geoInputCount(GeoNodeType type) {
    if (type == GeoNodeType::Primitive) {
        return 0;
    }
    return type == GeoNodeType::Merge ? 2 : 1;
}

GeoNode makeGeoNode(GeoNodeType type, const std::string& id) {
    GeoNode node;
    node.id = id;
    node.type = type;
    switch (type) {
    case GeoNodeType::Noise:
        node.amount = 0.2f;
        break;
    case GeoNodeType::Twist:
        node.amount = 90.0f;
        break;
    case GeoNodeType::Instance:
        node.count = 12;
        break;
    case GeoNodeType::Subdivide:
        node.levels = 2;
        break;
    default:
        break;
    }
    return node;
}

std::string geoNewNodeId(const GeometryGraph& graph) {
    for (size_t n = 1; n < 100000; ++n) {
        const std::string id = "n" + std::to_string(n);
        if (geoNodeIndex(graph, id) < 0) {
            return id;
        }
    }
    return "n0";
}

int geoNodeIndex(const GeometryGraph& graph, const std::string& id) {
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

GeometryGraph makeDefaultGeometryGraph() {
    GeometryGraph graph;
    graph.name = "Rock";
    GeoNode sphere = makeGeoNode(GeoNodeType::Primitive, "n1");
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "n2");
    divide.inputs[0] = "n1";
    GeoNode noise = makeGeoNode(GeoNodeType::Noise, "n3");
    noise.inputs[0] = "n2";
    noise.amount = 0.18f;
    GeoNode material = makeGeoNode(GeoNodeType::Material, "n4");
    material.inputs[0] = "n3";
    material.color = {0.55f, 0.5f, 0.45f};
    material.roughness = 0.85f;
    GeoNode output = makeGeoNode(GeoNodeType::Output, "n5");
    output.inputs[0] = "n4";
    graph.nodes = {sphere, divide, noise, material, output};
    return graph;
}

bool geoInputWouldLoop(const GeometryGraph& graph, int node, const std::string& input) {
    if (node < 0 || node >= static_cast<int>(graph.nodes.size())) {
        return true;
    }
    const std::string& self = graph.nodes[static_cast<size_t>(node)].id;
    std::set<std::string> seen;
    std::vector<std::string> stack{input};
    while (!stack.empty()) {
        const std::string current = stack.back();
        stack.pop_back();
        if (current.empty()) {
            continue;
        }
        if (current == self) {
            return true;
        }
        if (!seen.insert(current).second) {
            continue;
        }
        const int index = geoNodeIndex(graph, current);
        if (index < 0) {
            continue;
        }
        for (const std::string& next : graph.nodes[static_cast<size_t>(index)].inputs) {
            stack.push_back(next);
        }
    }
    return false;
}

bool geoRemoveNode(GeometryGraph& graph, int index) {
    if (index < 0 || index >= static_cast<int>(graph.nodes.size())) {
        return false;
    }
    const std::string id = graph.nodes[static_cast<size_t>(index)].id;
    graph.nodes.erase(graph.nodes.begin() + index);
    for (GeoNode& node : graph.nodes) {
        for (std::string& input : node.inputs) {
            if (input == id) {
                input.clear();
            }
        }
    }
    return true;
}

void sanitizeGeometryGraph(GeometryGraph& graph, std::vector<std::string>* notes) {
    graph.name = cleanName(graph.name, "Graph");
    if (graph.nodes.size() > kGeoMaxNodes) {
        if (notes) {
            addNote(*notes, "Too many nodes in a geometry graph, the rest were dropped: " + graph.name);
        }
        graph.nodes.resize(kGeoMaxNodes);
    }
    std::set<std::string> used;
    for (GeoNode& node : graph.nodes) {
        if (!safeId(node.id) || !used.insert(node.id).second) {
            std::string fresh;
            for (size_t n = 1; fresh.empty(); ++n) {
                const std::string candidate = "n" + std::to_string(n);
                if (!used.count(candidate)) {
                    bool taken = false;
                    for (const GeoNode& other : graph.nodes) {
                        taken = taken || (&other != &node && other.id == candidate);
                    }
                    if (!taken) {
                        fresh = candidate;
                    }
                }
            }
            if (notes) {
                addNote(*notes, "Node id was missing or repeated and was replaced: " + graph.name + ", " + fresh);
            }
            node.id = fresh;
            used.insert(fresh);
        }
        const int type = static_cast<int>(node.type);
        if (type < 0 || type >= kGeoNodeTypeCount) {
            node.type = GeoNodeType::Primitive;
        }
        for (std::string& input : node.inputs) {
            if (input.size() > kGeoMaxIdBytes) {
                input.clear();
            }
        }
        const int kind = static_cast<int>(node.primitive);
        if (kind < 0 || kind > static_cast<int>(PrimitiveKind::TorusKnot)) {
            node.primitive = PrimitiveKind::Sphere;
        }
        node.levels = clampInt(node.levels, 1, kGeoMaxLevels);
        node.move = {clampFinite(node.move.x, -100.0f, 100.0f, 0.0f), clampFinite(node.move.y, -100.0f, 100.0f, 0.0f), clampFinite(node.move.z, -100.0f, 100.0f, 0.0f)};
        node.rotate = {clampFinite(node.rotate.x, -720.0f, 720.0f, 0.0f), clampFinite(node.rotate.y, -720.0f, 720.0f, 0.0f), clampFinite(node.rotate.z, -720.0f, 720.0f, 0.0f)};
        for (float* s : {&node.scale.x, &node.scale.y, &node.scale.z}) {
            *s = clampFinite(*s, -100.0f, 100.0f, 1.0f);
            if (std::fabs(*s) < 0.001f) {
                *s = *s < 0.0f ? -0.001f : 0.001f;
            }
        }
        node.amount = node.type == GeoNodeType::Twist ? clampFinite(node.amount, -720.0f, 720.0f, 90.0f) : clampFinite(node.amount, -2.0f, 2.0f, 0.2f);
        node.frequency = clampFinite(node.frequency, 0.05f, 32.0f, 2.0f);
        node.octaves = clampInt(node.octaves, 1, kGeoMaxOctaves);
        node.seed = clampInt(node.seed, 0, 99999);
        const int instance = static_cast<int>(node.instance);
        if (instance < 0 || instance >= kGeoInstanceKindCount) {
            node.instance = GeoInstanceKind::Ring;
        }
        node.count = clampInt(node.count, 1, static_cast<int>(kGeoMaxInstances));
        node.radius = clampFinite(node.radius, 0.0f, 50.0f, 1.5f);
        node.spacing = clampFinite(node.spacing, 0.0f, 50.0f, 0.6f);
        node.jitter = clampFinite(node.jitter, 0.0f, 1.0f, 0.0f);
        for (float& c : node.color) {
            c = clampFinite(c, 0.0f, 1.0f, 0.8f);
        }
        for (float& c : node.emissive) {
            c = clampFinite(c, 0.0f, 1.0f, 0.0f);
        }
        node.metallic = clampFinite(node.metallic, 0.0f, 1.0f, 0.0f);
        node.roughness = clampFinite(node.roughness, 0.04f, 1.0f, 0.5f);
        node.emissiveStrength = clampFinite(node.emissiveStrength, 0.0f, 20.0f, 0.0f);
        const int source = static_cast<int>(node.source);
        if (source < 0 || source >= kBindSourceCount) {
            node.source = BindSource::Bass;
        }
        bool targetAllowed = false;
        for (const BindTarget allowed : kGeoAudioTargets) {
            targetAllowed = targetAllowed || allowed == node.target;
        }
        if (!targetAllowed) {
            node.target = BindTarget::Scale;
        }
        node.bindAmount = clampFinite(node.bindAmount, -4.0f, 4.0f, 0.3f);
    }
}

void geometryGraphToJson(const GeometryGraph& graph, json& out) {
    out = json::object();
    out["name"] = graph.name;
    json nodes = json::array();
    for (const GeoNode& n : graph.nodes) {
        json entry;
        entry["id"] = n.id;
        entry["type"] = geoNodeKey(n.type);
        if (!n.inputs[0].empty() || !n.inputs[1].empty()) {
            entry["inputs"] = json::array({n.inputs[0], n.inputs[1]});
        }
        switch (n.type) {
        case GeoNodeType::Primitive:
            entry["primitive"] = primitiveKey(n.primitive);
            break;
        case GeoNodeType::Subdivide:
            entry["levels"] = n.levels;
            break;
        case GeoNodeType::Transform:
            entry["move"] = json::array({n.move.x, n.move.y, n.move.z});
            entry["rotate"] = json::array({n.rotate.x, n.rotate.y, n.rotate.z});
            entry["scale"] = json::array({n.scale.x, n.scale.y, n.scale.z});
            break;
        case GeoNodeType::Noise:
            entry["amount"] = n.amount;
            entry["frequency"] = n.frequency;
            entry["octaves"] = n.octaves;
            entry["seed"] = n.seed;
            break;
        case GeoNodeType::Twist:
            entry["amount"] = n.amount;
            break;
        case GeoNodeType::Instance:
            entry["instance"] = geoInstanceKey(n.instance);
            entry["count"] = n.count;
            entry["radius"] = n.radius;
            entry["spacing"] = n.spacing;
            entry["jitter"] = n.jitter;
            entry["seed"] = n.seed;
            break;
        case GeoNodeType::Material:
            entry["color"] = json::array({n.color[0], n.color[1], n.color[2]});
            entry["metallic"] = n.metallic;
            entry["roughness"] = n.roughness;
            entry["emissive"] = json::array({n.emissive[0], n.emissive[1], n.emissive[2]});
            entry["emissiveStrength"] = n.emissiveStrength;
            entry["followPalette"] = n.followPalette;
            break;
        case GeoNodeType::Audio:
            entry["source"] = sourceKey(n.source);
            entry["target"] = targetKey(n.target);
            entry["amount"] = n.bindAmount;
            break;
        default:
            break;
        }
        nodes.push_back(entry);
    }
    out["nodes"] = nodes;
}

namespace {

float numberOr(const json& object, const char* key, float fallback) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<float>() : fallback;
}

int intOr(const json& object, const char* key, int fallback) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? static_cast<int>(std::lround(found->get<double>())) : fallback;
}

std::string textOr(const json& object, const char* key, const std::string& fallback) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : fallback;
}

bool vec3Of(const json& object, const char* key, Vec3& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_array() || found->size() != 3) {
        return false;
    }
    for (const json& v : *found) {
        if (!v.is_number()) {
            return false;
        }
    }
    out = {(*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>()};
    return true;
}

bool color3Of(const json& object, const char* key, std::array<float, 3>& out) {
    Vec3 v;
    if (!vec3Of(object, key, v)) {
        return false;
    }
    out = {v.x, v.y, v.z};
    return true;
}

}

bool geometryGraphFromJson(const json& in, GeometryGraph& graph, std::vector<std::string>* notes) {
    graph = GeometryGraph{};
    if (!in.is_object()) {
        return false;
    }
    graph.name = textOr(in, "name", "Graph");
    const auto nodes = in.find("nodes");
    if (nodes == in.end() || !nodes->is_array()) {
        return false;
    }
    for (const json& entry : *nodes) {
        if (!entry.is_object()) {
            continue;
        }
        if (graph.nodes.size() >= kGeoMaxNodes) {
            if (notes) {
                addNote(*notes, "Too many nodes in a geometry graph, the rest were dropped: " + graph.name);
            }
            break;
        }
        GeoNode node;
        node.id = textOr(entry, "id", "");
        const std::string typeKey = textOr(entry, "type", "");
        if (!geoNodeFromKey(typeKey, node.type)) {
            if (notes) {
                addNote(*notes, "Unknown node type, node left out: " + graph.name + ", " + typeKey.substr(0, 24));
            }
            continue;
        }
        const auto inputs = entry.find("inputs");
        if (inputs != entry.end() && inputs->is_array()) {
            for (size_t i = 0; i < 2 && i < inputs->size(); ++i) {
                if ((*inputs)[i].is_string()) {
                    node.inputs[i] = (*inputs)[i].get<std::string>();
                }
            }
        }
        PrimitiveKind kind;
        if (primitiveFromKey(textOr(entry, "primitive", ""), kind)) {
            node.primitive = kind;
        }
        node.levels = intOr(entry, "levels", node.levels);
        vec3Of(entry, "move", node.move);
        vec3Of(entry, "rotate", node.rotate);
        vec3Of(entry, "scale", node.scale);
        if (node.type == GeoNodeType::Noise || node.type == GeoNodeType::Twist) {
            node.amount = numberOr(entry, "amount", node.type == GeoNodeType::Twist ? 90.0f : node.amount);
        }
        node.frequency = numberOr(entry, "frequency", node.frequency);
        node.octaves = intOr(entry, "octaves", node.octaves);
        node.seed = intOr(entry, "seed", node.seed);
        geoInstanceFromKey(textOr(entry, "instance", ""), node.instance);
        node.count = intOr(entry, "count", node.count);
        node.radius = numberOr(entry, "radius", node.radius);
        node.spacing = numberOr(entry, "spacing", node.spacing);
        node.jitter = numberOr(entry, "jitter", node.jitter);
        color3Of(entry, "color", node.color);
        color3Of(entry, "emissive", node.emissive);
        node.metallic = numberOr(entry, "metallic", node.metallic);
        node.roughness = numberOr(entry, "roughness", node.roughness);
        node.emissiveStrength = numberOr(entry, "emissiveStrength", node.emissiveStrength);
        const auto follow = entry.find("followPalette");
        node.followPalette = follow != entry.end() && follow->is_boolean() && follow->get<bool>();
        if (node.type == GeoNodeType::Audio) {
            const std::string source = textOr(entry, "source", "");
            const std::string target = textOr(entry, "target", "");
            bool sourceKnown = false;
            bool targetKnown = false;
            for (int i = 0; i < kBindSourceCount; ++i) {
                if (source == sourceKey(static_cast<BindSource>(i))) {
                    node.source = static_cast<BindSource>(i);
                    sourceKnown = true;
                }
            }
            for (const BindTarget allowed : kGeoAudioTargets) {
                if (target == targetKey(allowed)) {
                    node.target = allowed;
                    targetKnown = true;
                }
            }
            if ((!sourceKnown || !targetKnown) && notes) {
                addNote(*notes, "Unknown audio source or target, Bass to Size used: " + graph.name + ", " + node.id);
            }
            node.bindAmount = numberOr(entry, "amount", node.bindAmount);
        }
        graph.nodes.push_back(node);
    }
    sanitizeGeometryGraph(graph, notes);
    return true;
}

uint64_t geometryGraphHash(const GeometryGraph& graph) {
    json out;
    geometryGraphToJson(graph, out);
    return fnv(out.dump());
}

GeoResult evaluateGeometryGraph(const GeometryGraph& graph, const GeoLimits& limits) {
    const auto begin = std::chrono::steady_clock::now();
    GeoResult result;
    result.hash = geometryGraphHash(graph);
    const auto finish = [&]() {
        result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    };
    if (graph.nodes.empty()) {
        result.errors.push_back("The graph has no nodes");
        finish();
        return result;
    }
    int outputIndex = -1;
    int outputs = 0;
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].type == GeoNodeType::Output) {
            ++outputs;
            outputIndex = static_cast<int>(i);
        }
    }
    if (outputs != 1) {
        result.errors.push_back(outputs == 0 ? "The graph needs an Output node" : "The graph has more than one Output node");
        finish();
        return result;
    }
    Evaluator evaluator{graph, limits, result, std::vector<int>(graph.nodes.size(), 0), std::vector<std::shared_ptr<Flow>>(graph.nodes.size())};
    const std::shared_ptr<Flow> flow = evaluator.visit(outputIndex);
    if (evaluator.failed || !flow) {
        if (result.errors.empty()) {
            result.errors.push_back("The graph could not be evaluated");
        }
        finish();
        return result;
    }
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        if (evaluator.state[i] == 0) {
            addNote(result.notes, "Not connected to the output, skipped: " + graph.nodes[i].id);
        }
    }
    if (flow->mesh.positions.empty() || flow->mesh.indices.empty()) {
        result.errors.push_back("The graph produced no geometry");
        finish();
        return result;
    }
    const size_t drawnTriangles = std::max<size_t>(flow->instances.size(), 1) * (flow->mesh.indices.size() / 3);
    if (drawnTriangles > limits.maxDrawnTriangles) {
        result.errors.push_back("Too many triangles to draw: " + graph.nodes[static_cast<size_t>(outputIndex)].id + ", " + std::to_string(drawnTriangles) + " > " + std::to_string(limits.maxDrawnTriangles));
        finish();
        return result;
    }
    result.mesh = flow->mesh;
    result.instances = flow->instances;
    result.hasMaterial = flow->hasMaterial;
    result.material = flow->material;
    result.bindings = flow->bindings;
    result.vertices = result.mesh.positions.size();
    result.indices = result.mesh.indices.size();
    result.ok = true;
    finish();
    return result;
}

std::shared_ptr<const GeoResult> evaluateGeometryGraphCached(const GeometryGraph& graph, bool* hit) {
    const uint64_t hash = geometryGraphHash(graph);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto it = cacheList.begin(); it != cacheList.end(); ++it) {
            if (it->hash == hash) {
                cacheList.splice(cacheList.begin(), cacheList, it);
                ++cacheStats.hits;
                if (hit) {
                    *hit = true;
                }
                return cacheList.front().result;
            }
        }
    }
    auto result = std::make_shared<GeoResult>(evaluateGeometryGraph(graph));
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        ++cacheStats.misses;
        cacheList.push_front({hash, result});
        size_t vertices = 0;
        size_t entries = 0;
        for (auto it = cacheList.begin(); it != cacheList.end();) {
            vertices += it->result->vertices;
            ++entries;
            if (entries > kGeoCacheEntries || (entries > 1 && vertices > kGeoCacheVertices)) {
                vertices -= it->result->vertices;
                --entries;
                it = cacheList.erase(it);
            } else {
                ++it;
            }
        }
        cacheStats.entries = entries;
        cacheStats.vertices = vertices;
    }
    if (hit) {
        *hit = false;
    }
    return result;
}

void setGeometryGraphsEnabled(bool enabled) {
    graphsEnabled.store(enabled);
}

bool geometryGraphsEnabled() {
    return graphsEnabled.load();
}

GeoCacheStats geometryGraphCacheStats() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return cacheStats;
}

void geometryGraphCacheClear() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cacheList.clear();
    cacheStats = GeoCacheStats{};
}

}

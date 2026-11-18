#include "scene3d/primitives.hpp"

#include "scene3d/procedural.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace nvj {

namespace {

constexpr float kPi = 3.14159265358979f;

void fixWinding(SceneMesh& mesh) {
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.positions[mesh.indices[i]];
        const Vec3& b = mesh.positions[mesh.indices[i + 1]];
        const Vec3& c = mesh.positions[mesh.indices[i + 2]];
        const Vec3 face = cross(b - a, c - a);
        const Vec3 average = mesh.normals[mesh.indices[i]] + mesh.normals[mesh.indices[i + 1]] + mesh.normals[mesh.indices[i + 2]];
        if (dot(face, average) < 0.0f) {
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }
}

void addVertex(SceneMesh& mesh, Vec3 position, Vec3 normal, float u, float v) {
    mesh.positions.push_back(position);
    mesh.normals.push_back(normal);
    mesh.uvs.push_back(u);
    mesh.uvs.push_back(v);
}

void normalizeExtent(SceneMesh& mesh) {
    constexpr float big = std::numeric_limits<float>::max();
    Vec3 lo{big, big, big};
    Vec3 hi{-big, -big, -big};
    for (const Vec3& p : mesh.positions) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const Vec3 center = (lo + hi) * 0.5f;
    const float extent = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f});
    for (Vec3& p : mesh.positions) {
        p = (p - center) * (1.0f / extent);
    }
}

void ensureUvs(SceneMesh& mesh) {
    if (!mesh.uvs.empty()) {
        return;
    }
    for (const Vec3& p : mesh.positions) {
        mesh.uvs.push_back(std::atan2(p.z, p.x) / (2.0f * kPi) + 0.5f);
        mesh.uvs.push_back(std::acos(std::clamp(p.y / std::max(length(p), 1e-6f), -1.0f, 1.0f)) / kPi);
    }
}

SceneMesh stripColors(SceneMesh mesh) {
    mesh.colors.clear();
    return mesh;
}

SceneMesh cubeMesh() {
    SceneMesh mesh;
    struct Face {
        Vec3 n;
        Vec3 u;
        Vec3 v;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const Face& f : faces) {
        const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
        const Vec3 c = f.n * 0.5f;
        addVertex(mesh, c - f.u * 0.5f - f.v * 0.5f, f.n, 0.0f, 0.0f);
        addVertex(mesh, c + f.u * 0.5f - f.v * 0.5f, f.n, 1.0f, 0.0f);
        addVertex(mesh, c + f.u * 0.5f + f.v * 0.5f, f.n, 1.0f, 1.0f);
        addVertex(mesh, c - f.u * 0.5f + f.v * 0.5f, f.n, 0.0f, 1.0f);
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    fixWinding(mesh);
    return mesh;
}

SceneMesh coneOrCylinder(bool cone) {
    constexpr int segments = 48;
    SceneMesh mesh;
    for (int j = 0; j <= segments; ++j) {
        const float a = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        const float u = static_cast<float>(j) / static_cast<float>(segments);
        if (cone) {
            const Vec3 n = normalize(Vec3{ca, 0.5f, sa});
            addVertex(mesh, {0.5f * ca, -0.5f, 0.5f * sa}, n, u, 0.0f);
            addVertex(mesh, {0.0f, 0.5f, 0.0f}, n, u, 1.0f);
        } else {
            addVertex(mesh, {0.5f * ca, -0.5f, 0.5f * sa}, {ca, 0.0f, sa}, u, 0.0f);
            addVertex(mesh, {0.5f * ca, 0.5f, 0.5f * sa}, {ca, 0.0f, sa}, u, 1.0f);
        }
    }
    for (int j = 0; j < segments; ++j) {
        const uint32_t a = static_cast<uint32_t>(j) * 2;
        mesh.indices.insert(mesh.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    const int caps = cone ? 1 : 2;
    for (int cap = 0; cap < caps; ++cap) {
        const float y = cap == 0 ? -0.5f : 0.5f;
        const Vec3 normal{0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f};
        const uint32_t center = static_cast<uint32_t>(mesh.positions.size());
        addVertex(mesh, {0.0f, y, 0.0f}, normal, 0.5f, 0.5f);
        for (int j = 0; j <= segments; ++j) {
            const float a = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
            addVertex(mesh, {0.5f * std::cos(a), y, 0.5f * std::sin(a)}, normal, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
        }
        for (int j = 0; j < segments; ++j) {
            const uint32_t ring = center + 1 + static_cast<uint32_t>(j);
            mesh.indices.insert(mesh.indices.end(), {center, ring, ring + 1});
        }
    }
    fixWinding(mesh);
    return mesh;
}

SceneMesh planeMesh() {
    SceneMesh mesh;
    addVertex(mesh, {-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f);
    addVertex(mesh, {0.5f, 0.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.0f);
    addVertex(mesh, {0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f);
    addVertex(mesh, {-0.5f, 0.0f, 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f, 1.0f);
    mesh.indices = {0, 2, 1, 0, 3, 2};
    fixWinding(mesh);
    return mesh;
}

}

const char* primitiveKey(PrimitiveKind kind) {
    static const char* const keys[kPrimitiveKindCount] = {"sphere", "cube", "cylinder", "cone", "plane", "torus", "torusknot", "model"};
    const int index = static_cast<int>(kind);
    return index >= 0 && index < kPrimitiveKindCount ? keys[index] : "sphere";
}

const char* primitiveLabel(PrimitiveKind kind) {
    static const char* const labels[kPrimitiveKindCount] = {"Sphere", "Cube", "Cylinder", "Cone", "Plane", "Torus", "Torus Knot", "Model"};
    const int index = static_cast<int>(kind);
    return index >= 0 && index < kPrimitiveKindCount ? labels[index] : "Sphere";
}

bool primitiveFromKey(const std::string& key, PrimitiveKind& kind) {
    for (int i = 0; i < kPrimitiveKindCount; ++i) {
        if (key == primitiveKey(static_cast<PrimitiveKind>(i))) {
            kind = static_cast<PrimitiveKind>(i);
            return true;
        }
    }
    return false;
}

SceneMesh primitiveMesh(PrimitiveKind kind) {
    SceneMesh mesh;
    switch (kind) {
    case PrimitiveKind::Sphere:
        mesh = stripColors(plainSphereMesh());
        break;
    case PrimitiveKind::Cube:
        mesh = cubeMesh();
        break;
    case PrimitiveKind::Cylinder:
        mesh = coneOrCylinder(false);
        break;
    case PrimitiveKind::Cone:
        mesh = coneOrCylinder(true);
        break;
    case PrimitiveKind::Plane:
        mesh = planeMesh();
        break;
    case PrimitiveKind::Torus:
        mesh = stripColors(plainTorusMesh());
        normalizeExtent(mesh);
        break;
    case PrimitiveKind::TorusKnot:
        mesh = stripColors(plainTorusKnotMesh());
        normalizeExtent(mesh);
        break;
    case PrimitiveKind::Model:
        break;
    }
    if (kind == PrimitiveKind::Sphere) {
        for (Vec3& p : mesh.positions) {
            p = p * 0.5f;
        }
    }
    ensureUvs(mesh);
    return mesh;
}

}

#include "scene3d/procedural.hpp"

#include "scene3d/builtin_docs.hpp"

#include <cmath>
#include <string>
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

void addColor(SceneMesh& mesh, float luminance) {
    mesh.colors.insert(mesh.colors.end(), {luminance, luminance, luminance, 1.0f});
}

SceneMesh makeTorus(float major, float minor, int segments, int rings) {
    SceneMesh mesh;
    for (int i = 0; i <= segments; ++i) {
        const float theta = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        for (int j = 0; j <= rings; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(rings);
            const float ring = major + minor * std::cos(phi);
            mesh.positions.push_back({ring * std::cos(theta), minor * std::sin(phi), ring * std::sin(theta)});
            mesh.normals.push_back({std::cos(phi) * std::cos(theta), std::sin(phi), std::cos(phi) * std::sin(theta)});
            addColor(mesh, 0.75f + 0.25f * std::cos(phi));
        }
    }
    const uint32_t stride = static_cast<uint32_t>(rings + 1);
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < rings; ++j) {
            const uint32_t a = static_cast<uint32_t>(i) * stride + static_cast<uint32_t>(j);
            const uint32_t b = a + stride;
            const uint32_t c = a + 1;
            const uint32_t d = b + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

SceneMesh makeSphere(float radius, int segments, int rings) {
    SceneMesh mesh;
    for (int i = 0; i <= rings; ++i) {
        const float v = kPi * static_cast<float>(i) / static_cast<float>(rings);
        for (int j = 0; j <= segments; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
            const Vec3 n{std::sin(v) * std::cos(phi), std::cos(v), std::sin(v) * std::sin(phi)};
            mesh.positions.push_back(n * radius);
            mesh.normals.push_back(n);
            addColor(mesh, 0.6f + 0.4f * (0.5f + 0.5f * n.y));
        }
    }
    const uint32_t stride = static_cast<uint32_t>(segments + 1);
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const uint32_t a = static_cast<uint32_t>(i) * stride + static_cast<uint32_t>(j);
            const uint32_t b = a + 1;
            const uint32_t c = a + stride;
            const uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    return mesh;
}

Vec3 knotPoint(float u, float p, float q) {
    const float cu = p * u;
    const float qp = q / p * cu;
    const float radius = (2.0f + std::cos(qp)) * 0.5f;
    return {std::cos(cu) * radius, std::sin(cu) * radius, std::sin(qp) * 0.5f};
}

SceneMesh makeTorusKnot(float p, float q, float tube, int tubular, int radial) {
    SceneMesh mesh;
    for (int i = 0; i <= tubular; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(tubular) * 2.0f * kPi;
        const Vec3 center = knotPoint(u, p, q);
        const Vec3 ahead = knotPoint(u + 0.01f, p, q);
        const Vec3 tangent = ahead - center;
        Vec3 n = ahead + center;
        const Vec3 b = normalize(cross(tangent, n));
        n = normalize(cross(b, tangent));
        const float lum = 0.62f + 0.38f * std::cos(u * q);
        for (int j = 0; j <= radial; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(radial) * 2.0f * kPi;
            const Vec3 normal = n * std::sin(v) + b * std::cos(v);
            mesh.positions.push_back(center + normal * tube);
            mesh.normals.push_back(normal);
            addColor(mesh, lum);
        }
    }
    const uint32_t stride = static_cast<uint32_t>(radial + 1);
    for (int i = 0; i < tubular; ++i) {
        for (int j = 0; j < radial; ++j) {
            const uint32_t a = static_cast<uint32_t>(i) * stride + static_cast<uint32_t>(j);
            const uint32_t b = a + stride;
            mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, b, b + 1, a + 1});
        }
    }
    fixWinding(mesh);
    return mesh;
}

void addFlatTriangle(SceneMesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 inside, float la, float lb, float lc) {
    Vec3 normal = cross(b - a, c - a);
    const Vec3 center = (a + b + c) * (1.0f / 3.0f);
    if (dot(normal, center - inside) < 0.0f) {
        std::swap(b, c);
        std::swap(lb, lc);
        normal = normal * -1.0f;
    }
    normal = normalize(normal);
    const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), {a, b, c});
    mesh.normals.insert(mesh.normals.end(), {normal, normal, normal});
    addColor(mesh, la);
    addColor(mesh, lb);
    addColor(mesh, lc);
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
}

SceneMesh makeCrystal() {
    SceneMesh mesh;
    constexpr int sides = 6;
    constexpr float radius = 0.34f;
    constexpr float height = 1.5f;
    constexpr float tip = 0.55f;
    const Vec3 inside{0.0f, height * 0.5f, 0.0f};
    const Vec3 apex{0.0f, height + tip, 0.0f};
    const Vec3 bottom{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * static_cast<float>(i) / sides;
        const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / sides;
        const Vec3 b0{radius * std::cos(a0), 0.0f, radius * std::sin(a0)};
        const Vec3 b1{radius * std::cos(a1), 0.0f, radius * std::sin(a1)};
        const Vec3 t0{b0.x, height, b0.z};
        const Vec3 t1{b1.x, height, b1.z};
        const float shade = 0.55f + 0.35f * (0.5f + 0.5f * std::cos(a0 * 1.0f - 0.6f));
        addFlatTriangle(mesh, b0, b1, t1, inside, shade * 0.6f, shade * 0.6f, shade);
        addFlatTriangle(mesh, b0, t1, t0, inside, shade * 0.6f, shade, shade);
        addFlatTriangle(mesh, t0, t1, apex, inside, shade, shade, 1.0f);
        addFlatTriangle(mesh, b0, bottom, b1, inside, 0.4f, 0.4f, 0.4f);
    }
    return mesh;
}

uint32_t nextRandom(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

float randomUnit(uint32_t& state) {
    return static_cast<float>(nextRandom(state) & 0xFFFFu) / 65535.0f;
}

int addNode(Scene& scene, const std::string& name, Vec3 t, Quat r, Vec3 s) {
    SceneNode node;
    node.name = name;
    node.translation = t;
    node.rotation = r;
    node.scale = s;
    scene.nodes.push_back(std::move(node));
    return static_cast<int>(scene.nodes.size()) - 1;
}

SceneMaterial plainMaterial() {
    SceneMaterial m;
    m.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    m.doubleSided = false;
    return m;
}

SceneChannel rotationChannel(int node, Vec3 axis, float seconds, int turns) {
    SceneChannel channel;
    channel.node = node;
    channel.path = AnimPath::Rotation;
    channel.interp = AnimInterp::Linear;
    const int keys = 4 * turns;
    for (int k = 0; k <= keys; ++k) {
        channel.times.push_back(seconds * static_cast<float>(k) / static_cast<float>(keys));
        const Quat q = axisAngle(axis, 2.0f * kPi * static_cast<float>(k) / 4.0f);
        channel.values.insert(channel.values.end(), {q.x, q.y, q.z, q.w});
    }
    return channel;
}

}

SceneMesh plainSphereMesh() {
    return makeSphere(1.0f, 48, 32);
}

SceneMesh plainTorusMesh() {
    return makeTorus(1.0f, 0.36f, 64, 32);
}

SceneMesh plainTorusKnotMesh() {
    return makeTorusKnot(2.0f, 3.0f, 0.3f, 220, 20);
}

Scene makeTorusKnotScene() {
    Scene scene;
    scene.name = "Torus Knot";
    scene.paletteTint = true;
    scene.materials.push_back(plainMaterial());
    SceneMesh mesh = makeTorusKnot(2.0f, 3.0f, 0.16f, 240, 20);
    mesh.material = 0;
    scene.meshes.push_back(std::move(mesh));
    const int node = addNode(scene, "Knot", {}, Quat{}, {1.0f, 1.0f, 1.0f});
    scene.nodes[static_cast<size_t>(node)].meshes = {0};
    scene.roots = {node};
    return scene;
}

Scene makeCrystalClusterScene() {
    Scene scene;
    scene.name = "Crystal Cluster";
    scene.paletteTint = true;
    scene.materials.push_back(plainMaterial());
    SceneMesh mesh = makeCrystal();
    mesh.material = 0;
    scene.meshes.push_back(std::move(mesh));
    const int root = addNode(scene, "Cluster", {}, Quat{}, {1.0f, 1.0f, 1.0f});
    uint32_t seed = 20261118u;
    constexpr int count = 16;
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(i) * 2.39996323f;
        const float spread = 0.15f + std::sqrt(static_cast<float>(i) / count) * 1.35f;
        const Vec3 position{spread * std::cos(angle), 0.0f, spread * std::sin(angle)};
        const float tilt = (0.12f + 0.42f * randomUnit(seed)) * (0.4f + spread / 1.5f);
        const Vec3 tangent{-std::sin(angle), 0.0f, std::cos(angle)};
        const Quat lean = axisAngle(tangent, -tilt);
        const Quat yaw = axisAngle({0.0f, 1.0f, 0.0f}, randomUnit(seed) * kPi);
        const float tall = 0.55f + 0.95f * randomUnit(seed) * (1.1f - spread / 2.2f);
        const float wide = 0.6f + 0.55f * randomUnit(seed);
        const int node = addNode(scene, "Crystal " + std::to_string(i), position, lean * yaw, {wide, tall, wide});
        scene.nodes[static_cast<size_t>(node)].meshes = {0};
        scene.nodes[static_cast<size_t>(root)].children.push_back(node);
    }
    scene.roots = {root};
    return scene;
}

Scene makeOrbitRingsScene() {
    Scene scene;
    scene.name = "Orbit Rings";
    scene.paletteTint = true;
    scene.materials.push_back(plainMaterial());
    SceneMesh ringA = makeTorus(1.0f, 0.045f, 96, 12);
    SceneMesh ringB = makeTorus(1.45f, 0.055f, 120, 12);
    SceneMesh ringC = makeTorus(1.9f, 0.065f, 144, 12);
    SceneMesh core = makeSphere(0.42f, 40, 24);
    ringA.material = ringB.material = ringC.material = core.material = 0;
    scene.meshes.push_back(std::move(ringA));
    scene.meshes.push_back(std::move(ringB));
    scene.meshes.push_back(std::move(ringC));
    scene.meshes.push_back(std::move(core));

    const int root = addNode(scene, "Rings", {}, Quat{}, {1.0f, 1.0f, 1.0f});
    const int coreNode = addNode(scene, "Core", {}, Quat{}, {1.0f, 1.0f, 1.0f});
    scene.nodes[static_cast<size_t>(coreNode)].meshes = {3};
    scene.nodes[static_cast<size_t>(root)].children.push_back(coreNode);

    const Vec3 axes[3] = {{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float tilts[3] = {1.05f, 0.5f, 0.85f};
    const int turns[3] = {1, 2, 1};
    SceneAnimation animation;
    animation.name = "Spin";
    for (int i = 0; i < 3; ++i) {
        const int gimbal = addNode(scene, "Gimbal " + std::to_string(i), {}, Quat{}, {1.0f, 1.0f, 1.0f});
        const Vec3 tiltAxis = i == 1 ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
        const int ring = addNode(scene, "Ring " + std::to_string(i), {}, axisAngle(tiltAxis, tilts[i]), {1.0f, 1.0f, 1.0f});
        scene.nodes[static_cast<size_t>(ring)].meshes = {i};
        scene.nodes[static_cast<size_t>(gimbal)].children.push_back(ring);
        scene.nodes[static_cast<size_t>(root)].children.push_back(gimbal);
        animation.channels.push_back(rotationChannel(gimbal, axes[i], 16.0f, turns[i]));
    }

    SceneChannel pulse;
    pulse.node = coreNode;
    pulse.path = AnimPath::Scale;
    pulse.times = {0.0f, 2.0f, 4.0f};
    pulse.values = {1.0f, 1.0f, 1.0f, 1.18f, 1.18f, 1.18f, 1.0f, 1.0f, 1.0f};
    animation.channels.push_back(std::move(pulse));
    SceneChannel bob;
    bob.node = coreNode;
    bob.path = AnimPath::Translation;
    bob.times = {0.0f, 2.0f, 4.0f};
    bob.values = {0.0f, 0.0f, 0.0f, 0.0f, 0.12f, 0.0f, 0.0f, 0.0f, 0.0f};
    animation.channels.push_back(std::move(bob));
    animation.duration = 16.0f;
    scene.animations.push_back(std::move(animation));
    scene.roots = {root};
    return scene;
}

const std::vector<BuiltinSceneInfo>& builtinScenes() {
    static const std::vector<BuiltinSceneInfo> scenes = {
        {"Torus Knot Mesh", &makeTorusKnotScene},
        {"Crystal Cluster Mesh", &makeCrystalClusterScene},
        {"Orbit Rings Mesh", &makeOrbitRingsScene},
        {"Chrome Orbs Mesh", &makeChromeOrbsScene},
        {"Neon Cubes Mesh", &makeNeonCubesScene},
        {"Gold Knot Mesh", &makeGoldKnotScene},
        {"Pulse Columns Mesh", &makePulseColumnsScene},
    };
    return scenes;
}

}

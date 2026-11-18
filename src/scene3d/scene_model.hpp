#pragma once

#include "scene3d/scene_math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

struct SceneImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct SceneMaterial {
    std::array<float, 4> baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    int baseTexture = -1;
    bool doubleSided = false;
};

struct SceneMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<float> uvs;
    std::vector<float> colors;
    std::vector<uint32_t> indices;
    int material = -1;
};

struct SceneNode {
    std::string name;
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    std::vector<int> children;
    std::vector<int> meshes;
};

enum class AnimPath { Translation, Rotation, Scale };
enum class AnimInterp { Linear, Step };

struct SceneChannel {
    int node = -1;
    AnimPath path = AnimPath::Translation;
    AnimInterp interp = AnimInterp::Linear;
    std::vector<float> times;
    std::vector<float> values;
};

struct SceneAnimation {
    std::string name;
    float duration = 0.0f;
    std::vector<SceneChannel> channels;
};

struct SceneBounds {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};
    bool valid = false;
};

struct SceneSphere {
    Vec3 center;
    float radius = 0.0f;
    bool valid = false;
};

struct SceneInstance {
    int mesh = -1;
    Mat4 world;
};

struct SceneStats {
    size_t vertices = 0;
    size_t triangles = 0;
    size_t meshes = 0;
    size_t nodes = 0;
    size_t images = 0;
    size_t cpuBytes = 0;
    size_t gpuBytes = 0;
};

struct Scene {
    std::string name;
    bool paletteTint = false;
    std::vector<SceneImage> images;
    std::vector<SceneMaterial> materials;
    std::vector<SceneMesh> meshes;
    std::vector<SceneNode> nodes;
    std::vector<int> roots;
    std::vector<SceneAnimation> animations;
};

constexpr int kSceneMaxDepth = 64;
constexpr int kSceneVertexFloats = 12;

std::vector<SceneInstance> evaluateScene(const Scene& scene, int animation, float time);
SceneBounds sceneBounds(const Scene& scene, int animation, float time);
SceneSphere sceneSphere(const Scene& scene, int animation, int samples);
SceneStats sceneStats(const Scene& scene);
bool validateScene(const Scene& scene, std::string& error);
float sceneDuration(const Scene& scene, int animation);

}

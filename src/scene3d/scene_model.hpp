#pragma once

#include "scene3d/scene_math.hpp"

#include <algorithm>
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

enum class Shading { Classic, Pbr };
enum class AlphaMode { Opaque, Mask, Blend };
enum class MotionKind { Spin, Bob, Orbit, Pulse, Sway };
enum class BindSource { Bass, Mid, High, Level, Kick, Beat, Band1, Band2, Band3, Band4, Band5, Band6, Band7, Band8 };
enum class BindTarget { Scale, Emissive, Twist, Lift, Light, Zoom, Hue, Roughness, UvX, UvY, Inflate, Ripple };
constexpr int kBindSourceCount = 14;
constexpr int kBindTargetCount = 12;
constexpr int kBindOldSourceCount = 5;
constexpr int kBindOldTargetCount = 6;
constexpr int kSceneBands = 8;
constexpr float kSceneMaxHueTurns = 8.0f;
constexpr float kSceneMaxUvShift = 8.0f;
constexpr float kSceneMaxDeform = 1.0f;

constexpr bool bindTargetIsMaterial(BindTarget target) {
    return static_cast<int>(target) >= static_cast<int>(BindTarget::Hue);
}
enum class RepeatKind { None, Ring, Line };

struct SceneMaterial {
    std::array<float, 4> baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    int baseTexture = -1;
    bool doubleSided = false;
    float metallic = 0.0f;
    float roughness = 0.6f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 1.0f;
    int metallicRoughnessTexture = -1;
    int normalTexture = -1;
    float normalScale = 1.0f;
    int emissiveTexture = -1;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool followPalette = false;
};

struct NodeMotion {
    MotionKind kind = MotionKind::Spin;
    float speed = 0.25f;
    float amount = 1.0f;
    int axis = 1;
    float phase = 0.0f;
};

struct NodeBinding {
    BindSource source = BindSource::Bass;
    BindTarget target = BindTarget::Scale;
    float amount = 0.0f;
};

struct NodeRepeat {
    RepeatKind kind = RepeatKind::None;
    int count = 1;
    float radius = 2.0f;
    float spacing = 1.5f;
};

struct SceneAudio {
    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float level = 0.0f;
    float kick = 0.0f;
    float beat = 0.0f;
    std::array<float, kSceneBands> bands{};
    bool materials = true;
};

struct SceneMorphTarget {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
};

struct SceneMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<float> uvs;
    std::vector<float> colors;
    std::vector<uint32_t> indices;
    int material = -1;
    std::vector<uint16_t> joints;
    std::vector<float> weights;
    std::vector<SceneMorphTarget> targets;
    bool keptSkin = false;
    size_t keptTargets = 0;

    bool skinned() const { return keptSkin || !joints.empty(); }
    size_t morphTargetCount() const { return std::max(keptTargets, targets.size()); }
    void rememberAnimationData() {
        keptSkin = skinned();
        keptTargets = morphTargetCount();
    }
};

struct SceneSkin {
    std::string name;
    std::vector<int> joints;
    std::vector<Mat4> inverseBind;
};

struct SceneNode {
    std::string name;
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    std::vector<int> children;
    std::vector<int> meshes;
    std::vector<NodeMotion> motions;
    std::vector<NodeBinding> bindings;
    NodeRepeat repeat;
    bool framing = true;
    int skin = -1;
    std::vector<float> weights;
};

enum class AnimPath { Translation, Rotation, Scale, Weights };
enum class AnimInterp { Linear, Step };

struct SceneChannel {
    int node = -1;
    AnimPath path = AnimPath::Translation;
    AnimInterp interp = AnimInterp::Linear;
    float loop = 0.0f;
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
    float emissive = 0.0f;
    float hue = 0.0f;
    float roughness = 0.0f;
    float uvX = 0.0f;
    float uvY = 0.0f;
    float inflate = 0.0f;
    float ripple = 0.0f;
    bool framing = true;
    int skin = -1;
    int jointOffset = 0;
    int jointCount = 0;
    int weightOffset = 0;
    int weightCount = 0;
};

struct SceneEval {
    std::vector<SceneInstance> instances;
    std::vector<Mat4> joints;
    std::vector<float> weights;
    size_t skippedSkinned = 0;
};

struct SceneStats {
    size_t vertices = 0;
    size_t triangles = 0;
    size_t meshes = 0;
    size_t nodes = 0;
    size_t images = 0;
    size_t cpuBytes = 0;
    size_t gpuBytes = 0;
    size_t skins = 0;
    size_t joints = 0;
    size_t morphTargets = 0;
    size_t blendMaterials = 0;
};

struct SceneLight {
    bool point = false;
    Vec3 position{0.5f, 0.8f, 0.6f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    std::vector<NodeBinding> bindings;
};

struct SceneSky {
    std::array<float, 3> top{0.30f, 0.45f, 0.75f};
    std::array<float, 3> horizon{0.78f, 0.80f, 0.86f};
    std::array<float, 3> ground{0.10f, 0.09f, 0.11f};
    float intensity = 1.0f;
};

struct SceneCamera {
    bool authored = false;
    float fovDegrees = 46.0f;
    float distance = 1.0f;
    float pitchDegrees = 18.0f;
    float yawDegrees = 0.0f;
    float orbitSpeed = 0.4f;
    float pitchSway = 0.0f;
    std::vector<NodeBinding> bindings;
};

struct SceneFileCamera {
    std::string name;
    bool orthographic = false;
    float yfov = 0.8f;
    float xmag = 1.0f;
    float ymag = 1.0f;
    float znear = 0.1f;
    float zfar = 100.0f;
    Mat4 world;
};

struct Scene {
    std::string name;
    bool paletteTint = false;
    Shading shading = Shading::Classic;
    std::vector<SceneLight> lights;
    SceneSky sky;
    SceneCamera camera;
    std::vector<SceneCamera> cameras;
    float cameraCutBeats = 0.0f;
    float exposure = 1.0f;
    std::vector<SceneImage> images;
    std::vector<SceneMaterial> materials;
    std::vector<SceneMesh> meshes;
    std::vector<SceneNode> nodes;
    std::vector<int> roots;
    std::vector<SceneAnimation> animations;
    std::vector<SceneSkin> skins;
    std::vector<SceneLight> fileLights;
    std::vector<SceneFileCamera> fileCameras;
};

constexpr int kSceneMaxDepth = 64;
constexpr int kSceneVertexFloats = 12;
constexpr int kSceneMaxRepeat = 64;
constexpr size_t kSceneMaxInstances = 8192;
constexpr size_t kSceneMaxLights = 4;
constexpr size_t kSceneMaxSkins = 64;
constexpr size_t kSceneMaxBones = 128;
constexpr size_t kSceneMaxMorphTargets = 32;
constexpr size_t kSceneMaxActiveMorphs = 8;
constexpr size_t kSceneMaxJointsPerFrame = 1024;
constexpr size_t kSceneMaxFileLights = 16;
constexpr size_t kSceneMaxFileCameras = 16;

float audioValue(const SceneAudio& audio, BindSource source);
float beatPulse(double beat);
std::array<float, kSceneBands> spectrumBands(const float* spectrum, int bins);
float bindingSum(const std::vector<NodeBinding>& bindings, BindTarget target, const SceneAudio& audio);
std::vector<SceneInstance> evaluateScene(const Scene& scene, int animation, float time, const SceneAudio* audio = nullptr);
SceneEval evaluateSceneFull(const Scene& scene, int animation, float time, const SceneAudio* audio = nullptr);
std::vector<Vec3> posedPositions(const Scene& scene, const SceneMesh& mesh, const SceneEval& eval, const SceneInstance& instance);
constexpr size_t kSceneMaxGpuBytes = 640u * 1024u * 1024u;
SceneBounds sceneBounds(const Scene& scene, int animation, float time);
SceneSphere sceneSphere(const Scene& scene, int animation, int samples);
SceneStats sceneStats(const Scene& scene);
bool validateScene(const Scene& scene, std::string& error);
const SceneCamera& activeSceneCamera(const Scene& scene, double beat);
float sceneDuration(const Scene& scene, int animation);

}

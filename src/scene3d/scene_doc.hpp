#pragma once

#include "scene3d/primitives.hpp"
#include "scene3d/scene_model.hpp"

#include <array>
#include <string>
#include <vector>

namespace nvj {

constexpr int kDocVersion = 4;
constexpr int kDocBaseVersion = 1;
constexpr int kDocBindingsVersion = 2;
constexpr int kDocCameraVersion = 3;
constexpr int kDocHierarchyVersion = 4;
constexpr size_t kDocMaxObjects = 64;
constexpr size_t kDocMaxLights = 4;
constexpr size_t kDocMaxCameras = 8;
constexpr float kDocMaxCutBeats = 64.0f;
constexpr size_t kDocMaxNameBytes = 64;
constexpr size_t kDocMaxBindings = 4;
constexpr size_t kDocMaxNotes = 16;
constexpr size_t kDocMaxAssetNameBytes = 96;
constexpr size_t kDocMaxIdBytes = 24;
constexpr int kDocMaxDepth = 8;

struct DocAnimation {
    bool enabled = false;
    MotionKind kind = MotionKind::Spin;
    float speed = 0.25f;
    float amount = 1.0f;
    int axis = 1;
    float phase = 0.0f;
};

struct DocBinding {
    bool enabled = false;
    BindSource source = BindSource::Bass;
    BindTarget target = BindTarget::Scale;
    float amount = 0.3f;
};

using DocBindings = std::array<DocBinding, kDocMaxBindings>;

struct DocMaterial {
    std::array<float, 3> color{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 0.0f;
    std::string texture;
    std::string normalTexture;
    std::string emissiveTexture;
    bool followPalette = false;
    bool doubleSided = false;
};

struct DocObject {
    std::string id;
    std::string parent;
    std::string name = "Object";
    PrimitiveKind kind = PrimitiveKind::Sphere;
    std::string model;
    Vec3 position;
    Vec3 rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    NodeRepeat repeat;
    DocMaterial material;
    std::array<DocAnimation, 2> animations;
    DocBindings bindings;
    bool framing = true;
    bool visible = true;
};

struct DocLight {
    std::string name = "Light";
    bool point = false;
    Vec3 position{0.5f, 0.8f, 0.6f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 3.0f;
    DocBindings bindings;
};

struct DocCamera {
    std::string name = "Camera 1";
    float distance = 1.0f;
    float pitchDegrees = 18.0f;
    float yawDegrees = 0.0f;
    float fovDegrees = 46.0f;
    float orbitSpeed = 0.4f;
    float pitchSway = 0.0f;
    Vec3 move{};
    float rollDegrees = 0.0f;
    float beats = 0.0f;
    DocBindings bindings;
};

struct DocSky {
    std::array<float, 3> top{0.30f, 0.45f, 0.75f};
    std::array<float, 3> horizon{0.78f, 0.80f, 0.86f};
    std::array<float, 3> ground{0.10f, 0.09f, 0.11f};
    float intensity = 1.0f;
};

struct SceneDoc {
    std::string name = "Untitled";
    std::vector<DocObject> objects;
    std::vector<DocLight> lights;
    DocCamera camera;
    std::vector<DocCamera> extraCameras;
    int activeCamera = 0;
    float cutBeats = 0.0f;
    float glide = 0.0f;
    CameraGlide glideShape = CameraGlide::Smooth;
    DocSky sky;
    float exposure = 1.0f;
    std::vector<std::string> notes;
};

bool isSafeAssetName(const std::string& name);
bool isModelAssetName(const std::string& name);
bool isTextureAssetName(const std::string& name);
DocObject makeDefaultObject(PrimitiveKind kind);
DocLight makeDefaultLight(bool point);
SceneDoc makeDefaultDoc();
int docCameraCount(const SceneDoc& doc);
DocCamera& docCameraAt(SceneDoc& doc, int index);
const DocCamera& docCameraAt(const SceneDoc& doc, int index);
int docAddCamera(SceneDoc& doc, const DocCamera& source);
bool docRemoveCamera(SceneDoc& doc, int index);
void sanitizeDoc(SceneDoc& doc);
int docRequiredVersion(const SceneDoc& doc);
bool docUsesCameraMoves(const SceneDoc& doc);
bool docUsesHierarchy(const SceneDoc& doc);
bool isSafeObjectId(const std::string& id);
std::string docNewObjectId(const SceneDoc& doc);
int docObjectById(const SceneDoc& doc, const std::string& id);
int docObjectDepth(const SceneDoc& doc, int index);
bool docCanParent(const SceneDoc& doc, int child, int parent);
bool docSetParent(SceneDoc& doc, int child, int parent);
void docRemoveObject(SceneDoc& doc, int index);
std::vector<int> docObjectOrder(const SceneDoc& doc);
DocCamera docCameraFromScene(const SceneCamera& camera, const std::string& name);
std::string docToJson(const SceneDoc& doc);
bool docFromJson(const std::string& text, SceneDoc& doc, std::string& error);
std::vector<std::string> docModelNames(const SceneDoc& doc);
std::vector<std::string> docTextureNames(const SceneDoc& doc);
const char* motionKey(MotionKind kind);
const char* sourceKey(BindSource source);
const char* targetKey(BindTarget target);

}

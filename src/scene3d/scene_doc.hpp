#pragma once

#include "scene3d/primitives.hpp"
#include "scene3d/scene_model.hpp"

#include <array>
#include <string>
#include <vector>

namespace nvj {

constexpr int kDocVersion = 1;
constexpr size_t kDocMaxObjects = 64;
constexpr size_t kDocMaxLights = 4;
constexpr size_t kDocMaxNameBytes = 64;
constexpr size_t kDocMaxAssetNameBytes = 96;

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
    std::string name = "Object";
    PrimitiveKind kind = PrimitiveKind::Sphere;
    std::string model;
    Vec3 position;
    Vec3 rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    NodeRepeat repeat;
    DocMaterial material;
    std::array<DocAnimation, 2> animations;
    std::array<DocBinding, 2> bindings;
    bool framing = true;
    bool visible = true;
};

struct DocLight {
    std::string name = "Light";
    bool point = false;
    Vec3 position{0.5f, 0.8f, 0.6f};
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 3.0f;
    std::array<DocBinding, 2> bindings;
};

struct DocCamera {
    float distance = 1.0f;
    float pitchDegrees = 18.0f;
    float yawDegrees = 0.0f;
    float fovDegrees = 46.0f;
    float orbitSpeed = 0.4f;
    float pitchSway = 0.0f;
    std::array<DocBinding, 2> bindings;
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
    DocSky sky;
    float exposure = 1.0f;
};

bool isSafeAssetName(const std::string& name);
bool isModelAssetName(const std::string& name);
bool isTextureAssetName(const std::string& name);
DocObject makeDefaultObject(PrimitiveKind kind);
DocLight makeDefaultLight(bool point);
SceneDoc makeDefaultDoc();
void sanitizeDoc(SceneDoc& doc);
std::string docToJson(const SceneDoc& doc);
bool docFromJson(const std::string& text, SceneDoc& doc, std::string& error);
std::vector<std::string> docModelNames(const SceneDoc& doc);
std::vector<std::string> docTextureNames(const SceneDoc& doc);
const char* motionKey(MotionKind kind);
const char* sourceKey(BindSource source);
const char* targetKey(BindTarget target);

}

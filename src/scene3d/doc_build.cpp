#include "scene3d/doc_build.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace nvj {

namespace {

constexpr float kDegrees = 0.01745329252f;

float linear(float v) {
    return std::pow(std::clamp(v, 0.0f, 1.0f), 2.2f);
}

Quat eulerToQuat(Vec3 degrees) {
    const Quat x = axisAngle({1.0f, 0.0f, 0.0f}, degrees.x * kDegrees);
    const Quat y = axisAngle({0.0f, 1.0f, 0.0f}, degrees.y * kDegrees);
    const Quat z = axisAngle({0.0f, 0.0f, 1.0f}, degrees.z * kDegrees);
    return y * x * z;
}

std::vector<NodeBinding> convertBindings(const std::array<DocBinding, 2>& bindings) {
    std::vector<NodeBinding> out;
    for (const DocBinding& b : bindings) {
        if (b.enabled && b.amount != 0.0f) {
            out.push_back({b.source, b.target, b.amount});
        }
    }
    return out;
}

bool cancelled(const GltfLimits& limits) {
    return limits.cancel != nullptr && limits.cancel->load();
}

void mergeModel(Scene& dst, const Scene& src, int wrapperIndex, int nodeOffset, int meshOffset, int materialOffset, int imageOffset) {
    dst.images.insert(dst.images.end(), src.images.begin(), src.images.end());
    for (SceneMaterial material : src.materials) {
        for (int* texture : {&material.baseTexture, &material.metallicRoughnessTexture, &material.normalTexture, &material.emissiveTexture}) {
            if (*texture >= 0) {
                *texture += imageOffset;
            }
        }
        dst.materials.push_back(material);
    }
    for (SceneMesh mesh : src.meshes) {
        if (mesh.material >= 0) {
            mesh.material += materialOffset;
        }
        dst.meshes.push_back(std::move(mesh));
    }
    for (SceneNode node : src.nodes) {
        for (int& child : node.children) {
            child += nodeOffset;
        }
        for (int& mesh : node.meshes) {
            mesh += meshOffset;
        }
        dst.nodes.push_back(std::move(node));
    }
    for (const int root : src.roots) {
        dst.nodes[static_cast<size_t>(wrapperIndex)].children.push_back(root + nodeOffset);
    }
    if (!src.animations.empty() && !src.animations.front().channels.empty()) {
        if (dst.animations.empty()) {
            dst.animations.push_back(SceneAnimation{"Models", 0.0f, {}});
        }
        SceneAnimation& combined = dst.animations.front();
        const SceneAnimation& first = src.animations.front();
        for (SceneChannel channel : first.channels) {
            channel.node += nodeOffset;
            channel.loop = first.duration;
            combined.channels.push_back(std::move(channel));
        }
        combined.duration = std::max(combined.duration, first.duration);
    }
}

}

GltfResult buildSceneFromDoc(const SceneDoc& source, const DocAssets& assets, const GltfLimits& limits) {
    const auto begin = std::chrono::steady_clock::now();
    GltfResult result;
    SceneDoc doc = source;
    sanitizeDoc(doc);
    Scene& scene = result.scene;
    scene.name = doc.name;
    scene.shading = Shading::Pbr;
    scene.exposure = doc.exposure;
    scene.sky.top = {linear(doc.sky.top[0]), linear(doc.sky.top[1]), linear(doc.sky.top[2])};
    scene.sky.horizon = {linear(doc.sky.horizon[0]), linear(doc.sky.horizon[1]), linear(doc.sky.horizon[2])};
    scene.sky.ground = {linear(doc.sky.ground[0]), linear(doc.sky.ground[1]), linear(doc.sky.ground[2])};
    scene.sky.intensity = doc.sky.intensity;
    scene.camera.authored = true;
    scene.camera.fovDegrees = doc.camera.fovDegrees;
    scene.camera.distance = doc.camera.distance;
    scene.camera.pitchDegrees = doc.camera.pitchDegrees;
    scene.camera.yawDegrees = doc.camera.yawDegrees;
    scene.camera.orbitSpeed = doc.camera.orbitSpeed;
    scene.camera.pitchSway = doc.camera.pitchSway;
    scene.camera.bindings = convertBindings(doc.camera.bindings);
    for (const DocLight& l : doc.lights) {
        SceneLight light;
        light.point = l.point;
        light.position = l.position;
        light.color = {linear(l.color[0]), linear(l.color[1]), linear(l.color[2])};
        light.intensity = l.intensity;
        light.bindings = convertBindings(l.bindings);
        scene.lights.push_back(std::move(light));
    }

    std::map<std::string, int> imageIndex;
    const auto imageFor = [&](const std::string& name) -> int {
        if (name.empty()) {
            return -1;
        }
        const auto known = imageIndex.find(name);
        if (known != imageIndex.end()) {
            return known->second;
        }
        int index = -1;
        std::string error;
        std::shared_ptr<const SceneImage> image = assets.loadImage ? assets.loadImage(name, error) : nullptr;
        if (image && !image->rgba.empty()) {
            scene.images.push_back(*image);
            index = static_cast<int>(scene.images.size()) - 1;
        } else {
            result.warnings.push_back("texture '" + name + "' is missing" + (error.empty() ? std::string() : ": " + error));
        }
        imageIndex[name] = index;
        return index;
    };

    size_t vertices = 0;
    for (const DocObject& object : doc.objects) {
        if (cancelled(limits)) {
            result.interrupted = true;
            result.error = "loading was cancelled";
            return result;
        }
        if (!object.visible) {
            continue;
        }
        SceneNode node;
        node.name = object.name;
        node.translation = object.position;
        node.rotation = eulerToQuat(object.rotation);
        node.scale = object.scale;
        node.framing = object.framing;
        if (object.repeat.kind != RepeatKind::None && object.repeat.count > 1) {
            node.repeat = object.repeat;
        }
        for (const DocAnimation& a : object.animations) {
            if (!a.enabled) {
                continue;
            }
            NodeMotion motion;
            motion.kind = a.kind;
            motion.speed = a.speed;
            motion.amount = a.kind == MotionKind::Sway ? a.amount * kDegrees : a.amount;
            motion.axis = a.axis;
            motion.phase = a.phase;
            node.motions.push_back(motion);
        }
        node.bindings = convertBindings(object.bindings);
        const int nodeIndex = static_cast<int>(scene.nodes.size());
        if (object.kind == PrimitiveKind::Model) {
            if (object.model.empty()) {
                result.warnings.push_back("object '" + object.name + "' has no model");
                continue;
            }
            std::shared_ptr<const GltfResult> model = assets.loadModel ? assets.loadModel(object.model, limits) : nullptr;
            if (cancelled(limits)) {
                result.interrupted = true;
                result.error = "loading was cancelled";
                return result;
            }
            if (!model || !model->ok) {
                result.warnings.push_back("model '" + object.model + "' could not be loaded" + (model && !model->error.empty() ? ": " + model->error : std::string()));
                continue;
            }
            for (const std::string& warning : model->warnings) {
                result.warnings.push_back(object.model + ": " + warning);
            }
            size_t modelVertices = 0;
            for (const SceneMesh& mesh : model->scene.meshes) {
                modelVertices += mesh.positions.size();
            }
            if (vertices + modelVertices > limits.maxVertices || scene.nodes.size() + model->scene.nodes.size() + 1 > limits.maxNodes || scene.meshes.size() + model->scene.meshes.size() > limits.maxMeshes) {
                result.error = "the scene is too large for its models";
                return result;
            }
            vertices += modelVertices;
            const int nodeOffset = nodeIndex + 1;
            scene.nodes.push_back(std::move(node));
            mergeModel(scene, model->scene, nodeIndex, nodeOffset, static_cast<int>(scene.meshes.size()), static_cast<int>(scene.materials.size()), static_cast<int>(scene.images.size()));
            scene.roots.push_back(nodeIndex);
            continue;
        }
        SceneMesh mesh = primitiveMesh(object.kind);
        vertices += mesh.positions.size();
        if (vertices > limits.maxVertices || scene.nodes.size() + 1 > limits.maxNodes || scene.meshes.size() + 1 > limits.maxMeshes) {
            result.error = "the scene is too large";
            return result;
        }
        SceneMaterial material;
        material.baseColor = {linear(object.material.color[0]), linear(object.material.color[1]), linear(object.material.color[2]), 1.0f};
        material.metallic = object.material.metallic;
        material.roughness = object.material.roughness;
        material.emissive = {linear(object.material.emissive[0]), linear(object.material.emissive[1]), linear(object.material.emissive[2])};
        material.emissiveStrength = object.material.emissiveStrength;
        material.followPalette = object.material.followPalette;
        material.doubleSided = object.material.doubleSided;
        material.baseTexture = imageFor(object.material.texture);
        material.normalTexture = imageFor(object.material.normalTexture);
        material.emissiveTexture = imageFor(object.material.emissiveTexture);
        scene.materials.push_back(material);
        mesh.material = static_cast<int>(scene.materials.size()) - 1;
        node.meshes = {static_cast<int>(scene.meshes.size())};
        scene.meshes.push_back(std::move(mesh));
        scene.nodes.push_back(std::move(node));
        scene.roots.push_back(nodeIndex);
    }
    if (scene.meshes.empty()) {
        result.error = "the scene has no objects that can be drawn";
        return result;
    }
    std::string error;
    if (!validateScene(scene, error)) {
        result.error = error;
        return result;
    }
    result.ok = true;
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return result;
}

}

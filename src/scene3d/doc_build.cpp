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

std::vector<NodeBinding> convertBindings(const DocBindings& bindings) {
    std::vector<NodeBinding> out;
    for (const DocBinding& b : bindings) {
        if (b.enabled && b.amount != 0.0f) {
            out.push_back({b.source, b.target, b.amount});
        }
    }
    return out;
}

SceneCamera convertCamera(const DocCamera& source) {
    SceneCamera camera;
    camera.authored = true;
    camera.fovDegrees = source.fovDegrees;
    camera.distance = source.distance;
    camera.pitchDegrees = source.pitchDegrees;
    camera.yawDegrees = source.yawDegrees;
    camera.orbitSpeed = source.orbitSpeed;
    camera.pitchSway = source.pitchSway;
    camera.move = source.move;
    camera.rollDegrees = source.rollDegrees;
    camera.beats = source.beats;
    camera.bindings = convertBindings(source.bindings);
    return camera;
}

bool cancelled(const GltfLimits& limits) {
    return limits.cancel != nullptr && limits.cancel->load();
}

struct MergedModel {
    int meshOffset = 0;
    int materialOffset = 0;
    int imageOffset = 0;
};

void mergeModel(Scene& dst, const Scene& src, int wrapperIndex, int nodeOffset, int meshOffset, int materialOffset, int imageOffset, bool addGeometry) {
    if (addGeometry) {
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
    }
    const int skinOffset = static_cast<int>(dst.skins.size());
    for (SceneSkin skin : src.skins) {
        for (int& joint : skin.joints) {
            joint += nodeOffset;
        }
        dst.skins.push_back(std::move(skin));
    }
    for (SceneNode node : src.nodes) {
        if (node.skin >= 0) {
            node.skin += skinOffset;
        }
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
    for (const std::string& note : doc.notes) {
        result.warnings.push_back(note);
    }
    scene.name = doc.name;
    scene.shading = Shading::Pbr;
    scene.exposure = doc.exposure;
    scene.sky.top = {linear(doc.sky.top[0]), linear(doc.sky.top[1]), linear(doc.sky.top[2])};
    scene.sky.horizon = {linear(doc.sky.horizon[0]), linear(doc.sky.horizon[1]), linear(doc.sky.horizon[2])};
    scene.sky.ground = {linear(doc.sky.ground[0]), linear(doc.sky.ground[1]), linear(doc.sky.ground[2])};
    scene.sky.intensity = doc.sky.intensity;
    scene.camera = convertCamera(docCameraAt(doc, doc.activeCamera));
    if (doc.cutBeats > 0.0f && docCameraCount(doc) > 1) {
        scene.cameraCutBeats = doc.cutBeats;
        scene.cameraGlide = doc.glide;
        scene.cameraGlideShape = doc.glideShape;
        for (int i = 0; i < docCameraCount(doc); ++i) {
            scene.cameras.push_back(convertCamera(docCameraAt(doc, i)));
        }
    }
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
    std::vector<bool> shown(doc.objects.size(), true);
    for (size_t i = 0; i < doc.objects.size(); ++i) {
        int at = static_cast<int>(i);
        int steps = 0;
        while (at >= 0 && steps <= kDocMaxDepth + 1) {
            if (!doc.objects[static_cast<size_t>(at)].visible) {
                shown[i] = false;
                break;
            }
            at = docObjectById(doc, doc.objects[static_cast<size_t>(at)].parent);
            ++steps;
        }
    }
    struct Link {
        int child = 0;
        std::string parent;
        std::string name;
    };
    std::vector<Link> links;
    std::map<std::string, int> nodeOfObject;
    std::map<std::string, MergedModel> mergedModels;
    const auto addRoot = [&](const DocObject& object, int nodeIndex) {
        nodeOfObject[object.id] = nodeIndex;
        if (object.parent.empty()) {
            scene.roots.push_back(nodeIndex);
        } else {
            links.push_back({nodeIndex, object.parent, object.name});
        }
    };
    for (size_t objectIndex = 0; objectIndex < doc.objects.size(); ++objectIndex) {
        const DocObject& object = doc.objects[objectIndex];
        if (cancelled(limits)) {
            result.interrupted = true;
            result.error = "loading was cancelled";
            return result;
        }
        if (!shown[objectIndex]) {
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
            const auto reused = mergedModels.find(object.model);
            const bool addGeometry = reused == mergedModels.end();
            size_t modelVertices = 0;
            for (const SceneMesh& mesh : model->scene.meshes) {
                modelVertices += addGeometry ? mesh.positions.size() : 0;
            }
            const size_t newMeshes = addGeometry ? model->scene.meshes.size() : 0;
            if (vertices + modelVertices > limits.maxVertices || scene.nodes.size() + model->scene.nodes.size() + 1 > limits.maxNodes || scene.meshes.size() + newMeshes > limits.maxMeshes) {
                result.error = "the scene is too large for its models";
                return result;
            }
            if (scene.skins.size() + model->scene.skins.size() > limits.maxSkins) {
                result.warnings.push_back("model '" + object.model + "' was skipped because the scene would have more than " + std::to_string(limits.maxSkins) + " animated figures");
                continue;
            }
            vertices += modelVertices;
            const int nodeOffset = nodeIndex + 1;
            const Mat4 wrapper = composeTrs(node.translation, node.rotation, node.scale);
            for (SceneFileCamera camera : model->scene.fileCameras) {
                if (scene.fileCameras.size() >= kSceneMaxFileCameras) {
                    break;
                }
                camera.world = wrapper * camera.world;
                camera.name = camera.name.empty() ? object.name : object.name + ": " + camera.name;
                scene.fileCameras.push_back(std::move(camera));
            }
            scene.nodes.push_back(std::move(node));
            MergedModel offsets;
            if (addGeometry) {
                offsets = {static_cast<int>(scene.meshes.size()), static_cast<int>(scene.materials.size()), static_cast<int>(scene.images.size())};
                mergedModels[object.model] = offsets;
            } else {
                offsets = reused->second;
            }
            mergeModel(scene, model->scene, nodeIndex, nodeOffset, offsets.meshOffset, offsets.materialOffset, offsets.imageOffset, addGeometry);
            addRoot(object, nodeIndex);
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
        addRoot(object, nodeIndex);
    }
    for (const Link& link : links) {
        const auto parent = nodeOfObject.find(link.parent);
        if (parent != nodeOfObject.end()) {
            scene.nodes[static_cast<size_t>(parent->second)].children.push_back(link.child);
        } else {
            scene.roots.push_back(link.child);
            result.warnings.push_back("object '" + link.name + "' lost its parent and was moved to the top level");
        }
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
    const SceneStats stats = sceneStats(scene);
    if (stats.gpuBytes > limits.maxGpuBytes) {
        result.error = "the scene needs about " + std::to_string(stats.gpuBytes / (1024 * 1024)) + " MiB of graphics memory, the limit is " + std::to_string(limits.maxGpuBytes / (1024 * 1024)) + " MiB";
        return result;
    }
    result.ok = true;
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return result;
}

}

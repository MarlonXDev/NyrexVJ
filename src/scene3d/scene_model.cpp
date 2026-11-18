#include "scene3d/scene_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nvj {

namespace {

struct Pose {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

int valueCount(AnimPath path) {
    return path == AnimPath::Rotation ? 4 : 3;
}

void sampleChannel(const SceneChannel& channel, float time, Pose& pose) {
    const int count = valueCount(channel.path);
    const size_t keys = channel.times.size();
    if (keys == 0 || channel.values.size() < keys * static_cast<size_t>(count)) {
        return;
    }
    size_t a = 0;
    size_t b = 0;
    float t = 0.0f;
    if (time <= channel.times.front()) {
        a = b = 0;
    } else if (time >= channel.times.back()) {
        a = b = keys - 1;
    } else {
        const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
        b = static_cast<size_t>(upper - channel.times.begin());
        a = b - 1;
        const float span = channel.times[b] - channel.times[a];
        t = span > 1e-9f ? (time - channel.times[a]) / span : 0.0f;
        if (channel.interp == AnimInterp::Step) {
            t = 0.0f;
        }
    }
    const float* va = &channel.values[a * static_cast<size_t>(count)];
    const float* vb = &channel.values[b * static_cast<size_t>(count)];
    if (channel.path == AnimPath::Rotation) {
        pose.rotation = slerp(Quat{va[0], va[1], va[2], va[3]}, Quat{vb[0], vb[1], vb[2], vb[3]}, t);
        return;
    }
    const Vec3 result{va[0] + (vb[0] - va[0]) * t, va[1] + (vb[1] - va[1]) * t, va[2] + (vb[2] - va[2]) * t};
    if (channel.path == AnimPath::Translation) {
        pose.translation = result;
    } else {
        pose.scale = result;
    }
}

constexpr float kTau = 6.28318531f;

Vec3 axisVector(int axis) {
    return axis == 0 ? Vec3{1.0f, 0.0f, 0.0f} : (axis == 2 ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f});
}

struct EvalContext {
    float time = 0.0f;
    SceneAudio audio;
    std::vector<SceneInstance>* out = nullptr;
};

Mat4 localMatrix(const SceneNode& node, const Pose& pose, const EvalContext& ctx, int copy, float& emissive) {
    Vec3 t = pose.translation;
    Quat r = pose.rotation;
    Vec3 s = pose.scale;
    for (const NodeMotion& m : node.motions) {
        const float cycle = kTau * (m.speed * ctx.time + m.phase * static_cast<float>(copy));
        switch (m.kind) {
        case MotionKind::Spin:
            r = r * axisAngle(axisVector(m.axis), cycle);
            break;
        case MotionKind::Bob:
            t.y += m.amount * std::sin(cycle);
            break;
        case MotionKind::Orbit:
            t.x += m.amount * std::cos(cycle);
            t.z += m.amount * std::sin(cycle);
            break;
        case MotionKind::Pulse:
            s = s * std::max(1.0f + m.amount * std::sin(cycle), 0.05f);
            break;
        case MotionKind::Sway:
            r = r * axisAngle(axisVector(m.axis), m.amount * std::sin(cycle));
            break;
        }
    }
    if (!node.bindings.empty()) {
        s = s * std::max(1.0f + bindingSum(node.bindings, BindTarget::Scale, ctx.audio), 0.05f);
        t.y += bindingSum(node.bindings, BindTarget::Lift, ctx.audio);
        const float twist = bindingSum(node.bindings, BindTarget::Twist, ctx.audio);
        if (twist != 0.0f) {
            r = r * axisAngle({0.0f, 1.0f, 0.0f}, twist);
        }
        emissive += std::max(bindingSum(node.bindings, BindTarget::Emissive, ctx.audio), 0.0f);
    }
    return composeTrs(t, r, s);
}

Mat4 repeatMatrix(const NodeRepeat& repeat, int copy, int count) {
    if (repeat.kind == RepeatKind::Ring) {
        const float angle = kTau * static_cast<float>(copy) / static_cast<float>(count);
        return rotation(axisAngle({0.0f, 1.0f, 0.0f}, -angle)) * translation({repeat.radius, 0.0f, 0.0f});
    }
    return translation({(static_cast<float>(copy) - 0.5f * static_cast<float>(count - 1)) * repeat.spacing, 0.0f, 0.0f});
}

void walk(const Scene& scene, const std::vector<Pose>& poses, int index, const Mat4& parent, int depth, EvalContext& ctx, int copy, float inheritedEmissive, bool framing) {
    if (depth > kSceneMaxDepth || index < 0 || index >= static_cast<int>(scene.nodes.size()) || ctx.out->size() >= kSceneMaxInstances) {
        return;
    }
    const SceneNode& node = scene.nodes[static_cast<size_t>(index)];
    const Pose& pose = poses[static_cast<size_t>(index)];
    const int copies = node.repeat.kind != RepeatKind::None ? std::clamp(node.repeat.count, 1, kSceneMaxRepeat) : 1;
    const bool nodeFraming = framing && node.framing;
    for (int c = 0; c < copies; ++c) {
        const int copyIndex = copies > 1 ? c : copy;
        float emissive = inheritedEmissive;
        const Mat4 local = localMatrix(node, pose, ctx, copyIndex, emissive);
        const Mat4 world = copies > 1 ? parent * repeatMatrix(node.repeat, c, copies) * local : parent * local;
        for (const int mesh : node.meshes) {
            if (ctx.out->size() >= kSceneMaxInstances) {
                return;
            }
            ctx.out->push_back({mesh, world, emissive, nodeFraming});
        }
        for (const int child : node.children) {
            walk(scene, poses, child, world, depth + 1, ctx, copyIndex, emissive, nodeFraming);
        }
    }
}

}

float audioValue(const SceneAudio& audio, BindSource source) {
    switch (source) {
    case BindSource::Bass:
        return audio.bass;
    case BindSource::Mid:
        return audio.mid;
    case BindSource::High:
        return audio.high;
    case BindSource::Level:
        return audio.level;
    case BindSource::Kick:
        return audio.kick;
    }
    return 0.0f;
}

float bindingSum(const std::vector<NodeBinding>& bindings, BindTarget target, const SceneAudio& audio) {
    float sum = 0.0f;
    for (const NodeBinding& binding : bindings) {
        if (binding.target == target) {
            sum += binding.amount * audioValue(audio, binding.source);
        }
    }
    return sum;
}

float sceneDuration(const Scene& scene, int animation) {
    if (animation < 0 || animation >= static_cast<int>(scene.animations.size())) {
        return 0.0f;
    }
    return scene.animations[static_cast<size_t>(animation)].duration;
}

std::vector<SceneInstance> evaluateScene(const Scene& scene, int animation, float time, const SceneAudio* audio) {
    std::vector<Pose> poses(scene.nodes.size());
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        poses[i].translation = scene.nodes[i].translation;
        poses[i].rotation = scene.nodes[i].rotation;
        poses[i].scale = scene.nodes[i].scale;
    }
    if (animation >= 0 && animation < static_cast<int>(scene.animations.size())) {
        const SceneAnimation& clip = scene.animations[static_cast<size_t>(animation)];
        for (const SceneChannel& channel : clip.channels) {
            if (channel.node < 0 || channel.node >= static_cast<int>(poses.size())) {
                continue;
            }
            const float period = channel.loop > 1e-6f ? channel.loop : clip.duration;
            float local = time;
            if (period > 1e-6f) {
                local = std::fmod(time, period);
                if (local < 0.0f) {
                    local += period;
                }
            }
            sampleChannel(channel, local, poses[static_cast<size_t>(channel.node)]);
        }
    }
    std::vector<SceneInstance> out;
    EvalContext ctx;
    ctx.time = time;
    if (audio != nullptr) {
        ctx.audio = *audio;
    }
    ctx.out = &out;
    for (const int root : scene.roots) {
        walk(scene, poses, root, Mat4{}, 0, ctx, 0, 0.0f, true);
    }
    return out;
}

SceneBounds sceneBounds(const Scene& scene, int animation, float time) {
    SceneBounds bounds;
    constexpr float big = std::numeric_limits<float>::max();
    Vec3 lo{big, big, big};
    Vec3 hi{-big, -big, -big};
    const std::vector<SceneInstance> evaluated = evaluateScene(scene, animation, time);
    const bool restrictFraming = std::any_of(evaluated.begin(), evaluated.end(), [](const SceneInstance& i) { return i.framing; });
    for (const SceneInstance& instance : evaluated) {
        if (instance.mesh < 0 || instance.mesh >= static_cast<int>(scene.meshes.size()) || (restrictFraming && !instance.framing)) {
            continue;
        }
        for (const Vec3& p : scene.meshes[static_cast<size_t>(instance.mesh)].positions) {
            const Vec3 w = transformPoint(instance.world, p);
            lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
            bounds.valid = true;
        }
    }
    if (bounds.valid) {
        bounds.min = lo;
        bounds.max = hi;
    }
    return bounds;
}

SceneSphere sceneSphere(const Scene& scene, int animation, int samples) {
    SceneSphere result;
    struct LocalSphere {
        Vec3 lo;
        Vec3 hi;
        Vec3 center;
        float radius = 0.0f;
        bool valid = false;
    };
    std::vector<LocalSphere> locals(scene.meshes.size());
    constexpr float big = std::numeric_limits<float>::max();
    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        const std::vector<Vec3>& positions = scene.meshes[i].positions;
        if (positions.empty()) {
            continue;
        }
        Vec3 lo{big, big, big};
        Vec3 hi{-big, -big, -big};
        for (const Vec3& p : positions) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        LocalSphere& local = locals[i];
        local.lo = lo;
        local.hi = hi;
        local.center = (lo + hi) * 0.5f;
        float farthest = 0.0f;
        for (const Vec3& p : positions) {
            farthest = std::max(farthest, length(p - local.center));
        }
        local.radius = farthest;
        local.valid = true;
    }
    const float duration = sceneDuration(scene, animation);
    const int count = std::max(samples, 1);
    std::vector<std::vector<SceneInstance>> frames;
    bool restrictFraming = false;
    for (int s = 0; s < count; ++s) {
        frames.push_back(evaluateScene(scene, animation, duration * static_cast<float>(s) / static_cast<float>(count)));
        for (const SceneInstance& instance : frames.back()) {
            restrictFraming = restrictFraming || instance.framing;
        }
    }
    const auto usable = [&](const SceneInstance& instance) {
        return instance.mesh >= 0 && instance.mesh < static_cast<int>(locals.size()) && locals[static_cast<size_t>(instance.mesh)].valid && (!restrictFraming || instance.framing);
    };
    Vec3 lo{big, big, big};
    Vec3 hi{-big, -big, -big};
    for (const std::vector<SceneInstance>& frame : frames) {
        for (const SceneInstance& instance : frame) {
            if (!usable(instance)) {
                continue;
            }
            const LocalSphere& local = locals[static_cast<size_t>(instance.mesh)];
            for (int corner = 0; corner < 8; ++corner) {
                const Vec3 p{(corner & 1) ? local.hi.x : local.lo.x, (corner & 2) ? local.hi.y : local.lo.y, (corner & 4) ? local.hi.z : local.lo.z};
                const Vec3 w = transformPoint(instance.world, p);
                lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
                hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
                result.valid = true;
            }
        }
    }
    if (!result.valid) {
        return result;
    }
    result.center = (lo + hi) * 0.5f;
    float radius = 0.0f;
    for (const std::vector<SceneInstance>& frame : frames) {
        for (const SceneInstance& instance : frame) {
            if (!usable(instance)) {
                continue;
            }
            const LocalSphere& local = locals[static_cast<size_t>(instance.mesh)];
            const Vec3 c = transformPoint(instance.world, local.center);
            const Mat4& m = instance.world;
            const float sx = length(Vec3{m.m[0], m.m[1], m.m[2]});
            const float sy = length(Vec3{m.m[4], m.m[5], m.m[6]});
            const float sz = length(Vec3{m.m[8], m.m[9], m.m[10]});
            radius = std::max(radius, length(c - result.center) + local.radius * std::max({sx, sy, sz}));
        }
    }
    result.radius = std::max(radius, 1e-3f);
    return result;
}

SceneStats sceneStats(const Scene& scene) {
    SceneStats stats;
    stats.meshes = scene.meshes.size();
    stats.nodes = scene.nodes.size();
    stats.images = scene.images.size();
    for (const SceneMesh& mesh : scene.meshes) {
        stats.vertices += mesh.positions.size();
        stats.triangles += mesh.indices.size() / 3;
        stats.cpuBytes += mesh.positions.size() * sizeof(Vec3) + mesh.normals.size() * sizeof(Vec3) + (mesh.uvs.size() + mesh.colors.size()) * sizeof(float) + mesh.indices.size() * sizeof(uint32_t);
        stats.gpuBytes += mesh.positions.size() * kSceneVertexFloats * sizeof(float) + mesh.indices.size() * sizeof(uint32_t);
    }
    for (const SceneImage& image : scene.images) {
        stats.cpuBytes += image.rgba.size();
        stats.gpuBytes += image.rgba.size() + image.rgba.size() / 3;
    }
    return stats;
}

bool validateScene(const Scene& scene, std::string& error) {
    const int meshCount = static_cast<int>(scene.meshes.size());
    const int nodeCount = static_cast<int>(scene.nodes.size());
    for (size_t i = 0; i < scene.images.size(); ++i) {
        const SceneImage& image = scene.images[i];
        if (image.width <= 0 || image.height <= 0 || image.rgba.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
            error = "image " + std::to_string(i) + " is inconsistent";
            return false;
        }
    }
    const int imageCount = static_cast<int>(scene.images.size());
    for (size_t i = 0; i < scene.materials.size(); ++i) {
        const SceneMaterial& m = scene.materials[i];
        if (m.baseTexture >= imageCount || m.metallicRoughnessTexture >= imageCount || m.normalTexture >= imageCount || m.emissiveTexture >= imageCount) {
            error = "material " + std::to_string(i) + " references a missing image";
            return false;
        }
        const float values[] = {m.metallic, m.roughness, m.emissiveStrength, m.normalScale, m.alphaCutoff, m.emissive[0], m.emissive[1], m.emissive[2], m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]};
        for (const float v : values) {
            if (!std::isfinite(v)) {
                error = "material " + std::to_string(i) + " has a non-finite value";
                return false;
            }
        }
    }
    if (scene.lights.size() > kSceneMaxLights) {
        error = "the scene has too many lights";
        return false;
    }
    for (const SceneLight& light : scene.lights) {
        if (!std::isfinite(light.position.x) || !std::isfinite(light.position.y) || !std::isfinite(light.position.z) || !std::isfinite(light.intensity)) {
            error = "a light has a non-finite value";
            return false;
        }
    }
    if (!std::isfinite(scene.camera.fovDegrees) || !std::isfinite(scene.camera.distance) || !std::isfinite(scene.camera.pitchDegrees) || scene.camera.fovDegrees < 10.0f || scene.camera.fovDegrees > 120.0f || scene.camera.distance <= 0.0f) {
        error = "the camera has invalid values";
        return false;
    }
    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        const SceneMesh& mesh = scene.meshes[i];
        const size_t count = mesh.positions.size();
        const std::string label = "mesh " + std::to_string(i);
        if (count == 0 || mesh.normals.size() != count) {
            error = label + " has missing positions or normals";
            return false;
        }
        if ((!mesh.uvs.empty() && mesh.uvs.size() != count * 2) || (!mesh.colors.empty() && mesh.colors.size() != count * 4)) {
            error = label + " has inconsistent attributes";
            return false;
        }
        if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
            error = label + " has an invalid index count";
            return false;
        }
        for (const uint32_t index : mesh.indices) {
            if (index >= count) {
                error = label + " has an index out of range";
                return false;
            }
        }
        for (const Vec3& p : mesh.positions) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                error = label + " has a non-finite position";
                return false;
            }
        }
        if (mesh.material >= static_cast<int>(scene.materials.size())) {
            error = label + " references a missing material";
            return false;
        }
    }
    std::vector<char> seen(scene.nodes.size(), 0);
    std::vector<std::pair<int, int>> stack;
    for (const int root : scene.roots) {
        if (root < 0 || root >= nodeCount) {
            error = "scene root out of range";
            return false;
        }
        stack.emplace_back(root, 0);
    }
    while (!stack.empty()) {
        const auto [index, depth] = stack.back();
        stack.pop_back();
        if (depth > kSceneMaxDepth) {
            error = "node hierarchy is too deep";
            return false;
        }
        if (seen[static_cast<size_t>(index)]) {
            error = "node " + std::to_string(index) + " is reachable more than once";
            return false;
        }
        seen[static_cast<size_t>(index)] = 1;
        const SceneNode& node = scene.nodes[static_cast<size_t>(index)];
        if (node.repeat.kind != RepeatKind::None && (node.repeat.count < 1 || node.repeat.count > kSceneMaxRepeat || !std::isfinite(node.repeat.radius) || !std::isfinite(node.repeat.spacing))) {
            error = "node " + std::to_string(index) + " has an invalid repeat";
            return false;
        }
        for (const NodeMotion& motion : node.motions) {
            if (!std::isfinite(motion.speed) || !std::isfinite(motion.amount) || !std::isfinite(motion.phase) || motion.axis < 0 || motion.axis > 2) {
                error = "node " + std::to_string(index) + " has an invalid motion";
                return false;
            }
        }
        for (const int mesh : node.meshes) {
            if (mesh < 0 || mesh >= meshCount) {
                error = "node " + std::to_string(index) + " references a missing mesh";
                return false;
            }
        }
        for (const int child : node.children) {
            if (child < 0 || child >= nodeCount) {
                error = "node " + std::to_string(index) + " has an invalid child";
                return false;
            }
            stack.emplace_back(child, depth + 1);
        }
    }
    for (size_t i = 0; i < scene.animations.size(); ++i) {
        for (const SceneChannel& channel : scene.animations[i].channels) {
            if (channel.node < 0 || channel.node >= nodeCount) {
                error = "animation " + std::to_string(i) + " targets a missing node";
                return false;
            }
            const size_t per = static_cast<size_t>(valueCount(channel.path));
            if (channel.times.empty() || channel.values.size() != channel.times.size() * per) {
                error = "animation " + std::to_string(i) + " has inconsistent keys";
                return false;
            }
            for (size_t k = 0; k < channel.times.size(); ++k) {
                if (!std::isfinite(channel.times[k]) || (k > 0 && channel.times[k] < channel.times[k - 1])) {
                    error = "animation " + std::to_string(i) + " has invalid key times";
                    return false;
                }
            }
            for (const float v : channel.values) {
                if (!std::isfinite(v)) {
                    error = "animation " + std::to_string(i) + " has non-finite values";
                    return false;
                }
            }
        }
    }
    return true;
}

}

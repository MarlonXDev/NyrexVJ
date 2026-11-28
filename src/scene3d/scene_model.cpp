#include "scene3d/scene_model.hpp"

#include "core/show_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nvj {

namespace {

struct Pose {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    std::vector<float> weights;
};

int valueCount(AnimPath path) {
    return path == AnimPath::Rotation ? 4 : (path == AnimPath::Weights ? 0 : 3);
}

void sampleChannel(const SceneChannel& channel, float time, Pose& pose) {
    const size_t keys = channel.times.size();
    if (keys == 0) {
        return;
    }
    const int count = channel.path == AnimPath::Weights ? static_cast<int>(channel.values.size() / keys) : valueCount(channel.path);
    if (count <= 0 || channel.values.size() < keys * static_cast<size_t>(count)) {
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
    if (channel.path == AnimPath::Weights) {
        pose.weights.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            pose.weights[static_cast<size_t>(i)] = va[i] + (vb[i] - va[i]) * t;
        }
        return;
    }
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

struct NodeMods {
    float emissive = 0.0f;
    float hue = 0.0f;
    float roughness = 0.0f;
    float uvX = 0.0f;
    float uvY = 0.0f;
    float inflate = 0.0f;
    float ripple = 0.0f;
};

struct EvalContext {
    float time = 0.0f;
    SceneAudio audio;
    SceneEval* out = nullptr;
    const Scene* scene = nullptr;
    const std::vector<Pose>* poses = nullptr;
    std::vector<int> parent;
    std::vector<int> stackCopy;
};

Mat4 localMatrix(const SceneNode& node, const Pose& pose, const EvalContext& ctx, int copy, NodeMods& mods) {
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
        mods.emissive += std::max(bindingSum(node.bindings, BindTarget::Emissive, ctx.audio), 0.0f);
        if (ctx.audio.materials) {
            for (const NodeBinding& binding : node.bindings) {
                if (!bindTargetIsMaterial(binding.target)) {
                    continue;
                }
                const float value = binding.amount * audioValue(ctx.audio, binding.source);
                switch (binding.target) {
                case BindTarget::Hue:
                    mods.hue += value;
                    break;
                case BindTarget::Roughness:
                    mods.roughness += value;
                    break;
                case BindTarget::UvX:
                    mods.uvX += value;
                    break;
                case BindTarget::UvY:
                    mods.uvY += value;
                    break;
                case BindTarget::Inflate:
                    mods.inflate += value;
                    break;
                case BindTarget::Ripple:
                    mods.ripple += value;
                    break;
                default:
                    break;
                }
            }
        }
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

Mat4 nodeWorld(const EvalContext& ctx, int index) {
    const Scene& scene = *ctx.scene;
    int chain[kSceneMaxDepth + 2];
    int length = 0;
    for (int n = index; n >= 0 && length < kSceneMaxDepth + 2; n = ctx.parent[static_cast<size_t>(n)]) {
        chain[length++] = n;
    }
    Mat4 world;
    int copy = 0;
    for (int i = length - 1; i >= 0; --i) {
        const int n = chain[i];
        const SceneNode& node = scene.nodes[static_cast<size_t>(n)];
        const int copies = node.repeat.kind != RepeatKind::None ? std::clamp(node.repeat.count, 1, kSceneMaxRepeat) : 1;
        const int c = copies > 1 ? std::max(ctx.stackCopy[static_cast<size_t>(n)], 0) : 0;
        const int copyIndex = copies > 1 ? c : copy;
        NodeMods mods;
        const Mat4 local = localMatrix(node, (*ctx.poses)[static_cast<size_t>(n)], ctx, copyIndex, mods);
        world = copies > 1 ? world * repeatMatrix(node.repeat, c, copies) * local : world * local;
        copy = copyIndex;
    }
    return world;
}

float clampOrZero(float value, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : 0.0f;
}

void emitMesh(const Scene& scene, const std::vector<Pose>& poses, int nodeIndex, int mesh, const Mat4& world, const NodeMods& mods, bool framing, EvalContext& ctx, int& jointBase, int& weightBase) {
    SceneEval& out = *ctx.out;
    const SceneNode& node = scene.nodes[static_cast<size_t>(nodeIndex)];
    SceneInstance instance;
    instance.mesh = mesh;
    instance.world = world;
    instance.emissive = mods.emissive;
    instance.hue = clampOrZero(mods.hue, -kSceneMaxHueTurns, kSceneMaxHueTurns);
    instance.roughness = clampOrZero(mods.roughness, -1.0f, 1.0f);
    instance.uvX = clampOrZero(mods.uvX, -kSceneMaxUvShift, kSceneMaxUvShift);
    instance.uvY = clampOrZero(mods.uvY, -kSceneMaxUvShift, kSceneMaxUvShift);
    instance.inflate = clampOrZero(mods.inflate, -kSceneMaxDeform, kSceneMaxDeform);
    instance.ripple = clampOrZero(mods.ripple, -kSceneMaxDeform, kSceneMaxDeform);
    instance.framing = framing;
    const SceneMesh& source = scene.meshes[static_cast<size_t>(mesh)];
    if (node.skin >= 0 && node.skin < static_cast<int>(scene.skins.size()) && source.skinned()) {
        const SceneSkin& skin = scene.skins[static_cast<size_t>(node.skin)];
        if (jointBase < 0) {
            if (out.joints.size() + skin.joints.size() > kSceneMaxJointsPerFrame) {
                ++out.skippedSkinned;
                return;
            }
            jointBase = static_cast<int>(out.joints.size());
            for (size_t k = 0; k < skin.joints.size(); ++k) {
                const int joint = skin.joints[k];
                const Mat4 bind = k < skin.inverseBind.size() ? skin.inverseBind[k] : Mat4{};
                out.joints.push_back(joint >= 0 && joint < static_cast<int>(scene.nodes.size()) ? nodeWorld(ctx, joint) * bind : Mat4{});
            }
        }
        instance.skin = node.skin;
        instance.jointOffset = jointBase;
        instance.jointCount = static_cast<int>(skin.joints.size());
        instance.world = Mat4{};
    }
    if (source.morphTargetCount() > 0) {
        if (weightBase < 0) {
            weightBase = static_cast<int>(out.weights.size());
            const std::vector<float>& weights = poses[static_cast<size_t>(nodeIndex)].weights;
            for (size_t t = 0; t < kSceneMaxMorphTargets; ++t) {
                out.weights.push_back(t < weights.size() ? weights[t] : 0.0f);
            }
        }
        instance.weightOffset = weightBase;
        instance.weightCount = static_cast<int>(std::min(source.morphTargetCount(), kSceneMaxMorphTargets));
    }
    out.instances.push_back(instance);
}

void walk(const Scene& scene, const std::vector<Pose>& poses, int index, const Mat4& parent, int depth, EvalContext& ctx, int copy, const NodeMods& inherited, bool framing) {
    if (depth > kSceneMaxDepth || index < 0 || index >= static_cast<int>(scene.nodes.size()) || ctx.out->instances.size() >= kSceneMaxInstances) {
        return;
    }
    const SceneNode& node = scene.nodes[static_cast<size_t>(index)];
    const Pose& pose = poses[static_cast<size_t>(index)];
    const int copies = node.repeat.kind != RepeatKind::None ? std::clamp(node.repeat.count, 1, kSceneMaxRepeat) : 1;
    const bool nodeFraming = framing && node.framing;
    for (int c = 0; c < copies; ++c) {
        const int copyIndex = copies > 1 ? c : copy;
        NodeMods mods = inherited;
        const Mat4 local = localMatrix(node, pose, ctx, copyIndex, mods);
        const Mat4 world = copies > 1 ? parent * repeatMatrix(node.repeat, c, copies) * local : parent * local;
        ctx.stackCopy[static_cast<size_t>(index)] = copies > 1 ? c : 0;
        int jointBase = -1;
        int weightBase = -1;
        for (const int mesh : node.meshes) {
            if (ctx.out->instances.size() >= kSceneMaxInstances) {
                return;
            }
            emitMesh(scene, poses, index, mesh, world, mods, nodeFraming, ctx, jointBase, weightBase);
        }
        for (const int child : node.children) {
            walk(scene, poses, child, world, depth + 1, ctx, copyIndex, mods, nodeFraming);
        }
    }
    ctx.stackCopy[static_cast<size_t>(index)] = 0;
}

}

float rawAudioValue(const SceneAudio& audio, BindSource source) {
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
    case BindSource::Beat:
        return audio.beat;
    case BindSource::Band1:
    case BindSource::Band2:
    case BindSource::Band3:
    case BindSource::Band4:
    case BindSource::Band5:
    case BindSource::Band6:
    case BindSource::Band7:
    case BindSource::Band8:
        return audio.bands[static_cast<size_t>(static_cast<int>(source) - static_cast<int>(BindSource::Band1))];
    }
    return 0.0f;
}

float audioValue(const SceneAudio& audio, BindSource source) {
    const float value = rawAudioValue(audio, source);
    return std::isfinite(value) ? value : 0.0f;
}

float beatPulse(double beat) {
    if (!std::isfinite(beat)) {
        return 0.0f;
    }
    const double phase = beat - std::floor(beat);
    return static_cast<float>(std::exp(-phase * 5.0));
}

std::array<float, kSceneBands> spectrumBands(const float* spectrum, int bins) {
    std::array<float, kSceneBands> out{};
    if (spectrum == nullptr || bins < kSceneBands) {
        return out;
    }
    for (int band = 0; band < kSceneBands; ++band) {
        const int from = band * bins / kSceneBands;
        const int to = (band + 1) * bins / kSceneBands;
        float sum = 0.0f;
        for (int i = from; i < to; ++i) {
            const float v = spectrum[i];
            sum += std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
        }
        out[static_cast<size_t>(band)] = to > from ? sum / static_cast<float>(to - from) : 0.0f;
    }
    return out;
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

SceneEval evaluateSceneFull(const Scene& scene, int animation, float time, const SceneAudio* audio) {
    std::vector<Pose> poses(scene.nodes.size());
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        poses[i].translation = scene.nodes[i].translation;
        poses[i].rotation = scene.nodes[i].rotation;
        poses[i].scale = scene.nodes[i].scale;
        poses[i].weights = scene.nodes[i].weights;
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
    SceneEval out;
    EvalContext ctx;
    ctx.time = time;
    if (audio != nullptr) {
        ctx.audio = *audio;
    }
    ctx.out = &out;
    ctx.scene = &scene;
    ctx.poses = &poses;
    ctx.parent.assign(scene.nodes.size(), -1);
    ctx.stackCopy.assign(scene.nodes.size(), 0);
    for (size_t n = 0; n < scene.nodes.size(); ++n) {
        for (const int child : scene.nodes[n].children) {
            if (child >= 0 && child < static_cast<int>(scene.nodes.size()) && ctx.parent[static_cast<size_t>(child)] < 0) {
                ctx.parent[static_cast<size_t>(child)] = static_cast<int>(n);
            }
        }
    }
    for (const int root : scene.roots) {
        walk(scene, poses, root, Mat4{}, 0, ctx, 0, NodeMods{}, true);
    }
    return out;
}

std::vector<SceneInstance> evaluateScene(const Scene& scene, int animation, float time, const SceneAudio* audio) {
    return evaluateSceneFull(scene, animation, time, audio).instances;
}

std::vector<Vec3> posedPositions(const Scene& scene, const SceneMesh& mesh, const SceneEval& eval, const SceneInstance& instance) {
    (void)scene;
    std::vector<Vec3> out = mesh.positions;
    if (instance.weightCount > 0 && !mesh.targets.empty()) {
        const size_t targets = std::min<size_t>({mesh.targets.size(), static_cast<size_t>(instance.weightCount), kSceneMaxMorphTargets});
        for (size_t t = 0; t < targets; ++t) {
            const size_t at = static_cast<size_t>(instance.weightOffset) + t;
            const float weight = at < eval.weights.size() ? eval.weights[at] : 0.0f;
            const SceneMorphTarget& target = mesh.targets[t];
            if (weight == 0.0f || target.positions.size() != out.size()) {
                continue;
            }
            for (size_t i = 0; i < out.size(); ++i) {
                out[i] = out[i] + target.positions[i] * weight;
            }
        }
    }
    if (instance.skin >= 0 && instance.jointCount > 0 && mesh.joints.size() == out.size() * 4 && mesh.weights.size() == out.size() * 4) {
        const std::vector<Vec3> source = out;
        for (size_t i = 0; i < out.size(); ++i) {
            Vec3 sum{0.0f, 0.0f, 0.0f};
            for (size_t k = 0; k < 4; ++k) {
                const float w = mesh.weights[i * 4 + k];
                const size_t joint = mesh.joints[i * 4 + k];
                const size_t at = static_cast<size_t>(instance.jointOffset) + joint;
                if (w == 0.0f || joint >= static_cast<size_t>(instance.jointCount) || at >= eval.joints.size()) {
                    continue;
                }
                sum = sum + transformPoint(eval.joints[at], source[i]) * w;
            }
            out[i] = sum;
        }
    }
    return out;
}

namespace {

bool needsPose(const SceneInstance& instance) {
    return instance.skin >= 0 || instance.weightCount > 0;
}

struct LocalSphere {
    Vec3 lo;
    Vec3 hi;
    Vec3 center;
    float radius = 0.0f;
    bool valid = false;
};

LocalSphere makeLocal(const std::vector<Vec3>& positions, size_t stride) {
    LocalSphere local;
    if (positions.empty()) {
        return local;
    }
    constexpr float big = std::numeric_limits<float>::max();
    Vec3 lo{big, big, big};
    Vec3 hi{-big, -big, -big};
    for (size_t i = 0; i < positions.size(); i += stride) {
        const Vec3& p = positions[i];
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    local.lo = lo;
    local.hi = hi;
    local.center = (lo + hi) * 0.5f;
    float farthest = 0.0f;
    for (size_t i = 0; i < positions.size(); i += stride) {
        farthest = std::max(farthest, length(positions[i] - local.center));
    }
    local.radius = farthest;
    local.valid = true;
    return local;
}

size_t poseStride(size_t count) {
    constexpr size_t kLimit = 40000;
    return count > kLimit ? (count + kLimit - 1) / kLimit : 1;
}

}

SceneBounds sceneBounds(const Scene& scene, int animation, float time) {
    SceneBounds bounds;
    constexpr float big = std::numeric_limits<float>::max();
    Vec3 lo{big, big, big};
    Vec3 hi{-big, -big, -big};
    const SceneEval eval = evaluateSceneFull(scene, animation, time);
    const bool restrictFraming = std::any_of(eval.instances.begin(), eval.instances.end(), [](const SceneInstance& i) { return i.framing; });
    for (const SceneInstance& instance : eval.instances) {
        if (instance.mesh < 0 || instance.mesh >= static_cast<int>(scene.meshes.size()) || (restrictFraming && !instance.framing)) {
            continue;
        }
        const SceneMesh& mesh = scene.meshes[static_cast<size_t>(instance.mesh)];
        std::vector<Vec3> posed;
        const std::vector<Vec3>* positions = &mesh.positions;
        if (needsPose(instance)) {
            posed = posedPositions(scene, mesh, eval, instance);
            positions = &posed;
        }
        for (const Vec3& p : *positions) {
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
    constexpr float big = std::numeric_limits<float>::max();
    std::vector<LocalSphere> locals(scene.meshes.size());
    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        locals[i] = makeLocal(scene.meshes[i].positions, 1);
    }
    const float duration = sceneDuration(scene, animation);
    const int count = std::max(samples, 1);
    std::vector<SceneEval> frames;
    std::vector<std::vector<LocalSphere>> posedLocals;
    bool restrictFraming = false;
    for (int s = 0; s < count; ++s) {
        frames.push_back(evaluateSceneFull(scene, animation, duration * static_cast<float>(s) / static_cast<float>(count)));
        const SceneEval& eval = frames.back();
        posedLocals.emplace_back(eval.instances.size());
        for (size_t i = 0; i < eval.instances.size(); ++i) {
            const SceneInstance& instance = eval.instances[i];
            restrictFraming = restrictFraming || instance.framing;
            if (needsPose(instance) && instance.mesh >= 0 && instance.mesh < static_cast<int>(scene.meshes.size())) {
                const SceneMesh& mesh = scene.meshes[static_cast<size_t>(instance.mesh)];
                posedLocals.back()[i] = makeLocal(posedPositions(scene, mesh, eval, instance), poseStride(mesh.positions.size()));
            }
        }
    }
    const auto localOf = [&](size_t frame, size_t index) -> const LocalSphere& {
        const SceneInstance& instance = frames[frame].instances[index];
        if (needsPose(instance) && posedLocals[frame][index].valid) {
            return posedLocals[frame][index];
        }
        return locals[static_cast<size_t>(instance.mesh)];
    };
    const auto usable = [&](const SceneInstance& instance) {
        return instance.mesh >= 0 && instance.mesh < static_cast<int>(locals.size()) && locals[static_cast<size_t>(instance.mesh)].valid && (!restrictFraming || instance.framing);
    };
    Vec3 lo{big, big, big};
    Vec3 hi{-big, -big, -big};
    for (size_t f = 0; f < frames.size(); ++f) {
        for (size_t i = 0; i < frames[f].instances.size(); ++i) {
            const SceneInstance& instance = frames[f].instances[i];
            if (!usable(instance)) {
                continue;
            }
            const LocalSphere& local = localOf(f, i);
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
    for (size_t f = 0; f < frames.size(); ++f) {
        for (size_t i = 0; i < frames[f].instances.size(); ++i) {
            const SceneInstance& instance = frames[f].instances[i];
            if (!usable(instance)) {
                continue;
            }
            const LocalSphere& local = localOf(f, i);
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
    stats.skins = scene.skins.size();
    for (const SceneSkin& skin : scene.skins) {
        stats.joints += skin.joints.size();
    }
    for (const SceneMesh& mesh : scene.meshes) {
        stats.vertices += mesh.positions.size();
        stats.triangles += mesh.indices.size() / 3;
        stats.cpuBytes += mesh.positions.size() * sizeof(Vec3) + mesh.normals.size() * sizeof(Vec3) + (mesh.uvs.size() + mesh.colors.size()) * sizeof(float) + mesh.indices.size() * sizeof(uint32_t);
        stats.gpuBytes += mesh.positions.size() * kSceneVertexFloats * sizeof(float) + mesh.indices.size() * sizeof(uint32_t);
        if (!mesh.joints.empty()) {
            stats.cpuBytes += mesh.joints.size() * sizeof(uint16_t) + mesh.weights.size() * sizeof(float);
            stats.gpuBytes += mesh.positions.size() * (4 * sizeof(uint16_t) + 4 * sizeof(float));
        }
        stats.morphTargets += mesh.targets.size();
        for (const SceneMorphTarget& target : mesh.targets) {
            const size_t bytes = (target.positions.size() + target.normals.size()) * sizeof(Vec3);
            stats.cpuBytes += bytes;
            stats.gpuBytes += mesh.positions.size() * 2 * 3 * sizeof(float);
        }
    }
    for (const SceneMaterial& material : scene.materials) {
        stats.blendMaterials += material.alphaMode == AlphaMode::Blend ? 1 : 0;
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
    const auto cameraValid = [](const SceneCamera& camera) {
        return std::isfinite(camera.fovDegrees) && std::isfinite(camera.distance) && std::isfinite(camera.pitchDegrees) && camera.fovDegrees >= 10.0f && camera.fovDegrees <= 120.0f && camera.distance > 0.0f;
    };
    const auto cameraPoseValid = [](const SceneCamera& camera) {
        return std::isfinite(camera.move.x) && std::isfinite(camera.move.y) && std::isfinite(camera.move.z) && std::isfinite(camera.rollDegrees) && std::isfinite(camera.beats) && camera.beats >= 0.0f;
    };
    bool camerasValid = cameraValid(scene.camera) && cameraPoseValid(scene.camera) && std::isfinite(scene.cameraCutBeats) && std::isfinite(scene.cameraGlide);
    for (const SceneCamera& camera : scene.cameras) {
        camerasValid = camerasValid && cameraValid(camera) && cameraPoseValid(camera);
    }
    if (!camerasValid) {
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
        if (mesh.joints.empty() != mesh.weights.empty() || (!mesh.joints.empty() && (mesh.joints.size() != count * 4 || mesh.weights.size() != count * 4))) {
            error = label + " has inconsistent skin attributes";
            return false;
        }
        for (const float w : mesh.weights) {
            if (!std::isfinite(w) || w < 0.0f) {
                error = label + " has an invalid skin weight";
                return false;
            }
        }
        if (mesh.targets.size() > kSceneMaxMorphTargets) {
            error = label + " has too many morph targets";
            return false;
        }
        for (const SceneMorphTarget& target : mesh.targets) {
            if ((!target.positions.empty() && target.positions.size() != count) || (!target.normals.empty() && target.normals.size() != count)) {
                error = label + " has an inconsistent morph target";
                return false;
            }
            for (const std::vector<Vec3>* list : {&target.positions, &target.normals}) {
                for (const Vec3& d : *list) {
                    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) {
                        error = label + " has a non-finite morph delta";
                        return false;
                    }
                }
            }
        }
    }
    if (scene.skins.size() > kSceneMaxSkins) {
        error = "the scene has too many skins";
        return false;
    }
    for (size_t i = 0; i < scene.skins.size(); ++i) {
        const SceneSkin& skin = scene.skins[i];
        if (skin.joints.empty() || skin.joints.size() > kSceneMaxBones) {
            error = "skin " + std::to_string(i) + " has " + std::to_string(skin.joints.size()) + " joints, the limit is " + std::to_string(kSceneMaxBones);
            return false;
        }
        if (skin.inverseBind.size() != skin.joints.size()) {
            error = "skin " + std::to_string(i) + " has inconsistent inverse bind matrices";
            return false;
        }
        for (const int joint : skin.joints) {
            if (joint < 0 || joint >= nodeCount) {
                error = "skin " + std::to_string(i) + " references a missing joint";
                return false;
            }
        }
        for (const Mat4& m : skin.inverseBind) {
            for (const float v : m.m) {
                if (!std::isfinite(v)) {
                    error = "skin " + std::to_string(i) + " has a non-finite matrix";
                    return false;
                }
            }
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
        if (node.skin >= static_cast<int>(scene.skins.size())) {
            error = "node " + std::to_string(index) + " references a missing skin";
            return false;
        }
        if (node.skin >= 0) {
            const size_t joints = scene.skins[static_cast<size_t>(node.skin)].joints.size();
            for (const int mesh : node.meshes) {
                const SceneMesh& skinned = scene.meshes[static_cast<size_t>(mesh)];
                for (const uint16_t joint : skinned.joints) {
                    if (joint >= joints) {
                        error = "mesh " + std::to_string(mesh) + " uses a joint outside its skin";
                        return false;
                    }
                }
            }
        }
        if (node.weights.size() > kSceneMaxMorphTargets) {
            error = "node " + std::to_string(index) + " has too many morph weights";
            return false;
        }
        for (const float w : node.weights) {
            if (!std::isfinite(w)) {
                error = "node " + std::to_string(index) + " has a non-finite morph weight";
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
            const size_t per = channel.path == AnimPath::Weights ? (channel.times.empty() ? 0 : channel.values.size() / channel.times.size()) : static_cast<size_t>(valueCount(channel.path));
            if (channel.times.empty() || per == 0 || channel.values.size() != channel.times.size() * per || (channel.path == AnimPath::Weights && per > kSceneMaxMorphTargets)) {
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

const SceneCamera& activeSceneCamera(const Scene& scene, double beat) {
    if (scene.cameraCutBeats > 0.0f && scene.cameras.size() > 1 && std::isfinite(beat)) {
        const double step = std::floor(std::max(beat, 0.0) / static_cast<double>(scene.cameraCutBeats));
        const size_t index = static_cast<size_t>(std::fmod(step, static_cast<double>(scene.cameras.size())));
        return scene.cameras[std::min(index, scene.cameras.size() - 1)];
    }
    return scene.camera;
}

namespace {

float wrapAngle(float angle) {
    constexpr float turn = 6.2831853f;
    return angle - turn * std::round(angle / turn);
}

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

KeyShape glideShape(CameraGlide shape) {
    switch (shape) {
    case CameraGlide::Cut:
        return KeyShape::Hold;
    case CameraGlide::Linear:
        return KeyShape::Linear;
    case CameraGlide::Smooth:
        break;
    }
    return KeyShape::Smooth;
}

float shotLength(const Scene& scene, const SceneCamera& camera) {
    return camera.beats > 0.0f ? camera.beats : scene.cameraCutBeats;
}

}

CameraShot cameraShotAt(const Scene& scene, double beat, bool glide) {
    CameraShot shot;
    const size_t count = scene.cameras.size();
    const double cut = scene.cameraCutBeats;
    if (!(cut > 0.0) || count < 2 || !std::isfinite(beat)) {
        shot.from = &scene.camera;
        shot.to = &scene.camera;
        return shot;
    }
    const double at = std::max(beat, 0.0);
    bool timed = false;
    if (glide) {
        for (const SceneCamera& camera : scene.cameras) {
            timed = timed || camera.beats > 0.0f;
        }
    }
    size_t index = 0;
    double fraction = 0.0;
    if (!timed) {
        const double step = std::floor(at / cut);
        index = std::min(static_cast<size_t>(std::fmod(step, static_cast<double>(count))), count - 1);
        fraction = at / cut - step;
    } else {
        double total = 0.0;
        for (const SceneCamera& camera : scene.cameras) {
            total += static_cast<double>(shotLength(scene, camera));
        }
        double position = total > 0.0 ? std::fmod(at, total) : 0.0;
        for (size_t i = 0; i < count; ++i) {
            const double length = static_cast<double>(shotLength(scene, scene.cameras[i]));
            if (position < length || i + 1 == count) {
                index = i;
                fraction = length > 0.0 ? std::clamp(position / length, 0.0, 1.0) : 0.0;
                break;
            }
            position -= length;
        }
    }
    shot.from = &scene.cameras[index];
    shot.to = shot.from;
    if (glide && scene.cameraGlide > 0.0f && scene.cameraGlideShape != CameraGlide::Cut) {
        const double window = std::clamp(static_cast<double>(scene.cameraGlide), 0.0, 1.0);
        const double start = 1.0 - window;
        if (fraction > start) {
            shot.to = &scene.cameras[(index + 1) % count];
            shot.blend = static_cast<float>(easeAlong(glideShape(scene.cameraGlideShape), (fraction - start) / window));
        }
    }
    return shot;
}

CameraPose cameraPoseOf(const SceneCamera& camera, float time, const SceneAudio& audio, bool moves) {
    CameraPose pose;
    if (!camera.authored) {
        pose.fov = kSceneDefaultFov;
        pose.distance = 1.0f - 0.04f * audio.kick;
        pose.yaw = time * 0.4f;
        pose.pitch = 0.32f + 0.12f * std::sin(time * 0.31f);
        return pose;
    }
    pose.fov = std::clamp(camera.fovDegrees, 15.0f, 110.0f) * kSceneRadians;
    pose.distance = camera.distance * std::clamp(1.0f - bindingSum(camera.bindings, BindTarget::Zoom, audio), 0.3f, 3.0f);
    pose.yaw = camera.yawDegrees * kSceneRadians + time * camera.orbitSpeed;
    pose.pitch = (camera.pitchDegrees + camera.pitchSway * std::sin(time * 0.31f)) * kSceneRadians;
    if (!moves) {
        return pose;
    }
    pose.roll = camera.rollDegrees * kSceneRadians;
    pose.move = camera.move;
    if (!camera.bindings.empty()) {
        pose.yaw += bindingSum(camera.bindings, BindTarget::Orbit, audio) * kSceneOrbitPerAmount;
        const float tilt = bindingSum(camera.bindings, BindTarget::Tilt, audio) * kSceneTiltPerAmount;
        if (tilt != 0.0f) {
            pose.pitch = std::clamp(pose.pitch + tilt, -kSceneMaxPitch, kSceneMaxPitch);
        }
        pose.roll += bindingSum(camera.bindings, BindTarget::Roll, audio) * kSceneTiltPerAmount;
        const float fov = bindingSum(camera.bindings, BindTarget::Fov, audio) * kSceneFovPerAmount;
        if (fov != 0.0f) {
            pose.fov = std::clamp(pose.fov + fov, 15.0f * kSceneRadians, 110.0f * kSceneRadians);
        }
        pose.move.x += bindingSum(camera.bindings, BindTarget::MoveX, audio) * kSceneMovePerAmount;
        pose.move.y += bindingSum(camera.bindings, BindTarget::MoveY, audio) * kSceneMovePerAmount;
        pose.move.z += bindingSum(camera.bindings, BindTarget::MoveZ, audio) * kSceneMovePerAmount;
    }
    pose.move.x = std::clamp(finiteOr(pose.move.x, 0.0f), -kSceneMaxMove, kSceneMaxMove);
    pose.move.y = std::clamp(finiteOr(pose.move.y, 0.0f), -kSceneMaxMove, kSceneMaxMove);
    pose.move.z = std::clamp(finiteOr(pose.move.z, 0.0f), -kSceneMaxMove, kSceneMaxMove);
    pose.roll = finiteOr(pose.roll, 0.0f);
    return pose;
}

CameraPose blendCameraPose(const CameraPose& a, const CameraPose& b, float blend) {
    const float u = std::clamp(finiteOr(blend, 0.0f), 0.0f, 1.0f);
    CameraPose out;
    out.yaw = a.yaw + wrapAngle(b.yaw - a.yaw) * u;
    out.pitch = a.pitch + (b.pitch - a.pitch) * u;
    out.roll = a.roll + wrapAngle(b.roll - a.roll) * u;
    out.fov = a.fov + (b.fov - a.fov) * u;
    out.distance = a.distance > 0.0f && b.distance > 0.0f ? a.distance * std::pow(b.distance / a.distance, u) : a.distance + (b.distance - a.distance) * u;
    out.move = a.move + (b.move - a.move) * u;
    return out;
}

CameraPose cameraPoseAt(const Scene& scene, double beat, float time, const SceneAudio& audio, bool moves) {
    const CameraShot shot = cameraShotAt(scene, beat, moves);
    CameraPose pose = cameraPoseOf(*shot.from, time, audio, moves);
    if (shot.blend > 0.0f && shot.to != shot.from) {
        pose = blendCameraPose(pose, cameraPoseOf(*shot.to, time, audio, moves), shot.blend);
    }
    return pose;
}

CameraView cameraView(const CameraPose& pose, Vec3 center, float radius, float zoom) {
    CameraView view;
    view.fov = pose.fov;
    float distance = radius / std::sin(pose.fov * 0.5f) * 1.04f / zoom;
    distance *= pose.distance;
    const Vec3 offset{std::cos(pose.pitch) * std::sin(pose.yaw), std::sin(pose.pitch), std::cos(pose.pitch) * std::cos(pose.yaw)};
    view.target = center + pose.move * radius;
    view.eye = view.target + offset * distance;
    view.distance = distance;
    if (pose.roll != 0.0f) {
        const Vec3 forward = normalize(view.target - view.eye);
        const Vec3 right = normalize(cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
        const Vec3 level = cross(right, forward);
        view.up = level * std::cos(pose.roll) + right * std::sin(pose.roll);
    }
    return view;
}

SceneCamera sceneCameraFromFile(const SceneFileCamera& file, Vec3 center, float radius) {
    SceneCamera camera;
    camera.authored = true;
    camera.orbitSpeed = 0.0f;
    camera.pitchSway = 0.0f;
    const float size = std::max(radius, 1e-4f);
    const float fov = std::clamp(finiteOr(file.yfov, kSceneDefaultFov), 15.0f * kSceneRadians, 110.0f * kSceneRadians);
    camera.fovDegrees = fov / kSceneRadians;
    const auto axis = [&](int column) {
        return normalize(Vec3{file.world.m[static_cast<size_t>(column * 4)], file.world.m[static_cast<size_t>(column * 4 + 1)], file.world.m[static_cast<size_t>(column * 4 + 2)]});
    };
    const Vec3 forward = axis(2) * -1.0f;
    const Vec3 upAxis = axis(1);
    const Vec3 eye{file.world.m[12], file.world.m[13], file.world.m[14]};
    float depth = dot(center - eye, forward);
    if (!(depth >= 0.05f * size)) {
        depth = std::max(length(center - eye), 0.5f * size);
    }
    const Vec3 target = eye + forward * depth;
    const float fit = size / std::sin(fov * 0.5f) * 1.04f;
    camera.distance = std::clamp(depth / fit, 0.2f, 6.0f);
    const Vec3 back = normalize(eye - target);
    camera.yawDegrees = std::atan2(back.x, back.z) / kSceneRadians;
    camera.pitchDegrees = std::clamp(std::asin(std::clamp(back.y, -1.0f, 1.0f)) / kSceneRadians, -85.0f, 85.0f);
    camera.move = (target - center) * (1.0f / size);
    camera.move.x = std::clamp(camera.move.x, -kSceneMaxMove, kSceneMaxMove);
    camera.move.y = std::clamp(camera.move.y, -kSceneMaxMove, kSceneMaxMove);
    camera.move.z = std::clamp(camera.move.z, -kSceneMaxMove, kSceneMaxMove);
    const Vec3 right = cross(forward, Vec3{0.0f, 1.0f, 0.0f});
    if (length(right) > 1e-4f) {
        const Vec3 side = normalize(right);
        const Vec3 level = cross(side, forward);
        camera.rollDegrees = std::atan2(dot(upAxis, side), dot(upAxis, level)) / kSceneRadians;
    }
    return camera;
}

}

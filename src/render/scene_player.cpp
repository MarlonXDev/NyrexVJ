#include "render/scene_player.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace nvj {

namespace {

const char* kSceneVertex = R"glsl(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec3 vWorld;
out vec2 vUv;
out vec4 vColor;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vNormal = uNormalMat * aNormal;
    vUv = aUv;
    vColor = aColor;
    gl_Position = uViewProj * world;
}
)glsl";

const char* kSceneFragment = R"glsl(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;
uniform vec4 uBase;
uniform int uHasTex;
uniform sampler2D uTex;
uniform vec3 uCamera;
uniform float uPalette;
uniform vec3 uColorA;
uniform vec3 uColorB;
uniform float uRainbow;
uniform float uTime;
uniform float uKick;
void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    vec4 albedo = uBase * vColor;
    if (uHasTex != 0) {
        albedo *= texture(uTex, vUv);
    }
    vec3 base = albedo.rgb;
    if (uPalette > 0.5) {
        float l = clamp(base.r, 0.0, 1.0);
        vec3 tint = mix(uColorA, uColorB, l);
        if (uRainbow > 0.5) {
            tint = 0.5 + 0.5 * cos(6.28318 * (l * 0.6 + uTime * 0.05 + vec3(0.0, 0.33, 0.67)));
        }
        base = tint * (0.55 + 0.7 * l);
    }
    vec3 keyDir = normalize(vec3(0.5, 0.8, 0.6));
    vec3 fillDir = normalize(vec3(-0.6, -0.2, -0.5));
    float diffuse = max(dot(n, keyDir), 0.0) * 0.85 + max(dot(n, fillDir), 0.0) * 0.25 + 0.18;
    vec3 view = normalize(uCamera - vWorld);
    float rim = pow(1.0 - max(dot(n, view), 0.0), 3.0);
    vec3 rimColor = uPalette > 0.5 ? mix(uColorA, uColorB, 0.5) : vec3(0.6);
    vec3 col = base * diffuse + rimColor * rim * (0.3 + uKick * 0.6);
    fragColor = vec4(col, 1.0);
}
)glsl";

const char* kScenePbrFragment = R"glsl(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;
uniform vec4 uBase;
uniform float uMetal;
uniform float uRough;
uniform vec4 uEmissive;
uniform float uBoost;
uniform int uFlags;
uniform float uCutoff;
uniform float uNormalScale;
uniform sampler2D uTex;
uniform sampler2D uTexMr;
uniform sampler2D uTexNormal;
uniform sampler2D uTexEmissive;
uniform vec3 uCamera;
uniform float uFollow;
uniform vec3 uColorA;
uniform vec3 uColorB;
uniform float uRainbow;
uniform float uTime;
uniform float uKick;
uniform int uLightCount;
uniform vec4 uLightPos[4];
uniform vec3 uLightColor[4];
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyGround;
uniform float uSkyIntensity;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uExposure;
const float PI = 3.14159265;

vec3 toLinear(vec3 c) {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 skyEnv(vec3 d) {
    float h = d.y;
    vec3 c = h >= 0.0 ? mix(uSkyHorizon, uSkyTop, pow(h, 0.45)) : mix(uSkyHorizon, uSkyGround, pow(-h, 0.45));
    float sun = pow(max(dot(d, uSunDir), 0.0), 96.0);
    float card = smoothstep(0.30, 0.0, abs(h - 0.25)) * 0.55;
    c += uSunColor * sun * 5.0 + uSkyHorizon * card;
    return c * uSkyIntensity;
}

vec3 diffuseEnv(vec3 n) {
    vec3 c = mix(uSkyGround, uSkyTop, smoothstep(-0.8, 0.8, n.y));
    c = mix(c, uSkyHorizon, 0.35 * (1.0 - abs(n.y)));
    return c * uSkyIntensity;
}

vec3 envBrdf(vec3 f0, float rough, float nov) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nov)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

vec3 perturb(vec3 n, vec3 p, vec2 uv, vec3 m) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float inv = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
    return normalize(mat3(t * inv, b * inv, n) * m);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    vec4 albedo = uBase * vColor;
    if ((uFlags & 1) != 0) {
        vec4 t = texture(uTex, vUv);
        albedo.rgb *= toLinear(t.rgb);
        albedo.a *= t.a;
    }
    if ((uFlags & 16) != 0 && albedo.a < uCutoff) {
        discard;
    }
    float metal = uMetal;
    float rough = uRough;
    if ((uFlags & 2) != 0) {
        vec4 mr = texture(uTexMr, vUv);
        rough *= mr.g;
        metal *= mr.b;
    }
    rough = clamp(rough, 0.045, 1.0);
    metal = clamp(metal, 0.0, 1.0);
    if ((uFlags & 4) != 0) {
        vec3 m = texture(uTexNormal, vUv).xyz * 2.0 - 1.0;
        m.xy *= uNormalScale;
        n = perturb(n, vWorld, vUv, m);
    }
    vec3 base = albedo.rgb;
    if (uFollow > 0.5) {
        float l = clamp(pow(dot(base, vec3(0.2126, 0.7152, 0.0722)), 0.4545), 0.0, 1.0);
        vec3 tint = mix(uColorA, uColorB, l);
        if (uRainbow > 0.5) {
            tint = 0.5 + 0.5 * cos(6.28318 * (l * 0.6 + uTime * 0.05 + vec3(0.0, 0.33, 0.67)));
        }
        base = toLinear(tint) * (0.1 + 0.9 * l);
    }
    float glowColor = max(max(uEmissive.r, uEmissive.g), uEmissive.b);
    vec3 emissive = uEmissive.rgb * uEmissive.a;
    if ((uFlags & 8) != 0) {
        emissive *= toLinear(texture(uTexEmissive, vUv).rgb);
    }
    emissive += glowColor > 0.001 ? uEmissive.rgb * uBoost : base * uBoost;

    vec3 v = normalize(uCamera - vWorld);
    float nov = max(dot(n, v), 1e-4);
    vec3 f0 = mix(vec3(0.04), base, metal);
    vec3 diffuseColor = base * (1.0 - metal);
    float a = rough * rough;
    float a2 = a * a;
    vec3 color = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (i >= uLightCount) {
            break;
        }
        vec3 l;
        float atten = 1.0;
        if (uLightPos[i].w > 0.5) {
            vec3 d = uLightPos[i].xyz - vWorld;
            float dist2 = dot(d, d);
            l = d * inversesqrt(max(dist2, 1e-6));
            atten = 1.0 / (1.0 + dist2);
        } else {
            l = normalize(uLightPos[i].xyz);
        }
        float nol = max(dot(n, l), 0.0);
        if (nol <= 0.0) {
            continue;
        }
        vec3 h = normalize(l + v);
        float noh = max(dot(n, h), 0.0);
        float voh = max(dot(v, h), 0.0);
        float dd = noh * noh * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * dd * dd);
        float vis = 0.5 / (nol * sqrt(nov * nov * (1.0 - a2) + a2) + nov * sqrt(nol * nol * (1.0 - a2) + a2));
        vec3 F = f0 + (1.0 - f0) * pow(1.0 - voh, 5.0);
        vec3 spec = D * vis * F;
        vec3 diff = (1.0 - F) * diffuseColor / PI;
        color += (diff + spec) * uLightColor[i] * atten * nol;
    }
    vec3 r = reflect(-v, n);
    vec3 specEnv = mix(skyEnv(r), diffuseEnv(r), clamp(rough * 1.15, 0.0, 1.0));
    color += diffuseColor * diffuseEnv(n) + specEnv * envBrdf(f0, rough, nov);
    color += emissive;
    color *= uExposure * (1.0 + 0.12 * uKick);
    color = aces(color);
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
)glsl";

double nowMs() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

constexpr float kFov = 0.8f;

}

void ScenePlayer::touch() {
    lastUse_ = nowMs();
}

double ScenePlayer::idleSeconds() const {
    return (nowMs() - lastUse_) * 0.001;
}

ScenePlayer::ScenePlayer(std::string label, Loader loader) : label_(std::move(label)), loader_(std::move(loader)) {}

ScenePlayer::~ScenePlayer() {
    cancel_.store(true);
    if (pending_.valid()) {
        pending_.wait();
    }
    freeGpu();
}

void ScenePlayer::start() {
    if (state_ != SceneState::Idle) {
        return;
    }
    state_ = SceneState::Loading;
    cancel_.store(false);
    pending_ = std::async(std::launch::async, [loader = loader_, &flag = cancel_]() {
        const double begin = nowMs();
        GltfLimits limits;
        limits.cancel = &flag;
        GltfResult result = loader(limits);
        result.milliseconds = nowMs() - begin;
        return result;
    });
}

SceneState ScenePlayer::poll() {
    if (state_ == SceneState::Idle) {
        start();
    }
    if (state_ != SceneState::Loading) {
        return state_;
    }
    if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return state_;
    }
    GltfResult result;
    try {
        result = pending_.get();
    } catch (const std::exception& e) {
        result.ok = false;
        result.error = e.what();
    }
    loadMs_ = result.milliseconds;
    warnings_ = result.warnings;
    if (!result.ok) {
        error_ = result.error;
        state_ = SceneState::Failed;
        logLine("3D scene '" + label_ + "' could not be loaded: " + error_);
        return state_;
    }
    scene_ = std::move(result.scene);
    const double uploadStart = nowMs();
    if (!upload()) {
        state_ = SceneState::Failed;
        logLine("3D scene '" + label_ + "' could not be uploaded: " + error_);
        return state_;
    }
    uploadMs_ = nowMs() - uploadStart;
    state_ = SceneState::Ready;
    lastUse_ = nowMs();
    for (const std::string& warning : warnings_) {
        logLine("3D scene '" + label_ + "': " + warning);
    }
    logLine("3D scene '" + label_ + "' ready: " + std::to_string(stats_.vertices) + " vertices, " + std::to_string(stats_.triangles) + " triangles, " + std::to_string(stats_.gpuBytes / 1024) + " KiB GPU, load " + std::to_string(static_cast<int>(loadMs_)) + " ms");
    return state_;
}

bool ScenePlayer::wait() {
    if (state_ == SceneState::Idle) {
        start();
    }
    if (state_ == SceneState::Loading && pending_.valid()) {
        pending_.wait();
    }
    return poll() == SceneState::Ready;
}

void ScenePlayer::cancel() {
    if (state_ == SceneState::Loading) {
        cancel_.store(true);
    }
}

void ScenePlayer::retry() {
    if (state_ == SceneState::Failed) {
        release();
    }
}

void ScenePlayer::release() {
    cancel_.store(true);
    if (pending_.valid()) {
        pending_.wait();
        pending_ = std::future<GltfResult>();
    }
    freeGpu();
    scene_ = Scene{};
    state_ = SceneState::Idle;
    error_.clear();
}

void ScenePlayer::freeGpu() {
    for (GpuMesh& mesh : meshes_) {
        if (mesh.vao) {
            glDeleteVertexArrays(1, &mesh.vao);
        }
        if (mesh.vbo) {
            glDeleteBuffers(1, &mesh.vbo);
        }
        if (mesh.ebo) {
            glDeleteBuffers(1, &mesh.ebo);
        }
    }
    meshes_.clear();
    if (!textures_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        textures_.clear();
    }
}

bool ScenePlayer::upload() {
    stats_ = sceneStats(scene_);
    animation_ = scene_.animations.empty() ? -1 : 0;
    const SceneSphere sphere = sceneSphere(scene_, animation_, animation_ >= 0 ? 12 : 1);
    if (!sphere.valid) {
        error_ = "the scene has no geometry";
        return false;
    }
    center_ = sphere.center;
    radius_ = sphere.radius;

    while (glGetError() != GL_NO_ERROR) {
    }
    for (const SceneImage& image : scene_.images) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        textures_.push_back(id);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    constexpr size_t stride = kSceneVertexFloats * sizeof(float);
    for (const SceneMesh& mesh : scene_.meshes) {
        std::vector<float> data(mesh.positions.size() * kSceneVertexFloats);
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
            float* v = &data[i * kSceneVertexFloats];
            v[0] = mesh.positions[i].x;
            v[1] = mesh.positions[i].y;
            v[2] = mesh.positions[i].z;
            v[3] = mesh.normals[i].x;
            v[4] = mesh.normals[i].y;
            v[5] = mesh.normals[i].z;
            v[6] = mesh.uvs.empty() ? 0.0f : mesh.uvs[i * 2];
            v[7] = mesh.uvs.empty() ? 0.0f : mesh.uvs[i * 2 + 1];
            for (size_t c = 0; c < 4; ++c) {
                v[8 + c] = mesh.colors.empty() ? 1.0f : mesh.colors[i * 4 + c];
            }
        }
        GpuMesh gpu;
        gpu.indexCount = static_cast<GLsizei>(mesh.indices.size());
        gpu.material = mesh.material;
        glGenVertexArrays(1, &gpu.vao);
        glGenBuffers(1, &gpu.vbo);
        glGenBuffers(1, &gpu.ebo);
        glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);
        const int widths[4] = {3, 3, 2, 4};
        size_t offset = 0;
        for (int a = 0; a < 4; ++a) {
            glEnableVertexAttribArray(static_cast<GLuint>(a));
            glVertexAttribPointer(static_cast<GLuint>(a), widths[a], GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<const void*>(offset * sizeof(float)));
            offset += static_cast<size_t>(widths[a]);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        meshes_.push_back(gpu);
    }
    if (glGetError() == GL_OUT_OF_MEMORY) {
        error_ = "the graphics card ran out of memory";
        freeGpu();
        return false;
    }
    for (SceneMesh& mesh : scene_.meshes) {
        std::vector<Vec3>().swap(mesh.positions);
        std::vector<Vec3>().swap(mesh.normals);
        std::vector<float>().swap(mesh.uvs);
        std::vector<float>().swap(mesh.colors);
        std::vector<uint32_t>().swap(mesh.indices);
    }
    std::vector<SceneImage>().swap(scene_.images);
    return true;
}

void SceneRenderer::locate(Program& p) {
    const auto at = [&](const char* name) { return glGetUniformLocation(p.id, name); };
    p.viewProj = at("uViewProj");
    p.model = at("uModel");
    p.normalMat = at("uNormalMat");
    p.base = at("uBase");
    p.hasTex = at("uHasTex");
    p.tex = at("uTex");
    p.camera = at("uCamera");
    p.palette = at("uPalette");
    p.colorA = at("uColorA");
    p.colorB = at("uColorB");
    p.rainbow = at("uRainbow");
    p.time = at("uTime");
    p.kick = at("uKick");
    p.metal = at("uMetal");
    p.rough = at("uRough");
    p.emissive = at("uEmissive");
    p.boost = at("uBoost");
    p.flags = at("uFlags");
    p.cutoff = at("uCutoff");
    p.normalScale = at("uNormalScale");
    p.texMr = at("uTexMr");
    p.texNormal = at("uTexNormal");
    p.texEmissive = at("uTexEmissive");
    p.follow = at("uFollow");
    p.lightCount = at("uLightCount");
    p.lightPos = at("uLightPos");
    p.lightColor = at("uLightColor");
    p.skyTop = at("uSkyTop");
    p.skyHorizon = at("uSkyHorizon");
    p.skyGround = at("uSkyGround");
    p.skyIntensity = at("uSkyIntensity");
    p.sunDir = at("uSunDir");
    p.sunColor = at("uSunColor");
    p.exposure = at("uExposure");
}

bool SceneRenderer::init() {
    std::string error;
    classic_.id = buildProgram(kSceneVertex, kSceneFragment, &error);
    if (!classic_.id) {
        logLine("3D scene shader failed: " + error);
        return false;
    }
    locate(classic_);
    pbr_.id = buildProgram(kSceneVertex, kScenePbrFragment, &error);
    if (!pbr_.id) {
        logLine("3D PBR shader failed: " + error);
    } else {
        locate(pbr_);
    }
    return true;
}

void SceneRenderer::destroy(Surface& surface) {
    if (surface.fbo) {
        glDeleteFramebuffers(1, &surface.fbo);
    }
    if (surface.depth) {
        glDeleteRenderbuffers(1, &surface.depth);
    }
    surface = Surface{};
}

void SceneRenderer::shutdown() {
    destroy(main_);
    destroy(thumb_);
    destroy(editor_);
    for (Program* program : {&classic_, &pbr_}) {
        if (program->id) {
            glDeleteProgram(program->id);
        }
        *program = Program{};
    }
}

size_t SceneRenderer::depthBytes() const {
    size_t total = 0;
    for (const Surface* surface : {&main_, &thumb_, &editor_}) {
        total += static_cast<size_t>(surface->width) * static_cast<size_t>(surface->height) * 4;
    }
    return total;
}

bool SceneRenderer::prepare(Surface& surface, const RenderTarget& target) {
    if (!surface.fbo) {
        glGenFramebuffers(1, &surface.fbo);
        glGenRenderbuffers(1, &surface.depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, surface.fbo);
    bool changed = false;
    if (surface.width != target.width || surface.height != target.height) {
        glBindRenderbuffer(GL_RENDERBUFFER, surface.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, target.width, target.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, surface.depth);
        surface.width = target.width;
        surface.height = target.height;
        changed = true;
    }
    if (surface.color != target.texture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
        surface.color = target.texture;
        changed = true;
    }
    if (changed && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        logLine("3D scene framebuffer is incomplete");
        surface.color = 0;
        surface.width = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    return true;
}

void SceneRenderer::draw(const ScenePlayer& player, const SceneFrame& frame, const RenderTarget& target) {
    if (!classic_.id || player.state() != SceneState::Ready || !target.texture) {
        return;
    }
    const Scene& scene = player.scene_;
    const bool usePbr = scene.shading == Shading::Pbr && pbr_.id != 0;
    const Program& p = usePbr ? pbr_ : classic_;
    Surface& surface = frame.editor ? editor_ : (target.width >= 512 ? main_ : thumb_);
    if (!prepare(surface, target)) {
        return;
    }
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SceneAudio audio;
    audio.bass = frame.bass;
    audio.mid = frame.mid;
    audio.high = frame.high;
    audio.level = frame.level;
    audio.kick = frame.kick;

    const SceneCamera& camera = scene.camera;
    const float fov = camera.authored ? std::clamp(camera.fovDegrees, 15.0f, 110.0f) * 0.0174532925f : kFov;
    const float zoom = std::clamp(frame.scale, 0.2f, 4.0f);
    float distance = player.radius_ / std::sin(fov * 0.5f) * 1.04f / zoom;
    float yaw;
    float pitch;
    if (camera.authored) {
        distance *= camera.distance * std::clamp(1.0f - bindingSum(camera.bindings, BindTarget::Zoom, audio), 0.3f, 3.0f);
        yaw = camera.yawDegrees * 0.0174532925f + frame.time * camera.orbitSpeed;
        pitch = (camera.pitchDegrees + camera.pitchSway * std::sin(frame.time * 0.31f)) * 0.0174532925f;
    } else {
        distance *= 1.0f - 0.04f * frame.kick;
        yaw = frame.time * 0.4f;
        pitch = 0.32f + 0.12f * std::sin(frame.time * 0.31f);
    }
    if (frame.editorCamera) {
        yaw = frame.editYaw;
        pitch = std::clamp(frame.editPitch, -1.5f, 1.5f);
        distance = player.radius_ / std::sin(fov * 0.5f) * 1.04f * std::clamp(frame.editDistance, 0.05f, 20.0f);
    }
    const Vec3 eye = player.center_ + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)} * distance;
    const float aspect = static_cast<float>(target.width) / static_cast<float>(std::max(target.height, 1));
    const Mat4 viewProj = perspective(fov, aspect, std::max(distance * 0.02f, 1e-3f), distance + player.radius_ * 8.0f) * lookAt(eye, player.center_, {0.0f, 1.0f, 0.0f});
    const float pulse = camera.authored ? 1.0f : 1.0f + 0.07f * frame.kick + 0.04f * frame.bass;
    const Mat4 pivot = translation(player.center_) * scaling({pulse, pulse, pulse}) * translation(player.center_ * -1.0f);

    glUseProgram(p.id);
    glUniformMatrix4fv(p.viewProj, 1, GL_FALSE, viewProj.m.data());
    glUniform3f(p.camera, eye.x, eye.y, eye.z);
    glUniform1f(p.palette, scene.paletteTint ? 1.0f : 0.0f);
    glUniform3f(p.colorA, frame.colorA[0], frame.colorA[1], frame.colorA[2]);
    glUniform3f(p.colorB, frame.colorB[0], frame.colorB[1], frame.colorB[2]);
    glUniform1f(p.rainbow, frame.rainbow ? 1.0f : 0.0f);
    glUniform1f(p.time, frame.time);
    glUniform1f(p.kick, frame.kick);
    glUniform1i(p.tex, 0);
    glActiveTexture(GL_TEXTURE0);

    if (usePbr) {
        float positions[16] = {};
        float colors[12] = {};
        int count = 0;
        Vec3 sunDir{0.5f, 0.8f, 0.6f};
        std::array<float, 3> sunColor{1.0f, 0.95f, 0.9f};
        bool sunSet = false;
        const auto addLight = [&](bool point, Vec3 position, std::array<float, 3> color, float intensity) {
            if (count >= static_cast<int>(kSceneMaxLights)) {
                return;
            }
            positions[count * 4 + 0] = position.x;
            positions[count * 4 + 1] = position.y;
            positions[count * 4 + 2] = position.z;
            positions[count * 4 + 3] = point ? 1.0f : 0.0f;
            for (int c = 0; c < 3; ++c) {
                colors[count * 3 + c] = color[static_cast<size_t>(c)] * intensity;
            }
            if (!point && !sunSet) {
                sunDir = normalize(position);
                sunColor = color;
                sunSet = true;
            }
            ++count;
        };
        if (scene.lights.empty()) {
            addLight(false, {0.5f, 0.8f, 0.6f}, {1.0f, 0.96f, 0.9f}, 3.2f);
            addLight(false, {-0.6f, -0.2f, -0.5f}, {0.6f, 0.7f, 1.0f}, 0.8f);
            addLight(false, {0.0f, 0.3f, -1.0f}, {1.0f, 1.0f, 1.0f}, 1.5f);
        } else {
            for (const SceneLight& light : scene.lights) {
                const float reactive = std::max(1.0f + bindingSum(light.bindings, BindTarget::Light, audio), 0.0f);
                addLight(light.point, light.position, light.color, light.intensity * reactive);
            }
        }
        glUniform1i(p.lightCount, count);
        glUniform4fv(p.lightPos, 4, positions);
        glUniform3fv(p.lightColor, 4, colors);
        glUniform3f(p.skyTop, scene.sky.top[0], scene.sky.top[1], scene.sky.top[2]);
        glUniform3f(p.skyHorizon, scene.sky.horizon[0], scene.sky.horizon[1], scene.sky.horizon[2]);
        glUniform3f(p.skyGround, scene.sky.ground[0], scene.sky.ground[1], scene.sky.ground[2]);
        glUniform1f(p.skyIntensity, scene.sky.intensity);
        glUniform3f(p.sunDir, sunDir.x, sunDir.y, sunDir.z);
        glUniform3f(p.sunColor, sunColor[0], sunColor[1], sunColor[2]);
        glUniform1f(p.exposure, scene.exposure);
        glUniform1i(p.texMr, 1);
        glUniform1i(p.texNormal, 2);
        glUniform1i(p.texEmissive, 3);
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    bool culling = true;
    const auto textureOf = [&](int index) -> GLuint {
        return index >= 0 && index < static_cast<int>(player.textures_.size()) ? player.textures_[static_cast<size_t>(index)] : 0;
    };
    for (const SceneInstance& instance : evaluateScene(scene, player.animation_, frame.time, &audio)) {
        if (instance.mesh < 0 || instance.mesh >= static_cast<int>(player.meshes_.size())) {
            continue;
        }
        const ScenePlayer::GpuMesh& mesh = player.meshes_[static_cast<size_t>(instance.mesh)];
        const Mat4 model = pivot * instance.world;
        const std::array<float, 9> normal = normalMatrix(model);
        glUniformMatrix4fv(p.model, 1, GL_FALSE, model.m.data());
        glUniformMatrix3fv(p.normalMat, 1, GL_FALSE, normal.data());
        SceneMaterial material;
        if (mesh.material >= 0 && mesh.material < static_cast<int>(scene.materials.size())) {
            material = scene.materials[static_cast<size_t>(mesh.material)];
        }
        glUniform4f(p.base, material.baseColor[0], material.baseColor[1], material.baseColor[2], material.baseColor[3]);
        const GLuint baseTexture = textureOf(material.baseTexture);
        if (usePbr) {
            const GLuint mrTexture = textureOf(material.metallicRoughnessTexture);
            const GLuint normalTexture = textureOf(material.normalTexture);
            const GLuint emissiveTexture = textureOf(material.emissiveTexture);
            int flags = 0;
            flags |= baseTexture ? 1 : 0;
            flags |= mrTexture ? 2 : 0;
            flags |= normalTexture ? 4 : 0;
            flags |= emissiveTexture ? 8 : 0;
            flags |= material.alphaMode == AlphaMode::Mask ? 16 : 0;
            glUniform1i(p.flags, flags);
            glUniform1f(p.metal, material.metallic);
            glUniform1f(p.rough, material.roughness);
            glUniform4f(p.emissive, material.emissive[0], material.emissive[1], material.emissive[2], material.emissiveStrength);
            glUniform1f(p.boost, instance.emissive);
            glUniform1f(p.cutoff, material.alphaCutoff);
            glUniform1f(p.normalScale, material.normalScale);
            glUniform1f(p.follow, material.followPalette ? 1.0f : 0.0f);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, mrTexture);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, normalTexture);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, emissiveTexture);
            glActiveTexture(GL_TEXTURE0);
        } else {
            glUniform1i(p.hasTex, baseTexture ? 1 : 0);
        }
        glBindTexture(GL_TEXTURE_2D, baseTexture);
        const bool wantCulling = !material.doubleSided;
        if (wantCulling != culling) {
            if (wantCulling) {
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }
            culling = wantCulling;
        }
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    if (usePbr) {
        for (const GLenum unit : {GL_TEXTURE3, GL_TEXTURE2, GL_TEXTURE1}) {
            glActiveTexture(unit);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}

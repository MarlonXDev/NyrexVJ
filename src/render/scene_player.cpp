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
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform int uSkinned;
uniform sampler2D uJointTex;
uniform int uJointBase;
uniform int uMorphCount;
uniform int uMorphVerts;
uniform sampler2D uMorphTex;
uniform int uMorphIndex[8];
uniform float uMorphWeight[8];
uniform float uInflate;
uniform float uRipple;
uniform float uDeformRadius;
uniform float uTime;
out vec3 vNormal;
out vec3 vWorld;
out vec2 vUv;
out vec4 vColor;
mat4 jointMatrix(uint joint) {
    int row = uJointBase + int(joint);
    return mat4(texelFetch(uJointTex, ivec2(0, row), 0), texelFetch(uJointTex, ivec2(1, row), 0), texelFetch(uJointTex, ivec2(2, row), 0), texelFetch(uJointTex, ivec2(3, row), 0));
}
vec3 morphFetch(int texel) {
    return texelFetch(uMorphTex, ivec2(texel % 2048, texel / 2048), 0).rgb;
}
void main() {
    vec3 position = aPos;
    vec3 normal = aNormal;
    for (int i = 0; i < 8; ++i) {
        if (i >= uMorphCount) {
            break;
        }
        int base = (uMorphIndex[i] * uMorphVerts + gl_VertexID) * 2;
        position += uMorphWeight[i] * morphFetch(base);
        normal += uMorphWeight[i] * morphFetch(base + 1);
    }
    if (uInflate != 0.0 || uRipple != 0.0) {
        float radius = max(uDeformRadius, 1e-4);
        vec3 direction = normalize(normal + vec3(1e-6));
        float shift = uInflate * 0.25;
        if (uRipple != 0.0) {
            shift += uRipple * 0.12 * sin(dot(position / radius, vec3(3.1, 4.3, 2.7)) + uTime * 5.0);
        }
        position += direction * (shift * radius);
    }
    vec4 local = vec4(position, 1.0);
    if (uSkinned != 0) {
        mat4 skin = aWeights.x * jointMatrix(aJoints.x) + aWeights.y * jointMatrix(aJoints.y) + aWeights.z * jointMatrix(aJoints.z) + aWeights.w * jointMatrix(aJoints.w);
        local = skin * local;
        normal = mat3(skin) * normal;
    }
    vec4 world = uModel * local;
    vWorld = world.xyz;
    vNormal = uNormalMat * normal;
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
uniform int uBlend;
uniform int uSplit;
void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    vec4 albedo = uBase * vColor;
    if (uHasTex != 0) {
        albedo *= texture(uTex, vUv);
    }
    if (uSplit == 1 && albedo.a < 0.98) {
        discard;
    }
    if (uSplit == 2 && albedo.a >= 0.98) {
        discard;
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
    fragColor = vec4(col, uBlend != 0 ? albedo.a : 1.0);
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
uniform float uHue;
uniform float uRoughAdd;
uniform vec2 uUvShift;
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
uniform int uSplit;
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

vec3 hueRotate(vec3 c, float turns) {
    float a = 6.28318 * turns;
    float cs = cos(a);
    float sn = sin(a);
    vec3 k = vec3(0.57735);
    return max(c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs), vec3(0.0));
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    vec2 uv = vUv + uUvShift;
    vec4 albedo = uBase * vColor;
    if ((uFlags & 1) != 0) {
        vec4 t = texture(uTex, uv);
        albedo.rgb *= toLinear(t.rgb);
        albedo.a *= t.a;
    }
    if ((uFlags & 16) != 0 && albedo.a < uCutoff) {
        discard;
    }
    if (uSplit == 1 && albedo.a < 0.98) {
        discard;
    }
    if (uSplit == 2 && albedo.a >= 0.98) {
        discard;
    }
    float metal = uMetal;
    float rough = uRough;
    if ((uFlags & 2) != 0) {
        vec4 mr = texture(uTexMr, uv);
        rough *= mr.g;
        metal *= mr.b;
    }
    rough = clamp(rough + uRoughAdd, 0.045, 1.0);
    metal = clamp(metal, 0.0, 1.0);
    if ((uFlags & 4) != 0) {
        vec3 m = texture(uTexNormal, uv).xyz * 2.0 - 1.0;
        m.xy *= uNormalScale;
        n = perturb(n, vWorld, uv, m);
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
    vec3 emissiveTint = uEmissive.rgb;
    if (uHue != 0.0) {
        base = hueRotate(base, uHue);
        emissiveTint = hueRotate(emissiveTint, uHue);
    }
    float glowColor = max(max(uEmissive.r, uEmissive.g), uEmissive.b);
    vec3 emissive = emissiveTint * uEmissive.a;
    if ((uFlags & 8) != 0) {
        vec3 glowTexture = toLinear(texture(uTexEmissive, uv).rgb);
        emissive *= uHue != 0.0 ? hueRotate(glowTexture, uHue) : glowTexture;
    }
    emissive += glowColor > 0.001 ? emissiveTint * uBoost : base * uBoost;

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
    fragColor = vec4(color, (uFlags & 32) != 0 ? albedo.a : 1.0);
}
)glsl";

double nowMs() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

constexpr float kFov = 0.8f;

}

void SceneFrame::setAudio(const AudioFeatures& features, double beatPosition) {
    bass = features.bass;
    mid = features.mid;
    high = features.high;
    level = features.level;
    kick = features.kick;
    beat = beatPosition;
    beatPulse = nvj::beatPulse(beatPosition);
    bands = spectrumBands(features.spectrum.data(), kSpectrumBins);
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
        if (mesh.skinVbo) {
            glDeleteBuffers(1, &mesh.skinVbo);
        }
        if (mesh.morphTex) {
            glDeleteTextures(1, &mesh.morphTex);
        }
    }
    meshes_.clear();
    if (!textures_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        textures_.clear();
    }
}

void ScenePlayer::uploadMorph(const SceneMesh& mesh, GpuMesh& gpu) {
    constexpr int kWidth = 2048;
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    const size_t vertices = mesh.positions.size();
    const size_t targets = std::min(mesh.targets.size(), kSceneMaxMorphTargets);
    const size_t texels = vertices * targets * 2;
    const size_t rows = (texels + kWidth - 1) / kWidth;
    if (rows > static_cast<size_t>(maxSize)) {
        warnings_.push_back("the morph targets of a mesh are too large for this graphics card and are ignored");
        return;
    }
    glGenTextures(1, &gpu.morphTex);
    glBindTexture(GL_TEXTURE_2D, gpu.morphTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, kWidth, static_cast<GLsizei>(rows), 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    constexpr size_t kChunkRows = 128;
    std::vector<float> chunk(static_cast<size_t>(kWidth) * kChunkRows * 3);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t row = 0; row < rows; row += kChunkRows) {
        const size_t count = std::min(kChunkRows, rows - row);
        std::fill(chunk.begin(), chunk.end(), 0.0f);
        for (size_t r = 0; r < count; ++r) {
            for (size_t x = 0; x < static_cast<size_t>(kWidth); ++x) {
                const size_t texel = (row + r) * kWidth + x;
                if (texel >= texels) {
                    break;
                }
                const size_t pair = texel / 2;
                const size_t target = pair / vertices;
                const size_t vertex = pair % vertices;
                const SceneMorphTarget& source = mesh.targets[target];
                const std::vector<Vec3>& list = texel % 2 == 0 ? source.positions : source.normals;
                if (list.empty()) {
                    continue;
                }
                float* out = &chunk[(r * kWidth + x) * 3];
                out[0] = list[vertex].x;
                out[1] = list[vertex].y;
                out[2] = list[vertex].z;
            }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(row), kWidth, static_cast<GLsizei>(count), GL_RGB, GL_FLOAT, chunk.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    gpu.morphTargets = static_cast<int>(targets);
}

bool ScenePlayer::upload() {
    stats_ = sceneStats(scene_);
    if (stats_.gpuBytes > kSceneMaxGpuBytes) {
        error_ = "the scene needs about " + std::to_string(stats_.gpuBytes / (1024 * 1024)) + " MiB of graphics memory, the limit is " + std::to_string(kSceneMaxGpuBytes / (1024 * 1024)) + " MiB";
        return false;
    }
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
        gpu.vertexCount = static_cast<GLsizei>(mesh.positions.size());
        gpu.material = mesh.material;
        Vec3 low = mesh.positions[0];
        Vec3 high = mesh.positions[0];
        for (const Vec3& point : mesh.positions) {
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        }
        gpu.centre = (low + high) * 0.5f;
        gpu.radius = std::max(length((high - low) * 0.5f), 1e-4f);
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
        if (!mesh.joints.empty()) {
            std::vector<uint16_t> skinData(mesh.positions.size() * 8);
            for (size_t i = 0; i < mesh.positions.size(); ++i) {
                for (size_t k = 0; k < 4; ++k) {
                    skinData[i * 8 + k] = mesh.joints[i * 4 + k];
                    skinData[i * 8 + 4 + k] = static_cast<uint16_t>(std::lround(std::clamp(mesh.weights[i * 4 + k], 0.0f, 1.0f) * 65535.0f));
                }
            }
            glGenBuffers(1, &gpu.skinVbo);
            glBindBuffer(GL_ARRAY_BUFFER, gpu.skinVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(skinData.size() * sizeof(uint16_t)), skinData.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(4);
            glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, 16, nullptr);
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 4, GL_UNSIGNED_SHORT, GL_TRUE, 16, reinterpret_cast<const void*>(8));
            gpu.skinned = true;
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (!mesh.targets.empty()) {
            uploadMorph(mesh, gpu);
        }
        meshes_.push_back(gpu);
    }
    if (glGetError() == GL_OUT_OF_MEMORY) {
        error_ = "the graphics card ran out of memory";
        freeGpu();
        return false;
    }
    for (SceneMesh& mesh : scene_.meshes) {
        mesh.rememberAnimationData();
        std::vector<Vec3>().swap(mesh.positions);
        std::vector<Vec3>().swap(mesh.normals);
        std::vector<float>().swap(mesh.uvs);
        std::vector<float>().swap(mesh.colors);
        std::vector<uint32_t>().swap(mesh.indices);
        std::vector<uint16_t>().swap(mesh.joints);
        std::vector<float>().swap(mesh.weights);
        std::vector<SceneMorphTarget>().swap(mesh.targets);
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
    p.hue = at("uHue");
    p.roughAdd = at("uRoughAdd");
    p.uvShift = at("uUvShift");
    p.inflate = at("uInflate");
    p.ripple = at("uRipple");
    p.deformRadius = at("uDeformRadius");
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
    p.blend = at("uBlend");
    p.split = at("uSplit");
    p.skinned = at("uSkinned");
    p.jointTex = at("uJointTex");
    p.jointBase = at("uJointBase");
    p.morphCount = at("uMorphCount");
    p.morphVerts = at("uMorphVerts");
    p.morphTex = at("uMorphTex");
    p.morphIndex = at("uMorphIndex");
    p.morphWeight = at("uMorphWeight");
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
    glGenTextures(1, &jointTexture_);
    glBindTexture(GL_TEXTURE_2D, jointTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, static_cast<GLsizei>(kSceneMaxJointsPerFrame), 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    if (jointTexture_) {
        glDeleteTextures(1, &jointTexture_);
        jointTexture_ = 0;
    }
    destroy(main_);
    destroy(thumb_);
    destroy(editor_);
    for (Surface& surface : worlds_) {
        destroy(surface);
    }
    resetWorlds();
    for (Program* program : {&classic_, &pbr_}) {
        if (program->id) {
            glDeleteProgram(program->id);
        }
        *program = Program{};
    }
}

size_t SceneRenderer::worldDepthBytes() const {
    size_t total = 0;
    for (const Surface& surface : worlds_) {
        total += static_cast<size_t>(surface.width) * static_cast<size_t>(surface.height) * 4;
    }
    return total;
}

size_t SceneRenderer::depthBytes() const {
    size_t total = worldDepthBytes();
    for (const Surface* surface : {&main_, &thumb_, &editor_}) {
        total += static_cast<size_t>(surface->width) * static_cast<size_t>(surface->height) * 4;
    }
    return total;
}

void SceneRenderer::resetWorlds() {
    for (WorldState& state : worldState_) {
        state.valid = false;
    }
}

void SceneRenderer::beginFrame() {
    ++frame_;
    resetWorlds();
    constexpr uint64_t kIdleFrames = 180;
    for (size_t i = 0; i < worlds_.size(); ++i) {
        if (worlds_[i].fbo && frame_ > worldUsed_[i] + kIdleFrames) {
            destroy(worlds_[i]);
        }
    }
}

SceneRenderer::LightSetup SceneRenderer::makeLights(const Scene& scene, const SceneAudio& audio) {
    LightSetup setup;
    bool sunSet = false;
    const auto addLight = [&](bool point, Vec3 position, std::array<float, 3> color, float intensity) {
        if (setup.count >= static_cast<int>(kSceneMaxLights)) {
            return;
        }
        const size_t at = static_cast<size_t>(setup.count);
        setup.positions[at * 4 + 0] = position.x;
        setup.positions[at * 4 + 1] = position.y;
        setup.positions[at * 4 + 2] = position.z;
        setup.positions[at * 4 + 3] = point ? 1.0f : 0.0f;
        for (size_t c = 0; c < 3; ++c) {
            setup.colors[at * 3 + c] = color[c] * intensity;
        }
        if (!point && !sunSet) {
            setup.sunDir = normalize(position);
            setup.sunColor = color;
            sunSet = true;
        }
        ++setup.count;
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
    setup.skyTop = {scene.sky.top[0], scene.sky.top[1], scene.sky.top[2]};
    setup.skyHorizon = {scene.sky.horizon[0], scene.sky.horizon[1], scene.sky.horizon[2]};
    setup.skyGround = {scene.sky.ground[0], scene.sky.ground[1], scene.sky.ground[2]};
    setup.skyIntensity = scene.sky.intensity;
    setup.exposure = scene.exposure;
    return setup;
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
    int worldIndex = -1;
    bool worldOwner = false;
    if (frame.world >= 1 && frame.world <= kWorldCount && !frame.editor) {
        const size_t candidate = static_cast<size_t>(frame.world - 1);
        if (frame.worldOwner) {
            worldIndex = static_cast<int>(candidate);
            worldOwner = true;
            worldState_[candidate].valid = false;
        } else if (worldState_[candidate].valid && worlds_[candidate].width == target.width && worlds_[candidate].height == target.height) {
            worldIndex = static_cast<int>(candidate);
        }
    }
    const bool shared = worldIndex >= 0;
    const bool member = shared && !worldOwner;
    Surface& surface = shared ? worlds_[static_cast<size_t>(worldIndex)] : (frame.editor ? editor_ : (target.width >= 512 ? main_ : thumb_));
    if (!prepare(surface, target)) {
        return;
    }
    if (shared) {
        worldUsed_[static_cast<size_t>(worldIndex)] = frame_;
    }
    lastShared_ = shared;
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 0.0f, member ? 0.0f : 1.0f);
    glClearDepth(1.0);
    glClear(member ? GL_COLOR_BUFFER_BIT : (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    SceneAudio audio;
    audio.bass = frame.bass;
    audio.mid = frame.mid;
    audio.high = frame.high;
    audio.level = frame.level;
    audio.kick = frame.kick;
    audio.beat = frame.beatPulse;
    audio.bands = frame.bands;
    audio.materials = frame.materialBindings;

    const WorldState* ownerState = member ? &worldState_[static_cast<size_t>(worldIndex)] : nullptr;
    const bool moves = frame.cameraMoves;
    const SceneCamera& camera = activeSceneCamera(scene, frame.beat);
    Mat4 viewProj;
    Vec3 eye;
    Vec3 forward;
    if (member) {
        viewProj = ownerState->viewProj;
        eye = ownerState->eye;
        forward = ownerState->forward;
    } else {
        CameraPose pose = cameraPoseAt(scene, frame.beat, frame.time, audio, moves);
        if (moves) {
            if (frame.cameraOrbit != 0.0f) {
                pose.yaw += frame.cameraOrbit * kSceneRadians;
            }
            if (frame.cameraTilt != 0.0f) {
                pose.pitch = std::clamp(pose.pitch + frame.cameraTilt * kSceneRadians, -kSceneMaxPitch, kSceneMaxPitch);
            }
            if (frame.cameraFov != 0.0f) {
                pose.fov = std::clamp(pose.fov + frame.cameraFov * kSceneRadians, 15.0f * kSceneRadians, 110.0f * kSceneRadians);
            }
        }
        float zoom = std::clamp(frame.scale, 0.2f, 4.0f);
        if (frame.editorCamera) {
            pose.yaw = frame.editYaw;
            pose.pitch = std::clamp(frame.editPitch, -1.5f, 1.5f);
            pose.distance = std::clamp(frame.editDistance, 0.05f, 20.0f);
            pose.roll = 0.0f;
            pose.move = Vec3{};
            zoom = 1.0f;
        }
        const CameraView view = cameraView(pose, player.center_, player.radius_, zoom);
        eye = view.eye;
        const float distance = view.distance;
        const float aspect = static_cast<float>(target.width) / static_cast<float>(std::max(target.height, 1));
        viewProj = perspective(view.fov, aspect, std::max(distance * 0.02f, 1e-3f), distance + player.radius_ * (8.0f + length(pose.move))) * lookAt(eye, view.target, view.up);
        forward = normalize(player.center_ - eye);
    }
    Mat4 place;
    bool placed = false;
    if (member) {
        place = worldPlacementMatrix(ownerState->ownerCenter, ownerState->ownerRadius, player.center_, player.radius_, frame.placement);
        placed = true;
    } else if (shared && !worldPlacementIsIdentity(frame.placement)) {
        place = worldPlacementMatrix(player.center_, player.radius_, player.center_, player.radius_, frame.placement);
        placed = true;
    }
    LightSetup ownLights;
    if (worldOwner || (usePbr && !member)) {
        ownLights = makeLights(scene, audio);
    }
    const LightSetup& lights = member ? ownerState->lights : ownLights;
    if (worldOwner) {
        WorldState& state = worldState_[static_cast<size_t>(worldIndex)];
        state.viewProj = viewProj;
        state.eye = eye;
        state.forward = forward;
        state.ownerCenter = player.center_;
        state.ownerRadius = player.radius_;
        state.lights = ownLights;
        state.valid = true;
    }
    const FrustumPlanes frustum = frustumFromViewProjection(viewProj);
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
        glUniform1i(p.lightCount, lights.count);
        glUniform4fv(p.lightPos, 4, lights.positions.data());
        glUniform3fv(p.lightColor, 4, lights.colors.data());
        glUniform3f(p.skyTop, lights.skyTop[0], lights.skyTop[1], lights.skyTop[2]);
        glUniform3f(p.skyHorizon, lights.skyHorizon[0], lights.skyHorizon[1], lights.skyHorizon[2]);
        glUniform3f(p.skyGround, lights.skyGround[0], lights.skyGround[1], lights.skyGround[2]);
        glUniform1f(p.skyIntensity, lights.skyIntensity);
        glUniform3f(p.sunDir, lights.sunDir.x, lights.sunDir.y, lights.sunDir.z);
        glUniform3f(p.sunColor, lights.sunColor[0], lights.sunColor[1], lights.sunColor[2]);
        glUniform1f(p.exposure, lights.exposure);
        glUniform1i(p.texMr, 1);
        glUniform1i(p.texNormal, 2);
        glUniform1i(p.texEmissive, 3);
    }

    glUniform1i(p.jointTex, 4);
    glUniform1i(p.morphTex, 5);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    int cullMode = 1;
    const auto setCull = [&](int mode) {
        if (mode == cullMode) {
            return;
        }
        if (mode == 0) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(mode == 1 ? GL_BACK : GL_FRONT);
        }
        cullMode = mode;
    };
    const auto textureOf = [&](int index) -> GLuint {
        return index >= 0 && index < static_cast<int>(player.textures_.size()) ? player.textures_[static_cast<size_t>(index)] : 0;
    };
    const SceneEval eval = evaluateSceneFull(scene, player.animation_, frame.time, &audio);
    if (!eval.joints.empty()) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, jointTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, static_cast<GLsizei>(eval.joints.size()), GL_RGBA, GL_FLOAT, eval.joints.data());
        glActiveTexture(GL_TEXTURE0);
    }
    skippedSkinned_ = eval.skippedSkinned;
    if (eval.skippedSkinned > 0 && !skipLogged_) {
        skipLogged_ = true;
        logLine("3D scene '" + player.label() + "': " + std::to_string(eval.skippedSkinned) + " animated figures were skipped because the limit of " + std::to_string(kSceneMaxJointsPerFrame) + " joints per frame was reached");
    }
    const auto materialOf = [&](const ScenePlayer::GpuMesh& mesh) {
        SceneMaterial material;
        if (mesh.material >= 0 && mesh.material < static_cast<int>(scene.materials.size())) {
            material = scene.materials[static_cast<size_t>(mesh.material)];
        }
        return material;
    };
    const auto modelOf = [&](const SceneInstance& instance) {
        return placed ? place * pivot * instance.world : pivot * instance.world;
    };
    const auto outsideView = [&](const SceneInstance& instance, const ScenePlayer::GpuMesh& mesh) {
        if (!culling_ || instance.skin >= 0 || instance.inflate != 0.0f || instance.ripple != 0.0f || instance.weightCount > 0) {
            return false;
        }
        const Mat4 model = modelOf(instance);
        return !sphereMayBeVisible(frustum, transformPoint(model, mesh.centre), mesh.radius * largestAxisScale(model) * 1.02f + 1e-4f);
    };
    const auto drawInstance = [&](const SceneInstance& instance, int pass) {
        const ScenePlayer::GpuMesh& mesh = player.meshes_[static_cast<size_t>(instance.mesh)];
        const Mat4 model = modelOf(instance);
        const std::array<float, 9> normal = normalMatrix(model);
        glUniformMatrix4fv(p.model, 1, GL_FALSE, model.m.data());
        glUniformMatrix3fv(p.normalMat, 1, GL_FALSE, normal.data());
        const SceneMaterial material = materialOf(mesh);
        const bool blended = material.alphaMode == AlphaMode::Blend && pass == 1;
        glUniform1i(p.split, pass == 2 ? 1 : (pass == 1 && material.alphaMode == AlphaMode::Blend ? 2 : 0));
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
            flags |= blended ? 32 : 0;
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
            glUniform1i(p.blend, blended ? 1 : 0);
        }
        glUniform1f(p.inflate, instance.inflate);
        glUniform1f(p.ripple, instance.ripple);
        glUniform1f(p.deformRadius, mesh.radius);
        if (usePbr) {
            glUniform1f(p.hue, instance.hue);
            glUniform1f(p.roughAdd, instance.roughness);
            glUniform2f(p.uvShift, instance.uvX, instance.uvY);
        }
        glBindTexture(GL_TEXTURE_2D, baseTexture);
        const bool skinned = instance.skin >= 0 && mesh.skinned;
        glUniform1i(p.skinned, skinned ? 1 : 0);
        if (skinned) {
            glUniform1i(p.jointBase, instance.jointOffset);
        }
        int active = 0;
        int morphIndex[kSceneMaxActiveMorphs] = {};
        float morphWeight[kSceneMaxActiveMorphs] = {};
        if (mesh.morphTex && instance.weightCount > 0) {
            const size_t count = std::min(static_cast<size_t>(instance.weightCount), static_cast<size_t>(mesh.morphTargets));
            for (size_t t = 0; t < count; ++t) {
                const float w = eval.weights[static_cast<size_t>(instance.weightOffset) + t];
                if (std::fabs(w) < 1e-4f) {
                    continue;
                }
                size_t slot = static_cast<size_t>(active);
                if (active < static_cast<int>(kSceneMaxActiveMorphs)) {
                    ++active;
                } else {
                    slot = 0;
                    for (size_t k = 1; k < kSceneMaxActiveMorphs; ++k) {
                        if (std::fabs(morphWeight[k]) < std::fabs(morphWeight[slot])) {
                            slot = k;
                        }
                    }
                    if (std::fabs(morphWeight[slot]) >= std::fabs(w)) {
                        continue;
                    }
                }
                morphIndex[slot] = static_cast<int>(t);
                morphWeight[slot] = w;
            }
        }
        glUniform1i(p.morphCount, active);
        if (active > 0) {
            glUniform1i(p.morphVerts, mesh.vertexCount);
            glUniform1iv(p.morphIndex, static_cast<GLsizei>(kSceneMaxActiveMorphs), morphIndex);
            glUniform1fv(p.morphWeight, static_cast<GLsizei>(kSceneMaxActiveMorphs), morphWeight);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, mesh.morphTex);
            glActiveTexture(GL_TEXTURE0);
        }
        glBindVertexArray(mesh.vao);
        if (pass != 1) {
            setCull(material.doubleSided ? 0 : 1);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        } else if (material.doubleSided) {
            setCull(2);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
            setCull(1);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        } else {
            setCull(1);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    };

    struct Pending {
        size_t index;
        float depth;
    };
    std::vector<Pending> blendList;
    drawnInstances_ = 0;
    culledInstances_ = 0;
    for (size_t i = 0; i < eval.instances.size(); ++i) {
        const SceneInstance& instance = eval.instances[i];
        if (instance.mesh < 0 || instance.mesh >= static_cast<int>(player.meshes_.size())) {
            continue;
        }
        const ScenePlayer::GpuMesh& mesh = player.meshes_[static_cast<size_t>(instance.mesh)];
        if (outsideView(instance, mesh)) {
            ++culledInstances_;
            continue;
        }
        ++drawnInstances_;
        if (materialOf(mesh).alphaMode == AlphaMode::Blend) {
            const Mat4 placedPivot = placed ? place * pivot : pivot;
            Vec3 centre = transformPoint(placedPivot * instance.world, mesh.centre);
            if (instance.skin >= 0 && instance.jointCount > 0) {
                Vec3 sum{0.0f, 0.0f, 0.0f};
                for (int k = 0; k < instance.jointCount; ++k) {
                    sum = sum + transformPoint(placedPivot * eval.joints[static_cast<size_t>(instance.jointOffset + k)], mesh.centre);
                }
                centre = sum * (1.0f / static_cast<float>(instance.jointCount));
            }
            if (materialOf(mesh).baseColor[3] >= 0.98f) {
                drawInstance(instance, 2);
            }
            blendList.push_back({i, dot(centre - eye, forward)});
            continue;
        }
        drawInstance(instance, 0);
    }
    blendedDraws_ = blendList.size();
    if (!blendList.empty()) {
        std::stable_sort(blendList.begin(), blendList.end(), [](const Pending& a, const Pending& b) { return a.depth > b.depth; });
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDepthMask(GL_FALSE);
        for (const Pending& entry : blendList) {
            drawInstance(eval.instances[entry.index], 1);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0);
    for (const GLenum unit : {GL_TEXTURE5, GL_TEXTURE4}) {
        glActiveTexture(unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
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

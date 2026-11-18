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

double nowMs() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

constexpr float kFov = 0.8f;

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

bool SceneRenderer::init() {
    std::string error;
    program_ = buildProgram(kSceneVertex, kSceneFragment, &error);
    if (!program_) {
        logLine("3D scene shader failed: " + error);
        return false;
    }
    viewProj_ = glGetUniformLocation(program_, "uViewProj");
    model_ = glGetUniformLocation(program_, "uModel");
    normalMat_ = glGetUniformLocation(program_, "uNormalMat");
    base_ = glGetUniformLocation(program_, "uBase");
    hasTex_ = glGetUniformLocation(program_, "uHasTex");
    tex_ = glGetUniformLocation(program_, "uTex");
    camera_ = glGetUniformLocation(program_, "uCamera");
    palette_ = glGetUniformLocation(program_, "uPalette");
    colorA_ = glGetUniformLocation(program_, "uColorA");
    colorB_ = glGetUniformLocation(program_, "uColorB");
    rainbow_ = glGetUniformLocation(program_, "uRainbow");
    time_ = glGetUniformLocation(program_, "uTime");
    kick_ = glGetUniformLocation(program_, "uKick");
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
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
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
    if (!program_ || player.state() != SceneState::Ready || !target.texture) {
        return;
    }
    Surface& surface = target.width >= 512 ? main_ : thumb_;
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

    const float zoom = std::clamp(frame.scale, 0.2f, 4.0f);
    const float distance = player.radius_ / std::sin(kFov * 0.5f) * 1.04f / zoom * (1.0f - 0.04f * frame.kick);
    const float yaw = frame.time * 0.4f;
    const float pitch = 0.32f + 0.12f * std::sin(frame.time * 0.31f);
    const Vec3 eye = player.center_ + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)} * distance;
    const float aspect = static_cast<float>(target.width) / static_cast<float>(std::max(target.height, 1));
    const Mat4 viewProj = perspective(kFov, aspect, distance * 0.04f, distance + player.radius_ * 6.0f) * lookAt(eye, player.center_, {0.0f, 1.0f, 0.0f});
    const float pulse = 1.0f + 0.07f * frame.kick + 0.04f * frame.bass;
    const Mat4 pivot = translation(player.center_) * scaling({pulse, pulse, pulse}) * translation(player.center_ * -1.0f);

    glUseProgram(program_);
    glUniformMatrix4fv(viewProj_, 1, GL_FALSE, viewProj.m.data());
    glUniform3f(camera_, eye.x, eye.y, eye.z);
    glUniform1f(palette_, player.scene_.paletteTint ? 1.0f : 0.0f);
    glUniform3f(colorA_, frame.colorA[0], frame.colorA[1], frame.colorA[2]);
    glUniform3f(colorB_, frame.colorB[0], frame.colorB[1], frame.colorB[2]);
    glUniform1f(rainbow_, frame.rainbow ? 1.0f : 0.0f);
    glUniform1f(time_, frame.time);
    glUniform1f(kick_, frame.kick);
    glUniform1i(tex_, 0);
    glActiveTexture(GL_TEXTURE0);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    bool culling = true;
    for (const SceneInstance& instance : evaluateScene(player.scene_, player.animation_, frame.time)) {
        if (instance.mesh < 0 || instance.mesh >= static_cast<int>(player.meshes_.size())) {
            continue;
        }
        const ScenePlayer::GpuMesh& mesh = player.meshes_[static_cast<size_t>(instance.mesh)];
        const Mat4 model = pivot * instance.world;
        const std::array<float, 9> normal = normalMatrix(model);
        glUniformMatrix4fv(model_, 1, GL_FALSE, model.m.data());
        glUniformMatrix3fv(normalMat_, 1, GL_FALSE, normal.data());
        SceneMaterial material;
        if (mesh.material >= 0 && mesh.material < static_cast<int>(player.scene_.materials.size())) {
            material = player.scene_.materials[static_cast<size_t>(mesh.material)];
        }
        glUniform4f(base_, material.baseColor[0], material.baseColor[1], material.baseColor[2], material.baseColor[3]);
        const bool textured = material.baseTexture >= 0 && material.baseTexture < static_cast<int>(player.textures_.size());
        glUniform1i(hasTex_, textured ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, textured ? player.textures_[static_cast<size_t>(material.baseTexture)] : 0);
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
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}

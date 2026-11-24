#pragma once

#include "render/gl_util.hpp"
#include "scene3d/gltf.hpp"
#include "scene3d/scene_model.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <vector>

namespace nvj {

enum class SceneState { Idle, Loading, Ready, Failed };

struct SceneFrame {
    float time = 0.0f;
    double beat = 0.0;
    float scale = 1.0f;
    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float level = 0.0f;
    float kick = 0.0f;
    bool editor = false;
    bool editorCamera = false;
    float editYaw = 0.0f;
    float editPitch = 0.3f;
    float editDistance = 1.0f;
    std::array<float, 3> colorA{0.0f, 0.8f, 1.0f};
    std::array<float, 3> colorB{1.0f, 0.2f, 0.7f};
    bool rainbow = false;
};

class ScenePlayer {
public:
    using Loader = std::function<GltfResult(const GltfLimits&)>;

    ScenePlayer(std::string label, Loader loader);
    ~ScenePlayer();
    ScenePlayer(const ScenePlayer&) = delete;
    ScenePlayer& operator=(const ScenePlayer&) = delete;

    void start();
    SceneState poll();
    bool wait();
    void release();
    void cancel();
    void retry();

    SceneState state() const { return state_; }
    const std::string& label() const { return label_; }
    const std::string& error() const { return error_; }
    const SceneStats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void touch();
    double idleSeconds() const;
    const Scene& scene() const { return scene_; }
    float fitRadius() const { return radius_; }
    double loadMilliseconds() const { return loadMs_; }
    double uploadMilliseconds() const { return uploadMs_; }
    bool thumbnailDone = false;

private:
    friend class SceneRenderer;

    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLuint skinVbo = 0;
        GLuint morphTex = 0;
        GLsizei indexCount = 0;
        GLsizei vertexCount = 0;
        int material = -1;
        int morphTargets = 0;
        bool skinned = false;
        Vec3 centre;
    };

    bool upload();
    void uploadMorph(const SceneMesh& mesh, GpuMesh& gpu);
    void freeGpu();

    std::string label_;
    Loader loader_;
    std::future<GltfResult> pending_;
    SceneState state_ = SceneState::Idle;
    std::atomic<bool> cancel_{false};
    std::string error_;
    std::vector<std::string> warnings_;
    Scene scene_;
    SceneStats stats_;
    std::vector<GpuMesh> meshes_;
    std::vector<GLuint> textures_;
    Vec3 center_;
    float radius_ = 1.0f;
    int animation_ = -1;
    double loadMs_ = 0.0;
    double uploadMs_ = 0.0;
    double lastUse_ = 0.0;
};

class SceneRenderer {
public:
    bool init();
    void shutdown();
    bool ready() const { return classic_.id != 0; }
    bool pbrReady() const { return pbr_.id != 0; }
    size_t depthBytes() const;
    void draw(const ScenePlayer& player, const SceneFrame& frame, const RenderTarget& target);
    size_t skippedSkinned() const { return skippedSkinned_; }
    size_t blendedDraws() const { return blendedDraws_; }

private:
    struct Surface {
        GLuint fbo = 0;
        GLuint depth = 0;
        GLuint color = 0;
        int width = 0;
        int height = 0;
    };

    struct Program {
        GLuint id = 0;
        GLint viewProj = -1;
        GLint model = -1;
        GLint normalMat = -1;
        GLint base = -1;
        GLint hasTex = -1;
        GLint tex = -1;
        GLint camera = -1;
        GLint palette = -1;
        GLint colorA = -1;
        GLint colorB = -1;
        GLint rainbow = -1;
        GLint time = -1;
        GLint kick = -1;
        GLint metal = -1;
        GLint rough = -1;
        GLint emissive = -1;
        GLint boost = -1;
        GLint flags = -1;
        GLint cutoff = -1;
        GLint normalScale = -1;
        GLint texMr = -1;
        GLint texNormal = -1;
        GLint texEmissive = -1;
        GLint follow = -1;
        GLint lightCount = -1;
        GLint lightPos = -1;
        GLint lightColor = -1;
        GLint skyTop = -1;
        GLint skyHorizon = -1;
        GLint skyGround = -1;
        GLint skyIntensity = -1;
        GLint sunDir = -1;
        GLint sunColor = -1;
        GLint exposure = -1;
        GLint blend = -1;
        GLint skinned = -1;
        GLint jointTex = -1;
        GLint jointBase = -1;
        GLint morphCount = -1;
        GLint morphVerts = -1;
        GLint morphTex = -1;
        GLint morphIndex = -1;
        GLint morphWeight = -1;
    };

    bool prepare(Surface& surface, const RenderTarget& target);
    void destroy(Surface& surface);
    void locate(Program& program);

    Program classic_;
    Program pbr_;
    GLuint jointTexture_ = 0;
    size_t skippedSkinned_ = 0;
    size_t blendedDraws_ = 0;
    bool skipLogged_ = false;
    Surface main_;
    Surface thumb_;
    Surface editor_;
};

}

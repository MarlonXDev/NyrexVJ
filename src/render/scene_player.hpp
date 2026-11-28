#pragma once

#include "audio/audio_features.hpp"
#include "render/gl_util.hpp"
#include "scene3d/gltf.hpp"
#include "scene3d/scene_model.hpp"
#include "scene3d/scene_world.hpp"

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
    float beatPulse = 0.0f;
    std::array<float, kSceneBands> bands{};
    bool materialBindings = true;
    bool cameraMoves = true;
    float cameraOrbit = 0.0f;
    float cameraTilt = 0.0f;
    float cameraFov = 0.0f;
    bool editor = false;
    bool editorCamera = false;
    float editYaw = 0.0f;
    float editPitch = 0.3f;
    float editDistance = 1.0f;
    std::array<float, 3> colorA{0.0f, 0.8f, 1.0f};
    std::array<float, 3> colorB{1.0f, 0.2f, 0.7f};
    bool rainbow = false;
    int world = 0;
    bool worldOwner = false;
    WorldPlacement placement;

    void setAudio(const AudioFeatures& features, double beatPosition);
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
    Vec3 fitCenter() const { return center_; }
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
        float radius = 1.0f;
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
    void beginFrame();
    void resetWorlds();
    void setCulling(bool on) { culling_ = on; }
    size_t skippedSkinned() const { return skippedSkinned_; }
    size_t blendedDraws() const { return blendedDraws_; }
    size_t drawnInstances() const { return drawnInstances_; }
    size_t culledInstances() const { return culledInstances_; }
    bool lastDrawShared() const { return lastShared_; }
    size_t worldDepthBytes() const;

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
        GLint hue = -1;
        GLint roughAdd = -1;
        GLint uvShift = -1;
        GLint inflate = -1;
        GLint ripple = -1;
        GLint deformRadius = -1;
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
        GLint split = -1;
        GLint skinned = -1;
        GLint jointTex = -1;
        GLint jointBase = -1;
        GLint morphCount = -1;
        GLint morphVerts = -1;
        GLint morphTex = -1;
        GLint morphIndex = -1;
        GLint morphWeight = -1;
    };

    struct LightSetup {
        std::array<float, 16> positions{};
        std::array<float, 12> colors{};
        int count = 0;
        Vec3 sunDir{0.5f, 0.8f, 0.6f};
        std::array<float, 3> sunColor{1.0f, 0.95f, 0.9f};
        std::array<float, 3> skyTop{};
        std::array<float, 3> skyHorizon{};
        std::array<float, 3> skyGround{};
        float skyIntensity = 1.0f;
        float exposure = 1.0f;
    };

    struct WorldState {
        bool valid = false;
        Mat4 viewProj;
        Vec3 eye;
        Vec3 forward;
        Vec3 ownerCenter;
        float ownerRadius = 1.0f;
        LightSetup lights;
    };

    bool prepare(Surface& surface, const RenderTarget& target);
    void destroy(Surface& surface);
    void locate(Program& program);
    static LightSetup makeLights(const Scene& scene, const SceneAudio& audio);

    Program classic_;
    Program pbr_;
    GLuint jointTexture_ = 0;
    size_t skippedSkinned_ = 0;
    size_t blendedDraws_ = 0;
    bool skipLogged_ = false;
    size_t drawnInstances_ = 0;
    size_t culledInstances_ = 0;
    bool lastShared_ = false;
    bool culling_ = true;
    uint64_t frame_ = 0;
    Surface main_;
    Surface thumb_;
    Surface editor_;
    std::array<Surface, kWorldCount> worlds_{};
    std::array<uint64_t, kWorldCount> worldUsed_{};
    std::array<WorldState, kWorldCount> worldState_{};
};

}

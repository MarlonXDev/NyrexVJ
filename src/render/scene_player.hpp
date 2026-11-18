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
    float scale = 1.0f;
    float bass = 0.0f;
    float level = 0.0f;
    float kick = 0.0f;
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
    double loadMilliseconds() const { return loadMs_; }
    double uploadMilliseconds() const { return uploadMs_; }
    bool thumbnailDone = false;

private:
    friend class SceneRenderer;

    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
        int material = -1;
    };

    bool upload();
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
};

class SceneRenderer {
public:
    bool init();
    void shutdown();
    bool ready() const { return program_ != 0; }
    size_t depthBytes() const { return (static_cast<size_t>(main_.width) * static_cast<size_t>(main_.height) + static_cast<size_t>(thumb_.width) * static_cast<size_t>(thumb_.height)) * 4; }
    void draw(const ScenePlayer& player, const SceneFrame& frame, const RenderTarget& target);

private:
    struct Surface {
        GLuint fbo = 0;
        GLuint depth = 0;
        GLuint color = 0;
        int width = 0;
        int height = 0;
    };

    bool prepare(Surface& surface, const RenderTarget& target);
    void destroy(Surface& surface);

    GLuint program_ = 0;
    GLint viewProj_ = -1;
    GLint model_ = -1;
    GLint normalMat_ = -1;
    GLint base_ = -1;
    GLint hasTex_ = -1;
    GLint tex_ = -1;
    GLint camera_ = -1;
    GLint palette_ = -1;
    GLint colorA_ = -1;
    GLint colorB_ = -1;
    GLint rainbow_ = -1;
    GLint time_ = -1;
    GLint kick_ = -1;
    Surface main_;
    Surface thumb_;
};

}

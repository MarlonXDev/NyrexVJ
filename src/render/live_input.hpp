#pragma once

#include "core/live_source.hpp"
#include "core/settings.hpp"
#include "media/camera_capture.hpp"
#include "render/video_gpu.hpp"

#include <future>
#include <memory>
#include <string>

namespace nvj {

constexpr double kLiveIdleSeconds = 3.0;

class PluginHost;

enum class LiveKind { Ndi, Camera, Plugin };

struct LiveInputConfig {
    std::string source;
    std::string excluded;
    CameraMode camera;
    ReconnectPolicy policy;
    int maxDimension = 1920;
    PluginHost* plugins = nullptr;
};

struct LiveInputInfo {
    LiveStatus status;
    std::string startError;
    double latencyMs = 0.0;
    double latencyMaxMs = 0.0;
    double uploadMs = 0.0;
    unsigned long long uploaded = 0;
    UploadRoute route = UploadRoute::None;
    VideoMemory memory;
    bool active = false;
};

class LiveInput {
public:
    LiveInput(VideoGpu& gpu, LiveKind kind);
    ~LiveInput();
    LiveInput(const LiveInput&) = delete;
    LiveInput& operator=(const LiveInput&) = delete;

    void update(const LiveInputConfig& config, double now);
    void release();
    void releaseAsync();
    bool active() const { return source_ != nullptr; }
    bool hasPicture() const { return uploader_.hasPicture(); }
    GLuint texture() const { return uploader_.texture(); }
    int width() const { return uploader_.width(); }
    int height() const { return uploader_.height(); }
    float scaleX() const { return uploader_.scaleX(); }
    float scaleY() const { return uploader_.scaleY(); }
    bool premultiplied() const { return false; }
    LiveKind kind() const { return kind_; }
    LiveInputInfo info() const;
    void resetLatencyWindow();

private:
    static bool sameConfig(const LiveInputConfig& a, const LiveInputConfig& b, LiveKind kind);
    bool present(const LiveFrame& frame, int maxDimension);
    void retire();

    VideoGpu& gpu_;
    LiveKind kind_;
    VideoUploader uploader_;
    std::unique_ptr<LiveSource> source_;
    std::future<void> reaper_;
    LiveInputConfig running_;
    LiveFrame scratch_;
    std::string startError_;
    double retryAt_ = -1.0e9;
    double latencyMs_ = 0.0;
    double latencyMaxMs_ = 0.0;
    double uploadMs_ = 0.0;
    unsigned long long uploaded_ = 0;
    LiveStatus lastStatus_;
};

}

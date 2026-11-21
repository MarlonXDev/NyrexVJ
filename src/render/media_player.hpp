#pragma once

#include "core/playhead.hpp"
#include "media/playback_driver.hpp"
#include "media/video_redirect.hpp"
#include "media/video_decoder.hpp"
#include "render/gl_util.hpp"
#include "render/video_gpu.hpp"
#include "scene3d/scene_model.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>

namespace nvj {

enum class MediaState { Idle, Loading, Ready, Failed };

class MediaPlayer {
public:
    MediaPlayer(std::string label, std::string path, bool video, VideoGpu* gpu = nullptr, const VideoOptions& options = VideoOptions());
    ~MediaPlayer();
    MediaPlayer(const MediaPlayer&) = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    MediaState poll();
    bool update(double clock);
    bool update(double clock, double beat);
    bool prepare(const ClipPlayback& playback);
    void setPlayback(const ClipPlayback& playback);
    void scrubTo(double position);
    void stepFrames(int frames);
    double position() const;
    double framePeriod() const;
    bool controllable() const { return video_ && state_ == MediaState::Ready && info_.duration > 0.0; }
    const Playhead& playhead() const { return driver_.head(); }
    void touch();
    double idleSeconds() const;
    void release();
    void retry();

    MediaState state() const { return state_; }
    bool ready() const { return state_ == MediaState::Ready && hasPicture(); }
    bool isVideo() const { return video_; }
    const std::string& label() const { return label_; }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }
    GLuint texture() const { return uploader_ ? uploader_->texture() : texture_; }
    int width() const;
    int height() const;
    float scaleX() const { return uploader_ ? uploader_->scaleX() : 1.0f; }
    float scaleY() const { return uploader_ ? uploader_->scaleY() : 1.0f; }
    bool ycocg() const { return uploader_ && uploader_->ycocg(); }
    bool premultiplied() const { return !video_; }
    const VideoInfo& videoInfo() const { return info_; }
    unsigned long long gpuBytes() const;
    VideoMemory videoMemory() const;
    VideoStatus status() const;
    UploadStats uploadStats() const;
    UploadRoute uploadRoute() const;
    bool safeUpload() const { return safeUpload_; }
    void setVideoOptions(const VideoOptions& options);
    const VideoOptions& videoOptions() const { return options_; }
    double lastClock() const { return clock_; }
    bool convertedActive() const { return convertedActive_; }
    bool convertedRetired() const { return convertedRetired_; }
    const std::string& openedPath() const { return openPath_; }

    bool thumbnailDone = false;

private:
    struct ImageLoad {
        SceneImage image;
        bool ok = false;
        std::string error;
    };
    struct VideoLoad {
        std::unique_ptr<VideoDecoder> decoder;
        bool ok = false;
        std::string error;
    };

    bool hasPicture() const;
    bool present(double seconds);
    void start();
    VideoOptions effectiveOptions() const;
    void serviceUpgrade();
    void retireConverted(const std::string& reason);
    void destroyTexture();
    void createTexture(int width, int height, const uint8_t* pixels, bool mipmaps);

    std::string label_;
    std::string path_;
    bool video_ = false;
    MediaState state_ = MediaState::Idle;
    std::string error_;
    GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool mipmaps_ = false;
    bool hasPicture_ = false;
    bool safeUpload_ = false;
    bool hardwareRetired_ = false;
    std::string hardwareFailure_;
    VideoGpu* gpu_ = nullptr;
    VideoOptions options_;
    std::unique_ptr<VideoUploader> uploader_;
    double clock_ = 0.0;
    double lastBeat_ = 0.0;
    PlaybackDriver driver_;
    unsigned long long lastFrame_ = 0;
    std::future<ImageLoad> imageLoad_;
    std::future<VideoLoad> videoLoad_;
    std::unique_ptr<VideoDecoder> decoder_;
    VideoInfo info_;
    std::string openPath_;
    bool convertedActive_ = false;
    bool convertedRetired_ = false;
    std::uint64_t redirectGeneration_ = 0;
    std::string upgradePath_;
    std::future<VideoLoad> upgradeLoad_;
    std::chrono::steady_clock::time_point lastUse_ = std::chrono::steady_clock::now();
};

}

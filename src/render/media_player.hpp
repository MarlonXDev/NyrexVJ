#pragma once

#include "media/video_decoder.hpp"
#include "render/gl_util.hpp"
#include "scene3d/scene_model.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <string>

namespace nvj {

enum class MediaState { Idle, Loading, Ready, Failed };

class MediaPlayer {
public:
    MediaPlayer(std::string label, std::string path, bool video);
    ~MediaPlayer();
    MediaPlayer(const MediaPlayer&) = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    MediaState poll();
    bool update(double clock);
    void touch();
    double idleSeconds() const;
    void release();
    void retry();

    MediaState state() const { return state_; }
    bool ready() const { return state_ == MediaState::Ready && hasPicture_; }
    bool isVideo() const { return video_; }
    const std::string& label() const { return label_; }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }
    GLuint texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool premultiplied() const { return !video_; }
    const VideoInfo& videoInfo() const { return info_; }
    unsigned long long gpuBytes() const;
    double lastClock() const { return clock_; }

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

    void start();
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
    double clock_ = 0.0;
    unsigned long long lastFrame_ = 0;
    std::future<ImageLoad> imageLoad_;
    std::future<VideoLoad> videoLoad_;
    std::unique_ptr<VideoDecoder> decoder_;
    VideoInfo info_;
    std::chrono::steady_clock::time_point lastUse_ = std::chrono::steady_clock::now();
};

}

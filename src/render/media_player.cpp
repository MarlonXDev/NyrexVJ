#include "render/media_player.hpp"

#include "core/log.hpp"
#include "media/media_image.hpp"

#include <algorithm>
#include <filesystem>

namespace nvj {

MediaPlayer::MediaPlayer(std::string label, std::string path, bool video) : label_(std::move(label)), path_(std::move(path)), video_(video) {}

MediaPlayer::~MediaPlayer() {
    if (imageLoad_.valid()) {
        imageLoad_.wait();
    }
    if (videoLoad_.valid()) {
        videoLoad_.wait();
    }
    destroyTexture();
}

void MediaPlayer::destroyTexture() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    width_ = 0;
    height_ = 0;
    hasPicture_ = false;
}

void MediaPlayer::createTexture(int width, int height, const uint8_t* pixels, bool mipmaps) {
    destroyTexture();
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = width;
    height_ = height;
    mipmaps_ = mipmaps;
}

void MediaPlayer::start() {
    state_ = MediaState::Loading;
    error_.clear();
    const std::string path = path_;
    if (video_) {
        videoLoad_ = std::async(std::launch::async, [path] {
            VideoLoad load;
            load.decoder = std::make_unique<VideoDecoder>();
            load.ok = load.decoder->open(path, load.error);
            return load;
        });
    } else {
        imageLoad_ = std::async(std::launch::async, [path] {
            ImageLoad load;
            load.ok = loadMediaImage(std::filesystem::path(std::u8string(path.begin(), path.end())), load.image, load.error);
            return load;
        });
    }
}

MediaState MediaPlayer::poll() {
    if (state_ == MediaState::Idle) {
        start();
    }
    if (state_ != MediaState::Loading) {
        return state_;
    }
    if (video_) {
        if (!videoLoad_.valid() || videoLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return state_;
        }
        VideoLoad load = videoLoad_.get();
        if (!load.ok) {
            error_ = load.error;
            state_ = MediaState::Failed;
            logLine("Video clip '" + label_ + "' failed: " + error_);
            return state_;
        }
        decoder_ = std::move(load.decoder);
        info_ = decoder_->info();
        createTexture(info_.decodedWidth, info_.decodedHeight, nullptr, false);
        lastFrame_ = 0;
        state_ = MediaState::Ready;
        return state_;
    }
    if (!imageLoad_.valid() || imageLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return state_;
    }
    ImageLoad load = imageLoad_.get();
    if (!load.ok) {
        error_ = load.error;
        state_ = MediaState::Failed;
        logLine("Image clip '" + label_ + "' failed: " + error_);
        return state_;
    }
    createTexture(load.image.width, load.image.height, load.image.rgba.data(), true);
    hasPicture_ = true;
    state_ = MediaState::Ready;
    return state_;
}

bool MediaPlayer::update(double clock) {
    clock_ = clock;
    if (state_ != MediaState::Ready) {
        return false;
    }
    if (!video_) {
        return true;
    }
    if (decoder_->failed()) {
        error_ = decoder_->failure();
        state_ = MediaState::Failed;
        logLine("Video clip '" + label_ + "' stopped: " + error_);
        return false;
    }
    const std::shared_ptr<const VideoFrame> frame = decoder_->frameAt(clock);
    if (frame && frame->id != lastFrame_ && frame->width == width_ && frame->height == height_) {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE, frame->rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        lastFrame_ = frame->id;
        hasPicture_ = true;
    }
    return hasPicture_;
}

void MediaPlayer::touch() {
    lastUse_ = std::chrono::steady_clock::now();
}

double MediaPlayer::idleSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - lastUse_).count();
}

void MediaPlayer::release() {
    if (imageLoad_.valid()) {
        imageLoad_.wait();
        imageLoad_ = std::future<ImageLoad>();
    }
    if (videoLoad_.valid()) {
        videoLoad_.wait();
        videoLoad_ = std::future<VideoLoad>();
    }
    decoder_.reset();
    destroyTexture();
    state_ = MediaState::Idle;
    lastFrame_ = 0;
}

void MediaPlayer::retry() {
    release();
    thumbnailDone = false;
    error_.clear();
}

unsigned long long MediaPlayer::gpuBytes() const {
    if (texture_ == 0) {
        return 0;
    }
    const unsigned long long base = static_cast<unsigned long long>(width_) * static_cast<unsigned long long>(height_) * 4ull;
    return mipmaps_ ? base + base / 3ull : base;
}

}

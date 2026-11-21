#include "render/media_player.hpp"

#include "core/log.hpp"
#include "media/media_image.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace nvj {

MediaPlayer::MediaPlayer(std::string label, std::string path, bool video, VideoGpu* gpu, const VideoOptions& options) : label_(std::move(label)), path_(std::move(path)), video_(video), gpu_(gpu), options_(options) {
    if (video_ && gpu_ != nullptr) {
        uploader_ = std::make_unique<VideoUploader>(*gpu_);
    }
}

MediaPlayer::~MediaPlayer() {
    if (imageLoad_.valid()) {
        imageLoad_.wait();
    }
    if (videoLoad_.valid()) {
        videoLoad_.wait();
    }
    if (upgradeLoad_.valid()) {
        upgradeLoad_.wait();
    }
    decoder_.reset();
    uploader_.reset();
    destroyTexture();
}

bool MediaPlayer::hasPicture() const {
    return uploader_ ? uploader_->hasPicture() : hasPicture_;
}

int MediaPlayer::width() const {
    if (uploader_) {
        return uploader_->hasPicture() ? uploader_->width() : info_.decodedWidth;
    }
    return width_;
}

int MediaPlayer::height() const {
    if (uploader_) {
        return uploader_->hasPicture() ? uploader_->height() : info_.decodedHeight;
    }
    return height_;
}

void MediaPlayer::setVideoOptions(const VideoOptions& options) {
    options_ = options;
    hardwareRetired_ = false;
    hardwareFailure_.clear();
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

VideoOptions MediaPlayer::effectiveOptions() const {
    VideoOptions options = options_;
    if (hardwareRetired_) {
        options.mode = VideoDecodeMode::ForceSoftware;
        options.hardwareFailure = hardwareFailure_;
    }
    if (safeUpload_ || gpu_ == nullptr || !gpu_->caps().conversion) {
        options.gpuConversion = false;
    }
    if (safeUpload_ || gpu_ == nullptr || !gpu_->caps().compressedTextures) {
        options.hapDirect = false;
        options.compressedTextures = false;
    }
    return options;
}

void MediaPlayer::retireConverted(const std::string& reason) {
    if (convertedRetired_) {
        return;
    }
    convertedRetired_ = true;
    logLine("Video clip '" + label_ + "': the converted HAP copy is not used (" + reason + "), the original plays instead");
    redirect::markBad(path_, reason);
}

void MediaPlayer::start() {
    state_ = MediaState::Loading;
    error_.clear();
    std::string path = path_;
    openPath_ = path_;
    convertedActive_ = false;
    if (video_ && !convertedRetired_ && redirect::installed()) {
        const std::string converted = redirect::lookup(path_);
        if (!converted.empty()) {
            path = converted;
            openPath_ = converted;
            convertedActive_ = true;
        }
    }
    redirectGeneration_ = redirect::generation();
    if (video_) {
        if (!uploader_) {
            error_ = "Video playback is not available because the video renderer is missing";
            state_ = MediaState::Failed;
            return;
        }
        const VideoOptions options = effectiveOptions();
        videoLoad_ = std::async(std::launch::async, [path, options] {
            VideoLoad load;
            load.decoder = std::make_unique<VideoDecoder>();
            load.ok = load.decoder->open(path, load.error, options);
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
        if (!load.ok && convertedActive_) {
            retireConverted("it cannot be opened: " + load.error);
            start();
            return state_;
        }
        if (!load.ok) {
            error_ = load.error;
            state_ = MediaState::Failed;
            logLine("Video clip '" + label_ + "' failed: " + error_);
            return state_;
        }
        decoder_ = std::move(load.decoder);
        info_ = decoder_->info();
        lastFrame_ = 0;
        driver_.reset();
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
    return update(clock, lastBeat_);
}

double MediaPlayer::framePeriod() const {
    return PlaybackDriver::periodOf(info_);
}

double MediaPlayer::position() const {
    return driver_.position();
}

void MediaPlayer::setPlayback(const ClipPlayback& playback) {
    driver_.setPlayback(playback);
}

void MediaPlayer::scrubTo(double position) {
    driver_.scrubTo(position);
}

void MediaPlayer::stepFrames(int frames) {
    driver_.stepFrames(frames);
}

bool MediaPlayer::present(double seconds) {
    const std::shared_ptr<const VideoFrame> frame = decoder_->frameAt(seconds);
    if (frame && frame->id != lastFrame_) {
        if (uploader_->present(*frame)) {
            lastFrame_ = frame->id;
        } else if (!safeUpload_) {
            logLine("Video clip '" + label_ + "': the GPU upload failed, switching to the safe CPU path");
            safeUpload_ = true;
            uploader_->release();
            decoder_.reset();
            driver_.reset();
            state_ = MediaState::Idle;
            lastFrame_ = 0;
            return false;
        } else {
            error_ = "The picture could not be uploaded to the graphics card";
            state_ = MediaState::Failed;
            logLine("Video clip '" + label_ + "' stopped: " + error_);
            return false;
        }
    }
    return uploader_->hasPicture();
}

bool MediaPlayer::prepare(const ClipPlayback& playback) {
    if (state_ != MediaState::Ready || !video_ || !decoder_ || decoder_->failed() || info_.duration <= 0.0) {
        return false;
    }
    if (!(driver_.playback() == clampedPlayback(playback))) {
        driver_.setPlayback(playback);
        driver_.reset();
    }
    double seconds = 0.0;
    return driver_.prepare(*decoder_, info_, seconds) && present(seconds);
}

bool MediaPlayer::update(double clock, double beat) {
    clock_ = clock;
    lastBeat_ = beat;
    if (state_ != MediaState::Ready) {
        return false;
    }
    if (!video_) {
        return true;
    }
    if (decoder_->failed()) {
        if (convertedActive_) {
            retireConverted(decoder_->failure());
            uploader_->release();
            decoder_.reset();
            driver_.reset();
            state_ = MediaState::Idle;
            lastFrame_ = 0;
            return false;
        }
        if (!hardwareRetired_ && decoder_->status().path == DecodePath::Hardware) {
            hardwareRetired_ = true;
            hardwareFailure_ = decoder_->failure();
            logLine("Video clip '" + label_ + "': the hardware decoder stopped (" + hardwareFailure_ + "), switching to the CPU decoder");
            uploader_->release();
            decoder_.reset();
            driver_.reset();
            state_ = MediaState::Idle;
            lastFrame_ = 0;
            return false;
        }
        error_ = decoder_->failure();
        state_ = MediaState::Failed;
        logLine("Video clip '" + label_ + "' stopped: " + error_);
        return false;
    }
    serviceUpgrade();
    double seconds = clock;
    if (!driver_.drive(*decoder_, info_, clock, beat, seconds)) {
        return false;
    }
    return present(seconds);
}

void MediaPlayer::serviceUpgrade() {
    if (upgradeLoad_.valid()) {
        if (upgradeLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        VideoLoad load = upgradeLoad_.get();
        const std::string path = upgradePath_;
        upgradePath_.clear();
        if (!load.ok) {
            retireConverted("it cannot be opened: " + load.error);
            return;
        }
        const VideoInfo next = load.decoder->info();
        const double tolerance = std::max(0.1, framePeriod() * 2.0);
        if (next.width != info_.width || next.height != info_.height || std::abs(next.duration - info_.duration) > tolerance) {
            retireConverted("its size or length differs from the original");
            return;
        }
        decoder_ = std::move(load.decoder);
        info_ = next;
        lastFrame_ = 0;
        openPath_ = path;
        convertedActive_ = true;
        logLine("Video clip '" + label_ + "': switched to the converted HAP copy");
        return;
    }
    if (convertedRetired_ || !redirect::installed() || !uploader_) {
        return;
    }
    const std::uint64_t generation = redirect::generation();
    if (generation == redirectGeneration_) {
        return;
    }
    redirectGeneration_ = generation;
    const std::string converted = redirect::lookup(path_);
    if (converted.empty() || converted == openPath_) {
        return;
    }
    upgradePath_ = converted;
    const VideoOptions options = effectiveOptions();
    upgradeLoad_ = std::async(std::launch::async, [converted, options] {
        VideoLoad load;
        load.decoder = std::make_unique<VideoDecoder>();
        load.ok = load.decoder->open(converted, load.error, options);
        return load;
    });
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
    if (upgradeLoad_.valid()) {
        upgradeLoad_.wait();
        upgradeLoad_ = std::future<VideoLoad>();
        upgradePath_.clear();
    }
    decoder_.reset();
    if (uploader_) {
        uploader_->release();
    }
    destroyTexture();
    state_ = MediaState::Idle;
    lastFrame_ = 0;
    driver_.reset();
}

void MediaPlayer::retry() {
    release();
    thumbnailDone = false;
    safeUpload_ = false;
    convertedRetired_ = false;
    error_.clear();
}

VideoMemory MediaPlayer::videoMemory() const {
    return uploader_ ? uploader_->memory() : VideoMemory{};
}

VideoStatus MediaPlayer::status() const {
    VideoStatus status;
    if (decoder_) {
        status = decoder_->status();
    }
    if (safeUpload_ && status.fallbackReason.empty()) {
        status.fallbackReason = "GPU upload failed, using the CPU path";
    }
    status.convertedCopy = convertedActive_;
    return status;
}

UploadStats MediaPlayer::uploadStats() const {
    return uploader_ ? uploader_->stats() : UploadStats{};
}

UploadRoute MediaPlayer::uploadRoute() const {
    return uploader_ ? uploader_->route() : UploadRoute::None;
}

unsigned long long MediaPlayer::gpuBytes() const {
    if (uploader_) {
        return uploader_->memory().total();
    }
    if (texture_ == 0) {
        return 0;
    }
    const unsigned long long base = static_cast<unsigned long long>(width_) * static_cast<unsigned long long>(height_) * 4ull;
    return mipmaps_ ? base + base / 3ull : base;
}

}

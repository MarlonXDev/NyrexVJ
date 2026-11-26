#include "render/live_input.hpp"

#include "core/ndi_receiver.hpp"
#include "core/plugin_host.hpp"
#include "media/stream_capture.hpp"

#include <algorithm>

namespace nvj {

LiveInput::LiveInput(VideoGpu& gpu, LiveKind kind) : gpu_(gpu), kind_(kind), uploader_(gpu) {}

LiveInput::~LiveInput() {
    release();
    if (reaper_.valid()) {
        reaper_.wait();
    }
}

bool LiveInput::sameConfig(const LiveInputConfig& a, const LiveInputConfig& b, LiveKind kind) {
    if (a.source != b.source) {
        return false;
    }
    if (kind == LiveKind::Ndi) {
        return a.excluded == b.excluded;
    }
    if (kind == LiveKind::Plugin) {
        return true;
    }
    if (kind == LiveKind::Stream) {
        return a.streamProfile == b.streamProfile;
    }
    return a.camera.width == b.camera.width && a.camera.height == b.camera.height && a.camera.fps == b.camera.fps;
}

void LiveInput::retire() {
    if (!source_) {
        return;
    }
    if (reaper_.valid()) {
        reaper_.wait();
    }
    std::unique_ptr<LiveSource> doomed = std::move(source_);
    reaper_ = std::async(std::launch::async, [doomed = std::move(doomed)]() mutable {
        doomed->stop();
        doomed.reset();
    });
}

void LiveInput::releaseAsync() {
    retire();
    uploader_.release();
    lastStatus_ = LiveStatus{};
    latencyMs_ = 0.0;
    latencyMaxMs_ = 0.0;
    uploadMs_ = 0.0;
}

void LiveInput::release() {
    if (source_) {
        source_->stop();
        source_.reset();
    }
    uploader_.release();
    lastStatus_ = LiveStatus{};
    latencyMs_ = 0.0;
    latencyMaxMs_ = 0.0;
    uploadMs_ = 0.0;
}

void LiveInput::resetLatencyWindow() {
    latencyMaxMs_ = 0.0;
}

void LiveInput::update(const LiveInputConfig& config, double now) {
    if (source_ && !sameConfig(config, running_, kind_)) {
        retire();
        uploader_.release();
        retryAt_ = -1.0e9;
    }
    if (!source_) {
        if (now < retryAt_) {
            return;
        }
        if (kind_ == LiveKind::Ndi) {
            std::unique_ptr<NdiReceiver> receiver = std::make_unique<NdiReceiver>();
            if (!receiver->start(config.source, config.excluded, config.policy)) {
                startError_ = receiver->error();
                retryAt_ = now + 3.0;
                return;
            }
            source_ = std::move(receiver);
        } else if (kind_ == LiveKind::Plugin) {
            std::string error;
            std::unique_ptr<LiveSource> plugin;
            if (config.plugins != nullptr) {
                plugin = config.plugins->openFrameSource(config.source, error);
            } else {
                error = "plugins are not available";
            }
            if (!plugin) {
                startError_ = error;
                retryAt_ = now + 3.0;
                return;
            }
            source_ = std::move(plugin);
        } else if (kind_ == LiveKind::Stream) {
            std::unique_ptr<StreamCapture> capture = std::make_unique<StreamCapture>();
            if (!capture->start(config.source, config.streamProfile, config.policy)) {
                startError_ = capture->error();
                retryAt_ = now + 3.0;
                return;
            }
            source_ = std::move(capture);
        } else {
            std::unique_ptr<CameraCapture> camera = std::make_unique<CameraCapture>();
            if (!camera->start(config.source, config.camera, config.policy)) {
                startError_ = camera->error();
                retryAt_ = now + 3.0;
                return;
            }
            source_ = std::move(camera);
        }
        startError_.clear();
        running_ = config;
    }
    lastStatus_ = source_->status();
    if (!source_->take(scratch_)) {
        return;
    }
    const double begin = steadySeconds();
    if (!present(scratch_, config.maxDimension)) {
        return;
    }
    const double end = steadySeconds();
    const double latency = std::max(0.0, end - scratch_.capturedAt) * 1000.0;
    const double upload = (end - begin) * 1000.0;
    latencyMs_ = uploaded_ == 0 ? latency : latencyMs_ * 0.9 + latency * 0.1;
    uploadMs_ = uploaded_ == 0 ? upload : uploadMs_ * 0.9 + upload * 0.1;
    latencyMaxMs_ = std::max(latencyMaxMs_, latency);
    ++uploaded_;
}

bool LiveInput::present(const LiveFrame& frame, int maxDimension) {
    if (frame.width <= 0 || frame.height <= 0 || frame.data.empty()) {
        return false;
    }
    VideoFrame video;
    video.width = frame.width;
    video.height = frame.height;
    video.id = frame.id;
    video.bitDepth = 8;
    video.fullRange = false;
    video.planeCount = 1;
    VideoPlane& plane = video.planes[0];
    plane.data = frame.data.data();
    plane.height = frame.height;
    plane.stride = frame.stride;
    plane.bytes = 1;
    if (frame.format == LiveFormat::Uyvy) {
        video.layout = VideoLayout::PackedUyvy;
        video.matrix = frame.height >= 720 ? 1 : 0;
        plane.width = frame.width / 2;
        plane.channels = 4;
        const int longest = std::max(frame.width, frame.height);
        const int cap = std::max(maxDimension, 64);
        if (longest > cap) {
            const double scale = static_cast<double>(cap) / static_cast<double>(longest);
            video.outWidth = std::max(2, static_cast<int>(frame.width * scale) & ~1);
            video.outHeight = std::max(2, static_cast<int>(frame.height * scale));
        } else {
            video.outWidth = frame.width;
            video.outHeight = frame.height;
        }
    } else if (frame.format == LiveFormat::Rgba) {
        video.layout = VideoLayout::Rgba;
        video.hasAlpha = true;
        video.outWidth = frame.width;
        video.outHeight = frame.height;
        plane.width = frame.width;
        plane.channels = 4;
    } else {
        return false;
    }
    return uploader_.present(video);
}

LiveInputInfo LiveInput::info() const {
    LiveInputInfo info;
    info.active = source_ != nullptr;
    info.status = source_ ? source_->status() : lastStatus_;
    info.startError = startError_;
    info.latencyMs = latencyMs_;
    info.latencyMaxMs = latencyMaxMs_;
    info.uploadMs = uploadMs_;
    info.uploaded = uploaded_;
    info.route = uploader_.route();
    info.memory = uploader_.memory();
    return info;
}

}

#include "render/person_session.hpp"

#include <algorithm>
#include <chrono>

namespace nvj {

namespace {

double elapsedMs(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

}

PersonSession::PersonSession() = default;

PersonSession::~PersonSession() {
    releaseGl();
}

void PersonSession::configure(const person::Settings& settings) {
    const person::Settings clean = person::sanitised(settings);
    if (configured_ && clean == settings_) {
        return;
    }
    settings_ = clean;
    configured_ = true;
    worker_.configure(clean);
}

void PersonSession::offer(const LiveFrame& frame) {
    ++offered_;
    const double now = steadySeconds();
    lastFrameAt_ = now;
    sourceSeen_ = true;
    if (frame.height > 0) {
        sourceAspect_ = static_cast<float>(frame.width) / static_cast<float>(frame.height);
    }
    const double interval = 1.0 / static_cast<double>(std::max(worker_.rate(), 1));
    if (now - lastAccepted_ < interval) {
        return;
    }
    const auto begin = std::chrono::steady_clock::now();
    if (!person::reduce(frame, worker_.tier(), scratch_)) {
        ++rejected_;
        return;
    }
    scratch_.time = now;
    const double spent = elapsedMs(begin);
    const double before = reduceMs_.load();
    reduceMs_ = before == 0.0 ? spent : before * 0.9 + spent * 0.1;
    lastAccepted_ = now;
    ++accepted_;
    worker_.submit(std::move(scratch_));
    scratch_ = person::Picture();
}

void PersonSession::update(bool sourceLive) {
    const double wall = steadySeconds();
    const double dt = lastUpdate_ < 0.0 ? 0.0 : std::clamp(wall - lastUpdate_, 0.0, 1.0);
    lastUpdate_ = wall;
    const bool quiet = state_ == person::State::SourceLost && result_ && result_->foregroundPercent == 0 && result_->tracks.empty() && signals_.presence < 0.005f && signals_.motion < 0.005f;
    if (sourceSeen_ && !quiet && (!sourceLive || wall - lastFrameAt_ > person::kLostAfterSeconds)) {
        worker_.lost(dt);
    }
    std::shared_ptr<const person::Result> latest = worker_.latest();
    if (latest && latest->generation != uploadedGeneration_) {
        uploadedGeneration_ = latest->generation;
        result_ = latest;
        state_ = latest->state;
        signals_ = latest->signals;
        resultAt_ = wall;
        if (latest->state != person::State::Failed && !latest->mask.coverage.empty()) {
            upload(*latest);
        }
    }
}

void PersonSession::upload(const person::Result& result) {
    const int width = result.mask.width;
    const int height = result.mask.height;
    if (width <= 0 || height <= 0 || result.mask.coverage.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return;
    }
    if (texture_ == 0) {
        glGenTextures(1, &texture_);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != maskWidth_ || height != maskHeight_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, result.mask.coverage.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        maskWidth_ = width;
        maskHeight_ = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, result.mask.coverage.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    ++uploads_;
}

void PersonSession::learn() {
    worker_.learn();
}

void PersonSession::releaseGl() {
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    maskWidth_ = 0;
    maskHeight_ = 0;
}

PersonSessionInfo PersonSession::info() const {
    PersonSessionInfo info;
    info.state = state_;
    info.worker = worker_.stats();
    info.tier = worker_.tier();
    info.rate = worker_.rate();
    info.maskWidth = maskWidth_;
    info.maskHeight = maskHeight_;
    info.persons = signals_.count;
    info.gpuBytes = gpuBytes();
    info.offered = offered_;
    info.accepted = accepted_;
    info.rejected = rejected_;
    info.uploads = uploads_;
    info.reduceMs = reduceMs_;
    info.analysisMs = info.worker.lastMs;
    info.ageMs = resultAt_ > 0.0 ? std::max(0.0, (steadySeconds() - resultAt_) * 1000.0) : 0.0;
    info.foregroundPercent = result_ ? result_->foregroundPercent : 0;
    info.note = result_ ? result_->note : std::string();
    info.sourceSeen = sourceSeen_;
    return info;
}

}

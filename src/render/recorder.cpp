#include "render/recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace nvj {

double steadyNowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Recorder::Recorder() : schedule_(60), clock_(&steadyNowSeconds) {}

Recorder::~Recorder() {
    shutdown(10.0);
}

void Recorder::setNow(double (*clock)()) {
    clock_ = clock != nullptr ? clock : &steadyNowSeconds;
}

void Recorder::setTestHooks(const RecorderTestHooks& hooks) {
    hooks_ = hooks;
}

bool Recorder::active() const {
    if (!video_) {
        return false;
    }
    const RecordState state = video_->stats().state;
    return state == RecordState::Recording || state == RecordState::Finishing;
}

bool Recorder::recording() const {
    return video_ && video_->stats().state == RecordState::Recording;
}

bool Recorder::start(const RecordSettings& requested, int sourceWidth, int sourceHeight, int audioRate, const std::string& pathUtf8, std::string& error, std::string& detail) {
    if (active() || !reported_) {
        error = "A recording is already running";
        return false;
    }
    const RecordCheck check = checkRecordSettings(requested, sourceWidth, sourceHeight);
    if (!check.valid) {
        error = check.problem;
        return false;
    }
    video_ = std::make_unique<VideoRecorder>();
    video_->setTestHooks(hooks_);
    const int rate = check.settings.audio && audioRate > 0 ? audioRate : 0;
    if (!video_->open(check.settings, pathUtf8, rate, error, detail)) {
        video_.reset();
        return false;
    }
    settings_ = check.settings;
    path_ = pathUtf8;
    schedule_ = FrameSchedule(settings_.fps);
    oldest_ = 0;
    next_ = 0;
    busy_ = 0;
    droppedPool_ = 0;
    droppedReadback_ = 0;
    sizeChanges_ = 0;
    captured_ = 0;
    captureMs_ = 0.0;
    captureMsMax_ = 0.0;
    lastSourceWidth_ = sourceWidth;
    lastSourceHeight_ = sourceHeight;
    firstFrame_ = true;
    origin_ = 0.0;
    reported_ = false;
    return true;
}

bool Recorder::ensureGl() {
    if (glReady_) {
        return true;
    }
    const size_t bytes = recordFrameBytes(settings_);
    GLint previousRead = 0;
    GLint previousDraw = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    const bool created = target_.create(settings_.width, settings_.height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    if (!created) {
        return false;
    }
    glGenFramebuffers(1, &readFramebuffer_);
    GLint previousPack = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPack);
    for (Slot& slot : slots_) {
        glGenBuffers(1, &slot.buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
        slot.fence = nullptr;
        slot.frame = nullptr;
        slot.busy = false;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousPack));
    glReady_ = true;
    return true;
}

void Recorder::releaseGl() {
    if (!glReady_) {
        return;
    }
    abandonSlots();
    for (Slot& slot : slots_) {
        if (slot.buffer != 0) {
            glDeleteBuffers(1, &slot.buffer);
            slot.buffer = 0;
        }
    }
    if (readFramebuffer_ != 0) {
        glDeleteFramebuffers(1, &readFramebuffer_);
        readFramebuffer_ = 0;
    }
    target_.destroy();
    glReady_ = false;
}

void Recorder::abandonSlots() {
    for (Slot& slot : slots_) {
        if (slot.busy) {
            if (slot.fence != nullptr) {
                glDeleteSync(slot.fence);
                slot.fence = nullptr;
            }
            if (slot.frame != nullptr && video_) {
                video_->discardFrame(slot.frame);
            }
            slot.frame = nullptr;
            slot.busy = false;
        }
    }
    busy_ = 0;
    oldest_ = 0;
    next_ = 0;
}

void Recorder::harvest(bool wait) {
    if (!glReady_ || !video_) {
        return;
    }
    const size_t bytes = recordFrameBytes(settings_);
    GLint previousPack = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPack);
    while (busy_ > 0) {
        Slot& slot = slots_[oldest_];
        const GLuint64 timeout = wait ? 200000000ull : 0ull;
        const GLenum result = glClientWaitSync(slot.fence, 0, timeout);
        if (result == GL_TIMEOUT_EXPIRED) {
            break;
        }
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        bool copied = false;
        if (result != GL_WAIT_FAILED) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
            const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT);
            if (mapped != nullptr) {
                std::memcpy(slot.frame, mapped, bytes);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                copied = true;
            }
        }
        if (copied) {
            video_->submitFrame(slot.frame, slot.index);
        } else {
            video_->discardFrame(slot.frame);
            ++droppedReadback_;
        }
        slot.frame = nullptr;
        slot.busy = false;
        oldest_ = (oldest_ + 1) % kSlots;
        --busy_;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousPack));
}

void Recorder::capture(GLuint texture, int sourceWidth, int sourceHeight, double nowSeconds) {
    if (!video_ || texture == 0 || sourceWidth <= 0 || sourceHeight <= 0) {
        return;
    }
    if (video_->stats().state != RecordState::Recording) {
        if (glReady_ && busy_ > 0) {
            harvest(false);
        }
        return;
    }
    const auto began = std::chrono::steady_clock::now();
    if (!ensureGl()) {
        video_->cancel();
        return;
    }
    harvest(false);
    if (!schedule_.wouldAccept(nowSeconds)) {
        schedule_.consider(nowSeconds);
        return;
    }
    if (busy_ >= kSlots) {
        ++droppedReadback_;
        return;
    }
    uint8_t* frame = video_->acquireFrame();
    if (frame == nullptr) {
        ++droppedPool_;
        return;
    }
    const FrameDecision decision = schedule_.consider(nowSeconds);
    if (!decision.accept) {
        video_->discardFrame(frame);
        return;
    }
    if (firstFrame_) {
        firstFrame_ = false;
        origin_ = nowSeconds;
    }
    if (sourceWidth != lastSourceWidth_ || sourceHeight != lastSourceHeight_) {
        ++sizeChanges_;
        lastSourceWidth_ = sourceWidth;
        lastSourceHeight_ = sourceHeight;
    }

    GLint previousRead = 0;
    GLint previousDraw = 0;
    GLint previousPack = 0;
    GLint previousAlignment = 4;
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPack);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    if (scissor) {
        glDisable(GL_SCISSOR_TEST);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer_);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_.fbo);
    const bool same = sourceWidth == settings_.width && sourceHeight == settings_.height;
    glBlitFramebuffer(0, 0, sourceWidth, sourceHeight, 0, settings_.height, settings_.width, 0, GL_COLOR_BUFFER_BIT, same ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_.fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    Slot& slot = slots_[next_];
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
    glReadPixels(0, 0, settings_.width, settings_.height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot.frame = frame;
    slot.index = decision.index;
    slot.busy = true;
    next_ = (next_ + 1) % kSlots;
    ++busy_;
    ++captured_;

    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousPack));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }

    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    captureMs_ = captured_ == 1 ? elapsed : captureMs_ * 0.95 + elapsed * 0.05;
    captureMsMax_ = std::max(captureMsMax_, elapsed);
}

void Recorder::pushSamples(const float* samples, size_t count, int sampleRate) {
    (void)sampleRate;
    if (!video_ || firstFrame_ || samples == nullptr || count == 0) {
        return;
    }
    video_->submitAudio(samples, count, clock_() - origin_);
}

void Recorder::stop(double nowSeconds) {
    if (!video_) {
        return;
    }
    const RecordState state = video_->stats().state;
    if (state != RecordState::Recording) {
        return;
    }
    harvest(true);
    video_->finish(firstFrame_ ? 0.0 : std::max(nowSeconds - origin_, 0.0));
}

void Recorder::cancel() {
    if (!video_) {
        return;
    }
    abandonSlots();
    video_->cancel();
}

RecordEvent Recorder::service() {
    if (!video_ || reported_) {
        return RecordEvent::None;
    }
    const RecordState state = video_->stats().state;
    if (state == RecordState::Recording) {
        return RecordEvent::None;
    }
    if (state == RecordState::Finishing) {
        harvest(false);
        return RecordEvent::None;
    }
    if (state != RecordState::Finished && state != RecordState::Failed && state != RecordState::Cancelled) {
        return RecordEvent::None;
    }
    video_->waitUntilDone(5.0);
    releaseGl();
    reported_ = true;
    if (state == RecordState::Finished) {
        return RecordEvent::Finished;
    }
    return state == RecordState::Failed ? RecordEvent::Failed : RecordEvent::Cancelled;
}

void Recorder::shutdown(double timeoutSeconds) {
    if (!video_) {
        return;
    }
    if (video_->stats().state == RecordState::Recording) {
        stop(clock_());
    }
    video_->waitUntilDone(timeoutSeconds);
    if (video_->stats().state == RecordState::Recording || video_->stats().state == RecordState::Finishing) {
        video_->cancel();
        video_->waitUntilDone(5.0);
    }
    releaseGl();
    video_.reset();
    reported_ = true;
}

RecordStatus Recorder::status() const {
    RecordStatus status;
    if (!video_) {
        return status;
    }
    const RecordStats stats = video_->stats();
    status.state = stats.state;
    status.stop = stats.stop;
    status.path = stats.path;
    status.error = stats.error;
    status.detail = stats.detail;
    status.encoder = stats.encoder;
    status.audioNote = stats.audioNote;
    status.width = settings_.width;
    status.height = settings_.height;
    status.fps = settings_.fps;
    status.frames = stats.framesEncoded;
    status.bytes = stats.bytes;
    status.droppedPool = droppedPool_;
    status.droppedReadback = droppedReadback_;
    status.missedSlots = static_cast<uint64_t>(schedule_.missedTotal());
    status.surplusFrames = static_cast<uint64_t>(schedule_.surplusTotal());
    status.sizeChanges = sizeChanges_;
    status.seconds = stats.videoSeconds;
    status.audioSeconds = stats.audioSeconds;
    status.audioInsertedSeconds = stats.audioInsertedSeconds;
    status.audioDroppedSeconds = stats.audioDroppedSeconds;
    status.audioOverruns = stats.audioOverruns;
    status.encodeMs = stats.encodeMsAverage;
    status.encodeMsMax = stats.encodeMsMax;
    status.captureMs = captureMs_;
    status.captureMsMax = captureMsMax_;
    status.queue = stats.queueDepth;
    status.queueMax = stats.queueDepthMax;
    status.pool = stats.poolSize;
    status.audio = stats.audioActive;
    return status;
}

}

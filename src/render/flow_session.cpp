#include "render/flow_session.hpp"

#include "core/log.hpp"
#include "media/frame_luma.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nvj {

namespace {

void targetSize(const VideoFrame& frame, int& width, int& height) {
    if (frame.layout == VideoLayout::Rgba) {
        width = frame.planes[0].width;
        height = frame.planes[0].height;
        return;
    }
    width = frame.outWidth > 0 ? frame.outWidth : frame.width;
    height = frame.outHeight > 0 ? frame.outHeight : frame.height;
}

}

FlowSession::FlowSession(VideoGpu& gpu) : gpu_(gpu) {}

FlowSession::~FlowSession() {
    releaseResources();
}

void FlowSession::releaseResources() {
    flow::globalBudget().release(this);
    renderer_.destroy();
    reservedMode_ = flow::Mode::Off;
    reservedWidth_ = 0;
    reservedHeight_ = 0;
    slotId_[0] = 0;
    slotId_[1] = 0;
    outMode_ = 0;
}

void FlowSession::configure(const flow::Settings& requested) {
    const flow::Settings next = flow::sanitised(requested);
    if (next == settings_) {
        return;
    }
    const bool tierChanged = next.tier != settings_.tier;
    const bool modeChanged = next.mode != settings_.mode;
    settings_ = next;
    flow::globalBudget().setLimit(static_cast<std::uint64_t>(next.budgetMb) * 1024ull * 1024ull);
    if (tierChanged) {
        worker_.setTier(next.tier);
    }
    if (modeChanged) {
        outMode_ = 0;
        reservedMode_ = flow::Mode::Off;
        flow::globalBudget().release(this);
    }
    outValue_ = -1.0f;
    disabledNote_ = false;
}

void FlowSession::reset() {
    neutralShown_ = false;
    worker_.reset();
    worker_.setTier(settings_.tier);
    slotId_[0] = 0;
    slotId_[1] = 0;
    uploaderFrame_ = 0;
    fieldA_ = 0;
    fieldB_ = 0;
    outMode_ = 0;
    outValue_ = -1.0f;
    engaged_ = false;
}

bool FlowSession::ensureRenderer() {
    if (renderer_.ready()) {
        return true;
    }
    if (rendererFailed_) {
        return false;
    }
    std::string error;
    if (!renderer_.create(gpu_, &error)) {
        rendererFailed_ = true;
        counters_.lastError = error;
        return false;
    }
    return true;
}

GLuint FlowSession::disengage(const std::string& reason) {
    engaged_ = false;
    state_ = "plain playback";
    reason_ = reason;
    return 0;
}

GLuint FlowSession::fallback(const std::string& reason, bool counted) {
    reason_ = reason;
    state_ = "holding the current picture";
    if (counted) {
        ++counters_.held;
    }
    if (settings_.mode == flow::Mode::Vectors) {
        return neutralVectors();
    }
    return renderer_.slotTexture(0);
}

GLuint FlowSession::neutralVectors() {
    if (!neutralShown_) {
        renderer_.setNeutralField();
        fieldA_ = 0;
        fieldB_ = 0;
        renderer_.drawVectors();
        neutralShown_ = true;
        outMode_ = 0;
    }
    return renderer_.outTexture();
}

bool FlowSession::upload(VideoUploader& uploader, const VideoFrame& frame, int slot) {
    if (!uploader.present(frame)) {
        ++counters_.failures;
        counters_.lastError = "the picture could not be uploaded for the flow pass";
        return false;
    }
    uploaderFrame_ = frame.id;
    renderer_.store(slot, uploader.texture());
    slotId_[slot] = frame.id;
    return true;
}

GLuint FlowSession::step(VideoDecoder& decoder, VideoUploader& uploader, const std::shared_ptr<const VideoFrame>& frame, double seconds, double nominalPeriod) {
    engaged_ = false;
    if (settings_.mode == flow::Mode::Off) {
        state_ = "off";
        reason_.clear();
        return 0;
    }
    if (!frame) {
        return disengage("no picture yet");
    }
    if (decoder.reversing()) {
        ++counters_.unusable;
        return disengage("reverse playback is not analysed");
    }
    std::string why;
    if (!flowCanAnalyse(*frame, &why)) {
        ++counters_.unusable;
        return disengage(why);
    }
    if (worker_.disabled()) {
        if (!disabledNote_) {
            disabledNote_ = true;
            logLine("Optical flow: the analysis cannot keep up even at the lowest quality tier, playing without it");
        }
        return disengage("the analysis is too slow for this source");
    }
    if (!ensureRenderer()) {
        return disengage("the flow shaders could not be built");
    }
    int width = 0;
    int height = 0;
    targetSize(*frame, width, height);
    if (width <= 0 || height <= 0) {
        return disengage("the picture size is unknown");
    }
    const int slots = settings_.mode == flow::Mode::Interpolate ? 2 : 1;
    if (reservedMode_ != settings_.mode || reservedWidth_ != width || reservedHeight_ != height) {
        const std::uint64_t needed = flow::estimateBytes(settings_.mode, width, height);
        if (!flow::globalBudget().reserve(this, needed)) {
            reservedMode_ = flow::Mode::Off;
            counters_.neededBytes = needed;
            return disengage("the video memory budget is too small");
        }
        counters_.neededBytes = 0;
        reservedMode_ = settings_.mode;
        reservedWidth_ = width;
        reservedHeight_ = height;
    }
    if (renderer_.width() != width || renderer_.height() != height || renderer_.slots() != slots) {
        slotId_[0] = 0;
        slotId_[1] = 0;
        outMode_ = 0;
        neutralShown_ = false;
        fieldA_ = 0;
        fieldB_ = 0;
    }
    if (!renderer_.resize(width, height, slots)) {
        flow::globalBudget().release(this);
        reservedMode_ = flow::Mode::Off;
        return disengage("the graphics card could not provide the textures");
    }
    if (slotId_[0] != frame->id) {
        if (slots == 2 && slotId_[1] == frame->id) {
            renderer_.swapSlots();
            std::swap(slotId_[0], slotId_[1]);
            slotId_[1] = 0;
        } else if (!upload(uploader, *frame, 0)) {
            return disengage("the picture could not be uploaded");
        }
    }
    engaged_ = true;
    const std::vector<std::shared_ptr<const VideoFrame>> ahead = decoder.peekAhead(2);
    const std::shared_ptr<const VideoFrame> next = ahead.empty() ? nullptr : ahead[0];
    if (next) {
        worker_.submit(frame, next);
        if (ahead.size() > 1) {
            worker_.submit(next, ahead[1]);
        }
    }
    counters_.drawMs = renderer_.lastDrawMs();
    if (!next) {
        return fallback("waiting for the next picture", true);
    }
    const flow::PairPlan plan = flow::planPair(seconds, frame->time, next->time, nominalPeriod, false);
    if (!plan.usable) {
        ++counters_.unusable;
        return fallback(plan.reason, true);
    }
    const FlowResult result = worker_.lookup(frame->id, next->id);
    if (settings_.mode == flow::Mode::Interpolate && slotId_[1] != next->id) {
        if (!upload(uploader, *next, 1)) {
            return fallback("the next picture could not be uploaded", true);
        }
        ++counters_.uploadsAhead;
    }
    if (!result.ready) {
        ++counters_.pending;
        if (settings_.mode == flow::Mode::Vectors) {
            reason_ = "the analysis is not ready yet";
            state_ = "analysing";
            return neutralVectors();
        }
        return fallback("the analysis is not ready yet", false);
    }
    if (!result.field) {
        counters_.lastError = result.error;
        return fallback("the analysis failed: " + result.error, true);
    }
    const flow::Field& field = *result.field;
    if (field.sceneCut && settings_.mode != flow::Mode::Vectors) {
        ++counters_.cuts;
        return fallback("scene cut", false);
    }
    state_ = "active";
    reason_.clear();
    const auto prepare = [&]() {
        if (fieldA_ != frame->id || fieldB_ != next->id) {
            renderer_.setField(field);
            fieldA_ = frame->id;
            fieldB_ = next->id;
        }
    };
    switch (settings_.mode) {
    case flow::Mode::Interpolate: {
        if (plan.exact) {
            return renderer_.slotTexture(plan.t < 0.5f ? 0 : 1);
        }
        prepare();
        ++counters_.synthesised;
        const GLuint texture = renderer_.drawInterpolated(plan.t);
        counters_.drawMs = renderer_.lastDrawMs();
        outMode_ = 0;
        neutralShown_ = false;
        return texture;
    }
    case flow::Mode::MotionBlur: {
        const double gap = std::max(next->time - frame->time, 1.0e-4);
        const float scale = static_cast<float>(std::clamp(nominalPeriod / gap, 0.25, 1.0));
        const float shutter = settings_.shutter * scale;
        const unsigned long long mode = static_cast<unsigned long long>(settings_.mode);
        if (outMode_ == mode && outA_ == frame->id && outB_ == next->id && outValue_ == shutter) {
            return renderer_.outTexture();
        }
        prepare();
        const GLuint texture = renderer_.drawBlur(shutter);
        counters_.drawMs = renderer_.lastDrawMs();
        outMode_ = mode;
        outA_ = frame->id;
        outB_ = next->id;
        outValue_ = shutter;
        neutralShown_ = false;
        ++counters_.synthesised;
        return texture;
    }
    default: {
        const unsigned long long mode = static_cast<unsigned long long>(settings_.mode);
        if (outMode_ == mode && outA_ == frame->id && outB_ == next->id) {
            return renderer_.outTexture();
        }
        prepare();
        const GLuint texture = renderer_.drawVectors();
        counters_.drawMs = renderer_.lastDrawMs();
        outMode_ = mode;
        outA_ = frame->id;
        outB_ = next->id;
        outValue_ = -1.0f;
        neutralShown_ = false;
        ++counters_.synthesised;
        return texture;
    }
    }
}

FlowStatus FlowSession::status() const {
    FlowStatus out = counters_;
    const FlowWorkerStats stats = worker_.stats();
    out.mode = settings_.mode;
    out.requested = stats.requested;
    out.current = stats.current;
    out.disabled = stats.disabled;
    out.engaged = engaged_;
    out.state = state_;
    out.reason = reason_;
    if (!stats.lastError.empty()) {
        out.lastError = stats.lastError;
    }
    out.analysisWidth = stats.analysisWidth;
    out.analysisHeight = stats.analysisHeight;
    out.blocksX = stats.cols;
    out.blocksY = stats.rows;
    out.pairsSubmitted = stats.submitted;
    out.pairsDone = stats.completed;
    out.pairsFailed = stats.failed;
    out.pairsSuperseded = stats.superseded;
    out.cuts = std::max(out.cuts, stats.cuts);
    out.tierDrops = stats.lowered;
    out.analysisAverageMs = stats.averageMs;
    out.analysisP95Ms = stats.p95Ms;
    out.analysisLastMs = stats.lastMs;
    out.gpuBytes = renderer_.bytes();
    out.budgetBytes = flow::globalBudget().limit();
    out.budgetUsed = flow::globalBudget().used();
    return out;
}

}

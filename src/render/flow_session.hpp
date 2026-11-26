#pragma once

#include "core/optical_flow.hpp"
#include "media/flow_worker.hpp"
#include "media/video_decoder.hpp"
#include "render/flow_interp.hpp"
#include "render/video_gpu.hpp"

#include <memory>
#include <string>

namespace nvj {

struct FlowStatus {
    flow::Mode mode = flow::Mode::Off;
    flow::Tier requested = flow::Tier::Balanced;
    flow::Tier current = flow::Tier::Balanced;
    bool engaged = false;
    bool disabled = false;
    std::string state;
    std::string reason;
    std::string lastError;
    int analysisWidth = 0;
    int analysisHeight = 0;
    int blocksX = 0;
    int blocksY = 0;
    unsigned long long synthesised = 0;
    unsigned long long held = 0;
    unsigned long long pending = 0;
    unsigned long long cuts = 0;
    unsigned long long unusable = 0;
    unsigned long long uploadsAhead = 0;
    unsigned long long failures = 0;
    unsigned long long pairsSubmitted = 0;
    unsigned long long pairsDone = 0;
    unsigned long long pairsFailed = 0;
    unsigned long long pairsSuperseded = 0;
    unsigned long long tierDrops = 0;
    double analysisAverageMs = 0.0;
    double analysisP95Ms = 0.0;
    double analysisLastMs = 0.0;
    double drawMs = 0.0;
    unsigned long long gpuBytes = 0;
    unsigned long long budgetBytes = 0;
    unsigned long long budgetUsed = 0;
    unsigned long long neededBytes = 0;
};

class FlowSession {
public:
    explicit FlowSession(VideoGpu& gpu);
    ~FlowSession();
    FlowSession(const FlowSession&) = delete;
    FlowSession& operator=(const FlowSession&) = delete;

    void configure(const flow::Settings& settings);
    const flow::Settings& settings() const { return settings_; }
    GLuint step(VideoDecoder& decoder, VideoUploader& uploader, const std::shared_ptr<const VideoFrame>& frame, double seconds, double nominalPeriod);
    void reset();
    unsigned long long uploaderFrame() const { return uploaderFrame_; }
    bool engaged() const { return engaged_; }
    FlowStatus status() const;
    unsigned long long gpuBytes() const { return renderer_.bytes(); }
    FlowWorker& worker() { return worker_; }

private:
    GLuint disengage(const std::string& reason);
    GLuint fallback(const std::string& reason, bool counted);
    GLuint neutralVectors();
    bool ensureRenderer();
    bool upload(VideoUploader& uploader, const VideoFrame& frame, int slot);
    void releaseResources();

    VideoGpu& gpu_;
    flow::Settings settings_;
    FlowWorker worker_;
    FlowRenderer renderer_;
    bool rendererFailed_ = false;
    bool engaged_ = false;
    bool disabledNote_ = false;
    unsigned long long slotId_[2] = {0, 0};
    unsigned long long uploaderFrame_ = 0;
    unsigned long long fieldA_ = 0;
    unsigned long long fieldB_ = 0;
    unsigned long long outMode_ = 0;
    unsigned long long outA_ = 0;
    unsigned long long outB_ = 0;
    float outValue_ = -1.0f;
    bool neutralShown_ = false;
    int reservedWidth_ = 0;
    int reservedHeight_ = 0;
    flow::Mode reservedMode_ = flow::Mode::Off;
    FlowStatus counters_;
    std::string state_;
    std::string reason_;
};

}

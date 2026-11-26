#pragma once

#include "core/optical_flow.hpp"
#include "media/video_types.hpp"

#include <memory>
#include <string>

namespace nvj {

bool analyseFrames(const VideoFrame& a, const VideoFrame& b, flow::Tier tier, flow::Field& field, double* milliseconds, std::string* error);

struct FlowResult {
    bool ready = false;
    std::shared_ptr<const flow::Field> field;
    std::string error;
    flow::Tier tier = flow::Tier::Balanced;
    double milliseconds = 0.0;
};

struct FlowWorkerStats {
    flow::Tier requested = flow::Tier::Balanced;
    flow::Tier current = flow::Tier::Balanced;
    bool disabled = false;
    unsigned long long submitted = 0;
    unsigned long long completed = 0;
    unsigned long long failed = 0;
    unsigned long long superseded = 0;
    unsigned long long cuts = 0;
    unsigned long long lowered = 0;
    double lastMs = 0.0;
    double averageMs = 0.0;
    double p95Ms = 0.0;
    int analysisWidth = 0;
    int analysisHeight = 0;
    int cols = 0;
    int rows = 0;
    std::string lastError;
};

class FlowWorker {
public:
    FlowWorker();
    ~FlowWorker();
    FlowWorker(const FlowWorker&) = delete;
    FlowWorker& operator=(const FlowWorker&) = delete;

    void setTier(flow::Tier tier);
    bool submit(const std::shared_ptr<const VideoFrame>& a, const std::shared_ptr<const VideoFrame>& b);
    FlowResult lookup(unsigned long long idA, unsigned long long idB) const;
    bool disabled() const;
    FlowWorkerStats stats() const;
    void reset();
    bool idle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

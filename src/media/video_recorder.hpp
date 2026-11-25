#pragma once

#include "core/record_plan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace nvj {

enum class RecordState { Idle, Recording, Finishing, Finished, Failed, Cancelled };

enum class RecordStop { None, User, DiskFull, Error };

struct RecordStats {
    RecordState state = RecordState::Idle;
    RecordStop stop = RecordStop::None;
    std::string path;
    std::string error;
    std::string detail;
    std::string encoder;
    std::string audioNote;
    uint64_t framesSubmitted = 0;
    uint64_t framesEncoded = 0;
    uint64_t framesRejected = 0;
    uint64_t bytes = 0;
    double videoSeconds = 0.0;
    double audioSeconds = 0.0;
    double audioInsertedSeconds = 0.0;
    double audioDroppedSeconds = 0.0;
    uint64_t audioOverruns = 0;
    double encodeMsAverage = 0.0;
    double encodeMsMax = 0.0;
    int queueDepth = 0;
    int queueDepthMax = 0;
    int poolSize = 0;
    bool audioActive = false;
};

struct RecorderTestHooks {
    int encodeDelayMs = 0;
    int failWritesAfter = -1;
    uint64_t minFreeBytes = 0;
    void (*frameTap)(void* user, const uint8_t* rgba, int width, int height, int64_t index) = nullptr;
    void* tapUser = nullptr;
};

struct RecordProbe {
    bool works = false;
    std::string encoder;
    std::string detail;
};

struct RecordCapabilities {
    bool ffmpegLoaded = false;
    std::string problem;
    std::array<RecordProbe, kRecordProfileCount> profiles;

    bool available(RecordProfile profile) const { return profiles[static_cast<size_t>(profile)].works; }
};

RecordCapabilities probeRecordCapabilities();

class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();
    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    void setTestHooks(const RecorderTestHooks& hooks);
    bool open(const RecordSettings& checked, const std::string& pathUtf8, int audioRate, std::string& error, std::string& detail);
    uint8_t* acquireFrame();
    void submitFrame(uint8_t* frame, int64_t index);
    void discardFrame(uint8_t* frame);
    void submitAudio(const float* samples, size_t count, double wallSeconds);
    void finish(double wallSeconds);
    void cancel();
    bool waitUntilDone(double timeoutSeconds);
    RecordStats stats() const;
    size_t frameBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

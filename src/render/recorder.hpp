#pragma once

#include "audio/audio_sink.hpp"
#include "core/record_plan.hpp"
#include "media/video_recorder.hpp"
#include "render/gl_util.hpp"

#include <glad/gl.h>

#include <memory>
#include <string>

namespace nvj {

enum class RecordEvent { None, Finished, Failed, Cancelled };

struct RecordStatus {
    RecordState state = RecordState::Idle;
    RecordStop stop = RecordStop::None;
    std::string path;
    std::string error;
    std::string detail;
    std::string encoder;
    std::string audioNote;
    int width = 0;
    int height = 0;
    int fps = 0;
    uint64_t frames = 0;
    uint64_t bytes = 0;
    uint64_t droppedPool = 0;
    uint64_t droppedReadback = 0;
    uint64_t missedSlots = 0;
    uint64_t surplusFrames = 0;
    uint64_t sizeChanges = 0;
    double seconds = 0.0;
    double audioSeconds = 0.0;
    double audioInsertedSeconds = 0.0;
    double audioDroppedSeconds = 0.0;
    uint64_t audioOverruns = 0;
    double encodeMs = 0.0;
    double encodeMsMax = 0.0;
    double captureMs = 0.0;
    double captureMsMax = 0.0;
    int queue = 0;
    int queueMax = 0;
    int pool = 0;
    bool audio = false;

    uint64_t dropped() const { return droppedPool + droppedReadback; }
    double load() const { return fps > 0 ? encodeMs * static_cast<double>(fps) / 1000.0 : 0.0; }
};

class Recorder : public AudioSink {
public:
    Recorder();
    ~Recorder() override;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool start(const RecordSettings& requested, int sourceWidth, int sourceHeight, int audioRate, const std::string& pathUtf8, std::string& error, std::string& detail);
    void capture(GLuint texture, int sourceWidth, int sourceHeight, double nowSeconds);
    void stop(double nowSeconds);
    void cancel();
    RecordEvent service();
    void shutdown(double timeoutSeconds);
    void pushSamples(const float* samples, size_t count, int sampleRate) override;
    void setNow(double (*clock)());

    bool active() const;
    bool recording() const;
    RecordStatus status() const;
    void setTestHooks(const RecorderTestHooks& hooks);

private:
    struct Slot {
        GLuint buffer = 0;
        GLsync fence = nullptr;
        uint8_t* frame = nullptr;
        int64_t index = 0;
        bool busy = false;
    };

    static constexpr int kSlots = 3;

    bool ensureGl();
    void harvest(bool wait);
    void releaseGl();
    void abandonSlots();

    std::unique_ptr<VideoRecorder> video_;
    RecordSettings settings_;
    FrameSchedule schedule_;
    Slot slots_[kSlots];
    GLuint readFramebuffer_ = 0;
    RenderTarget target_;
    int oldest_ = 0;
    int next_ = 0;
    int busy_ = 0;
    int lastSourceWidth_ = 0;
    int lastSourceHeight_ = 0;
    bool glReady_ = false;
    bool reported_ = true;
    bool firstFrame_ = true;
    double origin_ = 0.0;
    double (*clock_)() = nullptr;
    uint64_t droppedPool_ = 0;
    uint64_t droppedReadback_ = 0;
    uint64_t sizeChanges_ = 0;
    uint64_t captured_ = 0;
    double captureMs_ = 0.0;
    double captureMsMax_ = 0.0;
    RecorderTestHooks hooks_;
    std::string path_;
};

double steadyNowSeconds();

}

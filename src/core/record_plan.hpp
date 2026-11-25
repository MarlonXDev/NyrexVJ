#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace nvj {

enum class RecordProfile {
    H264Software,
    H264Hardware,
    HevcHardware,
    ProResHq,
    ProRes4444,
    Hap,
    HapAlpha,
    HapQ,
    MotionJpeg,
    Ffv1,
    Vp9
};

constexpr int kRecordProfileCount = 11;

enum class RecordAudioCodec { None, Aac, Pcm16 };

struct RecordProfileInfo {
    RecordProfile profile;
    const char* id;
    const char* extension;
    const char* container;
    bool hardware;
    bool alphaCapable;
    bool usesQuality;
    bool intraOnly;
    bool fragmentable;
    int sizeMultiple;
    RecordAudioCodec audio;
};

const RecordProfileInfo& recordProfileInfo(RecordProfile profile);
const RecordProfileInfo* findRecordProfile(const std::string& id);

constexpr int kMaxRecordPixels = 3840 * 2160;
constexpr int kMaxRecordDimension = 7680;
constexpr int kMinRecordDimension = 16;
constexpr int kRecordFrameRates[] = {24, 25, 30, 48, 50, 60, 90, 120};
constexpr int kRecordFrameRateCount = 8;

struct RecordSettings {
    RecordProfile profile = RecordProfile::H264Software;
    int width = 0;
    int height = 0;
    int fps = 60;
    int quality = 70;
    bool audio = true;
    bool alpha = false;
    bool crashSafe = true;
    std::string folder;
};

struct RecordCheck {
    RecordSettings settings;
    bool valid = false;
    std::string problem;
};

RecordCheck checkRecordSettings(const RecordSettings& requested, int sourceWidth, int sourceHeight);

size_t recordFrameBytes(const RecordSettings& settings);
int recordPoolSize(size_t frameBytes);
std::string recordFileName(RecordProfile profile, int year, int month, int day, int hour, int minute, int second);
std::string uniqueRecordPath(const std::string& folder, const std::string& fileName, const std::function<bool(const std::string&)>& exists);

struct FrameDecision {
    bool accept = false;
    int64_t index = 0;
    int64_t missed = 0;
};

class FrameSchedule {
public:
    explicit FrameSchedule(int fps = 60);

    bool wouldAccept(double seconds) const;
    FrameDecision consider(double seconds);
    int fps() const { return fps_; }
    int64_t last() const { return last_; }
    int64_t missedTotal() const { return missed_; }
    int64_t surplusTotal() const { return surplus_; }
    bool started() const { return started_; }

private:
    int64_t indexFor(double seconds) const;

    int fps_ = 60;
    bool started_ = false;
    double origin_ = 0.0;
    double lastSeconds_ = 0.0;
    int64_t last_ = -1;
    int64_t missed_ = 0;
    int64_t surplus_ = 0;
};

class AudioSync {
public:
    explicit AudioSync(int sampleRate, double toleranceSeconds = 0.04);

    int64_t adjust(double wallSeconds, size_t samples);
    int64_t finish(double wallSeconds) const;
    int64_t writtenSamples() const { return written_; }
    int64_t insertedSamples() const { return inserted_; }
    int64_t droppedSamples() const { return dropped_; }
    int sampleRate() const { return rate_; }

private:
    int rate_ = 48000;
    double tolerance_ = 0.04;
    bool started_ = false;
    int64_t written_ = 0;
    int64_t inserted_ = 0;
    int64_t dropped_ = 0;
};

bool recordAudioRateSupported(RecordAudioCodec codec, int sampleRate);

}

#include "core/record_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nvj {

namespace {

const RecordProfileInfo kProfiles[kRecordProfileCount] = {
    {RecordProfile::H264Software, "h264", "mp4", "mp4", false, false, true, false, true, 2, RecordAudioCodec::Aac},
    {RecordProfile::H264Hardware, "h264hw", "mp4", "mp4", true, false, true, false, true, 2, RecordAudioCodec::Aac},
    {RecordProfile::HevcHardware, "hevchw", "mp4", "mp4", true, false, true, false, true, 2, RecordAudioCodec::Aac},
    {RecordProfile::ProResHq, "prores", "mov", "mov", false, false, false, true, true, 2, RecordAudioCodec::Pcm16},
    {RecordProfile::ProRes4444, "prores4444", "mov", "mov", false, true, false, true, true, 2, RecordAudioCodec::Pcm16},
    {RecordProfile::Hap, "hap", "mov", "mov", false, false, false, true, true, 4, RecordAudioCodec::Pcm16},
    {RecordProfile::HapAlpha, "hapalpha", "mov", "mov", false, true, false, true, true, 4, RecordAudioCodec::Pcm16},
    {RecordProfile::HapQ, "hapq", "mov", "mov", false, false, false, true, true, 4, RecordAudioCodec::Pcm16},
    {RecordProfile::MotionJpeg, "mjpeg", "mov", "mov", false, false, true, true, true, 2, RecordAudioCodec::Pcm16},
    {RecordProfile::Ffv1, "ffv1", "mkv", "matroska", false, true, false, true, false, 2, RecordAudioCodec::Pcm16},
    {RecordProfile::Vp9, "vp9", "mkv", "matroska", false, true, true, false, false, 2, RecordAudioCodec::Aac},
};

int roundDown(int value, int multiple) {
    return value - value % multiple;
}

}

const RecordProfileInfo& recordProfileInfo(RecordProfile profile) {
    const int index = static_cast<int>(profile);
    if (index < 0 || index >= kRecordProfileCount) {
        return kProfiles[0];
    }
    return kProfiles[index];
}

const RecordProfileInfo* findRecordProfile(const std::string& id) {
    for (const RecordProfileInfo& info : kProfiles) {
        if (id == info.id) {
            return &info;
        }
    }
    return nullptr;
}

RecordCheck checkRecordSettings(const RecordSettings& requested, int sourceWidth, int sourceHeight) {
    RecordCheck check;
    check.settings = requested;
    RecordSettings& settings = check.settings;
    const RecordProfileInfo& info = recordProfileInfo(settings.profile);

    if (settings.width <= 0 || settings.height <= 0) {
        settings.width = sourceWidth;
        settings.height = sourceHeight;
    }
    if (settings.width <= 0 || settings.height <= 0) {
        check.problem = "The size of the picture is not known";
        return check;
    }
    settings.width = roundDown(settings.width, info.sizeMultiple);
    settings.height = roundDown(settings.height, info.sizeMultiple);
    if (settings.width < kMinRecordDimension || settings.height < kMinRecordDimension) {
        check.problem = "The picture is smaller than 16 pixels";
        return check;
    }
    if (settings.width > kMaxRecordDimension || settings.height > kMaxRecordDimension || static_cast<int64_t>(settings.width) * settings.height > kMaxRecordPixels) {
        check.problem = "The picture is larger than 3840 x 2160 pixels";
        return check;
    }

    int best = kRecordFrameRates[0];
    for (int rate : kRecordFrameRates) {
        if (std::abs(rate - settings.fps) < std::abs(best - settings.fps)) {
            best = rate;
        }
    }
    settings.fps = best;
    settings.quality = std::clamp(settings.quality, 0, 100);
    if (!info.alphaCapable) {
        settings.alpha = false;
    }
    check.valid = true;
    return check;
}

size_t recordFrameBytes(const RecordSettings& settings) {
    return static_cast<size_t>(std::max(settings.width, 0)) * static_cast<size_t>(std::max(settings.height, 0)) * 4u;
}

int recordPoolSize(size_t frameBytes) {
    constexpr size_t kBudget = 256u * 1024u * 1024u;
    if (frameBytes == 0) {
        return 5;
    }
    return static_cast<int>(std::clamp<size_t>(kBudget / frameBytes, 5, 10));
}

std::string recordFileName(RecordProfile profile, int year, int month, int day, int hour, int minute, int second) {
    char text[96];
    std::snprintf(text, sizeof(text), "NyrexVJ_%04d-%02d-%02d_%02d-%02d-%02d.%s", year, month, day, hour, minute, second, recordProfileInfo(profile).extension);
    return text;
}

std::string uniqueRecordPath(const std::string& folder, const std::string& fileName, const std::function<bool(const std::string&)>& exists) {
    std::string prefix = folder;
    if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\') {
        prefix += '/';
    }
    const size_t dot = fileName.rfind('.');
    const std::string stem = dot == std::string::npos ? fileName : fileName.substr(0, dot);
    const std::string extension = dot == std::string::npos ? std::string() : fileName.substr(dot);
    std::string candidate = prefix + fileName;
    if (!exists(candidate)) {
        return candidate;
    }
    for (int number = 2; number <= 999; ++number) {
        candidate = prefix + stem + "_" + std::to_string(number) + extension;
        if (!exists(candidate)) {
            return candidate;
        }
    }
    return std::string();
}

FrameSchedule::FrameSchedule(int fps) : fps_(std::max(fps, 1)) {}

int64_t FrameSchedule::indexFor(double seconds) const {
    return static_cast<int64_t>(std::floor((seconds - origin_) * static_cast<double>(fps_) + 0.25));
}

bool FrameSchedule::wouldAccept(double seconds) const {
    if (!std::isfinite(seconds)) {
        return false;
    }
    if (!started_) {
        return true;
    }
    if (seconds < lastSeconds_) {
        return false;
    }
    return indexFor(seconds) > last_;
}

FrameDecision FrameSchedule::consider(double seconds) {
    FrameDecision decision;
    if (!std::isfinite(seconds)) {
        return decision;
    }
    if (!started_) {
        started_ = true;
        origin_ = seconds;
        lastSeconds_ = seconds;
        last_ = 0;
        decision.accept = true;
        return decision;
    }
    if (seconds < lastSeconds_) {
        ++surplus_;
        return decision;
    }
    lastSeconds_ = seconds;
    const int64_t index = indexFor(seconds);
    if (index <= last_) {
        ++surplus_;
        return decision;
    }
    decision.accept = true;
    decision.index = index;
    decision.missed = index - last_ - 1;
    missed_ += decision.missed;
    last_ = index;
    return decision;
}

AudioSync::AudioSync(int sampleRate, double toleranceSeconds) : rate_(std::max(sampleRate, 1)), tolerance_(std::max(toleranceSeconds, 0.0)) {}

int64_t AudioSync::adjust(double wallSeconds, size_t samples) {
    const int64_t count = static_cast<int64_t>(samples);
    int64_t delta = 0;
    if (!started_) {
        started_ = true;
        const double chunkStart = wallSeconds - static_cast<double>(count) / static_cast<double>(rate_);
        delta = std::llround(chunkStart * static_cast<double>(rate_));
        delta = std::max(delta, -count);
    } else {
        const double covered = static_cast<double>(written_ + count) / static_cast<double>(rate_);
        const double drift = covered - wallSeconds;
        if (drift < -tolerance_) {
            delta = std::llround(-drift * static_cast<double>(rate_));
        } else if (drift > tolerance_) {
            delta = -std::min(count, static_cast<int64_t>(std::llround(drift * static_cast<double>(rate_))));
        }
    }
    if (delta > 0) {
        inserted_ += delta;
    } else {
        dropped_ -= delta;
    }
    written_ += count + delta;
    return delta;
}

int64_t AudioSync::finish(double wallSeconds) const {
    const int64_t wanted = static_cast<int64_t>(std::llround(std::max(wallSeconds, 0.0) * static_cast<double>(rate_)));
    return std::max<int64_t>(wanted - written_, 0);
}

bool recordAudioRateSupported(RecordAudioCodec codec, int sampleRate) {
    switch (codec) {
    case RecordAudioCodec::Aac:
        for (int rate : {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000}) {
            if (rate == sampleRate) {
                return true;
            }
        }
        return false;
    case RecordAudioCodec::Pcm16:
        return sampleRate >= 8000 && sampleRate <= 192000;
    case RecordAudioCodec::None:
        break;
    }
    return false;
}

}

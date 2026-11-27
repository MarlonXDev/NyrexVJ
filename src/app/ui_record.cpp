#include "app/app.hpp"
#include "app/ui_common.hpp"
#include "core/i18n.hpp"
#include "core/log.hpp"
#include "core/record_plan.hpp"
#include "media/video_decoder.hpp"
#include "media/video_recorder.hpp"
#include "platform/platform.hpp"
#include "scene3d/scene_pack.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);
const ImVec4 kRec(0.95f, 0.25f, 0.25f, 1.0f);

struct SizeChoice {
    int width;
    int height;
    const char* label;
};

constexpr SizeChoice kSizeChoices[] = {
    {1280, 720, "1280 x 720"},
    {1920, 1080, "1920 x 1080"},
    {2560, 1440, "2560 x 1440"},
    {3840, 2160, "3840 x 2160"},
};
constexpr int kSizeChoiceCount = 4;
constexpr int kSizeSameAsOutput = 0;
constexpr int kSizeCustom = kSizeChoiceCount + 1;

const char* profileLabel(RecordProfile profile) {
    switch (profile) {
    case RecordProfile::H264Software:
        return "H.264, MP4";
    case RecordProfile::H264Hardware:
        return "H.264, MP4 (GPU)";
    case RecordProfile::HevcHardware:
        return "HEVC, MP4 (GPU)";
    case RecordProfile::ProResHq:
        return "ProRes 422 HQ, MOV";
    case RecordProfile::ProRes4444:
        return "ProRes 4444, MOV";
    case RecordProfile::Hap:
        return "HAP, MOV";
    case RecordProfile::HapAlpha:
        return "HAP Alpha, MOV";
    case RecordProfile::HapQ:
        return "HAP Q, MOV";
    case RecordProfile::MotionJpeg:
        return "Motion JPEG, MOV";
    case RecordProfile::Ffv1:
        return "FFV1, MKV";
    case RecordProfile::Vp9:
        return "VP9, MKV";
    }
    return "";
}

const char* profileHint(RecordProfile profile) {
    switch (profile) {
    case RecordProfile::H264Software:
        return tr("Plays almost everywhere and keeps files small. The processor does the work.");
    case RecordProfile::H264Hardware:
        return tr("Like H.264, but the graphics card does the work. Needs a supported graphics card.");
    case RecordProfile::HevcHardware:
        return tr("Smaller files than H.264 at the same quality. The graphics card does the work.");
    case RecordProfile::ProResHq:
        return tr("High quality for editing programs. Large files, easy on the processor.");
    case RecordProfile::ProRes4444:
        return tr("The best ProRes quality. Very large files.");
    case RecordProfile::Hap:
        return tr("Made for VJ programs. Large files that are very light to play back.");
    case RecordProfile::HapAlpha:
        return tr("Like HAP, and it can keep transparency.");
    case RecordProfile::HapQ:
        return tr("HAP with better colors. Larger files.");
    case RecordProfile::MotionJpeg:
        return tr("Simple and robust. Every picture stands alone. Large files.");
    case RecordProfile::Ffv1:
        return tr("Lossless. Very large files, meant for archives.");
    case RecordProfile::Vp9:
        return tr("Open format for the web. Slow to encode, so use smaller sizes.");
    }
    return "";
}

std::string bytesText(uint64_t bytes) {
    char text[48];
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        std::snprintf(text, sizeof(text), "%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        std::snprintf(text, sizeof(text), "%.1f MB", value / (1024.0 * 1024.0));
    } else {
        std::snprintf(text, sizeof(text), "%.0f KB", value / 1024.0);
    }
    return text;
}

std::string clockText(double seconds) {
    const long long total = static_cast<long long>(std::max(seconds, 0.0));
    char text[32];
    std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
    return text;
}

std::string defaultRecordFolder() {
    return userDataDir() + "/Recordings";
}

}

void App::startRecordProbe() {
    if (recordProbe_.valid()) {
        return;
    }
    recordCapsKnown_ = false;
    recordProbe_ = std::async(std::launch::async, [] { return probeRecordCapabilities(); });
}

RecordSettings App::recordSettingsFromConfig() const {
    RecordSettings settings;
    const RecordProfileInfo* info = findRecordProfile(settings_.recordProfile);
    settings.profile = info != nullptr ? info->profile : RecordProfile::H264Software;
    settings.width = settings_.recordWidth;
    settings.height = settings_.recordHeight;
    settings.fps = settings_.recordFps;
    settings.quality = settings_.recordQuality;
    settings.audio = settings_.recordAudio;
    settings.alpha = false;
    settings.crashSafe = settings_.recordCrashSafe;
    settings.folder = recordFolder_[0] != '\0' ? std::string(recordFolder_) : defaultRecordFolder();
    return settings;
}

bool App::startRecording() {
    if (recorder_.active()) {
        return false;
    }
    RecordSettings settings = recordSettingsFromConfig();
    std::error_code ec;
    std::filesystem::create_directories(pathFromUtf8(settings.folder), ec);
    if (ec) {
        recordLastFailed_ = true;
        recordLastNote_ = tr("The recording folder cannot be created");
        toast(recordLastNote_);
        logLine("Recording folder could not be created: " + settings.folder);
        return false;
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const std::string name = recordFileName(settings.profile, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    const std::string path = uniqueRecordPath(settings.folder, name, [](const std::string& candidate) {
        std::error_code exists;
        return std::filesystem::exists(pathFromUtf8(candidate), exists);
    });
    if (path.empty()) {
        recordLastFailed_ = true;
        recordLastNote_ = tr("No free file name was found in the recording folder");
        toast(recordLastNote_);
        return false;
    }
    const int audioRate = settings.audio && audio_.deviceActive() ? audio_.sampleRate() : 0;
    std::string error;
    std::string detail;
    if (!recorder_.start(settings, engine_.width(), engine_.height(), audioRate, path, error, detail)) {
        recordLastFailed_ = true;
        recordLastNote_ = trMessage(error);
        if (!detail.empty()) {
            recordLastNote_ += ": " + detail;
        }
        toast(std::string(tr("Recording could not be started")) + ". " + recordLastNote_);
        logLine("Recording could not be started: " + error + (detail.empty() ? std::string() : " (" + detail + ")"));
        return false;
    }
    recordLastPath_ = path;
    recordLastFailed_ = false;
    recordLastNote_.clear();
    toast(tr("Recording started"));
    logLine("Recording started: " + path);
    return true;
}

void App::stopRecording() {
    if (!recorder_.recording()) {
        return;
    }
    recorder_.stop(steadyNowSeconds());
    toast(tr("Saving the recording..."));
}

void App::cancelRecording() {
    if (!recorder_.active()) {
        return;
    }
    recorder_.cancel();
}

void App::serviceRecording() {
    if (recordProbe_.valid() && recordProbe_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        recordCaps_ = recordProbe_.get();
        recordCapsKnown_ = true;
    }
    const RecordEvent event = recorder_.service();
    if (event == RecordEvent::None) {
        return;
    }
    const RecordStatus status = recorder_.status();
    recordLastPath_ = status.path;
    if (event == RecordEvent::Finished) {
        recordLastFailed_ = false;
        recordLastNote_.clear();
        if (status.stop == RecordStop::DiskFull) {
            recordLastFailed_ = true;
            recordLastNote_ = trMessage(status.error);
            toast(recordLastNote_);
        } else {
            toast(tr("Recording saved"));
        }
        logLine("Recording finished: " + status.path + " (" + std::to_string(status.frames) + " frames, " + std::to_string(status.dropped()) + " dropped)");
    } else if (event == RecordEvent::Failed) {
        recordLastFailed_ = true;
        recordLastNote_ = trMessage(status.error);
        if (!status.detail.empty()) {
            recordLastNote_ += ": " + status.detail;
        }
        toast(std::string(tr("Recording failed")) + ". " + recordLastNote_);
        logLine("Recording failed: " + status.error + (status.detail.empty() ? std::string() : " (" + status.detail + ")"));
    } else {
        recordLastFailed_ = false;
        recordLastNote_.clear();
        recordLastPath_.clear();
        toast(tr("Recording cancelled"));
        logLine("Recording cancelled");
    }
}

void App::drawRecordingStatusBar() {
    if (!recorder_.active()) {
        return;
    }
    const RecordStatus status = recorder_.status();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (status.state == RecordState::Finishing) {
        ImGui::TextColored(kWarn, "%s", tr("Saving the recording..."));
        return;
    }
    ImGui::TextColored(kRec, "REC %s", clockText(status.seconds).c_str());
    if (status.dropped() > 0) {
        ImGui::SameLine();
        ImGui::TextColored(kWarn, "%s: %llu", tr("Dropped frames"), static_cast<unsigned long long>(status.dropped()));
    }
}

void App::drawRecordingPage() {
    if (!recordCapsKnown_ && !recordProbe_.valid()) {
        startRecordProbe();
    }
    auto persist = [this]() {
        settings_.recordFolder = recordFolder_;
        saveSettings(settings_);
    };

    const bool running = recorder_.active();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Records the final picture, the same one that goes to the output windows, into a video file. The picture is read from the graphics card in the background, so the show keeps its frame rate."));
    ImGui::PopTextWrapPos();

    if (!recordCapsKnown_) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("Checking which formats work on this computer..."));
        return;
    }

    if (!recordCaps_.ffmpegLoaded) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", tr("FFmpeg is needed for recording and was not found."));
        ImGui::TextDisabled("%s", tr("Set it up in Settings, Performance. Then check again."));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(trId("Open Performance settings", "recOpenPerformance"))) {
            openSettings(SettingsPerformance);
        }
        ImGui::SameLine();
        if (ImGui::Button(trId("Check again", "recCheckAgain"))) {
            retryVideoSupport();
            recordCapsKnown_ = false;
            startRecordProbe();
        }
        return;
    }

    std::array<RecordProfile, kRecordProfileCount> available{};
    int availableCount = 0;
    for (int i = 0; i < kRecordProfileCount; ++i) {
        const RecordProfile profile = static_cast<RecordProfile>(i);
        if (recordCaps_.available(profile)) {
            available[static_cast<size_t>(availableCount++)] = profile;
        }
    }
    if (availableCount == 0) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", tr("No video format works on this computer with this FFmpeg."));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(trId("Check again", "recCheckAgain"))) {
            retryVideoSupport();
            recordCapsKnown_ = false;
            startRecordProbe();
        }
        return;
    }

    const RecordProfileInfo* storedInfo = findRecordProfile(settings_.recordProfile);
    RecordProfile current = storedInfo != nullptr ? storedInfo->profile : RecordProfile::H264Software;
    if (!recordCaps_.available(current)) {
        current = available[0];
    }
    const RecordProfileInfo& info = recordProfileInfo(current);
    if (storedInfo == nullptr || storedInfo->profile != current) {
        settings_.recordProfile = info.id;
    }

    ui::sectionHeader(tr("Format"));
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(ui::px(260.0f));
    if (ui::beginCombo(trId("Format", "recFormat"), profileLabel(current))) {
        for (int i = 0; i < availableCount; ++i) {
            const RecordProfile profile = available[static_cast<size_t>(i)];
            if (ImGui::Selectable(profileLabel(profile), profile == current)) {
                settings_.recordProfile = recordProfileInfo(profile).id;
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", profileHint(current));
    const RecordProbe& probe = recordCaps_.profiles[static_cast<size_t>(current)];
    if (!probe.encoder.empty()) {
        ImGui::TextDisabled("%s: %s", tr("Encoder"), probe.encoder.c_str());
    }
    ImGui::PopTextWrapPos();

    ui::sectionHeader(tr("Picture"));
    if (recordSizeChoice_ < 0) {
        recordSizeChoice_ = kSizeSameAsOutput;
        if (settings_.recordWidth > 0 && settings_.recordHeight > 0) {
            recordSizeChoice_ = kSizeCustom;
            for (int i = 0; i < kSizeChoiceCount; ++i) {
                if (kSizeChoices[i].width == settings_.recordWidth && kSizeChoices[i].height == settings_.recordHeight) {
                    recordSizeChoice_ = i + 1;
                }
            }
            recordCustomWidth_ = settings_.recordWidth;
            recordCustomHeight_ = settings_.recordHeight;
        }
    }
    char outputLabel[64];
    std::snprintf(outputLabel, sizeof(outputLabel), "%s (%d x %d)", tr("Same as output"), engine_.width(), engine_.height());
    const char* sizeLabel = outputLabel;
    if (recordSizeChoice_ == kSizeCustom) {
        sizeLabel = tr("Custom");
    } else if (recordSizeChoice_ > 0) {
        sizeLabel = kSizeChoices[recordSizeChoice_ - 1].label;
    }
    ImGui::SetNextItemWidth(ui::px(260.0f));
    if (ui::beginCombo(trId("Size", "recSize"), sizeLabel)) {
        if (ImGui::Selectable(outputLabel, recordSizeChoice_ == kSizeSameAsOutput)) {
            recordSizeChoice_ = kSizeSameAsOutput;
            settings_.recordWidth = 0;
            settings_.recordHeight = 0;
            persist();
        }
        for (int i = 0; i < kSizeChoiceCount; ++i) {
            if (ImGui::Selectable(kSizeChoices[i].label, recordSizeChoice_ == i + 1)) {
                recordSizeChoice_ = i + 1;
                settings_.recordWidth = kSizeChoices[i].width;
                settings_.recordHeight = kSizeChoices[i].height;
                persist();
            }
        }
        if (ImGui::Selectable(tr("Custom"), recordSizeChoice_ == kSizeCustom)) {
            recordSizeChoice_ = kSizeCustom;
            settings_.recordWidth = recordCustomWidth_;
            settings_.recordHeight = recordCustomHeight_;
            persist();
        }
        ImGui::EndCombo();
    }
    if (recordSizeChoice_ == kSizeCustom) {
        ImGui::SetNextItemWidth(ui::px(120.0f));
        bool edited = ImGui::InputInt(trId("Width", "recWidth"), &recordCustomWidth_, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::px(120.0f));
        edited = ImGui::InputInt(trId("Height", "recHeight"), &recordCustomHeight_, 0, 0) || edited;
        if (edited) {
            recordCustomWidth_ = std::clamp(recordCustomWidth_, 0, kMaxRecordDimension);
            recordCustomHeight_ = std::clamp(recordCustomHeight_, 0, kMaxRecordDimension);
            settings_.recordWidth = recordCustomWidth_;
            settings_.recordHeight = recordCustomHeight_;
            persist();
        }
    }

    int fpsIndex = 0;
    for (int i = 0; i < kRecordFrameRateCount; ++i) {
        if (kRecordFrameRates[i] == settings_.recordFps) {
            fpsIndex = i;
        }
    }
    char fpsLabel[24];
    std::snprintf(fpsLabel, sizeof(fpsLabel), "%d", kRecordFrameRates[fpsIndex]);
    ImGui::SetNextItemWidth(ui::px(120.0f));
    if (ui::beginCombo(trId("Frame rate", "recFps"), fpsLabel)) {
        for (int i = 0; i < kRecordFrameRateCount; ++i) {
            std::snprintf(fpsLabel, sizeof(fpsLabel), "%d", kRecordFrameRates[i]);
            if (ImGui::Selectable(fpsLabel, i == fpsIndex)) {
                settings_.recordFps = kRecordFrameRates[i];
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Pictures per second in the file. The show itself keeps running at its own speed."));

    if (info.usesQuality) {
        ImGui::SetNextItemWidth(ui::px(260.0f));
        ui::sliderInt(trId("Quality", "recQuality"), &settings_.recordQuality, 0, 100, "%d");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("Higher quality makes bigger files."));
    } else {
        ImGui::TextDisabled("%s", tr("This format has a fixed quality."));
    }

    ui::sectionHeader(tr("Sound"));
    const bool deviceAudio = audio_.deviceActive();
    bool wantAudio = settings_.recordAudio && info.audio != RecordAudioCodec::None;
    ImGui::BeginDisabled(info.audio == RecordAudioCodec::None);
    if (ImGui::Checkbox(trId("Record sound", "recAudio"), &wantAudio)) {
        settings_.recordAudio = wantAudio;
        persist();
    }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    if (info.audio == RecordAudioCodec::None) {
        ImGui::TextDisabled("%s", tr("This format cannot hold sound."));
    } else if (wantAudio && !deviceAudio) {
        ImGui::TextColored(kWarn, "%s", tr("No audio input is selected, so the file will have no sound. Choose an input in Settings, Audio. The demo signal is not recorded."));
    } else {
        ImGui::TextDisabled("%s", tr("The sound comes from the audio source in Settings, Audio."));
    }
    ImGui::PopTextWrapPos();

    ui::sectionHeader(tr("File"));
    if (info.fragmentable) {
        bool safe = settings_.recordCrashSafe;
        if (ImGui::Checkbox(trId("Keep the file readable after a crash", "recCrashSafe"), &safe)) {
            settings_.recordCrashSafe = safe;
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("If the program or the computer stops during a recording, everything recorded so far can still be played. Some programs open such files a little slower."));
    }
    const std::string folderShown = recordFolder_[0] != '\0' ? std::string(recordFolder_) : defaultRecordFolder();
    ImGui::SetNextItemWidth(ui::px(-250.0f));
    if (ImGui::InputTextWithHint(trId("Folder", "recFolder"), defaultRecordFolder().c_str(), recordFolder_, sizeof(recordFolder_))) {
        settings_.recordFolder = recordFolder_;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        persist();
    }
    ImGui::SameLine();
    ImGui::EndDisabled();
    if (ImGui::SmallButton(trId("Open folder", "recOpenFolder"))) {
        std::error_code ec;
        std::filesystem::create_directories(pathFromUtf8(folderShown), ec);
        platform::openFolder(folderShown);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    if (ImGui::SmallButton(trId("Default", "recFolderDefault"))) {
        recordFolder_[0] = '\0';
        persist();
    }
    ImGui::EndDisabled();
    {
        std::error_code ec;
        const std::filesystem::space_info space = std::filesystem::space(pathFromUtf8(folderShown), ec);
        if (ec) {
            std::error_code parentEc;
            const std::filesystem::path parent = pathFromUtf8(folderShown).parent_path();
            const std::filesystem::space_info parentSpace = std::filesystem::space(parent.empty() ? std::filesystem::path(".") : parent, parentEc);
            if (!parentEc) {
                ImGui::TextDisabled("%s: %s", tr("Free space"), bytesText(parentSpace.available).c_str());
            }
        } else {
            ImGui::TextDisabled("%s: %s", tr("Free space"), bytesText(space.available).c_str());
        }
    }

    const RecordCheck check = checkRecordSettings(recordSettingsFromConfig(), engine_.width(), engine_.height());
    RecordSettings effective = check.settings;
    effective.profile = current;
    if (check.valid) {
        const size_t frameBytes = recordFrameBytes(effective);
        const size_t memory = frameBytes * static_cast<size_t>(recordPoolSize(frameBytes) + 4);
        ImGui::TextDisabled("%s: %d x %d    %s: %s", tr("Picture in the file"), effective.width, effective.height, tr("Memory while recording"), bytesText(memory).c_str());
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", trMessage(check.problem).c_str());
        ImGui::PopTextWrapPos();
    }

    ui::sectionHeader(tr("Recording"));
    if (!running) {
        ImGui::BeginDisabled(!check.valid);
        if (ui::accentButton(trId("Start recording", "recStart"), true)) {
            startRecording();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Ctrl+Shift+R");
    } else {
        const RecordStatus status = recorder_.status();
        if (status.state == RecordState::Recording) {
            if (ui::accentButton(trId("Stop recording", "recStop"), true)) {
                stopRecording();
            }
            ImGui::SetItemTooltip("Ctrl+Shift+R");
            ImGui::SameLine();
            if (ImGui::Button(trId("Cancel and delete", "recCancel"))) {
                cancelRecording();
            }
            ImGui::SetItemTooltip("%s", tr("Stops the recording and deletes the file."));
        } else {
            ImGui::TextColored(kWarn, "%s", tr("Saving the recording..."));
        }
    }

    const RecordStatus status = recorder_.status();
    if (running || status.state == RecordState::Finished || status.state == RecordState::Failed) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        if (running) {
            ImGui::TextColored(kRec, "REC  %s", clockText(status.seconds).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%d x %d, %d", status.width, status.height, status.fps);
        }
        ImGui::TextDisabled("%s: %s", tr("File"), status.path.c_str());
        ImGui::TextDisabled("%s: %llu    %s: %s", tr("Recorded frames"), static_cast<unsigned long long>(status.frames), tr("File size"), bytesText(status.bytes).c_str());
        if (!status.encoder.empty()) {
            ImGui::TextDisabled("%s: %s", tr("Encoder"), status.encoder.c_str());
        }
        if (running) {
            const float load = static_cast<float>(status.load());
            ImGui::TextDisabled("%s: %.0f %%    %s: %d / %d", tr("Encoder load"), load * 100.0f, tr("Queue"), status.queue, std::max(status.pool, 1));
            ImGui::TextDisabled("%s: %.2f ms", tr("Time in the render loop"), status.captureMs);
        }
        if (status.dropped() > 0) {
            ImGui::TextColored(kWarn, "%s: %llu", tr("Dropped frames"), static_cast<unsigned long long>(status.dropped()));
        } else {
            ImGui::TextColored(kGood, "%s", tr("No frames dropped"));
        }
        if (status.missedSlots > 0) {
            ImGui::TextDisabled("%s: %llu", tr("Repeated frames"), static_cast<unsigned long long>(status.missedSlots));
            ImGui::SetItemTooltip("%s", tr("The show ran slower than the frame rate of the file. The last picture stays on during the gap, so the video keeps its true speed."));
        }
        if (status.sizeChanges > 0) {
            ImGui::TextDisabled("%s: %llu", tr("Output size changed"), static_cast<unsigned long long>(status.sizeChanges));
        }
        if (status.audio) {
            ImGui::TextDisabled("%s: %.1f s", tr("Sound recorded"), status.audioSeconds);
            if (status.audioInsertedSeconds > 0.05 || status.audioDroppedSeconds > 0.05) {
                ImGui::TextDisabled("%s: +%.2f s / -%.2f s", tr("Sound corrected"), status.audioInsertedSeconds, status.audioDroppedSeconds);
            }
        } else if (!status.audioNote.empty()) {
            ImGui::TextColored(kWarn, "%s", trMessage(status.audioNote).c_str());
        }
        ImGui::PopTextWrapPos();
    }
    if (!running && !recordLastNote_.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(recordLastFailed_ ? kWarn : kGood, "%s", recordLastNote_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!running && !recordLastPath_.empty() && !recordLastFailed_ && status.state == RecordState::Finished) {
        ImGui::TextDisabled("%s: %s", tr("Saved to"), recordLastPath_.c_str());
    }
}

}

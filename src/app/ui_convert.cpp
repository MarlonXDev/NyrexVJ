#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "media/hap_convert.hpp"
#include "media/video_decoder.hpp"
#include "media/video_redirect.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "scene3d/scene_pack.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

std::string sizeText(unsigned long long bytes) {
    char buffer[48];
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", bytes);
    }
    return buffer;
}

const char* stateLabel(hapjobs::JobState state) {
    switch (state) {
    case hapjobs::JobState::Queued:
        return tr("Queued");
    case hapjobs::JobState::Probing:
        return tr("Checking");
    case hapjobs::JobState::Waiting:
        return tr("Waiting for your answer");
    case hapjobs::JobState::Converting:
        return tr("Converting");
    case hapjobs::JobState::Done:
        return tr("Done");
    case hapjobs::JobState::Cached:
        return tr("Already converted");
    case hapjobs::JobState::Failed:
        return tr("Failed");
    case hapjobs::JobState::Cancelled:
        return tr("Cancelled");
    }
    return "";
}

float progressOf(const hapjobs::JobInfo& job) {
    if (job.framesTotal <= 0) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(job.framesDone) / static_cast<float>(job.framesTotal), 0.0f, 1.0f);
}

void beginCard(float offset) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 14.0f, viewport->WorkPos.y + viewport->WorkSize.y - 14.0f - offset), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ui::setNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(ImGui::GetFontSize() * 26.0f, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::kAccent);
}

void endCard() {
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

constexpr ImGuiWindowFlags kCardFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;

}

void App::startFfmpegCheck() {
    if (ffmpegCheck_.valid()) {
        return;
    }
    ffmpegCheck_ = std::async(std::launch::async, [] { return ffmpeg_setup::checkCapabilities(); });
}

void App::applyConvertSettings() {
    if (!convert_) {
        return;
    }
    hapjobs::Config config;
    config.mode = hapjobs::importModeFromName(settings_.convertMode);
    config.format = hapconv::targetFromName(settings_.convertFormat);
    config.snappy = true;
    config.cacheLimitBytes = static_cast<unsigned long long>(std::max(settings_.convertCacheMb, 256)) * 1024ull * 1024ull;
    convert_->configure(config);
}

void App::offerLibraryVideos(bool manual) {
    if (!convert_ || !convert_->ready()) {
        return;
    }
    for (const ClipDef& clip : clipLibrary()) {
        if (clip.kind == ClipKind::VideoFile && !clip.path.empty()) {
            convert_->offer(clip.path, manual);
        }
    }
}

void App::initConversion() {
    const std::filesystem::path directory = std::filesystem::path(userDataDir()) / "cache" / "hap";
    convert_ = std::make_unique<hapjobs::Manager>(directory);
    applyConvertSettings();
    hapjobs::Manager* manager = convert_.get();
    redirect::Hooks hooks;
    hooks.lookup = [manager](const std::string& path) { return manager->convertedPathFor(path); };
    hooks.markBad = [manager](const std::string& path, const std::string& reason) { manager->markBad(path, reason); };
    hooks.generation = [manager]() { return manager->generation(); };
    redirect::install(hooks);
    startFfmpegCheck();
}

void App::shutdownConversion() {
    redirect::uninstall();
    ffmpegInstaller_.cancel();
    ffmpegInstaller_.join();
    if (ffmpegCheck_.valid()) {
        ffmpegCheck_.wait();
    }
    if (convert_) {
        convert_->cancelAll();
    }
    convert_.reset();
}

void App::startFfmpegInstall() {
    if (ffmpegInstaller_.running()) {
        return;
    }
    if (!ffmpegPackageKnown_) {
        ffmpegPackageKnown_ = ffmpeg_setup::choosePackage(ffmpeg_setup::compiledAvcodecMajor(), ffmpeg_setup::platformKey(), ffmpegPackage_, ffmpegPackageReason_);
        if (!ffmpegPackageKnown_) {
            return;
        }
    }
    ffmpegInstallSeen_ = ffmpegInstaller_.start(ffmpegPackage_, pathFromUtf8(ffmpegDirectory()));
}

void App::serviceConversion() {
    if (!convert_) {
        return;
    }
    if (ffmpegCheck_.valid() && ffmpegCheck_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        ffmpegCaps_ = ffmpegCheck_.get();
        ffmpegCapsKnown_ = true;
        const bool ready = ffmpegCaps_.ready();
        convert_->setReady(ready);
        if (ready) {
            offerLibraryVideos(false);
        } else {
            logLine("DXV conversion is not available: " + (ffmpegCaps_.missing.empty() ? std::string("unknown reason") : ffmpegCaps_.missing.front()));
        }
    }
    if (ffmpegInstallSeen_ && !ffmpegInstaller_.running()) {
        ffmpegInstallSeen_ = false;
        const ffmpeg_setup::Status status = ffmpegInstaller_.status();
        if (status.ok) {
            retryVideoSupport();
            startFfmpegCheck();
            toast(tr("FFmpeg is ready. Press Try again on a video that did not load."));
        }
    }
    if (convertScanPending_) {
        convertScanPending_ = false;
        offerLibraryVideos(false);
    }
}

void App::drawConversionCards() {
    if (!convert_) {
        return;
    }
    float offset = 0.0f;

    const int waiting = convert_->waitingCount();
    if (waiting > 0) {
        const std::vector<hapjobs::JobInfo> jobs = convert_->jobs();
        beginCard(offset);
        if (ImGui::Begin("##convertask", nullptr, kCardFlags)) {
            ImGui::PushFont(nullptr, 17.0f);
            ImGui::TextColored(ui::kAccent, "%s", tr("DXV videos found"));
            ImGui::PopFont();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(tr("These videos use the DXV codec, which needs a lot of processor time. NyrexVJ can make HAP copies in the background. Your original files stay untouched."));
            ImGui::PopTextWrapPos();
            int shown = 0;
            for (const hapjobs::JobInfo& job : jobs) {
                if (job.state != hapjobs::JobState::Waiting) {
                    continue;
                }
                if (shown < 4) {
                    ImGui::BulletText("%s", job.name.c_str());
                }
                ++shown;
            }
            if (shown > 4) {
                ImGui::TextDisabled(tr("and %d more"), shown - 4);
            }
            if (ui::accentButton(trId("Convert", "convertApprove"), true)) {
                convert_->approveAll();
            }
            ImGui::SameLine();
            if (ImGui::Button(trId("Not now", "convertDismiss"))) {
                convert_->dismissAll();
            }
            ImGui::SetItemTooltip("%s", tr("These videos are not offered again. You can still convert them under Settings, Performance."));
            offset += ImGui::GetWindowHeight() + 10.0f;
        }
        ImGui::End();
        endCard();
    }

    const int active = convert_->activeCount();
    if (active > 0) {
        const std::vector<hapjobs::JobInfo> jobs = convert_->jobs();
        beginCard(offset);
        if (ImGui::Begin("##convertbusy", nullptr, kCardFlags)) {
            ImGui::PushFont(nullptr, 17.0f);
            ImGui::TextColored(ui::kAccent, "%s", tr("Converting to HAP"));
            ImGui::PopFont();
            int shown = 0;
            for (const hapjobs::JobInfo& job : jobs) {
                const bool running = job.state == hapjobs::JobState::Queued || job.state == hapjobs::JobState::Probing || job.state == hapjobs::JobState::Converting;
                if (!running) {
                    continue;
                }
                if (shown >= 3) {
                    ++shown;
                    continue;
                }
                ++shown;
                ImGui::PushID(job.id);
                ImGui::TextUnformatted(job.name.c_str());
                char overlay[48];
                if (job.state == hapjobs::JobState::Converting) {
                    std::snprintf(overlay, sizeof(overlay), "%d %%", static_cast<int>(progressOf(job) * 100.0f));
                } else {
                    std::snprintf(overlay, sizeof(overlay), "%s", stateLabel(job.state));
                }
                ImGui::ProgressBar(progressOf(job), ImVec2(ImGui::GetFontSize() * 14.0f, 0.0f), overlay);
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Cancel"))) {
                    convert_->cancel(job.id);
                }
                ImGui::PopID();
            }
            if (shown > 3) {
                ImGui::TextDisabled(tr("and %d more"), shown - 3);
            }
            ImGui::TextDisabled("%s", tr("The video keeps playing as it is until the copy is ready."));
            offset += ImGui::GetWindowHeight() + 10.0f;
        }
        ImGui::End();
        endCard();
    }

    const bool hintWanted = ffmpegCapsKnown_ && !ffmpegCaps_.ready() && !settings_.ffmpegHintDismissed && !ffmpegInstaller_.running() && ffmpegCaps_.compiled;
    bool hasMov = false;
    if (hintWanted && ffmpegCaps_.loaded) {
        for (const ClipDef& clip : clipLibrary()) {
            if (clip.kind == ClipKind::VideoFile && hapjobs::isCandidatePath(clip.path)) {
                hasMov = true;
                break;
            }
        }
    }
    if (hintWanted && (!ffmpegCaps_.loaded || hasMov)) {
        beginCard(offset);
        if (ImGui::Begin("##ffmpeghint", nullptr, kCardFlags)) {
            ImGui::PushFont(nullptr, 17.0f);
            ImGui::TextColored(ui::kAccent, "%s", ffmpegCaps_.loaded ? tr("DXV conversion is not ready") : tr("FFmpeg is missing"));
            ImGui::PopFont();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(tr("NyrexVJ needs FFmpeg to play videos and to convert DXV videos to HAP. It can set FFmpeg up for you. Nothing is downloaded until you press the button there."));
            ImGui::PopTextWrapPos();
            if (ui::accentButton(trId("Set up FFmpeg", "ffmpegHintOpen"), true)) {
                openSettings(SettingsPerformance);
            }
            ImGui::SameLine();
            if (ImGui::Button(trId("Not now", "ffmpegHintDismiss"))) {
                settings_.ffmpegHintDismissed = true;
                saveSettings(settings_);
            }
        }
        ImGui::End();
        endCard();
    }
}

void App::drawFfmpegSection() {
    ui::sectionHeader("FFmpeg");
    if (!ffmpegCapsKnown_) {
        ImGui::TextDisabled("%s", tr("Checking FFmpeg..."));
        return;
    }
    const ffmpeg_setup::Capabilities& caps = ffmpegCaps_;
    const ffmpeg_setup::Status install = ffmpegInstaller_.status();
    ImGui::PushTextWrapPos(0.0f);
    if (!caps.compiled) {
        ImGui::TextDisabled("%s", tr("This build of NyrexVJ was made without video support."));
        ImGui::PopTextWrapPos();
        return;
    }
    if (caps.ready()) {
        ImGui::TextColored(kGood, "%s", tr("FFmpeg is ready for video playback and DXV conversion."));
    } else {
        ImGui::TextColored(kWarn, "%s", tr("FFmpeg is missing or incomplete."));
    }
    if (!caps.version.empty()) {
        ImGui::TextDisabled("%s: %s", tr("Version"), caps.version.c_str());
    }
    if (!caps.license.empty()) {
        ImGui::TextDisabled("%s: %s", tr("License"), caps.license.c_str());
    }
    if (!caps.loadedFrom.empty()) {
        ImGui::TextDisabled("%s: %s", tr("Loaded from"), caps.loadedFrom.c_str());
    }
    ImGui::PopTextWrapPos();

    struct Row {
        const char* label;
        bool ok;
    };
    const Row rows[] = {{tr("FFmpeg libraries"), caps.loaded}, {tr("DXV decoder"), caps.dxvDecoder}, {tr("HAP encoder"), caps.hapEncoderWorks}, {tr("QuickTime (MOV) writer"), caps.movMuxer}};
    for (const Row& row : rows) {
        ImGui::TextUnformatted(row.label);
        ImGui::SameLine(ImGui::GetFontSize() * 14.0f);
        ImGui::TextColored(row.ok ? kGood : kWarn, "%s", row.ok ? tr("Works") : tr("Missing"));
    }
    if (caps.loaded && !caps.problem.empty() && !caps.ready()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", trMessage(caps.problem).c_str());
        ImGui::PopTextWrapPos();
    }

    if (install.running) {
        ImGui::Spacing();
        ImGui::TextUnformatted(trMessage(ffmpeg_setup::describeStage(install.stage)).c_str());
        char overlay[64];
        if (install.stage == ffmpeg_setup::Stage::Download && install.total > 0) {
            std::snprintf(overlay, sizeof(overlay), "%s / %s", sizeText(install.done).c_str(), sizeText(install.total).c_str());
        } else {
            std::snprintf(overlay, sizeof(overlay), "%d %%", static_cast<int>(install.progress * 100.0));
        }
        ImGui::ProgressBar(static_cast<float>(install.progress), ImVec2(-FLT_MIN, 0.0f), overlay);
        if (ImGui::Button(trId("Cancel", "ffmpegCancel"))) {
            ffmpegInstaller_.cancel();
        }
        return;
    }

    if (!install.error.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", tr("The setup did not finish."));
        ImGui::TextDisabled("%s", trMessage(install.error).c_str());
        ImGui::PopTextWrapPos();
    } else if (install.cancelled) {
        ImGui::TextDisabled("%s", tr("The setup was cancelled. Nothing was changed."));
    } else if (install.ok && !install.version.empty()) {
        ImGui::TextColored(kGood, "%s %s (%s)", tr("FFmpeg was set up:"), install.version.c_str(), install.license.c_str());
    }

    if (caps.ready()) {
        return;
    }

    if (!ffmpegPackageKnown_) {
        ffmpegPackageKnown_ = ffmpeg_setup::choosePackage(ffmpeg_setup::compiledAvcodecMajor(), ffmpeg_setup::platformKey(), ffmpegPackage_, ffmpegPackageReason_);
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    if (ffmpegPackageKnown_) {
        ImGui::TextUnformatted(tr("NyrexVJ can download a ready-made FFmpeg for you. It comes from the BtbN FFmpeg-Builds project on GitHub. The download is checked against the published checksum and the licence (only LGPL builds are accepted) and is put into the NyrexVJ folder below. Programs that are already installed on your computer are not touched."));
        ImGui::TextDisabled("%s: %s", tr("Version"), ffmpegPackage_.version.c_str());
        ImGui::TextDisabled("%s: %s", tr("Download"), ffmpegPackage_.assetName.c_str());
        ImGui::TextDisabled("%s: %s", tr("Folder"), ffmpegDirectory().c_str());
        ImGui::TextDisabled(tr("Needs about %s of free disk space during setup."), sizeText(ffmpeg_setup::kRequiredFreeBytes).c_str());
    } else {
        ImGui::TextColored(kWarn, "%s", tr("No ready-made FFmpeg is available for this version of NyrexVJ."));
        ImGui::TextDisabled("%s", trMessage(ffmpegPackageReason_).c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(!ffmpegPackageKnown_);
    if (ui::accentButton(trId("Download and set up FFmpeg", "ffmpegInstall"), true)) {
        startFfmpegInstall();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("Pressing this button starts the download. It is the only thing that is downloaded."));
    ImGui::SameLine();
    if (ImGui::Button(trId("Set up by hand", "ffmpegManual"))) {
        ffmpegManualOpen_ = !ffmpegManualOpen_;
    }
    if (ffmpegManualOpen_) {
        const VideoSupport support = videoSupport();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(tr("Get a shared LGPL build of FFmpeg that includes the HAP encoder and the DXV decoder, and copy these files into the folder below. Then press Check again."));
        for (const std::string& file : support.files) {
            ImGui::BulletText("%s", file.c_str());
        }
        ImGui::TextDisabled("%s", ffmpegDirectory().c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton(tr("Open FFmpeg folder"))) {
            std::error_code ec;
            std::filesystem::create_directories(pathFromUtf8(ffmpegDirectory()), ec);
            platform::openFolder(ffmpegDirectory());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Check again"))) {
            retryVideoSupport();
            ffmpegCapsKnown_ = false;
            startFfmpegCheck();
        }
    }
}

void App::drawConversionSettings() {
    if (!convert_) {
        return;
    }
    auto persist = [this]() {
        saveSettings(settings_);
        applyConvertSettings();
    };
    ui::sectionHeader(tr("Convert DXV videos to HAP"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("DXV videos need much more processor time than HAP videos. NyrexVJ can make a HAP copy in the background and play that copy instead. Your original files are never changed."));
    ImGui::PopTextWrapPos();

    const hapjobs::ImportMode mode = hapjobs::importModeFromName(settings_.convertMode);
    const char* modeLabels[] = {tr("Off"), tr("Ask"), tr("Automatic")};
    const hapjobs::ImportMode modes[] = {hapjobs::ImportMode::Off, hapjobs::ImportMode::Ask, hapjobs::ImportMode::Automatic};
    int modeSelected = 0;
    for (int i = 0; i < 3; ++i) {
        if (modes[i] == mode) {
            modeSelected = i;
        }
    }
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ImGui::BeginCombo(trId("Import mode", "optConvertMode"), modeLabels[modeSelected])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(modeLabels[i], i == modeSelected)) {
                settings_.convertMode = hapjobs::importModeName(modes[i]);
                persist();
                convertScanPending_ = modes[i] != hapjobs::ImportMode::Off;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Off never converts. Ask shows a notice and waits for your answer before it converts a DXV video. Automatic converts every DXV video it finds. The video keeps playing as it is until the copy is ready."));

    const hapconv::TargetFormat format = hapconv::targetFromName(settings_.convertFormat);
    const char* formatLabels[] = {tr("Automatic (recommended)"), "HAP", "HAP Alpha", "HAP Q"};
    const hapconv::TargetFormat formats[] = {hapconv::TargetFormat::Auto, hapconv::TargetFormat::Hap, hapconv::TargetFormat::HapAlpha, hapconv::TargetFormat::HapQ};
    int formatSelected = 0;
    for (int i = 0; i < 4; ++i) {
        if (formats[i] == format) {
            formatSelected = i;
        }
    }
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ImGui::BeginCombo(trId("HAP format", "optConvertFormat"), formatLabels[formatSelected])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(formatLabels[i], i == formatSelected)) {
                settings_.convertFormat = hapconv::targetKey(formats[i]);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Automatic picks HAP for videos without transparency, HAP Q for DXV High Quality and HAP Alpha for videos with transparency. HAP Q with transparency is not supported, so videos with transparency always become HAP Alpha. A format you choose here is only used when it can keep the picture as it is."));

    int cache = settings_.convertCacheMb;
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ui::sliderInt(trId("Cache size limit", "optConvertCache"), &cache, 256, 51200, "%d MB", ImGuiSliderFlags_Logarithmic)) {
        settings_.convertCacheMb = cache;
        persist();
        convert_->trimCache();
    }
    ImGui::SetItemTooltip("%s", tr("When the copies need more room, the ones used longest ago are deleted first. Copies that are playing are kept."));

    convertListRefresh_ += ImGui::GetIO().DeltaTime;
    if (convertCacheText_.empty() || convertListRefresh_ > 2.0) {
        convertListRefresh_ = 0.0;
        const hapjobs::CacheStats stats = convert_->cacheStats();
        char text[160];
        std::snprintf(text, sizeof(text), tr("Cache: %s in %d converted videos"), sizeText(stats.bytes).c_str(), stats.entries);
        convertCacheText_ = text;
    }
    ImGui::TextDisabled("%s", convertCacheText_.c_str());
    if (ImGui::SmallButton(tr("Open cache folder"))) {
        std::error_code ec;
        std::filesystem::create_directories(convert_->cacheDirectory(), ec);
        const std::u8string folder = convert_->cacheDirectory().u8string();
        platform::openFolder(std::string(folder.begin(), folder.end()));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Clear cache"))) {
        const int removed = convert_->clearCache();
        convertCacheText_.clear();
        toast(std::string(tr("Converted copies deleted: ")) + std::to_string(removed));
    }
    ImGui::SetItemTooltip("%s", tr("Deletes all converted copies. The original videos are not touched. Videos that use a copy play from the original again once they are loaded again."));
    ImGui::SameLine();
    ImGui::BeginDisabled(!convert_->ready());
    if (ImGui::SmallButton(tr("Convert library now"))) {
        offerLibraryVideos(true);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("Looks for DXV videos in the library and converts them, whatever the import mode is."));

    const std::vector<hapjobs::JobInfo> jobs = convert_->jobs();
    if (!jobs.empty()) {
        ImGui::Spacing();
        if (ImGui::BeginTable("##convertjobs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("Video"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(tr("State"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
            ImGui::TableHeadersRow();
            for (const hapjobs::JobInfo& job : jobs) {
                ImGui::PushID(job.id);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(job.name.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", job.source.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                const bool failed = job.state == hapjobs::JobState::Failed;
                const bool good = job.state == hapjobs::JobState::Done || job.state == hapjobs::JobState::Cached;
                if (job.state == hapjobs::JobState::Converting) {
                    char overlay[32];
                    std::snprintf(overlay, sizeof(overlay), "%d %%", static_cast<int>(progressOf(job) * 100.0f));
                    ImGui::ProgressBar(progressOf(job), ImVec2(-FLT_MIN, 0.0f), overlay);
                } else {
                    ImGui::TextColored(failed ? kWarn : good ? kGood : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", stateLabel(job.state));
                    if (good && !job.format.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s, %s", job.format.c_str(), sizeText(job.bytes).c_str());
                    }
                    if (failed && !job.error.empty()) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("%s", trMessage(job.error).c_str());
                        ImGui::PopTextWrapPos();
                    }
                }
                ImGui::TableSetColumnIndex(2);
                if (job.state == hapjobs::JobState::Waiting) {
                    if (ImGui::SmallButton(tr("Convert"))) {
                        convert_->approve(job.id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Not now"))) {
                        convert_->dismiss(job.id);
                    }
                } else if (!hapjobs::jobIsFinished(job.state)) {
                    if (ImGui::SmallButton(tr("Cancel"))) {
                        convert_->cancel(job.id);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::SmallButton(tr("Clear list"))) {
            convert_->clearFinished();
        }
    }

    drawFfmpegSection();
}

}

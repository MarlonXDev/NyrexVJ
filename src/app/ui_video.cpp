#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "media/video_hardware.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

std::string megabytes(unsigned long long bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

const char* decodePathLabel(DecodePath path) {
    switch (path) {
    case DecodePath::Software:
        return tr("Software (processor)");
    case DecodePath::Hardware:
        return tr("Hardware (graphics card)");
    case DecodePath::HapGpu:
        return tr("HAP, compressed upload");
    case DecodePath::HapCpu:
        return tr("HAP, processor fallback");
    default:
        return "-";
    }
}

const char* uploadRouteLabel(UploadRoute route) {
    switch (route) {
    case UploadRoute::CpuRgba:
        return tr("Processor converts, plain upload");
    case UploadRoute::GpuConvert:
        return tr("Colours converted on the graphics card");
    case UploadRoute::CompressedDirect:
        return tr("Compressed texture, no conversion");
    default:
        return "-";
    }
}

std::string backendText(const VideoStatus& status) {
    if (status.path == DecodePath::Hardware && !status.backend.empty()) {
        return hardwareBackendLabel(status.backend);
    }
    return std::string();
}

void probeCell(const HardwareProbeCell& cell) {
    switch (cell.state) {
    case ProbeState::Supported:
        ImGui::TextColored(kGood, "%s", tr("Works"));
        break;
    case ProbeState::Unsupported:
        ImGui::TextDisabled("%s", tr("Not supported"));
        break;
    case ProbeState::Failed:
        ImGui::TextColored(kWarn, "%s", tr("Failed"));
        break;
    default:
        ImGui::TextDisabled("-");
        break;
    }
    if (!cell.detail.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", trMessage(cell.detail).c_str());
    }
}

}

void App::drawVideoSettings() {
    auto persist = [this]() { saveSettings(settings_); };
    const VideoGpuCaps& caps = engine_.videoCaps();

    ImGui::SeparatorText(tr("Video playback"));
    const VideoDecodeMode mode = videoDecodeModeFromName(settings_.videoDecoder);
    const char* labels[] = {tr("Automatic"), tr("Prefer hardware"), tr("Force software")};
    const VideoDecodeMode modes[] = {VideoDecodeMode::Auto, VideoDecodeMode::PreferHardware, VideoDecodeMode::ForceSoftware};
    int selected = 0;
    for (int i = 0; i < 3; ++i) {
        if (modes[i] == mode) {
            selected = i;
        }
    }
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo(trId("Video decoder", "optVideoDecoder"), labels[selected])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(labels[i], i == selected)) {
                settings_.videoDecoder = videoDecodeModeName(modes[i]);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Automatic lets the graphics card decode large H.264, HEVC, AV1 and VP9 videos and keeps small ones on the processor. Prefer hardware always tries the graphics card first. Force software never uses it. If the graphics card cannot decode a video, the processor takes over by itself."));

    if (ImGui::Checkbox(trId("Convert video colours on the graphics card", "optVideoGpuConvert"), &settings_.videoGpuConvert)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("The processor only hands over the raw picture and the graphics card turns it into colours. This saves a lot of processor time. Turn it off only if a video shows wrong colours."));

    ImGui::BeginDisabled(!caps.compressedTextures);
    if (ImGui::Checkbox(trId("Upload HAP videos compressed", "optHapDirect"), &settings_.hapDirect)) {
        persist();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("HAP, HAP Alpha and HAP Q videos are sent to the graphics card as they are, without unpacking them into colours first. This is the cheapest way to play videos."));
    if (!caps.compressedTextures) {
        ImGui::TextColored(kWarn, "%s", tr("This graphics card cannot take compressed textures, HAP videos use the processor."));
    }

    if (caps.detected) {
        ImGui::TextDisabled("%s: %s", tr("Colour conversion on the graphics card"), caps.conversion ? tr("available") : tr("not available"));
        ImGui::TextDisabled("%s: %s", tr("Compressed textures (HAP)"), caps.compressedTextures ? tr("available") : tr("not available"));
        ImGui::TextDisabled("%s: %s", tr("Pixel buffers"), caps.pixelBuffers ? tr("available") : tr("not available"));
    }

    const bool running = hardwareProbe_.valid();
    if (running && hardwareProbe_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        hardwareReport_ = hardwareProbe_.get();
        hasHardwareReport_ = true;
    }
    ImGui::BeginDisabled(hardwareProbe_.valid());
    if (ImGui::Button(trId("Test hardware decoding", "optHardwareTest"))) {
        hardwareProbe_ = std::async(std::launch::async, [] { return probeHardwareDecoding(); });
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("Tries to decode a tiny test video of each type on every graphics card interface. This only takes a moment."));
    if (hardwareProbe_.valid()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("Testing..."));
    }

    if (!hasHardwareReport_) {
        return;
    }
    if (!hardwareReport_.ffmpegAvailable) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", hardwareReport_.problem.empty() ? tr("The FFmpeg libraries are missing, so videos cannot be tested.") : trMessage(hardwareReport_.problem).c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    if (ImGui::BeginTable("##hwmatrix", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Interface"), ImGuiTableColumnFlags_WidthStretch, 1.8f);
        for (size_t i = 0; i < kProbeCodecCount; ++i) {
            ImGui::TableSetupColumn(probeCodecLabel(i));
        }
        ImGui::TableHeadersRow();
        for (const HardwareProbeRow& row : hardwareReport_.rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.backend == "software" ? tr("Processor (software)") : row.label.c_str());
            if (!row.detail.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", trMessage(row.detail).c_str());
            }
            for (size_t i = 0; i < kProbeCodecCount; ++i) {
                ImGui::TableSetColumnIndex(static_cast<int>(i) + 1);
                probeCell(row.codecs[i]);
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", tr("Hover over an entry to see why it does not work."));
}

void App::drawVideoMonitor() {
    const std::vector<const MediaPlayer*> videos = engine_.activeVideos();
    ImGui::SeparatorText(tr("Videos"));
    if (videos.empty()) {
        ImGui::TextDisabled("%s", tr("No video is loaded right now."));
        return;
    }
    if (ImGui::BeginTable("##videos", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Clip"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(tr("Decoding"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(tr("Decode"));
        ImGui::TableSetupColumn(tr("Upload"));
        ImGui::TableSetupColumn(tr("Dropped"));
        ImGui::TableSetupColumn(tr("Late"));
        ImGui::TableSetupColumn(tr("Memory"));
        ImGui::TableHeadersRow();
        for (const MediaPlayer* player : videos) {
            const VideoStatus status = player->status();
            const UploadStats upload = player->uploadStats();
            const VideoMemory memory = player->videoMemory();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(player->label().c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s: %s", tr("Codec"), status.codec.c_str());
                ImGui::Text("%s: %d x %d", tr("Size"), player->videoInfo().width, player->videoInfo().height);
                if (!status.pixelFormat.empty()) {
                    ImGui::Text("%s: %s", tr("Picture format"), status.pixelFormat.c_str());
                }
                ImGui::Text("%s: %d", tr("Decoder threads"), status.threads);
                ImGui::Text("%s: %s", tr("Upload"), uploadRouteLabel(player->uploadRoute()));
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(1);
            const bool fallback = !status.fallbackReason.empty();
            ImGui::TextColored(fallback ? kWarn : kGood, "%s", decodePathLabel(status.path));
            std::string detail = status.decoder;
            const std::string backend = backendText(status);
            if (!backend.empty()) {
                detail += "  (" + backend + ")";
            }
            if (!status.variant.empty() && (status.path == DecodePath::HapGpu || status.path == DecodePath::HapCpu)) {
                detail = status.variant + (detail.empty() ? "" : "  -  " + detail);
            }
            if (!detail.empty()) {
                ImGui::TextDisabled("%s", detail.c_str());
            }
            if (fallback) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 22.0f);
                ImGui::TextDisabled("%s: %s", tr("Why"), trMessage(status.fallbackReason).c_str());
                ImGui::PopTextWrapPos();
                if (!status.fallbackDetail.empty() && ImGui::IsItemHovered()) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                    ImGui::SetTooltip("%s", status.fallbackDetail.c_str());
                    ImGui::PopTextWrapPos();
                }
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f ms", status.decodeMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f ms", upload.averageMs);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%llu", status.dropped);
            ImGui::TableSetColumnIndex(5);
            if (status.underruns > 0) {
                ImGui::TextColored(kWarn, "%llu", status.underruns);
            } else {
                ImGui::Text("0");
            }
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(megabytes(memory.total()).c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s: %s", tr("Pictures"), megabytes(memory.output).c_str());
                ImGui::Text("%s: %s", tr("Colour planes"), megabytes(memory.planes).c_str());
                ImGui::Text("%s: %s", tr("Compressed (HAP)"), megabytes(memory.compressed).c_str());
                ImGui::Text("%s: %s", tr("Upload buffers"), megabytes(memory.staging).c_str());
                ImGui::EndTooltip();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", tr("Decode is the processor time per picture. Dropped pictures were skipped to keep up. Late means a picture was not ready in time."));
}

}

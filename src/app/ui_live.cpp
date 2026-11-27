#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/ndi_receiver.hpp"
#include "core/ndi_runtime.hpp"
#include "core/stream_url.hpp"
#include "media/stream_capture.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

struct ModeChoice {
    int width;
    int height;
    const char* label;
};

constexpr ModeChoice kCameraModes[] = {
    {0, 0, "Auto"},
    {640, 480, "640 x 480"},
    {1280, 720, "1280 x 720"},
    {1920, 1080, "1920 x 1080"},
    {2560, 1440, "2560 x 1440"},
    {3840, 2160, "3840 x 2160"},
};

constexpr int kFrameRates[] = {0, 24, 25, 30, 50, 60, 120};

const char* stateText(LiveState state) {
    switch (state) {
    case LiveState::Searching:
        return tr("Searching");
    case LiveState::Connecting:
        return tr("Connecting");
    case LiveState::Live:
        return tr("Live");
    case LiveState::SignalLost:
        return tr("Signal lost");
    case LiveState::Reconnecting:
        return tr("Reconnecting");
    default:
        return tr("Idle");
    }
}

ImVec4 stateColor(LiveState state) {
    switch (state) {
    case LiveState::Live:
        return ui::kAccent;
    case LiveState::SignalLost:
    case LiveState::Reconnecting:
        return ImVec4(1.0f, 0.55f, 0.2f, 1.0f);
    default:
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

void drawStatus(const LiveInputInfo& info) {
    const LiveStatus& status = info.status;
    if (!info.active) {
        ImGui::TextDisabled("%s", tr("Idle: the source is opened when a layer shows this clip."));
        if (!info.startError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", trMessage(info.startError).c_str());
            ImGui::PopStyleColor();
        }
        return;
    }
    ImGui::TextColored(stateColor(status.state), "%s", stateText(status.state));
    if (!status.source.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status.source.c_str());
    }
    if (status.state == LiveState::Live) {
        ImGui::TextDisabled("%d x %d, %.0f fps", status.width, status.height, status.fps);
        ImGui::TextDisabled("%s %.0f ms (%s %.0f ms)", tr("Latency"), info.latencyMs, tr("worst"), info.latencyMaxMs);
        ImGui::TextDisabled("%s %.1f ms, %s %llu", tr("Upload"), info.uploadMs, tr("skipped"), status.dropped);
        if (status.jitterMs > 0.0 || status.resyncs > 0) {
            ImGui::TextDisabled("%s %.1f ms, %s %llu", tr("Jitter"), status.jitterMs, tr("catch-ups"), status.resyncs);
        }
        if (!status.detail.empty()) {
            ImGui::TextDisabled("%s", status.detail.c_str());
        }
    } else if (status.attempts > 0) {
        ImGui::TextDisabled("%s %d", tr("Attempt"), status.attempts);
    }
    if (!status.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.2f, 1.0f));
        ImGui::TextWrapped("%s", trMessage(status.error).c_str());
        ImGui::PopStyleColor();
    }
}

}

void App::serviceLiveBrowse() {
    if (ndiBrowsing_ && ImGui::GetTime() - ndiBrowseTouch_ > 4.0) {
        NdiFinder::instance().release();
        ndiBrowsing_ = false;
    }
}

void App::drawStreamControls(int slot) {
    auto persist = [this]() { saveSettings(settings_); };
    if (ImGui::Checkbox(trId("Allow network stream inputs", "streamAllow"), &settings_.streamInputEnabled)) {
        persist();
    }
    ImGui::PushTextWrapPos(0.0f);
    if (!settings_.streamInputEnabled) {
        ImGui::TextDisabled("%s", tr("Off: the four stream clips stay empty and no network connection is made. Turn this on only if you want to receive RTSP, SRT or UDP video."));
        ImGui::PopTextWrapPos();
        return;
    }
    const StreamSupport support = streamSupport();
    if (!support.compiled || !support.ffmpeg) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("Network streams are not available"));
        ImGui::TextDisabled("%s", tr("Streams need the FFmpeg libraries. See Settings, Video, for the files and the folder."));
        if (!support.problem.empty()) {
            ImGui::TextDisabled("%s", trMessage(support.problem).c_str());
        }
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::TextDisabled("%s", tr("Streams from the network are untrusted data. Only open addresses you know. Allowed: rtsp://, srt:// and udp://."));
    ImGui::PopTextWrapPos();

    const size_t index = static_cast<size_t>(slot);
    char* buffer = streamText_[index].data();
    if (!streamTyping_[index]) {
        const std::string& stored = settings_.streamInputs[index];
        std::snprintf(buffer, streamText_[index].size(), "%s", stored.c_str());
    }
    const bool hidden = stream::maskUrl(buffer) != buffer;
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (hidden) {
        flags |= ImGuiInputTextFlags_Password;
    }
    ImGui::SetNextItemWidth(ui::px(-90.0f));
    const bool entered = ImGui::InputText(trId("Address", "streamUrl"), buffer, streamText_[index].size(), flags);
    const bool active = ImGui::IsItemActive();
    const bool finished = entered || ImGui::IsItemDeactivatedAfterEdit();
    streamTyping_[index] = active && !finished;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Examples: rtsp://192.168.1.20:8554/live, srt://host:9000, srt://:9000?mode=listener, udp://@:5000"));
    }
    if (finished) {
        const std::string text = buffer;
        const stream::ParseResult parsed = stream::parseUrl(text);
        if (text.empty()) {
            settings_.streamInputs[index].clear();
            streamProblem_[index].clear();
            persist();
        } else if (parsed.ok) {
            settings_.streamInputs[index] = text;
            streamProblem_[index].clear();
            persist();
        } else {
            streamProblem_[index] = parsed.error;
        }
    }
    if (!streamProblem_[index].empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", trMessage(streamProblem_[index]).c_str());
        ImGui::PopStyleColor();
    }
    const std::string& stored = settings_.streamInputs[index];
    if (!stored.empty()) {
        ImGui::TextDisabled("%s %s", tr("In use:"), stream::maskUrl(stored).c_str());
        const stream::ParseResult parsed = stream::parseUrl(stored);
        if (parsed.ok && parsed.url.listener) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s %d. %s", tr("This opens port"), parsed.url.port, tr("Anyone who can reach this computer on that port can send a picture. Use it only on networks you trust and allow the port in your firewall on purpose."));
            ImGui::PopTextWrapPos();
        }
    }

    const char* const profileNames[] = {tr("Low latency"), tr("Balanced"), tr("Smooth")};
    const int profile = std::clamp(settings_.streamProfile, 0, 2);
    ImGui::SetNextItemWidth(ui::px(-90.0f));
    if (ui::beginCombo(trId("Profile", "streamProfile"), profileNames[profile])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(profileNames[i], profile == i)) {
                settings_.streamProfile = i;
                persist();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Low latency keeps almost no buffer and may stutter on a bad network. Smooth buffers about a second and rides out bursts. Balanced sits between."));
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("A stream has no audio here, only the picture. When the sender stops, the picture freezes and the input reconnects by itself."));
    ImGui::PopTextWrapPos();
}

void App::drawLiveInputControls(const ClipDef& def) {
    const int slot = def.sceneIndex;
    if (slot < 0 || slot >= kLiveInputSlots) {
        return;
    }
    const bool ndi = def.kind == ClipKind::NdiSource;
    const bool plugin = def.kind == ClipKind::PluginSource;
    const bool network = def.kind == ClipKind::StreamSource;
    ui::sectionHeader(network ? tr("Network stream input") : (plugin ? tr("Plugin source") : (ndi ? tr("NDI input") : tr("Camera or capture card"))));
    auto persist = [this]() { saveSettings(settings_); };

    if (network) {
        drawStreamControls(slot);
    } else if (plugin) {
        drawPluginFrameSlot(slot);
    } else if (ndi) {
        if (!ndi::available()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("NDI runtime not found"));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", tr("Install the free NDI Tools or the NDI Runtime from ndi.video. NyrexVJ does not ship the NDI library."));
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton(tr("Download NDI Tools"))) {
                platform::openUrl("https://ndi.video/tools/");
            }
            return;
        }
        ndiBrowseTouch_ = ImGui::GetTime();
        if (!ndiBrowsing_ && NdiFinder::instance().acquire()) {
            ndiBrowsing_ = true;
        }
        std::string& wanted = settings_.ndiInputs[static_cast<size_t>(slot)];
        const std::vector<NdiSourceInfo> sources = NdiFinder::instance().sources();
        const std::string preview = wanted.empty() ? std::string(tr("Automatic")) : wanted;
        ImGui::SetNextItemWidth(ui::px(-90.0f));
        if (ui::beginCombo(trId("NDI source", "ndiPick"), preview.c_str())) {
            if (ImGui::Selectable(tr("Automatic"), wanted.empty())) {
                wanted.clear();
                persist();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr("Uses the first NDI source found that is not the NyrexVJ output."));
            }
            for (const NdiSourceInfo& source : sources) {
                if (ImGui::Selectable(source.name.c_str(), wanted == source.name)) {
                    wanted = source.name;
                    persist();
                }
            }
            if (!wanted.empty()) {
                bool listed = false;
                for (const NdiSourceInfo& source : sources) {
                    listed = listed || source.name == wanted;
                }
                if (!listed) {
                    ImGui::Selectable(wanted.c_str(), true, ImGuiSelectableFlags_Disabled);
                }
            }
            if (sources.empty()) {
                ImGui::TextDisabled("%s", tr("No NDI sources found"));
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("%s %zu", tr("Sources on the network:"), sources.size());
    } else {
        const CameraSupport support = cameraSupport();
        if (!support.compiled || !support.ffmpeg || !support.devices) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("Camera support is not available"));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", tr("Cameras need the FFmpeg libraries including the device library. See Settings, Video, for the files and the folder."));
            ImGui::PopTextWrapPos();
            if (!support.problem.empty()) {
                ImGui::TextDisabled("%s", support.problem.c_str());
            }
            return;
        }
        if (ImGui::GetTime() - cameraListedAt_ > 30.0 || (cameraList_.empty() && ImGui::GetTime() - cameraListedAt_ > 5.0)) {
            cameraList_ = listCameras();
            cameraListedAt_ = ImGui::GetTime();
        }
        std::string& wanted = settings_.cameraInputs[static_cast<size_t>(slot)];
        std::string preview = tr("None");
        for (const CameraDevice& device : cameraList_) {
            if (device.id == wanted) {
                preview = device.name;
            }
        }
        if (!wanted.empty() && preview == tr("None")) {
            preview = wanted;
        }
        ImGui::SetNextItemWidth(ui::px(-90.0f));
        if (ui::beginCombo(trId("Camera", "cameraPick"), preview.c_str())) {
            if (ImGui::Selectable(tr("None"), wanted.empty())) {
                wanted.clear();
                persist();
            }
            for (const CameraDevice& device : cameraList_) {
                if (ImGui::Selectable(device.name.c_str(), wanted == device.id)) {
                    wanted = device.id;
                    persist();
                }
            }
            if (cameraList_.empty()) {
                ImGui::TextDisabled("%s", tr("No cameras found"));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(trId("Refresh", "cameraRefresh"))) {
            cameraList_ = listCameras();
            cameraListedAt_ = ImGui::GetTime();
        }

        std::string resolutionLabel = tr("Auto");
        for (const ModeChoice& choice : kCameraModes) {
            if (choice.width == settings_.cameraWidth && choice.height == settings_.cameraHeight) {
                resolutionLabel = choice.width == 0 ? std::string(tr("Auto")) : std::string(choice.label);
            }
        }
        ImGui::SetNextItemWidth(ui::px(-90.0f));
        if (ui::beginCombo(trId("Resolution", "cameraResolution"), resolutionLabel.c_str())) {
            for (const ModeChoice& choice : kCameraModes) {
                const bool selected = choice.width == settings_.cameraWidth && choice.height == settings_.cameraHeight;
                if (ImGui::Selectable(choice.width == 0 ? tr("Auto") : choice.label, selected)) {
                    settings_.cameraWidth = choice.width;
                    settings_.cameraHeight = choice.height;
                    persist();
                }
            }
            ImGui::EndCombo();
        }
        char rate[24];
        if (settings_.cameraFps <= 0) {
            std::snprintf(rate, sizeof(rate), "%s", tr("Auto"));
        } else {
            std::snprintf(rate, sizeof(rate), "%d fps", settings_.cameraFps);
        }
        ImGui::SetNextItemWidth(ui::px(-90.0f));
        if (ui::beginCombo(trId("Frame rate", "cameraRate"), rate)) {
            for (const int fps : kFrameRates) {
                char label[24];
                if (fps == 0) {
                    std::snprintf(label, sizeof(label), "%s", tr("Auto"));
                } else {
                    std::snprintf(label, sizeof(label), "%d fps", fps);
                }
                if (ImGui::Selectable(label, settings_.cameraFps == fps)) {
                    settings_.cameraFps = fps;
                    persist();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("The resolution and frame rate are requested from the device. Cameras offer fixed modes, so the closest one is used."));
        ImGui::PopTextWrapPos();
    }

    drawStatus(engine_.liveInfo(network ? LiveKind::Stream : (plugin ? LiveKind::Plugin : (ndi ? LiveKind::Ndi : LiveKind::Camera)), slot));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("The source is closed three seconds after no layer shows this clip, so it costs nothing while unused."));
    ImGui::PopTextWrapPos();
}

}

#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/optical_flow.hpp"
#include "render/flow_session.hpp"
#include "render/media_player.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kFlowGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kFlowWarn(1.0f, 0.65f, 0.2f, 1.0f);

const char* modeLabel(flow::Mode mode) {
    switch (mode) {
    case flow::Mode::Interpolate:
        return tr("Interpolate frames");
    case flow::Mode::MotionBlur:
        return tr("Motion blur");
    case flow::Mode::Vectors:
        return tr("Show motion vectors");
    default:
        return tr("Off");
    }
}

const char* tierLabel(flow::Tier tier) {
    switch (tier) {
    case flow::Tier::Fast:
        return tr("Fast");
    case flow::Tier::Fine:
        return tr("Fine");
    default:
        return tr("Balanced");
    }
}

std::string megabytesText(unsigned long long bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

}

void App::drawFlowSettings() {
    auto persist = [this]() { saveSettings(settings_); };

    ImGui::SeparatorText(tr("Optical flow"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Estimates how the picture moves between two video frames. It is experimental and off by default, while it is off the videos play exactly as before and nothing extra is created."));
    ImGui::PopTextWrapPos();

    const flow::Mode mode = flow::modeFromName(settings_.flowMode, flow::Mode::Off);
    ImGui::SetNextItemWidth(ui::px(260.0f));
    if (ImGui::BeginCombo(trId("Optical flow mode (experimental)", "optFlowMode"), modeLabel(mode))) {
        for (int i = 0; i < flow::kModeCount; ++i) {
            const flow::Mode entry = static_cast<flow::Mode>(i);
            if (ImGui::Selectable(modeLabel(entry), entry == mode)) {
                settings_.flowMode = flow::modeName(entry);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Interpolate frames creates the pictures between two real frames, for example 30 frames per second shown smoothly on a 60 Hz output. Motion blur smears the picture along its motion. Show motion vectors draws the estimated motion as colour, which is meant for checking. Videos that play backwards and sources the analysis cannot read, for example compressed HAP pictures, are shown without it."));
    if (mode == flow::Mode::Off) {
        return;
    }

    const flow::Tier tier = flow::tierFromName(settings_.flowQuality, flow::Tier::Balanced);
    ImGui::SetNextItemWidth(ui::px(260.0f));
    if (ImGui::BeginCombo(trId("Quality", "optFlowQuality"), tierLabel(tier))) {
        for (int i = 0; i < flow::kTierCount; ++i) {
            const flow::Tier entry = static_cast<flow::Tier>(i);
            if (ImGui::Selectable(tierLabel(entry), entry == tier)) {
                settings_.flowQuality = flow::tierName(entry);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("The processor looks at a small copy of the picture on its own thread. Fast uses about 160 pixels width, Balanced about 240 and Fine about 360. Finer levels find small and quick movement better and need more time. If the analysis cannot keep up it drops to the next lower level by itself and switches off at the lowest one."));

    int budget = settings_.flowBudgetMb;
    ImGui::SetNextItemWidth(ui::px(260.0f));
    if (ImGui::SliderInt(trId("Graphics memory limit (MB)", "optFlowBudget"), &budget, flow::kMinBudgetMb, flow::kMaxBudgetMb)) {
        settings_.flowBudgetMb = flow::clampBudgetMb(budget);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("All videos together may use at most this much graphics memory for optical flow. A video that would need more is shown without it and says why."));

    if (mode == flow::Mode::MotionBlur) {
        int shutter = settings_.flowShutterPercent;
        ImGui::SetNextItemWidth(ui::px(260.0f));
        if (ImGui::SliderInt(trId("Shutter (%)", "optFlowShutter"), &shutter, 0, 100, "%d %%")) {
            settings_.flowShutterPercent = std::clamp(shutter, 0, 100);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("How long the virtual shutter stays open. 0 gives no blur, 100 smears over the whole time between two frames."));
    }

    ImGui::TextDisabled("%s: %s / %s", tr("Graphics memory in use for optical flow"), megabytesText(flow::globalBudget().used()).c_str(), megabytesText(flow::globalBudget().limit()).c_str());

    const std::vector<const MediaPlayer*> videos = engine_.activeVideos();
    if (videos.empty()) {
        ImGui::TextDisabled("%s", tr("No video is loaded right now."));
        return;
    }
    if (ImGui::BeginTable("##flowvideos", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Clip"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(tr("State"), ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn(tr("Quality"));
        ImGui::TableSetupColumn(tr("Analysis"));
        ImGui::TableSetupColumn(tr("Pictures"));
        ImGui::TableSetupColumn(tr("Memory"));
        ImGui::TableHeadersRow();
        for (const MediaPlayer* player : videos) {
            const FlowStatus status = player->flowStatus();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(player->label().c_str());
            ImGui::TableSetColumnIndex(1);
            const bool plain = status.state == "off" || status.state == "waiting for the first picture";
            ImGui::TextColored(status.engaged ? kFlowGood : (plain ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kFlowWarn), "%s", trMessage(status.state).c_str());
            if (!status.reason.empty() && status.state != "off") {
                std::string why = trMessage(status.reason);
                if (status.neededBytes > 0 && status.state == "plain playback") {
                    why += " (" + megabytesText(status.neededBytes) + " " + tr("needed") + ")";
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 22.0f);
                ImGui::TextDisabled("%s: %s", tr("Why"), why.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(tierLabel(status.current));
            if (status.disabled) {
                ImGui::TextColored(kFlowWarn, "%s", tr("switched off, too slow"));
            } else if (status.requested != status.current) {
                ImGui::TextColored(kFlowWarn, "%s %s", tr("lowered from"), tierLabel(status.requested));
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f ms", status.analysisAverageMs);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(tr("Analysis time per picture pair: %.2f ms on average, %.2f ms for the 95th percentile. Analysis size: %d x %d pixels."), status.analysisAverageMs, status.analysisP95Ms, status.analysisWidth, status.analysisHeight);
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%llu / %llu", status.synthesised, status.held);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(tr("Made by optical flow: %llu. Shown unchanged: %llu. Hard cuts found: %llu. Unsuitable pictures: %llu. Quality lowered: %llu times."), status.synthesised, status.held, status.cuts, status.unusable, status.tierDrops);
            }
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(megabytesText(status.gpuBytes).c_str());
        }
        ImGui::EndTable();
    }
}

}

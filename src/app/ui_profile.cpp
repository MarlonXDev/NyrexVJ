#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/render_policy.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

std::string passLabel(const ProfileRow& row) {
    char text[160];
    const char* prefix = row.deck == 1 ? tr("Outgoing deck") : "";
    const char* separator = row.deck == 1 ? ": " : "";
    switch (row.kind) {
    case PassKind::Source:
        std::snprintf(text, sizeof(text), "%s%s%s %d: %s", prefix, separator, tr("Layer"), row.layer, row.name.c_str());
        break;
    case PassKind::Effect:
        if (row.layer > 0) {
            std::snprintf(text, sizeof(text), "%s%s%s %d, %s %d: %s", prefix, separator, tr("Layer"), row.layer, tr("effect"), row.slot, row.name.c_str());
        } else if (row.layer == 0) {
            std::snprintf(text, sizeof(text), "%s%s%s, %s %d: %s", prefix, separator, tr("Master"), tr("effect"), row.slot, row.name.c_str());
        } else {
            std::snprintf(text, sizeof(text), "%s, %s %d: %s", tr("Cue preview"), tr("effect"), row.slot, row.name.c_str());
        }
        break;
    case PassKind::Composite:
        std::snprintf(text, sizeof(text), "%s%s%s %d: %s", prefix, separator, tr("Layer"), row.layer, tr("blend into the picture"));
        break;
    case PassKind::Preview:
        if (row.layer > 0) {
            std::snprintf(text, sizeof(text), "%s %d: %s", tr("Layer"), row.layer, tr("small preview"));
        } else {
            std::snprintf(text, sizeof(text), "%s: %s", tr("Cue preview"), row.name.c_str());
        }
        break;
    case PassKind::Final:
        std::snprintf(text, sizeof(text), "%s", tr("Final picture (level and grade)"));
        break;
    case PassKind::Transition:
        std::snprintf(text, sizeof(text), "%s", tr("Deck transition"));
        break;
    case PassKind::Housekeeping:
        std::snprintf(text, sizeof(text), "%s", tr("Uploads, thumbnails and housekeeping"));
        break;
    default:
        std::snprintf(text, sizeof(text), "%s", row.name.empty() ? tr("Other") : row.name.c_str());
        break;
    }
    return text;
}

}

void App::drawPassProfiler() {
    ImGui::SeparatorText(tr("Where the GPU time goes"));
    if (!engine_.gpuTimingAvailable()) {
        ImGui::TextDisabled("%s", tr("GPU timing is not available on this graphics card."));
        return;
    }
    bool profiling = engine_.profiling();
    if (ImGui::Checkbox(trId("Measure every render pass", "optProfile"), &profiling)) {
        engine_.setProfiling(profiling);
    }
    ImGui::SetItemTooltip("%s", tr("Times every render pass with GPU timers and shows which clip or effect costs the most. Costs a little speed, so it is off by default and not saved."));
    if (!profiling) {
        return;
    }
    const std::vector<ProfileRow> rows = engine_.profiler().table().sorted();
    if (rows.empty()) {
        ImGui::TextDisabled("%s", tr("Measuring..."));
        return;
    }
    const double total = engine_.profiler().table().averageTotalMs();
    const double budget = budgetMilliseconds(settings_.perfBudgetFps);
    ImGui::Text("%s: %.2f ms", tr("GPU time per frame"), total);
    ImGui::SameLine();
    ImGui::TextDisabled("(%.0f %% %s)", budget > 0.0 ? total / budget * 100.0 : 0.0, tr("of the frame budget"));
    const ProfileRow& top = rows.front();
    if (top.share >= 0.25 && rows.size() > 2) {
        ImGui::TextColored(kWarn, "%s: %s (%.2f ms, %.0f %%)", tr("Biggest cost"), passLabel(top).c_str(), top.averageMs, top.share * 100.0);
    }
    if (ImGui::BeginTable("##passes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Pass"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupColumn(tr("Time"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn(tr("Share"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        const size_t limit = std::min<size_t>(rows.size(), 14);
        for (size_t i = 0; i < limit; ++i) {
            const ProfileRow& row = rows[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(passLabel(row).c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f ms", row.averageMs);
            ImGui::TableSetColumnIndex(2);
            char overlay[16];
            std::snprintf(overlay, sizeof(overlay), "%.0f %%", row.share * 100.0);
            ImGui::ProgressBar(static_cast<float>(row.share), ImVec2(-1.0f, ImGui::GetTextLineHeight()), overlay);
        }
        ImGui::EndTable();
    }
    if (rows.size() > 14) {
        ImGui::TextDisabled("%d %s", static_cast<int>(rows.size() - 14), tr("smaller passes not shown"));
    }
}

}

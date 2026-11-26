#include "app/app.hpp"

#include "app/theme.hpp"
#include "app/ui_common.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

double megabytes(unsigned long long bytes) {
    return static_cast<double>(bytes) / static_cast<double>(gpudiag::kMiB);
}

ImVec4 severityColor(gpudiag::Severity severity) {
    switch (severity) {
    case gpudiag::Severity::Critical:
        return ImVec4(1.0f, 0.38f, 0.32f, 1.0f);
    case gpudiag::Severity::Warning:
        return ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
    case gpudiag::Severity::Info:
        break;
    }
    return ImVec4(0.62f, 0.75f, 0.95f, 1.0f);
}

std::string describe(const gpudiag::Warning& warning, const Diagnostics& d) {
    char text[320];
    text[0] = '\0';
    switch (warning.code) {
    case gpudiag::WarningCode::FrameTime:
        std::snprintf(text, sizeof(text), tr("Slow frames: %.1f ms for the slowest 1 %%, the limit is %.1f ms"), warning.value, warning.limit);
        break;
    case gpudiag::WarningCode::DroppedFrames:
        std::snprintf(text, sizeof(text), tr("Dropped frames in the last minute: %d (limit %d)"), static_cast<int>(warning.value), static_cast<int>(warning.limit));
        break;
    case gpudiag::WarningCode::MemoryNearBudget:
        std::snprintf(text, sizeof(text), tr("Graphics memory is close to the budget: %.0f of %.0f MB"), megabytes(d.estimateBytes), megabytes(d.budgetBytes));
        break;
    case gpudiag::WarningCode::MemoryOverBudget:
        std::snprintf(text, sizeof(text), tr("Graphics memory is over the budget: %.0f of %.0f MB"), megabytes(d.estimateBytes), megabytes(d.budgetBytes));
        break;
    case gpudiag::WarningCode::ResidentOverBudget:
        std::snprintf(text, sizeof(text), tr("What cannot be released (%.0f MB) is already over the budget (%.0f MB). Use a bigger budget or a lower resolution."), warning.value / static_cast<double>(gpudiag::kMiB), warning.limit / static_cast<double>(gpudiag::kMiB));
        break;
    case gpudiag::WarningCode::DriverMemoryLow:
        std::snprintf(text, sizeof(text), tr("The graphics card has little free memory: %.0f MB free"), warning.value / static_cast<double>(gpudiag::kMiB));
        break;
    case gpudiag::WarningCode::DriverEvictions:
        std::snprintf(text, sizeof(text), tr("The driver moved video memory to system memory %d times in the last 30 seconds"), static_cast<int>(warning.value));
        break;
    case gpudiag::WarningCode::UploadTraffic:
        std::snprintf(text, sizeof(text), tr("Video upload is very high: %.0f MB/s"), warning.value / static_cast<double>(gpudiag::kMiB));
        break;
    }
    return text;
}

}

void App::drawDiagnosticsBadge() {
    const std::vector<gpudiag::Warning>& active = diagMonitor_->tracker().active();
    ImGui::TextColored(severityColor(diagMonitor_->tracker().worst()), "%s: %d", tr("Warnings"), static_cast<int>(active.size()));
    if (ImGui::BeginItemTooltip()) {
        const Diagnostics& d = engine_.diagnostics();
        for (const gpudiag::Warning& warning : active) {
            ImGui::TextColored(severityColor(warning.severity), "%s", describe(warning, d).c_str());
        }
        ImGui::EndTooltip();
    }
}

void App::drawGpuDiagnostics() {
    auto persist = [this]() { saveSettings(settings_); };
    const Diagnostics& d = engine_.diagnostics();

    ImGui::SeparatorText(tr("Graphics memory and traffic"));
    ImGui::TextUnformatted(tr("Measured by the graphics driver"));
    if (d.driver.measured()) {
        if (d.driver.hasTotal) {
            ImGui::Text(tr("In use by all programs: %.0f of %.0f MB (%.0f MB free)"), megabytes(d.driver.usedBytes()), megabytes(d.driver.totalBytes), megabytes(d.driver.freeBytes));
        } else {
            ImGui::Text(tr("Free: %.0f MB (this driver does not report the total)"), megabytes(d.driver.freeBytes));
        }
        if (d.driver.hasEvictions) {
            ImGui::Text(tr("Moved to system memory by the driver: %d times, %.0f MB (%d in the last 30 seconds)"), static_cast<int>(d.driver.evictionCount), megabytes(d.driver.evictedBytes), static_cast<int>(diagMonitor_->evictions().recentCount()));
        }
        ImGui::TextDisabled("%s", gpudiag::memorySourceName(d.driver.source));
    } else {
        ImGui::TextWrapped("%s", tr("This graphics driver does not report usable video memory numbers (it needs GL_NVX_gpu_memory_info or GL_ATI_meminfo, and software renderers give meaningless values), so only the estimate below exists."));
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Estimated by NyrexVJ (counted from what it allocated)"));
    ImGui::Text(tr("%.0f MB in total: %.0f MB cannot be released now, %.0f MB belong to clips that are not playing"), megabytes(d.estimateBytes), megabytes(d.residentBytes), megabytes(d.evictableBytes));

    ImGui::Spacing();
    int budgetMb = settings_.gpuMemoryBudgetMb;
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ImGui::DragInt(trId("Memory budget (0 = off)", "optGpuBudget"), &budgetMb, 32.0f, 0, gpudiag::kMaxBudgetMb, "%d MB")) {
        if (budgetMb > 0 && budgetMb < gpudiag::kMinBudgetMb) {
            budgetMb = settings_.gpuMemoryBudgetMb >= gpudiag::kMinBudgetMb ? 0 : gpudiag::kMinBudgetMb;
        }
        settings_.gpuMemoryBudgetMb = std::max(budgetMb, 0);
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("The most graphics memory NyrexVJ should use for clips, scenes and buffers. 0 turns the budget off and nothing changes. The number is an estimate of what NyrexVJ itself allocated; it cannot see other programs such as VRChat."));
    if (settings_.gpuMemoryBudgetMb > 0) {
        const double budgetBytes = static_cast<double>(settings_.gpuMemoryBudgetMb) * static_cast<double>(gpudiag::kMiB);
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), tr("%.0f of %.0f MB"), megabytes(d.estimateBytes), megabytes(static_cast<unsigned long long>(budgetBytes)));
        const float fill = static_cast<float>(std::min(1.0, static_cast<double>(d.estimateBytes) / budgetBytes));
        const bool over = static_cast<double>(d.estimateBytes) > budgetBytes;
        if (over) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.55f, 0.2f, 1.0f));
        }
        ImGui::ProgressBar(fill, ImVec2(-1.0f, 0.0f), overlay);
        if (over) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::BeginDisabled(settings_.gpuMemoryBudgetMb <= 0);
    if (ImGui::Checkbox(trId("Release clips that are not in use when over the budget", "optGpuTrim"), &settings_.gpuMemoryTrim)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Over the budget, images, videos and 3D scenes that are not playing and were not used for a few seconds are unloaded, the least costly to load again first (prepared videos, then images, then 3D scenes). A clip that plays is never unloaded. An unloaded clip loads again when you launch it, which can take a moment."));
    if (ImGui::Checkbox(trId("Also lower the layer resolution when it is still too much", "optGpuLowerQuality"), &settings_.gpuMemoryLowerQuality)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("If releasing is not enough, NyrexVJ first stops preparing the next clips, then unloads sooner, then draws layers at 75 % (and then 50 %) of the resolution. It steps back up when there is room again."));
    ImGui::EndDisabled();
    if (settings_.gpuMemoryBudgetMb > 0) {
        ImGui::TextDisabled(tr("Released because of the budget: %d, %.0f MB. Pressure step %d of %d."), static_cast<int>(d.releasedCount), megabytes(d.releasedBytes), d.pressureLevel, gpudiag::kMaxPressureLevel);
    }

    ImGui::Spacing();
    ImGui::Text(tr("Video and live source upload: %.1f MB/s now, %.1f MB/s at most in the last 10 seconds, %.2f GB since the start"), d.uploadBytesPerSecond / static_cast<double>(gpudiag::kMiB), d.uploadPeakBytesPerSecond / static_cast<double>(gpudiag::kMiB), static_cast<double>(d.uploadTotalBytes) / (1024.0 * 1024.0 * 1024.0));
    ImGui::SetItemTooltip("%s", tr("Counts the picture data NyrexVJ hands to the graphics driver for videos and live sources. Images and models are loaded once and are not counted."));

    const gpudiag::WindowStats& window = diagMonitor_->window();
    if (diagMonitor_->percentilesReliable()) {
        ImGui::Text(tr("Last minute: slowest 5 %% %.1f ms, slowest 1 %% %.1f ms, late frames %d, dropped frames %d"), diagMonitor_->p95(), diagMonitor_->p99(), static_cast<int>(diagMonitor_->watch().lateRecent()), static_cast<int>(diagMonitor_->watch().droppedRecent()));
        ImGui::SetItemTooltip("%s", tr("A frame is late when it takes more than 1.5 times the budget. Dropped counts the frames that were missed: a frame of 3 times the budget misses 2."));
        (void)window;
    }

    ImGui::SeparatorText(tr("Warnings"));
    const std::vector<gpudiag::Warning>& active = diagMonitor_->tracker().active();
    if (active.empty()) {
        ImGui::TextDisabled("%s", tr("No warnings."));
    }
    for (const gpudiag::Warning& warning : active) {
        ImGui::TextColored(severityColor(warning.severity), "%s", describe(warning, d).c_str());
    }
    if (ImGui::Checkbox(trId("Show warnings in the status bar", "optDiagWarnings"), &settings_.diagWarnings)) {
        persist();
    }
    if (ImGui::TreeNode(tr("Warning limits"))) {
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(tr("Slow frame limit (slowest 1 %)"), &settings_.diagP99Percent, 110, 400, "%d %% of the budget")) {
            persist();
        }
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(tr("Dropped frames per minute from"), &settings_.diagDroppedPerMinute, 1, 120)) {
            persist();
        }
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(tr("Memory budget used from"), &settings_.diagMemoryPercent, 50, 99, "%d %%")) {
            persist();
        }
        ImGui::TreePop();
    }
}

}

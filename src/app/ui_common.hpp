#pragma once

#include "core/composition.hpp"
#include "core/i18n.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace nvj::ui {

inline bool kHighContrast = false;
inline ImVec4 kAccent(1.00f, 0.55f, 0.12f, 1.00f);
inline ImVec4 kAccentHover(1.00f, 0.68f, 0.32f, 1.00f);
inline ImVec4 kAccentDim(0.50f, 0.28f, 0.06f, 1.00f);
inline ImVec4 kSelectionBg(0.20f, 0.12f, 0.06f, 1.00f);
inline const ImVec4 kBlack(0.04f, 0.04f, 0.04f, 1.00f);
inline const char* const kReleasesUrl = "https://github.com/MarlonXDev/NyrexVJ/releases";
inline const char* const kRepoUrl = "https://github.com/MarlonXDev/NyrexVJ";
inline const char* const kObsPluginUrl = "https://github.com/Off-World-Live/obs-spout2-plugin/releases";

struct Palette {
    const char* name;
    std::array<float, 3> a;
    std::array<float, 3> b;
};

inline const Palette kPalettes[] = {
    {"Orange / Cyan", {1.00f, 0.45f, 0.00f}, {0.00f, 0.80f, 1.00f}},
    {"Neon Pink / Blue", {1.00f, 0.10f, 0.60f}, {0.20f, 0.50f, 1.00f}},
    {"Synthwave", {1.00f, 0.20f, 0.55f}, {0.20f, 0.85f, 1.00f}},
    {"Toxic", {0.45f, 1.00f, 0.10f}, {0.00f, 0.60f, 0.35f}},
    {"Ice", {0.55f, 0.85f, 1.00f}, {0.15f, 0.30f, 1.00f}},
    {"Sunset", {1.00f, 0.35f, 0.10f}, {0.70f, 0.10f, 0.60f}},
    {"Acid", {0.80f, 1.00f, 0.00f}, {1.00f, 0.00f, 0.80f}},
    {"Gold", {1.00f, 0.80f, 0.20f}, {0.70f, 0.30f, 0.00f}},
    {"Violet", {0.55f, 0.25f, 1.00f}, {0.00f, 0.85f, 0.90f}},
    {"Mono", {1.00f, 1.00f, 1.00f}, {0.35f, 0.35f, 0.35f}},
};

inline float scale() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return style.FontScaleMain * style.FontScaleDpi;
}

inline float px(float value) {
    return value * scale();
}

inline void setNextWindowSize(const ImVec2& size, ImGuiCond condition = 0) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 16.0f;
    const float factor = scale();
    ImGui::SetNextWindowSize(ImVec2(std::min(size.x * factor, std::max(160.0f, viewport->Size.x - margin * 2.0f)), std::min(size.y * factor, std::max(120.0f, viewport->Size.y - margin * 2.0f))), condition);
}

inline void setNextWindowSizeConstraints(const ImVec2& smallest, const ImVec2& largest) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 16.0f;
    const float factor = scale();
    const float maxWidth = std::max(160.0f, viewport->Size.x - margin * 2.0f);
    const float maxHeight = std::max(120.0f, viewport->Size.y - margin * 2.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(smallest.x * factor, maxWidth), std::min(smallest.y * factor, maxHeight)), largest);
}

inline float fitComboWidth(float minWidth, const char* text) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float needed = ImGui::CalcTextSize(text).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    return std::max(minWidth, needed);
}

inline float sliderRatio(double value, double low, double high, bool logarithmic) {
    if (high == low) {
        return 0.0f;
    }
    double ratio = (value - low) / (high - low);
    if (logarithmic && low > 0.0 && high > 0.0 && value > 0.0) {
        ratio = std::log(value / low) / std::log(high / low);
    }
    return static_cast<float>(std::clamp(ratio, 0.0, 1.0));
}

inline void sliderBackground(float ratio) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const float rounding = ImGui::GetStyle().FrameRounding;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
    const float grab = std::min(ImGui::GetStyle().GrabMinSize, width);
    const float fillEnd = pos.x + grab * 0.5f + ratio * (width - grab);
    if (fillEnd - pos.x > 2.0f) {
        const ImVec4 fill(kAccentDim.x, kAccentDim.y, kAccentDim.z, 0.85f);
        draw->AddRectFilled(pos, ImVec2(fillEnd, pos.y + height), ImGui::GetColorU32(fill), rounding, ratio >= 0.999f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
    }
    draw->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_Border), rounding);
}

inline bool sliderFloat(const char* label, float* value, float low, float high, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    sliderBackground(sliderRatio(*value, low, high, (flags & ImGuiSliderFlags_Logarithmic) != 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const bool changed = ImGui::SliderFloat(label, value, low, high, format, flags);
    ImGui::PopStyleColor(4);
    return changed;
}

inline bool sliderInt(const char* label, int* value, int low, int high, const char* format = "%d", ImGuiSliderFlags flags = 0) {
    sliderBackground(sliderRatio(*value, low, high, (flags & ImGuiSliderFlags_Logarithmic) != 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const bool changed = ImGui::SliderInt(label, value, low, high, format, flags);
    ImGui::PopStyleColor(4);
    return changed;
}

inline std::string cellLabel(int layer, int column) {
    return std::string(tr("Layer ")) + std::to_string(layer + 1) + ", " + tr("Column ") + std::to_string(column + 1);
}

inline void newTag(const ImVec2& textStart, const char* label, float lineHeight) {
    const float size = ImGui::GetFontSize() * 0.66f;
    const float x = textStart.x + ImGui::CalcTextSize(label).x + 7.0f;
    const float y = textStart.y + (lineHeight - size) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, ImVec2(x, y), ImGui::GetColorU32(kAccent), tr("new"));
}

inline ImTextureID texId(unsigned int texture) {
    return static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture));
}

inline bool toggleButton(const char* label, bool& value) {
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, kBlack);
    }
    bool pressed = ImGui::SmallButton(label);
    if (value) {
        ImGui::PopStyleColor(3);
    }
    if (pressed) {
        value = !value;
    }
    return pressed;
}

inline bool accentButton(const char* label, bool highlighted, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    if (highlighted) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, kBlack);
    }
    bool pressed = ImGui::Button(label, size);
    if (highlighted) {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

}

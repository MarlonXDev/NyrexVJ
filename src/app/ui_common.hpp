#pragma once

#include "core/composition.hpp"
#include "core/i18n.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace nvj::ui {

inline bool kHighContrast = false;
inline bool kFlatLook = false;
inline bool kSliderSteppers = true;
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

inline float labelColumn(const char* text, const char* end) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float column = std::clamp(avail * 0.36f, px(56.0f), px(150.0f));
    const float textWidth = ImGui::CalcTextSize(text, end).x;
    return std::max(column, std::min(textWidth + px(10.0f), avail * 0.5f));
}

inline bool beginTool(const char* title, bool* open, ImGuiWindowFlags flags = 0) {
    const bool shown = ImGui::Begin(title, open, flags);
    if (open != nullptr && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        *open = false;
    }
    return shown;
}

inline ImU32 flatLineColor() {
    return ImGui::GetColorU32(ImVec4(0.039f, 0.035f, 0.035f, 1.00f));
}

inline void drawFieldLabel(ImDrawList* draw, const ImVec2& origin, float column, float height, const char* text, const char* end) {
    const float textWidth = ImGui::CalcTextSize(text, end).x;
    const float inner = column - px(8.0f);
    const float offset = kFlatLook ? std::max(0.0f, inner - textWidth) : 0.0f;
    draw->PushClipRect(origin, ImVec2(origin.x + column - px(6.0f), origin.y + height), true);
    draw->AddText(ImVec2(origin.x + offset, origin.y + ImGui::GetStyle().FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), text, end);
    draw->PopClipRect();
    if (kFlatLook) {
        draw->AddRectFilled(ImVec2(origin.x + column - 1.0f, origin.y), ImVec2(origin.x + column, origin.y + height), flatLineColor());
    }
}

inline void fieldLabel(const char* label) {
    const float column = labelColumn(label, label + std::strlen(label));
    const float height = ImGui::GetFrameHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    drawFieldLabel(ImGui::GetWindowDrawList(), origin, column, height, label, label + std::strlen(label));
    ImGui::Dummy(ImVec2(column, height));
    ImGui::SameLine(0.0f, 0.0f);
}

inline bool stepCell(const char* id, const char* glyph, float width, float height) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect bb(min, ImVec2(min.x + width, min.y + height));
    const ImGuiID widgetId = window->GetID(id);
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, widgetId)) {
        return false;
    }
    bool hovered = false;
    bool held = false;
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    const bool pressed = ImGui::ButtonBehavior(bb, widgetId, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    ImGui::PopItemFlag();
    const ImVec2 size = ImGui::CalcTextSize(glyph);
    ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    colour.w = held ? 1.0f : (hovered ? 0.95f : 0.55f);
    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + (width - size.x) * 0.5f, min.y + (height - size.y) * 0.5f), ImGui::GetColorU32(colour), glyph);
    return pressed;
}

namespace detail {

inline const char* labelEnd(const char* label) {
    const char* hash = std::strstr(label, "##");
    return hash ? hash : label + std::strlen(label);
}

template <typename Run, typename Legacy, typename Step>
inline bool styledSlider(const char* label, const std::string& valueText, float ratio, Run run, Legacy legacy, Step step) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* end = labelEnd(label);
    const bool hasLabel = end != label;
    const float requested = ImGui::CalcItemWidth();
    const bool explicitWidth = (g.NextItemData.HasFlags & ImGuiNextItemDataFlags_HasWidth) != 0 || ImGui::GetCurrentWindowRead()->DC.ItemWidth != ImGui::GetCurrentWindowRead()->DC.ItemWidthDefault;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float frameHeight = ImGui::GetFrameHeight();
    float labelWidth = 0.0f;
    float area = requested;
    if (hasLabel) {
        labelWidth = ui::labelColumn(label, end);
        if (!explicitWidth) {
            area = std::max(0.0f, avail - labelWidth);
        }
    }
    const bool flat = kFlatLook;
    const float valueWidth = flat ? std::max(px(52.0f), ImGui::CalcTextSize(valueText.c_str()).x + px(14.0f)) : std::max(px(46.0f), ImGui::CalcTextSize(valueText.c_str()).x + px(4.0f));
    const float gap = flat ? 0.0f : px(8.0f);
    const float stepWidth = px(44.0f);
    const bool steppers = flat && kSliderSteppers && area - valueWidth - gap - stepWidth >= px(72.0f);
    const float trackWidth = area - valueWidth - gap - (steppers ? stepWidth : 0.0f);
    if (trackWidth < px(56.0f)) {
        return legacy(label);
    }

    struct SpacingGuard {
        bool on;
        explicit SpacingGuard(bool enable, float x, float y) : on(enable) {
            if (on) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(x, y));
            }
        }
        ~SpacingGuard() {
            if (on) {
                ImGui::PopStyleVar();
            }
        }
    } spacingGuard(flat, style.ItemSpacing.x, px(1.0f));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    if (hasLabel) {
        drawFieldLabel(draw, rowPos, labelWidth, frameHeight, label, end);
        ImGui::Dummy(ImVec2(labelWidth, frameHeight));
        ImGui::SameLine(0.0f, 0.0f);
    }
    bool stepped = false;
    if (flat) {
        ImGui::Dummy(ImVec2(valueWidth, frameHeight));
        ImGui::SameLine(0.0f, 0.0f);
        if (steppers) {
            ImGui::PushID(label);
            const float cell = stepWidth * 0.5f;
            const bool down = stepCell("dec", "-", cell, frameHeight);
            ImGui::SameLine(0.0f, 0.0f);
            const bool up = stepCell("inc", "+", cell, frameHeight);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PopID();
            if (down) {
                stepped = step(-1) || stepped;
            }
            if (up) {
                stepped = step(1) || stepped;
            }
        }
    }

    const float radius = px(6.0f);
    const std::string id = hasLabel ? std::string("##") + label : std::string(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(trackWidth);
    const ImGuiID widgetId = ImGui::GetCurrentWindow()->GetID(id.c_str());
    const bool typingBefore = ImGui::TempInputIsActive(widgetId);
    bool changed = false;
    if (typingBefore) {
        const bool ran = run(id.c_str(), trackWidth);
        changed = ran || stepped;
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, radius * 2.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const bool ran = run(id.c_str(), trackWidth);
        changed = ran || stepped;
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar();
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool typing = typingBefore || ImGui::TempInputIsActive(widgetId);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (typing) {
        if (!flat) {
            window->DC.CursorPosPrevLine.x += gap + valueWidth;
            window->DC.CursorMaxPos.x = std::max(window->DC.CursorMaxPos.x, window->DC.CursorPosPrevLine.x);
        }
        return changed;
    }

    if (flat) {
        const float inset = std::max(1.0f, px(2.0f));
        const ImVec2 barMin(pos.x, pos.y + inset);
        const ImVec2 barMax(pos.x + trackWidth, pos.y + frameHeight - inset);
        const float markerWidth = std::max(2.0f, px(3.0f));
        const float markerX = barMin.x + std::clamp(ratio, 0.0f, 1.0f) * (trackWidth - markerWidth);
        const float level = hovered || active ? 0.188f : 0.145f;
        const float valueStart = pos.x - valueWidth - (steppers ? stepWidth : 0.0f);
        draw->AddRectFilled(barMin, barMax, ImGui::GetColorU32(ImVec4(0.031f, 0.031f, 0.031f, 1.00f)));
        draw->AddRectFilled(barMin, ImVec2(markerX + markerWidth, barMax.y), ImGui::GetColorU32(ImVec4(level, level, level, 1.00f)));
        draw->AddRectFilled(ImVec2(markerX, barMin.y), ImVec2(markerX + markerWidth, barMax.y), ImGui::GetColorU32(active ? kAccentHover : kAccent));
        draw->AddRectFilled(ImVec2(rowPos.x, pos.y + frameHeight), ImVec2(barMax.x + inset, pos.y + frameHeight + 1.0f), flatLineColor());
        draw->PushClipRect(ImVec2(valueStart, pos.y), ImVec2(valueStart + valueWidth - 2.0f, pos.y + frameHeight), true);
        draw->AddText(ImVec2(valueStart + px(7.0f), pos.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), valueText.c_str());
        draw->PopClipRect();
        return changed;
    }

    const float centerY = pos.y + frameHeight * 0.5f;
    const float trackHeight = std::max(3.0f, px(4.0f));
    const float usableMin = pos.x + 2.0f + radius;
    const float usableMax = pos.x + trackWidth - 2.0f - radius;
    const float handleX = usableMin + std::clamp(ratio, 0.0f, 1.0f) * std::max(0.0f, usableMax - usableMin);
    const ImVec2 trackMin(pos.x, centerY - trackHeight * 0.5f);
    const ImVec2 trackMax(pos.x + trackWidth, centerY + trackHeight * 0.5f);
    const ImVec4 trackColor = ImGui::GetStyleColorVec4(hovered || active ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    draw->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(trackColor), trackHeight * 0.5f);
    if (kHighContrast) {
        draw->AddRect(trackMin, trackMax, ImGui::GetColorU32(ImGuiCol_Border), trackHeight * 0.5f);
    }
    if (handleX - trackMin.x > 1.0f) {
        draw->AddRectFilled(trackMin, ImVec2(handleX, trackMax.y), ImGui::GetColorU32(kAccent), trackHeight * 0.5f);
    }
    const float handleRadius = active ? radius : (hovered ? radius * 0.95f : radius * 0.85f);
    draw->AddCircleFilled(ImVec2(handleX, centerY), handleRadius, ImGui::GetColorU32(active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.90f, 0.90f, 0.92f, 1.0f)));
    if (active || hovered || kHighContrast) {
        draw->AddCircle(ImVec2(handleX, centerY), handleRadius, ImGui::GetColorU32(kAccent), 0, std::max(1.5f, px(1.5f)));
    }

    const float textWidth = ImGui::CalcTextSize(valueText.c_str()).x;
    draw->AddText(ImVec2(pos.x + trackWidth + gap + std::max(0.0f, valueWidth - textWidth), pos.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), valueText.c_str());

    window->DC.CursorPosPrevLine.x += gap + valueWidth;
    window->DC.CursorMaxPos.x = std::max(window->DC.CursorMaxPos.x, window->DC.CursorPosPrevLine.x);
    return changed;
}

}

inline bool sliderFloat(const char* label, float* value, float low, float high, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    const bool logarithmic = (flags & ImGuiSliderFlags_Logarithmic) != 0;
    char text[64];
    std::snprintf(text, sizeof(text), format, static_cast<double>(*value));
    return detail::styledSlider(
        label,
        text,
        sliderRatio(*value, low, high, logarithmic),
        [&](const char* id, float) { return ImGui::SliderFloat(id, value, low, high, format, flags); },
        [&](const char* original) {
            sliderBackground(sliderRatio(*value, low, high, logarithmic));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            const bool changed = ImGui::SliderFloat(original, value, low, high, format, flags);
            ImGui::PopStyleColor(4);
            return changed;
        },
        [&](int direction) {
            const float before = *value;
            const float lowest = std::min(low, high);
            const float highest = std::max(low, high);
            if (logarithmic && lowest > 0.0f) {
                *value = std::clamp(*value * (direction > 0 ? 1.05f : 1.0f / 1.05f), lowest, highest);
            } else {
                *value = std::clamp(*value + static_cast<float>(direction) * (highest - lowest) * 0.01f, lowest, highest);
            }
            return *value != before;
        });
}

inline bool sliderInt(const char* label, int* value, int low, int high, const char* format = "%d", ImGuiSliderFlags flags = 0) {
    const bool logarithmic = (flags & ImGuiSliderFlags_Logarithmic) != 0;
    char text[64];
    std::snprintf(text, sizeof(text), format, *value);
    return detail::styledSlider(
        label,
        text,
        sliderRatio(*value, low, high, logarithmic),
        [&](const char* id, float) { return ImGui::SliderInt(id, value, low, high, format, flags); },
        [&](const char* original) {
            sliderBackground(sliderRatio(*value, low, high, logarithmic));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            const bool changed = ImGui::SliderInt(original, value, low, high, format, flags);
            ImGui::PopStyleColor(4);
            return changed;
        },
        [&](int direction) {
            const int before = *value;
            *value = std::clamp(*value + direction, std::min(low, high), std::max(low, high));
            return *value != before;
        });
}

inline void sectionHeader(const char* label) {
    if (!kFlatLook) {
        ImGui::SeparatorText(label);
        return;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float left = pos.x - style.WindowPadding.x;
    const float right = pos.x + width + style.WindowPadding.x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(left, pos.y), ImVec2(right, pos.y + height), ImGui::GetColorU32(ImVec4(0.173f, 0.173f, 0.173f, 1.00f)));
    const float arrow = std::max(3.0f, height * 0.17f);
    const float arrowX = left + px(10.0f);
    const float arrowY = pos.y + height * 0.5f - arrow * 0.35f;
    draw->AddTriangleFilled(ImVec2(arrowX - arrow, arrowY), ImVec2(arrowX + arrow, arrowY), ImVec2(arrowX, arrowY + arrow * 1.2f), ImGui::GetColorU32(ImVec4(0.56f, 0.56f, 0.56f, 1.00f)));
    draw->AddText(ImVec2(left + px(22.0f), pos.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::Dummy(ImVec2(width, height));
}

inline bool collapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) {
    if (!kFlatLook) {
        return ImGui::CollapsingHeader(label, flags);
    }
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(label);
    bool open = ImGui::TreeNodeUpdateNextOpen(id, flags);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float left = pos.x - style.WindowPadding.x;
    const float right = pos.x + width + style.WindowPadding.x;
    const ImRect bounds(ImVec2(left, pos.y), ImVec2(right, pos.y + height));
    ImGui::ItemSize(ImVec2(width, height), 0.0f);
    if (!ImGui::ItemAdd(bounds, id)) {
        return open;
    }
    bool hovered = false;
    bool held = false;
    if (ImGui::ButtonBehavior(bounds, id, &hovered, &held)) {
        open = !open;
        ImGui::TreeNodeSetOpen(id, open);
    }
    ImDrawList* draw = window->DrawList;
    const float shade = hovered ? 0.208f : 0.173f;
    draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(ImVec4(shade, shade, shade, 1.00f)));
    const float arrow = std::max(3.0f, height * 0.17f);
    const float arrowX = left + px(10.0f);
    const float arrowY = pos.y + height * 0.5f;
    const ImU32 arrowColor = ImGui::GetColorU32(ImVec4(0.56f, 0.56f, 0.56f, 1.00f));
    if (open) {
        draw->AddTriangleFilled(ImVec2(arrowX - arrow, arrowY - arrow * 0.6f), ImVec2(arrowX + arrow, arrowY - arrow * 0.6f), ImVec2(arrowX, arrowY + arrow * 0.7f), arrowColor);
    } else {
        draw->AddTriangleFilled(ImVec2(arrowX - arrow * 0.6f, arrowY - arrow), ImVec2(arrowX - arrow * 0.6f, arrowY + arrow), ImVec2(arrowX + arrow * 0.7f, arrowY), arrowColor);
    }
    draw->AddText(ImVec2(left + px(22.0f), pos.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
    return open;
}

inline bool beginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0) {
    if (!kFlatLook || (flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) != 0) {
        return ImGui::BeginCombo(label, preview, flags);
    }
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::CalcItemWidth(), min.y + ImGui::GetFrameHeight());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool open = ImGui::BeginCombo(label, preview, flags | ImGuiComboFlags_NoArrowButton);
    const float size = std::max(3.0f, (max.y - min.y) * 0.14f);
    const float x = max.x - px(10.0f);
    const float y = (min.y + max.y) * 0.5f;
    draw->AddTriangleFilled(ImVec2(x - size, y - size * 0.6f), ImVec2(x + size, y - size * 0.6f), ImVec2(x, y + size * 0.8f), ImGui::GetColorU32(ImVec4(0.78f, 0.78f, 0.78f, 1.00f)));
    return open;
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

inline bool toggleButton(const char* label, bool& value, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, kBlack);
    }
    bool pressed = size.x > 0.0f ? ImGui::Button(label, size) : ImGui::SmallButton(label);
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

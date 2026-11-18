#pragma once

#include "core/composition.hpp"
#include "core/i18n.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <string>

namespace nvj::ui {

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

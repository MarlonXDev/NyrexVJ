#include "app/theme.hpp"

#include "app/ui_common.hpp"

#include <imgui.h>

#include <algorithm>

namespace nvj {

namespace {

const ThemeInfo kThemes[] = {
    {"orange", "Orange", 1.00f, 0.55f, 0.12f},
    {"blue", "Blue", 0.22f, 0.56f, 1.00f},
    {"red", "Red", 1.00f, 0.27f, 0.30f},
    {"green", "Green", 0.22f, 0.86f, 0.46f},
    {"purple", "Purple", 0.66f, 0.42f, 1.00f},
    {"pink", "Pink", 1.00f, 0.32f, 0.68f},
    {"cyan", "Cyan", 0.10f, 0.82f, 0.92f},
    {"gold", "Gold", 1.00f, 0.78f, 0.20f},
    {"mono", "Mono", 0.88f, 0.88f, 0.92f},
};

ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 withAlpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

}

int themeCount() {
    return static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0]));
}

const ThemeInfo& themeInfo(int index) {
    return kThemes[std::clamp(index, 0, themeCount() - 1)];
}

int themeIndex(const std::string& id) {
    for (int i = 0; i < themeCount(); ++i) {
        if (id == kThemes[i].id) {
            return i;
        }
    }
    return 0;
}

void applyTheme(int index) {
    const ThemeInfo& theme = themeInfo(index);
    const bool mono = std::string(theme.id) == "mono";

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    const ImVec4 accent(theme.r, theme.g, theme.b, 1.00f);
    const ImVec4 white(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 accentHover = mixColor(accent, white, 0.28f);
    const ImVec4 accentDim(accent.x * 0.50f, accent.y * 0.50f, accent.z * 0.50f, 1.00f);
    const float tint = mono ? 0.0f : 0.035f;
    const ImVec4 bg = mixColor(ImVec4(0.055f, 0.055f, 0.065f, 1.00f), accent, tint);
    const ImVec4 panel = mixColor(ImVec4(0.085f, 0.085f, 0.095f, 1.00f), accent, tint);
    const ImVec4 raised = mixColor(ImVec4(0.145f, 0.145f, 0.165f, 1.00f), accent, tint * 1.4f);
    const ImVec4 raisedHover = mixColor(ImVec4(0.205f, 0.205f, 0.235f, 1.00f), accent, tint * 1.4f);
    const ImVec4 text(0.90f, 0.90f, 0.92f, 1.00f);
    const ImVec4 textDim(0.52f, 0.52f, 0.56f, 1.00f);
    const ImVec4 border(0.19f, 0.19f, 0.21f, 1.00f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = withAlpha(mixColor(ImVec4(0.09f, 0.09f, 0.10f, 1.0f), accent, tint), 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = raised;
    c[ImGuiCol_FrameBgHovered] = raisedHover;
    c[ImGuiCol_FrameBgActive] = accentDim;
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_ScrollbarBg] = bg;
    c[ImGuiCol_ScrollbarGrab] = raisedHover;
    c[ImGuiCol_ScrollbarGrabHovered] = accentDim;
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = raisedHover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = accentDim;
    c[ImGuiCol_HeaderHovered] = accent;
    c[ImGuiCol_HeaderActive] = accentHover;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHover;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = accentDim;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = accent;
    c[ImGuiCol_TabSelected] = accentDim;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = raised;
    c[ImGuiCol_DockingPreview] = withAlpha(accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = bg;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    c[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = accent;

    style.WindowRounding = 0.0f;
    c[ImGuiCol_WindowBg].w = 1.0f;

    ui::kAccent = accent;
    ui::kAccentHover = accentHover;
    ui::kAccentDim = accentDim;
    ui::kSelectionBg = mixColor(panel, accent, 0.16f);
}

}

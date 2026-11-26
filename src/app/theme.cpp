#include "app/theme.hpp"

#include "app/ui_common.hpp"

#include <imgui.h>

#include <algorithm>

namespace nvj {

namespace {

const ThemeInfo kThemes[] = {
    {"studio", "Studio", 0.92f, 0.31f, 0.25f},
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

void applyTheme(int index, const ThemeOptions& options) {
    const ThemeInfo& theme = themeInfo(index);
    const bool mono = std::string(theme.id) == "mono";
    const bool high = options.highContrast;

    ImGuiStyle& style = ImGui::GetStyle();
    const float fontSizeBase = style.FontSizeBase;
    const float fontScaleMain = style.FontScaleMain;
    const float fontScaleDpi = style.FontScaleDpi;
    style = ImGuiStyle();
    style.FontSizeBase = fontSizeBase;
    style.FontScaleMain = fontScaleMain;
    style.FontScaleDpi = fontScaleDpi;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBarOverlineSize = high ? 3.0f : 2.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = high ? 9.0f : 6.0f;

    const ImVec4 white(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 baseAccent(theme.r, theme.g, theme.b, 1.00f);
    const ImVec4 accent = high ? mixColor(baseAccent, white, 0.30f) : baseAccent;
    const ImVec4 accentHover = mixColor(accent, white, 0.22f);
    const ImVec4 accentDim(accent.x * 0.52f, accent.y * 0.52f, accent.z * 0.52f, 1.00f);
    const float tint = mono ? 0.0f : 0.02f;
    const ImVec4 bg = high ? ImVec4(0.00f, 0.00f, 0.00f, 1.00f) : mixColor(ImVec4(0.075f, 0.078f, 0.086f, 1.00f), accent, tint);
    const ImVec4 panel = high ? ImVec4(0.035f, 0.035f, 0.040f, 1.00f) : mixColor(ImVec4(0.125f, 0.130f, 0.141f, 1.00f), accent, tint);
    const ImVec4 well = high ? ImVec4(0.012f, 0.012f, 0.014f, 1.00f) : mixColor(ImVec4(0.100f, 0.105f, 0.118f, 1.00f), accent, tint);
    const ImVec4 raised = high ? ImVec4(0.150f, 0.152f, 0.165f, 1.00f) : mixColor(ImVec4(0.165f, 0.170f, 0.185f, 1.00f), accent, tint);
    const ImVec4 raisedHover = high ? ImVec4(0.270f, 0.275f, 0.300f, 1.00f) : mixColor(ImVec4(0.215f, 0.222f, 0.240f, 1.00f), accent, tint);
    const ImVec4 text = high ? ImVec4(1.00f, 1.00f, 1.00f, 1.00f) : ImVec4(0.86f, 0.87f, 0.89f, 1.00f);
    const ImVec4 textDim = high ? ImVec4(0.80f, 0.81f, 0.84f, 1.00f) : ImVec4(0.60f, 0.61f, 0.64f, 1.00f);
    const ImVec4 border = high ? ImVec4(0.66f, 0.67f, 0.71f, 1.00f) : ImVec4(0.205f, 0.212f, 0.230f, 1.00f);
    const ImVec4 knob = high ? ImVec4(0.95f, 0.95f, 0.97f, 1.00f) : ImVec4(0.78f, 0.78f, 0.80f, 0.70f);
    const ImVec4 focus = high ? ImVec4(1.00f, 0.92f, 0.20f, 1.00f) : accent;
    const float headerMix = mono ? 0.0f : 0.05f;
    const ImVec4 header = mixColor(raised, accent, headerMix);
    const ImVec4 headerHover = mixColor(raisedHover, accent, headerMix + 0.12f);
    const ImVec4 headerActive = mixColor(raisedHover, accent, high ? 0.20f : headerMix + 0.38f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = well;
    c[ImGuiCol_PopupBg] = withAlpha(mixColor(ImVec4(0.115f, 0.120f, 0.132f, 1.0f), accent, tint), 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = raised;
    c[ImGuiCol_FrameBgHovered] = raisedHover;
    c[ImGuiCol_FrameBgActive] = mixColor(raisedHover, accent, 0.30f);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = high ? border : raisedHover;
    c[ImGuiCol_ScrollbarGrabHovered] = accentDim;
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = knob;
    c[ImGuiCol_SliderGrabActive] = white;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = raisedHover;
    c[ImGuiCol_ButtonActive] = high ? ImVec4(accent.x * 0.40f, accent.y * 0.40f, accent.z * 0.40f, 1.00f) : accent;
    c[ImGuiCol_Header] = header;
    c[ImGuiCol_HeaderHovered] = headerHover;
    c[ImGuiCol_HeaderActive] = headerActive;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHover;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = accentDim;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = raisedHover;
    c[ImGuiCol_TabSelected] = raised;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = raised;
    c[ImGuiCol_TabDimmedSelectedOverline] = accentDim;
    c[ImGuiCol_DockingPreview] = withAlpha(accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = bg;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = high ? ImVec4(0.45f, 0.46f, 0.50f, 1.00f) : ImVec4(0.15f, 0.155f, 0.17f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    c[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = focus;
    c[ImGuiCol_WindowBg].w = 1.0f;

    if (options.sizeScale > 0.0f && options.sizeScale != 1.0f) {
        style.ScaleAllSizes(options.sizeScale);
    }

    ui::kHighContrast = high;
    ui::kAccent = accent;
    ui::kAccentHover = accentHover;
    ui::kAccentDim = accentDim;
    ui::kSelectionBg = mixColor(panel, accent, 0.16f);
}

}

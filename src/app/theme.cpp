#include "app/theme.hpp"

#include "app/ui_common.hpp"
#include "core/contrast.hpp"

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
    {"arena", "Arena", 0.30f, 0.55f, 0.46f},
};

ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 withAlpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

ImVec4 fromHsv(float hue, float saturation, float value) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, saturation, value, r, g, b);
    return ImVec4(r, g, b, 1.0f);
}

struct FlatAccents {
    ImVec4 mid;
    ImVec4 bright;
    ImVec4 dim;
};

FlatAccents flatAccents(const ThemeInfo& theme, const ImVec4& under) {
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
    ImGui::ColorConvertRGBtoHSV(theme.r, theme.g, theme.b, hue, saturation, value);
    const float chroma = std::clamp((saturation - 0.04f) / 0.11f, 0.0f, 1.0f);
    FlatAccents out;
    float midValue = 0.55f;
    const contrast::Rgb back{under.x, under.y, under.z};
    out.mid = fromHsv(hue, 0.45f * chroma, midValue);
    while (midValue < 0.92f && contrast::ratio(contrast::Rgb{out.mid.x, out.mid.y, out.mid.z}, back) < 3.3) {
        midValue += 0.02f;
        out.mid = fromHsv(hue, 0.45f * chroma, midValue);
    }
    out.bright = fromHsv(hue, 0.50f * chroma, 1.00f);
    out.dim = fromHsv(hue, 0.49f * chroma, 0.32f);
    return out;
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
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = high ? 1.0f : 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = high ? 1.0f : 0.0f;
    style.TabBarOverlineSize = 0.0f;
    style.WindowPadding = ImVec2(6.0f, 6.0f);
    style.FramePadding = ImVec2(7.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = high ? 9.0f : 6.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    const ImVec4 white(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 baseAccent(theme.r, theme.g, theme.b, 1.00f);
    ImVec4 bg = ImVec4(0.039f, 0.035f, 0.035f, 1.00f);
    ImVec4 panel = ImVec4(0.086f, 0.082f, 0.078f, 1.00f);
    ImVec4 well = ImVec4(0.051f, 0.051f, 0.047f, 1.00f);
    ImVec4 raised = ImVec4(0.227f, 0.227f, 0.224f, 1.00f);
    ImVec4 raisedHover = ImVec4(0.290f, 0.290f, 0.286f, 1.00f);
    ImVec4 text = ImVec4(0.79f, 0.79f, 0.79f, 1.00f);
    ImVec4 textDim = ImVec4(0.56f, 0.56f, 0.56f, 1.00f);
    ImVec4 border = ImVec4(0.039f, 0.035f, 0.035f, 1.00f);
    ImVec4 knob = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    ImVec4 frame = ImVec4(0.118f, 0.118f, 0.114f, 1.00f);
    ImVec4 frameHover = ImVec4(0.165f, 0.165f, 0.165f, 1.00f);
    ImVec4 popup = ImVec4(0.165f, 0.165f, 0.165f, 0.98f);
    ImVec4 menuBar = ImVec4(0.165f, 0.165f, 0.165f, 1.00f);
    ImVec4 header = ImVec4(0.173f, 0.173f, 0.173f, 1.00f);
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 accentDim;
    ImVec4 focus;
    if (high) {
        bg = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        panel = ImVec4(0.035f, 0.035f, 0.040f, 1.00f);
        well = ImVec4(0.012f, 0.012f, 0.014f, 1.00f);
        raised = ImVec4(0.150f, 0.152f, 0.165f, 1.00f);
        raisedHover = ImVec4(0.270f, 0.275f, 0.300f, 1.00f);
        text = white;
        textDim = ImVec4(0.80f, 0.81f, 0.84f, 1.00f);
        border = ImVec4(0.66f, 0.67f, 0.71f, 1.00f);
        knob = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
        frame = raised;
        frameHover = raisedHover;
        popup = ImVec4(0.115f, 0.120f, 0.132f, 0.98f);
        menuBar = bg;
        header = raised;
        accent = mixColor(baseAccent, white, 0.30f);
        accentHover = mixColor(accent, white, 0.22f);
        accentDim = ImVec4(accent.x * 0.52f, accent.y * 0.52f, accent.z * 0.52f, 1.00f);
        focus = ImVec4(1.00f, 0.92f, 0.20f, 1.00f);
    } else {
        const FlatAccents flat = flatAccents(theme, frame);
        accent = flat.mid;
        accentHover = flat.bright;
        accentDim = flat.dim;
        focus = accentHover;
    }
    const ImVec4 headerHover = high ? mixColor(raisedHover, accent, 0.12f) : raised;
    const ImVec4 headerActive = high ? mixColor(raisedHover, accent, 0.20f) : accentDim;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = well;
    c[ImGuiCol_PopupBg] = popup;
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = mixColor(raisedHover, accent, 0.30f);
    c[ImGuiCol_TitleBg] = high ? bg : menuBar;
    c[ImGuiCol_TitleBgActive] = high ? bg : menuBar;
    c[ImGuiCol_TitleBgCollapsed] = high ? bg : menuBar;
    c[ImGuiCol_MenuBarBg] = menuBar;
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = high ? border : raised;
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
    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = accentDim;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = high ? bg : menuBar;
    c[ImGuiCol_TabHovered] = high ? raised : ImVec4(0.227f, 0.227f, 0.224f, 1.00f);
    c[ImGuiCol_TabSelected] = high ? raised : bg;
    c[ImGuiCol_TabSelectedOverline] = clear;
    c[ImGuiCol_TabDimmed] = high ? bg : menuBar;
    c[ImGuiCol_TabDimmedSelected] = high ? raised : bg;
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_DockingPreview] = withAlpha(accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = high ? bg : menuBar;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = high ? ImVec4(0.45f, 0.46f, 0.50f, 1.00f) : ImVec4(0.15f, 0.155f, 0.17f, 1.00f);
    c[ImGuiCol_TableRowBg] = clear;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    c[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = focus;
    c[ImGuiCol_WindowBg].w = 1.0f;

    if (options.sizeScale > 0.0f && options.sizeScale != 1.0f) {
        style.ScaleAllSizes(options.sizeScale);
    }
    const float factor = options.sizeScale > 0.0f ? options.sizeScale : 1.0f;
    style.FramePadding = ImVec2(7.0f * factor, 3.0f * factor);

    style.TabMinWidthBase = high ? 0.0f : 72.0f * factor;
    ui::kHighContrast = high;
    ui::kFlatLook = !high;
    ui::kAccent = accent;
    ui::kAccentHover = accentHover;
    ui::kAccentDim = accentDim;
    ui::kSelectionBg = mixColor(panel, accent, 0.16f);
}

}

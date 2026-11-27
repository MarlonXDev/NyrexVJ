#include "check.hpp"

#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/contrast.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using nvj::contrast::Rgb;

bool printing = false;

Rgb opaque(const ImVec4& colour, const ImVec4& under) {
    const Rgb top{colour.x, colour.y, colour.z};
    const Rgb bottom{under.x, under.y, under.z};
    return nvj::contrast::blend(top, colour.w, bottom);
}

void expectRatio(const char* mode, const char* theme, const char* what, const ImVec4& foreground, const ImVec4& background, double minimum) {
    const Rgb back{background.x, background.y, background.z};
    const double value = nvj::contrast::ratio(opaque(foreground, background), back);
    if (printing) {
        std::printf("%-8s %-8s %-34s %6.2f (needs %.1f)\n", mode, theme, what, value, minimum);
    }
    testing::check(value >= minimum, what, __LINE__);
    if (value < minimum) {
        std::fprintf(stderr, "  %s / %s: %.2f < %.1f\n", mode, theme, value, minimum);
    }
}

void measure(int theme, bool high) {
    nvj::ThemeOptions options;
    options.highContrast = high;
    nvj::applyTheme(theme, options);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4* c = style.Colors;
    const char* mode = high ? "high" : "standard";
    const char* name = nvj::themeInfo(theme).id;
    const double textMinimum = high ? nvj::contrast::kEnhancedText : nvj::contrast::kNormalText;

    expectRatio(mode, name, "text on window", c[ImGuiCol_Text], c[ImGuiCol_WindowBg], nvj::contrast::kEnhancedText);
    expectRatio(mode, name, "text on child", c[ImGuiCol_Text], c[ImGuiCol_ChildBg], textMinimum);
    expectRatio(mode, name, "text on popup", c[ImGuiCol_Text], c[ImGuiCol_PopupBg], textMinimum);
    expectRatio(mode, name, "text on frame", c[ImGuiCol_Text], c[ImGuiCol_FrameBg], textMinimum);
    expectRatio(mode, name, "text on frame hovered", c[ImGuiCol_Text], c[ImGuiCol_FrameBgHovered], nvj::contrast::kNormalText);
    expectRatio(mode, name, "text on button", c[ImGuiCol_Text], c[ImGuiCol_Button], textMinimum);
    expectRatio(mode, name, "text on button hovered", c[ImGuiCol_Text], c[ImGuiCol_ButtonHovered], nvj::contrast::kNormalText);
    expectRatio(mode, name, "text on header", c[ImGuiCol_Text], c[ImGuiCol_Header], textMinimum);
    expectRatio(mode, name, "text on header hovered", c[ImGuiCol_Text], c[ImGuiCol_HeaderHovered], nvj::contrast::kNormalText);
    expectRatio(mode, name, "text on tab selected", c[ImGuiCol_Text], c[ImGuiCol_TabSelected], nvj::contrast::kNormalText);
    expectRatio(mode, name, "text on menu bar", c[ImGuiCol_Text], c[ImGuiCol_MenuBarBg], textMinimum);
    expectRatio(mode, name, "dim text on window", c[ImGuiCol_TextDisabled], c[ImGuiCol_WindowBg], nvj::contrast::kNormalText);
    expectRatio(mode, name, "dim text on child", c[ImGuiCol_TextDisabled], c[ImGuiCol_ChildBg], nvj::contrast::kNormalText);
    expectRatio(mode, name, "dim text on frame", c[ImGuiCol_TextDisabled], c[ImGuiCol_FrameBg], high ? nvj::contrast::kNormalText : 3.0);
    expectRatio(mode, name, "focus ring on window", c[ImGuiCol_NavCursor], c[ImGuiCol_WindowBg], nvj::contrast::kComponent);
    expectRatio(mode, name, "focus ring on frame", c[ImGuiCol_NavCursor], c[ImGuiCol_FrameBg], nvj::contrast::kComponent);
    expectRatio(mode, name, "focus ring on button", c[ImGuiCol_NavCursor], c[ImGuiCol_Button], nvj::contrast::kComponent);
    expectRatio(mode, name, "check mark on frame", c[ImGuiCol_CheckMark], c[ImGuiCol_FrameBg], nvj::contrast::kComponent);
    expectRatio(mode, name, "slider grab on frame", c[ImGuiCol_SliderGrab], c[ImGuiCol_FrameBg], nvj::contrast::kComponent);
    if (high) {
        expectRatio(mode, name, "border on window", c[ImGuiCol_Border], c[ImGuiCol_WindowBg], nvj::contrast::kComponent);
        expectRatio(mode, name, "border on child", c[ImGuiCol_Border], c[ImGuiCol_ChildBg], nvj::contrast::kComponent);
        expectRatio(mode, name, "border on frame", c[ImGuiCol_Border], c[ImGuiCol_FrameBg], nvj::contrast::kComponent);
        expectRatio(mode, name, "text on button active", c[ImGuiCol_Text], c[ImGuiCol_ButtonActive], nvj::contrast::kNormalText);
        expectRatio(mode, name, "text on header active", c[ImGuiCol_Text], c[ImGuiCol_HeaderActive], nvj::contrast::kNormalText);
        expectRatio(mode, name, "accent on window", nvj::ui::kAccent, c[ImGuiCol_WindowBg], nvj::contrast::kEnhancedText);
        expectRatio(mode, name, "scrollbar grab on window", c[ImGuiCol_ScrollbarGrab], c[ImGuiCol_WindowBg], nvj::contrast::kComponent);
    }
}

void testContrast() {
    for (int theme = 0; theme < nvj::themeCount(); ++theme) {
        measure(theme, false);
        measure(theme, true);
    }
}

void testSameLookForEveryColour() {
    nvj::applyTheme(nvj::themeIndex("arena"));
    const ImVec4 windowBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    const ImVec4 tabSelected = ImGui::GetStyle().Colors[ImGuiCol_TabSelected];
    for (int theme = 0; theme < nvj::themeCount(); ++theme) {
        nvj::applyTheme(theme);
        const ImGuiStyle& style = ImGui::GetStyle();
        CHECK(nvj::ui::kFlatLook);
        CHECK(style.Colors[ImGuiCol_WindowBg].x == windowBg.x && style.Colors[ImGuiCol_WindowBg].z == windowBg.z);
        CHECK(style.Colors[ImGuiCol_TabSelected].x == tabSelected.x);
        CHECK(style.FrameRounding == 2.0f && style.WindowMenuButtonPosition == ImGuiDir_None);
        CHECK(style.TabMinWidthBase == 72.0f);
        nvj::ThemeOptions larger;
        larger.sizeScale = 1.5f;
        nvj::applyTheme(theme, larger);
        CHECK(std::fabs(ImGui::GetStyle().TabMinWidthBase - 108.0f) < 0.01f);
        nvj::ThemeOptions high;
        high.highContrast = true;
        nvj::applyTheme(theme, high);
        CHECK(!nvj::ui::kFlatLook);
        CHECK(ImGui::GetStyle().TabMinWidthBase == 0.0f);
    }
    nvj::applyTheme(nvj::themeIndex("blue"));
    CHECK(nvj::ui::kAccent.z > nvj::ui::kAccent.x && nvj::ui::kAccentHover.z > nvj::ui::kAccentHover.x);
    nvj::applyTheme(nvj::themeIndex("orange"));
    CHECK(nvj::ui::kAccent.x > nvj::ui::kAccent.z && nvj::ui::kAccentHover.x > nvj::ui::kAccentHover.z);
    nvj::applyTheme(nvj::themeIndex("mono"));
    CHECK(std::fabs(nvj::ui::kAccent.x - nvj::ui::kAccent.z) < 0.04f);
    nvj::applyTheme(nvj::themeIndex("arena"));
    CHECK(std::fabs(nvj::ui::kAccentHover.x - 0.502f) < 0.01f && std::fabs(nvj::ui::kAccentHover.y - 0.996f) < 0.01f);
}

void testStyleReset() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = 1.4f;
    style.FontScaleDpi = 1.5f;
    style.FontSizeBase = 17.0f;
    nvj::applyTheme(0);
    const ImVec2 basePadding = style.FramePadding;
    const float baseSpacing = style.ItemSpacing.x;
    const float baseMin = style.WindowMinSize.x;
    CHECK(style.FontScaleMain == 1.4f);
    CHECK(style.FontScaleDpi == 1.5f);
    CHECK(style.FontSizeBase == 17.0f);

    nvj::ThemeOptions larger;
    larger.sizeScale = 2.0f;
    nvj::applyTheme(0, larger);
    CHECK(std::fabs(style.FramePadding.x - basePadding.x * 2.0f) < 0.01f);
    CHECK(std::fabs(style.ItemSpacing.x - baseSpacing * 2.0f) < 0.01f);
    CHECK(std::fabs(style.WindowMinSize.x - baseMin * 2.0f) < 0.01f);
    nvj::applyTheme(0, larger);
    CHECK(std::fabs(style.FramePadding.x - basePadding.x * 2.0f) < 0.01f);
    CHECK(std::fabs(style.WindowMinSize.x - baseMin * 2.0f) < 0.01f);

    nvj::applyTheme(0);
    CHECK(std::fabs(style.FramePadding.x - basePadding.x) < 0.01f);
    CHECK(std::fabs(style.ItemSpacing.x - baseSpacing) < 0.01f);
    CHECK(std::fabs(style.WindowMinSize.x - baseMin) < 0.01f);
    CHECK(style.FontScaleMain == 1.4f);

    nvj::ThemeOptions broken;
    broken.sizeScale = -3.0f;
    nvj::applyTheme(0, broken);
    CHECK(std::fabs(style.FramePadding.x - basePadding.x) < 0.01f);
    broken.sizeScale = 0.0f;
    nvj::applyTheme(0, broken);
    CHECK(std::fabs(style.FramePadding.x - basePadding.x) < 0.01f);

    CHECK(!nvj::ui::kHighContrast);
    nvj::ThemeOptions high;
    high.highContrast = true;
    nvj::applyTheme(0, high);
    CHECK(nvj::ui::kHighContrast);
    nvj::applyTheme(0);
    CHECK(!nvj::ui::kHighContrast);
}

void testIndexes() {
    CHECK(nvj::themeCount() == 11);
    CHECK(nvj::themeIndex("arena") == 10);
    CHECK(nvj::themeIndex("studio") == 0);
    CHECK(nvj::themeIndex("mono") == 9);
    CHECK(nvj::themeIndex("unknown") == 0);
    nvj::applyTheme(-5);
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_Text].x > 0.5f);
    nvj::applyTheme(500);
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_Text].x > 0.5f);
}

}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print") == 0) {
            printing = true;
        }
    }
    ImGuiContext* context = ImGui::CreateContext();
    testIndexes();
    testStyleReset();
    testSameLookForEveryColour();
    testContrast();
    ImGui::DestroyContext(context);
    return testing::finish();
}

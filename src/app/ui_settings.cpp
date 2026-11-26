#include "app/app.hpp"

#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/frame_pacer.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "update/update_logic.hpp"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

namespace {

const char* settingsPageTitle(int page) {
    switch (page) {
    case SettingsGeneral:
        return tr("General");
    case SettingsAppearance:
        return tr("Appearance");
    case SettingsAudio:
        return tr("Audio");
    case SettingsOutput:
        return tr("Output");
    case SettingsRecording:
        return tr("Recording");
    case SettingsControl:
        return tr("Control");
    case SettingsStage:
        return tr("Stage");
    case SettingsSync:
        return tr("Sync");
    case SettingsUpdates:
        return tr("Updates");
    case SettingsShortcuts:
        return tr("Shortcuts");
    case SettingsPerformance:
        return tr("Performance");
    case SettingsPlugins:
        return tr("Plugins");
    default:
        return tr("About");
    }
}

const char* settingsPageHint(int page) {
    switch (page) {
    case SettingsGeneral:
        return tr("Language, start-up behavior and decks.");
    case SettingsAppearance:
        return tr("Theme color and size of the interface.");
    case SettingsAudio:
        return tr("Audio source, level and beat detection.");
    case SettingsOutput:
        return tr("Output window, resolution and Spout.");
    case SettingsRecording:
        return tr("Record the final picture to a video file.");
    case SettingsControl:
        return tr("Play NyrexVJ with OSC and MIDI.");
    case SettingsStage:
        return tr("DMX for stage lights, the VRSL strip and VRChat.");
    case SettingsSync:
        return tr("Beat clock, timecode, DJ players and cues.");
    case SettingsUpdates:
        return tr("Update channel and automatic updates.");
    case SettingsShortcuts:
        return tr("Keyboard shortcuts for live use.");
    case SettingsPerformance:
        return tr("Frame time, budget and memory.");
    case SettingsPlugins:
        return tr("Extra modulation sources and image sources from plugin files.");
    default:
        return tr("Version and links.");
    }
}

std::string releaseLabel(const ReleaseInfo& release) {
    std::string label;
    if (release.version.valid) {
        label = "v" + versionToString(release.version);
    } else if (!release.name.empty()) {
        label = release.name;
    } else {
        label = release.tag;
    }
    if (release.buildNumber > 0) {
        label += "  build " + std::to_string(release.buildNumber);
    }
    return label;
}

}

std::string App::audioSourceLabel() const {
    if (audio_.selected().empty()) {
        return tr("Off");
    }
    if (audio_.selected() == AudioSystem::demoId()) {
        return tr("Demo signal");
    }
    for (const AudioDevice& device : audio_.devices()) {
        if (device.id == audio_.selected()) {
            return std::string(device.loopback ? tr("System audio: ") : tr("Input: ")) + device.name;
        }
    }
    return audio_.selected();
}

void App::openSettings(int page) {
    settingsOpen_ = true;
    settingsFocus_ = true;
    if (page >= 0) {
        settingsPage_ = std::clamp(page, 0, static_cast<int>(SettingsPageCount) - 1);
    }
    uiScaleEdit_ = settings_.uiScale;
}

void App::drawSettingsWindow() {
    if (!settingsOpen_) {
        if (control_.learning()) {
            control_.cancelLearn();
        }
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ui::setNextWindowSize(ImVec2(std::min(880.0f, viewport->Size.x - 40.0f), std::min(580.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSizeConstraints(ImVec2(560.0f, 340.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (settingsFocus_) {
        ImGui::SetNextWindowFocus();
        settingsFocus_ = false;
    }
    if (!ImGui::Begin(trId("Settings", "settingsWindow"), &settingsOpen_, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()) {
        settingsOpen_ = false;
    }

    ImGui::BeginChild("settingsNav", ImVec2(180.0f, 0.0f), ImGuiChildFlags_Borders);
    for (int i = 0; i < SettingsPageCount; ++i) {
        ImGui::PushID(i);
        const ImVec2 itemPos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(settingsPageTitle(i), settingsPage_ == i, 0, ImVec2(0.0f, 30.0f))) {
            settingsPage_ = i;
        }
        if (settingsPage_ == i) {
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(itemPos.x - 2.0f, itemPos.y), ImVec2(itemPos.x + 2.0f, itemPos.y + 30.0f), ImGui::GetColorU32(ui::kAccent));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("settingsPage", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    drawSettingsPage(settingsPage_);
    ImGui::EndChild();
    ImGui::End();
}

void App::drawThemePicker() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((avail + spacing) / (112.0f + spacing)));
    const float width = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const ImVec2 size(width, 58.0f);
    const int current = themeIndex(settings_.theme);
    for (int i = 0; i < themeCount(); ++i) {
        if (i % columns != 0) {
            ImGui::SameLine();
        }
        const ThemeInfo& theme = themeInfo(i);
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("theme", size);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        const ImU32 accent = ImGui::GetColorU32(ImVec4(theme.r, theme.g, theme.b, 1.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 4.0f);
        draw->AddRectFilled(ImVec2(pos.x + 8.0f, pos.y + 8.0f), ImVec2(max.x - 8.0f, pos.y + 26.0f), accent, 3.0f);
        draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 33.0f), IM_COL32(235, 235, 240, 255), tr(theme.name));
        if (i == current) {
            draw->AddRect(pos, max, accent, 4.0f, 0, 2.5f);
            draw->AddCircleFilled(ImVec2(max.x - 14.0f, pos.y + 40.0f), 4.0f, accent);
        } else if (hovered) {
            draw->AddRect(pos, max, IM_COL32(200, 200, 210, 255), 4.0f, 0, 1.5f);
        }
        if (clicked) {
            settings_.theme = theme.id;
            saveSettings(settings_);
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("Preview"));
    static bool toggleA = true;
    static bool toggleB = false;
    static bool checked = true;
    static float value = 0.6f;
    ui::toggleButton(trId("On", "previewOn"), toggleA);
    ImGui::SameLine();
    ui::toggleButton(trId("Off", "previewOff"), toggleB);
    ImGui::SameLine();
    ui::accentButton(trId("Button", "previewButton"), true);
    ImGui::SameLine();
    ImGui::Button(trId("Button", "previewPlain"));
    ImGui::SameLine();
    ImGui::Checkbox(trId("Checkbox", "previewCheck"), &checked);
    ImGui::SetNextItemWidth(ui::px(260.0f));
    ui::sliderFloat(trId("Slider", "previewSlider"), &value, 0.0f, 1.0f, "%.2f");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::kAccent);
    ImGui::ProgressBar(0.65f, ImVec2(260.0f, 8.0f), "");
    ImGui::PopStyleColor();
}

void App::drawSettingsPage(int page) {
    auto persist = [this]() { saveSettings(settings_); };
    if (page != SettingsControl && control_.learning()) {
        control_.cancelLearn();
    }
    if (page != SettingsShortcuts) {
        shortcutCapture_ = -1;
    }
    ImGui::PushFont(nullptr, 20.0f);
    ImGui::TextColored(ui::kAccent, "%s", settingsPageTitle(page));
    ImGui::PopFont();
    ImGui::TextDisabled("%s", settingsPageHint(page));
    ImGui::Spacing();

    if (page == SettingsGeneral) {
        ImGui::SeparatorText(tr("Language"));
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::BeginCombo("##language", languageLabel(currentLanguage()))) {
            for (int i = 0; i < languageCount(); ++i) {
                Language language = static_cast<Language>(i);
                if (ImGui::Selectable(languageLabel(language), currentLanguage() == language)) {
                    setLanguageAndSave(language);
                }
            }
            ImGui::EndCombo();
        }
        if (languageNeedsCjkFont(currentLanguage()) && cjkTried_ && !nativeLabelsAvailable()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s", "No Japanese system font was found. Install one, for example Yu Gothic or Noto Sans CJK JP, to see the Japanese text.");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", tr("Spanish, French, Italian, Portuguese, Dutch and Japanese were translated with machine help and are not reviewed by native speakers yet. Corrections are welcome on GitHub."));
        ImGui::PopStyleColor();
        if (ImGui::SmallButton(tr("Open languages folder"))) {
            std::error_code languageError;
            std::filesystem::create_directories(userLanguageDirectory(), languageError);
            platform::openFolder(userLanguageDirectory());
        }
        ImGui::SetItemTooltip("%s", tr("Your own languages: put .json files into this folder and press Reload languages. docs/LANGUAGES.md explains the format."));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Export translation template"))) {
            std::string written;
            if (writeLanguageTemplate(userLanguageDirectory(), written)) {
                toast(std::string(tr("Created: ")) + written);
            } else {
                toast(tr("The template could not be written"));
            }
        }
        ImGui::SetItemTooltip("%s", tr("Writes template.json into the languages folder. Copy it, set a code and a name, then translate the texts."));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Reload languages"))) {
            const UserLanguageReport report = loadUserLanguages(userLanguageDirectory());
            std::string message = std::string(tr("Languages loaded: ")) + std::to_string(report.loaded);
            if (report.skipped > 0) {
                message += "  " + std::string(tr("Skipped: ")) + report.firstProblem;
            } else if (report.skippedTexts > 0) {
                message += "  " + std::to_string(report.skippedTexts) + " " + tr("texts skipped (characters the font cannot show)");
            }
            toast(message);
        }
        ImGui::SeparatorText(tr("Startup"));
        if (ImGui::Checkbox(trId("Restore last session on start", "optRestore"), &settings_.restoreSession)) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("Opens the decks and layers exactly as you left them."));
        if (ImGui::Checkbox(trId("Open the output window on start", "optOpenOutput"), &settings_.openOutputOnStart)) {
            persist();
        }
        ImGui::SeparatorText(tr("Decks"));
        ImGui::TextWrapped("%s", tr("You can set up to 16 decks and switch between them with a smooth transition. Click a deck button above the clip grid, right click it for rename, duplicate and more."));
        ImGui::TextWrapped("%s", tr("Choosing a factory look in Presets loads it into the current deck."));
    } else if (page == SettingsAudio) {
        drawAudioSource();
        ImGui::SeparatorText(tr("Analysis"));
        ImGui::SetNextItemWidth(ui::px(-170.0f));
        ui::sliderFloat(trId("Gain", "audioGain"), &settings_.audioGain, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(ui::px(-170.0f));
        ui::sliderFloat(trId("Smoothing", "audioSmoothing"), &settings_.audioSmoothing, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(ui::px(-170.0f));
        ui::sliderFloat(trId("Beat sensitivity", "audioSensitivity"), &settings_.audioBeatSensitivity, 0.0f, 1.0f, "%.2f");
        if (ImGui::Checkbox(trId("Sync BPM to audio", "audioSyncBpm"), &settings_.audioSyncBpm)) {
            persist();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("Effects can react to the music in two ways. Parameter moves one value with a band. Gate shows the effect only while that band is loud."));
    } else if (page == SettingsOutput) {
        drawOutputControls();
        ImGui::SeparatorText(tr("Quality"));
        Composition& comp = engine_.comp;
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::BeginCombo(trId("Resolution", "resolution"), kResolutions[comp.resolution].label)) {
            for (int i = 0; i < kResolutionCount; ++i) {
                if (ImGui::Selectable(kResolutions[i].label, i == comp.resolution)) {
                    engine_.setResolutionIndex(i);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox(trId("Sync to display refresh (VSync)", "optVsync"), &settings_.vsync)) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("Turn this off to let the program render as fast as possible."));
        drawFrameRateLimit();
        drawSpoutControls();
        drawNdiControls();
    } else if (page == SettingsRecording) {
        drawRecordingPage();
    } else if (page == SettingsControl) {
        drawControlPage();
    } else if (page == SettingsStage) {
        drawStagePage();
    } else if (page == SettingsSync) {
        drawSyncPage();
    } else if (page == SettingsUpdates) {
        drawUpdateSection();
    } else if (page == SettingsAppearance) {
        ImGui::SeparatorText(tr("Theme"));
        drawThemePicker();
        drawInterfaceOptions();
        ImGui::SeparatorText(tr("Layout"));
        if (ImGui::Checkbox(trId("Show status bar", "optStatusBar"), &settings_.showStatusBar)) {
            persist();
        }
        if (ImGui::Button(trId("Reset window layout", "optLayoutReset"))) {
            resetWindowLayout();
        }
        ImGui::SetItemTooltip("%s", tr("Puts all panels back to their default places."));
        ImGui::SeparatorText(tr("Workspaces"));
        drawWorkspacePanel();
    } else if (page == SettingsPerformance) {
        drawPerformancePage();
    } else if (page == SettingsPlugins) {
        drawPluginsPage();
    } else if (page == SettingsShortcuts) {
        drawShortcutsPage();
    } else {
        ImGui::Text("NyrexVJ %s", NYREXVJ_VERSION);
        if (NYREXVJ_BUILD_NUMBER > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s %d", tr("build"), NYREXVJ_BUILD_NUMBER);
        }
        ImGui::TextDisabled("%s", tr("Made by MarlonXDev"));
        ImGui::Spacing();
        if (ImGui::Button(trId("What's new", "linkNews"))) {
            openWelcome(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("GitHub")) {
            platform::openUrl(ui::kRepoUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button(trId("Releases", "linkReleases"))) {
            platform::openUrl(ui::kReleasesUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button(trId("Data folder", "linkData"))) {
            platform::openFolder(userDataDir());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", userDataDir().c_str());
    }
}

void App::drawUpdateSection() {
    auto persist = [this]() { saveSettings(settings_); };
    ImGui::SeparatorText(tr("Channel"));
    int channel = settings_.channel == UpdateChannel::Nightly ? 0 : 1;
    const char* channelNames[2] = {tr("Latest build (every commit)"), tr("Stable releases")};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##channel", channelNames[channel])) {
        for (int i = 0; i < 2; ++i) {
            if (ImGui::Selectable(channelNames[i], channel == i)) {
                settings_.channel = i == 0 ? UpdateChannel::Nightly : UpdateChannel::Stable;
                updater_.setChannel(settings_.channel);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SeparatorText(tr("Automatic updates"));
    if (ImGui::Checkbox(trId("Check for updates on start", "optCheck"), &settings_.checkUpdates)) {
        persist();
    }
    ImGui::BeginDisabled(!settings_.checkUpdates);
    if (ImGui::Checkbox(trId("Download updates automatically", "optAutoInstall"), &settings_.autoInstall)) {
        persist();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("The new version is prepared in the background. You decide when to restart."));

    ImGui::SeparatorText(tr("Status"));
    UpdateStatus update = updater_.status();
    ImGui::BeginDisabled(updater_.busy());
    if (ImGui::Button(trId("Check now", "updCheck"))) {
        updater_.checkAsync();
        updateCardCollapsed_ = false;
    }
    ImGui::EndDisabled();
    if (update.state == UpdateState::Available) {
        ImGui::SameLine();
        if (ImGui::Button(trId("Download and install", "updInstall"))) {
            updater_.installAsync();
        }
    }
    if (update.state == UpdateState::ReadyToRestart) {
        ImGui::SameLine();
        if (ImGui::Button(trId("Restart now", "updRestart"))) {
            restart_ = true;
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
    if (update.state == UpdateState::Downloading) {
        ImGui::ProgressBar(update.progress, ImVec2(-FLT_MIN, 0.0f));
    }
    if (!update.message.empty()) {
        ImGui::TextWrapped("%s", trMessage(update.message).c_str());
    }
    if (update.state == UpdateState::Failed) {
        if (ImGui::Button(trId("Open download page", "updDownloadPage"))) {
            const std::string base = ui::kReleasesUrl;
            platform::openUrl(settings_.channel == UpdateChannel::Nightly ? base + "/tag/nightly" : base + "/latest");
        }
    }
    if (update.state == UpdateState::Available && !update.release.notes.empty()) {
        ImGui::BeginChild("notes", ImVec2(0.0f, 130.0f), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", update.release.notes.c_str());
        ImGui::EndChild();
    }
}

void App::drawUpdateCard() {
    const UpdateStatus update = updater_.status();
    const bool visible = update.state == UpdateState::Available || update.state == UpdateState::Downloading || update.state == UpdateState::ReadyToRestart;
    if (!visible) {
        updateCardCollapsed_ = false;
        return;
    }

    if (update.state == UpdateState::Available && settings_.autoInstall && !updater_.busy()) {
        const std::string key = update.release.tag + "#" + std::to_string(update.release.buildNumber);
        if (key != autoInstallKey_) {
            autoInstallKey_ = key;
            updater_.installAsync();
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 14.0f, viewport->WorkPos.y + 14.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::kAccent);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##updatecard", nullptr, flags)) {
        const bool ready = update.state == UpdateState::ReadyToRestart;
        const bool downloading = update.state == UpdateState::Downloading;
        const std::string version = releaseLabel(update.release);
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.2f);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        if (updateCardCollapsed_ && ready) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            draw->AddCircleFilled(ImVec2(origin.x + 6.0f, origin.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f, ImGui::GetColorU32(ImVec4(ui::kAccent.x, ui::kAccent.y, ui::kAccent.z, pulse)));
            ImGui::Dummy(ImVec2(16.0f, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            ImGui::TextUnformatted(tr("Update ready"));
            ImGui::SameLine();
            if (ImGui::SmallButton(trId("Restart now", "cardRestartMini"))) {
                restart_ = true;
                glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(trId("Details", "cardExpand"))) {
                updateCardCollapsed_ = false;
            }
        } else {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            draw->AddCircleFilled(ImVec2(origin.x + 6.0f, origin.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f, ImGui::GetColorU32(ImVec4(ui::kAccent.x, ui::kAccent.y, ui::kAccent.z, pulse)));
            ImGui::Dummy(ImVec2(16.0f, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            ImGui::PushFont(nullptr, 17.0f);
            ImGui::TextColored(ui::kAccent, "%s", ready ? tr("Update installed") : downloading ? tr("Downloading update") : tr("Update available"));
            ImGui::PopFont();
            ImGui::TextDisabled("%s", version.c_str());
            if (downloading) {
                ImGui::ProgressBar(update.progress, ImVec2(240.0f, 8.0f), "");
            } else if (ready) {
                ImGui::TextUnformatted(tr("Restart to start the new version."));
                if (ui::accentButton(trId("Restart now", "cardRestart"), true)) {
                    restart_ = true;
                    glfwSetWindowShouldClose(window_, GLFW_TRUE);
                }
                ImGui::SameLine();
                if (ImGui::Button(trId("Later", "cardLater"))) {
                    updateCardCollapsed_ = true;
                }
            } else {
                ImGui::TextUnformatted(tr("A newer version is ready to download."));
                if (ui::accentButton(trId("Download and install", "cardInstall"), true)) {
                    updater_.installAsync();
                }
                ImGui::SameLine();
                if (ImGui::Button(trId("Later", "cardLater"))) {
                    updateCardCollapsed_ = true;
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    if (ImGuiWindow* card = ImGui::FindWindowByName("##updatecard")) {
        ImGui::BringWindowToDisplayFront(card);
    }
}

void App::drawFrameRateLimit() {
    char current[32];
    if (settings_.fpsLimit > 0) {
        std::snprintf(current, sizeof(current), "%d FPS", settings_.fpsLimit);
    } else {
        std::snprintf(current, sizeof(current), "%s", tr("Unlimited"));
    }
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ImGui::BeginCombo(trId("Frame rate limit", "optFpsLimit"), current)) {
        for (const int fps : kFpsLimits) {
            char label[32];
            if (fps > 0) {
                std::snprintf(label, sizeof(label), "%d FPS", fps);
            } else {
                std::snprintf(label, sizeof(label), "%s", tr("Unlimited"));
            }
            if (ImGui::Selectable(label, settings_.fpsLimit == fps)) {
                settings_.fpsLimit = fps;
                saveSettings(settings_);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Caps how often the program draws a frame, with or without VSync. A lower limit leaves more graphics and processor time for VRChat."));
}

namespace {

const char* qualityLabel(int mode) {
    switch (mode) {
    case QualityBalanced:
        return tr("Balanced (75 %)");
    case QualityPerformance:
        return tr("Performance (50 %)");
    case QualityAuto:
        return tr("Automatic");
    default:
        return tr("Full quality");
    }
}

}

void App::drawPerformancePage() {
    auto persist = [this]() { saveSettings(settings_); };
    const double budget = budgetMilliseconds(settings_.perfBudgetFps);

    ImGui::SeparatorText(tr("Frame rate"));
    drawFrameRateLimit();
    if (ImGui::Checkbox(trId("Sync to display refresh (VSync)", "optVsync"), &settings_.vsync)) {
        persist();
    }

    drawVideoSettings();
    drawConversionSettings();
    drawVideoMonitor();

    ImGui::SeparatorText(tr("Frame budget"));
    static const int kBudgets[] = {30, 45, 60, 90, 120, 144};
    char current[48];
    std::snprintf(current, sizeof(current), "%d FPS  (%.1f ms)", settings_.perfBudgetFps, budget);
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ImGui::BeginCombo(trId("Target frame rate", "optBudget"), current)) {
        for (const int fps : kBudgets) {
            char label[48];
            std::snprintf(label, sizeof(label), "%d FPS  (%.1f ms)", fps, budgetMilliseconds(fps));
            if (ImGui::Selectable(label, settings_.perfBudgetFps == fps)) {
                settings_.perfBudgetFps = fps;
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("The budget is the time one frame may take. Frames above it count as too slow."));
    if (ImGui::Checkbox(trId("Show timings in the status bar", "optTimings"), &settings_.showTimings)) {
        persist();
    }

    ImGui::SeparatorText(tr("Render quality"));
    ImGui::SetNextItemWidth(ui::px(220.0f));
    if (ImGui::BeginCombo(trId("Layer resolution", "optQuality"), qualityLabel(settings_.renderQuality))) {
        for (int mode = 0; mode < QualityModeCount; ++mode) {
            if (ImGui::Selectable(qualityLabel(mode), settings_.renderQuality == mode)) {
                settings_.renderQuality = mode;
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Layers and their effects are drawn at a lower resolution and scaled up when they are mixed. Master effects and every output stay at full resolution. Full quality changes nothing."));
    if (settings_.renderQuality == QualityAuto) {
        ImGui::TextWrapped("%s", tr("Automatic lowers the resolution in small steps only while a frame takes longer than the budget, and raises it again when there is room."));
    }
    if (engine_.layerScale() < 0.999f) {
        ImGui::TextDisabled(tr("Layers are drawn at %d × %d (%d %%)"), engine_.layerWidth(), engine_.layerHeight(), static_cast<int>(engine_.layerScale() * 100.0f + 0.5f));
    }

    ImGui::SeparatorText(tr("Text and particles"));
    {
        int limit = settings_.particleLimit;
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(trId("Particle limit per layer", "optParticleLimit"), &limit, particles::kMinLimit, particles::kHardMaxParticles, "%d", ImGuiSliderFlags_Logarithmic)) {
            settings_.particleLimit = particles::clampLimit(limit);
            engine_.setParticleLimit(settings_.particleLimit);
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("No particle clip ever has more particles alive than this. Each particle costs 24 bytes of graphics memory. When the limit is reached, newer particles are dropped and the picture stays smooth."));
        if (ImGui::Checkbox(trId("Experimental GPU particles", "optGpuParticles"), &settings_.gpuParticles)) {
            engine_.setGpuParticles(settings_.gpuParticles, settings_.gpuParticleBudgetMb);
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("Lets particle clips that ask for it run on the graphics card with up to a million particles. If the card cannot do it, or the memory budget is used up, the clip runs on the processor with fewer particles. Off until it has been tried on a real graphics card."));
        ImGui::BeginDisabled(!settings_.gpuParticles);
        int budget = settings_.gpuParticleBudgetMb;
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(trId("Graphics memory for GPU particles", "optGpuParticleBudget"), &budget, gpuparticles::kMinBudgetMb, gpuparticles::kMaxBudgetMb, "%d MB", ImGuiSliderFlags_Logarithmic)) {
            settings_.gpuParticleBudgetMb = gpuparticles::clampBudgetMb(budget);
            engine_.setGpuParticles(settings_.gpuParticles, settings_.gpuParticleBudgetMb);
            persist();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", tr("All GPU particle clips together never use more than this. Each particle costs 64 bytes, so 64 MB hold about a million particles. A clip that does not fit runs on the processor instead."));
        if (settings_.gpuParticles) {
            ImGui::TextDisabled(tr("In use now: %.1f MB of %d MB"), static_cast<double>(engine_.gpuParticleBytes()) / (1024.0 * 1024.0), engine_.gpuParticleBudgetMb());
        }
        if (ImGui::Checkbox(trId("Show text and particle clips in the library", "optGeneratorClips"), &settings_.generatorClips)) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("Turn this off if you never use them. The change applies the next time NyrexVJ starts."));
    }

    ImGui::SeparatorText(tr("Measured"));
    if (ImGui::BeginTable("##perf", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(tr("Average"));
        ImGui::TableSetupColumn(tr("Slowest 5%"));
        ImGui::TableSetupColumn(tr("Slowest 1%"));
        ImGui::TableSetupColumn(tr("Worst"));
        ImGui::TableHeadersRow();
        const auto row = [&](const char* name, const FrameSummary& summary, bool available) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);
            const double values[4] = {summary.avg, summary.p95, summary.p99, summary.max};
            for (int i = 0; i < 4; ++i) {
                ImGui::TableSetColumnIndex(i + 1);
                if (!available) {
                    ImGui::TextDisabled("-");
                } else if (values[i] > budget) {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "%.1f ms", values[i]);
                } else {
                    ImGui::Text("%.1f ms", values[i]);
                }
            }
        };
        row(tr("Frame interval"), perfFrameSummary_, perfFrameSummary_.count > 0);
        row(tr("CPU work"), perfCpuSummary_, perfCpuSummary_.count > 0);
        row(tr("GPU"), perfGpuSummary_, engine_.gpuTimingAvailable() && perfGpuSummary_.count > 0);
        ImGui::EndTable();
    }
    if (!engine_.gpuTimingAvailable()) {
        ImGui::TextDisabled("%s", tr("GPU timing is not available on this graphics card."));
    }
    const double within = 1.0 - perfFrame_.overBudgetFraction(budget * 1.1);
    char overlay[48];
    std::snprintf(overlay, sizeof(overlay), "%.0f %%", within * 100.0);
    ImGui::ProgressBar(static_cast<float>(within), ImVec2(-170.0f, 0.0f), overlay);
    ImGui::SameLine();
    ImGui::TextUnformatted(tr("Frames within budget"));
    const std::vector<float> history = perfFrame_.ordered();
    if (!history.empty()) {
        ImGui::PlotLines("##frametimes", history.data(), static_cast<int>(history.size()), 0, nullptr, 0.0f, static_cast<float>(budget * 3.0), ImVec2(-1.0f, 70.0f));
    }
    if (perfFrameSummary_.count > 0 && perfFrameSummary_.p95 > budget * 1.1) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "%s", tr("Over budget. Use fewer layers or effects, or a lower resolution."));
    }
    if (ImGui::Button(trId("Restart measurement", "optPerfReset"))) {
        perfFrame_.clear();
        perfCpu_.clear();
        perfGpu_.clear();
        perfFrameSummary_ = FrameSummary{};
        perfCpuSummary_ = FrameSummary{};
        perfGpuSummary_ = FrameSummary{};
        diagMonitor_->reset();
    }

    drawPassProfiler();

    ImGui::SeparatorText(tr("Memory"));
    const GpuMemory memory = engine_.gpuMemory();
    const auto megabytes = [](unsigned long long bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    ImGui::Text("%s: %.0f MB", tr("GPU memory (estimate)"), megabytes(memory.total()));
    if (ImGui::BeginItemTooltip()) {
        ImGui::Text("%s: %.1f MB", tr("Render targets"), megabytes(memory.targets));
        ImGui::Text("%s: %.1f MB", tr("Thumbnails"), megabytes(memory.thumbnails));
        ImGui::Text("%s: %.1f MB", tr("3D depth buffers"), megabytes(memory.sceneDepth));
        ImGui::Text("%s: %.1f MB", tr("3D geometry and textures"), megabytes(memory.sceneGeometry));
        ImGui::Text("%s: %.1f MB", tr("Images and videos"), megabytes(memory.media));
        ImGui::Text("%s: %.1f MB", tr("Shader buffers"), megabytes(memory.shaderBuffers));
        ImGui::Text("%s: %.1f MB", tr("Text and particles"), megabytes(memory.generators));
        ImGui::EndTooltip();
    }
    ImGui::Text("%s: %.1f MB", tr("Video memory"), megabytes(memory.video.total()));
    if (ImGui::BeginItemTooltip()) {
        ImGui::Text("%s: %.1f MB", tr("Pictures"), megabytes(memory.video.output));
        ImGui::Text("%s: %.1f MB", tr("Colour planes"), megabytes(memory.video.planes));
        ImGui::Text("%s: %.1f MB", tr("Compressed (HAP)"), megabytes(memory.video.compressed));
        ImGui::Text("%s: %.1f MB", tr("Upload buffers"), megabytes(memory.video.staging));
        ImGui::EndTooltip();
    }
    ImGui::Text("%s: %.0f MB", tr("Program memory"), static_cast<double>(platform::processMemoryBytes()) / (1024.0 * 1024.0));
    ImGui::Text("%s: %d", tr("3D scenes loaded"), engine_.loadedSceneCount());

    drawGpuDiagnostics();

    ImGui::SeparatorText(tr("Load"));
    const RenderCost cost = estimateRenderCost(engine_.comp);
    ImGui::Text("%s: %d", tr("Layers on air"), cost.layers());
    ImGui::Text("%s: %d", tr("Effect passes"), cost.layerEffects + cost.masterEffects);
    ImGui::Text("%s: %d", tr("Render passes per frame"), cost.total());
}

void App::drawStatusBar() {
    if (!settings_.showStatusBar) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight() + 6.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.045f, 0.052f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (ImGui::BeginViewportSideBar("##statusbar", const_cast<ImGuiViewport*>(viewport), ImGuiDir_Down, height, flags)) {
        ImGui::AlignTextToFramePadding();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::TextColored(fps >= static_cast<float>(settings_.perfBudgetFps) * 0.92f ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%.0f FPS", fps);
        ImGui::SameLine();
        if (settings_.diagWarnings && !diagMonitor_->tracker().active().empty()) {
            drawDiagnosticsBadge();
            ImGui::SameLine();
        }
        if (settings_.showTimings && perfCpuSummary_.count > 0) {
            if (engine_.gpuTimingAvailable() && perfGpuSummary_.count > 0) {
                ImGui::TextDisabled("CPU %.1f  GPU %.1f ms", perfCpuSummary_.avg, perfGpuSummary_.avg);
            } else {
                ImGui::TextDisabled("CPU %.1f ms", perfCpuSummary_.avg);
            }
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%d x %d", engine_.width(), engine_.height());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%.1f BPM", engine_.comp.bpm);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%s %d", tr("Deck"), engine_.comp.activeDeck + 1);
        const std::string& deckName = engine_.comp.deckNames[static_cast<size_t>(engine_.comp.activeDeck)];
        if (!deckName.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", deckName.c_str());
        }
        if (engine_.transitioning()) {
            ImGui::SameLine();
            ImGui::TextColored(ui::kAccent, "%s %d  %d%%", tr("Transition"), engine_.transitionSource() + 1, static_cast<int>(engine_.transitionProgress() * 100.0f));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (!audio_.active()) {
            ImGui::TextDisabled("%s: %s", tr("Audio"), tr("Off"));
        } else if (audio_.noSignal()) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "%s: %s", tr("Audio"), tr("No signal"));
        } else {
            const float level = std::clamp(engine_.audio().level, 0.0f, 1.0f);
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", tr("Audio"));
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::kAccent);
            ImGui::ProgressBar(level, ImVec2(70.0f, 8.0f), "");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", audioSourceLabel().c_str());
        }
        drawRecordingStatusBar();
        if (spout_.active()) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(ui::kAccent, "Spout");
        }
        if (outputs_.anyOpen()) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (outputs_.openCount() > 1) {
                ImGui::TextDisabled("%s: %d", tr("Outputs open"), outputs_.openCount());
            } else {
                ImGui::TextDisabled("%s", outputs_.anyFullscreen() ? tr("Output fullscreen") : tr("Output open"));
            }
        }

        const UpdateStatus update = updater_.status();
        std::string right = std::string("NyrexVJ ") + NYREXVJ_VERSION;
        if (NYREXVJ_BUILD_NUMBER > 0) {
            right += "  build " + std::to_string(NYREXVJ_BUILD_NUMBER);
        }
        const bool pending = update.state == UpdateState::Available || update.state == UpdateState::ReadyToRestart;
        const char* pendingLabel = update.state == UpdateState::ReadyToRestart ? tr("Update ready") : tr("Update available");
        float width = ImGui::CalcTextSize(right.c_str()).x;
        if (pending) {
            width += ImGui::CalcTextSize(pendingLabel).x + ImGui::GetStyle().ItemSpacing.x + 12.0f;
        }
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - width - 16.0f));
        if (pending) {
            ImGui::TextColored(ui::kAccent, "%s", pendingLabel);
            ImGui::SameLine(0.0f, 12.0f);
        }
        ImGui::TextDisabled("%s", right.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

}

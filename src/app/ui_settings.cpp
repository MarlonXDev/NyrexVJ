#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "update/update_logic.hpp"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace nvj {

namespace {

const char* settingsPageTitle(int page) {
    switch (page) {
    case SettingsGeneral:
        return tr("General");
    case SettingsAudio:
        return tr("Audio");
    case SettingsOutput:
        return tr("Output");
    case SettingsControl:
        return tr("Control");
    case SettingsUpdates:
        return tr("Updates");
    case SettingsInterface:
        return tr("Interface");
    case SettingsShortcuts:
        return tr("Shortcuts");
    default:
        return tr("About");
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
    ImGui::SetNextWindowSize(ImVec2(std::min(820.0f, viewport->Size.x - 40.0f), std::min(540.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 340.0f), ImVec2(FLT_MAX, FLT_MAX));
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

    ImGui::BeginChild("settingsNav", ImVec2(170.0f, 0.0f), ImGuiChildFlags_Borders);
    for (int i = 0; i < SettingsPageCount; ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(settingsPageTitle(i), settingsPage_ == i, 0, ImVec2(0.0f, 28.0f))) {
            settingsPage_ = i;
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

void App::drawSettingsPage(int page) {
    auto persist = [this]() { saveSettings(settings_); };
    if (page != SettingsControl && control_.learning()) {
        control_.cancelLearn();
    }
    ImGui::PushFont(nullptr, 20.0f);
    ImGui::TextColored(ui::kAccent, "%s", settingsPageTitle(page));
    ImGui::PopFont();
    ImGui::Spacing();

    if (page == SettingsGeneral) {
        ImGui::SeparatorText(tr("Language"));
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##language", languageLabel(currentLanguage()))) {
            for (int i = 0; i < kLanguageCount; ++i) {
                Language language = static_cast<Language>(i);
                if (ImGui::Selectable(languageLabel(language), currentLanguage() == language)) {
                    setLanguageAndSave(language);
                }
            }
            ImGui::EndCombo();
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
        ImGui::SetNextItemWidth(-170.0f);
        ImGui::SliderFloat(trId("Gain", "audioGain"), &settings_.audioGain, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(-170.0f);
        ImGui::SliderFloat(trId("Smoothing", "audioSmoothing"), &settings_.audioSmoothing, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(-170.0f);
        ImGui::SliderFloat(trId("Beat sensitivity", "audioSensitivity"), &settings_.audioBeatSensitivity, 0.0f, 1.0f, "%.2f");
        if (ImGui::Checkbox(trId("Sync BPM to audio", "audioSyncBpm"), &settings_.audioSyncBpm)) {
            persist();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("Effects can react to the music in two ways. Parameter moves one value with a band. Gate shows the effect only while that band is loud."));
    } else if (page == SettingsOutput) {
        drawOutputControls();
        ImGui::SeparatorText(tr("Quality"));
        Composition& comp = engine_.comp;
        ImGui::SetNextItemWidth(220.0f);
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
        drawSpoutControls();
    } else if (page == SettingsControl) {
        drawControlPage();
    } else if (page == SettingsUpdates) {
        drawUpdateSection();
    } else if (page == SettingsInterface) {
        ImGui::SeparatorText(tr("Scale"));
        ImGui::SetNextItemWidth(260.0f);
        ImGui::SliderFloat(trId("Interface size", "optScale"), &uiScaleEdit_, 0.7f, 1.8f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settings_.uiScale = uiScaleEdit_;
            persist();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(trId("Reset", "optScaleReset"))) {
            uiScaleEdit_ = 1.0f;
            settings_.uiScale = 1.0f;
            persist();
        }
        ImGui::SeparatorText(tr("Layout"));
        if (ImGui::Checkbox(trId("Show status bar", "optStatusBar"), &settings_.showStatusBar)) {
            persist();
        }
        if (ImGui::Button(trId("Reset window layout", "optLayoutReset"))) {
            needsLayout_ = true;
            toast(tr("Window layout reset"));
        }
        ImGui::SetItemTooltip("%s", tr("Puts all panels back to their default places."));
    } else if (page == SettingsShortcuts) {
        ImGui::SeparatorText(tr("Clips and tempo"));
        ImGui::BulletText("%s", tr("1 - 8  trigger a column"));
        ImGui::BulletText("%s", tr("Space  tap tempo"));
        ImGui::BulletText("%s", tr("B  blackout"));
        ImGui::SeparatorText(tr("Decks"));
        ImGui::BulletText("%s", tr("Page Up / Page Down  previous / next deck"));
        ImGui::BulletText("%s", tr("Ctrl + 1 - 8  deck 1 - 8"));
        ImGui::BulletText("%s", tr("Ctrl + Shift + 1 - 8  deck 9 - 16"));
        ImGui::SeparatorText(tr("Window"));
        ImGui::BulletText("%s", tr("F11  output window fullscreen"));
        ImGui::BulletText("%s", tr("Ctrl + ,  open settings"));
        ImGui::BulletText("%s", tr("Right click a slider  reset effect value"));
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
        ImGui::TextColored(fps >= 50.0f ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%.0f FPS", fps);
        ImGui::SameLine();
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
        if (spout_.active()) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(ui::kAccent, "Spout");
        }
        if (output_.isOpen()) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", output_.isFullscreen() ? tr("Output fullscreen") : tr("Output open"));
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

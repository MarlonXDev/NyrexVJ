#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "platform/platform.hpp"
#include "update/changelog.hpp"

#include <algorithm>
#include <cfloat>
#include <string>

namespace nvj {

void App::drawWelcome() {
    if (welcomeRequested_) {
        ImGui::OpenPopup("###welcome");
        welcomeRequested_ = false;
        welcomeOpen_ = true;
    }
    if (!welcomeOpen_) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSize(ImVec2(std::min(780.0f, viewport->Size.x - 40.0f), std::min(640.0f, viewport->Size.y - 40.0f)), ImGuiCond_Appearing);
    const char* title = welcomeFirstRun_ ? trId("Welcome to NyrexVJ", "welcome") : trId("What's new in NyrexVJ", "welcome");
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        welcomeOpen_ = false;
        return;
    }

    ImGui::PushFont(nullptr, 26.0f);
    ImGui::TextColored(ui::kAccent, "NyrexVJ");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", NYREXVJ_VERSION);

    ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
    ImGui::SetNextItemWidth(ui::px(160.0f));
    if (ImGui::BeginCombo("##welcomeLanguage", languageLabel(currentLanguage()))) {
        for (int i = 0; i < languageCount(); ++i) {
            Language language = static_cast<Language>(i);
            if (ImGui::Selectable(languageLabel(language), currentLanguage() == language)) {
                setLanguageAndSave(language);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    if (welcomeFirstRun_) {
        ImGui::TextWrapped("%s", tr("NyrexVJ is a live visuals program for VRChat events and parties. Trigger clips, stack effects, let everything follow your music and send the picture to OBS."));
        ImGui::Spacing();
        ImGui::SeparatorText(tr("Quick start"));
        ImGui::BulletText("%s", tr("Pick a preset in the Browser or press 1 - 8 to trigger a column."));
        ImGui::BulletText("%s", tr("Choose an audio source in the Audio tab so the visuals follow your music."));
        ImGui::BulletText("%s", tr("Send the picture to OBS with Spout in Settings, Output, or use the output window (F11)."));
        ImGui::BulletText("%s", tr("Press F1 any time to open the manual."));
    } else {
        ImGui::TextWrapped("%s %s", tr("You are now using version"), baseVersionString(NYREXVJ_VERSION).c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("What's new"));
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    ImGui::BeginChild("##welcomeNotes", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
    const std::vector<ChangelogEntry>& entries = changelog(currentLanguage() == Language::German);
    for (size_t e = 0; e < entries.size(); ++e) {
        const ChangelogEntry& entry = entries[e];
        const bool isNew = !welcomeFirstRun_ && isNewerVersion(entry.version, welcomeSince_);
        std::string label = std::string(tr("Version ")) + entry.version;
        if (!entry.date.empty()) {
            label += "  (" + entry.date + ")";
        }
        if (isNew) {
            label += "  - " + std::string(tr("NEW"));
        }
        label += "###entry" + std::to_string(e);
        ImGui::SetNextItemOpen(isNew || e == 0, ImGuiCond_Appearing);
        if (isNew) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kAccent);
        }
        bool open = ImGui::CollapsingHeader(label.c_str());
        if (isNew) {
            ImGui::PopStyleColor();
        }
        if (open) {
            for (const std::string& item : entry.items) {
                ImGui::Bullet();
                ImGui::TextWrapped("%s", item.c_str());
            }
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();

    if (ui::accentButton(trId("Let's go", "welcomeClose"), true, ImVec2(120.0f, 0.0f))) {
        settings_.lastSeenVersion = baseVersionString(NYREXVJ_VERSION);
        saveSettings(settings_);
        welcomeOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Releases page", "welcomeReleases"))) {
        platform::openUrl(ui::kReleasesUrl);
    }
    ImGui::EndPopup();
}

}

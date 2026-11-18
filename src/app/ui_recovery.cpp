#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "scene3d/scene_doc.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace nvj {

void App::restoreAutosave(const AutosaveEntry& entry) {
    std::string text;
    Composition comp;
    std::string error;
    if (!readWholeFile(entry.path, text) || !compositionFromJson(text, comp, error)) {
        toast(tr("This autosave could not be read"));
        return;
    }
    loadComposition(comp, std::string(tr("Restored the autosave from ")) + formatTimestamp(entry.timestamp));
}

void App::restoreCreatorScene() {
    std::string text;
    SceneDoc doc;
    std::string error;
    if (!readCreatorRecovery(userDataDir(), text) || !docFromJson(text, doc, error)) {
        toast(tr("The unsaved Creator scene could not be read"));
        return;
    }
    restoreCreatorDoc(doc);
    creatorFocus_ = false;
    recoveryCreator_ = false;
    toast(tr("Restored the unsaved Creator scene. Save it to keep it."));
}

void App::drawRecovery() {
    if (recoveryRequested_ && !welcomeOpen_ && !welcomeRequested_) {
        ImGui::OpenPopup("###recovery");
        recoveryRequested_ = false;
        recoveryOpen_ = true;
        recoverySelected_ = 0;
        recoveryDetails_.clear();
        if (!previous_.crashReport.empty()) {
            recoveryDetails_ += previous_.crashReport;
        }
        if (!previous_.logTail.empty()) {
            recoveryDetails_ += "\n" + previous_.logTail;
        }
    }
    if (!recoveryOpen_) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(680.0f, viewport->Size.x - 40.0f), std::min(470.0f, viewport->Size.y - 40.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(trId("Recover your work", "recovery"), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        recoveryOpen_ = false;
        keepLatestAutosave();
        return;
    }

    ImGui::TextWrapped("%s", tr("NyrexVJ did not close properly last time. Pick the state you want to continue from."));
    if (!previous_.version.empty()) {
        ImGui::TextDisabled("%s %s, %s %s", tr("Version"), previous_.version.c_str(), tr("started"), formatTimestamp(previous_.startedAt).c_str());
    }
    ImGui::Spacing();
    if (previous_.crashReport.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("No crash report was written. The program was probably ended from outside, for example by a power cut or the task manager."));
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", tr("NyrexVJ crashed. A crash report was saved."));
    }

    ImGui::SeparatorText(tr("Autosaves"));
    if (recoveryEntries_.empty()) {
        ImGui::TextDisabled("%s", tr("There are no autosaves."));
    } else {
        const float listHeight = std::min(180.0f, static_cast<float>(recoveryEntries_.size()) * ImGui::GetTextLineHeightWithSpacing() + 12.0f);
        ImGui::BeginChild("##recoveryList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < recoveryEntries_.size(); ++i) {
            const AutosaveEntry& entry = recoveryEntries_[i];
            char label[160];
            std::snprintf(label, sizeof(label), "%s%s   %llu KiB###autosave%zu", formatTimestamp(entry.timestamp).c_str(), entry.latest ? (std::string("   ") + tr("latest")).c_str() : "", static_cast<unsigned long long>(entry.bytes / 1024 + 1), i);
            if (ImGui::Selectable(label, recoverySelected_ == static_cast<int>(i), ImGuiSelectableFlags_AllowDoubleClick)) {
                recoverySelected_ = static_cast<int>(i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    restoreAutosave(entry);
                    ImGui::EndChild();
                    recoveryOpen_ = false;
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    keepLatestAutosave();
                    return;
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    bool close = false;
    ImGui::BeginDisabled(recoveryEntries_.empty());
    if (ui::accentButton(trId("Restore selected", "recoveryRestore"), true, ImVec2(210.0f, 0.0f))) {
        restoreAutosave(recoveryEntries_[static_cast<size_t>(std::clamp(recoverySelected_, 0, static_cast<int>(recoveryEntries_.size()) - 1))]);
        close = true;
    }
    ImGui::EndDisabled();
    if (recoveryCreator_) {
        ImGui::SameLine();
        if (ImGui::Button(trId("Open the unsaved Creator scene", "recoveryCreator"))) {
            restoreCreatorScene();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Start fresh", "recoveryFresh"))) {
        close = true;
    }

    if (!recoveryDetails_.empty()) {
        ImGui::Spacing();
        if (ImGui::CollapsingHeader(tr("Details for bug reports"))) {
            if (ImGui::Button(trId("Copy details", "recoveryCopy"))) {
                ImGui::SetClipboardText(recoveryDetails_.c_str());
            }
            ImGui::InputTextMultiline("##recoveryDetails", recoveryDetails_.data(), recoveryDetails_.size() + 1, ImVec2(-1.0f, 120.0f), ImGuiInputTextFlags_ReadOnly);
        }
    }

    if (close) {
        recoveryOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (close) {
        keepLatestAutosave();
    }
}

void App::keepLatestAutosave() {
    for (const AutosaveEntry& entry : recoveryEntries_) {
        if (!entry.latest) {
            continue;
        }
        std::string text;
        if (readWholeFile(entry.path, text)) {
            writeRotatingAutosave(userDataDir(), text, entry.timestamp);
        }
    }
}

}

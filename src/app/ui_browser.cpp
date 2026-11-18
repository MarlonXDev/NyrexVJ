#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace nvj {

void App::drawPresetsTab() {
    ImGui::TextDisabled("%s", tr("Factory"));
    const std::vector<Composition>& factory = factoryPresets();
    for (int i = 0; i < static_cast<int>(factory.size()); ++i) {
        const Composition& preset = factory[static_cast<size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Selectable(preset.name.c_str(), engine_.comp.name == preset.name)) {
            loadComposition(preset, std::string(tr("Loaded ")) + preset.name);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("My presets"));
    if (userPresets_.empty()) {
        ImGui::TextDisabled("%s", tr("Nothing saved yet"));
    }
    std::string toDelete;
    for (int i = 0; i < static_cast<int>(userPresets_.size()); ++i) {
        const std::string& name = userPresets_[static_cast<size_t>(i)];
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(name.c_str(), selectedUserPreset_ == name)) {
            Composition loaded;
            std::string error;
            if (loadUserPreset(name, loaded, error)) {
                selectedUserPreset_ = name;
                loadComposition(loaded, std::string(tr("Loaded ")) + name);
            } else {
                toast(trMessage(error));
            }
        }
        if (ImGui::BeginPopupContextItem("presetmenu")) {
            if (ImGui::MenuItem(tr("Overwrite with current"))) {
                std::snprintf(presetName_, sizeof(presetName_), "%s", name.c_str());
                saveCurrentPreset();
            }
            if (ImGui::MenuItem(tr("Delete"))) {
                toDelete = name;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (!toDelete.empty()) {
        deleteUserPreset(toDelete);
        if (selectedUserPreset_ == toDelete) {
            selectedUserPreset_.clear();
        }
        refreshUserPresets();
        toast(std::string(tr("Deleted ")) + toDelete);
    }

    ImGui::SeparatorText(tr("Save current look"));
    const char* saveLabel = tr("Save");
    const float saveWidth = ImGui::CalcTextSize(saveLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x + 4.0f;
    ImGui::SetNextItemWidth(-saveWidth);
    ImGui::InputText("##presetname", presetName_, sizeof(presetName_));
    ImGui::SameLine();
    if (ImGui::Button(trId("Save", "presetSave"))) {
        saveCurrentPreset();
    }
}

void App::drawLibraryTab() {
    const std::vector<ClipDef>& library = clipLibrary();
    ImGui::TextDisabled("%s", tr("Drag a clip onto a deck cell"));
    const float thumbW = 96.0f;
    const float thumbH = 54.0f;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(library.size()); ++i) {
        const ClipDef& clip = library[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::BeginGroup();
        ImGui::Image(ui::texId(engine_.clipThumbnail(i)), ImVec2(thumbW, thumbH), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        ImGui::PushClipRect(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + thumbW, ImGui::GetItemRectMax().y + ImGui::GetTextLineHeightWithSpacing()), true);
        ImGui::TextUnformatted(clip.name.c_str());
        ImGui::PopClipRect();
        ImGui::EndGroup();
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("NVJ_CLIP", &i, sizeof(int));
            ImGui::TextUnformatted(clip.name.c_str());
            ImGui::EndDragDropSource();
        }
        float nextX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + thumbW;
        if (i + 1 < static_cast<int>(library.size()) && nextX < right) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText(tr("Effects"));
    const std::vector<EffectDef>& effects = effectLibrary();
    for (const EffectDef& effect : effects) {
        ImGui::BulletText("%s%s", effect.name.c_str(), effect.is3d ? "  (3D)" : "");
    }
}

void App::drawAudioTab() {
    std::string preview = tr("Off");
    if (audio_.selected() == AudioSystem::demoId()) {
        preview = tr("Demo signal");
    } else if (!audio_.selected().empty()) {
        preview = audio_.selected();
        for (const AudioDevice& device : audio_.devices()) {
            if (device.id == audio_.selected()) {
                preview = std::string(device.loopback ? tr("System audio: ") : tr("Input: ")) + device.name;
            }
        }
    }

    ImGui::TextDisabled("%s", tr("Audio source"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##audiosource", preview.c_str())) {
        if (ImGui::Selectable(tr("Off"), audio_.selected().empty())) {
            audio_.select("");
            settings_.audioDevice.clear();
        }
        if (ImGui::Selectable(tr("Demo signal"), audio_.selected() == AudioSystem::demoId())) {
            audio_.select(AudioSystem::demoId());
        }
        bool loopbackHeader = false;
        bool inputHeader = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (const AudioDevice& device : audio_.devices()) {
                if (device.loopback != (pass == 0)) {
                    continue;
                }
                bool& header = pass == 0 ? loopbackHeader : inputHeader;
                if (!header) {
                    ImGui::SeparatorText(pass == 0 ? tr("System audio (what your PC plays)") : tr("Inputs (microphone, line-in)"));
                    header = true;
                }
                ImGui::PushID(device.id.c_str());
                if (ImGui::Selectable(device.name.c_str(), audio_.selected() == device.id)) {
                    if (audio_.select(device.id)) {
                        settings_.audioDevice = device.id;
                    } else {
                        toast(trMessage(audio_.error()));
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::SmallButton(trId("Refresh devices", "audioRefresh"))) {
        audio_.refreshDevices();
    }
    if (!audio_.error().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", trMessage(audio_.error()).c_str());
    }
    if (audio_.lost()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", tr("The audio device is no longer available."));
    }

    const AudioFeatures& features = engine_.audio();
    ImGui::Spacing();
    if (!audio_.active()) {
        ImGui::TextWrapped("%s", tr("Audio is off. Clips and effects follow the BPM clock instead."));
    }

    ImVec2 size(ImGui::GetContentRegionAvail().x, 84.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(14, 14, 17, 255));
    const float barWidth = size.x / static_cast<float>(kSpectrumBins);
    for (int i = 0; i < kSpectrumBins; ++i) {
        const float value = std::clamp(features.spectrum[static_cast<size_t>(i)], 0.0f, 1.0f);
        const float x0 = origin.x + barWidth * static_cast<float>(i);
        const float t = static_cast<float>(i) / static_cast<float>(kSpectrumBins - 1);
        const ImU32 color = IM_COL32(static_cast<int>(255.0f - 255.0f * t), static_cast<int>(115.0f + 85.0f * t), static_cast<int>(0.0f + 255.0f * t), 255);
        draw->AddRectFilled(ImVec2(x0, origin.y + size.y * (1.0f - value)), ImVec2(x0 + std::max(1.0f, barWidth - 1.0f), origin.y + size.y), color);
    }
    ImGui::Dummy(size);

    const char* names[4] = {tr("Bass"), tr("Mid"), tr("High"), tr("Level")};
    const float values[4] = {features.bass, features.mid, features.high, features.level};
    for (int i = 0; i < 4; ++i) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::kAccent);
        ImGui::ProgressBar(std::clamp(values[i], 0.0f, 1.0f), ImVec2(-FLT_MIN, 12.0f), "");
        ImGui::PopStyleColor();
        ImGui::SameLine(8.0f);
        ImGui::TextUnformatted(names[i]);
    }

    ImVec2 dot = ImGui::GetCursorScreenPos();
    const float kick = std::clamp(features.kick, 0.0f, 1.0f);
    draw->AddCircleFilled(ImVec2(dot.x + 8.0f, dot.y + 9.0f), 7.0f, ImGui::GetColorU32(ImVec4(1.0f, 0.45f + 0.4f * kick, 0.0f + 0.5f * kick, 0.25f + 0.75f * kick)));
    ImGui::Dummy(ImVec2(18.0f, 18.0f));
    ImGui::SameLine();
    if (audio_.active() && features.bpm > 0.0f) {
        ImGui::Text("%s %.1f BPM", tr("Detected tempo"), features.bpm);
        ImGui::SameLine();
        ImGui::TextDisabled("(%d%%)", static_cast<int>(features.bpmConfidence * 100.0f));
        ImGui::SameLine();
        if (ImGui::SmallButton(trId("Use", "audioUseBpm"))) {
            engine_.comp.bpm = std::clamp(features.bpm, 40.0f, 240.0f);
        }
    } else if (audio_.active()) {
        ImGui::TextDisabled("%s", tr("Listening for the beat..."));
    } else {
        ImGui::TextDisabled("%s", tr("Beat"));
    }
    ImGui::Checkbox(trId("Sync BPM to audio", "audioSyncBpm"), &settings_.audioSyncBpm);

    ImGui::SeparatorText(tr("Analysis"));
    ImGui::SliderFloat(trId("Gain", "audioGain"), &settings_.audioGain, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(trId("Smoothing", "audioSmoothing"), &settings_.audioSmoothing, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(trId("Beat sensitivity", "audioSensitivity"), &settings_.audioBeatSensitivity, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr("System audio listens to everything your PC plays, for example a DJ program or a music player. Inputs are microphones, line-in and audio interfaces."));
    ImGui::TextWrapped("%s", tr("Use the Audio reactivity rows on layers, the master and effects to let values follow Bass, Mid, High, Level or Kick."));
#if !defined(_WIN32)
    ImGui::TextWrapped("%s", tr("Capturing from devices is only available on Windows. Use the demo signal to try the audio reaction."));
#endif
}

void App::drawSettingsTab() {
    ImGui::Text("NyrexVJ %s", NYREXVJ_VERSION);
    if (NYREXVJ_BUILD_NUMBER > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s %d", tr("build"), NYREXVJ_BUILD_NUMBER);
    }
    ImGui::TextDisabled("%s", tr("Made by MarlonXDev"));

    ImGui::SeparatorText(tr("Language"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##language", languageLabel(currentLanguage()))) {
        for (int i = 0; i < kLanguageCount; ++i) {
            Language language = static_cast<Language>(i);
            if (ImGui::Selectable(languageLabel(language), currentLanguage() == language)) {
                setLanguageAndSave(language);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText(tr("Updates"));
    int channel = settings_.channel == UpdateChannel::Nightly ? 0 : 1;
    const char* channelNames[2] = {tr("Latest build (every commit)"), tr("Stable releases")};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##channel", channelNames[channel])) {
        for (int i = 0; i < 2; ++i) {
            if (ImGui::Selectable(channelNames[i], channel == i)) {
                settings_.channel = i == 0 ? UpdateChannel::Nightly : UpdateChannel::Stable;
                updater_.setChannel(settings_.channel);
                saveSettings(settings_);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox(trId("Check for updates on start", "optCheck"), &settings_.checkUpdates)) {
        saveSettings(settings_);
    }
    if (ImGui::Checkbox(trId("Restore last session on start", "optRestore"), &settings_.restoreSession)) {
        saveSettings(settings_);
    }

    UpdateStatus update = updater_.status();
    ImGui::BeginDisabled(updater_.busy());
    if (ImGui::Button(trId("Check now", "updCheck"))) {
        updater_.checkAsync();
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
    if (update.state == UpdateState::Available && !update.release.notes.empty()) {
        ImGui::BeginChild("notes", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", update.release.notes.c_str());
        ImGui::EndChild();
    }

    ImGui::SeparatorText(tr("Links and files"));
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
    ImGui::TextDisabled("%s", userDataDir().c_str());

    ImGui::SeparatorText(tr("Shortcuts"));
    ImGui::BulletText("%s", tr("1 - 8  trigger a column"));
    ImGui::BulletText("%s", tr("Space  tap tempo"));
    ImGui::BulletText("%s", tr("B  blackout"));
    ImGui::BulletText("%s", tr("F11  output window fullscreen"));
    ImGui::BulletText("%s", tr("Right click a slider  reset effect value"));
}

void App::drawBrowser() {
    ImGui::Begin(trId("Browser", "browser"));
    if (ImGui::BeginTabBar("browser_tabs")) {
        const char* names[] = {trId("Presets", "tabPresets"), trId("Library", "tabLibrary"), trId("Audio", "tabAudio"), trId("Settings", "tabSettings")};
        for (int i = 0; i < 4; ++i) {
            ImGuiTabItemFlags flags = focusTab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, flags)) {
                if (i == 0) {
                    drawPresetsTab();
                } else if (i == 1) {
                    drawLibraryTab();
                } else if (i == 2) {
                    drawAudioTab();
                } else {
                    drawSettingsTab();
                }
                ImGui::EndTabItem();
            }
        }
        focusTab_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}

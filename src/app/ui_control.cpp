#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "app/ui_targets.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cfloat>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.45f, 0.35f, 1.0f);

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isGlobalItem(ControlItem item) {
    return item == ControlItem::DeckSelect || item == ControlItem::DeckNext || item == ControlItem::DeckPrevious || item == ControlItem::ColumnTrigger || item == ControlItem::Macro;
}

std::string itemName(const ControlTarget& target) {
    switch (target.item) {
    case ControlItem::Level:
        return tr("Level");
    case ControlItem::Blackout:
        return tr("Blackout");
    case ControlItem::Bpm:
        return "BPM";
    case ControlItem::Tap:
        return tr("Tap tempo");
    case ControlItem::Resync:
        return tr("Resync");
    case ControlItem::Hue:
        return tr("Hue");
    case ControlItem::Saturation:
        return tr("Saturation");
    case ControlItem::Brightness:
        return tr("Brightness");
    case ControlItem::Contrast:
        return tr("Contrast");
    case ControlItem::Opacity:
        return tr("Opacity");
    case ControlItem::Speed:
        return tr("Speed");
    case ControlItem::Scale:
        return tr("Scale");
    case ControlItem::Bypass:
        return tr("Bypass");
    case ControlItem::Solo:
        return tr("Solo");
    case ControlItem::ClearLayer:
        return tr("Clear layer");
    case ControlItem::ClipTrigger:
        return std::string(tr("Clip")) + " " + std::to_string(target.index + 1);
    case ControlItem::ColumnTrigger:
        return tr("Column ") + std::to_string(target.index + 1);
    case ControlItem::DeckSelect:
        return std::string(tr("Deck")) + " " + std::to_string(target.index + 1);
    case ControlItem::DeckNext:
        return tr("Next deck");
    case ControlItem::DeckPrevious:
        return tr("Previous deck");
    case ControlItem::EffectEnable:
        return std::string(tr("Effect")) + " " + std::to_string(target.slot + 1) + " / " + tr("Enable");
    case ControlItem::EffectParam:
        return std::string(tr("Effect")) + " " + std::to_string(target.slot + 1) + " / " + tr("Parameter") + " " + std::to_string(target.param + 1);
    case ControlItem::Macro:
        return std::string(tr("Macro")) + " " + std::to_string(target.index + 1);
    case ControlItem::FeedbackFreeze:
        return tr("Freeze feedback");
    case ControlItem::FeedbackReset:
        return tr("Reset feedback");
    }
    return "";
}

}

std::string targetLabel(const ControlTarget& target) {
    std::string prefix;
    if (target.layer >= 0) {
        prefix = tr("Layer ") + std::to_string(target.layer + 1) + " / ";
    } else if (!isGlobalItem(target.item)) {
        prefix = std::string(tr("Master")) + " / ";
    }
    return prefix + itemName(target);
}

std::string targetLabelById(const std::string& id) {
    const int index = controlTargetIndex(id);
    if (index < 0) {
        return id;
    }
    return targetLabel(controlTargets()[static_cast<size_t>(index)]);
}

namespace {

const char* kindLabel(ControlKind kind) {
    switch (kind) {
    case ControlKind::Toggle:
        return tr("on / off");
    case ControlKind::Trigger:
        return tr("trigger");
    default:
        return tr("value");
    }
}

}

void App::drawControlPage() {
    if (control_.learning() && !ImGui::IsPopupOpen("##pickTarget")) {
        ImGui::BeginChild("learnBox", ImVec2(0.0f, 52.0f), ImGuiChildFlags_Borders);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ui::kAccent, "%s", tr("Move a knob or press a key for"));
        ImGui::SameLine();
        ImGui::Text("%s", targetLabelById(control_.learnTarget()).c_str());
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - 110.0f));
        if (ImGui::Button(trId("Cancel", "learnCancel"), ImVec2(90.0f, 0.0f))) {
            control_.cancelLearn();
        }
        ImGui::EndChild();
        ImGui::Spacing();
    }
    ui::sectionHeader("OSC");
    drawOscSection();
    ui::sectionHeader(tr("Web interface"));
    drawApiSection();
    ui::sectionHeader(tr("OSC output"));
    drawOscOutSection();
    ui::sectionHeader("MIDI");
    drawMidiSection();
    ui::sectionHeader(tr("Controller profiles"));
    drawProfilesSection();
    ui::sectionHeader(tr("Mappings"));
    drawBindingsSection();
}

void App::drawApiSection() {
    auto persist = [this]() { saveSettings(settings_); };
    ImGui::TextWrapped("%s", tr("Control NyrexVJ from a web page in your browser. It stays off until you turn it on, and every request needs the token below."));
    if (ImGui::Checkbox(trId("Turn on the web interface", "apiEnable"), &settings_.apiEnabled)) {
        persist();
    }
    ImGui::BeginDisabled(!settings_.apiEnabled);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Port");
    ImGui::SameLine();
    int port = settings_.apiPort;
    ImGui::SetNextItemWidth(ui::px(120.0f));
    ImGui::InputInt("##apiPort", &port, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.apiPort = std::clamp(port, 1024, 65535);
        persist();
    }
    if (ImGui::Checkbox(trId("Allow other devices on the network", "apiRemote"), &settings_.apiAllowRemote)) {
        persist();
    }
    ImGui::EndDisabled();
    if (settings_.apiEnabled && settings_.apiAllowRemote) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%s", tr("Anyone on your network who has the token can then control NyrexVJ. The connection is not encrypted. Only turn this on in a network you trust."));
    }

    ImGui::Spacing();
    if (!settings_.apiEnabled) {
        ImGui::TextDisabled("%s", tr("The web interface is off"));
        return;
    }
    if (api_.running()) {
        ImGui::TextColored(kGood, "%s%d", tr("Listening on port "), api_.port());
        ImGui::TextDisabled("%s", api_.allowRemote() ? tr("Other devices on the network can reach it with the IP address of this computer") : tr("Only this computer can reach it"));
    } else if (!api_.error().empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(api_.error()).c_str());
        if (ImGui::Button(trId("Try again", "apiRetry"))) {
            api_.restart();
        }
    }
    if (!api_.warning().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%s", trMessage(api_.warning()).c_str());
    }
    if (!api_.running()) {
        return;
    }

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID showId = ImGui::GetID("apiShowToken");
    bool showToken = storage->GetBool(showId, false);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Token"));
    ImGui::SameLine();
    char tokenText[96] = {};
    std::snprintf(tokenText, sizeof(tokenText), "%s", api_.token().c_str());
    ImGui::SetNextItemWidth(ui::px(260.0f));
    ImGui::InputText("##apiToken", tokenText, sizeof(tokenText), ImGuiInputTextFlags_ReadOnly | (showToken ? 0 : ImGuiInputTextFlags_Password));
    ImGui::SameLine();
    if (ImGui::Button(showToken ? trId("Hide", "apiHide") : trId("Show", "apiShow"))) {
        storage->SetBool(showId, !showToken);
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Copy token", "apiCopy"))) {
        ImGui::SetClipboardText(api_.token().c_str());
        toast(tr("Copied"));
    }
    if (ImGui::Button(trId("Open web interface", "apiOpen"))) {
        platform::openUrl(api_.browserUrl());
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("New token", "apiNewToken"))) {
        api_.regenerateToken();
    }
    ImGui::SetItemTooltip("%s", tr("A new token signs out every open web page."));
    ImGui::TextDisabled("%s", tr("The token is saved in a separate file. Do not share it or show it on a stream."));
    const api::ServerStats server = api_.serverStats();
    const api::Stats* stats = api_.routerStats();
    if (stats != nullptr) {
        ImGui::TextDisabled("%s: %llu   %s: %llu   %s: %zu", tr("Requests"), static_cast<unsigned long long>(stats->requests.load()), tr("Refused"), static_cast<unsigned long long>(stats->authFailures.load() + stats->rejected.load() + stats->limited.load() + server.refused), tr("Waiting"), api_.queued());
    }
}

void App::drawOscOutSection() {
    auto persist = [this]() { saveSettings(settings_); };
    ImGui::TextWrapped("%s", tr("OSC output sends tempo, beats, audio levels and chosen values to other programs, for example Unreal Engine. All messages start with /nyrex/out/."));
    if (ImGui::Checkbox(trId("Send OSC data", "oscOutEnable"), &settings_.oscOutEnabled)) {
        persist();
    }
    ImGui::BeginDisabled(!settings_.oscOutEnabled);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Address"));
    ImGui::SameLine();
    char host[64] = {};
    std::snprintf(host, sizeof(host), "%s", settings_.oscOutHost.c_str());
    ImGui::SetNextItemWidth(ui::px(150.0f));
    ImGui::InputText("##oscOutHost", host, sizeof(host));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.oscOutHost = host[0] ? host : "127.0.0.1";
        persist();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Port");
    ImGui::SameLine();
    int port = settings_.oscOutPort;
    ImGui::SetNextItemWidth(ui::px(90.0f));
    ImGui::InputInt("##oscOutPort", &port, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.oscOutPort = std::clamp(port, 1, 65535);
        persist();
    }
    int rate = settings_.oscOutRate;
    ImGui::SetNextItemWidth(ui::px(160.0f));
    if (ui::sliderInt(trId("Updates per second", "oscOutRate"), &rate, 10, 120)) {
        settings_.oscOutRate = rate;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        persist();
    }
    if (ImGui::Checkbox(trId("Send spectrum bands", "oscOutSpectrum"), &settings_.oscOutSpectrum)) {
        persist();
    }
    ImGui::EndDisabled();
    if (settings_.oscOutEnabled && settings_.oscOutHost != "127.0.0.1" && settings_.oscOutHost != "localhost") {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%s", tr("Data is sent to another address. Only use addresses in a network you trust."));
    }

    ImGui::Spacing();
    if (!settings_.oscOutEnabled) {
        ImGui::TextDisabled("%s", tr("OSC output is off"));
    } else if (oscOut_.sending()) {
        ImGui::TextColored(kGood, "%s%s:%d", tr("Sending to "), settings_.oscOutHost.c_str(), settings_.oscOutPort);
        ImGui::SameLine();
        ImGui::TextDisabled("(%llu %s)", oscOut_.packets(), tr("packets"));
    } else if (!oscOut_.error().empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(oscOut_.error()).c_str());
    }

    if (ImGui::TreeNode(trId("Parameters to send", "oscOutParams"))) {
        ImGui::TextWrapped("%s", tr("Each parameter sends the 0 to 1 value of one control as /nyrex/out/param/<name>."));
        const auto& targets = controlTargets();
        int removeIndex = -1;
        for (size_t i = 0; i < settings_.oscOutParams.size(); ++i) {
            OscOutParam& param = settings_.oscOutParams[i];
            ImGui::PushID(static_cast<int>(i));
            char name[40] = {};
            std::snprintf(name, sizeof(name), "%s", param.name.c_str());
            ImGui::SetNextItemWidth(ui::px(120.0f));
            ImGui::InputText("##name", name, sizeof(name));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string clean;
                for (const char* c = name; *c; ++c) {
                    if (validOscParamName(std::string(1, *c))) {
                        clean += *c;
                    }
                }
                param.name = clean.empty() ? "param" : clean.substr(0, 32);
                persist();
            }
            ImGui::SameLine();
            const int index = controlTargetIndex(param.target);
            const std::string label = index < 0 ? param.target : targetLabel(targets[static_cast<size_t>(index)]);
            if (ImGui::Button((label + "##pick").c_str(), ImVec2(std::max(120.0f, ImGui::GetContentRegionAvail().x - 70.0f), 0.0f))) {
                oscOutPick_ = static_cast<int>(i);
                oscOutFilter_[0] = '\0';
                ImGui::OpenPopup("##pickOutTarget");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Remove"))) {
                removeIndex = static_cast<int>(i);
            }
            ImGui::TextDisabled("/nyrex/out/param/%s", param.name.c_str());
            ImGui::PopID();
        }
        if (removeIndex >= 0) {
            settings_.oscOutParams.erase(settings_.oscOutParams.begin() + removeIndex);
            persist();
        }
        ImGui::BeginDisabled(settings_.oscOutParams.size() >= kOscMaxParams);
        if (ImGui::Button(trId("Add parameter", "oscOutAdd"))) {
            settings_.oscOutParams.push_back({"param" + std::to_string(settings_.oscOutParams.size() + 1), "master/level"});
            persist();
        }
        ImGui::EndDisabled();

        ui::setNextWindowSize(ImVec2(420.0f, 360.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##pickOutTarget")) {
            ImGui::TextColored(ui::kAccent, "%s", tr("Choose the value to send"));
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##pickOutFilter", tr("Search"), oscOutFilter_, sizeof(oscOutFilter_));
            const std::string filter = lower(oscOutFilter_);
            std::vector<std::pair<int, std::string>> shown;
            for (size_t i = 0; i < targets.size(); ++i) {
                if (targets[i].kind == ControlKind::Trigger) {
                    continue;
                }
                std::string entry = targetLabel(targets[i]);
                if (filter.empty() || lower(entry).find(filter) != std::string::npos || lower(targets[i].id).find(filter) != std::string::npos) {
                    shown.emplace_back(static_cast<int>(i), std::move(entry));
                }
            }
            ImGui::BeginChild("pickOutList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shown.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto& entry = shown[static_cast<size_t>(row)];
                    ImGui::PushID(row);
                    if (ImGui::Selectable(entry.second.c_str()) && oscOutPick_ >= 0 && oscOutPick_ < static_cast<int>(settings_.oscOutParams.size())) {
                        settings_.oscOutParams[static_cast<size_t>(oscOutPick_)].target = targets[static_cast<size_t>(entry.first)].id;
                        persist();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::TreePop();
    }
}

void App::drawOscSection() {
    auto persist = [this]() { saveSettings(settings_); };
    if (ImGui::Checkbox(trId("Receive OSC messages", "oscEnable"), &settings_.oscEnabled)) {
        persist();
    }
    ImGui::BeginDisabled(!settings_.oscEnabled);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Port");
    ImGui::SameLine();
    int port = settings_.oscPort;
    ImGui::SetNextItemWidth(ui::px(120.0f));
    ImGui::InputInt("##oscPort", &port, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.oscPort = std::clamp(port, 1, 65535);
        persist();
    }
    if (ImGui::Checkbox(trId("Accept messages from other computers", "oscRemote"), &settings_.oscAllowRemote)) {
        persist();
    }
    ImGui::EndDisabled();
    if (settings_.oscEnabled && settings_.oscAllowRemote) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%s", tr("Anyone on your network can then control NyrexVJ. Only turn this on in a network you trust."));
    }

    ImGui::Spacing();
    const bool wanted = settings_.oscEnabled || oscOverridePort_ > 0;
    if (!wanted) {
        ImGui::TextDisabled("%s", tr("OSC is off"));
    } else if (control_.oscListening()) {
        ImGui::TextColored(kGood, "%s%d", tr("Listening on port "), control_.oscPort());
    } else if (!control_.oscError().empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(control_.oscError()).c_str());
    }
    if (wanted) {
        if (control_.lastOsc().empty()) {
            ImGui::TextDisabled("%s", tr("Nothing received yet"));
        } else {
            ImGui::TextDisabled("%s: %s", tr("Last message"), control_.lastOsc().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%llu)", control_.oscCount());
        }
    }

    if (ImGui::TreeNode(trId("OSC addresses", "oscAddresses"))) {
        ImGui::TextWrapped("%s", tr("Every address starts with /nyrex/. Send a value from 0 to 1. Click an address to copy it."));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##oscFilter", tr("Search"), targetFilter_, sizeof(targetFilter_));
        const std::string filter = lower(targetFilter_);
        std::vector<int> shown;
        const auto& targets = controlTargets();
        for (size_t i = 0; i < targets.size(); ++i) {
            if (filter.empty() || lower(targets[i].id).find(filter) != std::string::npos || lower(targetLabel(targets[i])).find(filter) != std::string::npos) {
                shown.push_back(static_cast<int>(i));
            }
        }
        ImGui::BeginChild("oscList", ImVec2(0.0f, 190.0f), ImGuiChildFlags_Borders);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ControlTarget& target = targets[static_cast<size_t>(shown[static_cast<size_t>(row)])];
                ImGui::PushID(row);
                const std::string address = "/nyrex/" + target.id;
                if (ImGui::Selectable(address.c_str())) {
                    ImGui::SetClipboardText(address.c_str());
                    toast(tr("Copied"));
                }
                ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 70.0f);
                ImGui::TextDisabled("%s", kindLabel(target.kind));
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }
}

void App::drawMidiSection() {
    auto persist = [this]() { saveSettings(settings_); };
    const std::vector<std::string>& devices = control_.midiDevices();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Device"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui::px(280.0f));
    const std::string current = settings_.midiDevice.empty() ? std::string(tr("None")) : settings_.midiDevice;
    if (ui::beginCombo("##midiDevice", current.c_str())) {
        if (ImGui::Selectable(tr("None"), settings_.midiDevice.empty())) {
            settings_.midiDevice.clear();
            persist();
        }
        for (size_t i = 0; i < devices.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(devices[i].c_str(), devices[i] == settings_.midiDevice)) {
                settings_.midiDevice = devices[i];
                persist();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Refresh devices", "midiRefresh"))) {
        control_.refreshMidiDevices();
    }

    if (control_.midiRunning()) {
        ImGui::TextColored(kGood, "%s%s", tr("Receiving from "), settings_.midiDevice.c_str());
    } else if (!control_.midiError().empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(control_.midiError()).c_str());
    } else if (devices.empty()) {
        ImGui::TextDisabled("%s", tr("No MIDI devices found"));
    } else {
        ImGui::TextDisabled("%s", tr("MIDI is off"));
    }
    if (!control_.lastMidi().empty()) {
        ImGui::TextDisabled("%s: %s", tr("Last message"), control_.lastMidi().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%llu)", control_.midiCount());
    }

    ImGui::Spacing();
    if (ImGui::Checkbox(trId("Send feedback to the controller", "midiFeedback"), &settings_.midiFeedback)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Lights pads and moves faders on a controller that has an output port. Pads light while their clip, column, deck or switch is active, faders follow the value."));
    if (!control_.midiOutSupported()) {
        ImGui::TextDisabled("%s", trMessage("MIDI feedback is only available on Windows in this version").c_str());
    }
    ImGui::BeginDisabled(!settings_.midiFeedback || !control_.midiOutSupported());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Feedback device"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui::px(280.0f));
    const std::vector<std::string>& outputs = control_.midiOutDevices();
    const std::string currentOut = settings_.midiOutDevice.empty() ? std::string(tr("None")) : settings_.midiOutDevice;
    if (ui::beginCombo("##midiOutDevice", currentOut.c_str())) {
        if (ImGui::Selectable(tr("None"), settings_.midiOutDevice.empty())) {
            settings_.midiOutDevice.clear();
            persist();
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(outputs[i].c_str(), outputs[i] == settings_.midiOutDevice)) {
                settings_.midiOutDevice = outputs[i];
                persist();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Refresh devices", "midiOutRefresh"))) {
        control_.refreshMidiOutDevices();
    }
    ImGui::EndDisabled();
    if (settings_.midiFeedback && control_.midiOutSupported()) {
        if (control_.midiOutOpen()) {
            ImGui::TextColored(kGood, "%s%s", tr("Sending feedback to "), settings_.midiOutDevice.c_str());
        } else if (!control_.midiOutError().empty()) {
            ImGui::TextColored(kBad, "%s", trMessage(control_.midiOutError()).c_str());
        } else if (settings_.midiOutDevice.empty()) {
            ImGui::TextDisabled("%s", tr("Choose the device that receives the feedback"));
        }
    }
}

void App::drawProfilesSection() {
    ImGui::TextWrapped("%s", tr("A profile saves all your mappings under a name, so you can switch controllers or share the file. Loading a profile replaces the current mappings."));
    ImGui::SetNextItemWidth(ui::px(220.0f));
    ImGui::InputTextWithHint("##profileName", tr("Profile name"), profileName_, sizeof(profileName_));
    ImGui::SameLine();
    if (ImGui::Button(trId("Save profile", "profileSave"))) {
        std::string error;
        if (control_.saveProfile(profileName_, error)) {
            profileMessage_ = tr("Profile saved");
            profileMessageGood_ = true;
        } else {
            profileMessage_ = trMessage(error);
            profileMessageGood_ = false;
        }
    }
    const std::vector<std::string> profiles = control_.listProfiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(profiles[i].c_str());
        ImGui::SameLine(260.0f);
        if (ImGui::SmallButton(trId("Load", "profileLoad"))) {
            std::string error;
            if (control_.loadProfile(profiles[i], error)) {
                profileMessage_ = tr("Profile loaded");
                profileMessageGood_ = true;
                std::snprintf(profileName_, sizeof(profileName_), "%s", profiles[i].c_str());
            } else {
                profileMessage_ = trMessage(error);
                profileMessageGood_ = false;
            }
        }
        ImGui::PopID();
    }
    if (profiles.empty()) {
        ImGui::TextDisabled("%s", tr("No profiles yet"));
    }
    if (!profileMessage_.empty()) {
        ImGui::TextColored(profileMessageGood_ ? kGood : kBad, "%s", profileMessage_.c_str());
    }
    ImGui::TextDisabled("%s", ControlSystem::profileDirectory().string().c_str());
}

void App::drawBindingsSection() {
    if (ImGui::Button(trId("Add mapping", "mapAdd"))) {
        targetFilter_[0] = '\0';
        ImGui::OpenPopup("##pickTarget");
    }

    ui::setNextWindowSize(ImVec2(420.0f, 360.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##pickTarget")) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Choose what to control"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##pickFilter", tr("Search"), targetFilter_, sizeof(targetFilter_));
        const std::string filter = lower(targetFilter_);
        const auto& targets = controlTargets();
        std::vector<std::pair<int, std::string>> shown;
        for (size_t i = 0; i < targets.size(); ++i) {
            std::string label = targetLabel(targets[i]);
            if (filter.empty() || lower(label).find(filter) != std::string::npos || lower(targets[i].id).find(filter) != std::string::npos) {
                shown.emplace_back(static_cast<int>(i), std::move(label));
            }
        }
        ImGui::BeginChild("pickList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& entry = shown[static_cast<size_t>(row)];
                ImGui::PushID(row);
                if (ImGui::Selectable(entry.second.c_str())) {
                    control_.startLearn(-1, targets[static_cast<size_t>(entry.first)].id);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 70.0f);
                ImGui::TextDisabled("%s", kindLabel(targets[static_cast<size_t>(entry.first)].kind));
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    std::vector<MidiBinding>& bindings = control_.bindings();
    if (bindings.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("No mappings yet. Add one, pick what it controls, then move a knob or press a key on your controller."));
        return;
    }

    ImGui::Spacing();
    int removeIndex = -1;
    if (ImGui::BeginTable("bindings", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Source"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn(tr("Controls"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Invert"), ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableSetupColumn("##learn", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < bindings.size(); ++i) {
            MidiBinding& binding = bindings[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string kind = binding.kind == MidiKind::ControlChange ? "CC" : "Note";
            const std::string channel = binding.channel == 0 ? std::string(tr("any")) : std::to_string(binding.channel);
            ImGui::Text("%s %d  ch %s", kind.c_str(), binding.number, channel.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(targetLabelById(binding.target).c_str());
            ImGui::TableNextColumn();
            bool inverted = binding.low > binding.high;
            if (ImGui::Checkbox("##invert", &inverted)) {
                std::swap(binding.low, binding.high);
                control_.markDirty();
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(trId("Learn", "mapLearn"))) {
                control_.startLearn(static_cast<int>(i), binding.target);
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(trId("Remove", "mapRemove"))) {
                removeIndex = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0) {
        bindings.erase(bindings.begin() + removeIndex);
        control_.markDirty();
    }
}

}

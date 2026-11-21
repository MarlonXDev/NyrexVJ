#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "app/ui_targets.hpp"
#include "core/cue_list.hpp"
#include "core/timecode.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

constexpr float kLabelColumn = 170.0f;
constexpr float kFieldWidth = 220.0f;

const SyncMode kModes[] = {SyncMode::Auto, SyncMode::Manual, SyncMode::Audio, SyncMode::MidiClock, SyncMode::Timecode, SyncMode::DjLink};

const SyncSource kRealSources[] = {SyncSource::DjLink, SyncSource::Timecode, SyncSource::MidiClock, SyncSource::Audio};

const char* sourceLabel(SyncSource source) {
    switch (source) {
    case SyncSource::Audio:
        return tr("Audio");
    case SyncSource::MidiClock:
        return tr("MIDI clock");
    case SyncSource::Timecode:
        return tr("Timecode");
    case SyncSource::DjLink:
        return "Pro DJ Link";
    default:
        return tr("Manual");
    }
}

const char* modeLabel(SyncMode mode) {
    if (mode == SyncMode::Auto) {
        return tr("Automatic");
    }
    return sourceLabel(modeSource(mode));
}

const char* lockLabel(LockState state) {
    switch (state) {
    case LockState::Searching:
        return tr("Searching");
    case LockState::Acquiring:
        return tr("Acquiring");
    case LockState::Locked:
        return tr("Locked");
    case LockState::Lost:
        return tr("Lost");
    default:
        return tr("Free running");
    }
}

ImVec4 lockColor(LockState state) {
    switch (state) {
    case LockState::Locked:
        return kGood;
    case LockState::Lost:
        return kBad;
    case LockState::Searching:
    case LockState::Acquiring:
        return kWarn;
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
}

const char* rateText(FrameRate rate) {
    switch (rate) {
    case FrameRate::Fps24:
        return "24 fps";
    case FrameRate::Fps25:
        return "25 fps";
    case FrameRate::Fps2997Drop:
        return "29.97 fps DF";
    default:
        return "30 fps";
    }
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

void warnText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void rowLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(kLabelColumn);
}

void valueRow(const char* label, const char* value, const ImVec4* color = nullptr) {
    rowLabel(label);
    if (color != nullptr) {
        ImGui::TextColored(*color, "%s", value);
    } else {
        ImGui::TextUnformatted(value);
    }
}

std::string number(unsigned long long value) {
    return std::to_string(value);
}

std::string fixed(double value, int digits) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

}

void App::drawSyncPage() {
    ImGui::SeparatorText(tr("Beat clock"));
    drawSyncClockSection();
    ImGui::Spacing();
    ImGui::SeparatorText(tr("MIDI clock"));
    drawSyncMidiSection();
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Timecode"));
    drawSyncTimecodeSection();
    ImGui::Spacing();
    ImGui::SeparatorText("Pro DJ Link");
    drawSyncDjLinkSection();
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Cue list"));
    drawSyncCueSection();
}

void App::drawSyncClockSection() {
    auto persist = [this]() { saveSettings(settings_); };
    const BeatClock& clock = engine_.beatClock();
    const SyncMode current = syncModeFromName(settings_.syncMode.c_str());

    rowLabel(tr("Clock source"));
    ImGui::SetNextItemWidth(kFieldWidth);
    if (ImGui::BeginCombo("##syncMode", modeLabel(current))) {
        for (SyncMode mode : kModes) {
            if (ImGui::Selectable(modeLabel(mode), mode == current)) {
                settings_.syncMode = syncModeName(mode);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Automatic uses the best source that is present: Pro DJ Link, then timecode, MIDI clock, audio and finally manual."));

    const SyncSource active = clock.active();
    const LockState state = clock.state();
    const ImVec4 stateColor = lockColor(state);
    valueRow(tr("Active source"), sourceLabel(active));
    valueRow(tr("State"), lockLabel(state), &stateColor);
    valueRow("BPM", fixed(clock.bpm(), 2).c_str());
    const double errorMs = clock.phaseError() * 60.0 / std::max(1.0, clock.bpm()) * 1000.0;
    valueRow(tr("Phase error"), (fixed(errorMs, 1) + " ms").c_str());
    rowLabel(tr("Lock quality"));
    ImGui::ProgressBar(static_cast<float>(std::clamp(clock.lockQuality(), 0.0, 1.0)), ImVec2(kFieldWidth, 0.0f), "");

    for (SyncSource source : kRealSources) {
        const bool present = clock.present(source);
        const std::string label = std::string(sourceLabel(source));
        rowLabel(label.c_str());
        if (present) {
            const std::string text = source == active ? std::string(tr("Present")) + " (" + tr("active") + ")" : std::string(tr("Present"));
            ImGui::TextColored(kGood, "%s", text.c_str());
        } else {
            ImGui::TextDisabled("%s", tr("Not present"));
        }
    }

    ImGui::Spacing();
    rowLabel(tr("Latency"));
    ImGui::SetNextItemWidth(kFieldWidth);
    ImGui::SliderFloat("##syncLatency", &settings_.syncLatencyMs, -500.0f, 500.0f, "%.0f ms");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.syncLatencyMs = std::clamp(settings_.syncLatencyMs, -500.0f, 500.0f);
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Positive values move the beat forward, negative values hold it back. Use this to make up for delay in cables, network or screens."));

    if (ImGui::Checkbox(trId("Align to the bar start", "syncBarAlign"), &settings_.syncBarAlign)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Sources that report the position inside the bar also set the bar start. Turn this off to only follow the tempo and the beat."));
}

void App::drawSyncMidiSection() {
    const MidiClockTracker& midi = sync_.midiClock();
    const bool active = midi.active(sync_.midiNowSeconds());
    const ImVec4 good = kGood;
    valueRow(tr("Clock"), active ? tr("Receiving") : tr("Not receiving"), active ? &good : nullptr);
    if (active) {
        valueRow("BPM", fixed(midi.bpm(), 2).c_str());
        valueRow(tr("Transport"), midi.running() ? tr("Running") : tr("Stopped"));
        valueRow(tr("Position"), midi.positionKnown() ? tr("Known") : tr("Unknown, tempo only"));
    }
    valueRow(tr("Ticks"), number(midi.ticks()).c_str());
    if (midi.gaps() > 0) {
        valueRow(tr("Gaps"), number(midi.gaps()).c_str());
    }
    ImGui::TextDisabled("%s", tr("Choose the MIDI device on the Control page. Clock, start, stop and MTC use the same port."));
    if (settings_.midiDevice.empty()) {
        warnText(tr("No MIDI device is selected."));
    }
}

void App::drawSyncTimecodeSection() {
    auto persist = [this]() { saveSettings(settings_); };

    ImGui::PushFont(nullptr, 26.0f);
    if (sync_.timecodeActive()) {
        ImGui::TextColored(ui::kAccent, "%s", formatTimecode(sync_.timecode()).c_str());
    } else {
        ImGui::TextDisabled("--:--:--:--");
    }
    ImGui::PopFont();
    ImGui::SameLine();
    if (sync_.timecodeActive()) {
        const char* kind = sync_.timecodeSource() == TimecodeSource::Ltc ? "LTC" : "MTC";
        const std::string text = std::string(kind) + "  " + rateText(sync_.timecode().rate);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kGood, "%s", text.c_str());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("No timecode"));
    }

    ImGui::Spacing();
    if (ltcDeviceListedFrame_ < 0) {
        sync_.refreshLtcDevices();
        ltcDeviceListedFrame_ = 0;
    }
    const std::vector<AudioDevice>& devices = sync_.ltcDevices();
    std::string currentName = tr("Off");
    if (!settings_.ltcDevice.empty()) {
        currentName = settings_.ltcDevice;
        for (const AudioDevice& device : devices) {
            if (device.id == settings_.ltcDevice) {
                currentName = device.name;
            }
        }
    }
    rowLabel(tr("LTC input"));
    ImGui::SetNextItemWidth(kFieldWidth + 80.0f);
    if (ImGui::BeginCombo("##ltcDevice", currentName.c_str())) {
        if (ImGui::Selectable(tr("Off"), settings_.ltcDevice.empty())) {
            settings_.ltcDevice.clear();
            persist();
        }
        for (const AudioDevice& device : devices) {
            if (device.loopback) {
                continue;
            }
            ImGui::PushID(device.id.c_str());
            if (ImGui::Selectable(device.name.c_str(), device.id == settings_.ltcDevice)) {
                settings_.ltcDevice = device.id;
                persist();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Refresh", "ltcRefresh"))) {
        sync_.refreshLtcDevices();
    }

    if (!settings_.ltcDevice.empty()) {
        if (!sync_.ltcError().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kBad);
            ImGui::TextWrapped("%s", sync_.ltcError().c_str());
            ImGui::PopStyleColor();
        } else if (sync_.ltcRunning()) {
            const bool signal = sync_.ltc().signalSeen();
            const ImVec4 good = kGood;
            valueRow(tr("Input"), signal ? tr("Signal detected") : tr("No signal"), signal ? &good : nullptr);
            const std::string counts = number(sync_.ltc().frames()) + " " + tr("frames") + "   " + number(sync_.ltc().resyncs()) + " " + tr("resyncs") + "   " + number(sync_.ltc().rejected()) + " " + tr("rejected");
            valueRow("LTC", counts.c_str());
        }
    }
    ImGui::TextDisabled("%s", tr("LTC is read from its own audio input. Use a line input that only carries the timecode signal."));

    ImGui::Spacing();
    const TimecodeTracker& mtc = sync_.mtcTracker();
    const bool mtcRunning = mtc.running();
    const ImVec4 good = kGood;
    valueRow("MTC", mtcRunning ? tr("Receiving") : tr("Not receiving"), mtcRunning ? &good : nullptr);
    if (mtc.fixes() > 0) {
        const std::string counts = number(mtc.fixes()) + " " + tr("fixes") + "   " + number(mtc.jumps()) + " " + tr("jumps") + "   " + number(sync_.mtc().dropped()) + " " + tr("dropped");
        valueRow(tr("MTC statistics"), counts.c_str());
    }
    ImGui::TextDisabled("%s", tr("If LTC and MTC both run, LTC is used."));
}

void App::drawSyncDjLinkSection() {
    auto persist = [this]() { saveSettings(settings_); };
    if (ImGui::Checkbox(trId("Listen for Pro DJ Link beats", "djLinkEnable"), &settings_.djLinkEnabled)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Opens a UDP listener on port 50001 in your local network and follows the beat packets of the players."));

    ImGui::BeginDisabled(!settings_.djLinkEnabled);
    const int selected = std::clamp(settings_.djLinkPlayer, 0, kDjLinkMaxPlayer);
    const std::string playerText = selected == 0 ? std::string(tr("Automatic")) : std::string(tr("Player ")) + std::to_string(selected);
    rowLabel(tr("Follow player"));
    ImGui::SetNextItemWidth(kFieldWidth);
    if (ImGui::BeginCombo("##djPlayer", playerText.c_str())) {
        for (int i = 0; i <= kDjLinkMaxPlayer; ++i) {
            const std::string label = i == 0 ? std::string(tr("Automatic")) : std::string(tr("Player ")) + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected == i)) {
                settings_.djLinkPlayer = i;
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (!settings_.djLinkEnabled) {
        ImGui::TextDisabled("%s", tr("Off. NyrexVJ does not listen on the network."));
        return;
    }
    if (!sync_.djLinkError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", sync_.djLinkError().c_str());
        ImGui::PopStyleColor();
        return;
    }

    const DjLinkTracker& dj = sync_.djLink();
    const ImVec4 good = kGood;
    valueRow(tr("Listener"), sync_.djLinkListening() ? tr("Listening") : tr("Not listening"), sync_.djLinkListening() ? &good : nullptr);
    valueRow(tr("Beats"), dj.active() ? tr("Receiving") : tr("Not receiving"), dj.active() ? &good : nullptr);
    if (dj.active()) {
        valueRow(tr("Following"), (std::string(tr("Player ")) + std::to_string(dj.followed())).c_str());
        valueRow("BPM", fixed(dj.bpm(), 2).c_str());
        if (dj.beatInBar() > 0) {
            valueRow(tr("Beat in bar"), std::to_string(dj.beatInBar()).c_str());
        }
    }
    std::string seen;
    for (int i = 1; i <= kDjLinkMaxPlayer; ++i) {
        if (dj.playerSeen(i)) {
            seen += (seen.empty() ? "" : ", ") + std::to_string(i);
        }
    }
    valueRow(tr("Players seen"), seen.empty() ? "-" : seen.c_str());
    const std::string counts = number(dj.packets()) + " " + tr("packets") + "   " + number(dj.ignored()) + " " + tr("ignored") + "   " + number(dj.malformed()) + " " + tr("malformed");
    valueRow(tr("Packets"), counts.c_str());
    ImGui::TextDisabled("%s", tr("Only the beat packets are used. NyrexVJ never announces itself as a player and gets no play state."));
}

void App::drawSyncCueSection() {
    auto persist = [this]() { saveSettings(settings_); };
    std::vector<Cue>& cues = sync_.cues();

    if (ImGui::Checkbox(trId("Run cues", "cuesEnable"), &settings_.cuesEnabled)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Fires each cue once when the timecode passes its time. Cues do not fire after large jumps."));

    const CueRunner& runner = sync_.cueRunner();
    if (settings_.cuesEnabled) {
        if (runner.tracking()) {
            valueRow(tr("Position"), formatTimecode(sync_.timecode()).c_str());
        } else {
            valueRow(tr("Position"), tr("Waiting for timecode"));
        }
        if (runner.skipped() > 0) {
            valueRow(tr("Skipped after jumps"), number(runner.skipped()).c_str());
        }
    }

    FrameRate rate = FrameRate::Fps25;
    if (sync_.timecodeSource() == TimecodeSource::Ltc) {
        rate = sync_.ltcTracker().rate();
    } else if (sync_.timecodeSource() == TimecodeSource::Mtc) {
        rate = sync_.mtcTracker().rate();
    } else if (sync_.ltcTracker().everSeen()) {
        rate = sync_.ltcTracker().rate();
    } else if (sync_.mtcTracker().everSeen()) {
        rate = sync_.mtcTracker().rate();
    }

    ImGui::Spacing();
    bool openPicker = false;
    bool needSort = false;
    int removeRow = -1;
    if (cues.empty()) {
        ImGui::TextDisabled("%s", tr("No cues yet. Add a cue to trigger a clip, column or value at a timecode."));
    } else {
        const float height = std::min(260.0f, 34.0f + static_cast<float>(cues.size()) * 28.0f);
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##cueTable", 4, flags, ImVec2(0.0f, height))) {
            ImGui::TableSetupColumn(tr("Time"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(tr("Action"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("Value"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("##cueButtons", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < cues.size(); ++i) {
                Cue& cue = cues[i];
                const int row = static_cast<int>(i);
                ImGui::PushID(row);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                char text[24];
                if (cueEditRow_ == row) {
                    std::memcpy(text, cueTimeBuffer_, sizeof(text));
                } else {
                    std::snprintf(text, sizeof(text), "%s", formatTimecode(timecodeFromSeconds(cue.seconds, rate)).c_str());
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##time", text, sizeof(text));
                if (ImGui::IsItemActive()) {
                    cueEditRow_ = row;
                    std::memcpy(cueTimeBuffer_, text, sizeof(cueTimeBuffer_));
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    Timecode parsed;
                    if (parseTimecode(text, rate, parsed)) {
                        cue.seconds = timecodeToSeconds(parsed);
                        needSort = true;
                        sync_.markCuesDirty();
                    }
                }
                if (!ImGui::IsItemActive() && cueEditRow_ == row) {
                    cueEditRow_ = -1;
                }

                ImGui::TableSetColumnIndex(1);
                const std::string label = targetLabelById(cue.target);
                if (ImGui::Selectable(label.c_str())) {
                    cueTargetPick_ = row;
                    cueFilter_[0] = '\0';
                    openPicker = true;
                }

                ImGui::TableSetColumnIndex(2);
                const int targetIndex = controlTargetIndex(cue.target);
                const ControlKind kind = targetIndex >= 0 ? controlTargets()[static_cast<size_t>(targetIndex)].kind : ControlKind::Continuous;
                if (kind == ControlKind::Trigger) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", tr("trigger"));
                } else if (kind == ControlKind::Toggle) {
                    bool on = cue.value > 0.5f;
                    if (ImGui::Checkbox("##on", &on)) {
                        cue.value = on ? 1.0f : 0.0f;
                        sync_.markCuesDirty();
                    }
                } else {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat("##value", &cue.value, 0.005f, 0.0f, 1.0f, "%.2f")) {
                        cue.value = std::clamp(cue.value, 0.0f, 1.0f);
                        sync_.markCuesDirty();
                    }
                }

                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton(trId("Test", "cueTest"))) {
                    control_.fireTarget(cue.target, cue.value);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(trId("Delete", "cueDelete"))) {
                    removeRow = row;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    if (removeRow >= 0 && removeRow < static_cast<int>(cues.size())) {
        cues.erase(cues.begin() + removeRow);
        cueEditRow_ = -1;
        sync_.markCuesDirty();
    }
    if (needSort) {
        sortCues(cues);
        cueEditRow_ = -1;
    }

    ImGui::BeginDisabled(cues.size() >= kMaxCues);
    if (ImGui::Button(trId("Add cue", "cueAdd"))) {
        cueTargetPick_ = -1;
        cueFilter_[0] = '\0';
        openPicker = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", sync_.timecodeActive() ? tr("The new cue is placed at the current timecode.") : tr("Without timecode the new cue is placed after the last one."));
    if (cues.size() >= kMaxCues) {
        warnText(tr("The cue list is full."));
    }

    if (openPicker) {
        ImGui::OpenPopup("##pickCueTarget");
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 360.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##pickCueTarget")) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Choose what the cue does"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##cueFilter", tr("Search"), cueFilter_, sizeof(cueFilter_));
        const std::string filter = lower(cueFilter_);
        const auto& targets = controlTargets();
        std::vector<std::pair<int, std::string>> shown;
        for (size_t i = 0; i < targets.size(); ++i) {
            std::string entry = targetLabel(targets[i]);
            if (filter.empty() || lower(entry).find(filter) != std::string::npos || lower(targets[i].id).find(filter) != std::string::npos) {
                shown.emplace_back(static_cast<int>(i), std::move(entry));
            }
        }
        ImGui::BeginChild("##cuePickList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& entry = shown[static_cast<size_t>(row)];
                ImGui::PushID(row);
                if (ImGui::Selectable(entry.second.c_str())) {
                    const std::string& id = targets[static_cast<size_t>(entry.first)].id;
                    if (cueTargetPick_ >= 0 && cueTargetPick_ < static_cast<int>(cues.size())) {
                        cues[static_cast<size_t>(cueTargetPick_)].target = id;
                    } else if (cues.size() < kMaxCues) {
                        Cue cue;
                        cue.target = id;
                        cue.value = 1.0f;
                        if (sync_.timecodeActive()) {
                            cue.seconds = sync_.timecodeSeconds();
                        } else if (!cues.empty()) {
                            cue.seconds = cues.back().seconds + 1.0;
                        }
                        cues.push_back(cue);
                        sortCues(cues);
                    }
                    sync_.markCuesDirty();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

}

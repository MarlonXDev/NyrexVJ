#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/modulation.hpp"
#include "core/ndi_sender.hpp"
#include "render/spout_output.hpp"
#include "scene3d/scene_pack.hpp"
#include "render/transition_library.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

std::string customLabel(const std::string& name) {
    const size_t colon = name.find(": ");
    return colon == std::string::npos ? name : name.substr(colon + 2);
}

bool transitionCombo(const char* id, DeckSettings& settings) {
    const std::string preview = settings.customTransition.empty() ? std::string(tr(transitionName(settings.transition))) : customLabel(settings.customTransition);
    bool changed = false;
    if (ui::beginCombo(id, preview.c_str())) {
        for (int t = 0; t < TransitionTypeCount; ++t) {
            if (ImGui::Selectable(tr(transitionName(t)), settings.customTransition.empty() && settings.transition == t)) {
                settings.transition = t;
                settings.customTransition.clear();
                changed = true;
            }
        }
        const std::vector<TransitionDef>& custom = transitionLibrary();
        if (!custom.empty()) {
            ImGui::SeparatorText(tr("Your transitions"));
            for (const TransitionDef& def : custom) {
                const std::string label = customLabel(def.name) + (def.usable() ? "" : "  (!)");
                if (ImGui::Selectable(label.c_str(), settings.customTransition == def.name)) {
                    if (settings.customTransition != def.name) {
                        settings.transitionValues = unsetClipValues();
                    }
                    settings.customTransition = def.name;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void meterRow(const char* label, float value) {
    ui::fieldLabel(label);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::kAccent);
    ImGui::ProgressBar(std::clamp(value, 0.0f, 1.0f), ImVec2(-FLT_MIN, ui::px(10.0f)), "");
    ImGui::PopStyleColor();
}

}

void App::drawTransportBar() {
    Composition& comp = engine_.comp;
    drawDeckBar();

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("BPM");
    ImGui::SameLine();
    float bpm = comp.bpm;
    ImGui::SetNextItemWidth(ui::px(74.0f));
    if (ImGui::DragFloat("##bpm", &bpm, 0.1f, 40.0f, 240.0f, "%.1f")) {
        comp.bpm = std::clamp(bpm, 40.0f, 240.0f);
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("-##bpmDown")) {
        comp.bpm = std::clamp(comp.bpm - 1.0f, 40.0f, 240.0f);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("+##bpmUp")) {
        comp.bpm = std::clamp(comp.bpm + 1.0f, 40.0f, 240.0f);
    }
    if (ImGui::GetContentRegionAvail().x > ui::px(640.0f)) {
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("/2")) {
            comp.bpm = std::clamp(comp.bpm * 0.5f, 40.0f, 240.0f);
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (ImGui::Button("x2")) {
            comp.bpm = std::clamp(comp.bpm * 2.0f, 40.0f, 240.0f);
        }
    }
    ImGui::SameLine(0.0f, ui::px(10.0f));
    if (ImGui::Button(trId("Tap", "tap"))) {
        engine_.tap();
    }
    ImGui::SetItemTooltip("%s", tr("Tap the tempo (Space)"));
    if (ImGui::GetContentRegionAvail().x > ui::px(300.0f)) {
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button(trId("Resync", "resync"))) {
            engine_.resync();
        }
        ImGui::SetItemTooltip("%s", tr("Restart the beat count at 1"));
    }

    ImGui::SameLine(0.0f, ui::px(18.0f));
    if (ui::accentButton(trId("Play", "play"), !engine_.paused)) {
        engine_.paused = false;
    }
    ImGui::SetItemTooltip("%s", tr("Runs the tempo clock and all clip motion"));
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::accentButton(trId("Pause", "pause"), engine_.paused)) {
        engine_.paused = !engine_.paused;
    }
    ImGui::SetItemTooltip("%s", tr("Freezes the tempo clock and all clip motion"));
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(trId("Stop", "stop"))) {
        engine_.paused = true;
        engine_.resync();
    }
    ImGui::SetItemTooltip("%s", tr("Pauses and restarts the beat count at 1"));

    const int bar = clampBeatsPerBar(comp.beatsPerBar);
    const double beat = engine_.beat();
    const int index = static_cast<int>(std::floor(beat)) % bar;
    const float phase = static_cast<float>(beat - std::floor(beat));
    if (ImGui::GetContentRegionAvail().x > ui::px(130.0f)) {
        ImGui::SameLine(0.0f, ui::px(18.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s %d/%d", tr("Beat"), index + 1, bar);
    }

    const float segment = ui::px(16.0f);
    const float gap = ui::px(3.0f);
    const float ticks = static_cast<float>(bar) * (segment + gap);
    if (ImGui::GetContentRegionAvail().x > ticks + ui::px(90.0f)) {
        ImGui::SameLine(0.0f, ui::px(12.0f));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float frame = ImGui::GetFrameHeight();
        ImGui::Dummy(ImVec2(ticks - gap, frame));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float top = origin.y + (frame - ui::px(8.0f)) * 0.5f;
        const float bottom = top + ui::px(8.0f);
        for (int i = 0; i < bar; ++i) {
            const float x0 = origin.x + static_cast<float>(i) * (segment + gap);
            const float fill = i < index ? 1.0f : (i == index ? phase : 0.0f);
            draw->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + segment, bottom), IM_COL32(40, 40, 46, 255), 2.0f);
            if (fill > 0.0f) {
                const ImVec4 color = i == 0 ? ui::kAccentHover : ui::kAccent;
                draw->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + segment * fill, bottom), ImGui::GetColorU32(color), 2.0f);
            }
        }
        ImGui::SameLine(0.0f, ui::px(12.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s %.2f", tr("Phase"), phase);
    }

    if (ImGui::GetContentRegionAvail().x > ui::px(960.0f)) {
        DeckSettings& deckSettings = comp.deckSettings;
        ImGui::SameLine(0.0f, ui::px(22.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("Transition"));
        ImGui::SameLine(0.0f, ui::px(6.0f));
        ImGui::SetNextItemWidth(ui::px(104.0f));
        transitionCombo("##tbTransition", deckSettings);
        if (ImGui::GetContentRegionAvail().x > ui::px(1180.0f)) {
            ImGui::SameLine(0.0f, ui::px(10.0f));
            ImGui::Checkbox(trId("Beat sync", "tbSync"), &deckSettings.syncBeats);
            ImGui::SetItemTooltip("%s", tr("Transition length follows the tempo"));
            ImGui::SameLine(0.0f, ui::px(10.0f));
            ImGui::Checkbox(trId("Auto cycle", "tbCycle"), &deckSettings.autoCycle);
            ImGui::SetItemTooltip("%s", tr("Switches to the next used deck by itself"));
        }
    }

    if (engine_.transitioning()) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + size.y - 3.0f), ImVec2(pos.x + size.x * engine_.transitionProgress(), pos.y + size.y), ImGui::GetColorU32(ui::kAccent));
    }
}

void App::drawTransitionDesigner() {
    DeckSettings& settings = engine_.comp.deckSettings;
    const int customIndex = settings.customTransition.empty() ? -1 : customTransitionIndex(settings.customTransition);
    const TransitionDef* def = customIndex >= 0 ? &transitionLibrary()[static_cast<size_t>(customIndex)] : nullptr;
    std::string problem;
    if (!settings.customTransition.empty()) {
        problem = def ? engine_.customTransitionError(settings.customTransition) : std::string("The transition file is not in the shaders folder");
        if (problem.empty() && def && !def->usable()) {
            problem = def->error;
        }
    }
    if (!settings.customTransition.empty() && !problem.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kBad, "%s", tr("This transition cannot run. The built-in transition is used instead."));
        ImGui::TextDisabled("%s", trMessage(problem).c_str());
        ImGui::PopTextWrapPos();
    }
    if (def && problem.empty() && def->plan) {
        drawShaderControlValues(*def->plan, settings.transitionValues);
    }

    ui::sectionHeader(tr("Preview"));
    ImGui::Checkbox(tr("Play"), &transitionPreviewPlay_);
    ImGui::SameLine();
    if (transitionPreviewPlay_) {
        transitionPreviewProgress_ = static_cast<float>(std::fmod(ImGui::GetTime() * 0.5, 1.0));
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::sliderFloat("##transPreviewProgress", &transitionPreviewProgress_, 0.0f, 1.0f, "%.2f");
    ShaderValues values = unsetShaderValues();
    if (def && def->plan && problem.empty()) {
        values = resolveShaderValues(*def->plan, settings.transitionValues);
    }
    engine_.requestTransitionPreview(problem.empty() ? settings.customTransition : std::string(), settings.transition, transitionPreviewProgress_, values);
    if (engine_.transitionPreviewTexture() != 0) {
        const float width = std::min(ImGui::GetContentRegionAvail().x, ui::px(240.0f));
        ImGui::Image(ui::texId(engine_.transitionPreviewTexture()), ImVec2(width, width * 9.0f / 16.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    if (ImGui::SmallButton(tr("Create example transition"))) {
        const std::string created = createExampleTransition();
        if (created.empty()) {
            toast(tr("The example transition could not be created"));
        } else {
            syncNodeEffects(std::string());
            toast(std::string(tr("Created: ")) + created);
        }
    }
    ImGui::SetItemTooltip("%s", tr("Writes a small transition file into the shaders folder. Edit it in the shader editor and it shows up in the list above."));
    if (def && !settings.customTransition.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Edit in shader editor"))) {
            openShaderEditor(std::filesystem::path(pathFromUtf8(def->path)).filename().string());
        }
    }
}

void App::drawTrigger() {
    if (!ImGui::Begin(trId("Trigger", "trigger"))) {
        ImGui::End();
        return;
    }
    Composition& comp = engine_.comp;
    DeckSettings& settings = comp.deckSettings;

    ui::sectionHeader(tr("Clip start"));
    ui::fieldLabel(tr("Quantize"));
    {
        const char* gridLabels[kQuantizeGridCount] = {tr("Off"), tr("Beat"), tr("Bar"), tr("2 bars"), tr("4 bars")};
        const QuantizeGrid current = quantizeGridFromName(settings_.launchQuantize.c_str());
        const size_t waiting = engine_.quantizer().pending();
        ImGui::SetNextItemWidth(waiting > 0 ? -ui::px(34.0f) : -FLT_MIN);
        if (ui::beginCombo("##launchQuantize", gridLabels[static_cast<int>(current)])) {
            for (int g = 0; g < kQuantizeGridCount; ++g) {
                if (ImGui::Selectable(gridLabels[g], static_cast<int>(current) == g)) {
                    settings_.launchQuantize = quantizeGridName(static_cast<QuantizeGrid>(g));
                    saveSettings(settings_);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("Clips, columns and decks that you start with a click, a key, MIDI, OSC or a cue wait for the next beat or bar. If the beat clock is lost, they start at once."));
        if (waiting > 0) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(ui::kAccent, "%zu", waiting);
        }
    }

    ui::sectionHeader(tr("Transition"));
    ui::fieldLabel(tr("Type"));
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        transitionCombo("##transition", settings);
    }
    ImGui::Checkbox(trId("Beat sync", "transSync"), &settings.syncBeats);
    ImGui::SetItemTooltip("%s", tr("Transition length follows the tempo"));
    if (settings.syncBeats) {
        ui::sliderInt(tr("Length"), &settings.beats, 1, 32, "%d beats");
    } else {
        ui::sliderFloat(tr("Length"), &settings.seconds, 0.0f, 10.0f, "%.1f s");
    }
    ImGui::Checkbox(trId("Auto cycle", "transCycle"), &settings.autoCycle);
    ImGui::SetItemTooltip("%s", tr("Switches to the next used deck by itself"));
    if (settings.autoCycle) {
        ui::sliderInt(tr("every"), &settings.cycleBeats, 4, 128, "%d beats");
    }
    drawTransitionDesigner();

    ui::sectionHeader(tr("Beat grid"));
    int beatsPerBar = comp.beatsPerBar;
    if (ui::sliderInt(tr("Beats per bar"), &beatsPerBar, kMinBeatsPerBar, kMaxBeatsPerBar)) {
        comp.beatsPerBar = clampBeatsPerBar(beatsPerBar);
    }
    const int bar = clampBeatsPerBar(comp.beatsPerBar);
    ui::fieldLabel(tr("Nudge"));
    if (ImGui::Button("<<")) {
        engine_.shiftBeat(-0.0625);
    }
    ImGui::SetItemTooltip("%s", tr("Move the beat grid earlier by a sixteenth of a beat"));
    ImGui::SameLine();
    if (ImGui::Button(">>")) {
        engine_.shiftBeat(0.0625);
    }
    ImGui::SetItemTooltip("%s", tr("Move the beat grid later by a sixteenth of a beat"));
    if (ImGui::Button(trId("Snap to beat", "snapBeat"))) {
        engine_.nudgeBeat(1.0f);
    }
    ImGui::SetItemTooltip("%s", tr("Align the grid to the nearest beat without changing the count"));
    ImGui::SameLine();
    if (ImGui::Button(trId("Bar start here", "barStart"))) {
        engine_.shiftBeat(std::round(engine_.beat() / bar) * bar - engine_.beat());
    }
    ImGui::SetItemTooltip("%s", tr("Make the nearest beat the first beat of the bar"));

    if (audio_.active()) {
        ui::sectionHeader(tr("Tempo"));
        ImGui::Checkbox(trId("Sync BPM to audio", "syncBpm"), &settings_.audioSyncBpm);
        const AudioFeatures& features = audio_.features();
        if (features.bpm > 0.0f) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f", features.bpm);
        }
    }
    ImGui::End();
}

void App::drawAudioWindow() {
    if (focusTab_ == 2) {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin(trId("Audio", "audio"))) {
        ImGui::End();
        return;
    }
    drawAudioTab();
    ImGui::End();
}

void App::drawStripMaster() {
    if (!ImGui::Begin(trId("Master level", "stripMaster"))) {
        ImGui::End();
        return;
    }
    Composition& comp = engine_.comp;
    ui::sliderFloat(trId("Level", "stripLevel"), &comp.master.level, 0.0f, 1.0f, "%.2f");
    ui::fieldLabel("dB");
    if (comp.master.level > 0.0001f) {
        ImGui::TextDisabled("%.1f", 20.0f * std::log10(comp.master.level));
    } else {
        ImGui::TextDisabled("-inf");
    }
    ui::fieldLabel(tr("Blackout"));
    ui::toggleButton(trId("Blackout", "stripBlackout"), engine_.blackout);
    ImGui::End();
}

void App::drawStripAudio() {
    if (!ImGui::Begin(trId("Audio input", "stripAudio"))) {
        ImGui::End();
        return;
    }
    drawAudioSource();
    if (audio_.active()) {
        meterRow(tr("Level"), engine_.audio().level);
    }
    ImGui::End();
}

void App::drawStripNetwork() {
    if (!ImGui::Begin(trId("Network", "stripNetwork"))) {
        ImGui::End();
        return;
    }
    ui::fieldLabel("NDI");
    if (NdiSender::available()) {
        bool active = ndi_.active();
        if (ImGui::Checkbox(trId("Send via NDI", "stripNdi"), &active)) {
            if (active) {
                if (!ndi_.start(ndiName_)) {
                    toast(tr("NDI sender could not be started"));
                }
            } else {
                ndi_.stop();
            }
        }
    } else {
        ImGui::TextColored(kWarn, "%s", tr("NDI runtime not found"));
    }
    ui::fieldLabel("Spout");
    if (SpoutOutput::supported()) {
        bool active = spout_.active();
        if (ImGui::Checkbox(trId("Send to OBS via Spout", "stripSpout"), &active)) {
            if (active) {
                spout_.start(spoutName_);
            } else {
                spout_.stop();
            }
        }
    } else {
        ImGui::TextDisabled("%s", tr("Not available"));
    }
    ui::fieldLabel(tr("Network sync"));
    const netsync::Role role = netSync_.role();
    if (role == netsync::Role::Off) {
        ImGui::TextDisabled("%s", tr("Off"));
    } else {
        const char* name = role == netsync::Role::Master ? tr("Master") : tr("Follower");
        if (!netSync_.error().empty()) {
            ImGui::TextColored(kBad, "%s", name);
        } else if (netSync_.running()) {
            ImGui::TextColored(kGood, "%s", name);
        } else {
            ImGui::TextColored(kWarn, "%s", name);
        }
        if (netSync_.running() && netSync_.localPort() > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%d", netSync_.localPort());
        }
    }
    ImGui::SameLine(0.0f, ui::px(10.0f));
    if (ImGui::SmallButton(trId("Open settings", "stripNetworkSettings"))) {
        openSettings(SettingsSync);
    }
    ImGui::End();
}

void App::drawStripMidi() {
    if (!ImGui::Begin("MIDI###stripMidi")) {
        ImGui::End();
        return;
    }
    const std::vector<std::string>& devices = control_.midiDevices();
    ui::fieldLabel(tr("Device"));
    const std::string current = settings_.midiDevice.empty() ? std::string(tr("None")) : settings_.midiDevice;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ui::beginCombo("##stripMidiDevice", current.c_str())) {
        if (ImGui::Selectable(tr("None"), settings_.midiDevice.empty())) {
            settings_.midiDevice.clear();
            saveSettings(settings_);
        }
        for (size_t i = 0; i < devices.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(devices[i].c_str(), devices[i] == settings_.midiDevice)) {
                settings_.midiDevice = devices[i];
                saveSettings(settings_);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
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
    }
    ImGui::End();
}

void App::drawStripOsc() {
    if (!ImGui::Begin("OSC###stripOsc")) {
        ImGui::End();
        return;
    }
    if (ImGui::Checkbox(trId("Receive OSC messages", "stripOscEnable"), &settings_.oscEnabled)) {
        saveSettings(settings_);
    }
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
        }
    }
    ImGui::End();
}

}

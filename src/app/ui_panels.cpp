#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

bool bandCombo(const char* id, int& band, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, tr(audioBandName(band)))) {
        for (int b = 0; b < AudioBandCount; ++b) {
            if (ImGui::Selectable(tr(audioBandName(b)), band == b)) {
                band = b;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

}

void App::drawGrade(ColorGrade& grade) {
    ImGui::SliderFloat(trId("Hue", "gradeHue"), &grade.hue, -0.5f, 0.5f, "%.2f");
    ImGui::SliderFloat(trId("Saturation", "gradeSat"), &grade.saturation, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat(trId("Brightness", "gradeBri"), &grade.brightness, -0.5f, 0.5f, "%.2f");
    ImGui::SliderFloat(trId("Contrast", "gradeCon"), &grade.contrast, 0.5f, 2.0f, "%.2f");
    if (!grade.isIdentity() && ImGui::SmallButton(trId("Reset grade", "gradeReset"))) {
        grade = ColorGrade{};
    }
}

void App::drawMods(std::array<AudioMod, kModSlots>& mods, bool master) {
    for (int i = 0; i < kModSlots; ++i) {
        AudioMod& mod = mods[static_cast<size_t>(i)];
        ImGui::PushID(i);
        bandCombo("##band", mod.band, 78.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(104.0f);
        const bool level = master && mod.target == TargetOpacity;
        const char* preview = level ? tr("Level") : tr(modTargetName(mod.target));
        if (ImGui::BeginCombo("##target", preview)) {
            for (int t = 0; t < ModTargetCount; ++t) {
                if (master && (t == TargetScale || t == TargetSpeed)) {
                    continue;
                }
                const char* label = (master && t == TargetOpacity) ? tr("Level") : tr(modTargetName(t));
                if (ImGui::Selectable(label, mod.target == t)) {
                    mod.target = t;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(mod.band == BandOff);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##amount", &mod.amount, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void App::drawEffectRack(std::array<EffectSlot, kEffectSlots>& slots) {
    const std::vector<EffectDef>& library = effectLibrary();
    for (int s = 0; s < kEffectSlots; ++s) {
        EffectSlot& slot = slots[static_cast<size_t>(s)];
        ImGui::PushID(s);
        ImGui::Checkbox("##enabled", &slot.enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char* preview = slot.effect >= 0 ? library[static_cast<size_t>(slot.effect)].name.c_str() : tr("- empty -");
        if (ImGui::BeginCombo("##effect", preview)) {
            if (ImGui::Selectable(tr("- empty -"), slot.effect < 0)) {
                setEffect(slot, -1);
            }
            for (int i = 0; i < static_cast<int>(library.size()); ++i) {
                const EffectDef& def = library[static_cast<size_t>(i)];
                std::string label = def.is3d ? def.name + "  [3D]" : def.name;
                if (ImGui::Selectable(label.c_str(), slot.effect == i)) {
                    setEffect(slot, i);
                }
            }
            ImGui::EndCombo();
        }
        if (slot.effect >= 0) {
            const EffectDef& def = library[static_cast<size_t>(slot.effect)];
            ImGui::BeginDisabled(!slot.enabled);
            for (int p = 0; p < kParamCount; ++p) {
                if (!def.paramNames[static_cast<size_t>(p)]) {
                    continue;
                }
                ImGui::PushID(p);
                ImGui::SliderFloat(tr(def.paramNames[static_cast<size_t>(p)]), &slot.params[static_cast<size_t>(p)], 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    slot.params[static_cast<size_t>(p)] = def.defaults[static_cast<size_t>(p)];
                }
                ImGui::PopID();
            }
            slot.reactParam = std::clamp(slot.reactParam, 0, std::max(0, def.paramCount() - 1));
            ImGui::PushID("react");
            bandCombo("##band", slot.reactBand, 78.0f);
            if (slot.reactBand != BandOff) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(92.0f);
                if (ImGui::BeginCombo("##mode", tr(reactModeName(slot.reactMode)))) {
                    for (int m = 0; m < EffectReactModeCount; ++m) {
                        if (ImGui::Selectable(tr(reactModeName(m)), slot.reactMode == m)) {
                            slot.reactMode = m;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("%s", tr("Parameter: the band moves one value. Gate: the effect only shows while the band is loud."));
                if (slot.reactMode == ReactParameter) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const char* paramPreview = def.paramNames[static_cast<size_t>(slot.reactParam)] ? tr(def.paramNames[static_cast<size_t>(slot.reactParam)]) : "-";
                    if (ImGui::BeginCombo("##param", paramPreview)) {
                        for (int p = 0; p < kParamCount; ++p) {
                            if (!def.paramNames[static_cast<size_t>(p)]) {
                                continue;
                            }
                            if (ImGui::Selectable(tr(def.paramNames[static_cast<size_t>(p)]), slot.reactParam == p)) {
                                slot.reactParam = p;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderFloat("##amount", &slot.reactAmount, 0.0f, 1.0f, slot.reactMode == ReactGate ? tr("Sensitivity %.2f") : tr("Amount %.2f"));
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tr("Audio reaction"));
            }
            ImGui::PopID();
            ImGui::EndDisabled();
        }
        ImGui::PopID();
        if (s + 1 < kEffectSlots) {
            ImGui::Separator();
        }
    }
}

void App::drawLayer() {
    ImGui::Begin(trId("Layer", "layer"));
    selectedLayer_ = std::clamp(selectedLayer_, 0, kLayerCount - 1);
    Layer& layer = engine_.comp.layers[static_cast<size_t>(selectedLayer_)];
    const std::vector<ClipDef>& library = clipLibrary();

    for (int i = 0; i < kLayerCount; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        if (ui::accentButton(std::to_string(i + 1).c_str(), i == selectedLayer_, ImVec2(28.0f, 0.0f))) {
            selectedLayer_ = i;
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    int activeClip = layer.activeColumn >= 0 ? layer.cells[static_cast<size_t>(layer.activeColumn)] : -1;
    ImGui::TextDisabled("%s", activeClip >= 0 ? library[static_cast<size_t>(activeClip)].name.c_str() : tr("no clip"));

    ImGui::SeparatorText(tr("Look"));
    ImGui::SliderFloat(trId("Speed", "layerSpeed"), &layer.speed, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(trId("Scale", "layerScale"), &layer.scale, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText(tr("Color"));
    ImGui::ColorEdit3(trId("Color A", "colorA"), layer.colorA.data(), ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::ColorEdit3(trId("Color B", "colorB"), layer.colorB.data(), ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    if (ImGui::SmallButton(trId("Swap", "colorSwap"))) {
        std::swap(layer.colorA, layer.colorB);
    }
    ImGui::Checkbox(trId("Rainbow", "rainbow"), &layer.rainbow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##palette", tr("Palette"))) {
        for (const ui::Palette& palette : ui::kPalettes) {
            if (ImGui::Selectable(palette.name)) {
                layer.colorA = palette.a;
                layer.colorB = palette.b;
                layer.rainbow = false;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::CollapsingHeader(trId("Grade", "hdrGrade"))) {
        drawGrade(layer.grade);
    }
    if (ImGui::CollapsingHeader(trId("Audio reactivity", "hdrMods"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawMods(layer.mods, false);
    }
    if (ImGui::CollapsingHeader(trId("Effects", "hdrEffects"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawEffectRack(layer.fx);
    }
    ImGui::End();
}

void App::drawOutputControls() {
    ImGui::SeparatorText(tr("Output window"));
    bool open = output_.isOpen();
    if (ImGui::Button(open ? tr("Close") : tr("Open"))) {
        if (open) {
            output_.close();
        } else {
            openOutputWindow(false);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!output_.isOpen());
    if (ImGui::Button(output_.isFullscreen() ? tr("Windowed") : tr("Fullscreen"))) {
        output_.setFullscreen(!output_.isFullscreen(), outputMonitor_);
    }
    ImGui::EndDisabled();
    std::vector<std::string> monitors = OutputWindow::monitorNames();
    if (!monitors.empty()) {
        outputMonitor_ = std::clamp(outputMonitor_, 0, static_cast<int>(monitors.size()) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##display", monitors[static_cast<size_t>(outputMonitor_)].c_str())) {
            for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                if (ImGui::Selectable(monitors[static_cast<size_t>(i)].c_str(), i == outputMonitor_)) {
                    outputMonitor_ = i;
                    if (output_.isFullscreen()) {
                        output_.setFullscreen(true, outputMonitor_);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::TextDisabled("%s", tr("F11 fullscreen, Esc leaves it"));
}

void App::drawSpoutControls() {
    ImGui::SeparatorText(tr("Spout (OBS)"));
    if (!SpoutOutput::supported()) {
        ImGui::TextDisabled("%s", tr("Spout is only available on Windows."));
        return;
    }
    bool active = spout_.active();
    if (ImGui::Checkbox(trId("Send to OBS via Spout", "spoutToggle"), &active)) {
        if (active) {
            spout_.start(spoutName_);
        } else {
            spout_.stop();
        }
    }
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText(trId("Sender name", "spoutName"), spoutName_, sizeof(spoutName_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (spout_.active()) {
            spout_.start(spoutName_);
        }
    }
    if (active) {
        if (spout_.connected()) {
            ImGui::TextColored(ui::kAccent, "%s '%s'", tr("Sending as"), spout_.name().c_str());
        } else {
            ImGui::TextDisabled("%s", tr("Starting sender..."));
        }
    }
    ImGui::TextWrapped("%s", tr("In OBS add the source \"Spout2 Capture\" and choose this sender."));
    if (ImGui::SmallButton(trId("OBS Spout plugin", "obsPlugin"))) {
        platform::openUrl(ui::kObsPluginUrl);
    }
}

void App::drawMaster() {
    ImGui::Begin(trId("Master", "master"));
    Composition& comp = engine_.comp;
    ImGui::SliderFloat(trId("Level", "masterLevel"), &comp.master.level, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ui::toggleButton(trId("Blackout", "blackout"), engine_.blackout);

    float bpm = comp.bpm;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::DragFloat("BPM", &bpm, 0.1f, 40.0f, 240.0f, "%.1f")) {
        comp.bpm = std::clamp(bpm, 40.0f, 240.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Tap", "tap"))) {
        engine_.tap();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Resync", "resync"))) {
        engine_.resync();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("/2")) {
        comp.bpm = std::clamp(comp.bpm * 0.5f, 40.0f, 240.0f);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("x2")) {
        comp.bpm = std::clamp(comp.bpm * 2.0f, 40.0f, 240.0f);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s %d", tr("Beat"), static_cast<int>(std::floor(engine_.beat())) % 4 + 1);

    if (audio_.active()) {
        const AudioFeatures& features = audio_.features();
        ImGui::Checkbox(trId("Sync BPM to audio", "syncBpm"), &settings_.audioSyncBpm);
        if (features.bpm > 0.0f) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f", features.bpm);
        }
    }

    if (ImGui::BeginCombo(trId("Resolution", "resolution"), kResolutions[comp.resolution].label)) {
        for (int i = 0; i < kResolutionCount; ++i) {
            if (ImGui::Selectable(kResolutions[i].label, i == comp.resolution)) {
                engine_.setResolutionIndex(i);
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::CollapsingHeader(trId("Grade", "hdrGrade"))) {
        drawGrade(comp.master.grade);
    }
    if (ImGui::CollapsingHeader(trId("Audio reactivity", "hdrMods"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawMods(comp.master.mods, true);
    }
    if (ImGui::CollapsingHeader(trId("Effects", "hdrEffects"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawEffectRack(comp.master.fx);
    }
    ImGui::End();
}

}

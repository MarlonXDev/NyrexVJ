#include "app/app.hpp"

#include "app/ui_common.hpp"

#include "core/ndi_runtime.hpp"
#include "core/node_graph.hpp"
#include "core/person_mask.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace nvj {

namespace {

bool pluginBandVisible(const PluginHost* plugins, int band, int current, bool personOn) {
    if (band >= BandPersonPresent && band <= BandPersonCount) {
        return personOn || band == current;
    }
    if (band < BandPlugin1 || band > BandPlugin8 || band == current) {
        return true;
    }
    return plugins != nullptr && !plugins->config().modulation[static_cast<size_t>(band - BandPlugin1)].empty();
}

bool bandCombo(const char* id, int& band, float width, const PluginHost* plugins, bool personOn) {
    bool changed = false;
    width = ui::fitComboWidth(width, tr(audioBandName(band)));
    width = ui::fitComboWidth(width, tr(audioBandName(BandOff)));
    ImGui::SetNextItemWidth(width);
    if (ui::beginCombo(id, tr(audioBandName(band)))) {
        for (int b = 0; b < AudioBandCount; ++b) {
            if (!pluginBandVisible(plugins, b, band, personOn)) {
                continue;
            }
            if (b == BandLfo1 || b == BandMacro1 || b == BandBeat || b == BandTimecodeSecond || b == BandPlugin1 || b == BandPersonPresent) {
                ImGui::Separator();
            }
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
    ui::sliderFloat(trId("Hue", "gradeHue"), &grade.hue, -0.5f, 0.5f, "%.2f");
    ui::sliderFloat(trId("Saturation", "gradeSat"), &grade.saturation, 0.0f, 2.0f, "%.2f");
    ui::sliderFloat(trId("Brightness", "gradeBri"), &grade.brightness, -0.5f, 0.5f, "%.2f");
    ui::sliderFloat(trId("Contrast", "gradeCon"), &grade.contrast, 0.5f, 2.0f, "%.2f");
    if (!grade.isIdentity() && ImGui::SmallButton(trId("Reset grade", "gradeReset"))) {
        grade = ColorGrade{};
    }
}

void App::drawMods(std::array<AudioMod, kModSlots>& mods, bool master) {
    for (int i = 0; i < kModSlots; ++i) {
        AudioMod& mod = mods[static_cast<size_t>(i)];
        ImGui::PushID(i);
        bandCombo("##band", mod.band, 84.0f, plugins_.get(), person::modeFromName(settings_.personMode, person::Mode::Off) != person::Mode::Off);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::px(104.0f));
        const bool level = master && mod.target == TargetOpacity;
        const char* preview = level ? tr("Level") : tr(modTargetName(mod.target));
        if (ui::beginCombo("##target", preview)) {
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
        ui::sliderFloat("##amount", &mod.amount, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void App::drawModulators(Modulators& modulators) {
    ImGui::TextDisabled("%s", tr("Macros"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Pick a macro as the source of any audio reactivity slot, then one knob moves everything that uses it. Map the knobs to a MIDI controller in Settings, Control."));
    ImGui::PopTextWrapPos();
    for (int i = 0; i < kMacroCount; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "%s %d###macro%d", tr("Macro"), i + 1, i);
        ui::sliderFloat(label, &modulators.macros[static_cast<size_t>(i)], 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::SmallButton(trId("Reset macros", "macroReset"))) {
        modulators.macros.fill(0.0f);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("LFOs"));
    for (int i = 0; i < kLfoCount; ++i) {
        Lfo& lfo = modulators.lfos[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("LFO %d", i + 1);
        ImGui::SameLine(52.0f);
        ImGui::SetNextItemWidth(ui::px(84.0f));
        if (ui::beginCombo("##shape", tr(lfoShapeName(lfo.shape)))) {
            for (int s = 0; s < LfoShapeCount; ++s) {
                if (ImGui::Selectable(tr(lfoShapeName(s)), lfo.shape == s)) {
                    lfo.shape = s;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::px(92.0f));
        if (ui::beginCombo("##rate", tr(lfoRateName(lfo.rate)))) {
            for (int r = 0; r < kLfoRateCount; ++r) {
                if (ImGui::Selectable(tr(lfoRateName(r)), lfo.rate == r)) {
                    lfo.rate = r;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::px(50.0f));
        ui::sliderFloat("##phase", &lfo.phase, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("%s", tr("Phase: where in its cycle the LFO starts"));
        ImGui::SameLine();
        const float now = engine_.sourceValue(BandLfo1 + i);
        ImGui::ProgressBar(now, ImVec2(-FLT_MIN, 0.0f), "");
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("The sources Beat and Bar rise from 0 to 1 over one beat and over one bar."));
    ImGui::PopTextWrapPos();
}

void App::drawEffectRack(std::array<EffectSlot, kEffectSlots>& slots) {
    const std::vector<EffectDef>& library = effectLibrary();
    for (int s = 0; s < kEffectSlots; ++s) {
        EffectSlot& slot = slots[static_cast<size_t>(s)];
        ImGui::PushID(s);
        const bool flatRow = ui::kFlatLook;
        const ImGuiID openId = ImGui::GetID("##fxopen");
        ImGuiStorage* openStore = ImGui::GetStateStorage();
        const bool expanded = !flatRow || openStore->GetInt(openId, 1) != 0;
        int bandColors = 0;
        if (flatRow) {
            const ImGuiStyle& rowStyle = ImGui::GetStyle();
            const float rowHeight = ImGui::GetFrameHeight();
            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            const ImVec4 band(ui::kAccentDim.x * 0.62f + 0.05f, ui::kAccentDim.y * 0.62f + 0.05f, ui::kAccentDim.z * 0.62f + 0.05f, 1.0f);
            const ImVec4 cell(band.x + 0.09f, band.y + 0.09f, band.z + 0.09f, 1.0f);
            const ImVec4 cellHover(band.x + 0.17f, band.y + 0.17f, band.z + 0.17f, 1.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(rowPos.x - rowStyle.WindowPadding.x, rowPos.y), ImVec2(rowPos.x + rowWidth + rowStyle.WindowPadding.x, rowPos.y + rowHeight), ImGui::GetColorU32(band));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, band);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, cell);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, cell);
            ImGui::PushStyleColor(ImGuiCol_Button, cell);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cellHover);
            bandColors = 5;
            ImGui::InvisibleButton("##fxopen", ImVec2(rowHeight * 0.8f, rowHeight));
            if (ImGui::IsItemClicked()) {
                openStore->SetInt(openId, expanded ? 0 : 1);
            }
            const ImVec2 arrowMin = ImGui::GetItemRectMin();
            const float arrowSize = std::max(3.0f, rowHeight * 0.17f);
            const float arrowX = arrowMin.x + rowHeight * 0.4f;
            const float arrowY = arrowMin.y + rowHeight * 0.5f;
            const ImU32 arrowColor = ImGui::GetColorU32(ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
            if (expanded) {
                ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(arrowX - arrowSize, arrowY - arrowSize * 0.6f), ImVec2(arrowX + arrowSize, arrowY - arrowSize * 0.6f), ImVec2(arrowX, arrowY + arrowSize * 0.7f), arrowColor);
            } else {
                ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(arrowX - arrowSize * 0.6f, arrowY - arrowSize), ImVec2(arrowX - arrowSize * 0.6f, arrowY + arrowSize), ImVec2(arrowX + arrowSize * 0.7f, arrowY), arrowColor);
            }
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetNextItemWidth(-(rowHeight * 2.0f + rowStyle.ItemSpacing.x * 2.0f));
        } else {
            ImGui::Checkbox("##enabled", &slot.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
        }
        const char* preview = slot.effect >= 0 ? library[static_cast<size_t>(slot.effect)].name.c_str() : tr("- empty -");
        if (ui::beginCombo("##effect", preview)) {
            if (ImGui::IsWindowAppearing()) {
                effectPickFilter_[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##effectfilter", tr("Search"), effectPickFilter_, sizeof(effectPickFilter_));
            std::string needle = effectPickFilter_;
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (needle.empty() && ImGui::Selectable(tr("- empty -"), slot.effect < 0)) {
                setEffect(slot, -1);
            }
            for (int i = 0; i < static_cast<int>(library.size()); ++i) {
                const EffectDef& def = library[static_cast<size_t>(i)];
                if (!needle.empty()) {
                    std::string lowered = def.name;
                    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    if (lowered.find(needle) == std::string::npos) {
                        continue;
                    }
                }
                std::string label = def.is3d ? def.name + "  [3D]" : def.name;
                if (ImGui::Selectable(label.c_str(), slot.effect == i)) {
                    setEffect(slot, i);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NVJ_EFFECT")) {
                setEffect(slot, *static_cast<const int*>(payload->Data));
            }
            ImGui::EndDragDropTarget();
        }
        if (flatRow) {
            const float rowHeight = ImGui::GetFrameHeight();
            ImGui::SameLine();
            bool bypassed = !slot.enabled;
            ui::toggleButton("B", bypassed, ImVec2(rowHeight, rowHeight));
            slot.enabled = !bypassed;
            ImGui::SetItemTooltip("%s", tr("Bypass: skip this effect"));
            ImGui::SameLine();
            if (ImGui::Button("X", ImVec2(rowHeight, rowHeight))) {
                setEffect(slot, -1);
                slot.enabled = true;
            }
            ImGui::SetItemTooltip("%s", tr("Remove this effect"));
            ImGui::PopStyleColor(bandColors);
        }
        if (slot.effect >= 0 && expanded) {
            const EffectDef& def = library[static_cast<size_t>(slot.effect)];
            if (def.isIsf) {
                const std::string problem = engine_.effectError(slot.effect);
                if (!problem.empty()) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", trMessage(problem).c_str());
                    ImGui::PopTextWrapPos();
                }
                if (def.plan && !def.plan->info.notes.empty()) {
                    ImGui::TextDisabled("%s", tr("Notes about this file"));
                    if (ImGui::BeginItemTooltip()) {
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                        for (const std::string& note : def.plan->info.notes) {
                            ImGui::BulletText("%s", note.c_str());
                        }
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
            }
            if (def.isGraph) {
                if (!def.error.empty()) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", def.error.c_str());
                    ImGui::PopTextWrapPos();
                }
                std::string stem;
                if (graphStemFromEffectName(def.name, stem) && ImGui::SmallButton(tr("Edit nodes"))) {
                    openNodeEditor(stem);
                }
            }
            ImGui::BeginDisabled(!slot.enabled);
            for (int p = 0; p < kParamCount; ++p) {
                if (!def.paramNames[static_cast<size_t>(p)]) {
                    continue;
                }
                ImGui::PushID(p);
                ui::sliderFloat(tr(def.paramNames[static_cast<size_t>(p)]), &slot.params[static_cast<size_t>(p)], 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    slot.params[static_cast<size_t>(p)] = def.defaults[static_cast<size_t>(p)];
                }
                ImGui::PopID();
            }
            slot.reactParam = std::clamp(slot.reactParam, 0, std::max(0, def.paramCount() - 1));
            ImGui::PushID("react");
            bandCombo("##band", slot.reactBand, 78.0f, plugins_.get(), person::modeFromName(settings_.personMode, person::Mode::Off) != person::Mode::Off);
            if (slot.reactBand != BandOff) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ui::px(92.0f));
                if (ui::beginCombo("##mode", tr(reactModeName(slot.reactMode)))) {
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
                    if (ui::beginCombo("##param", paramPreview)) {
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
                ui::sliderFloat("##amount", &slot.reactAmount, 0.0f, 1.0f, slot.reactMode == ReactGate ? tr("Sensitivity %.2f") : tr("Amount %.2f"));
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
    bool feedbackInChain = false;
    for (const EffectSlot& slot : slots) {
        feedbackInChain = feedbackInChain || (slot.enabled && slot.effect >= 0 && slot.effect < static_cast<int>(library.size()) && library[static_cast<size_t>(slot.effect)].usesPrevious);
    }
    if (feedbackInChain) {
        ImGui::Separator();
        bool frozen = engine_.feedbackFrozen();
        if (ImGui::Checkbox(trId("Freeze feedback", "fbFreezeRack"), &frozen)) {
            engine_.setFeedbackFrozen(frozen);
        }
        ImGui::SetItemTooltip("%s", tr("Holds the feedback pictures where they are. Applies to all feedback effects."));
        ImGui::SameLine();
        if (ImGui::SmallButton(trId("Reset feedback", "fbResetRack"))) {
            engine_.resetFeedback();
        }
        ImGui::SetItemTooltip("%s", tr("Clears all feedback pictures."));
    }
    ImGui::Separator();
    drawChainButtons(slots);
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

    ui::sectionHeader(tr("Video"));
    ui::sliderFloat(trId("Opacity", "layerOpacity"), &layer.opacity, 0.0f, 1.0f, "%.2f");
    ui::fieldLabel(tr("Blend"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ui::beginCombo("##layerBlendMode", tr(blendName(layer.blend)))) {
        for (int b = 0; b < BlendModeCount; ++b) {
            if (ImGui::Selectable(tr(blendName(b)), layer.blend == b)) {
                layer.blend = b;
            }
        }
        ImGui::EndCombo();
    }
    ui::fieldLabel("");
    ui::toggleButton("B", layer.bypass);
    ImGui::SetItemTooltip("%s", tr("Bypass: skip this layer"));
    ImGui::SameLine();
    ui::toggleButton("S", layer.solo);
    ImGui::SetItemTooltip("%s", tr("Solo: show only this layer"));

    if (ui::collapsingHeader(trId("Audio reactivity", "hdrMods"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawMods(layer.mods, false);
    }
    if (ui::collapsingHeader(trId("Effects", "hdrEffects"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawEffectRack(layer.fx);
    }
    ImGui::End();
}

void App::drawSceneControls(ScenePlayer& player) {
    drawSceneDetails(player);
    ImGui::PushID("scenecontrols");
    if (player.state() == SceneState::Loading) {
        if (ImGui::SmallButton(trId("Cancel loading", "sceneCancel"))) {
            player.cancel();
        }
    } else if (player.state() == SceneState::Failed) {
        if (ImGui::SmallButton(trId("Try again", "sceneRetry"))) {
            player.retry();
        }
    }
    ImGui::PopID();
}


static void drawShaderNotes(const ShaderPlan& plan) {
    const ShaderInfo& info = plan.info;
    if (!info.description.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", info.description.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!info.credit.empty()) {
        ImGui::TextDisabled("%s", info.credit.c_str());
    }
    if (!info.notes.empty() && ImGui::TreeNode(tr("Notes about this file"))) {
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& note : info.notes) {
            ImGui::BulletText("%s", note.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::TreePop();
    }
}

void drawShaderControlValues(const ShaderPlan& plan, ShaderValues& values) {
    if (plan.params.empty()) {
        return;
    }
    ui::sectionHeader(tr("Shader controls"));
    const ShaderValues resolved = resolveShaderValues(plan, values);
    for (const ShaderParam& param : plan.params) {
        ImGui::PushID(param.name.c_str());
        const size_t slot = static_cast<size_t>(param.slot);
        bool changed = false;
        float value[4] = {resolved[slot], resolved[std::min<size_t>(slot + 1, resolved.size() - 1)], resolved[std::min<size_t>(slot + 2, resolved.size() - 1)], resolved[std::min<size_t>(slot + 3, resolved.size() - 1)]};
        switch (param.kind) {
        case ShaderParamKind::Float:
            changed = ui::sliderFloat(param.label.c_str(), &value[0], param.min[0], param.max[0], "%.3f");
            break;
        case ShaderParamKind::Int: {
            int number = static_cast<int>(std::lround(value[0]));
            changed = ImGui::SliderInt(param.label.c_str(), &number, static_cast<int>(param.min[0]), static_cast<int>(param.max[0]));
            value[0] = static_cast<float>(number);
            break;
        }
        case ShaderParamKind::Bool: {
            bool on = value[0] >= 0.5f;
            changed = ImGui::Checkbox(param.label.c_str(), &on);
            value[0] = on ? 1.0f : 0.0f;
            break;
        }
        case ShaderParamKind::Point:
            changed = ImGui::SliderFloat2(param.label.c_str(), value, std::min(param.min[0], param.min[1]), std::max(param.max[0], param.max[1]), "%.1f");
            break;
        case ShaderParamKind::Color:
            changed = ImGui::ColorEdit4(param.label.c_str(), value);
            break;
        case ShaderParamKind::List: {
            int index = static_cast<int>(std::lround(value[0]));
            const char* preview = index >= 0 && index < static_cast<int>(param.labels.size()) ? param.labels[static_cast<size_t>(index)].c_str() : "-";
            if (ui::beginCombo(param.label.c_str(), preview)) {
                for (int i = 0; i < static_cast<int>(param.labels.size()); ++i) {
                    if (ImGui::Selectable(param.labels[static_cast<size_t>(i)].c_str(), i == index)) {
                        index = i;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            value[0] = static_cast<float>(index);
            break;
        }
        }
        if (changed) {
            for (int c = 0; c < param.count; ++c) {
                values[slot + static_cast<size_t>(c)] = shaderValueClamp(param, c, value[c]);
            }
        }
        ImGui::SetItemTooltip("%s", tr("Right click resets this control to the default of the file"));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            for (int c = 0; c < param.count; ++c) {
                values[slot + static_cast<size_t>(c)] = kClipValueUnset;
            }
        }
        ImGui::PopID();
    }
    if (!shaderValuesUnset(values) && ImGui::SmallButton(tr("Reset controls"))) {
        values = unsetShaderValues();
    }
}

static void drawShaderControls(const ShaderPlan& plan, ClipPlayback& playback) {
    drawShaderControlValues(plan, playback.values);
}

void App::drawClipPanel() {
    ImGui::Begin(trId("Clip", "clip"));
    selectedLayer_ = std::clamp(selectedLayer_, 0, kLayerCount - 1);
    selectedColumn_ = std::clamp(selectedColumn_, 0, kColumnCount - 1);
    Layer& layer = engine_.comp.layers[static_cast<size_t>(selectedLayer_)];
    const std::vector<ClipDef>& library = clipLibrary();
    int clip = layer.cells[static_cast<size_t>(selectedColumn_)];
    int clipColumn = selectedColumn_;
    if (clip < 0 && layer.activeColumn >= 0) {
        clipColumn = layer.activeColumn;
        clip = layer.cells[static_cast<size_t>(clipColumn)];
    }

    if (clip < 0) {
        ImGui::TextWrapped("%s", tr("Select a clip in the deck, or click a clip in the Library to put it into the selected cell."));
        ImGui::End();
        return;
    }

    const ClipDef& def = library[static_cast<size_t>(clip)];
    ImGui::Image(ui::texId(engine_.clipThumbnail(clip)), ImVec2(112.0f, 63.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(def.name.c_str());
    ImGui::TextDisabled("%s", ui::cellLabel(selectedLayer_, selectedColumn_).c_str());
    if (def.kind == ClipKind::SpoutSource) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Spout input"));
    } else if (def.kind == ClipKind::NdiSource) {
        ImGui::TextColored(ui::kAccent, "%s", tr("NDI input"));
    } else if (def.kind == ClipKind::CameraSource) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Camera or capture card"));
    } else if (def.kind == ClipKind::PluginSource) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Plugin source"));
    } else if (def.kind == ClipKind::StreamSource) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Network stream input"));
    } else if (engine_.isSceneClip(clip)) {
        ImGui::TextColored(ui::kAccent, "%s", def.kind == ClipKind::ModelFile ? tr("3D model file") : tr("3D scene"));
    } else if (def.kind == ClipKind::VideoFile) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Video file"));
    } else if (def.kind == ClipKind::ImageFile) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Image file"));
    } else if (def.kind == ClipKind::TextFile) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Text clip"));
    } else if (def.kind == ClipKind::ParticleClip) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Particle clip"));
    } else if (def.isShaderFile()) {
        const bool multipass = def.isAdvancedShader() && def.plan->bufferCount() > 0;
        ImGui::TextColored(ui::kAccent, "%s", multipass ? tr("Shader file (multipass)") : (def.shadertoy ? tr("Shader file (Shadertoy style)") : tr("Shader file")));
        if (multipass) {
            ImGui::TextDisabled("%s: %d", tr("Buffers"), def.plan->bufferCount());
        }
        if (def.isAdvancedShader() && def.plan->alpha) {
            ImGui::TextDisabled("%s", tr("Alpha output"));
        }
        if (def.isAdvancedShader() && !def.plan->info.format.empty()) {
            ImGui::TextDisabled("%s", def.plan->info.format.c_str());
        }
    } else if (def.is3d) {
        ImGui::TextColored(ui::kAccent, "3D");
    }
    ImGui::EndGroup();

    if (MediaPlayer* media = engine_.mediaPlayer(clip)) {
        media->touch();
        ImGui::Spacing();
        drawMediaControls(*media);
        if (media->isVideo()) {
            drawPlaybackControls(*media, layer.playback[static_cast<size_t>(clipColumn)]);
        }
    }

    if (def.isGenerator()) {
        ImGui::Spacing();
        drawGeneratorClipInfo(def, clip);
    }

    if (def.isShaderFile()) {
        ImGui::Spacing();
        const std::string shaderError = engine_.clipError(clip);
        if (!shaderError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", tr("This shader does not compile. Fix the file and save it, NyrexVJ reloads it."));
            ImGui::PopStyleColor();
            ImGui::BeginChild("shaderError", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(trMessage(shaderError).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        } else {
            const std::string runtimeError = engine_.shaderRuntimeError(clip);
            if (!runtimeError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.35f, 1.0f));
                ImGui::TextWrapped("%s", tr("This shader cannot run right now."));
                ImGui::PopStyleColor();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(trMessage(runtimeError).c_str());
                ImGui::PopTextWrapPos();
            } else {
                ImGui::TextDisabled("%s", tr("Saving the file reloads the shader."));
                const std::string note = engine_.shaderNote(clip);
                if (!note.empty()) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", trMessage(note).c_str());
                    ImGui::PopTextWrapPos();
                }
            }
        }
        if (def.isAdvancedShader() && shaderError.empty()) {
            drawShaderNotes(*def.plan);
            drawShaderControls(*def.plan, layer.playback[static_cast<size_t>(clipColumn)]);
        }
        ImGui::TextDisabled("%s", def.path.c_str());
        if (ImGui::SmallButton(tr("Edit in shader editor"))) {
            openShaderEditor(std::filesystem::path(def.path).filename().string());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Open shaders folder"))) {
            platform::openFolder(shaderDirectory());
        }
    }

    if (def.kind == ClipKind::SpoutSource) {
        ImGui::Spacing();
        drawSpoutInputControls(def.sceneIndex);
    }

    if (def.isLive()) {
        ImGui::Spacing();
        drawLiveInputControls(def);
    }

    if (engine_.isSceneClip(clip)) {
        ImGui::Spacing();
        drawSceneControls(*engine_.scenePlayer(clip));
        ui::sectionHeader(tr("Camera"));
        const char* cameraHint = tr("Moves the camera of the 3D scene on top of its own camera. Zero leaves it alone. Turn the switch Camera moves in 3D scenes off in the settings to ignore these.");
        ui::sliderFloat(trId("Camera orbit", "layerCameraOrbit"), &layer.cameraOrbit, -180.0f, 180.0f, "%.0f");
        ImGui::SetItemTooltip("%s", cameraHint);
        ui::sliderFloat(trId("Camera tilt", "layerCameraTilt"), &layer.cameraTilt, -60.0f, 60.0f, "%.0f");
        ImGui::SetItemTooltip("%s", cameraHint);
        ui::sliderFloat(trId("Camera field of view", "layerCameraFov"), &layer.cameraFov, -30.0f, 30.0f, "%.0f");
        ImGui::SetItemTooltip("%s", cameraHint);
    }

    ui::sectionHeader(tr("Look"));
    ui::sliderFloat(trId("Speed", "layerSpeed"), &layer.speed, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ui::sliderFloat(trId("Scale", "layerScale"), &layer.scale, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

    ui::sectionHeader(tr("Color"));
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
    if (ui::beginCombo("##palette", tr("Palette"))) {
        for (const ui::Palette& palette : ui::kPalettes) {
            if (ImGui::Selectable(palette.name)) {
                layer.colorA = palette.a;
                layer.colorB = palette.b;
                layer.rainbow = false;
            }
        }
        ImGui::EndCombo();
    }

    if (ui::collapsingHeader(trId("Grade", "hdrGrade"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawGrade(layer.grade);
    }
    ImGui::End();
}

void App::drawSpoutInputControls(int slot) {
    if (slot < 0 || slot >= kSpoutInputSlots) {
        return;
    }
    ui::sectionHeader(tr("Spout input"));
    if (!SpoutInput::supported() && !SpoutInput::testPattern()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Spout input only works on Windows."));
        ImGui::PopTextWrapPos();
        return;
    }
    auto persist = [this]() { saveSettings(settings_); };
    std::string& wanted = settings_.spoutInputs[static_cast<size_t>(slot)];
    const std::vector<std::string>& senders = engine_.spoutSenders();
    const std::string preview = wanted.empty() ? std::string(tr("Automatic")) : wanted;
    ImGui::SetNextItemWidth(ui::px(-90.0f));
    if (ui::beginCombo(trId("Spout sender", "spoutPick"), preview.c_str())) {
        if (ImGui::Selectable(tr("Automatic"), wanted.empty())) {
            wanted.clear();
            persist();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Uses the first sender found that is not the NyrexVJ output."));
        }
        for (const std::string& name : senders) {
            if (ImGui::Selectable(name.c_str(), wanted == name)) {
                wanted = name;
                persist();
            }
        }
        if (senders.empty()) {
            ImGui::TextDisabled("%s", tr("No Spout senders found"));
        }
        ImGui::EndCombo();
    }
    const SpoutReceiveStatus& status = engine_.spoutStatus(slot);
    if (status.hasFrame) {
        ImGui::TextColored(ui::kAccent, "%s%s", tr("Connected to "), status.sender.c_str());
        ImGui::TextDisabled("%d x %d, %.0f fps", status.width, status.height, status.fps);
    } else if (status.connected) {
        ImGui::TextDisabled("%s", tr("Connecting..."));
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Waiting for a sender. The clip shows a placeholder until one arrives."));
        ImGui::PopTextWrapPos();
    }
    if (ImGui::Checkbox(trId("Flip the image vertically", "spoutFlip"), &settings_.spoutInputFlip)) {
        persist();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Use this if the image arrives upside down."));
    ImGui::PopTextWrapPos();
}

void App::drawSpoutControls() {
    ui::sectionHeader(tr("Spout (OBS)"));
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
    ImGui::SetNextItemWidth(ui::px(-90.0f));
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

void App::drawNdiControls() {
    ui::sectionHeader(tr("NDI"));
    const bool available = NdiSender::available();
    bool active = ndi_.active();
    ImGui::BeginDisabled(!available);
    if (ImGui::Checkbox(trId("Send via NDI", "ndiToggle"), &active)) {
        if (active) {
            if (!ndi_.start(ndiName_)) {
                toast(tr("NDI sender could not be started"));
            }
        } else {
            ndi_.stop();
        }
    }
    ImGui::SetNextItemWidth(ui::px(-90.0f));
    if (ImGui::InputText(trId("NDI name", "ndiName"), ndiName_, sizeof(ndiName_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (ndi_.active()) {
            ndi_.start(ndiName_);
        }
    }
    ImGui::EndDisabled();
    if (!available) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("NDI runtime not found"));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Install the free NDI Tools or the NDI Runtime from ndi.video. NyrexVJ does not ship the NDI library."));
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton(tr("Download NDI Tools"))) {
            platform::openUrl("https://ndi.video/tools/");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Search again"))) {
            NdiSender::recheck();
        }
        return;
    }
    {
        const ndi::RuntimeInfo runtime = ndi::runtimeInfo();
        const std::string line = std::string(tr("NDI runtime:")) + " " + (runtime.version.empty() ? std::string(tr("version not reported")) : runtime.version);
        ImGui::TextDisabled("%s", line.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s", runtime.library.c_str(), runtime.captureV3 ? tr("This runtime has no NDIlib_recv_capture_v2, so NyrexVJ receives through NDIlib_recv_capture_v3.") : tr("NyrexVJ calls NDIlib_send_send_video_v2, NDIlib_find_create_v2, NDIlib_recv_create_v3 and NDIlib_recv_capture_v2."));
        }
    }
    if (active) {
        if (ndi_.connections() > 0) {
            ImGui::TextColored(ui::kAccent, "%s '%s' (%d)", tr("Sending as"), ndi_.name().c_str(), ndi_.connections());
        } else {
            ImGui::TextDisabled("%s '%s' - %s", tr("Sending as"), ndi_.name().c_str(), tr("no receiver connected, nothing is read back"));
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("The picture of the main output is sent over the network. Receivers: OBS with the NDI plugin, Resolume, vMix, NDI Studio Monitor."));
    ImGui::PopTextWrapPos();
}

void App::drawMaster() {
    if (focusTab_ == 3) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::Begin(trId("Master", "master"));
    Composition& comp = engine_.comp;
    ui::sliderFloat(trId("Level", "masterLevel"), &comp.master.level, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ui::toggleButton(trId("Blackout", "blackout"), engine_.blackout);

    if (ui::beginCombo(trId("Resolution", "resolution"), kResolutions[comp.resolution].label)) {
        for (int i = 0; i < kResolutionCount; ++i) {
            if (ImGui::Selectable(kResolutions[i].label, i == comp.resolution)) {
                engine_.setResolutionIndex(i);
            }
        }
        ImGui::EndCombo();
    }

    if (ui::collapsingHeader(trId("Grade", "hdrGrade"))) {
        drawGrade(comp.master.grade);
    }
    if (ui::collapsingHeader(trId("Audio reactivity", "hdrMods"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawMods(comp.master.mods, true);
    }
    if (ui::collapsingHeader(trId("Macros and LFOs", "hdrModulators"))) {
        drawModulators(comp.modulators);
    }
    if (ui::collapsingHeader(trId("Effects", "hdrEffects"), ImGuiTreeNodeFlags_DefaultOpen)) {
        drawEffectRack(comp.master.fx);
    }
    ImGui::End();
}

}

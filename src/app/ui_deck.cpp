#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/modulation.hpp"
#include "core/novelty.hpp"
#include "core/preset.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace nvj {

void App::switchDeckTo(int index) {
    engine_.requestSwitchDeck(index);
}

void App::fillDeckFromLook(int index, const Composition& look) {
    if (index < 0 || index >= kDeckCount) {
        return;
    }
    Composition& comp = engine_.comp;
    const size_t slot = static_cast<size_t>(index);
    if (index == comp.activeDeck) {
        engine_.loadLook(look);
    } else {
        comp.decks[slot] = captureDeck(look);
        comp.deckUsed[slot] = true;
    }
    comp.deckNames[slot] = look.name;
}

void App::loadFactoryBank() {
    Composition comp = engine_.comp;
    const std::vector<int>& bank = factoryBank();
    const std::vector<Composition>& factory = factoryPresets();
    comp.deckUsed.fill(false);
    comp.deckNames = {};
    comp.activeDeck = 0;
    for (size_t slot = 0; slot < bank.size() && slot < static_cast<size_t>(kDeckCount); ++slot) {
        const Composition& look = factory[static_cast<size_t>(bank[slot])];
        comp.deckUsed[slot] = true;
        comp.deckNames[slot] = look.name;
        if (slot == 0) {
            restoreDeck(comp, captureDeck(look));
            comp.bpm = look.bpm;
        } else {
            comp.decks[slot] = captureDeck(look);
        }
    }
    comp.name = "Factory Bank";
    engine_.applyComposition(comp);
    std::snprintf(presetName_, sizeof(presetName_), "%s", engine_.comp.name.c_str());
    toast(tr("Loaded 16 factory looks into the decks"));
}

void App::drawDeckBar() {
    Composition& comp = engine_.comp;
    const float buttonWidth = 34.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Decks"));
    const bool compact = ImGui::GetContentRegionAvail().x < ui::px(1480.0f);
    auto drawDeckButton = [&](int d, bool inPicker) {
        ImGui::PushID(d);
        const bool used = comp.deckUsed[static_cast<size_t>(d)];
        const bool active = d == comp.activeDeck;
        const bool outgoing = engine_.transitioning() && engine_.transitionSource() == d && !active;
        int colors = 0;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kBlack);
            colors = 3;
        } else if (outgoing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentDim);
            colors = 1;
        } else if (!used) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.085f, 0.085f, 0.095f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.38f, 0.42f, 1.0f));
            colors = 2;
        }
        if (ImGui::Button(std::to_string(d + 1).c_str(), ImVec2(buttonWidth, 0.0f))) {
            switchDeckTo(d);
            if (inPicker) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopStyleColor(colors);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            std::string tip = deckDisplayName(comp, d);
            tip += used ? std::string("\n") + tr("Click to switch with a smooth transition") : std::string("\n") + tr("Empty. Click to start a new deck here");
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (ImGui::BeginPopupContextItem("deckmenu")) {
            ImGui::TextDisabled("%s", deckDisplayName(comp, d).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Rename..."))) {
                renameDeck_ = d;
                std::snprintf(deckNameBuffer_, sizeof(deckNameBuffer_), "%s", comp.deckNames[static_cast<size_t>(d)].c_str());
                renameRequested_ = true;
            }
            if (ImGui::MenuItem(tr("Copy the current deck here"), nullptr, false, d != comp.activeDeck)) {
                engine_.duplicateActiveDeckTo(d);
            }
            if (ImGui::BeginMenu(tr("Fill with factory look"))) {
                for (const Composition& look : factoryPresets()) {
                    if (ImGui::MenuItem(look.name.c_str(), isNewPreset(look.name) ? tr("new") : nullptr)) {
                        fillDeckFromLook(d, look);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Clear deck"))) {
                engine_.clearDeck(d);
            }
            if (ImGui::MenuItem(tr("Remove deck"), nullptr, false, used && usedDeckCount(comp) > 1)) {
                engine_.removeDeck(d);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    if (!compact) {
        for (int d = 0; d < kDeckCount; ++d) {
            ImGui::SameLine(0.0f, 4.0f);
            drawDeckButton(d, false);
        }
    } else {
        ImGui::SameLine(0.0f, 4.0f);
        const std::string pickLabel = std::string(tr("Deck")) + " " + std::to_string(comp.activeDeck + 1) + "###deckPick";
        if (ImGui::Button(pickLabel.c_str(), ImVec2(ui::px(84.0f), 0.0f))) {
            ImGui::OpenPopup("###deckPicker");
        }
        if (ImGui::BeginPopup("###deckPicker")) {
            for (int d = 0; d < kDeckCount; ++d) {
                if (d % 8 != 0) {
                    ImGui::SameLine(0.0f, 4.0f);
                }
                drawDeckButton(d, true);
            }
            ImGui::EndPopup();
        }
    }

    if (renameRequested_) {
        ImGui::OpenPopup("###renameDeck");
        renameRequested_ = false;
    }
    if (ImGui::BeginPopup("###renameDeck")) {
        ImGui::TextDisabled("%s %d", tr("Deck"), renameDeck_ + 1);
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        if (ImGui::InputText("##deckname", deckNameBuffer_, sizeof(deckNameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (renameDeck_ >= 0 && renameDeck_ < kDeckCount) {
                comp.deckNames[static_cast<size_t>(renameDeck_)] = deckNameBuffer_;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button("<")) {
        switchDeckTo(nextUsedDeck(comp, comp.activeDeck, -1));
    }
    ImGui::SetItemTooltip("%s", tr("Previous deck"));
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button(">")) {
        switchDeckTo(nextUsedDeck(comp, comp.activeDeck, 1));
    }
    ImGui::SetItemTooltip("%s", tr("Next deck"));
}

void App::drawDeck() {
    ImGui::Begin(trId("Deck", "deck"));
    const std::vector<ClipDef>& library = clipLibrary();
    Composition& comp = engine_.comp;
    const float cellW = 108.0f;
    const float cellH = 61.0f;
    const bool flat = ui::kFlatLook;
    ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings;
    if (!flat) {
        flags |= ImGuiTableFlags_BordersInnerV;
    }

    const ImGuiStyle& deckStyle = ImGui::GetStyle();
    const float transportHeight = (ImGui::GetFrameHeight() + deckStyle.ItemSpacing.y) * 2.0f + deckStyle.ItemSpacing.y;
    const ImVec2 tableSize(0.0f, std::max(80.0f, ImGui::GetContentRegionAvail().y - transportHeight));

    if (ImGui::BeginTable("deck", kColumnCount + 1, flags, tableSize)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("##layers", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        for (int c = 0; c < kColumnCount; ++c) {
            ImGui::TableSetupColumn(("##column" + std::to_string(c)).c_str(), ImGuiTableColumnFlags_WidthFixed, flat ? cellW + 5.0f : cellW + 8.0f);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        const std::string& activeName = comp.deckNames[static_cast<size_t>(comp.activeDeck)];
        ImGui::TextDisabled("%s", activeName.empty() ? comp.name.c_str() : activeName.c_str());
        for (int c = 0; c < kColumnCount; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            ImGui::PushID(c);
            std::string label = std::string(tr("Column ")) + std::to_string(c + 1);
            bool playing = false;
            for (const Layer& other : comp.layers) {
                playing = playing || other.activeColumn == c;
            }
            int headerColors = 0;
            if (playing && flat) {
                ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::kAccentHover);
                ImGui::PushStyleColor(ImGuiCol_Text, ui::kBlack);
                headerColors += 3;
            } else {
                if (playing) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentDim);
                    ++headerColors;
                }
                if (c == selectedColumn_) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::kAccentHover);
                    ++headerColors;
                }
            }
            if (ImGui::Button(label.c_str(), ImVec2(cellW, 0.0f))) {
                engine_.requestTriggerColumn(c);
                selectedColumn_ = c;
            }
            ImGui::PopStyleColor(headerColors);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NVJ_CLIP")) {
                    selectedColumn_ = c;
                    assignClipToSelectedCell(*static_cast<const int*>(payload->Data));
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SetItemTooltip("%s", tr("Plays this column. Drop a clip here to put it into the selected layer."));
            ImGui::PopID();
        }

        for (int row = kLayerCount - 1; row >= 0; --row) {
            Layer& layer = comp.layers[static_cast<size_t>(row)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, flat ? 78.0f : 84.0f);
            ImGui::TableSetColumnIndex(0);
            if (row == selectedLayer_) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ui::kSelectionBg));
            }
            ImGui::PushID(row);
            if (flat) {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                ImGui::Image(ui::texId(engine_.layerPreview(row)), ImVec2(96.0f, 54.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                const ImVec2 nameMin(start.x, start.y + 57.0f);
                ImGui::GetWindowDrawList()->AddRectFilled(nameMin, ImVec2(nameMin.x + 96.0f, nameMin.y + 17.0f), ImGui::GetColorU32(ImGuiCol_Button));
                ImGui::SetCursorScreenPos(nameMin);
                const bool nameSelected = row == selectedLayer_;
                if (nameSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ui::kAccentHover);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui::kAccentHover);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::kBlack);
                }
                const std::string name = std::string(tr("Layer ")) + std::to_string(row + 1);
                if (ImGui::Selectable(name.c_str(), nameSelected, 0, ImVec2(96.0f, 17.0f))) {
                    selectedLayer_ = row;
                }
                if (nameSelected) {
                    ImGui::PopStyleColor(3);
                }
                ImGui::SetCursorScreenPos(ImVec2(start.x + 102.0f, start.y));
                ImGui::BeginGroup();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));
                const float buttonWidth = std::floor((ImGui::GetContentRegionAvail().x - 6.0f) / 3.0f);
                const ImVec2 buttonSize(buttonWidth, 24.0f);
                if (ImGui::Button("X", buttonSize)) {
                    engine_.clearLayer(row);
                }
                ImGui::SameLine();
                ui::toggleButton("B", layer.bypass, buttonSize);
                ImGui::SameLine();
                ui::toggleButton("S", layer.solo, buttonSize);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##blend", tr(blendName(layer.blend)))) {
                    for (int b = 0; b < BlendModeCount; ++b) {
                        if (ImGui::Selectable(tr(blendName(b)), layer.blend == b)) {
                            layer.blend = b;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                ui::sliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
                ImGui::PopStyleVar();
            } else {
                ImGui::Image(ui::texId(engine_.layerPreview(row)), ImVec2(96.0f, 54.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
                std::string title = std::string(tr("Layer ")) + std::to_string(row + 1);
                if (ImGui::Selectable(title.c_str(), row == selectedLayer_, 0, ImVec2(62.0f, 0.0f))) {
                    selectedLayer_ = row;
                }
                ImGui::SameLine();
                ui::toggleButton("B", layer.bypass);
                ImGui::SameLine();
                ui::toggleButton("S", layer.solo);
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    engine_.clearLayer(row);
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                ui::sliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##blend", tr(blendName(layer.blend)))) {
                    for (int b = 0; b < BlendModeCount; ++b) {
                        if (ImGui::Selectable(tr(blendName(b)), layer.blend == b)) {
                            layer.blend = b;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopStyleVar();
            }
            ImGui::EndGroup();
            ImGui::PopID();

            for (int c = 0; c < kColumnCount; ++c) {
                ImGui::TableSetColumnIndex(c + 1);
                ImGui::PushID(row * 1000 + c);
                int clip = layer.cells[static_cast<size_t>(c)];
                bool active = layer.activeColumn == c;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 size(cellW, cellH);
                if (ImGui::InvisibleButton("cell", size)) {
                    if (ImGui::GetIO().KeyCtrl) {
                        selectedLayer_ = row;
                        selectedColumn_ = c;
                    } else {
                        engine_.requestTrigger(row, c);
                        selectedLayer_ = row;
                        selectedColumn_ = c;
                    }
                }
                bool hovered = ImGui::IsItemHovered();
                if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", clip >= 0 ? tr("Click to play. Ctrl+click to preview without playing. Drag to copy it to another cell. Right click for more.") : tr("Empty cell. Drag a clip here or click a clip in the Library."));
                }

                ImDrawList* draw = ImGui::GetWindowDrawList();
                ImVec2 max(pos.x + size.x, pos.y + size.y);
                if (clip >= 0) {
                    GLuint texture = (active && engine_.layerLive(row)) ? engine_.layerPreview(row) : engine_.clipThumbnail(clip);
                    ImU32 tint = (active && !engine_.layerLive(row)) ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE;
                    draw->AddImage(ui::texId(texture), pos, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
                    draw->AddRectFilled(ImVec2(pos.x, max.y - 16.0f), max, flat ? ImGui::GetColorU32(ImGuiCol_Button) : IM_COL32(0, 0, 0, 170));
                    draw->PushClipRect(pos, max, true);
                    draw->AddText(ImVec2(pos.x + 4.0f, max.y - 15.0f), IM_COL32(235, 235, 235, 255), library[static_cast<size_t>(clip)].name.c_str());
                    draw->PopClipRect();
                } else {
                    draw->AddRectFilled(pos, max, flat ? IM_COL32(27, 26, 25, 255) : IM_COL32(19, 19, 22, 255));
                }
                if (row == selectedLayer_ && c == selectedColumn_) {
                    draw->AddTriangleFilled(pos, ImVec2(pos.x + 19.0f, pos.y), ImVec2(pos.x, pos.y + 19.0f), IM_COL32(0, 0, 0, 230));
                    draw->AddTriangleFilled(pos, ImVec2(pos.x + 14.0f, pos.y), ImVec2(pos.x, pos.y + 14.0f), ImGui::GetColorU32(ui::kAccentHover));
                }
                if (active) {
                    draw->AddRect(pos, max, ImGui::GetColorU32(flat ? ui::kAccentHover : ui::kAccent), 0.0f, 0, 2.5f);
                } else if (hovered) {
                    draw->AddRect(pos, max, IM_COL32(200, 200, 210, 255), 0.0f, 0, 1.5f);
                } else if (!flat) {
                    draw->AddRect(pos, max, IM_COL32(50, 50, 58, 255));
                }

                if (clip >= 0 && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("NVJ_CLIP", &clip, sizeof(int));
                    ImGui::TextUnformatted(library[static_cast<size_t>(clip)].name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NVJ_CLIP")) {
                        layer.setCell(static_cast<size_t>(c), *static_cast<const int*>(payload->Data));
                        selectedLayer_ = row;
                        selectedColumn_ = c;
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("cellmenu")) {
                    if (ImGui::MenuItem(tr("Preview without playing"), nullptr, false, clip >= 0)) {
                        selectedLayer_ = row;
                        selectedColumn_ = c;
                    }
                    if (ImGui::MenuItem(tr("Clear cell"), nullptr, false, clip >= 0)) {
                        layer.setCell(static_cast<size_t>(c), -1);
                        if (active) {
                            layer.activeColumn = -1;
                        }
                    }
                    if (ImGui::BeginMenu(tr("Assign clip"))) {
                        for (int i = 0; i < static_cast<int>(library.size()); ++i) {
                            if (ImGui::MenuItem(library[static_cast<size_t>(i)].name.c_str(), nullptr, clip == i)) {
                                layer.setCell(static_cast<size_t>(c), i);
                            }
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    drawTransportBar();
    ImGui::End();
}

void App::drawOutput() {
    ImGui::Begin(trId("Output", "output"));
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetTextLineHeightWithSpacing();
    if (avail.x > 8.0f && avail.y > 8.0f && engine_.outputTexture()) {
        const float aspect = static_cast<float>(engine_.width()) / static_cast<float>(engine_.height());
        ImVec2 size = avail;
        if (size.x / size.y > aspect) {
            size.x = size.y * aspect;
        } else {
            size.y = size.x / aspect;
        }
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - size.x) * 0.5f, cursor.y + (avail.y - size.y) * 0.5f));
        ImGui::Image(ui::texId(engine_.outputTexture()), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y * 0.5f);
    ImGui::TextDisabled("%d x %d   %.0f FPS   %.0f BPM", engine_.width(), engine_.height(), ImGui::GetIO().Framerate, engine_.comp.bpm);
    if (spout_.active()) {
        ImGui::SameLine();
        ImGui::TextColored(ui::kAccent, "Spout");
    }
    ImGui::End();
}

void App::drawPreview() {
    if (!ImGui::Begin(trId("Preview", "preview"))) {
        ImGui::End();
        return;
    }
    selectedLayer_ = std::clamp(selectedLayer_, 0, kLayerCount - 1);
    selectedColumn_ = std::clamp(selectedColumn_, 0, kColumnCount - 1);
    const Layer& layer = engine_.comp.layers[static_cast<size_t>(selectedLayer_)];
    const int clip = layer.cells[static_cast<size_t>(selectedColumn_)];
    const bool live = layer.activeColumn == selectedColumn_ && engine_.layerLive(selectedLayer_);
    GLuint texture = 0;
    if (live) {
        texture = engine_.layerPreview(selectedLayer_);
    } else if (clip >= 0) {
        engine_.requestPreview(selectedLayer_, selectedColumn_);
        texture = engine_.previewTexture();
        if (!texture) {
            texture = engine_.clipThumbnail(clip);
        }
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetTextLineHeightWithSpacing();
    if (texture && avail.x > 8.0f && avail.y > 8.0f) {
        const float aspect = static_cast<float>(engine_.width()) / static_cast<float>(std::max(1, engine_.height()));
        ImVec2 size = avail;
        if (size.x / size.y > aspect) {
            size.x = size.y * aspect;
        } else {
            size.y = size.x / aspect;
        }
        ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - size.x) * 0.5f, cursor.y + (avail.y - size.y) * 0.5f));
        ImGui::Image(ui::texId(texture), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y * 0.5f);
    const std::vector<ClipDef>& library = clipLibrary();
    std::string label = ui::cellLabel(selectedLayer_, selectedColumn_);
    if (clip >= 0) {
        label += "   " + library[static_cast<size_t>(clip)].name;
    }
    if (clip >= 0) {
        if (live) {
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", tr("Live"));
        } else {
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", tr("Preview"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Play now"))) {
                engine_.trigger(selectedLayer_, selectedColumn_);
            }
        }
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", label.c_str());
    ImGui::End();
}

}

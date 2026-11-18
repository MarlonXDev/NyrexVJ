#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <cfloat>
#include <string>

namespace nvj {

void App::drawDeck() {
    ImGui::Begin(trId("Deck", "deck"));
    const std::vector<ClipDef>& library = clipLibrary();
    Composition& comp = engine_.comp;
    const float cellW = 108.0f;
    const float cellH = 61.0f;
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings;

    if (ImGui::BeginTable("deck", kColumnCount + 1, flags)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("##layers", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        for (int c = 0; c < kColumnCount; ++c) {
            ImGui::TableSetupColumn(("##column" + std::to_string(c)).c_str(), ImGuiTableColumnFlags_WidthFixed, cellW + 8.0f);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", comp.name.c_str());
        for (int c = 0; c < kColumnCount; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            ImGui::PushID(c);
            std::string label = std::string(tr("Column ")) + std::to_string(c + 1);
            if (ImGui::Button(label.c_str(), ImVec2(cellW, 0.0f))) {
                engine_.triggerColumn(c);
            }
            ImGui::PopID();
        }

        for (int row = kLayerCount - 1; row >= 0; --row) {
            Layer& layer = comp.layers[static_cast<size_t>(row)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 84.0f);
            ImGui::TableSetColumnIndex(0);
            if (row == selectedLayer_) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.20f, 0.10f, 0.02f, 1.0f)));
            }
            ImGui::PushID(row);
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
            ImGui::SliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
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
                    engine_.trigger(row, c);
                    selectedLayer_ = row;
                }
                bool hovered = ImGui::IsItemHovered();

                ImDrawList* draw = ImGui::GetWindowDrawList();
                ImVec2 max(pos.x + size.x, pos.y + size.y);
                if (clip >= 0) {
                    GLuint texture = (active && engine_.layerLive(row)) ? engine_.layerPreview(row) : engine_.clipThumbnail(clip);
                    ImU32 tint = (active && !engine_.layerLive(row)) ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE;
                    draw->AddImage(ui::texId(texture), pos, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
                    draw->AddRectFilled(ImVec2(pos.x, max.y - 16.0f), max, IM_COL32(0, 0, 0, 170));
                    draw->PushClipRect(pos, max, true);
                    draw->AddText(ImVec2(pos.x + 4.0f, max.y - 15.0f), IM_COL32(235, 235, 235, 255), library[static_cast<size_t>(clip)].name.c_str());
                    draw->PopClipRect();
                } else {
                    draw->AddRectFilled(pos, max, IM_COL32(19, 19, 22, 255));
                }
                if (active) {
                    draw->AddRect(pos, max, ImGui::GetColorU32(ui::kAccent), 0.0f, 0, 2.5f);
                } else if (hovered) {
                    draw->AddRect(pos, max, IM_COL32(200, 200, 210, 255), 0.0f, 0, 1.5f);
                } else {
                    draw->AddRect(pos, max, IM_COL32(50, 50, 58, 255));
                }

                if (clip >= 0 && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("NVJ_CLIP", &clip, sizeof(int));
                    ImGui::TextUnformatted(library[static_cast<size_t>(clip)].name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NVJ_CLIP")) {
                        layer.cells[static_cast<size_t>(c)] = *static_cast<const int*>(payload->Data);
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("cellmenu")) {
                    if (ImGui::MenuItem(tr("Clear cell"), nullptr, false, clip >= 0)) {
                        layer.cells[static_cast<size_t>(c)] = -1;
                        if (active) {
                            layer.activeColumn = -1;
                        }
                    }
                    if (ImGui::BeginMenu(tr("Assign clip"))) {
                        for (int i = 0; i < static_cast<int>(library.size()); ++i) {
                            if (ImGui::MenuItem(library[static_cast<size_t>(i)].name.c_str(), nullptr, clip == i)) {
                                layer.cells[static_cast<size_t>(c)] = i;
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

}

#include "app.hpp"

#include "clip_library.hpp"
#include "log.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>

namespace nvj {

namespace {

struct ResolutionPreset {
    const char* label;
    int width;
    int height;
};

const ResolutionPreset kResolutions[] = {
    {"1280 x 720", 1280, 720},
    {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440},
};

const ImVec4 kAccent(1.00f, 0.45f, 0.00f, 1.00f);
const ImVec4 kAccentHover(1.00f, 0.58f, 0.20f, 1.00f);
const ImVec4 kBlack(0.04f, 0.04f, 0.04f, 1.00f);

bool toggleButton(const char* label, bool& value) {
    if (value) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, kBlack);
    }
    bool pressed = ImGui::SmallButton(label);
    if (value) {
        ImGui::PopStyleColor(3);
    }
    if (pressed) {
        value = !value;
    }
    return pressed;
}

}

bool App::init(GLFWwindow* window) {
    window_ = window;
    ImGuiIO& io = ImGui::GetIO();
    iniPath_ = (std::filesystem::path(userDataDir()) / "layout.ini").string();
    io.IniFilename = iniPath_.c_str();
    needsLayout_ = !std::filesystem::exists(iniPath_);
    const ResolutionPreset& preset = kResolutions[resolutionIndex_];
    if (!engine_.init(preset.width, preset.height, 4, 8)) {
        logLine("Engine initialisation failed");
        return false;
    }
    return true;
}

void App::shutdown() {
    engine_.shutdown();
}

void App::frame(double dt) {
    engine_.update(dt);
    engine_.render();
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (needsLayout_) {
        buildLayout(dock);
        needsLayout_ = false;
    }
    handleShortcuts();
    drawDeck();
    drawOutput();
    drawMaster();
    drawLibrary();
}

void App::buildLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
    ImGuiID bottom = 0;
    ImGuiID top = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Up, 0.42f, nullptr, &bottom);
    ImGuiID rest = 0;
    ImGuiID output = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.48f, nullptr, &rest);
    ImGuiID library = 0;
    ImGuiID master = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.62f, nullptr, &library);
    ImGui::DockBuilderDockWindow("Deck", top);
    ImGui::DockBuilderDockWindow("Output", output);
    ImGui::DockBuilderDockWindow("Master", master);
    ImGui::DockBuilderDockWindow("Library", library);
    ImGui::DockBuilderFinish(dock);
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    const int count = std::min(engine_.columns, 9);
    for (int i = 0; i < count; ++i) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
            engine_.triggerColumn(i);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        engine_.tap();
    }
}

void App::drawDeck() {
    ImGui::Begin("Deck");
    const std::vector<ClipDef>& library = clipLibrary();
    const int columnCount = engine_.columns;
    const int layerCount = static_cast<int>(engine_.layers.size());
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings;

    if (ImGui::BeginTable("deck", columnCount + 1, flags)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("##layers", ImGuiTableColumnFlags_WidthFixed, 250.0f);
        for (int c = 0; c < columnCount; ++c) {
            ImGui::TableSetupColumn(("##column" + std::to_string(c)).c_str(), ImGuiTableColumnFlags_WidthFixed, 108.0f);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Composition");
        for (int c = 0; c < columnCount; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            ImGui::PushID(c);
            if (ImGui::Button(("Column " + std::to_string(c + 1)).c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                engine_.triggerColumn(c);
            }
            ImGui::PopID();
        }

        for (int row = layerCount - 1; row >= 0; --row) {
            Layer& layer = engine_.layers[static_cast<size_t>(row)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 52.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(row);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Layer %d", row + 1);
            ImGui::SameLine();
            toggleButton("B", layer.bypass);
            ImGui::SameLine();
            toggleButton("S", layer.solo);
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                engine_.clearLayer(row);
            }
            ImGui::SetNextItemWidth(112.0f);
            ImGui::SliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##blend", &layer.blend, "Normal\0Add\0Screen\0Multiply\0");
            ImGui::PopID();

            for (int c = 0; c < columnCount; ++c) {
                ImGui::TableSetColumnIndex(c + 1);
                ImGui::PushID(row * 1000 + c);
                int clip = layer.cells[static_cast<size_t>(c)];
                bool active = layer.activeColumn == c;
                int styleColors = 0;
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
                    ImGui::PushStyleColor(ImGuiCol_Text, kBlack);
                    styleColors = 3;
                } else if (clip < 0) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.075f, 0.075f, 0.085f, 1.0f));
                    styleColors = 1;
                }
                std::string label = (clip >= 0 ? library[static_cast<size_t>(clip)].name : std::string()) + "##cell";
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 44.0f))) {
                    engine_.trigger(row, c);
                }
                ImGui::PopStyleColor(styleColors);

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NVJ_CLIP")) {
                        layer.cells[static_cast<size_t>(c)] = *static_cast<const int*>(payload->Data);
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("cellmenu")) {
                    if (ImGui::MenuItem("Clear cell", nullptr, false, clip >= 0)) {
                        layer.cells[static_cast<size_t>(c)] = -1;
                        if (active) {
                            layer.activeColumn = -1;
                        }
                    }
                    if (ImGui::BeginMenu("Assign clip")) {
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
    ImGui::Begin("Output");
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
        ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(engine_.outputTexture())), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y * 0.5f);
    ImGui::TextDisabled("%d x %d   %.0f FPS", engine_.width(), engine_.height(), ImGui::GetIO().Framerate);
    ImGui::End();
}

void App::drawMaster() {
    ImGui::Begin("Master");
    ImGui::SliderFloat("Master", &engine_.master, 0.0f, 1.0f, "%.2f");

    float bpm = engine_.bpm;
    if (ImGui::DragFloat("BPM", &bpm, 0.1f, 40.0f, 240.0f, "%.1f")) {
        engine_.bpm = std::clamp(bpm, 40.0f, 240.0f);
    }
    if (ImGui::Button("Tap")) {
        engine_.tap();
    }
    ImGui::SameLine();
    if (ImGui::Button("Resync")) {
        engine_.resync();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Beat %d", static_cast<int>(std::floor(engine_.beat())) % 4 + 1);

    int resolution = resolutionIndex_;
    if (ImGui::BeginCombo("Resolution", kResolutions[resolution].label)) {
        for (int i = 0; i < static_cast<int>(sizeof(kResolutions) / sizeof(kResolutions[0])); ++i) {
            if (ImGui::Selectable(kResolutions[i].label, i == resolution)) {
                if (engine_.setResolution(kResolutions[i].width, kResolutions[i].height)) {
                    resolutionIndex_ = i;
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Effects");
    ImGui::SliderFloat("Hue", &engine_.fx.hue, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Pixelate", &engine_.fx.pixelate, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("RGB split", &engine_.fx.rgbSplit, 0.0f, 0.03f, "%.4f");
    ImGui::Checkbox("Invert", &engine_.fx.invert);
    ImGui::SameLine();
    ImGui::Checkbox("Mirror X", &engine_.fx.mirrorX);
    ImGui::SameLine();
    ImGui::Checkbox("Mirror Y", &engine_.fx.mirrorY);
    ImGui::End();
}

void App::drawLibrary() {
    ImGui::Begin("Library");
    ImGui::TextDisabled("Drag a clip onto a cell");
    const std::vector<ClipDef>& library = clipLibrary();
    for (int i = 0; i < static_cast<int>(library.size()); ++i) {
        ImGui::PushID(i);
        const char* name = library[static_cast<size_t>(i)].name.c_str();
        if (ImGui::Selectable(name, selectedClip_ == i)) {
            selectedClip_ = i;
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("NVJ_CLIP", &i, sizeof(int));
            ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

}

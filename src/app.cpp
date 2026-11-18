#include "app.hpp"

#include "clip_library.hpp"
#include "effect_library.hpp"
#include "log.hpp"
#include "platform.hpp"
#include "preset.hpp"

#include <GLFW/glfw3.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace nvj {

namespace {

const ImVec4 kAccent(1.00f, 0.45f, 0.00f, 1.00f);
const ImVec4 kAccentHover(1.00f, 0.58f, 0.20f, 1.00f);
const ImVec4 kBlack(0.04f, 0.04f, 0.04f, 1.00f);
const char* const kReleasesUrl = "https://github.com/MarlonXDev/NyrexVJ/releases";
const char* const kRepoUrl = "https://github.com/MarlonXDev/NyrexVJ";

struct Palette {
    const char* name;
    std::array<float, 3> a;
    std::array<float, 3> b;
};

const Palette kPalettes[] = {
    {"Orange / Cyan", {1.00f, 0.45f, 0.00f}, {0.00f, 0.80f, 1.00f}},
    {"Neon Pink / Blue", {1.00f, 0.10f, 0.60f}, {0.20f, 0.50f, 1.00f}},
    {"Synthwave", {1.00f, 0.20f, 0.55f}, {0.20f, 0.85f, 1.00f}},
    {"Toxic", {0.45f, 1.00f, 0.10f}, {0.00f, 0.60f, 0.35f}},
    {"Ice", {0.55f, 0.85f, 1.00f}, {0.15f, 0.30f, 1.00f}},
    {"Sunset", {1.00f, 0.35f, 0.10f}, {0.70f, 0.10f, 0.60f}},
    {"Acid", {0.80f, 1.00f, 0.00f}, {1.00f, 0.00f, 0.80f}},
    {"Gold", {1.00f, 0.80f, 0.20f}, {0.70f, 0.30f, 0.00f}},
    {"Violet", {0.55f, 0.25f, 1.00f}, {0.00f, 0.85f, 0.90f}},
    {"Mono", {1.00f, 1.00f, 1.00f}, {0.35f, 0.35f, 0.35f}},
};

ImTextureID texId(GLuint texture) {
    return static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture));
}

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

Composition emptyComposition() {
    Composition comp;
    comp.name = "Empty";
    return comp;
}

}

bool App::init(GLFWwindow* window, const StartOptions& options) {
    window_ = window;
    ImGuiIO& io = ImGui::GetIO();
    iniPath_ = (std::filesystem::path(userDataDir()) / "layout_v2.ini").string();
    io.IniFilename = iniPath_.c_str();
    needsLayout_ = !std::filesystem::exists(iniPath_);

    loadSettings(settings_);
    outputMonitor_ = settings_.outputMonitor;

    if (!engine_.init()) {
        logLine("Engine initialisation failed");
        return false;
    }

    Composition start = defaultComposition();
    bool chosen = false;
    if (!options.preset.empty()) {
        int factory = factoryPresetIndex(options.preset);
        std::string error;
        if (factory >= 0) {
            start = factoryPresets()[static_cast<size_t>(factory)];
            chosen = true;
        } else if (loadUserPreset(options.preset, start, error)) {
            chosen = true;
        } else {
            logLine("Preset '" + options.preset + "' not found");
        }
    }
    if (!chosen && settings_.restoreSession && !options.fresh && options.clip.empty()) {
        Composition session;
        if (loadSession(session)) {
            start = session;
        }
    }
    if (!options.clip.empty()) {
        int clip = clipIndexByName(options.clip);
        if (clip >= 0) {
            start = emptyComposition();
            start.name = options.clip;
            start.layers[0].cells[0] = clip;
            start.layers[0].activeColumn = 0;
        } else {
            logLine("Clip '" + options.clip + "' not found");
        }
    }
    if (!options.effect.empty()) {
        int effect = effectIndexByName(options.effect);
        if (effect >= 0) {
            setEffect(start.master.fx[0], effect);
        } else {
            logLine("Effect '" + options.effect + "' not found");
        }
    }
    if (options.resolution >= 0 && options.resolution < kResolutionCount) {
        start.resolution = options.resolution;
    }
    engine_.applyComposition(start);
    std::snprintf(presetName_, sizeof(presetName_), "%s", engine_.comp.name.c_str());

    if (options.column >= 0) {
        engine_.triggerColumn(options.column);
    }
    if (options.selectLayer >= 0 && options.selectLayer < kLayerCount) {
        selectedLayer_ = options.selectLayer;
    }
    focusTab_ = options.tab;

    refreshUserPresets();

    updater_.setChannel(settings_.channel);
    if (settings_.checkUpdates && !options.skipUpdateCheck) {
        updater_.checkAsync();
    }

    if (options.openOutput) {
        openOutputWindow(false);
    }
    return true;
}

void App::shutdown() {
    if (shutDown_) {
        return;
    }
    shutDown_ = true;
    saveSession(engine_.comp);
    settings_.outputMonitor = outputMonitor_;
    saveSettings(settings_);
    output_.close();
    engine_.shutdown();
}

void App::toast(const std::string& message) {
    toastText_ = message;
    toastUntil_ = ImGui::GetTime() + 4.0;
}

void App::openOutputWindow(bool fullscreen) {
    if (!output_.open(window_, outputMonitor_, fullscreen)) {
        toast("Could not open the output window");
    }
}

void App::refreshUserPresets() {
    userPresets_ = listUserPresets();
}

void App::loadComposition(const Composition& comp, const std::string& message) {
    engine_.applyComposition(comp);
    std::snprintf(presetName_, sizeof(presetName_), "%s", engine_.comp.name.c_str());
    toast(message);
}

void App::saveCurrentPreset() {
    std::string error;
    std::string name = sanitizePresetName(presetName_);
    if (!saveUserPreset(engine_.comp, name, error)) {
        toast(error);
        return;
    }
    engine_.comp.name = name;
    selectedUserPreset_ = name;
    refreshUserPresets();
    toast("Saved preset: " + name);
}

void App::frame(double dt) {
    engine_.update(dt);
    engine_.render();
    output_.render(engine_.outputTexture(), engine_.width(), engine_.height());

    drawMenuBar();
    ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (needsLayout_) {
        buildLayout(dock);
        needsLayout_ = false;
    }
    handleShortcuts();
    drawDeck();
    drawOutput();
    drawLayer();
    drawMaster();
    drawBrowser();

    autosaveTimer_ += dt;
    if (autosaveTimer_ > 30.0) {
        autosaveTimer_ = 0.0;
        saveSession(engine_.comp);
    }
}

void App::buildLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
    ImGuiID bottom = 0;
    ImGuiID top = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Up, 0.45f, nullptr, &bottom);
    ImGuiID rest = 0;
    ImGuiID output = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.30f, nullptr, &rest);
    ImGuiID rest2 = 0;
    ImGuiID layer = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.34f, nullptr, &rest2);
    ImGuiID browser = 0;
    ImGuiID master = ImGui::DockBuilderSplitNode(rest2, ImGuiDir_Left, 0.55f, nullptr, &browser);
    ImGui::DockBuilderDockWindow("Deck", top);
    ImGui::DockBuilderDockWindow("Output", output);
    ImGui::DockBuilderDockWindow("Layer", layer);
    ImGui::DockBuilderDockWindow("Master", master);
    ImGui::DockBuilderDockWindow("Browser", browser);
    ImGui::DockBuilderFinish(dock);
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    for (int i = 0; i < kColumnCount; ++i) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
            engine_.triggerColumn(i);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        engine_.tap();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        engine_.blackout = !engine_.blackout;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
        if (output_.isOpen()) {
            output_.setFullscreen(!output_.isFullscreen(), outputMonitor_);
        } else {
            openOutputWindow(true);
        }
    }
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New empty composition")) {
            loadComposition(emptyComposition(), "New composition");
        }
        if (ImGui::MenuItem("Save as preset...")) {
            focusTab_ = 0;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open data folder")) {
            platform::openFolder(userDataDir());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Output")) {
        if (ImGui::MenuItem(output_.isOpen() ? "Close output window" : "Open output window")) {
            if (output_.isOpen()) {
                output_.close();
            } else {
                openOutputWindow(false);
            }
        }
        if (ImGui::MenuItem("Fullscreen", "F11", output_.isFullscreen(), output_.isOpen())) {
            output_.setFullscreen(!output_.isFullscreen(), outputMonitor_);
        }
        std::vector<std::string> monitors = OutputWindow::monitorNames();
        if (!monitors.empty() && ImGui::BeginMenu("Display")) {
            for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                if (ImGui::MenuItem(monitors[static_cast<size_t>(i)].c_str(), nullptr, i == outputMonitor_)) {
                    outputMonitor_ = i;
                    if (output_.isFullscreen()) {
                        output_.setFullscreen(true, outputMonitor_);
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("NyrexVJ on GitHub")) {
            platform::openUrl(kRepoUrl);
        }
        if (ImGui::MenuItem("Releases")) {
            platform::openUrl(kReleasesUrl);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Check for updates", nullptr, false, !updater_.busy())) {
            updater_.checkAsync();
            focusTab_ = 2;
        }
        ImGui::EndMenu();
    }

    UpdateStatus update = updater_.status();
    if (update.state == UpdateState::Available) {
        ImGui::TextColored(kAccent, "Update available");
        if (ImGui::SmallButton("Install")) {
            updater_.installAsync();
        }
    } else if (update.state == UpdateState::Downloading) {
        ImGui::ProgressBar(update.progress, ImVec2(140.0f, 0.0f), "Updating");
    } else if (update.state == UpdateState::ReadyToRestart) {
        ImGui::TextColored(kAccent, "Update installed");
        if (ImGui::SmallButton("Restart now")) {
            restart_ = true;
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
    if (!toastText_.empty() && ImGui::GetTime() < toastUntil_) {
        ImGui::TextDisabled("%s", toastText_.c_str());
    }
    if (engine_.blackout) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "BLACKOUT");
    }
    ImGui::EndMainMenuBar();
}

void App::drawDeck() {
    ImGui::Begin("Deck");
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
            if (ImGui::Button(("Column " + std::to_string(c + 1)).c_str(), ImVec2(cellW, 0.0f))) {
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
            ImGui::Image(texId(engine_.layerPreview(row)), ImVec2(96.0f, 54.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
            std::string title = "Layer " + std::to_string(row + 1);
            if (ImGui::Selectable(title.c_str(), row == selectedLayer_, 0, ImVec2(62.0f, 0.0f))) {
                selectedLayer_ = row;
            }
            ImGui::SameLine();
            toggleButton("B", layer.bypass);
            ImGui::SameLine();
            toggleButton("S", layer.solo);
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                engine_.clearLayer(row);
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##opacity", &layer.opacity, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##blend", &layer.blend, "Normal\0Add\0Screen\0Multiply\0");
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
                    draw->AddImage(texId(texture), pos, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
                    draw->AddRectFilled(ImVec2(pos.x, max.y - 16.0f), max, IM_COL32(0, 0, 0, 170));
                    draw->PushClipRect(pos, max, true);
                    draw->AddText(ImVec2(pos.x + 4.0f, max.y - 15.0f), IM_COL32(235, 235, 235, 255), library[static_cast<size_t>(clip)].name.c_str());
                    draw->PopClipRect();
                } else {
                    draw->AddRectFilled(pos, max, IM_COL32(19, 19, 22, 255));
                }
                if (active) {
                    draw->AddRect(pos, max, ImGui::GetColorU32(kAccent), 0.0f, 0, 2.5f);
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
        ImGui::Image(texId(engine_.outputTexture()), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y * 0.5f);
    ImGui::TextDisabled("%d x %d   %.0f FPS   %.0f BPM", engine_.width(), engine_.height(), ImGui::GetIO().Framerate, engine_.comp.bpm);
    ImGui::End();
}

void App::drawGrade(ColorGrade& grade) {
    ImGui::SliderFloat("Hue", &grade.hue, -0.5f, 0.5f, "%.2f");
    ImGui::SliderFloat("Saturation", &grade.saturation, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Brightness", &grade.brightness, -0.5f, 0.5f, "%.2f");
    ImGui::SliderFloat("Contrast", &grade.contrast, 0.5f, 2.0f, "%.2f");
    if (!grade.isIdentity() && ImGui::SmallButton("Reset grade")) {
        grade = ColorGrade{};
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
        const char* preview = slot.effect >= 0 ? library[static_cast<size_t>(slot.effect)].name.c_str() : "- empty -";
        if (ImGui::BeginCombo("##effect", preview)) {
            if (ImGui::Selectable("- empty -", slot.effect < 0)) {
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
                ImGui::SliderFloat(def.paramNames[static_cast<size_t>(p)], &slot.params[static_cast<size_t>(p)], 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    slot.params[static_cast<size_t>(p)] = def.defaults[static_cast<size_t>(p)];
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
        if (s + 1 < kEffectSlots) {
            ImGui::Separator();
        }
    }
}

void App::drawLayer() {
    ImGui::Begin("Layer");
    selectedLayer_ = std::clamp(selectedLayer_, 0, kLayerCount - 1);
    Layer& layer = engine_.comp.layers[static_cast<size_t>(selectedLayer_)];
    const std::vector<ClipDef>& library = clipLibrary();

    for (int i = 0; i < kLayerCount; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        bool selected = i == selectedLayer_;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_Text, kBlack);
        }
        if (ImGui::Button(std::to_string(i + 1).c_str(), ImVec2(28.0f, 0.0f))) {
            selectedLayer_ = i;
        }
        if (selected) {
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::SameLine();
    int activeClip = layer.activeColumn >= 0 ? layer.cells[static_cast<size_t>(layer.activeColumn)] : -1;
    ImGui::TextDisabled("%s", activeClip >= 0 ? library[static_cast<size_t>(activeClip)].name.c_str() : "no clip");

    ImGui::SeparatorText("Look");
    ImGui::SliderFloat("Speed", &layer.speed, 0.1f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Scale", &layer.scale, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Color");
    ImGui::ColorEdit3("Color A", layer.colorA.data(), ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::ColorEdit3("Color B", layer.colorB.data(), ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    if (ImGui::SmallButton("Swap")) {
        std::swap(layer.colorA, layer.colorB);
    }
    ImGui::Checkbox("Rainbow", &layer.rainbow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##palette", "Palette")) {
        for (const Palette& palette : kPalettes) {
            if (ImGui::Selectable(palette.name)) {
                layer.colorA = palette.a;
                layer.colorB = palette.b;
                layer.rainbow = false;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Grade");
    drawGrade(layer.grade);

    ImGui::SeparatorText("Effects");
    drawEffectRack(layer.fx);
    ImGui::End();
}

void App::drawOutputControls() {
    ImGui::SeparatorText("Output window");
    bool open = output_.isOpen();
    if (ImGui::Button(open ? "Close" : "Open")) {
        if (open) {
            output_.close();
        } else {
            openOutputWindow(false);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!output_.isOpen());
    if (ImGui::Button(output_.isFullscreen() ? "Windowed" : "Fullscreen")) {
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
    ImGui::TextDisabled("F11 fullscreen, Esc leaves it");
}

void App::drawMaster() {
    ImGui::Begin("Master");
    Composition& comp = engine_.comp;
    ImGui::SliderFloat("Level", &comp.master.level, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    toggleButton("Blackout", engine_.blackout);

    float bpm = comp.bpm;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::DragFloat("BPM", &bpm, 0.1f, 40.0f, 240.0f, "%.1f")) {
        comp.bpm = std::clamp(bpm, 40.0f, 240.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Tap")) {
        engine_.tap();
    }
    ImGui::SameLine();
    if (ImGui::Button("Resync")) {
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
    ImGui::TextDisabled("Beat %d", static_cast<int>(std::floor(engine_.beat())) % 4 + 1);

    if (ImGui::BeginCombo("Resolution", kResolutions[comp.resolution].label)) {
        for (int i = 0; i < kResolutionCount; ++i) {
            if (ImGui::Selectable(kResolutions[i].label, i == comp.resolution)) {
                engine_.setResolutionIndex(i);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Grade");
    drawGrade(comp.master.grade);
    ImGui::SeparatorText("Effects");
    drawEffectRack(comp.master.fx);
    drawOutputControls();
    ImGui::End();
}

void App::drawPresetsTab() {
    ImGui::TextDisabled("Factory");
    const std::vector<Composition>& factory = factoryPresets();
    for (int i = 0; i < static_cast<int>(factory.size()); ++i) {
        const Composition& preset = factory[static_cast<size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Selectable(preset.name.c_str(), engine_.comp.name == preset.name)) {
            loadComposition(preset, "Loaded " + preset.name);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("My presets");
    if (userPresets_.empty()) {
        ImGui::TextDisabled("Nothing saved yet");
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
                loadComposition(loaded, "Loaded " + name);
            } else {
                toast(error);
            }
        }
        if (ImGui::BeginPopupContextItem("presetmenu")) {
            if (ImGui::MenuItem("Overwrite with current")) {
                std::snprintf(presetName_, sizeof(presetName_), "%s", name.c_str());
                saveCurrentPreset();
            }
            if (ImGui::MenuItem("Delete")) {
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
        toast("Deleted " + toDelete);
    }

    ImGui::SeparatorText("Save current look");
    ImGui::SetNextItemWidth(-70.0f);
    ImGui::InputText("##presetname", presetName_, sizeof(presetName_));
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        saveCurrentPreset();
    }
}

void App::drawLibraryTab() {
    const std::vector<ClipDef>& library = clipLibrary();
    ImGui::TextDisabled("Drag a clip onto a deck cell");
    const float thumbW = 96.0f;
    const float thumbH = 54.0f;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(library.size()); ++i) {
        const ClipDef& clip = library[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::BeginGroup();
        ImGui::Image(texId(engine_.clipThumbnail(i)), ImVec2(thumbW, thumbH), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        const std::string& label = clip.name;
        ImGui::PushClipRect(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + thumbW, ImGui::GetItemRectMax().y + ImGui::GetTextLineHeightWithSpacing()), true);
        ImGui::TextUnformatted(label.c_str());
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

    ImGui::SeparatorText("Effects");
    const std::vector<EffectDef>& effects = effectLibrary();
    for (const EffectDef& effect : effects) {
        ImGui::BulletText("%s%s", effect.name.c_str(), effect.is3d ? "  (3D)" : "");
    }
}

void App::drawSettingsTab() {
    ImGui::Text("NyrexVJ %s", NYREXVJ_VERSION);
    if (NYREXVJ_BUILD_NUMBER > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("build %d", NYREXVJ_BUILD_NUMBER);
    }
    ImGui::TextDisabled("Made by MarlonXDev");

    ImGui::SeparatorText("Updates");
    int channel = settings_.channel == UpdateChannel::Nightly ? 0 : 1;
    if (ImGui::Combo("Channel", &channel, "Latest build (every commit)\0Stable releases\0")) {
        settings_.channel = channel == 0 ? UpdateChannel::Nightly : UpdateChannel::Stable;
        updater_.setChannel(settings_.channel);
        saveSettings(settings_);
    }
    if (ImGui::Checkbox("Check for updates on start", &settings_.checkUpdates)) {
        saveSettings(settings_);
    }
    if (ImGui::Checkbox("Restore last session on start", &settings_.restoreSession)) {
        saveSettings(settings_);
    }

    UpdateStatus update = updater_.status();
    ImGui::BeginDisabled(updater_.busy());
    if (ImGui::Button("Check now")) {
        updater_.checkAsync();
    }
    ImGui::EndDisabled();
    if (update.state == UpdateState::Available) {
        ImGui::SameLine();
        if (ImGui::Button("Download and install")) {
            updater_.installAsync();
        }
    }
    if (update.state == UpdateState::ReadyToRestart) {
        ImGui::SameLine();
        if (ImGui::Button("Restart now")) {
            restart_ = true;
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
    if (update.state == UpdateState::Downloading) {
        ImGui::ProgressBar(update.progress, ImVec2(-FLT_MIN, 0.0f));
    }
    if (!update.message.empty()) {
        ImGui::TextWrapped("%s", update.message.c_str());
    }
    if (update.state == UpdateState::Available && !update.release.notes.empty()) {
        ImGui::BeginChild("notes", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", update.release.notes.c_str());
        ImGui::EndChild();
    }

    ImGui::SeparatorText("Links and files");
    if (ImGui::Button("GitHub")) {
        platform::openUrl(kRepoUrl);
    }
    ImGui::SameLine();
    if (ImGui::Button("Releases")) {
        platform::openUrl(kReleasesUrl);
    }
    ImGui::SameLine();
    if (ImGui::Button("Data folder")) {
        platform::openFolder(userDataDir());
    }
    ImGui::TextDisabled("%s", userDataDir().c_str());

    ImGui::SeparatorText("Shortcuts");
    ImGui::BulletText("1 - 8  trigger a column");
    ImGui::BulletText("Space  tap tempo");
    ImGui::BulletText("B  blackout");
    ImGui::BulletText("F11  output window fullscreen");
    ImGui::BulletText("Right click a slider  reset effect value");
}

void App::drawBrowser() {
    ImGui::Begin("Browser");
    if (ImGui::BeginTabBar("browser_tabs")) {
        const char* names[] = {"Presets", "Library", "Settings"};
        for (int i = 0; i < 3; ++i) {
            ImGuiTabItemFlags flags = focusTab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, flags)) {
                if (i == 0) {
                    drawPresetsTab();
                } else if (i == 1) {
                    drawLibraryTab();
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

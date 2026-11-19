#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/output_map.hpp"
#include "render/output_window.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace nvj {

namespace {

bool percentSlider(const char* label, float& value, float low, float high) {
    float shown = value * 100.0f;
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::SliderFloat(label, &shown, low * 100.0f, high * 100.0f, "%.1f %%");
    if (changed) {
        value = shown / 100.0f;
    }
    return changed;
}

Vec2f toCanvas(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size) {
    return {(mouse.x - origin.x) / size.x, (mouse.y - origin.y) / size.y};
}

std::string outputLabel(int index) {
    return std::string(tr("Output")) + " " + std::to_string(index + 1);
}

}

void App::drawMappingCanvas(OutputSpec& spec, int index) {
    (void)index;
    OutputMapping& map = spec.map;
    const float width = std::max(160.0f, std::min(ImGui::GetContentRegionAvail().x, 560.0f));
    const ImVec2 size(width, width * 9.0f / 16.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("mapcanvas", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(10, 10, 12, 255));
    draw->PushClipRect(origin, end, true);

    const auto screen = [&](const Vec2f& p) { return ImVec2(origin.x + p.x * size.x, origin.y + p.y * size.y); };
    constexpr int kDiv = 16;
    const std::vector<MeshVertex> mesh = buildMesh(map, kDiv);
    const ImTextureID texture = ui::texId(engine_.outputTexture());
    for (int j = 0; j < kDiv; ++j) {
        for (int i = 0; i < kDiv; ++i) {
            const MeshVertex& a = mesh[static_cast<size_t>(j * (kDiv + 1) + i)];
            const MeshVertex& b = mesh[static_cast<size_t>(j * (kDiv + 1) + i + 1)];
            const MeshVertex& c = mesh[static_cast<size_t>((j + 1) * (kDiv + 1) + i + 1)];
            const MeshVertex& d = mesh[static_cast<size_t>((j + 1) * (kDiv + 1) + i)];
            draw->AddImageQuad(texture, screen({a.x, a.y}), screen({b.x, b.y}), screen({c.x, c.y}), screen({d.x, d.y}), ImVec2(a.u, a.v), ImVec2(b.u, b.v), ImVec2(c.u, c.v), ImVec2(d.u, d.v), IM_COL32(255, 255, 255, 230));
        }
    }

    const ImU32 blendColor = IM_COL32(80, 200, 255, 200);
    if (map.blend.left > 0.0f) {
        draw->AddLine(ImVec2(origin.x + map.blend.left * size.x, origin.y), ImVec2(origin.x + map.blend.left * size.x, end.y), blendColor);
    }
    if (map.blend.right > 0.0f) {
        draw->AddLine(ImVec2(end.x - map.blend.right * size.x, origin.y), ImVec2(end.x - map.blend.right * size.x, end.y), blendColor);
    }
    if (map.blend.top > 0.0f) {
        draw->AddLine(ImVec2(origin.x, origin.y + map.blend.top * size.y), ImVec2(end.x, origin.y + map.blend.top * size.y), blendColor);
    }
    if (map.blend.bottom > 0.0f) {
        draw->AddLine(ImVec2(origin.x, end.y - map.blend.bottom * size.y), ImVec2(end.x, end.y - map.blend.bottom * size.y), blendColor);
    }

    const ImU32 lineColor = IM_COL32(255, 140, 30, 220);
    const int columns = map.warpColumns;
    const int rows = map.warpRows;
    std::vector<ImVec2> nodes(static_cast<size_t>(columns) * static_cast<size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const float s = static_cast<float>(c) / static_cast<float>(columns - 1);
            const float t = static_cast<float>(r) / static_cast<float>(rows - 1);
            nodes[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c)] = screen(mapPoint(map, s, t));
        }
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const ImVec2 here = nodes[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c)];
            if (c + 1 < columns) {
                draw->AddLine(here, nodes[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c + 1)], lineColor, 1.0f);
            }
            if (r + 1 < rows) {
                draw->AddLine(here, nodes[static_cast<size_t>((r + 1)) * static_cast<size_t>(columns) + static_cast<size_t>(c)], lineColor, 1.0f);
            }
        }
    }

    const auto isCorner = [&](int r, int c) { return (r == 0 || r == rows - 1) && (c == 0 || c == columns - 1); };
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hot = -1;
    int hotWarp = -1;
    float best = 14.0f * 14.0f;
    for (int k = 0; k < 4; ++k) {
        const ImVec2 p = screen(map.corners[static_cast<size_t>(k)]);
        const float d2 = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
        if (d2 < best) {
            best = d2;
            hot = k;
            hotWarp = -1;
        }
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            if (isCorner(r, c)) {
                continue;
            }
            const ImVec2 p = nodes[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c)];
            const float d2 = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
            if (d2 < best) {
                best = d2;
                hot = -1;
                hotWarp = r * columns + c;
            }
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        draggedPoint_ = hot;
        draggedWarp_ = hotWarp;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggedPoint_ = -1;
        draggedWarp_ = -1;
    }
    if (draggedPoint_ >= 0) {
        Vec2f p = toCanvas(mouse, origin, size);
        p.x = std::clamp(p.x, -0.5f, 1.5f);
        p.y = std::clamp(p.y, -0.5f, 1.5f);
        map.corners[static_cast<size_t>(draggedPoint_)] = p;
    } else if (draggedWarp_ >= 0 && draggedWarp_ < columns * rows) {
        const int r = draggedWarp_ / columns;
        const int c = draggedWarp_ % columns;
        OutputMapping flat = map;
        resetWarp(flat);
        const Vec2f base = mapPoint(flat, static_cast<float>(c) / static_cast<float>(columns - 1), static_cast<float>(r) / static_cast<float>(rows - 1));
        const Vec2f p = toCanvas(mouse, origin, size);
        map.warp[static_cast<size_t>(draggedWarp_)] = {std::clamp(p.x - base.x, -0.5f, 0.5f), std::clamp(p.y - base.y, -0.5f, 0.5f)};
    }

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            if (isCorner(r, c)) {
                continue;
            }
            const ImVec2 p = nodes[static_cast<size_t>(r) * static_cast<size_t>(columns) + static_cast<size_t>(c)];
            const bool active = draggedWarp_ == r * columns + c || (draggedWarp_ < 0 && draggedPoint_ < 0 && hotWarp == r * columns + c && hovered);
            draw->AddCircleFilled(p, active ? 6.0f : 4.5f, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 200, 120, 255));
            draw->AddCircle(p, active ? 6.0f : 4.5f, IM_COL32(0, 0, 0, 255));
        }
    }
    for (int k = 0; k < 4; ++k) {
        const ImVec2 p = screen(map.corners[static_cast<size_t>(k)]);
        const bool active = draggedPoint_ == k || (draggedPoint_ < 0 && draggedWarp_ < 0 && hot == k && hovered);
        const float half = active ? 8.0f : 6.0f;
        draw->AddRectFilled(ImVec2(p.x - half, p.y - half), ImVec2(p.x + half, p.y + half), active ? IM_COL32(255, 255, 255, 255) : ImGui::ColorConvertFloat4ToU32(ui::kAccent));
        draw->AddRect(ImVec2(p.x - half, p.y - half), ImVec2(p.x + half, p.y + half), IM_COL32(0, 0, 0, 255));
    }
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(90, 90, 100, 255));
}

void App::drawOutputMapping(int index) {
    OutputSpec& spec = outputs_.specs()[static_cast<size_t>(index)];
    OutputMapping& map = spec.map;

    ImGui::SeparatorText(tr("Picture area"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Which part of the composition this output shows. Use it to split one picture over several projectors."));
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton(tr("Whole picture"))) {
        map.slice = {0.0f, 0.0f, 1.0f, 1.0f};
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Left half"))) {
        map.slice = {0.0f, 0.0f, 0.5f, 1.0f};
        map.fill = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Right half"))) {
        map.slice = {0.5f, 0.0f, 0.5f, 1.0f};
        map.fill = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Top half"))) {
        map.slice = {0.0f, 0.0f, 1.0f, 0.5f};
        map.fill = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Bottom half"))) {
        map.slice = {0.0f, 0.5f, 1.0f, 0.5f};
        map.fill = true;
    }
    ImGui::PushID("slice");
    ImGui::TextUnformatted(tr("Left"));
    ImGui::SameLine(90.0f);
    percentSlider("##sx", map.slice[0], 0.0f, 0.98f);
    ImGui::TextUnformatted(tr("Top"));
    ImGui::SameLine(90.0f);
    percentSlider("##sy", map.slice[1], 0.0f, 0.98f);
    ImGui::TextUnformatted(tr("Width"));
    ImGui::SameLine(90.0f);
    percentSlider("##sw", map.slice[2], 0.02f, 1.0f);
    ImGui::TextUnformatted(tr("Height"));
    ImGui::SameLine(90.0f);
    percentSlider("##sh", map.slice[3], 0.02f, 1.0f);
    ImGui::PopID();
    ImGui::Checkbox(tr("Fill the whole window"), &map.fill);
    ImGui::SetItemTooltip("%s", tr("Off: the picture keeps its proportions and gets black bars. On: it is stretched over the whole window, which is right for projectors."));
    ImGui::SameLine();
    ImGui::Checkbox(tr("Mirror horizontally"), &map.flipX);
    ImGui::SameLine();
    ImGui::Checkbox(tr("Mirror vertically"), &map.flipY);

    ImGui::SeparatorText(tr("Corners and warp"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Drag the orange squares to fit the picture onto the surface. Drag the round points to bend it."));
    ImGui::PopTextWrapPos();
    drawMappingCanvas(spec, index);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo(tr("Warp points"), (std::to_string(map.warpColumns) + " x " + std::to_string(map.warpRows)).c_str())) {
        for (int n = kMinWarpPoints; n <= kMaxWarpPoints; ++n) {
            const std::string label = std::to_string(n) + " x " + std::to_string(n);
            if (ImGui::Selectable(label.c_str(), map.warpColumns == n && map.warpRows == n)) {
                resizeWarp(map, n, n);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Reset corners"))) {
        resetCorners(map);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Reset warp"))) {
        resetWarp(map);
    }

    ImGui::SeparatorText(tr("Edge blending"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Fades the edge where two projectors overlap, in percent of this output. Set the same overlap on both neighbours."));
    ImGui::PopTextWrapPos();
    ImGui::PushID("blend");
    ImGui::TextUnformatted(tr("Left"));
    ImGui::SameLine(90.0f);
    percentSlider("##bl", map.blend.left, 0.0f, 0.5f);
    ImGui::TextUnformatted(tr("Right"));
    ImGui::SameLine(90.0f);
    percentSlider("##br", map.blend.right, 0.0f, 0.5f);
    ImGui::TextUnformatted(tr("Top"));
    ImGui::SameLine(90.0f);
    percentSlider("##bt", map.blend.top, 0.0f, 0.5f);
    ImGui::TextUnformatted(tr("Bottom"));
    ImGui::SameLine(90.0f);
    percentSlider("##bb", map.blend.bottom, 0.0f, 0.5f);
    ImGui::TextUnformatted(tr("Curve"));
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##bp", &map.blend.power, 1.0f, 4.0f, "%.2f");
    ImGui::TextUnformatted(tr("Gamma"));
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##bg", &map.blend.gamma, 1.0f, 3.0f, "%.2f");
    ImGui::PopID();

    ImGui::SeparatorText(tr("Brightness"));
    percentSlider("##brightness", map.brightness, 0.0f, 1.0f);
    ImGui::SetItemTooltip("%s", tr("Lower it to match this projector to a darker neighbour"));
    if (ImGui::SmallButton(tr("Reset everything for this output"))) {
        const std::string name = spec.name;
        const bool enabled = spec.enabled;
        map = OutputMapping{};
        spec.name = name;
        spec.enabled = enabled;
    }
    normalizeMapping(map);
}

void App::drawOutputControls() {
    ImGui::SeparatorText(tr("Output windows"));
    if (ImGui::Button(outputs_.anyOpen() ? tr("Close all") : tr("Open all"))) {
        if (outputs_.anyOpen()) {
            outputs_.closeAll();
        } else {
            openOutputWindow(false);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!outputs_.anyOpen());
    if (ImGui::Button(outputs_.anyFullscreen() ? tr("Windowed") : tr("Fullscreen"))) {
        outputs_.toggleFullscreen(window_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("F11 fullscreen, Esc leaves it"));

    std::array<OutputSpec, kMaxOutputs>& specs = outputs_.specs();
    selectedOutput_ = std::clamp(selectedOutput_, 0, kMaxOutputs - 1);
    if (ImGui::BeginTabBar("outputTabs")) {
        for (int i = 0; i < kMaxOutputs; ++i) {
            const OutputSpec& spec = specs[static_cast<size_t>(i)];
            const bool used = spec.enabled || i == 0;
            std::string label = outputLabel(i);
            if (outputs_.isOpen(i)) {
                label += " *";
            } else if (!used) {
                label += " (-)";
            }
            label += "###out" + std::to_string(i);
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
            if (selectedOutput_ == i && outputTabForce_ > 0) {
                flags |= ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem(label.c_str(), nullptr, flags)) {
                selectedOutput_ = i;
                ImGui::EndTabItem();
            }
        }
        outputTabForce_ = std::max(0, outputTabForce_ - 1);
        ImGui::EndTabBar();
    }

    const int index = selectedOutput_;
    OutputSpec& spec = specs[static_cast<size_t>(index)];
    if (index > 0) {
        if (ImGui::Checkbox(tr("Use this output"), &spec.enabled) && !spec.enabled) {
            outputs_.close(index);
        }
    } else {
        spec.enabled = true;
    }
    const bool used = spec.enabled || index == 0;
    ImGui::BeginDisabled(!used);

    ImGui::BeginDisabled(!used);
    if (ImGui::Button(outputs_.isOpen(index) ? tr("Close") : tr("Open"))) {
        if (outputs_.isOpen(index)) {
            outputs_.close(index);
        } else if (!outputs_.open(index, window_)) {
            toast(tr("Could not open the output window"));
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!outputs_.isOpen(index));
    if (ImGui::Button(outputs_.isFullscreen(index) ? tr("Windowed") : tr("Fullscreen"))) {
        outputs_.setFullscreen(index, !outputs_.isFullscreen(index));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool pattern = outputs_.testPattern(index);
    if (ImGui::Checkbox(tr("Test pattern"), &pattern)) {
        outputs_.setTestPattern(index, pattern);
    }
    ImGui::SetItemTooltip("%s", tr("Shows a grid instead of the picture to line up projectors and see the corners, warp and blending"));
    ImGui::EndDisabled();

    const std::vector<std::string> monitors = OutputWindow::monitorNames();
    if (monitors.empty()) {
        ImGui::TextDisabled("%s", tr("No display found"));
    } else {
        spec.monitor = std::clamp(spec.monitor, 0, static_cast<int>(monitors.size()) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##display", monitors[static_cast<size_t>(spec.monitor)].c_str())) {
            for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                if (ImGui::Selectable(monitors[static_cast<size_t>(i)].c_str(), i == spec.monitor)) {
                    spec.monitor = i;
                    if (outputs_.isFullscreen(index)) {
                        outputs_.setFullscreen(index, true);
                    }
                }
            }
            ImGui::EndCombo();
        }
        for (int other = 0; other < kMaxOutputs; ++other) {
            const OutputSpec& o = specs[static_cast<size_t>(other)];
            if (other != index && (o.enabled || other == 0) && std::min(o.monitor, static_cast<int>(monitors.size()) - 1) == spec.monitor) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s %s", tr("Same display as"), outputLabel(other).c_str());
                break;
            }
        }
    }

    drawOutputMapping(index);
    ImGui::EndDisabled();

    ImGui::SeparatorText(tr("Several projectors"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Splits the picture over a grid of outputs with the right overlap and edge blending. It turns the outputs on and replaces their picture area and blending."));
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderInt(tr("Columns"), &gridColumns_, 1, kMaxOutputs);
    gridRows_ = std::clamp(gridRows_, 1, kMaxOutputs);
    gridColumns_ = std::clamp(gridColumns_, 1, kMaxOutputs);
    if (gridColumns_ * gridRows_ > kMaxOutputs) {
        gridRows_ = std::max(1, kMaxOutputs / gridColumns_);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderInt(tr("Rows"), &gridRows_, 1, kMaxOutputs);
    if (gridColumns_ * gridRows_ > kMaxOutputs) {
        gridColumns_ = std::max(1, kMaxOutputs / gridRows_);
    }
    ImGui::TextUnformatted(tr("Overlap"));
    ImGui::SameLine(90.0f);
    percentSlider("##gridOverlap", gridOverlap_, 0.0f, 0.4f);
    ImGui::SetItemTooltip("%s", tr("Overlap between neighbouring projectors, in percent of the whole picture"));
    if (ImGui::Button(tr("Arrange outputs"))) {
        std::vector<OutputMapping> maps;
        layoutGrid(gridColumns_, gridRows_, gridOverlap_, maps);
        for (size_t i = 0; i < maps.size() && i < specs.size(); ++i) {
            OutputMapping keep = specs[i].map;
            maps[i].corners = keep.corners;
            maps[i].warpColumns = keep.warpColumns;
            maps[i].warpRows = keep.warpRows;
            maps[i].warp = keep.warp;
            maps[i].brightness = keep.brightness;
            specs[i].map = maps[i];
            specs[i].enabled = true;
        }
        for (size_t i = maps.size(); i < specs.size(); ++i) {
            if (i > 0 && outputs_.isOpen(static_cast<int>(i))) {
                outputs_.close(static_cast<int>(i));
            }
            if (i > 0) {
                specs[i].enabled = false;
            }
        }
        toast(tr("Outputs arranged"));
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && settings_.outputs != savedOutputs_) {
        saveSettings(settings_);
        savedOutputs_ = settings_.outputs;
    }
}

}

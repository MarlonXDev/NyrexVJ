#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/mask_images.hpp"
#include "core/ndi_sender.hpp"
#include "core/output_map.hpp"
#include "core/venue_setup.hpp"
#include "platform/platform.hpp"
#include "render/output_window.hpp"
#include "scene3d/scene_pack.hpp"

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
    const bool changed = ui::sliderFloat(label, &shown, low * 100.0f, high * 100.0f, "%.1f %%");
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

float distanceToSegmentSquared(const Vec2f& p, const Vec2f& a, const Vec2f& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    float t = lengthSquared > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = a.x + t * dx - p.x;
    const float ey = a.y + t * dy - p.y;
    return ex * ex + ey * ey;
}

void colorControls(OutputColor& color, const char* id) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(tr("Red"));
    ImGui::SameLine(110.0f);
    percentSlider("##cr", color.red, 0.0f, 1.5f);
    ImGui::TextUnformatted(tr("Green"));
    ImGui::SameLine(110.0f);
    percentSlider("##cg", color.green, 0.0f, 1.5f);
    ImGui::TextUnformatted(tr("Blue"));
    ImGui::SameLine(110.0f);
    percentSlider("##cb", color.blue, 0.0f, 1.5f);
    ImGui::TextUnformatted(tr("Saturation"));
    ImGui::SameLine(110.0f);
    percentSlider("##cs", color.saturation, 0.0f, 2.0f);
    ImGui::TextUnformatted(tr("Contrast"));
    ImGui::SameLine(110.0f);
    percentSlider("##cc", color.contrast, 0.25f, 2.0f);
    ImGui::TextUnformatted(tr("Gamma"));
    ImGui::SameLine(110.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::sliderFloat("##cgm", &color.gamma, 0.4f, 2.5f, "%.2f");
    ImGui::TextUnformatted(tr("Black lift"));
    ImGui::SameLine(110.0f);
    percentSlider("##ck", color.black, 0.0f, 0.3f);
    ImGui::SetItemTooltip("%s", tr("Raises black to match a projector that cannot show a deep black"));
    ImGui::PopID();
    ImGui::BeginDisabled(isDefaultColor(color));
    if (ImGui::SmallButton(tr("Reset colour"))) {
        color = OutputColor{};
    }
    ImGui::EndDisabled();
    normalizeColor(color);
}

std::string megabytes(size_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

bool sliceActive(const OutputSpec& spec, int index) {
    return index == 0 || spec.slices[static_cast<size_t>(index - 1)].enabled;
}

}

void App::drawMappingCanvas(OutputSpec& spec, int index) {
    (void)index;
    selectedSlice_ = std::clamp(selectedSlice_, 0, sliceCount(spec) - 1);
    OutputMapping& map = sliceMapping(spec, selectedSlice_);
    const bool maskMode = canvasMode_ == 1 && !maskIsImage(map.mask);
    const bool freehand = canvasMode_ == 2;
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

    for (int k = 0; k < sliceCount(spec); ++k) {
        if (k == selectedSlice_ || !sliceActive(spec, k)) {
            continue;
        }
        const OutputMapping& other = sliceMapping(spec, k);
        const ImVec2 outline[4] = {screen(mapPoint(other, 0.0f, 0.0f)), screen(mapPoint(other, 1.0f, 0.0f)), screen(mapPoint(other, 1.0f, 1.0f)), screen(mapPoint(other, 0.0f, 1.0f))};
        draw->AddPolyline(outline, 4, IM_COL32(120, 160, 255, 170), ImDrawFlags_Closed, 1.0f);
        const std::string otherName = sliceName(spec, k);
        draw->AddText(ImVec2(outline[0].x + 4.0f, outline[0].y + 2.0f), IM_COL32(150, 185, 255, 220), otherName.c_str());
    }

    const SliceMask& shown = map.mask;
    if (maskIsImage(shown)) {
        const MaskImages& images = MaskImages::instance();
        std::string state = shown.image;
        if (!images.error(shown.image).empty()) {
            state += " - " + trMessage(images.error(shown.image));
        } else if (images.loading(shown.image)) {
            state += " - " + std::string(tr("loading"));
        }
        draw->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), shown.enabled ? IM_COL32(90, 255, 140, 235) : IM_COL32(150, 150, 150, 200), state.c_str());
    } else if (!shown.points.empty()) {
        const std::vector<Vec2f> curve = maskOutline(shown);
        std::vector<ImVec2> outline;
        outline.reserve(curve.size());
        for (const Vec2f& point : curve) {
            outline.push_back(screen(point));
        }
        if (maskUsable(shown) && outline.size() >= 3) {
            draw->AddConcavePolyFilled(outline.data(), static_cast<int>(outline.size()), shown.invert ? IM_COL32(255, 70, 70, 55) : IM_COL32(70, 255, 120, 45));
        }
        draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), shown.enabled ? IM_COL32(90, 255, 140, 235) : IM_COL32(150, 150, 150, 200), ImDrawFlags_Closed, 1.5f);
    }
    if (freehand && maskStroke_.size() >= 2) {
        std::vector<ImVec2> stroke;
        stroke.reserve(maskStroke_.size());
        for (const Vec2f& point : maskStroke_) {
            stroke.push_back(screen(point));
        }
        draw->AddPolyline(stroke.data(), static_cast<int>(stroke.size()), IM_COL32(255, 230, 80, 255), ImDrawFlags_None, 2.0f);
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
    int hotMask = -1;
    int hotHandle = -1;
    float best = 14.0f * 14.0f;
    const bool curved = maskMode && maskHasCurves(map.mask) && map.mask.handles.size() == map.mask.points.size();
    if (maskMode) {
        for (size_t k = 0; k < map.mask.points.size(); ++k) {
            const ImVec2 p = screen(map.mask.points[k]);
            const float d2 = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
            if (d2 < best) {
                best = d2;
                hotMask = static_cast<int>(k);
                hotHandle = -1;
            }
            if (curved) {
                for (int side = 0; side < 2; ++side) {
                    const Vec2f offset = side == 0 ? map.mask.handles[k].in : map.mask.handles[k].out;
                    const ImVec2 h = screen({map.mask.points[k].x + offset.x, map.mask.points[k].y + offset.y});
                    const float hd2 = (h.x - mouse.x) * (h.x - mouse.x) + (h.y - mouse.y) * (h.y - mouse.y);
                    if (hd2 < best) {
                        best = hd2;
                        hotMask = -1;
                        hotHandle = static_cast<int>(k) * 2 + side;
                    }
                }
            }
        }
    } else if (!freehand) {
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
    }

    if (freehand) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            maskStroke_.clear();
            maskStroke_.push_back(toCanvas(mouse, origin, size));
        } else if (!maskStroke_.empty() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Vec2f p = toCanvas(mouse, origin, size);
            p.x = std::clamp(p.x, -0.5f, 1.5f);
            p.y = std::clamp(p.y, -0.5f, 1.5f);
            const Vec2f& last = maskStroke_.back();
            if ((p.x - last.x) * (p.x - last.x) + (p.y - last.y) * (p.y - last.y) > 0.0025f * 0.0025f && maskStroke_.size() < 4096) {
                maskStroke_.push_back(p);
            }
        } else if (!maskStroke_.empty()) {
            if (maskFromStroke(map.mask, maskStroke_)) {
                canvasMode_ = 1;
            }
            maskStroke_.clear();
        }
    } else if (!maskStroke_.empty()) {
        maskStroke_.clear();
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        draggedPoint_ = hot;
        draggedWarp_ = hotWarp;
        draggedMask_ = hotMask;
        draggedHandle_ = hotHandle;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggedPoint_ = -1;
        draggedWarp_ = -1;
        draggedMask_ = -1;
        draggedHandle_ = -1;
    }
    if (maskMode && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hotMask >= 0) {
        maskRemovePoint(map.mask, hotMask);
        draggedMask_ = -1;
        hotMask = -1;
    }
    if (maskMode && hovered && hotMask < 0 && draggedMask_ < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && map.mask.points.size() < static_cast<size_t>(kMaxMaskPoints)) {
        Vec2f p = toCanvas(mouse, origin, size);
        p.x = std::clamp(p.x, -0.5f, 1.5f);
        p.y = std::clamp(p.y, -0.5f, 1.5f);
        std::vector<Vec2f>& points = map.mask.points;
        size_t insertAt = points.size();
        if (points.size() >= 2) {
            float nearest = FLT_MAX;
            for (size_t k = 0; k < points.size(); ++k) {
                const float d = distanceToSegmentSquared(p, points[k], points[(k + 1) % points.size()]);
                if (d < nearest) {
                    nearest = d;
                    insertAt = k + 1;
                }
            }
        }
        points.insert(points.begin() + static_cast<std::ptrdiff_t>(insertAt), p);
        if (!map.mask.handles.empty()) {
            map.mask.handles.insert(map.mask.handles.begin() + static_cast<std::ptrdiff_t>(insertAt), MaskHandle{});
        }
        if (points.size() >= static_cast<size_t>(kMinMaskPoints)) {
            map.mask.enabled = true;
        }
    }
    if (draggedHandle_ >= 0 && map.mask.handles.size() == map.mask.points.size() && draggedHandle_ / 2 < static_cast<int>(map.mask.points.size())) {
        const size_t k = static_cast<size_t>(draggedHandle_ / 2);
        const Vec2f p = toCanvas(mouse, origin, size);
        const Vec2f offset = {std::clamp(p.x - map.mask.points[k].x, -0.5f, 0.5f), std::clamp(p.y - map.mask.points[k].y, -0.5f, 0.5f)};
        const bool outHandle = (draggedHandle_ % 2) == 1;
        (outHandle ? map.mask.handles[k].out : map.mask.handles[k].in) = offset;
        if (!ImGui::GetIO().KeyAlt) {
            (outHandle ? map.mask.handles[k].in : map.mask.handles[k].out) = {-offset.x, -offset.y};
        }
    } else if (draggedMask_ >= 0 && draggedMask_ < static_cast<int>(map.mask.points.size())) {
        Vec2f p = toCanvas(mouse, origin, size);
        p.x = std::clamp(p.x, -0.5f, 1.5f);
        p.y = std::clamp(p.y, -0.5f, 1.5f);
        map.mask.points[static_cast<size_t>(draggedMask_)] = p;
    } else if (draggedPoint_ >= 0) {
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

    if (maskMode) {
        for (size_t k = 0; k < map.mask.points.size(); ++k) {
            const ImVec2 p = screen(map.mask.points[k]);
            const bool active = draggedMask_ == static_cast<int>(k) || (draggedMask_ < 0 && hotMask == static_cast<int>(k) && hovered);
            const float half = active ? 6.5f : 5.0f;
            draw->AddCircleFilled(p, half, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(90, 255, 140, 255));
            draw->AddCircle(p, half, IM_COL32(0, 0, 0, 255));
            if (curved) {
                for (int side = 0; side < 2; ++side) {
                    const Vec2f offset = side == 0 ? map.mask.handles[k].in : map.mask.handles[k].out;
                    const ImVec2 h = screen({map.mask.points[k].x + offset.x, map.mask.points[k].y + offset.y});
                    const bool handleActive = draggedHandle_ == static_cast<int>(k) * 2 + side || (draggedHandle_ < 0 && draggedMask_ < 0 && hotHandle == static_cast<int>(k) * 2 + side && hovered);
                    draw->AddLine(p, h, IM_COL32(120, 200, 255, 150), 1.0f);
                    draw->AddRectFilled(ImVec2(h.x - 3.5f, h.y - 3.5f), ImVec2(h.x + 3.5f, h.y + 3.5f), handleActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 200, 255, 255));
                    draw->AddRect(ImVec2(h.x - 3.5f, h.y - 3.5f), ImVec2(h.x + 3.5f, h.y + 3.5f), IM_COL32(0, 0, 0, 255));
                }
            }
        }
    } else if (!freehand) {
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
    }
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(90, 90, 100, 255));
}

void App::drawOutputSlices(OutputSpec& spec) {
    ImGui::SeparatorText(tr("Slices"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("A slice shows a part of the picture in a part of this output. Add slices to feed several surfaces from one output. Later slices are drawn on top of earlier ones."));
    ImGui::PopTextWrapPos();
    selectedSlice_ = std::clamp(selectedSlice_, 0, sliceCount(spec) - 1);
    const int count = sliceCount(spec);
    const float available = ImGui::GetContentRegionAvail().x;
    float used = 0.0f;
    for (int i = 0; i < count; ++i) {
        const std::string label = sliceName(spec, i);
        const float width = ImGui::CalcTextSize(label.c_str()).x + 18.0f;
        if (i > 0 && used + width + ImGui::GetStyle().ItemSpacing.x <= available) {
            ImGui::SameLine();
            used += width + ImGui::GetStyle().ItemSpacing.x;
        } else {
            used = width;
        }
        const bool hidden = !sliceActive(spec, i);
        ImGui::PushID(i);
        if (hidden) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        }
        if (ImGui::Selectable(label.c_str(), selectedSlice_ == i, 0, ImVec2(width, 0.0f))) {
            selectedSlice_ = i;
            draggedPoint_ = -1;
            draggedWarp_ = -1;
            draggedMask_ = -1;
            draggedHandle_ = -1;
            maskStroke_.clear();
        }
        if (hidden) {
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(count >= kMaxSlices);
    if (ImGui::SmallButton(tr("Add slice")) && addSlice(spec)) {
        selectedSlice_ = sliceCount(spec) - 1;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Duplicate")) && duplicateSlice(spec, selectedSlice_)) {
        selectedSlice_ = sliceCount(spec) - 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(count <= 1);
    if (ImGui::SmallButton(tr("Remove")) && removeSlice(spec, selectedSlice_)) {
        selectedSlice_ = std::clamp(selectedSlice_, 0, sliceCount(spec) - 1);
    }
    ImGui::EndDisabled();
    if (selectedSlice_ > 0) {
        OutputSlice& slice = spec.slices[static_cast<size_t>(selectedSlice_ - 1)];
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%s", slice.name.c_str());
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::InputText("##slicename", buffer, sizeof(buffer))) {
            slice.name = buffer;
        }
        ImGui::SameLine();
        ImGui::Checkbox(tr("Show this slice"), &slice.enabled);
    }
}

void App::drawMaskImage(SliceMask& mask) {
    ImGui::TextUnformatted(tr("Mask from image"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Uses a picture as the mask: bright or opaque parts show the slice, dark or transparent parts hide it. The picture is stretched over the whole output. PNG, JPG, BMP and TGA up to 16 MiB."));
    ImGui::PopTextWrapPos();
    MaskImages& images = MaskImages::instance();
    if (maskIsImage(mask)) {
        ImGui::TextUnformatted(mask.image.c_str());
        const std::string failure = images.error(mask.image);
        if (!failure.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", trMessage(failure).c_str());
        } else if (images.loading(mask.image)) {
            ImGui::TextDisabled("%s", tr("loading"));
        } else if (images.get(mask.image)) {
            ImGui::TextDisabled("%s", tr("Mask image loaded"));
        }
        if (ImGui::SmallButton(tr("Reload image"))) {
            images.reload(mask.image);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Remove image"))) {
            mask.image.clear();
            mask.enabled = mask.points.size() >= static_cast<size_t>(kMinMaskPoints) && mask.enabled;
        }
    }
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputTextWithHint("##maskImagePath", tr("Image file path"), maskImagePath_, sizeof(maskImagePath_));
    ImGui::SetItemTooltip("%s", tr("Full path of a picture on this computer. It is copied into the mask folder of NyrexVJ, so the original can be moved afterwards."));
    ImGui::SameLine();
    ImGui::BeginDisabled(maskImagePath_[0] == '\0');
    if (ImGui::Button(tr("Use image"))) {
        std::string name;
        std::string error;
        if (images.importFile(maskImagePath_, name, error)) {
            mask.image = name;
            mask.enabled = true;
            maskImagePath_[0] = '\0';
            canvasMode_ = 1;
            toast(std::string(tr("Mask image imported: ")) + name);
        } else {
            toast(trMessage(error));
        }
    }
    ImGui::EndDisabled();
}

void App::drawSliceMask(OutputMapping& map) {
    ImGui::SeparatorText(tr("Mask"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Cuts the slice to a shape, for example around a screen or a pillar. Choose Mask above the canvas, then drag the green points, double-click to add a point and right-click a point to remove it. Choose Freehand to draw the shape with the mouse."));
    ImGui::PopTextWrapPos();
    SliceMask& mask = map.mask;
    const auto preset = [&](const SliceMask& shape) {
        const float feather = mask.feather;
        const bool invert = mask.invert;
        mask = shape;
        mask.feather = feather;
        mask.invert = invert;
        mask.enabled = true;
        canvasMode_ = 1;
    };
    bool enabled = mask.enabled;
    if (ImGui::Checkbox(tr("Use mask"), &enabled)) {
        if (enabled && !maskIsImage(mask) && mask.points.size() < static_cast<size_t>(kMinMaskPoints)) {
            preset(maskRectangle(0.1f, 0.1f, 0.9f, 0.9f));
        }
        mask.enabled = enabled;
        if (enabled) {
            canvasMode_ = 1;
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("Invert"), &mask.invert);
    ImGui::SetItemTooltip("%s", tr("Hides the inside of the shape and shows the rest"));
    if (ImGui::SmallButton(tr("Rectangle"))) {
        preset(maskRectangle(0.1f, 0.1f, 0.9f, 0.9f));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Ellipse"))) {
        preset(maskEllipse(0.5f, 0.5f, 0.45f, 0.45f, 32));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Freehand"))) {
        mask.image.clear();
        canvasMode_ = 2;
    }
    ImGui::SetItemTooltip("%s", tr("Draw the shape with the mouse on the canvas. It is simplified to at most 32 points that you can edit afterwards."));
    ImGui::SameLine();
    ImGui::BeginDisabled(maskIsImage(mask) || mask.points.size() >= static_cast<size_t>(kMaxMaskPoints));
    if (ImGui::SmallButton(tr("Add point"))) {
        maskInsertPoint(mask);
        canvasMode_ = 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Clear"))) {
        mask.points.clear();
        mask.handles.clear();
        mask.image.clear();
        mask.enabled = false;
    }
    if (!maskIsImage(mask)) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d / %d", static_cast<int>(mask.points.size()), kMaxMaskPoints);
        ImGui::BeginDisabled(mask.points.size() < static_cast<size_t>(kMinMaskPoints));
        if (ImGui::SmallButton(tr("Smooth edges"))) {
            maskSmooth(mask, 1.0f);
            canvasMode_ = 1;
        }
        ImGui::SetItemTooltip("%s", tr("Turns the corners into curves. Drag the blue squares to bend each edge. Hold Alt to move one handle on its own."));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!maskHasCurves(mask));
        if (ImGui::SmallButton(tr("Straight edges"))) {
            maskClearCurves(mask);
        }
        ImGui::EndDisabled();
    }
    ImGui::TextUnformatted(tr("Soft edge"));
    ImGui::SameLine(90.0f);
    percentSlider("##maskFeather", mask.feather, 0.0f, kMaxMaskFeather);
    ImGui::SetItemTooltip("%s", tr("Softens the edge of the mask, in percent of the picture height"));
    drawMaskImage(mask);
    normalizeMask(mask);
}

void App::drawOutputColor(OutputSpec& spec) {
    ImGui::SeparatorText(tr("Colour of this output"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Matches the colour of this projector to its neighbours. It applies to all slices of this output but not to the test pattern."));
    ImGui::PopTextWrapPos();
    colorControls(spec.color, "outputColor");
}

void App::drawSliceColor(OutputMapping& map) {
    ImGui::SeparatorText(tr("Colour of this slice"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Corrects the colour of this slice only, for example when two surfaces on one output look different. It is applied first, then the colour of the output. The test pattern is not changed."));
    ImGui::PopTextWrapPos();
    colorControls(map.sliceColor, "sliceColor");
}

void App::drawOutputBudget() {
    const std::array<OutputSpec, kMaxOutputs>& specs = outputs_.specs();
    const OutputBudget planned = estimateOutputBudget(specs, engine_.width(), engine_.height(), 1920, 1080);
    const size_t used = outputs_.gpuBytes();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Output memory"));
    ImGui::SameLine();
    const std::string line = megabytes(used) + " / " + megabytes(outputs_.budgetBytes());
    ImGui::TextUnformatted(line.c_str());
    ImGui::SetItemTooltip("%s", tr("Graphics memory used by the output windows, masks, delay and NDI readback. NDI readback is not started when it would go over the limit."));
    if (planned.total() > outputs_.budgetBytes()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s %s", tr("The current setup needs about"), megabytes(planned.total()).c_str());
    }
}

void App::drawOutputNdi(OutputSpec& spec, int index) {
    ImGui::SeparatorText(tr("NDI output"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Sends this output exactly as the window shows it, with slices, warp, masks, blending and colour, as its own NDI source. It only reads the picture back while a receiver is connected."));
    ImGui::PopTextWrapPos();
    ImGui::Checkbox(tr("Send this output as NDI"), &spec.ndi);
    if (ndiOutputNameFor_ != index) {
        std::snprintf(ndiOutputName_, sizeof(ndiOutputName_), "%s", spec.ndiName.c_str());
        ndiOutputNameFor_ = index;
    }
    ImGui::BeginDisabled(!spec.ndi);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##ndiOutputName", defaultNdiName(index).c_str(), ndiOutputName_, sizeof(ndiOutputName_))) {
        spec.ndiName = ndiOutputName_;
    }
    ImGui::SetItemTooltip("%s", tr("Name of the NDI source. Empty uses the default name. A changed name restarts the sender."));
    ImGui::EndDisabled();
    if (spec.ndi) {
        const OutputNdiStatus& status = outputs_.ndiStatus(index);
        if (!outputs_.isOpen(index)) {
            ImGui::TextDisabled("%s", tr("Open this output to start sending."));
        } else if (!status.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", trMessage(status.error).c_str());
        } else if (status.tooLarge) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("The window is larger than 4096 pixels, so no NDI picture is sent."));
        } else if (status.overBudget) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("Not enough graphics memory budget for the NDI readback."));
        } else if (status.active && status.connections > 0) {
            ImGui::TextDisabled("%s: %d", tr("Receivers connected"), status.connections);
        } else if (status.active) {
            ImGui::TextDisabled("%s", tr("Waiting for a receiver."));
        } else {
            ImGui::TextDisabled("%s", tr("Starting NDI..."));
        }
    }
}

void App::drawOutputMapping(int index) {
    OutputSpec& spec = outputs_.specs()[static_cast<size_t>(index)];
    drawOutputSlices(spec);
    OutputMapping& map = sliceMapping(spec, selectedSlice_);

    ImGui::SeparatorText(tr("Picture area"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Which part of the composition this slice shows. Use it to split one picture over several projectors."));
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton(tr("Whole picture"))) {
        map.slice = {0.0f, 0.0f, 1.0f, 1.0f};
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Left half"))) {
        map.slice = {0.0f, 0.0f, 0.5f, 1.0f};
        if (selectedSlice_ == 0) {
            map.fill = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Right half"))) {
        map.slice = {0.5f, 0.0f, 0.5f, 1.0f};
        if (selectedSlice_ == 0) {
            map.fill = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Top half"))) {
        map.slice = {0.0f, 0.0f, 1.0f, 0.5f};
        if (selectedSlice_ == 0) {
            map.fill = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Bottom half"))) {
        map.slice = {0.0f, 0.5f, 1.0f, 0.5f};
        if (selectedSlice_ == 0) {
            map.fill = true;
        }
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
    if (selectedSlice_ == 0) {
        ImGui::Checkbox(tr("Fill the whole window"), &map.fill);
        ImGui::SetItemTooltip("%s", tr("Off: the picture keeps its proportions and gets black bars. On: it is stretched over the whole window, which is right for projectors."));
        ImGui::SameLine();
    }
    ImGui::Checkbox(tr("Mirror horizontally"), &map.flipX);
    ImGui::SameLine();
    ImGui::Checkbox(tr("Mirror vertically"), &map.flipY);

    drawSpatialMapping(spec, index, map);

    ImGui::SeparatorText(tr("Corners and warp"));
    ImGui::PushTextWrapPos(0.0f);
    if (map.spatial.enabled) {
        ImGui::TextDisabled("%s", tr("Not used while 3D mapping is on for this slice. The mask below is still drawn on the output."));
        if (canvasMode_ == 0) {
            canvasMode_ = 1;
        }
    } else {
        ImGui::TextDisabled("%s", tr("Drag the orange squares to fit the picture onto the surface. Drag the round points to bend it."));
    }
    ImGui::PopTextWrapPos();
    if (!map.spatial.enabled) {
        ImGui::RadioButton(tr("Corners and warp"), &canvasMode_, 0);
        ImGui::SameLine();
    }
    ImGui::RadioButton(tr("Mask"), &canvasMode_, 1);
    ImGui::SameLine();
    ImGui::RadioButton(tr("Freehand"), &canvasMode_, 2);
    drawMappingCanvas(spec, index);
    if (!map.spatial.enabled) {
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
    }

    drawSliceMask(map);
    drawSliceColor(map);

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
    ui::sliderFloat("##bp", &map.blend.power, 1.0f, 4.0f, "%.2f");
    ImGui::TextUnformatted(tr("Gamma"));
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::sliderFloat("##bg", &map.blend.gamma, 1.0f, 3.0f, "%.2f");
    ImGui::TextUnformatted(tr("Black level"));
    ImGui::SameLine(90.0f);
    percentSlider("##bk", map.blend.blackLevel, 0.0f, 0.3f);
    ImGui::SetItemTooltip("%s", tr("Projectors never show a true black, so the overlap looks brighter. This lifts the rest of the slice to the same level. It only works together with edge blending."));
    ImGui::PopID();

    ImGui::SeparatorText(tr("Brightness"));
    percentSlider("##brightness", map.brightness, 0.0f, 1.0f);
    ImGui::SetItemTooltip("%s", tr("Lower it to match this projector to a darker neighbour"));
    normalizeMapping(map);

    drawOutputColor(spec);

    if (ImGui::SmallButton(tr("Reset everything for this output"))) {
        spec.map = OutputMapping{};
        spec.slices.clear();
        spec.color = OutputColor{};
        selectedSlice_ = 0;
        draggedPoint_ = -1;
        draggedWarp_ = -1;
        draggedMask_ = -1;
        draggedHandle_ = -1;
        maskStroke_.clear();
    }
}

void App::applyVenue(const std::array<OutputSpec, kMaxOutputs>& set) {
    std::array<OutputSpec, kMaxOutputs>& specs = outputs_.specs();
    for (int i = 0; i < kMaxOutputs; ++i) {
        const bool wasOpen = outputs_.isOpen(i);
        specs[static_cast<size_t>(i)] = set[static_cast<size_t>(i)];
        if (i == 0) {
            specs[0].enabled = true;
        }
        if (wasOpen) {
            if (!specs[static_cast<size_t>(i)].enabled) {
                outputs_.close(i);
            } else {
                outputs_.setFullscreen(i, specs[static_cast<size_t>(i)].fullscreen);
            }
        }
    }
    selectedSlice_ = 0;
    draggedPoint_ = -1;
    draggedWarp_ = -1;
    draggedMask_ = -1;
    draggedHandle_ = -1;
    maskStroke_.clear();
    ndiOutputNameFor_ = -1;
}

bool App::importDroppedVenue(const std::string& path) {
    std::string imported;
    std::string error;
    if (!importVenue(venuesDirectory(), pathFromUtf8(path), imported, error)) {
        toast(trMessage(error));
        return false;
    }
    venueListDirty_ = true;
    toast(std::string(tr("Venue setup imported: ")) + imported);
    return true;
}

void App::drawVenueControls() {
    ImGui::SeparatorText(tr("Venue setups"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Saves the whole layout of all outputs (displays, slices, masks, warp, blending and colour) under a name so you can switch between venues. You can also drop a venue file onto the window to import it."));
    ImGui::PopTextWrapPos();
    const std::filesystem::path directory = venuesDirectory();
    if (venueListDirty_) {
        venueList_ = listVenues(directory);
        venueListDirty_ = false;
    }
    if (selectedVenue_ >= static_cast<int>(venueList_.size())) {
        selectedVenue_ = -1;
    }
    const std::string preview = selectedVenue_ >= 0 ? venueList_[static_cast<size_t>(selectedVenue_)] : std::string(tr("(none saved)"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##venues", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(venueList_.size()); ++i) {
            if (ImGui::Selectable(venueList_[static_cast<size_t>(i)].c_str(), i == selectedVenue_)) {
                selectedVenue_ = i;
                std::snprintf(venueName_, sizeof(venueName_), "%s", venueList_[static_cast<size_t>(i)].c_str());
            }
        }
        ImGui::EndCombo();
    }
    const std::string chosen = selectedVenue_ >= 0 ? venueList_[static_cast<size_t>(selectedVenue_)] : std::string();
    ImGui::BeginDisabled(selectedVenue_ < 0);
    if (ImGui::Button(tr("Load"))) {
        std::array<OutputSpec, kMaxOutputs> set;
        std::string error;
        if (loadVenue(directory, chosen, set, error)) {
            applyVenue(set);
            toast(std::string(tr("Venue setup loaded: ")) + chosen);
        } else {
            toast(trMessage(error));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Duplicate"))) {
        std::string error;
        if (duplicateVenue(directory, chosen, std::string(), error)) {
            venueListDirty_ = true;
            toast(std::string(tr("Venue setup duplicated: ")) + chosen);
        } else {
            toast(trMessage(error));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Delete"))) {
        ImGui::OpenPopup("deleteVenue");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Open folder"))) {
        platform::openFolder(pathToUtf8(directory));
    }
    if (ImGui::BeginPopupModal("deleteVenue", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", tr("Delete this venue setup?"));
        ImGui::TextDisabled("%s", chosen.c_str());
        if (ImGui::Button(tr("Delete"))) {
            std::string error;
            if (deleteVenue(directory, chosen, error)) {
                selectedVenue_ = -1;
                venueListDirty_ = true;
                toast(std::string(tr("Venue setup deleted: ")) + chosen);
            } else {
                toast(trMessage(error));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##venueName", tr("Name"), venueName_, sizeof(venueName_));
    ImGui::SameLine();
    const bool exists = venueExists(directory, venueName_);
    if (ImGui::Button(exists ? tr("Overwrite") : tr("Save as"))) {
        std::string error;
        if (saveVenue(directory, venueName_, outputs_.specs(), error)) {
            venueListDirty_ = true;
            toast(std::string(tr("Venue setup saved: ")) + sanitizeVenueName(venueName_));
            venueList_ = listVenues(directory);
            venueListDirty_ = false;
            const std::string saved = sanitizeVenueName(venueName_);
            selectedVenue_ = -1;
            for (size_t i = 0; i < venueList_.size(); ++i) {
                if (venueList_[i] == saved) {
                    selectedVenue_ = static_cast<int>(i);
                }
            }
        } else {
            toast(trMessage(error));
        }
    }

    ImGui::SetNextItemWidth(-130.0f);
    ImGui::InputTextWithHint("##venuePath", tr("File path"), venuePath_, sizeof(venuePath_));
    ImGui::SetItemTooltip("%s", tr("Where to export the selected venue setup to, or which venue file to import"));
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedVenue_ < 0 || venuePath_[0] == '\0');
    if (ImGui::Button(tr("Export"))) {
        std::string error;
        if (exportVenue(directory, chosen, pathFromUtf8(venuePath_), error)) {
            toast(tr("Venue setup exported"));
        } else {
            toast(trMessage(error));
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(venuePath_[0] == '\0');
    if (ImGui::Button(tr("Import"))) {
        importDroppedVenue(venuePath_);
    }
    ImGui::EndDisabled();
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

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Delay"));
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(160.0f);
    ui::sliderInt("##delayFrames", &spec.delayFrames, 0, kMaxDelayFrames, "%d");
    spec.delayFrames = clampDelay(spec.delayFrames);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("frames"));
    ImGui::SetItemTooltip("%s", tr("Delays this output by whole frames to match outputs that show their picture later, for example projectors with more processing delay. While a delay is set, extra graphics memory is used."));
    if (outputs_.historyActive()) {
        char memory[64];
        std::snprintf(memory, sizeof(memory), "%.1f MiB", static_cast<double>(outputs_.historyBytes()) / (1024.0 * 1024.0));
        ImGui::TextDisabled("%s: %s", tr("Delay memory"), memory);
    }

    drawOutputNdi(spec, index);
    drawOutputBudget();

    drawOutputMapping(index);
    ImGui::EndDisabled();

    ImGui::SeparatorText(tr("Frame lock"));
    if (ImGui::Checkbox(trId("Show the same frame on all outputs", "frameLock"), &settings_.outputFrameLock)) {
        saveSettings(settings_);
    }
    ImGui::SetItemTooltip("%s", tr("Draws all output windows first and then swaps them one right after the other, so they change their picture at nearly the same time. Turn this off to swap each window right after drawing it."));
    bool bars = outputs_.syncBars();
    if (ImGui::Checkbox(trId("Sync bars", "syncBars"), &bars)) {
        outputs_.setSyncBars(bars);
    }
    ImGui::SetItemTooltip("%s", tr("Shows a frame counter, a flash and a moving bar on every open output instead of the picture. Film several screens at once to see how far apart they are. The counter includes the delay of each output."));
    const SkewTracker& skew = outputs_.skew();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Swap skew (last / average / longest)"));
    ImGui::SameLine();
    if (outputs_.openCount() < 2) {
        ImGui::TextDisabled("%s", tr("Open two or more outputs to measure it."));
    } else if (skew.count() == 0) {
        ImGui::TextUnformatted("-");
    } else {
        char values[96];
        std::snprintf(values, sizeof(values), "%.2f / %.2f / %.2f ms", skew.last(), skew.average(), skew.maximum());
        ImGui::TextUnformatted(values);
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Measured when the swap is handed to the graphics driver. Delay inside projectors and screens is not included."));
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText(tr("Several projectors"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Splits the picture over a grid of outputs with the right overlap and edge blending. It turns the outputs on and replaces their picture area and blending."));
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(80.0f);
    ui::sliderInt(tr("Columns"), &gridColumns_, 1, kMaxOutputs);
    gridRows_ = std::clamp(gridRows_, 1, kMaxOutputs);
    gridColumns_ = std::clamp(gridColumns_, 1, kMaxOutputs);
    if (gridColumns_ * gridRows_ > kMaxOutputs) {
        gridRows_ = std::max(1, kMaxOutputs / gridColumns_);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ui::sliderInt(tr("Rows"), &gridRows_, 1, kMaxOutputs);
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
            maps[i].mask = keep.mask;
            maps[i].blend.blackLevel = keep.blend.blackLevel;
            maps[i].corners = keep.corners;
            maps[i].warpColumns = keep.warpColumns;
            maps[i].warpRows = keep.warpRows;
            maps[i].warp = keep.warp;
            maps[i].brightness = keep.brightness;
            maps[i].sliceColor = keep.sliceColor;
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

    drawVenueControls();

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && settings_.outputs != savedOutputs_) {
        saveSettings(settings_);
        savedOutputs_ = settings_.outputs;
    }
}

}

#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/spatial_calibration.hpp"
#include "core/spatial_mesh.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kPixelWidth = 1920.0f;

float labelColumn() {
    return ImGui::GetFontSize() * 11.0f;
}

std::string surfaceLabel(const SpatialSurface& surface, int index) {
    if (!surface.name.empty()) {
        return surface.name;
    }
    return std::string(tr("Surface")) + " " + std::to_string(index + 1);
}

std::string pointLabel(const CalibrationPoint& point, int index) {
    if (!point.name.empty()) {
        return point.name;
    }
    return std::string(tr("Point")) + " " + std::to_string(index + 1);
}

bool surfacesCentre(const SpatialMapping& spatial, Vec3f& centre) {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int count = 0;
    for (const SpatialSurface& surface : spatial.surfaces) {
        if (!surfaceUsable(surface)) {
            continue;
        }
        const Vec3f middle = surfacePoint(surface, 0.5f, 0.5f);
        x += static_cast<double>(middle.x);
        y += static_cast<double>(middle.y);
        z += static_cast<double>(middle.z);
        ++count;
    }
    if (count == 0) {
        return false;
    }
    centre = Vec3f{static_cast<float>(x / count), static_cast<float>(y / count), static_cast<float>(z / count)};
    return true;
}

bool deviceControls(SpatialDevice& device, const char* id, const SpatialMapping& spatial) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Position"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::DragFloat3("##position", &device.pose.position.x, 0.01f, -kMaxSpatialCoordinate, kMaxSpatialCoordinate, "%.3f");
    ImGui::SetItemTooltip("%s", tr("Metres. X is right, Y is up and Z points towards the audience. With all angles at zero the device looks along minus Z."));
    float angles[3] = {device.pose.yaw, device.pose.pitch, device.pose.roll};
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Angles"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat3("##angles", angles, 0.1f, -360.0f, 360.0f, "%.2f")) {
        device.pose.yaw = angles[0];
        device.pose.pitch = angles[1];
        device.pose.roll = angles[2];
        changed = true;
    }
    ImGui::SetItemTooltip("%s", tr("Yaw, pitch and roll in degrees. Positive yaw turns to the left, positive pitch looks up, positive roll lifts the right side."));
    Vec3f centre;
    ImGui::BeginDisabled(!surfacesCentre(spatial, centre));
    if (ImGui::SmallButton(tr("Aim at the surfaces"))) {
        const float roll = device.pose.roll;
        device.pose = poseLookAt(device.pose.position, centre, roll);
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("Turns the device towards the middle of the surfaces and keeps its roll."));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Field of view"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::DragFloat("##fov", &device.lens.fovY, 0.1f, 1.0f, 120.0f, "%.2f");
    ImGui::SetItemTooltip("%s", tr("Vertical opening angle of the lens in degrees."));
    float throwRatio = throwRatioFromFovY(device.lens.fovY, device.lens.aspect);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Throw ratio"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("##throw", &throwRatio, 0.005f, 0.1f, 20.0f, "%.3f")) {
        device.lens.fovY = fovYFromThrowRatio(throwRatio, device.lens.aspect);
        changed = true;
    }
    ImGui::SetItemTooltip("%s", tr("Distance divided by picture width, as printed in a projector data sheet. It is the same setting as the field of view."));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Aspect"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::DragFloat("##aspect", &device.lens.aspect, 0.005f, 0.25f, 4.0f, "%.4f");
    ImGui::SetItemTooltip("%s", tr("Width divided by height of the picture. It has to match the output window or the projector."));
    const float presets[3] = {16.0f / 9.0f, 16.0f / 10.0f, 4.0f / 3.0f};
    const char* labels[3] = {"16:9", "16:10", "4:3"};
    for (int i = 0; i < 3; ++i) {
        ImGui::SameLine();
        if (ImGui::SmallButton(labels[i])) {
            device.lens.aspect = presets[i];
            changed = true;
        }
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Lens shift"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::DragFloat2("##shift", &device.lens.shiftX, 0.002f, -1.5f, 1.5f, "%.3f");
    ImGui::SetItemTooltip("%s", tr("Moves the picture without turning the device, as a fraction of the picture size. Positive Y moves it up."));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Near and far"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::DragFloat2("##planes", &device.lens.nearPlane, 0.01f, 0.01f, 5000.0f, "%.2f");
    ImGui::SetItemTooltip("%s", tr("Nearest and farthest distance in metres that are drawn."));
    ImGui::PopID();
    return changed;
}

}

void App::drawSpatialMapping(OutputSpec& spec, int index, OutputMapping& map) {
    (void)spec;
    SpatialMapping& spatial = map.spatial;
    const int owner = index * 100 + selectedSlice_;
    if (spatialOwner_ != owner) {
        spatialOwner_ = owner;
        spatialSurface_ = 0;
        spatialPoint_ = 0;
        spatialHasResult_ = false;
        spatialCanUndo_ = false;
        spatialDrag_ = -1;
        spatialMeshValid_ = false;
    }
    ImGui::SeparatorText(tr("3D mapping"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Lights real surfaces in space, such as a wall, a pillar or a box. You describe the projector and the surfaces in metres and the picture is projected onto them with the right perspective. While it is on, the corners and the warp of this slice are not used. Masks, edge blending and colour still work."));
    ImGui::PopTextWrapPos();
    bool enabled = spatial.enabled;
    if (ImGui::Checkbox(tr("Use 3D mapping for this slice"), &enabled)) {
        if (enabled && spatial.surfaces.empty() && spatial.points.empty()) {
            spatial = defaultSpatialSetup();
        }
        spatial.enabled = enabled;
        spatialMeshValid_ = false;
    }
    ImGui::SetItemTooltip("%s", tr("Off: this slice is drawn flat with its corners and warp. On: it is drawn from a virtual projector onto the surfaces below. Other slices and outputs are not changed."));
    if (!spatial.enabled) {
        return;
    }

    ImGui::RadioButton(tr("Picture on the surfaces"), reinterpret_cast<int*>(&spatial.source), static_cast<int>(SpatialSource::SurfaceUv));
    ImGui::SetItemTooltip("%s", tr("Every surface shows its own rectangle of the picture, set under Surfaces."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("Picture seen by a viewer"), reinterpret_cast<int*>(&spatial.source), static_cast<int>(SpatialSource::ViewerCamera));
    ImGui::SetItemTooltip("%s", tr("A virtual viewer looks at the picture and every surface shows what the viewer would see there. Use it for forced perspective, where the picture only looks right from one spot."));

    OutputMapping probe;
    probe.spatial = spatial;
    probe.slice = map.slice;
    probe.flipX = map.flipX;
    probe.flipY = map.flipY;
    if (!spatialMeshValid_ || !(spatialMeshKey_ == probe)) {
        spatialMesh_ = buildSpatialMesh(probe);
        spatialMeshKey_ = probe;
        spatialMeshValid_ = true;
    }
    const SpatialMeshStats& stats = spatialMesh_.stats;
    if (usableSurfaceCount(spatial) == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("No surface is switched on, so nothing is drawn."));
    } else {
        char line[256];
        std::snprintf(line, sizeof(line), tr("%d triangles drawn, %d facing away, %d behind the projector, %d cut at the near plane, %d outside the picture"), stats.trianglesOut, stats.culled, stats.behind, stats.clipped, stats.offscreen);
        ImGui::TextDisabled("%s", line);
    }
    if (outputs_.isOpen(index) && !outputs_.depthAvailable(index)) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("This output window has no depth buffer, so surfaces are drawn in list order and do not hide each other."));
    }
    ImGui::TextDisabled("%s", tr("Coordinates: X to the right, Y up, Z towards the audience, metres, angles in degrees."));

    ImGui::SeparatorText(tr("Projector"));
    deviceControls(spatial.projector, "projector", spatial);
    if (spatial.source == SpatialSource::ViewerCamera) {
        ImGui::SeparatorText(tr("Viewer"));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("The spot from which the picture is meant to be seen. Its field of view and aspect set how much of the picture the viewer sees."));
        ImGui::PopTextWrapPos();
        deviceControls(spatial.camera, "viewer", spatial);
    }

    drawSpatialSurfaces(spatial);
    drawSpatialCalibration(index, spatial);

    ImGui::SeparatorText(tr("Views"));
    ImGui::TextDisabled("%s", tr("Output view: what the projector draws. Top view: the room seen from above."));
    drawSpatialOutputView(spatial);
    drawSpatialTopView(spatial);
    normalizeSpatial(spatial);
}

void App::drawSpatialSurfaces(SpatialMapping& spatial) {
    ImGui::SeparatorText(tr("Surfaces"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("The real things that the projector lights. Describe each one by its four corners in metres, in this order as seen from the front: top left, top right, bottom right, bottom left. The front is the side that the projector has to be on."));
    ImGui::PopTextWrapPos();
    spatialSurface_ = std::clamp(spatialSurface_, 0, std::max(0, static_cast<int>(spatial.surfaces.size()) - 1));
    for (size_t i = 0; i < spatial.surfaces.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool hidden = !spatial.surfaces[i].enabled || !surfaceUsable(spatial.surfaces[i]);
        if (hidden) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        }
        const std::string label = surfaceLabel(spatial.surfaces[i], static_cast<int>(i));
        if (ImGui::Selectable(label.c_str(), spatialSurface_ == static_cast<int>(i))) {
            spatialSurface_ = static_cast<int>(i);
        }
        if (hidden) {
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
    }
    const bool full = static_cast<int>(spatial.surfaces.size()) >= kMaxSpatialSurfaces;
    ImGui::BeginDisabled(full);
    if (ImGui::SmallButton(tr("Add wall"))) {
        SpatialSurface wall = wallSurface(std::string(tr("Wall")) + " " + std::to_string(spatial.surfaces.size() + 1), Vec3f{0.0f, 1.5f, 0.0f}, 4.0f, 2.25f, 0.0f, 0.0f);
        if (addSpatialSurface(spatial, wall)) {
            spatialSurface_ = static_cast<int>(spatial.surfaces.size()) - 1;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Add box"))) {
        const std::vector<SpatialSurface> faces = boxSurfaces(std::string(tr("Box")) + " " + std::to_string(spatial.surfaces.size() + 1), Vec3f{0.0f, 1.0f, 0.0f}, 1.0f, 2.0f, 1.0f);
        for (const SpatialSurface& face : faces) {
            if (addSpatialSurface(spatial, face)) {
                spatialSurface_ = static_cast<int>(spatial.surfaces.size()) - 1;
            }
        }
    }
    ImGui::SetItemTooltip("%s", tr("Adds the five visible faces of a box that stands on the floor: front, left, right, back and top."));
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Duplicate")) && !spatial.surfaces.empty()) {
        SpatialSurface copy = spatial.surfaces[static_cast<size_t>(spatialSurface_)];
        copy.name += " 2";
        if (addSpatialSurface(spatial, copy)) {
            spatialSurface_ = static_cast<int>(spatial.surfaces.size()) - 1;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(spatial.surfaces.empty());
    if (ImGui::SmallButton(tr("Remove")) && removeSpatialSurface(spatial, spatialSurface_)) {
        spatialSurface_ = std::clamp(spatialSurface_, 0, std::max(0, static_cast<int>(spatial.surfaces.size()) - 1));
    }
    ImGui::EndDisabled();
    if (spatial.surfaces.empty()) {
        return;
    }
    SpatialSurface& surface = spatial.surfaces[static_cast<size_t>(spatialSurface_)];
    ImGui::PushID("surface");
    char name[48];
    std::snprintf(name, sizeof(name), "%s", surface.name.c_str());
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::InputText("##name", name, sizeof(name))) {
        surface.name = name;
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("Show"), &surface.enabled);
    ImGui::SameLine();
    ImGui::Checkbox(tr("Both sides"), &surface.doubleSided);
    ImGui::SetItemTooltip("%s", tr("Off: the surface is only drawn when it faces the projector, so the back of a box costs nothing. On: it is drawn from behind as well."));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Subdivisions"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::sliderInt("##divisions", &surface.divisions, 1, kMaxSurfaceDivisions, "%d");
    ImGui::SetItemTooltip("%s", tr("A flat surface needs very few. More subdivisions only make the mesh finer; they cost memory."));
    const char* const cornerNames[4] = {tr("Top left"), tr("Top right"), tr("Bottom right"), tr("Bottom left")};
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(cornerNames[i]);
        ImGui::SameLine(labelColumn());
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat3("##corner", &surface.corners[static_cast<size_t>(i)].x, 0.01f, -kMaxSpatialCoordinate, kMaxSpatialCoordinate, "%.3f");
        ImGui::PopID();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Picture rectangle"));
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat4("##uv", surface.uv.data(), 0.002f, -8.0f, 8.0f, "%.3f");
    ImGui::SetItemTooltip("%s", tr("Left, top, right and bottom edge of the part of this slice's picture that the surface shows, from 0 to 1. The default shows all of it."));
    if (ImGui::SmallButton(tr("Show all of the picture"))) {
        surface.uv = {0.0f, 0.0f, 1.0f, 1.0f};
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s %.2f m2", tr("Area"), static_cast<double>(surfaceArea(surface)));
    ImGui::PopID();
}

void App::drawSpatialCalibration(int index, SpatialMapping& spatial) {
    ImGui::SeparatorText(tr("Calibration"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Finds the position and angles of the projector from points that you know. For each point enter where it is in space, switch on the markers and the test pattern, then move the green cross until the light lands on the real spot. With four or more points that do not lie on one line, Solve computes the projector pose. It needs six points when the field of view is solved as well."));
    ImGui::PopTextWrapPos();
    bool markers = outputs_.markers(index);
    spatialPoint_ = std::clamp(spatialPoint_, 0, std::max(0, static_cast<int>(spatial.points.size()) - 1));
    if (ImGui::Checkbox(tr("Show the points on the output"), &markers)) {
        outputs_.setMarkers(index, markers, markers ? spatialPoint_ : -1);
    } else if (markers) {
        outputs_.setMarkers(index, true, spatialPoint_);
    }
    ImGui::SetItemTooltip("%s", tr("Draws a green cross where a point should land, an orange X where the current pose puts it and a yellow line between them. The selected point is white. The markers are not sent over NDI."));

    std::vector<float> errors;
    const double rms = calibrationError(spatial.points, spatial.projector, &errors);
    size_t shown = 0;
    for (size_t i = 0; i < spatial.points.size(); ++i) {
        const CalibrationPoint& point = spatial.points[i];
        ImGui::PushID(static_cast<int>(i));
        char text[128];
        const std::string label = pointLabel(point, static_cast<int>(i));
        if (point.enabled && shown < errors.size()) {
            std::snprintf(text, sizeof(text), "%s   %.1f px", label.c_str(), static_cast<double>(errors[shown]) * kPixelWidth);
            ++shown;
        } else {
            std::snprintf(text, sizeof(text), "%s", label.c_str());
        }
        if (!point.enabled) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        }
        if (ImGui::Selectable(text, spatialPoint_ == static_cast<int>(i))) {
            spatialPoint_ = static_cast<int>(i);
        }
        if (!point.enabled) {
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
    }
    if (!errors.empty()) {
        ImGui::TextDisabled("%s %.1f px", tr("Average error at 1920 pixels wide"), rms * static_cast<double>(kPixelWidth));
    }

    const bool full = static_cast<int>(spatial.points.size()) >= kMaxSpatialPoints;
    ImGui::BeginDisabled(full);
    if (ImGui::SmallButton(tr("Add point"))) {
        CalibrationPoint point;
        point.name = std::string(tr("Point")) + " " + std::to_string(spatial.points.size() + 1);
        Vec3f middle;
        if (surfacesCentre(spatial, middle)) {
            point.world = middle;
        }
        const ProjectedPoint p = projectPoint(spatial.projector, point.world);
        if (p.inFront) {
            point.outputX = std::clamp(p.x, 0.0f, 1.0f);
            point.outputY = std::clamp(p.y, 0.0f, 1.0f);
        }
        if (addCalibrationPoint(spatial, point)) {
            spatialPoint_ = static_cast<int>(spatial.points.size()) - 1;
        }
    }
    ImGui::SameLine();
    const bool haveSurface = !spatial.surfaces.empty();
    ImGui::BeginDisabled(!haveSurface);
    if (ImGui::SmallButton(tr("Add the corners of the selected surface"))) {
        const SpatialSurface& surface = spatial.surfaces[static_cast<size_t>(std::clamp(spatialSurface_, 0, static_cast<int>(spatial.surfaces.size()) - 1))];
        static const char* const kShort[4] = {"TL", "TR", "BR", "BL"};
        for (int i = 0; i < 4; ++i) {
            CalibrationPoint point;
            point.name = surface.name + " " + kShort[i];
            point.world = surface.corners[static_cast<size_t>(i)];
            const ProjectedPoint p = projectPoint(spatial.projector, point.world);
            if (p.inFront) {
                point.outputX = std::clamp(p.x, 0.0f, 1.0f);
                point.outputY = std::clamp(p.y, 0.0f, 1.0f);
            }
            if (addCalibrationPoint(spatial, point)) {
                spatialPoint_ = static_cast<int>(spatial.points.size()) - 1;
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(spatial.points.empty());
    if (ImGui::SmallButton(tr("Remove point")) && removeCalibrationPoint(spatial, spatialPoint_)) {
        spatialPoint_ = std::clamp(spatialPoint_, 0, std::max(0, static_cast<int>(spatial.points.size()) - 1));
    }
    ImGui::EndDisabled();

    if (!spatial.points.empty()) {
        CalibrationPoint& point = spatial.points[static_cast<size_t>(spatialPoint_)];
        ImGui::PushID("point");
        char name[48];
        std::snprintf(name, sizeof(name), "%s", point.name.c_str());
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::InputText("##name", name, sizeof(name))) {
            point.name = name;
        }
        ImGui::SameLine();
        ImGui::Checkbox(tr("Use this point"), &point.enabled);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Place in space"));
        ImGui::SameLine(labelColumn());
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat3("##world", &point.world.x, 0.01f, -kMaxSpatialCoordinate, kMaxSpatialCoordinate, "%.3f");
        ImGui::SetItemTooltip("%s", tr("Where the real spot is, in metres, measured in the same coordinates as the surfaces."));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Place on output"));
        ImGui::SameLine(labelColumn());
        ImGui::SetNextItemWidth(-FLT_MIN);
        float output[2] = {point.outputX, point.outputY};
        if (ImGui::DragFloat2("##output", output, 0.0005f, 0.0f, 1.0f, "%.4f")) {
            point.outputX = output[0];
            point.outputY = output[1];
        }
        ImGui::SetItemTooltip("%s", tr("Where the light for this spot comes from in the output, from 0 to 1, x to the right and y downwards. Move it until the light lands on the real spot."));
        const float step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
        const float stepX = step / kPixelWidth;
        const float stepY = stepX * std::clamp(spatial.projector.lens.aspect, 0.25f, 4.0f);
        ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
        if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
            point.outputX -= stepX;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
            point.outputX += stepX;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            point.outputY -= stepY;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            point.outputY += stepY;
        }
        ImGui::PopItemFlag();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("One pixel per click at 1920 wide, ten with Shift"));
        if (ImGui::SmallButton(tr("Put the cross where the pose puts the point"))) {
            const ProjectedPoint p = projectPoint(spatial.projector, point.world);
            if (p.inFront) {
                point.outputX = std::clamp(p.x, 0.0f, 1.0f);
                point.outputY = std::clamp(p.y, 0.0f, 1.0f);
            }
        }
        ImGui::PopID();
    }

    ImGui::Checkbox(tr("Solve the field of view too"), &spatialSolveFov_);
    ImGui::SetItemTooltip("%s", tr("Needs six points. Leave it off when the lens is known; the pose is then more stable."));
    if (ImGui::Button(tr("Solve the projector pose"))) {
        CalibrationOptions options;
        options.solveFov = spatialSolveFov_;
        spatialResult_ = solveDevice(spatial.points, spatial.projector, options);
        spatialHasResult_ = true;
    }
    if (spatialHasResult_) {
        const CalibrationResult& result = spatialResult_;
        char line[256];
        switch (result.status) {
            case CalibrationStatus::Ok:
                std::snprintf(line, sizeof(line), tr("Solved. Average error %.1f px, worst %.1f px at 1920 wide."), result.rmsPixels, result.maxError * static_cast<double>(kPixelWidth));
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", line);
                break;
            case CalibrationStatus::TooFewPoints:
                std::snprintf(line, sizeof(line), tr("Not enough points: %d are switched on, %d are needed."), result.usedPoints, spatialSolveFov_ ? kMinCalibrationPointsWithFov : kMinCalibrationPoints);
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", line);
                break;
            case CalibrationStatus::Collinear:
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", tr("The points lie on one line. Spread them over the surfaces."));
                break;
            case CalibrationStatus::NotConverged:
                std::snprintf(line, sizeof(line), tr("No pose fits these points well: average error %.1f px. Check that every place in space belongs to its cross."), result.rmsPixels);
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", line);
                break;
        }
        const bool usable = result.status == CalibrationStatus::Ok || result.status == CalibrationStatus::NotConverged;
        ImGui::BeginDisabled(!usable);
        if (ImGui::Button(tr("Apply to the projector"))) {
            spatialBefore_ = spatial.projector;
            spatial.projector = result.device;
            spatialCanUndo_ = true;
            spatialHasResult_ = false;
        }
        ImGui::EndDisabled();
    }
    if (spatialCanUndo_) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Undo"))) {
            spatial.projector = spatialBefore_;
            spatialCanUndo_ = false;
        }
    }
}

void App::drawSpatialOutputView(SpatialMapping& spatial) {
    const float aspect = std::clamp(spatial.projector.lens.aspect, 0.25f, 4.0f);
    const float width = std::max(160.0f, std::min(ImGui::GetContentRegionAvail().x, 480.0f));
    const ImVec2 size(width, width / aspect);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("spatialoutput", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(10, 10, 12, 255));
    draw->PushClipRect(origin, end, true);
    const auto screen = [&](float x, float y) { return ImVec2(origin.x + x * size.x, origin.y + y * size.y); };
    const size_t triangles = std::min<size_t>(spatialMesh_.indices.size() / 3u, 6000u);
    for (size_t t = 0; t < triangles; ++t) {
        const SpatialVertex& a = spatialMesh_.vertices[spatialMesh_.indices[t * 3u]];
        const SpatialVertex& b = spatialMesh_.vertices[spatialMesh_.indices[t * 3u + 1u]];
        const SpatialVertex& c = spatialMesh_.vertices[spatialMesh_.indices[t * 3u + 2u]];
        draw->AddTriangleFilled(screen(a.x, a.y), screen(b.x, b.y), screen(c.x, c.y), IM_COL32(70, 110, 170, 110));
    }
    for (const SpatialSurface& surface : spatial.surfaces) {
        if (!surface.enabled || !surfaceUsable(surface)) {
            continue;
        }
        ProjectedPoint p[4];
        bool visible = true;
        for (size_t i = 0; i < 4; ++i) {
            p[i] = projectPoint(spatial.projector, surface.corners[i]);
            visible = visible && p[i].inFront;
        }
        if (!visible) {
            continue;
        }
        for (size_t i = 0; i < 4; ++i) {
            const ProjectedPoint& from = p[i];
            const ProjectedPoint& to = p[(i + 1) % 4];
            draw->AddLine(screen(from.x, from.y), screen(to.x, to.y), IM_COL32(150, 190, 240, 200), 1.0f);
        }
    }
    const bool dragging = spatialDrag_ >= 0 && spatialDrag_ < static_cast<int>(spatial.points.size());
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int best = -1;
        float bestDistance = 12.0f;
        for (size_t i = 0; i < spatial.points.size(); ++i) {
            const ImVec2 at = screen(spatial.points[i].outputX, spatial.points[i].outputY);
            const float distance = std::fabs(at.x - mouse.x) + std::fabs(at.y - mouse.y);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        if (best >= 0) {
            spatialDrag_ = best;
            spatialPoint_ = best;
        }
    }
    if (dragging || spatialDrag_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && spatialDrag_ >= 0 && spatialDrag_ < static_cast<int>(spatial.points.size())) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            CalibrationPoint& point = spatial.points[static_cast<size_t>(spatialDrag_)];
            point.outputX = std::clamp((mouse.x - origin.x) / size.x, 0.0f, 1.0f);
            point.outputY = std::clamp((mouse.y - origin.y) / size.y, 0.0f, 1.0f);
        } else {
            spatialDrag_ = -1;
        }
    }
    for (size_t i = 0; i < spatial.points.size(); ++i) {
        const CalibrationPoint& point = spatial.points[i];
        const ImVec2 target = screen(point.outputX, point.outputY);
        const bool selected = static_cast<int>(i) == spatialPoint_;
        const ImU32 targetColour = !point.enabled ? IM_COL32(115, 115, 115, 255) : (selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(26, 255, 102, 255));
        const ProjectedPoint p = projectPoint(spatial.projector, point.world);
        if (point.enabled && p.inFront) {
            const ImVec2 predicted = screen(p.x, p.y);
            draw->AddLine(target, predicted, IM_COL32(255, 230, 26, 255), 1.0f);
            draw->AddLine(ImVec2(predicted.x - 4.0f, predicted.y - 4.0f), ImVec2(predicted.x + 4.0f, predicted.y + 4.0f), IM_COL32(255, 140, 26, 255), 2.0f);
            draw->AddLine(ImVec2(predicted.x - 4.0f, predicted.y + 4.0f), ImVec2(predicted.x + 4.0f, predicted.y - 4.0f), IM_COL32(255, 140, 26, 255), 2.0f);
        }
        draw->AddLine(ImVec2(target.x - 7.0f, target.y), ImVec2(target.x + 7.0f, target.y), targetColour, 2.0f);
        draw->AddLine(ImVec2(target.x, target.y - 7.0f), ImVec2(target.x, target.y + 7.0f), targetColour, 2.0f);
    }
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(90, 90, 100, 255));
    ImGui::SetItemTooltip("%s", tr("Output view. Blue: the surfaces as the projector draws them. Green cross: where a point should land. Orange X: where the current pose puts it. Drag a green cross to move that point."));
}

void App::drawSpatialTopView(SpatialMapping& spatial) {
    const float width = std::max(160.0f, std::min(ImGui::GetContentRegionAvail().x, 480.0f));
    const ImVec2 size(width, width * 0.7f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("spatialtop", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(10, 10, 12, 255));
    draw->PushClipRect(origin, end, true);

    float minX = 1e9f;
    float maxX = -1e9f;
    float minZ = 1e9f;
    float maxZ = -1e9f;
    const auto grow = [&](float x, float z) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
    };
    grow(spatial.projector.pose.position.x, spatial.projector.pose.position.z);
    if (spatial.source == SpatialSource::ViewerCamera) {
        grow(spatial.camera.pose.position.x, spatial.camera.pose.position.z);
    }
    for (const SpatialSurface& surface : spatial.surfaces) {
        if (!surface.enabled || !surfaceUsable(surface)) {
            continue;
        }
        for (const Vec3f& corner : surface.corners) {
            grow(corner.x, corner.z);
        }
    }
    for (const CalibrationPoint& point : spatial.points) {
        grow(point.world.x, point.world.z);
    }
    const float spanX = std::max(maxX - minX, 1.0f);
    const float spanZ = std::max(maxZ - minZ, 1.0f);
    const float margin = 18.0f;
    const float scale = std::min((size.x - 2.0f * margin) / spanX, (size.y - 2.0f * margin) / spanZ);
    const float centreX = 0.5f * (minX + maxX);
    const float centreZ = 0.5f * (minZ + maxZ);
    const auto at = [&](float x, float z) {
        return ImVec2(origin.x + size.x * 0.5f + (x - centreX) * scale, origin.y + size.y * 0.5f + (z - centreZ) * scale);
    };
    const float barLength = scale;
    draw->AddLine(ImVec2(origin.x + 8.0f, end.y - 10.0f), ImVec2(origin.x + 8.0f + barLength, end.y - 10.0f), IM_COL32(150, 150, 160, 255), 2.0f);
    draw->AddText(ImVec2(origin.x + 12.0f + barLength, end.y - 18.0f), IM_COL32(150, 150, 160, 255), "1 m");

    for (const SpatialSurface& surface : spatial.surfaces) {
        if (!surface.enabled || !surfaceUsable(surface)) {
            continue;
        }
        ImVec2 corners[4];
        for (size_t i = 0; i < 4; ++i) {
            corners[i] = at(surface.corners[i].x, surface.corners[i].z);
        }
        draw->AddConvexPolyFilled(corners, 4, IM_COL32(70, 110, 170, 70));
        for (size_t i = 0; i < 4; ++i) {
            draw->AddLine(corners[i], corners[(i + 1) % 4], IM_COL32(150, 190, 240, 220), 2.0f);
        }
    }
    for (size_t i = 0; i < spatial.points.size(); ++i) {
        const CalibrationPoint& point = spatial.points[i];
        const ImVec2 position = at(point.world.x, point.world.z);
        draw->AddCircleFilled(position, 3.0f, static_cast<int>(i) == spatialPoint_ ? IM_COL32(255, 255, 255, 255) : IM_COL32(26, 255, 102, 255));
    }
    const auto device = [&](const SpatialDevice& d, ImU32 colour) {
        const ImVec2 position = at(d.pose.position.x, d.pose.position.z);
        const Vec3f forward = forwardOf(d.pose);
        const Vec3f right = rightOf(d.pose);
        const float halfHeight = std::tan(static_cast<float>(d.lens.fovY * 0.5 * kPi / 180.0));
        const float halfWidth = halfHeight * d.lens.aspect;
        const float reach = std::max(1.0f, 0.5f * std::max(spanX, spanZ)) * scale;
        const auto edge = [&](float side) {
            float dx = forward.x + right.x * halfWidth * side;
            float dz = forward.z + right.z * halfWidth * side;
            const float length = std::sqrt(dx * dx + dz * dz);
            if (length < 1e-6f) {
                return position;
            }
            dx /= length;
            dz /= length;
            return ImVec2(position.x + dx * reach, position.y + dz * reach);
        };
        draw->AddLine(position, edge(-1.0f), colour, 1.0f);
        draw->AddLine(position, edge(1.0f), colour, 1.0f);
        draw->AddCircleFilled(position, 4.5f, colour);
    };
    if (spatial.source == SpatialSource::ViewerCamera) {
        device(spatial.camera, IM_COL32(120, 200, 255, 255));
    }
    device(spatial.projector, IM_COL32(255, 140, 26, 255));
    draw->PopClipRect();
    draw->AddRect(origin, end, IM_COL32(90, 90, 100, 255));
    ImGui::SetItemTooltip("%s", tr("Top view, looking down. Orange: the projector and the area it lights. Light blue: the viewer. Blue: the surfaces. Green dots: calibration points. The audience is at the bottom."));
}

}

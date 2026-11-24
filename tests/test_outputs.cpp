#include "check.hpp"

#include "core/frame_lock.hpp"
#include "core/mask_images.hpp"
#include "core/output_map.hpp"
#include "core/output_map_json.hpp"
#include "core/settings.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

namespace {

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

void testDefaultMesh() {
    nvj::OutputMapping map;
    CHECK(nvj::isDefaultMapping(map));
    const std::vector<nvj::MeshVertex> mesh = nvj::buildMesh(map, 4);
    CHECK(mesh.size() == 25);
    CHECK(near(mesh.front().x, 0.0f) && near(mesh.front().y, 0.0f) && near(mesh.front().u, 0.0f) && near(mesh.front().v, 1.0f));
    CHECK(near(mesh.back().x, 1.0f) && near(mesh.back().y, 1.0f) && near(mesh.back().u, 1.0f) && near(mesh.back().v, 0.0f));
    CHECK(near(mesh[12].x, 0.5f) && near(mesh[12].y, 0.5f));
    const std::vector<uint32_t> indices = nvj::buildMeshIndices(4);
    CHECK(indices.size() == 4u * 4u * 6u);
    for (const uint32_t index : indices) {
        CHECK(index < mesh.size());
    }
}

void testCornerPin() {
    nvj::OutputMapping map;
    map.corners = {{{0.1f, 0.2f}, {0.9f, 0.1f}, {0.8f, 0.9f}, {0.2f, 0.8f}}};
    CHECK(!nvj::isDefaultMapping(map));
    const nvj::Vec2f tl = nvj::mapPoint(map, 0.0f, 0.0f);
    const nvj::Vec2f tr = nvj::mapPoint(map, 1.0f, 0.0f);
    const nvj::Vec2f br = nvj::mapPoint(map, 1.0f, 1.0f);
    const nvj::Vec2f bl = nvj::mapPoint(map, 0.0f, 1.0f);
    CHECK(near(tl.x, 0.1f) && near(tl.y, 0.2f));
    CHECK(near(tr.x, 0.9f) && near(tr.y, 0.1f));
    CHECK(near(br.x, 0.8f) && near(br.y, 0.9f));
    CHECK(near(bl.x, 0.2f) && near(bl.y, 0.8f));


    nvj::OutputMapping shifted;
    shifted.corners = {{{0.1f, 0.1f}, {1.1f, 0.1f}, {1.1f, 1.1f}, {0.1f, 1.1f}}};
    const nvj::Vec2f moved = nvj::mapPoint(shifted, 0.25f, 0.75f);
    CHECK(near(moved.x, 0.35f) && near(moved.y, 0.85f));
}

void testPerspective() {
    nvj::OutputMapping map;
    map.corners = {{{0.25f, 0.0f}, {0.75f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    const nvj::Vec2f centre = nvj::mapPoint(map, 0.5f, 0.5f);
    CHECK(near(centre.x, 0.5f, 1e-4f));
    CHECK(near(centre.y, 1.0f / 3.0f, 1e-3f));
    const nvj::Vec2f left = nvj::mapPoint(map, 0.0f, 0.5f);
    const nvj::Vec2f right = nvj::mapPoint(map, 1.0f, 0.5f);
    CHECK(near(left.y, right.y, 1e-4f));
    const float widthAtCentre = right.x - left.x;
    CHECK(widthAtCentre > 0.5f && widthAtCentre < 1.0f);
    CHECK(near(widthAtCentre, 0.5f + 0.5f * (1.0f / 3.0f), 1e-3f));
}

void testWarp() {
    nvj::OutputMapping map;
    nvj::resizeWarp(map, 3, 3);
    CHECK(map.warp.size() == 9);
    CHECK(nvj::isDefaultMapping(map) == false || map.warpColumns == 3);
    map.warp[4] = {0.05f, -0.03f};
    const nvj::Vec2f centre = nvj::mapPoint(map, 0.5f, 0.5f);
    CHECK(near(centre.x, 0.55f, 1e-4f) && near(centre.y, 0.47f, 1e-4f));
    const nvj::Vec2f corner = nvj::mapPoint(map, 0.0f, 0.0f);
    CHECK(near(corner.x, 0.0f) && near(corner.y, 0.0f));
    const nvj::Vec2f edge = nvj::mapPoint(map, 1.0f, 1.0f);
    CHECK(near(edge.x, 1.0f) && near(edge.y, 1.0f));
    const nvj::Vec2f between = nvj::mapPoint(map, 0.25f, 0.5f);
    CHECK(between.x > 0.25f && between.x < 0.3f);

    nvj::OutputMapping linear;
    nvj::resizeWarp(linear, 2, 2);
    linear.warp[1] = {0.1f, 0.0f};
    CHECK(near(nvj::mapPoint(linear, 0.5f, 0.0f).x, 0.55f, 1e-4f));
    CHECK(near(nvj::mapPoint(linear, 1.0f, 0.0f).x, 1.1f, 1e-4f));

    nvj::resizeWarp(map, 5, 5);
    CHECK(map.warp.size() == 25);
    CHECK(near(nvj::mapPoint(map, 0.5f, 0.5f).x, 0.55f, 5e-3f));
    nvj::resetWarp(map);
    CHECK(near(nvj::mapPoint(map, 0.5f, 0.5f).x, 0.5f));
}

void testSliceAndFlip() {
    nvj::OutputMapping map;
    map.slice = {0.25f, 0.5f, 0.5f, 0.5f};
    std::vector<nvj::MeshVertex> mesh = nvj::buildMesh(map, 2);
    CHECK(near(mesh.front().u, 0.25f) && near(mesh.front().v, 0.5f));
    CHECK(near(mesh.back().u, 0.75f) && near(mesh.back().v, 0.0f));
    map.flipX = true;
    mesh = nvj::buildMesh(map, 2);
    CHECK(near(mesh.front().u, 0.75f) && near(mesh.back().u, 0.25f));
    map.flipY = true;
    mesh = nvj::buildMesh(map, 2);
    CHECK(near(mesh.front().v, 0.0f) && near(mesh.back().v, 0.5f));
}

void testNormalize() {
    nvj::OutputMapping map;
    map.slice = {0.9f, 0.9f, 0.9f, 5.0f};
    map.blend.left = 3.0f;
    map.blend.gamma = 9.0f;
    map.brightness = -1.0f;
    map.warpColumns = 50;
    map.warpRows = 1;
    map.corners[0] = {99.0f, -99.0f};
    nvj::normalizeMapping(map);
    CHECK(map.slice[0] + map.slice[2] <= 1.0001f && map.slice[1] + map.slice[3] <= 1.0001f);
    CHECK(map.blend.left == 0.5f && map.blend.gamma == 3.0f && map.brightness == 0.0f);
    CHECK(map.warpColumns == nvj::kMaxWarpPoints && map.warpRows == nvj::kMinWarpPoints);
    CHECK(map.warp.size() == static_cast<size_t>(map.warpColumns * map.warpRows));
    CHECK(map.corners[0].x == 1.5f && map.corners[0].y == -0.5f);
}

void testEdgeBlend() {
    CHECK(near(nvj::edgeRamp(0.0f, 2.0f, 2.2f), 0.0f));
    CHECK(near(nvj::edgeRamp(1.0f, 2.0f, 2.2f), 1.0f));
    CHECK(near(nvj::edgeRamp(0.5f, 2.0f, 1.0f), 0.5f));
    float last = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float value = nvj::edgeRamp(static_cast<float>(i) / 20.0f, 2.0f, 2.2f);
        CHECK(value >= last);
        last = value;
    }
    for (int i = 0; i <= 20; ++i) {
        const float t = static_cast<float>(i) / 20.0f;
        const float a = std::pow(nvj::edgeRamp(t, 2.0f, 2.2f), 2.2f);
        const float b = std::pow(nvj::edgeRamp(1.0f - t, 2.0f, 2.2f), 2.2f);
        CHECK(near(a + b, 1.0f, 1e-3f));
    }

    nvj::EdgeBlend blend;
    CHECK(near(nvj::edgeFactor(blend, 0.0f, 0.0f), 1.0f));
    blend.left = 0.2f;
    blend.right = 0.2f;
    CHECK(near(nvj::edgeFactor(blend, 0.0f, 0.5f), 0.0f));
    CHECK(near(nvj::edgeFactor(blend, 1.0f, 0.5f), 0.0f));
    CHECK(near(nvj::edgeFactor(blend, 0.5f, 0.5f), 1.0f));
    CHECK(nvj::edgeFactor(blend, 0.1f, 0.5f) > 0.0f && nvj::edgeFactor(blend, 0.1f, 0.5f) < 1.0f);
}

void testLayout() {
    std::vector<nvj::OutputMapping> maps;
    nvj::layoutGrid(3, 1, 0.1f, maps);
    CHECK(maps.size() == 3);
    CHECK(near(maps[0].slice[0], 0.0f));
    CHECK(near(maps[2].slice[0] + maps[2].slice[2], 1.0f, 1e-4f));
    CHECK(maps[0].blend.left == 0.0f && maps[0].blend.right > 0.0f);
    CHECK(maps[1].blend.left > 0.0f && maps[1].blend.right > 0.0f);
    CHECK(maps[2].blend.right == 0.0f && maps[2].blend.left > 0.0f);
    const float overlap = maps[0].slice[0] + maps[0].slice[2] - maps[1].slice[0];
    CHECK(near(overlap, 0.1f, 1e-4f));
    CHECK(near(maps[0].blend.right, 0.1f / maps[0].slice[2], 1e-4f));

    nvj::layoutGrid(2, 2, 0.0f, maps);
    CHECK(maps.size() == 4);
    CHECK(near(maps[3].slice[0], 0.5f) && near(maps[3].slice[1], 0.5f) && near(maps[3].slice[2], 0.5f));
    CHECK(maps[3].blend.left == 0.0f && maps[3].blend.top == 0.0f);

    nvj::layoutGrid(1, 1, 0.2f, maps);
    CHECK(maps.size() == 1 && nvj::isDefaultMapping(maps[0]));
}

void testJson() {
    nvj::OutputSpec spec = nvj::defaultOutputSpec(1);
    CHECK(spec.name == "Output 2" && !spec.enabled && spec.monitor == 1);
    spec.enabled = true;
    spec.fullscreen = true;
    spec.map.slice = {0.5f, 0.0f, 0.5f, 1.0f};
    spec.map.corners[2] = {0.95f, 0.9f};
    nvj::resizeWarp(spec.map, 4, 3);
    spec.map.warp[5] = {0.02f, -0.01f};
    spec.map.blend.left = 0.1f;
    spec.map.blend.power = 3.0f;
    spec.map.brightness = 0.8f;
    spec.map.flipX = true;
    const nlohmann::json doc = nvj::outputToJson(spec);
    nvj::OutputSpec back;
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back == spec);

    nvj::OutputSpec broken = nvj::defaultOutputSpec(2);
    nlohmann::json bad = {{"name", 5}, {"slice", {1, 2}}, {"corners", "x"}, {"warpColumns", 3}, {"warpRows", 3}, {"warp", {1, 2, 3}}, {"blend", 7}, {"monitor", -4}};
    CHECK(nvj::outputFromJson(bad, broken));
    CHECK(broken.name == "Output 3" && broken.monitor == 0);
    CHECK(broken.map.warp.size() == 9);
    CHECK(nvj::isDefaultMapping(broken.map) || broken.map.warpColumns == 3);
    CHECK(!nvj::outputFromJson(nlohmann::json::array(), broken));
}

void testSlices() {
    nvj::OutputSpec spec = nvj::defaultOutputSpec(0);
    CHECK(nvj::sliceCount(spec) == 1);
    CHECK(nvj::sliceName(spec, 0) == "Slice 1");
    CHECK(&nvj::sliceMapping(spec, 0) == &spec.map);
    CHECK(&nvj::sliceMapping(spec, 5) == &spec.map);
    CHECK(!nvj::removeSlice(spec, 0));
    CHECK(nvj::addSlice(spec));
    CHECK(nvj::sliceCount(spec) == 2 && nvj::sliceName(spec, 1) == "Slice 2");
    CHECK(&nvj::sliceMapping(spec, 1) == &spec.slices[0].map);
    CHECK(&nvj::sliceMapping(spec, 99) == &spec.slices[0].map);
    spec.slices[0].map.slice = {0.5f, 0.0f, 0.5f, 1.0f};
    spec.slices[0].name = "Right";
    spec.map.fill = true;
    CHECK(nvj::duplicateSlice(spec, 1));
    CHECK(nvj::sliceCount(spec) == 3 && spec.slices[1].name == "Right copy");
    CHECK(spec.slices[1].map.slice == spec.slices[0].map.slice && !spec.slices[1].map.fill);
    while (nvj::addSlice(spec)) {
    }
    CHECK(nvj::sliceCount(spec) == nvj::kMaxSlices);
    CHECK(!nvj::addSlice(spec) && !nvj::duplicateSlice(spec, 0));
    CHECK(nvj::removeSlice(spec, 0));
    CHECK(nvj::sliceCount(spec) == nvj::kMaxSlices - 1);
    const std::array<float, 4> promoted{0.5f, 0.0f, 0.5f, 1.0f};
    CHECK(spec.map.slice == promoted && spec.map.fill);
    CHECK(nvj::removeSlice(spec, 2));
    CHECK(!nvj::removeSlice(spec, 40) && !nvj::removeSlice(spec, -1));
    while (nvj::sliceCount(spec) > 1) {
        CHECK(nvj::removeSlice(spec, nvj::sliceCount(spec) - 1));
    }
    CHECK(spec.slices.empty());

    nvj::OutputSpec many = nvj::defaultOutputSpec(0);
    for (int i = 0; i < 20; ++i) {
        many.slices.push_back(nvj::OutputSlice{});
    }
    many.slices[0].name = std::string(100, 'x');
    nvj::normalizeSpec(many);
    CHECK(many.slices.size() == static_cast<size_t>(nvj::kMaxSlices - 1));
    CHECK(many.slices[0].name.size() == 40);
    CHECK(many.slices[1].name == "Slice 3");
}

void testMask() {
    nvj::SliceMask mask;
    CHECK(!nvj::maskUsable(mask));
    CHECK(nvj::maskContains(mask, 0.5f, 0.5f));
    mask.enabled = true;
    CHECK(!nvj::maskUsable(mask));
    mask.points = {{0.1f, 0.1f}, {0.9f, 0.1f}};
    nvj::normalizeMask(mask);
    CHECK(!mask.enabled);

    const nvj::SliceMask box = nvj::maskRectangle(0.25f, 0.25f, 0.75f, 0.75f);
    CHECK(nvj::maskUsable(box) && box.points.size() == 4);
    CHECK(nvj::maskContains(box, 0.5f, 0.5f));
    CHECK(!nvj::maskContains(box, 0.1f, 0.5f));
    CHECK(!nvj::maskContains(box, 0.5f, 0.9f));
    nvj::SliceMask inverted = box;
    inverted.invert = true;
    CHECK(!nvj::maskContains(inverted, 0.5f, 0.5f) && nvj::maskContains(inverted, 0.1f, 0.5f));

    const nvj::SliceMask ellipse = nvj::maskEllipse(0.5f, 0.5f, 0.4f, 0.3f, 24);
    CHECK(ellipse.points.size() == 24);
    CHECK(nvj::maskContains(ellipse, 0.5f, 0.5f));
    CHECK(nvj::maskContains(ellipse, 0.85f, 0.5f));
    CHECK(!nvj::maskContains(ellipse, 0.95f, 0.5f));
    CHECK(!nvj::maskContains(ellipse, 0.15f, 0.25f));
    CHECK(nvj::maskEllipse(0.5f, 0.5f, 0.4f, 0.3f, 999).points.size() == static_cast<size_t>(nvj::kMaxMaskPoints));
    CHECK(nvj::maskEllipse(0.5f, 0.5f, 0.4f, 0.3f, 1).points.size() == 6);

    nvj::SliceMask edit = nvj::maskRectangle(0.0f, 0.0f, 1.0f, 0.5f);
    const int added = nvj::maskInsertPoint(edit);
    CHECK(added == 1 && edit.points.size() == 5);
    CHECK(near(edit.points[1].x, 0.5f) && near(edit.points[1].y, 0.0f));
    CHECK(nvj::maskRemovePoint(edit, 1) && edit.points.size() == 4);
    CHECK(nvj::maskRemovePoint(edit, 0) && edit.points.size() == 3);
    CHECK(!nvj::maskRemovePoint(edit, 0) && edit.points.size() == 3);
    CHECK(!nvj::maskRemovePoint(edit, 7));
    nvj::SliceMask full = nvj::maskEllipse(0.5f, 0.5f, 0.4f, 0.4f, nvj::kMaxMaskPoints);
    CHECK(nvj::maskInsertPoint(full) == -1 && full.points.size() == static_cast<size_t>(nvj::kMaxMaskPoints));
    nvj::SliceMask empty;
    CHECK(nvj::maskInsertPoint(empty) == 0 && empty.points.size() == 1);

    nvj::SliceMask wild;
    wild.enabled = true;
    wild.feather = 5.0f;
    for (int i = 0; i < 100; ++i) {
        wild.points.push_back({99.0f, -99.0f});
    }
    nvj::normalizeMask(wild);
    CHECK(wild.points.size() == static_cast<size_t>(nvj::kMaxMaskPoints));
    CHECK(wild.points[0].x == 1.5f && wild.points[0].y == -0.5f && wild.feather == nvj::kMaxMaskFeather);
}

uint8_t pixel(const std::vector<uint8_t>& image, int width, int x, int y) {
    return image[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

void testMaskImage() {
    const int w = 128;
    const int h = 72;
    nvj::SliceMask none;
    const std::vector<uint8_t> open = nvj::buildMaskImage(none, w, h);
    CHECK(open.size() == static_cast<size_t>(w * h));
    CHECK(pixel(open, w, 0, 0) == 255 && pixel(open, w, 64, 36) == 255);

    nvj::SliceMask box = nvj::maskRectangle(0.25f, 0.25f, 0.75f, 0.75f);
    std::vector<uint8_t> image = nvj::buildMaskImage(box, w, h);
    CHECK(pixel(image, w, 64, 36) == 255);
    CHECK(pixel(image, w, 2, 2) == 0 && pixel(image, w, 125, 69) == 0);
    CHECK(pixel(image, w, 20, 36) == 0 && pixel(image, w, 40, 36) == 255);
    CHECK(pixel(image, w, 64, 10) == 0 && pixel(image, w, 64, 30) == 255);

    nvj::SliceMask half = nvj::maskRectangle(32.5f / 128.0f, 0.25f, 0.75f, 0.75f);
    image = nvj::buildMaskImage(half, w, h);
    CHECK(std::abs(static_cast<int>(pixel(image, w, 32, 36)) - 128) <= 2);

    box.invert = true;
    image = nvj::buildMaskImage(box, w, h);
    CHECK(pixel(image, w, 64, 36) == 0 && pixel(image, w, 2, 2) == 255);

    nvj::SliceMask soft = nvj::maskRectangle(0.25f, 0.25f, 0.75f, 0.75f);
    soft.feather = 0.2f;
    image = nvj::buildMaskImage(soft, w, h);
    CHECK(pixel(image, w, 0, 0) == 0);
    CHECK(pixel(image, w, 64, 36) > 200);
    const int edge = pixel(image, w, 32, 36);
    CHECK(edge > 90 && edge < 170);
    bool monotonic = true;
    for (int x = 1; x < 64; ++x) {
        if (pixel(image, w, x, 36) < pixel(image, w, x - 1, 36)) {
            monotonic = false;
        }
    }
    CHECK(monotonic);

    nvj::SliceMask softInverted = soft;
    softInverted.invert = true;
    const std::vector<uint8_t> flipped = nvj::buildMaskImage(softInverted, w, h);
    CHECK(pixel(image, w, 32, 36) + pixel(flipped, w, 32, 36) == 255);

    const std::vector<uint8_t> defaultSize = nvj::buildMaskImage(box);
    CHECK(defaultSize.size() == static_cast<size_t>(nvj::kMaskWidth * nvj::kMaskHeight));
    CHECK(nvj::buildMaskImage(box, 0, 0).size() == 1);
    nvj::SliceMask outside = nvj::maskRectangle(-0.5f, -0.5f, -0.2f, -0.2f);
    image = nvj::buildMaskImage(outside, w, h);
    CHECK(pixel(image, w, 64, 36) == 0 && pixel(image, w, 0, 0) == 0);
    nvj::SliceMask covering = nvj::maskRectangle(-0.5f, -0.5f, 1.5f, 1.5f);
    image = nvj::buildMaskImage(covering, w, h);
    CHECK(pixel(image, w, 0, 0) == 255 && pixel(image, w, 127, 71) == 255);
}

void testColor() {
    nvj::OutputColor color;
    CHECK(nvj::isDefaultColor(color));
    float rgb[3] = {0.2f, 0.5f, 0.9f};
    nvj::applyColor(color, rgb);
    CHECK(near(rgb[0], 0.2f) && near(rgb[1], 0.5f) && near(rgb[2], 0.9f));

    color.saturation = 0.0f;
    CHECK(!nvj::isDefaultColor(color));
    float grey[3] = {1.0f, 0.0f, 0.0f};
    nvj::applyColor(color, grey);
    CHECK(near(grey[0], 0.2126f, 1e-3f) && near(grey[1], 0.2126f, 1e-3f) && near(grey[2], 0.2126f, 1e-3f));

    nvj::OutputColor gain;
    gain.red = 0.5f;
    float tinted[3] = {0.8f, 0.8f, 0.8f};
    nvj::applyColor(gain, tinted);
    CHECK(near(tinted[0], 0.4f) && near(tinted[1], 0.8f) && near(tinted[2], 0.8f));

    nvj::OutputColor contrast;
    contrast.contrast = 2.0f;
    float low[3] = {0.25f, 0.5f, 0.75f};
    nvj::applyColor(contrast, low);
    CHECK(near(low[0], 0.0f) && near(low[1], 0.5f) && near(low[2], 1.0f));

    nvj::OutputColor gamma;
    gamma.gamma = 2.0f;
    float curve[3] = {0.25f, 1.0f, 0.0f};
    nvj::applyColor(gamma, curve);
    CHECK(near(curve[0], 0.5f) && near(curve[1], 1.0f) && near(curve[2], 0.0f));

    nvj::OutputColor lift;
    lift.black = 0.1f;
    float lifted[3] = {0.0f, 1.0f, 0.5f};
    nvj::applyColor(lift, lifted);
    CHECK(near(lifted[0], 0.1f) && near(lifted[1], 1.0f) && near(lifted[2], 0.55f));

    nvj::OutputColor wild;
    wild.contrast = 50.0f;
    wild.saturation = -3.0f;
    wild.gamma = 0.0f;
    wild.red = 9.0f;
    wild.black = 4.0f;
    nvj::normalizeColor(wild);
    CHECK(wild.contrast == 2.0f && wild.saturation == 0.0f && wild.gamma == 0.4f && wild.red == 1.5f && wild.black == 0.3f);
    float limited[3] = {1.0f, 1.0f, 1.0f};
    nvj::applyColor(wild, limited);
    for (float v : limited) {
        CHECK(v >= 0.0f && v <= 1.0f);
    }
}

void testBlackLevel() {
    nvj::EdgeBlend blend;
    blend.blackLevel = 0.1f;
    CHECK(!nvj::hasEdgeBlend(blend));
    CHECK(near(nvj::blackLift(blend, 0.5f, 0.5f), 0.0f) && near(nvj::blackLift(blend, 0.0f, 0.0f), 0.0f));
    blend.left = 0.2f;
    CHECK(nvj::hasEdgeBlend(blend));
    CHECK(near(nvj::blendZone(blend, 0.1f, 0.5f), 1.0f) && near(nvj::blendZone(blend, 0.5f, 0.5f), 0.0f));
    CHECK(near(nvj::blackLift(blend, 0.1f, 0.5f), 0.0f));
    CHECK(near(nvj::blackLift(blend, 0.5f, 0.5f), 0.1f));
    blend.bottom = 0.1f;
    CHECK(near(nvj::blackLift(blend, 0.5f, 0.95f), 0.0f) && near(nvj::blackLift(blend, 0.5f, 0.5f), 0.1f));
    nvj::OutputMapping map;
    map.blend.left = 0.1f;
    map.blend.blackLevel = 7.0f;
    nvj::normalizeMapping(map);
    CHECK(map.blend.blackLevel == 0.3f);
}

void testLargeWarp() {
    nvj::OutputMapping map;
    nvj::resizeWarp(map, 17, 17);
    CHECK(map.warpColumns == 17 && map.warpRows == 17 && map.warp.size() == 289);
    nvj::resizeWarp(map, 30, 30);
    CHECK(map.warpColumns == nvj::kMaxWarpPoints && map.warp.size() == 289);
    map.warp[8 * 17 + 8] = {0.04f, -0.02f};
    const nvj::Vec2f centre = nvj::mapPoint(map, 0.5f, 0.5f);
    CHECK(near(centre.x, 0.54f, 1e-4f) && near(centre.y, 0.48f, 1e-4f));
    CHECK(near(nvj::mapPoint(map, 0.0f, 0.0f).x, 0.0f) && near(nvj::mapPoint(map, 1.0f, 1.0f).y, 1.0f));

    nvj::OutputMapping plain;
    CHECK(nvj::meshDivisionsFor(plain) == nvj::kMeshDivisions);
    nvj::resizeWarp(plain, 9, 9);
    CHECK(nvj::meshDivisionsFor(plain) == nvj::kMeshDivisions);
    nvj::resizeWarp(plain, 11, 5);
    CHECK(nvj::meshDivisionsFor(plain) == 40);
    CHECK(nvj::meshDivisionsFor(map) == 64);
    nvj::OutputMapping extreme = map;
    extreme.warpColumns = 1000;
    CHECK(nvj::meshDivisionsFor(extreme) == nvj::kMaxMeshDivisions);
    const int divisions = nvj::meshDivisionsFor(map);
    const std::vector<nvj::MeshVertex> mesh = nvj::buildMesh(map, divisions);
    CHECK(mesh.size() == static_cast<size_t>((divisions + 1) * (divisions + 1)));
    const std::vector<uint32_t> indices = nvj::buildMeshIndices(divisions);
    CHECK(indices.size() == static_cast<size_t>(divisions * divisions * 6));
    for (const uint32_t index : indices) {
        CHECK(index < mesh.size());
    }
}

void testJsonSlicesMaskColor() {
    nvj::OutputSpec spec = nvj::defaultOutputSpec(0);
    spec.color.saturation = 0.7f;
    spec.color.red = 0.9f;
    spec.color.gamma = 1.2f;
    spec.color.black = 0.02f;
    spec.map.mask = nvj::maskEllipse(0.5f, 0.5f, 0.4f, 0.4f, 12);
    spec.map.mask.feather = 0.05f;
    spec.map.blend.left = 0.1f;
    spec.map.blend.blackLevel = 0.04f;
    CHECK(nvj::addSlice(spec));
    spec.slices[0].name = "Wall";
    spec.slices[0].enabled = false;
    spec.slices[0].map.slice = {0.5f, 0.5f, 0.5f, 0.5f};
    spec.slices[0].map.mask = nvj::maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    spec.slices[0].map.mask.invert = true;
    nvj::resizeWarp(spec.slices[0].map, 17, 17);
    spec.slices[0].map.warp[40] = {0.01f, 0.02f};
    const nlohmann::json doc = nvj::outputToJson(spec);
    nvj::OutputSpec back = nvj::defaultOutputSpec(0);
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back == spec);
    CHECK(back.slices.size() == 1 && back.slices[0].name == "Wall" && !back.slices[0].enabled);
    CHECK(back.slices[0].map.warpColumns == 17 && back.slices[0].map.mask.invert);

    nlohmann::json legacy = nvj::outputToJson(nvj::defaultOutputSpec(1));
    legacy.erase("color");
    legacy.erase("slices");
    legacy.erase("mask");
    legacy["blend"].erase("blackLevel");
    nvj::OutputSpec fresh = nvj::defaultOutputSpec(1);
    nvj::OutputSpec loaded = fresh;
    loaded.map.mask = nvj::maskRectangle(0.0f, 0.0f, 1.0f, 1.0f);
    loaded.color.red = 0.3f;
    loaded.slices.resize(2);
    CHECK(nvj::outputFromJson(legacy, loaded));
    CHECK(loaded == fresh);

    nlohmann::json damaged = nvj::outputToJson(nvj::defaultOutputSpec(2));
    damaged["slices"] = {5, "x", {{"name", 7}, {"slice", "no"}, {"mask", 3}}, nullptr, {{"mask", {{"points", {1, {2}, {0.1f, 0.1f}, {0.9f, 0.1f}, {0.5f, 0.9f}}}, {"enabled", true}, {"feather", "x"}}}}};
    damaged["color"] = {{"saturation", "x"}, {"red", 99}, {"gamma", nullptr}};
    damaged["mask"] = {{"points", "nope"}, {"enabled", true}};
    nvj::OutputSpec target = nvj::defaultOutputSpec(2);
    CHECK(nvj::outputFromJson(damaged, target));
    CHECK(target.slices.size() == 2);
    CHECK(target.slices[0].name == "Slice 2" && nvj::isDefaultMapping(target.slices[0].map));
    CHECK(target.slices[1].map.mask.enabled && target.slices[1].map.mask.points.size() == 3);
    CHECK(target.color.saturation == 1.0f && target.color.red == 1.5f && target.color.gamma == 1.0f);
    CHECK(!target.map.mask.enabled && target.map.mask.points.empty());

    nlohmann::json flood = nvj::outputToJson(nvj::defaultOutputSpec(0));
    nlohmann::json many = nlohmann::json::array();
    for (int i = 0; i < 30; ++i) {
        many.push_back(nlohmann::json::object());
    }
    flood["slices"] = many;
    nvj::OutputSpec capped;
    CHECK(nvj::outputFromJson(flood, capped));
    CHECK(capped.slices.size() == static_cast<size_t>(nvj::kMaxSlices - 1));
}

void testSettingsRoundTrip() {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nyrexvj_outputs_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", scratch.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", scratch.string().c_str(), 1);
#endif
    nvj::Settings settings;
    CHECK(settings.outputs[0].enabled && !settings.outputs[1].enabled);
    settings.outputs[1].enabled = true;
    settings.outputs[1].monitor = 2;
    settings.outputs[1].map.blend.right = 0.15f;
    settings.outputs[2].map.corners[1] = {0.9f, 0.05f};
    settings.outputs[0].monitor = 3;
    CHECK(nvj::saveSettings(settings));
    nvj::Settings loaded;
    CHECK(nvj::loadSettings(loaded));
    CHECK(loaded.outputs == settings.outputs);
    CHECK(loaded.outputs[0].monitor == 3);

    nvj::Settings legacy;
    legacy.outputs[0].monitor = 1;
    CHECK(nvj::saveSettings(legacy));
    std::filesystem::path file = scratch / "settings.json";
    nlohmann::json doc;
    {
        std::ifstream in(file);
        doc = nlohmann::json::parse(in);
    }
    doc.erase("outputs");
    doc["outputMonitor"] = 2;
    {
        std::ofstream out(file, std::ios::trunc);
        out << doc.dump();
    }
    nvj::Settings old;
    CHECK(nvj::loadSettings(old));
    CHECK(old.outputs[0].monitor == 2 && old.outputs[0].enabled);
    CHECK(old.outputs[1] == nvj::defaultOutputSpec(1));
}

std::vector<uint8_t> makeTga(int width, int height, bool alpha, const std::function<void(int, int, uint8_t*)>& pixel) {
    std::vector<uint8_t> bytes(18, 0);
    bytes[2] = 2;
    bytes[12] = static_cast<uint8_t>(width & 0xff);
    bytes[13] = static_cast<uint8_t>(width >> 8);
    bytes[14] = static_cast<uint8_t>(height & 0xff);
    bytes[15] = static_cast<uint8_t>(height >> 8);
    bytes[16] = alpha ? 32 : 24;
    bytes[17] = static_cast<uint8_t>(0x20 | (alpha ? 8 : 0));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t rgba[4] = {0, 0, 0, 255};
            pixel(x, y, rgba);
            bytes.push_back(rgba[2]);
            bytes.push_back(rgba[1]);
            bytes.push_back(rgba[0]);
            if (alpha) {
                bytes.push_back(rgba[3]);
            }
        }
    }
    return bytes;
}

std::vector<uint8_t> halfWhiteTga() {
    return makeTga(64, 36, false, [](int x, int, uint8_t* p) { p[0] = p[1] = p[2] = x < 32 ? 255 : 0; });
}

std::vector<uint8_t> topOpaqueTga() {
    return makeTga(64, 36, true, [](int, int y, uint8_t* p) {
        p[0] = p[1] = p[2] = 255;
        p[3] = y < 18 ? 255 : 0;
    });
}

void writeBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void setDataDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

float polygonArea(const std::vector<nvj::Vec2f>& points) {
    float sum = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const nvj::Vec2f& a = points[i];
        const nvj::Vec2f& b = points[(i + 1) % points.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::fabs(sum) * 0.5f;
}

void testBezierMask() {
    nvj::SliceMask box = nvj::maskRectangle(0.2f, 0.2f, 0.8f, 0.8f);
    CHECK(!nvj::maskHasCurves(box));
    CHECK(nvj::maskOutline(box).size() == 4);
    CHECK(nvj::maskContains(box, 0.5f, 0.5f) && !nvj::maskContains(box, 0.42f, 0.17f));

    nvj::SliceMask curved = box;
    nvj::maskSmooth(curved, 1.0f);
    CHECK(nvj::maskHasCurves(curved) && curved.handles.size() == 4);
    for (const nvj::MaskHandle& handle : curved.handles) {
        CHECK(near(handle.in.x, -handle.out.x) && near(handle.in.y, -handle.out.y));
    }
    const std::vector<nvj::Vec2f> outline = nvj::maskOutline(curved);
    CHECK(outline.size() == 4u * static_cast<size_t>(nvj::kBezierSegments));
    for (const nvj::Vec2f& corner : box.points) {
        float best = 1.0e9f;
        for (const nvj::Vec2f& p : outline) {
            best = std::min(best, std::hypot(p.x - corner.x, p.y - corner.y));
        }
        CHECK(best < 1e-4f);
    }
    CHECK(nvj::maskContains(curved, 0.42f, 0.17f));
    CHECK(nvj::maskContains(curved, 0.5f, 0.5f) && !nvj::maskContains(curved, 0.05f, 0.05f));
    CHECK(polygonArea(outline) > polygonArea(box.points));

    nvj::SliceMask zero = curved;
    nvj::maskSmooth(zero, 0.0f);
    CHECK(!nvj::maskHasCurves(zero));

    nvj::SliceMask inserted = curved;
    const int at = nvj::maskInsertPoint(inserted);
    CHECK(at >= 0 && inserted.points.size() == 5 && inserted.handles.size() == 5);
    const float areaBefore = polygonArea(nvj::maskOutline(curved));
    const float areaAfter = polygonArea(nvj::maskOutline(inserted));
    CHECK(std::fabs(areaAfter - areaBefore) < areaBefore * 0.01f);
    for (float probe : {0.3f, 0.5f, 0.7f}) {
        CHECK(nvj::maskContains(inserted, probe, 0.15f) == nvj::maskContains(curved, probe, 0.15f));
        CHECK(nvj::maskContains(inserted, probe, 0.5f) && !nvj::maskContains(inserted, probe, 0.02f));
    }
    CHECK(nvj::maskRemovePoint(inserted, at));
    CHECK(inserted.points.size() == 4 && inserted.handles.size() == 4);

    nvj::SliceMask cleared = curved;
    nvj::maskClearCurves(cleared);
    CHECK(!nvj::maskHasCurves(cleared) && nvj::maskOutline(cleared).size() == 4);

    nvj::SliceMask triangle;
    triangle.enabled = true;
    triangle.points = {{0.2f, 0.2f}, {0.8f, 0.2f}, {0.5f, 0.8f}};
    nvj::maskSmooth(triangle, 1.0f);
    CHECK(triangle.handles.size() == 3 && nvj::maskOutline(triangle).size() == 3u * static_cast<size_t>(nvj::kBezierSegments));

    nvj::SliceMask wrong = box;
    wrong.handles.assign(7, nvj::MaskHandle{{0.1f, 0.0f}, {0.0f, 0.1f}});
    nvj::normalizeMask(wrong);
    CHECK(wrong.handles.empty());

    nvj::SliceMask huge = box;
    huge.handles.assign(4, nvj::MaskHandle{{50.0f, -50.0f}, {9.0f, 9.0f}});
    nvj::normalizeMask(huge);
    CHECK(huge.handles.size() == 4 && near(huge.handles[0].in.x, 1.0f) && near(huge.handles[0].in.y, -1.0f) && near(huge.handles[0].out.x, 1.0f));

    const std::vector<uint8_t> image = nvj::buildMaskImage(curved, 256, 144);
    CHECK(pixel(image, 256, static_cast<int>(0.42f * 256.0f), static_cast<int>(0.17f * 144.0f)) == 255);
    const std::vector<uint8_t> straight = nvj::buildMaskImage(box, 256, 144);
    CHECK(pixel(straight, 256, static_cast<int>(0.42f * 256.0f), static_cast<int>(0.17f * 144.0f)) == 0);
}

void testFreehandStroke() {
    std::vector<nvj::Vec2f> stroke;
    for (int i = 0; i <= 600; ++i) {
        const float a = static_cast<float>(i) / 600.0f * 6.2831853f;
        const float wobble = 0.003f * std::sin(a * 31.0f);
        stroke.push_back({0.5f + (0.3f + wobble) * std::cos(a), 0.5f + (0.3f + wobble) * std::sin(a)});
    }
    nvj::SliceMask mask;
    CHECK(nvj::maskFromStroke(mask, stroke));
    CHECK(mask.enabled && mask.points.size() >= 3 && mask.points.size() <= static_cast<size_t>(nvj::kMaxMaskPoints));
    CHECK(!nvj::maskHasCurves(mask) && mask.image.empty());
    const float circle = 3.14159265f * 0.3f * 0.3f;
    CHECK(std::fabs(polygonArea(mask.points) - circle) < circle * 0.06f);
    CHECK(nvj::maskContains(mask, 0.5f, 0.5f) && !nvj::maskContains(mask, 0.05f, 0.05f));
    CHECK(near(mask.points.front().x, mask.points.back().x, 1e-3f) == false || near(mask.points.front().y, mask.points.back().y, 1e-3f) == false);

    const std::vector<nvj::Vec2f> few = nvj::simplifyStroke(stroke, 8);
    CHECK(few.size() >= 3 && few.size() <= 8);

    std::vector<nvj::Vec2f> line;
    for (int i = 0; i < 100; ++i) {
        line.push_back({0.1f + 0.008f * static_cast<float>(i), 0.5f});
    }
    nvj::SliceMask flat;
    CHECK(!nvj::maskFromStroke(flat, line));
    CHECK(nvj::simplifyStroke(line, nvj::kMaxMaskPoints).size() <= 2);

    nvj::SliceMask tiny;
    CHECK(!nvj::maskFromStroke(tiny, {{0.5f, 0.5f}, {0.5001f, 0.5001f}}));
    CHECK(!nvj::maskFromStroke(tiny, {}));
    CHECK(!tiny.enabled);

    nvj::SliceMask replaced = nvj::maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    nvj::maskSmooth(replaced, 1.0f);
    replaced.image = "old.png";
    CHECK(nvj::maskFromStroke(replaced, stroke));
    CHECK(!nvj::maskHasCurves(replaced) && replaced.image.empty());

    std::vector<nvj::Vec2f> many;
    for (int i = 0; i < 20000; ++i) {
        const float a = static_cast<float>(i) / 20000.0f * 6.2831853f;
        many.push_back({0.5f + 0.35f * std::cos(a) + 0.002f * std::sin(a * 200.0f), 0.5f + 0.35f * std::sin(a)});
    }
    const auto begin = std::chrono::steady_clock::now();
    nvj::SliceMask big;
    CHECK(nvj::maskFromStroke(big, many) && big.points.size() <= static_cast<size_t>(nvj::kMaxMaskPoints));
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    CHECK(elapsed < 500.0);
}

void testMaskBitmap() {
    nvj::MaskBitmap luma;
    std::string error;
    const std::vector<uint8_t> lumaFile = halfWhiteTga();
    CHECK(nvj::decodeMaskBitmap(lumaFile.data(), lumaFile.size(), luma, error));
    CHECK(luma.width == nvj::kMaskWidth && luma.height == nvj::kMaskHeight);
    CHECK(luma.coverage.size() == static_cast<size_t>(nvj::kMaskWidth * nvj::kMaskHeight));
    CHECK(pixel(luma.coverage, luma.width, 100, 300) == 255 && pixel(luma.coverage, luma.width, 900, 300) == 0);
    const int middle = pixel(luma.coverage, luma.width, 512, 300);
    CHECK(middle > 20 && middle < 235);

    nvj::MaskBitmap alpha;
    const std::vector<uint8_t> alphaFile = topOpaqueTga();
    CHECK(nvj::decodeMaskBitmap(alphaFile.data(), alphaFile.size(), alpha, error));
    CHECK(pixel(alpha.coverage, alpha.width, 500, 50) == 255 && pixel(alpha.coverage, alpha.width, 500, 520) == 0);

    nvj::MaskBitmap bad;
    const std::vector<uint8_t> garbage(200, 0x5a);
    error.clear();
    CHECK(!nvj::decodeMaskBitmap(garbage.data(), garbage.size(), bad, error) && !error.empty());
    error.clear();
    CHECK(!nvj::decodeMaskBitmap(nullptr, 0, bad, error) && !error.empty());
    std::vector<uint8_t> truncated(lumaFile.begin(), lumaFile.begin() + 60);
    error.clear();
    CHECK(!nvj::decodeMaskBitmap(truncated.data(), truncated.size(), bad, error));

    nvj::SliceMask mask;
    mask.enabled = true;
    mask.image = "shape.png";
    const int w = 128;
    const int h = 72;
    CHECK(nvj::maskIsImage(mask) && nvj::maskUsable(mask) && nvj::maskContains(mask, 0.5f, 0.5f));
    std::vector<uint8_t> hidden = nvj::buildMaskImage(mask, w, h);
    CHECK(pixel(hidden, w, 10, 10) == 0 && pixel(hidden, w, 100, 50) == 0);
    mask.invert = true;
    hidden = nvj::buildMaskImage(mask, w, h);
    CHECK(pixel(hidden, w, 10, 10) == 0);
    mask.invert = false;

    std::vector<uint8_t> shown = nvj::buildMaskImage(mask, w, h, &luma);
    CHECK(pixel(shown, w, 10, 36) == 255 && pixel(shown, w, 118, 36) == 0);
    mask.invert = true;
    shown = nvj::buildMaskImage(mask, w, h, &luma);
    CHECK(pixel(shown, w, 10, 36) == 0 && pixel(shown, w, 118, 36) == 255);
    mask.invert = false;
    mask.feather = 0.1f;
    shown = nvj::buildMaskImage(mask, w, h, &luma);
    CHECK(pixel(shown, w, 4, 36) > 200 && pixel(shown, w, 124, 36) < 50);
    const int soft = pixel(shown, w, 64, 36);
    CHECK(soft > 60 && soft < 200);

    nvj::MaskBitmap broken;
    broken.width = 10;
    broken.height = 10;
    broken.coverage.assign(5, 255);
    mask.feather = 0.0f;
    const std::vector<uint8_t> refused = nvj::buildMaskImage(mask, w, h, &broken);
    CHECK(pixel(refused, w, 10, 36) == 0);

    mask.enabled = false;
    const std::vector<uint8_t> off = nvj::buildMaskImage(mask, w, h, &luma);
    CHECK(pixel(off, w, 118, 36) == 255);

    CHECK(nvj::maskImageNameValid("wall.png") && nvj::maskImageNameValid("Pillar 2.JPG") && nvj::maskImageNameValid("a.tga") && nvj::maskImageNameValid("b.bmp") && nvj::maskImageNameValid("c.jpeg"));
    CHECK(!nvj::maskImageNameValid("") && !nvj::maskImageNameValid("noext") && !nvj::maskImageNameValid("..") && !nvj::maskImageNameValid("a.png.exe"));
    CHECK(!nvj::maskImageNameValid("../x.png") && !nvj::maskImageNameValid("a/b.png") && !nvj::maskImageNameValid("a\\b.png") && !nvj::maskImageNameValid("c:x.png") && !nvj::maskImageNameValid("x.gif"));
    CHECK(!nvj::maskImageNameValid(std::string(nvj::kMaxMaskImageNameLength, 'a') + ".png") && !nvj::maskImageNameValid("bad\nname.png"));
}

void testMaskImageCache() {
    namespace fs = std::filesystem;
    const fs::path data = fs::temp_directory_path() / "nyrexvj-mask-cache-test";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data, ec);
    setDataDirectory(data);
    nvj::MaskImages& images = nvj::MaskImages::instance();
    images.clear();
    CHECK(images.folder() == data / "masks");

    const fs::path source = data / "source wall.tga";
    writeBytes(source, halfWhiteTga());
    std::string name;
    std::string error;
    CHECK(images.importFile(source.string(), name, error));
    CHECK(name == "source wall.tga" && fs::exists(images.folder() / name));
    std::string again;
    CHECK(images.importFile(source.string(), again, error) && again == "source wall 2.tga" && fs::exists(images.folder() / again));
    CHECK(nvj::maskImageNameValid(name) && nvj::maskImageNameValid(again));

    images.clear();
    CHECK(images.get(name) == nullptr);
    CHECK(images.waitFor(name, 10000));
    const std::shared_ptr<const nvj::MaskBitmap> bitmap = images.get(name);
    CHECK(bitmap != nullptr && bitmap->width == nvj::kMaskWidth && images.error(name).empty() && !images.loading(name));
    CHECK(bitmap && pixel(bitmap->coverage, bitmap->width, 100, 300) == 255 && pixel(bitmap->coverage, bitmap->width, 900, 300) == 0);
    CHECK(images.get(name) == bitmap);

    CHECK(images.get("not-there.png") == nullptr);
    CHECK(!images.waitFor("not-there.png", 10000) && !images.loading("not-there.png"));
    CHECK(images.get("not-there.png") == nullptr && !images.error("not-there.png").empty());
    CHECK(images.get("../escape.png") == nullptr);

    writeBytes(images.folder() / "damaged.png", std::vector<uint8_t>(300, 0x11));
    CHECK(images.get("damaged.png") == nullptr);
    CHECK(!images.waitFor("damaged.png", 10000) && !images.loading("damaged.png"));
    CHECK(images.get("damaged.png") == nullptr && !images.error("damaged.png").empty());

    writeBytes(images.folder() / "later.png", halfWhiteTga());
    images.reload("not-there.png");
    CHECK(!images.waitFor("not-there.png", 10000) && !images.error("not-there.png").empty());

    const fs::path text = data / "notes.txt";
    writeBytes(text, std::vector<uint8_t>{'h', 'i'});
    std::string refused;
    error.clear();
    CHECK(!images.importFile(text.string(), refused, error) && refused.empty() && !error.empty());
    error.clear();
    CHECK(!images.importFile((data / "missing.png").string(), refused, error) && error == "The file does not exist");

    const fs::path huge = data / "huge.tga";
    writeBytes(huge, std::vector<uint8_t>(nvj::kMaxMaskFileBytes + 1024u, 0));
    error.clear();
    CHECK(!images.importFile(huge.string(), refused, error) && !error.empty());

    images.clear();
    fs::remove_all(data, ec);
}

void testBudget() {
    CHECK(nvj::ndiReadbackBytes(1920, 1080) == 2u * 1920u * 1080u * 4u);
    CHECK(nvj::ndiReadbackBytes(nvj::kMaxNdiFrameSize, nvj::kMaxNdiFrameSize) == 2u * 4096u * 4096u * 4u);
    CHECK(nvj::ndiReadbackBytes(nvj::kMaxNdiFrameSize + 1, 100) == 0 && nvj::ndiReadbackBytes(100, nvj::kMaxNdiFrameSize + 1) == 0);
    CHECK(nvj::ndiReadbackBytes(0, 100) == 0 && nvj::ndiReadbackBytes(100, -4) == 0);
    CHECK(nvj::defaultNdiName(0) == "NyrexVJ Output 1" && nvj::defaultNdiName(3) == "NyrexVJ Output 4");

    std::array<nvj::OutputSpec, nvj::kMaxOutputs> specs = nvj::defaultOutputs();
    nvj::OutputBudget base = nvj::estimateOutputBudget(specs, 1920, 1080, 1920, 1080);
    CHECK(base.meshBytes > 0 && base.maskBytes == 0 && base.delayBytes == 0 && base.ndiBytes == 0);
    CHECK(base.total() == base.meshBytes);

    specs[0].map.mask = nvj::maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    nvj::OutputBudget hard = nvj::estimateOutputBudget(specs, 1920, 1080, 1920, 1080);
    CHECK(hard.maskBytes == static_cast<size_t>(nvj::kMaskWidth) * static_cast<size_t>(nvj::kMaskHeight));
    specs[0].map.mask.feather = 0.05f;
    nvj::OutputBudget soft = nvj::estimateOutputBudget(specs, 1920, 1080, 1920, 1080);
    CHECK(soft.maskBytes == static_cast<size_t>(nvj::kMaskWidth / 2) * static_cast<size_t>(nvj::kMaskHeight / 2));
    specs[0].map.mask.feather = 0.0f;
    specs[0].map.mask.points.clear();
    specs[0].map.mask.image = "wall.png";
    CHECK(nvj::estimateOutputBudget(specs, 1920, 1080, 1920, 1080).maskBytes == hard.maskBytes);
    specs[0].map.mask = nvj::SliceMask{};

    specs[1].enabled = true;
    specs[1].ndi = true;
    specs[1].delayFrames = 3;
    nvj::OutputBudget second = nvj::estimateOutputBudget(specs, 1920, 1080, 1280, 720);
    CHECK(second.ndiBytes == nvj::ndiReadbackBytes(1280, 720));
    CHECK(second.delayBytes == 4u * 1920u * 1080u * 4u);
    CHECK(second.total() == second.meshBytes + second.maskBytes + second.delayBytes + second.ndiBytes);
    specs[1].enabled = false;
    CHECK(nvj::estimateOutputBudget(specs, 1920, 1080, 1280, 720).ndiBytes == 0);
    CHECK(nvj::estimateOutputBudget(specs, 1920, 1080, 1280, 720).delayBytes == 0);
    specs[0].ndi = true;
    CHECK(nvj::estimateOutputBudget(specs, 1920, 1080, 5000, 720).ndiBytes == 0);

    nvj::addSlice(specs[0]);
    const size_t withSlice = nvj::estimateOutputBudget(specs, 1920, 1080, 1280, 720).meshBytes;
    specs[0].slices[0].enabled = false;
    CHECK(nvj::estimateOutputBudget(specs, 1920, 1080, 1280, 720).meshBytes < withSlice);

    std::array<nvj::OutputSpec, nvj::kMaxOutputs> heavy = nvj::defaultOutputs();
    for (nvj::OutputSpec& spec : heavy) {
        spec.enabled = true;
        spec.ndi = true;
        spec.delayFrames = nvj::kMaxDelayFrames;
    }
    CHECK(nvj::estimateOutputBudget(heavy, 3840, 2160, 3840, 2160).total() > nvj::kOutputGpuBudgetBytes);
    CHECK(nvj::estimateOutputBudget(heavy, 1920, 1080, 1920, 1080).total() < nvj::kOutputGpuBudgetBytes);
    CHECK(nvj::kOutputGpuBudgetBytes == 384u * 1024u * 1024u);
}

void testAdvancedJson() {
    nvj::OutputSpec plain = nvj::defaultOutputSpec(0);
    plain.color.saturation = 0.8f;
    plain.map.mask = nvj::maskEllipse(0.5f, 0.5f, 0.4f, 0.4f, 12);
    plain.map.mask.feather = 0.03f;
    nvj::addSlice(plain);
    plain.slices[0].map.mask = nvj::maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    const std::string text = nvj::outputToJson(plain).dump();
    CHECK(text.find("sliceColor") == std::string::npos && text.find("ndi") == std::string::npos);
    CHECK(text.find("curves") == std::string::npos && text.find("image") == std::string::npos);
    nvj::OutputSpec reread = nvj::defaultOutputSpec(0);
    CHECK(nvj::outputFromJson(nlohmann::json::parse(text), reread) && reread == plain);
    CHECK(!reread.ndi && reread.ndiName.empty() && nvj::isDefaultColor(reread.map.sliceColor));
    CHECK(nvj::outputToJson(reread).dump() == text);

    nvj::OutputSpec rich = plain;
    rich.ndi = true;
    rich.ndiName = "Left wall";
    rich.map.sliceColor.red = 0.8f;
    rich.map.sliceColor.gamma = 1.3f;
    nvj::maskSmooth(rich.map.mask, 0.8f);
    rich.slices[0].map.sliceColor.saturation = 0.5f;
    rich.slices[0].map.mask = nvj::SliceMask{};
    rich.slices[0].map.mask.enabled = true;
    rich.slices[0].map.mask.image = "wall mask.png";
    rich.slices[0].map.mask.feather = 0.02f;
    const nlohmann::json doc = nvj::outputToJson(rich);
    nvj::OutputSpec back = nvj::defaultOutputSpec(0);
    CHECK(nvj::outputFromJson(nlohmann::json::parse(doc.dump()), back));
    CHECK(back == rich);
    CHECK(back.ndi && back.ndiName == "Left wall" && back.map.mask.handles.size() == rich.map.mask.points.size());
    CHECK(back.slices[0].map.mask.image == "wall mask.png" && near(back.map.sliceColor.red, 0.8f));
    CHECK(nvj::outputToJson(back) == doc);

    nlohmann::json damaged = nvj::outputToJson(nvj::defaultOutputSpec(1));
    damaged["ndi"] = 5;
    nvj::OutputSpec target = nvj::defaultOutputSpec(1);
    target.ndi = true;
    target.ndiName = "stale";
    CHECK(nvj::outputFromJson(damaged, target));
    CHECK(!target.ndi && target.ndiName.empty());
    damaged["ndi"] = {{"enabled", "yes"}, {"name", 7}};
    CHECK(nvj::outputFromJson(damaged, target) && !target.ndi && target.ndiName.empty());
    damaged["ndi"] = {{"enabled", true}, {"name", std::string(300, 'x')}};
    CHECK(nvj::outputFromJson(damaged, target) && target.ndi && target.ndiName.size() <= 60);

    damaged = nvj::outputToJson(nvj::defaultOutputSpec(1));
    damaged["mask"] = {{"enabled", true}, {"points", nlohmann::json::array({{0.1, 0.1}, {0.9, 0.1}, {0.5, 0.9}})}, {"image", "../../secret.png"}, {"curves", {1, 2}}};
    CHECK(nvj::outputFromJson(damaged, target));
    CHECK(target.map.mask.image.empty() && target.map.mask.handles.empty() && target.map.mask.points.size() == 3 && target.map.mask.enabled);
    damaged["mask"] = {{"enabled", true}, {"points", nlohmann::json::array({{0.1, 0.1}, {0.9, 0.1}, {0.5, 0.9}})}, {"curves", {{0.1, 0.0, -0.1, 0.0}, "x", {1, 2, 3}}}};
    CHECK(nvj::outputFromJson(damaged, target) && target.map.mask.handles.size() == 3);
    CHECK(target.map.mask.handles[1] == nvj::MaskHandle{} && target.map.mask.handles[2] == nvj::MaskHandle{});
    damaged["mask"] = {{"enabled", true}, {"image", "ok.png"}};
    CHECK(nvj::outputFromJson(damaged, target) && target.map.mask.enabled && target.map.mask.image == "ok.png");
    damaged["mask"] = {{"enabled", true}, {"image", 12}};
    CHECK(nvj::outputFromJson(damaged, target) && !target.map.mask.enabled && target.map.mask.image.empty());
    damaged["sliceColor"] = {{"red", 99}, {"gamma", "x"}};
    CHECK(nvj::outputFromJson(damaged, target) && near(target.map.sliceColor.red, 1.5f) && near(target.map.sliceColor.gamma, 1.0f));
    damaged["sliceColor"] = "broken";
    CHECK(nvj::outputFromJson(damaged, target) && nvj::isDefaultColor(target.map.sliceColor));
}

}

int main() {
    testDefaultMesh();
    testCornerPin();
    testPerspective();
    testWarp();
    testSliceAndFlip();
    testNormalize();
    testEdgeBlend();
    testLayout();
    testJson();
    testSlices();
    testMask();
    testMaskImage();
    testColor();
    testBlackLevel();
    testLargeWarp();
    testJsonSlicesMaskColor();
    testSettingsRoundTrip();
    testBezierMask();
    testFreehandStroke();
    testMaskBitmap();
    testMaskImageCache();
    testBudget();
    testAdvancedJson();
    return testing::finish();
}

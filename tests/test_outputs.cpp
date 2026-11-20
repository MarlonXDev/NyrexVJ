#include "check.hpp"

#include "core/output_map.hpp"
#include "core/output_map_json.hpp"
#include "core/settings.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

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
    return testing::finish();
}

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
    testSettingsRoundTrip();
    return testing::finish();
}

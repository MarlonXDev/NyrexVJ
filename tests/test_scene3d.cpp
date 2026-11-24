#include "check.hpp"

#include "scene3d/gltf.hpp"
#include "scene3d/gltf_compression.hpp"
#include "scene3d/procedural.hpp"
#include "scene3d/scene_math.hpp"
#include "scene3d/scene_model.hpp"

#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace nvj;
using json = nlohmann::json;

bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

bool nearVec(Vec3 a, Vec3 b, float eps = 0.001f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

struct Blob {
    json doc;
    std::vector<uint8_t> bin;
};

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

void alignBlob(std::vector<uint8_t>& bin) {
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
}

Blob buildMesh(const std::vector<Vec3>& positions, const std::vector<uint16_t>& indices, int mode = 4) {
    Blob blob;
    std::vector<float> flat;
    for (const Vec3& p : positions) {
        flat.insert(flat.end(), {p.x, p.y, p.z});
    }
    appendBytes(blob.bin, flat.data(), flat.size() * 4);
    const size_t indexOffset = blob.bin.size();
    appendBytes(blob.bin, indices.data(), indices.size() * 2);
    const size_t indexBytes = indices.size() * 2;
    alignBlob(blob.bin);
    blob.doc = json::parse(R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "buffers": [{"byteLength": 0}],
        "bufferViews": [],
        "accessors": []
    })");
    blob.doc["meshes"][0]["primitives"][0]["mode"] = mode;
    blob.doc["buffers"][0]["byteLength"] = blob.bin.size();
    blob.doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", 0}, {"byteLength", flat.size() * 4}});
    blob.doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", indexOffset}, {"byteLength", indexBytes}});
    blob.doc["accessors"].push_back({{"bufferView", 0}, {"componentType", 5126}, {"count", positions.size()}, {"type", "VEC3"}});
    blob.doc["accessors"].push_back({{"bufferView", 1}, {"componentType", 5123}, {"count", indices.size()}, {"type", "SCALAR"}});
    return blob;
}

Blob triangleBlob() {
    return buildMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

std::vector<uint8_t> glbFrom(const json& doc, const std::vector<uint8_t>& bin) {
    std::string text = doc.dump();
    while (text.size() % 4 != 0) {
        text.push_back(' ');
    }
    std::vector<uint8_t> padded = bin;
    alignBlob(padded);
    std::vector<uint8_t> out;
    const uint32_t total = static_cast<uint32_t>(12 + 8 + text.size() + (padded.empty() ? 0 : 8 + padded.size()));
    appendU32(out, 0x46546C67u);
    appendU32(out, 2);
    appendU32(out, total);
    appendU32(out, static_cast<uint32_t>(text.size()));
    appendU32(out, 0x4E4F534Au);
    out.insert(out.end(), text.begin(), text.end());
    if (!padded.empty()) {
        appendU32(out, static_cast<uint32_t>(padded.size()));
        appendU32(out, 0x004E4942u);
        out.insert(out.end(), padded.begin(), padded.end());
    }
    return out;
}

GltfResult loadBlob(const Blob& blob, const GltfLimits& limits = {}) {
    const std::vector<uint8_t> glb = glbFrom(blob.doc, blob.bin);
    return loadGltfMemory(glb.data(), glb.size(), std::filesystem::path(), limits);
}

std::string base64(const std::vector<uint8_t>& data) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
        const uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(table[(triple >> 18) & 63]);
        out.push_back(table[(triple >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? table[(triple >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? table[triple & 63] : '=');
    }
    return out;
}

void testMath() {
    const Quat q = axisAngle({0, 0, 1}, 1.5707963f);
    const Vec3 rotated = transformPoint(rotation(q), {1, 0, 0});
    CHECK(nearVec(rotated, {0, 1, 0}));

    const Quat composed = axisAngle({0, 0, 1}, 1.5707963f) * axisAngle({0, 0, 1}, 1.5707963f);
    CHECK(nearVec(transformPoint(rotation(composed), {1, 0, 0}), {-1, 0, 0}));

    const Mat4 trs = composeTrs({1, 2, 3}, Quat{}, {2, 2, 2});
    CHECK(nearVec(transformPoint(trs, {1, 1, 1}), {3, 4, 5}));

    const Mat4 view = lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(nearVec(transformPoint(view, {0, 0, 0}), {0, 0, -5}));

    const Mat4 projection = perspective(1.0f, 1.0f, 0.1f, 100.0f);
    CHECK(projection.m[11] == -1.0f);

    const Quat half = slerp(Quat{}, axisAngle({0, 1, 0}, 1.0f), 0.5f);
    CHECK(nearVec(transformPoint(rotation(half), {1, 0, 0}), transformPoint(rotation(axisAngle({0, 1, 0}, 0.5f)), {1, 0, 0})));

    const std::array<float, 9> identity = normalMatrix(Mat4{});
    CHECK(near(identity[0], 1.0f) && near(identity[4], 1.0f) && near(identity[8], 1.0f));
}

void testProcedural() {
    CHECK(builtinScenes().size() == 7);
    size_t sceneIndex = 0;
    for (const BuiltinSceneInfo& info : builtinScenes()) {
        const bool classic = sceneIndex++ < 3;
        const Scene scene = info.build();
        std::string error;
        CHECK(validateScene(scene, error));
        CHECK(scene.paletteTint == classic);
        CHECK((scene.shading == Shading::Pbr) == !classic);
        const SceneStats stats = sceneStats(scene);
        CHECK(stats.vertices > 50);
        CHECK(stats.triangles > 20);
        CHECK(stats.gpuBytes > stats.vertices * 40);
        const SceneBounds bounds = sceneBounds(scene, -1, 0.0f);
        CHECK(bounds.valid);
        const Vec3 size = bounds.max - bounds.min;
        CHECK(size.x > 0.5f && size.y > 0.5f && size.z > 0.2f);
        CHECK(size.x < 10.0f && size.y < 10.0f && size.z < 10.0f);

        size_t outward = 0;
        size_t total = 0;
        for (const SceneMesh& mesh : scene.meshes) {
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const Vec3& a = mesh.positions[mesh.indices[i]];
                const Vec3& b = mesh.positions[mesh.indices[i + 1]];
                const Vec3& c = mesh.positions[mesh.indices[i + 2]];
                const Vec3 face = cross(b - a, c - a);
                if (length(face) < 1e-9f) {
                    continue;
                }
                ++total;
                const Vec3 n = mesh.normals[mesh.indices[i]] + mesh.normals[mesh.indices[i + 1]] + mesh.normals[mesh.indices[i + 2]];
                if (dot(face, n) > 0.0f) {
                    ++outward;
                }
            }
            for (const Vec3& n : mesh.normals) {
                CHECK(near(length(n), 1.0f, 0.01f));
            }
        }
        CHECK(total > 0 && outward * 100 >= total * 99);
    }
}

void testSphere() {
    for (const BuiltinSceneInfo& info : builtinScenes()) {
        const Scene scene = info.build();
        const int animation = scene.animations.empty() ? -1 : 0;
        const SceneSphere sphere = sceneSphere(scene, animation, 12);
        CHECK(sphere.valid && sphere.radius > 0.5f && sphere.radius < 6.0f);
        const float duration = sceneDuration(scene, animation);
        for (int s = 0; s < 24; ++s) {
            for (const SceneInstance& instance : evaluateScene(scene, animation, duration * static_cast<float>(s) / 24.0f + 0.1f)) {
                if (!instance.framing) {
                    continue;
                }
                for (const Vec3& p : scene.meshes[static_cast<size_t>(instance.mesh)].positions) {
                    CHECK(length(transformPoint(instance.world, p) - sphere.center) <= sphere.radius * 1.001f);
                }
            }
        }
    }
    Scene empty;
    CHECK(!sceneSphere(empty, -1, 1).valid);
    const std::array<float, 9> scaled = normalMatrix(scaling({2.0f, 1.0f, 1.0f}));
    CHECK(near(scaled[0], 0.5f) && near(scaled[4], 1.0f) && near(scaled[8], 1.0f));
}

void testAnimation() {
    const Scene rings = makeOrbitRingsScene();
    CHECK(rings.animations.size() == 1);
    CHECK(near(sceneDuration(rings, 0), 16.0f));
    CHECK(near(sceneDuration(rings, 5), 0.0f));
    const SceneBounds start = sceneBounds(rings, 0, 0.0f);
    const SceneBounds later = sceneBounds(rings, 0, 3.3f);
    const SceneBounds looped = sceneBounds(rings, 0, 16.0f);
    const SceneBounds twice = sceneBounds(rings, 0, 32.0f);
    CHECK(nearVec(start.min, looped.min, 0.01f) && nearVec(start.max, looped.max, 0.01f));
    CHECK(nearVec(start.min, twice.min, 0.01f) && nearVec(start.max, twice.max, 0.01f));
    CHECK(!nearVec(start.max, later.max, 0.01f) || !nearVec(start.min, later.min, 0.01f));
    const SceneBounds negative = sceneBounds(rings, 0, -16.0f);
    CHECK(nearVec(start.max, negative.max, 0.01f));

    Scene scene;
    scene.materials.push_back(SceneMaterial{});
    SceneMesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    mesh.indices = {0, 1, 2};
    mesh.material = 0;
    scene.meshes.push_back(mesh);
    SceneNode parent;
    parent.translation = {10, 0, 0};
    parent.children = {1};
    SceneNode child;
    child.meshes = {0};
    scene.nodes = {parent, child};
    scene.roots = {0};
    SceneAnimation animation;
    SceneChannel step;
    step.node = 1;
    step.path = AnimPath::Translation;
    step.interp = AnimInterp::Step;
    step.times = {0.0f, 1.0f, 2.0f};
    step.values = {0, 0, 0, 0, 5, 0, 0, 9, 0};
    animation.channels.push_back(step);
    animation.duration = 2.0f;
    scene.animations.push_back(animation);
    const auto at = [&](float t) { return evaluateScene(scene, 0, t); };
    CHECK(at(0.5f).size() == 1 && nearVec(transformPoint(at(0.5f)[0].world, {0, 0, 0}), {10, 0, 0}));
    CHECK(nearVec(transformPoint(at(1.5f)[0].world, {0, 0, 0}), {10, 5, 0}));
    CHECK(nearVec(transformPoint(evaluateScene(scene, -1, 1.5f)[0].world, {0, 0, 0}), {10, 0, 0}));
}

void testRoundTrip() {
    size_t sceneIndex = 0;
    for (const BuiltinSceneInfo& info : builtinScenes()) {
        const bool classic = sceneIndex++ < 3;
        const Scene original = info.build();
        const std::vector<uint8_t> glb = writeGlb(original);
        CHECK(glb.size() > 1000);
        const GltfResult loaded = loadGltfMemory(glb.data(), glb.size(), std::filesystem::path());
        CHECK(loaded.ok);
        if (!loaded.ok) {
            std::fprintf(stderr, "round trip error: %s\n", loaded.error.c_str());
            continue;
        }
        const Scene& scene = loaded.scene;
        CHECK(scene.meshes.size() == original.meshes.size());
        CHECK(scene.nodes.size() == original.nodes.size());
        CHECK(scene.animations.size() == original.animations.size());
        CHECK(scene.materials.size() == original.materials.size());
        CHECK(sceneStats(scene).vertices == sceneStats(original).vertices);
        CHECK(sceneStats(scene).triangles == sceneStats(original).triangles);
        size_t colorsLoaded = 0;
        size_t colorsOriginal = 0;
        for (const SceneMesh& m : scene.meshes) {
            colorsLoaded += m.colors.size();
        }
        for (const SceneMesh& m : original.meshes) {
            colorsOriginal += m.colors.size();
        }
        CHECK(colorsLoaded == colorsOriginal && (colorsLoaded > 0) == classic);
        if (!classic) {
            continue;
        }
        if (!original.animations.empty()) {
            CHECK(near(sceneDuration(scene, 0), sceneDuration(original, 0)));
        }
        for (const float t : {0.0f, 2.5f, 7.0f}) {
            const SceneBounds a = sceneBounds(original, original.animations.empty() ? -1 : 0, t);
            const SceneBounds b = sceneBounds(scene, scene.animations.empty() ? -1 : 0, t);
            CHECK(nearVec(a.min, b.min, 0.01f) && nearVec(a.max, b.max, 0.01f));
        }
    }
}

void testMinimalAndNormals() {
    const GltfResult result = loadBlob(triangleBlob());
    CHECK(result.ok);
    CHECK(result.scene.meshes.size() == 1);
    CHECK(result.scene.meshes[0].normals.size() == 3);
    CHECK(nearVec(result.scene.meshes[0].normals[0], {0, 0, 1}));
    CHECK(result.scene.materials.empty() && result.scene.meshes[0].material == -1);
    CHECK(result.scene.roots.size() == 1);

    Blob strip = buildMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 1, 2, 3}, 5);
    const GltfResult stripResult = loadBlob(strip);
    CHECK(stripResult.ok && stripResult.scene.meshes[0].indices.size() == 6);
    if (stripResult.ok) {
        for (size_t i = 0; i < 6; i += 3) {
            const auto& m = stripResult.scene.meshes[0];
            const Vec3 face = cross(m.positions[m.indices[i + 1]] - m.positions[m.indices[i]], m.positions[m.indices[i + 2]] - m.positions[m.indices[i]]);
            CHECK(face.z > 0.0f);
        }
    }

    Blob fan = buildMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {0, 1, 2, 3}, 6);
    const GltfResult fanResult = loadBlob(fan);
    CHECK(fanResult.ok && fanResult.scene.meshes[0].indices.size() == 6);

    Blob points = buildMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, {0, 1, 2}, 0);
    const GltfResult pointsResult = loadBlob(points);
    CHECK(!pointsResult.ok);

    Blob matrix = triangleBlob();
    matrix.doc["nodes"][0]["matrix"] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 2, 3, 1};
    const GltfResult matrixResult = loadBlob(matrix);
    CHECK(matrixResult.ok);
    if (matrixResult.ok) {
        const SceneNode& node = matrixResult.scene.nodes[0];
        CHECK(nearVec(node.translation, {1, 2, 3}) && nearVec(node.scale, {2, 2, 2}));
        CHECK(near(node.rotation.w, 1.0f));
    }

    Blob rotated = triangleBlob();
    const float s = std::sqrt(0.5f);
    rotated.doc["nodes"][0]["matrix"] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const GltfResult rotatedResult = loadBlob(rotated);
    CHECK(rotatedResult.ok);
    if (rotatedResult.ok) {
        const SceneNode& node = rotatedResult.scene.nodes[0];
        CHECK(near(std::fabs(node.rotation.z), s) && near(std::fabs(node.rotation.w), s));
    }
}

void testRejections() {
    CHECK(!loadGltfMemory(nullptr, 0, std::filesystem::path()).ok);
    const std::string garbage = "this is not a model";
    CHECK(!loadGltfMemory(reinterpret_cast<const uint8_t*>(garbage.data()), garbage.size(), std::filesystem::path()).ok);

    const Blob good = triangleBlob();
    const std::vector<uint8_t> glb = glbFrom(good.doc, good.bin);
    CHECK(loadGltfMemory(glb.data(), glb.size(), std::filesystem::path()).ok);
    for (const size_t cut : {size_t(8), size_t(20), glb.size() / 2, glb.size() - 1}) {
        CHECK(!loadGltfMemory(glb.data(), cut, std::filesystem::path()).ok);
    }

    std::vector<uint8_t> wrongVersion = glb;
    wrongVersion[4] = 1;
    CHECK(!loadGltfMemory(wrongVersion.data(), wrongVersion.size(), std::filesystem::path()).ok);

    std::vector<uint8_t> lyingChunk = glb;
    lyingChunk[12] = 0xFF;
    lyingChunk[13] = 0xFF;
    lyingChunk[14] = 0xFF;
    lyingChunk[15] = 0x7F;
    CHECK(!loadGltfMemory(lyingChunk.data(), lyingChunk.size(), std::filesystem::path()).ok);

    Blob badAccessor = triangleBlob();
    badAccessor.doc["accessors"][0]["count"] = 1000;
    CHECK(!loadBlob(badAccessor).ok);

    Blob hugeCount = triangleBlob();
    hugeCount.doc["accessors"][0]["count"] = 4000000000.0;
    CHECK(!loadBlob(hugeCount).ok);

    Blob negativeOffset = triangleBlob();
    negativeOffset.doc["bufferViews"][0]["byteOffset"] = -4;
    CHECK(!loadBlob(negativeOffset).ok);

    Blob viewBeyond = triangleBlob();
    viewBeyond.doc["bufferViews"][1]["byteOffset"] = 1000000;
    CHECK(!loadBlob(viewBeyond).ok);

    Blob outOfRangeIndex = buildMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 7});
    CHECK(!loadBlob(outOfRangeIndex).ok);

    Blob missingBuffer = triangleBlob();
    missingBuffer.doc["bufferViews"][0]["buffer"] = 4;
    CHECK(!loadBlob(missingBuffer).ok);

    Blob shortBuffer = triangleBlob();
    shortBuffer.doc["buffers"][0]["byteLength"] = 100000;
    CHECK(!loadBlob(shortBuffer).ok);

    Blob sparse = triangleBlob();
    sparse.doc["accessors"][0]["sparse"] = {{"count", 1}};
    CHECK(!loadBlob(sparse).ok);

    Blob draco = triangleBlob();
    draco.doc["extensionsRequired"] = {"KHR_draco_mesh_compression"};
    const GltfResult dracoResult = loadBlob(draco);
    CHECK(dracoResult.ok == dracoAvailable());
    CHECK(dracoAvailable() || dracoResult.error.find("NYREXVJ_DRACO") != std::string::npos);

    Blob meshopt = triangleBlob();
    meshopt.doc["extensionsRequired"] = {"EXT_meshopt_compression"};
    const GltfResult meshoptResult = loadBlob(meshopt);
    CHECK(meshoptResult.ok == meshoptAvailable());
    CHECK(meshoptAvailable() || meshoptResult.error.find("NYREXVJ_MESHOPT") != std::string::npos);

    Blob unknownRequired = triangleBlob();
    unknownRequired.doc["extensionsRequired"] = {"KHR_materials_sheen_of_doom"};
    CHECK(!loadBlob(unknownRequired).ok);

    Blob cycle = triangleBlob();
    cycle.doc["nodes"] = json::array({{{"mesh", 0}, {"children", {1}}}, {{"children", {0}}}});
    CHECK(!loadBlob(cycle).ok);

    Blob selfChild = triangleBlob();
    selfChild.doc["nodes"][0]["children"] = {0};
    CHECK(!loadBlob(selfChild).ok);

    Blob shared = triangleBlob();
    shared.doc["nodes"] = json::array({{{"children", {2, 1}}}, {{"children", {2}}}, {{"mesh", 0}}});
    CHECK(!loadBlob(shared).ok);

    Blob nonFinite = triangleBlob();
    nonFinite.doc["nodes"][0]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1e39, 0, 0, 1};
    CHECK(!loadBlob(nonFinite).ok);

    Blob wrongType = triangleBlob();
    wrongType.doc["nodes"][0]["mesh"] = "zero";
    CHECK(!loadBlob(wrongType).ok);

    Blob noMesh = triangleBlob();
    noMesh.doc.erase("meshes");
    noMesh.doc["nodes"][0].erase("mesh");
    CHECK(!loadBlob(noMesh).ok);

    GltfLimits tiny;
    tiny.maxVertices = 2;
    CHECK(!loadBlob(triangleBlob(), tiny).ok);
    GltfLimits fewNodes;
    fewNodes.maxNodes = 0;
    CHECK(!loadBlob(triangleBlob(), fewNodes).ok);
    GltfLimits smallFile;
    smallFile.maxFileBytes = 64;
    CHECK(!loadBlob(triangleBlob(), smallFile).ok);
    GltfLimits smallJson;
    smallJson.maxJsonBytes = 16;
    CHECK(!loadBlob(triangleBlob(), smallJson).ok);

    Blob deep = triangleBlob();
    json nested = json::array();
    json* cursor = &nested;
    for (int i = 0; i < 200; ++i) {
        cursor->push_back(json::array());
        cursor = &(*cursor)[0];
    }
    deep.doc["extras"] = nested;
    CHECK(!loadBlob(deep).ok);

    Blob traversal = triangleBlob();
    traversal.doc["buffers"][0]["uri"] = "../secret.bin";
    CHECK(!loadBlob(traversal).ok);
    Blob absolute = triangleBlob();
    absolute.doc["buffers"][0]["uri"] = "/etc/passwd";
    CHECK(!loadBlob(absolute).ok);
    Blob drive = triangleBlob();
    drive.doc["buffers"][0]["uri"] = "C:\\Windows\\win.ini";
    CHECK(!loadBlob(drive).ok);
    Blob encoded = triangleBlob();
    encoded.doc["buffers"][0]["uri"] = "%2e%2e/secret.bin";
    CHECK(!loadBlob(encoded).ok);
    Blob external = triangleBlob();
    external.doc["buffers"][0]["uri"] = "mesh.bin";
    CHECK(!loadBlob(external).ok);

    Blob badBase64 = triangleBlob();
    badBase64.doc["buffers"][0]["uri"] = "data:application/octet-stream;base64,@@@@";
    CHECK(!loadBlob(badBase64).ok);

    Blob badMaterial = triangleBlob();
    badMaterial.doc["meshes"][0]["primitives"][0]["material"] = 3;
    CHECK(!loadBlob(badMaterial).ok);
}

void testDataUriAndFiles() {
    Blob embedded = triangleBlob();
    embedded.doc["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64(embedded.bin);
    const std::string text = embedded.doc.dump();
    const GltfResult embeddedResult = loadGltfMemory(reinterpret_cast<const uint8_t*>(text.data()), text.size(), std::filesystem::path());
    CHECK(embeddedResult.ok);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nvj_scene3d_tests";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "models", ec);

    const std::vector<uint8_t> glb = writeGlb(makeCrystalClusterScene());
    {
        std::ofstream out(dir / "models" / "Cluster Copy.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    }
    const GltfResult file = loadGltfFile(dir / "models" / "Cluster Copy.glb");
    CHECK(file.ok && file.scene.name == "Cluster Copy");
    CHECK(!loadGltfFile(dir / "models" / "missing.glb").ok);
    CHECK(!loadGltfFile(dir / "models").ok);

    Blob split = triangleBlob();
    split.doc["buffers"][0]["uri"] = "tri.bin";
    {
        std::ofstream bin(dir / "models" / "tri.bin", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(split.bin.data()), static_cast<std::streamsize>(split.bin.size()));
        std::ofstream gltf(dir / "models" / "tri.gltf");
        gltf << split.doc.dump();
        std::ofstream secret(dir / "secret.bin", std::ios::binary);
        secret.write(reinterpret_cast<const char*>(split.bin.data()), static_cast<std::streamsize>(split.bin.size()));
    }
    CHECK(loadGltfFile(dir / "models" / "tri.gltf").ok);

    split.doc["buffers"][0]["uri"] = "../secret.bin";
    {
        std::ofstream gltf(dir / "models" / "escape.gltf");
        gltf << split.doc.dump();
    }
    CHECK(!loadGltfFile(dir / "models" / "escape.gltf").ok);

    GltfLimits small;
    small.maxFileBytes = 100;
    CHECK(!loadGltfFile(dir / "models" / "Cluster Copy.glb", small).ok);

    std::filesystem::remove_all(dir, ec);
}

void pngWrite(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    appendBytes(*out, data, static_cast<size_t>(size));
}

void testTextures() {
    std::vector<uint8_t> pixels;
    for (int i = 0; i < 4; ++i) {
        pixels.insert(pixels.end(), {static_cast<uint8_t>(60 * i), 128, 255, 255});
    }
    std::vector<uint8_t> png;
    CHECK(stbi_write_png_to_func(pngWrite, &png, 2, 2, 4, pixels.data(), 8) != 0);

    Blob textured = triangleBlob();
    alignBlob(textured.bin);
    const size_t offset = textured.bin.size();
    textured.bin.insert(textured.bin.end(), png.begin(), png.end());
    const size_t pngBytes = png.size();
    alignBlob(textured.bin);
    textured.doc["buffers"][0]["byteLength"] = textured.bin.size();
    textured.doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", pngBytes}});
    textured.doc["images"] = json::array({{{"bufferView", 2}, {"mimeType", "image/png"}}});
    textured.doc["textures"] = json::array({{{"source", 0}}});
    textured.doc["materials"] = json::array({{{"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}, {"baseColorFactor", {1.0, 0.5, 0.25, 1.0}}}}, {"doubleSided", true}}});
    textured.doc["meshes"][0]["primitives"][0]["material"] = 0;

    const GltfResult result = loadBlob(textured);
    CHECK(result.ok);
    if (result.ok) {
        CHECK(result.scene.images.size() == 1);
        CHECK(result.scene.images[0].width == 2 && result.scene.images[0].height == 2);
        CHECK(result.scene.images[0].rgba.size() == 16);
        CHECK(result.scene.images[0].rgba[4] == 60);
        CHECK(result.scene.materials.size() == 1);
        CHECK(result.scene.materials[0].baseTexture == 0);
        CHECK(result.scene.materials[0].doubleSided);
        CHECK(near(result.scene.materials[0].baseColor[1], 0.5f));
    }

    GltfLimits strict;
    strict.maxImageDimension = 1;
    const GltfResult limited = loadBlob(textured, strict);
    CHECK(limited.ok);
    CHECK(limited.scene.images.empty());
    CHECK(limited.scene.materials.size() == 1 && limited.scene.materials[0].baseTexture == -1);
    CHECK(!limited.warnings.empty());

    Blob brokenImage = textured;
    brokenImage.bin[offset + 12] ^= 0xFF;
    brokenImage.bin[offset + 13] ^= 0xFF;
    const GltfResult broken = loadBlob(brokenImage);
    CHECK(broken.ok);
    CHECK(broken.scene.materials[0].baseTexture == -1 || broken.scene.images.size() == 1);

    Blob notAnImage = textured;
    for (size_t i = 0; i < 8; ++i) {
        notAnImage.bin[offset + i] = 0x41;
    }
    const GltfResult rejected = loadBlob(notAnImage);
    CHECK(rejected.ok && rejected.scene.images.empty() && !rejected.warnings.empty());
}

void testPbrMaterials() {
    std::vector<uint8_t> pixels;
    for (int i = 0; i < 4; ++i) {
        pixels.insert(pixels.end(), {static_cast<uint8_t>(60 * i), 128, 255, 255});
    }
    std::vector<uint8_t> png;
    CHECK(stbi_write_png_to_func(pngWrite, &png, 2, 2, 4, pixels.data(), 8) != 0);

    Blob blob = triangleBlob();
    alignBlob(blob.bin);
    const size_t offset = blob.bin.size();
    blob.bin.insert(blob.bin.end(), png.begin(), png.end());
    const size_t pngBytes = png.size();
    alignBlob(blob.bin);
    blob.doc["buffers"][0]["byteLength"] = blob.bin.size();
    blob.doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", pngBytes}});
    blob.doc["images"] = json::array({{{"bufferView", 2}, {"mimeType", "image/png"}}});
    blob.doc["textures"] = json::array({{{"source", 0}}});
    blob.doc["materials"] = json::array({
        {{"pbrMetallicRoughness", {{"metallicFactor", 0.25}, {"roughnessFactor", 0.75}, {"metallicRoughnessTexture", {{"index", 0}}}}},
         {"normalTexture", {{"index", 0}, {"scale", 2.0}}},
         {"emissiveTexture", {{"index", 0}}},
         {"emissiveFactor", {1.0, 0.5, 0.0}},
         {"extensions", {{"KHR_materials_emissive_strength", {{"emissiveStrength", 4.0}}}}},
         {"alphaMode", "MASK"},
         {"alphaCutoff", 0.3}},
        {{"alphaMode", "BLEND"}},
        {{"pbrMetallicRoughness", {{"metallicFactor", 9.0}, {"roughnessFactor", -3.0}}}, {"emissiveFactor", {5.0, 5.0, 5.0}}},
    });
    blob.doc["meshes"][0]["primitives"][0]["material"] = 0;
    const GltfResult result = loadBlob(blob);
    CHECK(result.ok);
    CHECK(result.scene.shading == Shading::Pbr);
    CHECK(result.scene.materials.size() == 3);
    if (result.scene.materials.size() == 3) {
        const SceneMaterial& m = result.scene.materials[0];
        CHECK(near(m.metallic, 0.25f) && near(m.roughness, 0.75f));
        CHECK(m.metallicRoughnessTexture == 0 && m.normalTexture == 0 && m.emissiveTexture == 0);
        CHECK(near(m.normalScale, 2.0f));
        CHECK(near(m.emissive[0], 1.0f) && near(m.emissive[1], 0.5f) && near(m.emissive[2], 0.0f));
        CHECK(near(m.emissiveStrength, 4.0f));
        CHECK(m.alphaMode == AlphaMode::Mask && near(m.alphaCutoff, 0.3f));
        CHECK(result.scene.materials[1].alphaMode == AlphaMode::Blend);
        CHECK(near(result.scene.materials[1].metallic, 1.0f) && near(result.scene.materials[1].roughness, 1.0f));
        CHECK(near(result.scene.materials[2].metallic, 1.0f) && near(result.scene.materials[2].roughness, 0.0f));
        CHECK(result.scene.materials[2].emissive[0] == 1.0f);
    }
    bool blendWarning = false;
    for (const std::string& warning : result.warnings) {
        blendWarning = blendWarning || warning.find("alpha mode") != std::string::npos;
    }
    CHECK(!blendWarning);

    Blob unknownMode = blob;
    unknownMode.doc["materials"][1]["alphaMode"] = "SMOKE";
    const GltfResult unknown = loadBlob(unknownMode);
    bool unknownWarning = false;
    for (const std::string& warning : unknown.warnings) {
        unknownWarning = unknownWarning || warning.find("SMOKE") != std::string::npos;
    }
    CHECK(unknown.ok && unknown.scene.materials[1].alphaMode == AlphaMode::Opaque && unknownWarning);

    Blob broken = blob;
    broken.doc["materials"][0]["normalTexture"] = {{"index", 99}};
    const GltfResult tolerant = loadBlob(broken);
    CHECK(tolerant.ok && tolerant.scene.materials[0].normalTexture == -1);

    Blob nan = blob;
    nan.doc["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = "high";
    nan.doc["materials"][0]["emissiveFactor"] = {"a", 1, 2};
    const GltfResult sloppy = loadBlob(nan);
    CHECK(sloppy.ok && near(sloppy.scene.materials[0].roughness, 1.0f) && near(sloppy.scene.materials[0].emissive[0], 0.0f));
}

void testInterruption() {
    const Blob good = triangleBlob();
    CHECK(loadBlob(good).ok);

    std::atomic<bool> stop{true};
    GltfLimits cancelled;
    cancelled.cancel = &stop;
    const GltfResult byFlag = loadBlob(good, cancelled);
    CHECK(!byFlag.ok);
    CHECK(byFlag.interrupted);
    CHECK(byFlag.scene.nodes.empty());

    std::atomic<bool> go{false};
    GltfLimits waiting;
    waiting.cancel = &go;
    CHECK(loadBlob(good, waiting).ok);

    GltfLimits tooSlow;
    tooSlow.maxSeconds = 1e-9;
    const GltfResult byTime = loadBlob(good, tooSlow);
    CHECK(!byTime.ok);
    CHECK(byTime.interrupted);
    CHECK(byTime.error.find("time limit") != std::string::npos);

    GltfLimits unlimited;
    unlimited.maxSeconds = 0.0;
    CHECK(loadBlob(good, unlimited).ok);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nvj_interrupt_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::vector<uint8_t> glb = writeGlb(makeTorusKnotScene());
    {
        std::ofstream out(dir / "knot.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    }
    CHECK(loadGltfFile(dir / "knot.glb").ok);
    const GltfResult fileCancelled = loadGltfFile(dir / "knot.glb", cancelled);
    CHECK(!fileCancelled.ok);
    CHECK(fileCancelled.interrupted);
    std::filesystem::remove_all(dir, ec);
}

void testDeterministicAndSmall() {
    const std::vector<uint8_t> a = writeGlb(makeTorusKnotScene());
    const std::vector<uint8_t> b = writeGlb(makeTorusKnotScene());
    CHECK(a == b);
    const SceneStats stats = sceneStats(makeTorusKnotScene());
    CHECK(stats.gpuBytes < 4u * 1024u * 1024u);
    CHECK(stats.triangles < 40000);
}

}

const std::filesystem::path kModels = std::filesystem::path(NYREXVJ_TEST_DATA) / "models";

GltfResult loadModelFile(const std::string& name, const GltfLimits& limits = {}) {
    return loadGltfFile(kModels / name, limits);
}

Blob splitGlb(const std::string& name) {
    std::ifstream in(kModels / name, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Blob blob;
    uint32_t jsonLength = 0;
    std::memcpy(&jsonLength, bytes.data() + 12, 4);
    blob.doc = json::parse(bytes.begin() + 20, bytes.begin() + 20 + jsonLength);
    uint32_t binLength = 0;
    std::memcpy(&binLength, bytes.data() + 20 + jsonLength, 4);
    blob.bin.assign(bytes.begin() + 28 + jsonLength, bytes.begin() + 28 + jsonLength + binLength);
    return blob;
}

std::vector<Vec3> worldVertices(const Scene& scene, int animation, float time) {
    const SceneEval eval = evaluateSceneFull(scene, animation, time);
    std::vector<Vec3> out;
    for (const SceneInstance& instance : eval.instances) {
        const std::vector<Vec3> posed = posedPositions(scene, scene.meshes[static_cast<size_t>(instance.mesh)], eval, instance);
        for (const Vec3& p : posed) {
            out.push_back(transformPoint(instance.world, p));
        }
    }
    std::sort(out.begin(), out.end(), [](const Vec3& a, const Vec3& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z < b.z;
    });
    return out;
}

bool sameVertexSets(const std::vector<Vec3>& a, const std::vector<Vec3>& b, float eps) {
    if (a.size() != b.size()) {
        return false;
    }
    std::vector<char> used(b.size(), 0);
    for (const Vec3& p : a) {
        bool found = false;
        for (size_t i = 0; i < b.size(); ++i) {
            if (!used[i] && nearVec(p, b[i], eps)) {
                used[i] = 1;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool hasWarning(const GltfResult& result, const std::string& text) {
    for (const std::string& warning : result.warnings) {
        if (warning.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void testSkinning() {
    const GltfResult result = loadModelFile("skin_arm.glb");
    CHECK(result.ok);
    if (!result.ok) {
        return;
    }
    const Scene& scene = result.scene;
    CHECK(result.warnings.empty());
    CHECK(scene.skins.size() == 1 && scene.skins[0].joints.size() == 2);
    CHECK(scene.skins[0].joints[0] == 1 && scene.skins[0].joints[1] == 2);
    CHECK(near(scene.skins[0].inverseBind[1].m[13], -1.0f) && near(scene.skins[0].inverseBind[0].m[13], 0.0f));
    CHECK(scene.nodes[0].skin == 0 && scene.nodes[1].skin == -1);
    CHECK(scene.meshes.size() == 1 && scene.meshes[0].joints.size() == scene.meshes[0].positions.size() * 4);
    const SceneStats stats = sceneStats(scene);
    CHECK(stats.skins == 1 && stats.joints == 2 && stats.blendMaterials == 0);

    const SceneEval bind = evaluateSceneFull(scene, 0, 0.0f);
    CHECK(bind.instances.size() == 1 && bind.joints.size() == 2 && bind.skippedSkinned == 0);
    CHECK(bind.instances[0].skin == 0 && bind.instances[0].jointCount == 2 && bind.instances[0].jointOffset == 0);
    CHECK(nearVec(transformPoint(bind.instances[0].world, {1, 2, 3}), {1, 2, 3}));
    const std::vector<Vec3> bindPose = posedPositions(scene, scene.meshes[0], bind, bind.instances[0]);
    bool same = bindPose.size() == scene.meshes[0].positions.size();
    for (size_t i = 0; same && i < bindPose.size(); ++i) {
        same = nearVec(bindPose[i], scene.meshes[0].positions[i], 1e-4f);
    }
    CHECK(same);

    const SceneEval bent = evaluateSceneFull(scene, 0, 1.0f);
    const std::vector<Vec3> posed = posedPositions(scene, scene.meshes[0], bent, bent.instances[0]);
    CHECK(nearVec(posed[16], {-1.0f, 1.1f, 0.1f}, 0.002f));
    CHECK(nearVec(posed[0], scene.meshes[0].positions[0], 0.002f));
    CHECK(nearVec(posed[8], {0.05f, 1.05f, 0.1f}, 0.002f));
    const SceneBounds restBounds = sceneBounds(scene, 0, 0.0f);
    const SceneBounds bentBounds = sceneBounds(scene, 0, 1.0f);
    CHECK(restBounds.valid && bentBounds.valid);
    CHECK(restBounds.max.y > 1.9f && restBounds.min.x > -0.2f);
    CHECK(bentBounds.min.x < -0.9f && bentBounds.max.y < 1.3f);
    const SceneSphere sphere = sceneSphere(scene, 0, 12);
    CHECK(sphere.valid && sphere.radius > 1.0f);

    Scene broken = scene;
    broken.meshes[0].joints[0] = 5;
    std::string error;
    CHECK(!validateScene(broken, error) && error.find("joint") != std::string::npos);
    broken = scene;
    broken.meshes[0].weights[1] = -0.5f;
    CHECK(!validateScene(broken, error));
    broken = scene;
    broken.meshes[0].weights.pop_back();
    CHECK(!validateScene(broken, error));
    broken = scene;
    broken.skins[0].inverseBind.pop_back();
    CHECK(!validateScene(broken, error));
    broken = scene;
    broken.skins[0].joints[1] = 99;
    CHECK(!validateScene(broken, error));
    broken = scene;
    broken.nodes[0].skin = 4;
    CHECK(!validateScene(broken, error));

    Scene crowd = scene;
    for (int i = 0; i < 600; ++i) {
        SceneNode copy = scene.nodes[0];
        copy.translation = {static_cast<float>(i), 0, 0};
        crowd.nodes.push_back(copy);
        crowd.roots.push_back(static_cast<int>(crowd.nodes.size()) - 1);
    }
    const SceneEval many = evaluateSceneFull(crowd, 0, 0.0f);
    CHECK(many.joints.size() <= kSceneMaxJointsPerFrame);
    CHECK(many.skippedSkinned > 0);
    CHECK(many.instances.size() + many.skippedSkinned == 601);

    GltfLimits oneBone;
    oneBone.maxBones = 1;
    const GltfResult tooManyBones = loadModelFile("skin_arm.glb", oneBone);
    CHECK(!tooManyBones.ok && tooManyBones.error.find("joints") != std::string::npos);
    GltfLimits noSkins;
    noSkins.maxSkins = 0;
    const GltfResult tooManySkins = loadModelFile("skin_arm.glb", noSkins);
    CHECK(!tooManySkins.ok && tooManySkins.error.find("skins") != std::string::npos);
    GltfLimits tinyGpu;
    tinyGpu.maxGpuBytes = 1000;
    const GltfResult tooBig = loadModelFile("skin_arm.glb", tinyGpu);
    CHECK(!tooBig.ok && tooBig.error.find("graphics memory") != std::string::npos);

    const std::vector<uint8_t> glb = writeGlb(scene);
    const GltfResult again = loadGltfMemory(glb.data(), glb.size(), std::filesystem::path());
    CHECK(again.ok && again.scene.skins.size() == 1 && again.scene.nodes[0].skin == 0);
    CHECK(sameVertexSets(worldVertices(scene, 0, 1.0f), worldVertices(again.scene, 0, 1.0f), 1e-4f));
    CHECK(sameVertexSets(worldVertices(scene, 0, 0.4f), worldVertices(again.scene, 0, 0.4f), 1e-4f));
}

void testReleasedGeometryStillSkins() {
    for (const char* name : {"skin_arm.glb", "skin_morph.glb", "morph_cube.glb"}) {
        const GltfResult result = loadModelFile(name);
        CHECK(result.ok);
        if (!result.ok) {
            continue;
        }
        Scene scene = result.scene;
        const SceneEval before = evaluateSceneFull(scene, 0, 1.0f);
        for (SceneMesh& mesh : scene.meshes) {
            mesh.rememberAnimationData();
            std::vector<Vec3>().swap(mesh.positions);
            std::vector<Vec3>().swap(mesh.normals);
            std::vector<uint32_t>().swap(mesh.indices);
            std::vector<uint16_t>().swap(mesh.joints);
            std::vector<float>().swap(mesh.weights);
            std::vector<SceneMorphTarget>().swap(mesh.targets);
        }
        const SceneEval after = evaluateSceneFull(scene, 0, 1.0f);
        CHECK(after.instances.size() == before.instances.size());
        CHECK(after.joints.size() == before.joints.size());
        CHECK(after.weights.size() == before.weights.size());
        for (size_t i = 0; i < before.instances.size() && i < after.instances.size(); ++i) {
            CHECK(after.instances[i].skin == before.instances[i].skin);
            CHECK(after.instances[i].jointCount == before.instances[i].jointCount);
            CHECK(after.instances[i].weightCount == before.instances[i].weightCount);
        }
    }
}

void testSkinRejections() {
    const auto reload = [](const std::function<void(json&)>& edit) {
        Blob blob = splitGlb("skin_arm.glb");
        edit(blob.doc);
        return loadBlob(blob);
    };
    CHECK(reload([](json&) {}).ok);
    CHECK(!reload([](json& d) { d["skins"][0]["joints"] = json::array(); }).ok);
    CHECK(!reload([](json& d) { d["skins"][0]["joints"][1] = 40; }).ok);
    CHECK(!reload([](json& d) { d["skins"][0]["joints"][1] = "elbow"; }).ok);
    CHECK(!reload([](json& d) { d["nodes"][0]["skin"] = 3; }).ok);
    CHECK(!reload([](json& d) { d["skins"][0]["inverseBindMatrices"] = 99; }).ok);
    CHECK(!reload([](json& d) { d["skins"][0]["joints"] = json::array({1, 2, 1}); }).ok);
    CHECK(!reload([](json& d) { d["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = d["meshes"][0]["primitives"][0]["attributes"]["POSITION"]; }).ok);
    const GltfResult mismatch = reload([](json& d) { d["accessors"][d["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"].get<int>()]["count"] = 3; });
    CHECK(mismatch.ok && mismatch.scene.meshes[0].joints.empty() && hasWarning(mismatch, "skinning is ignored"));
    const GltfResult lonely = reload([](json& d) { d["meshes"][0]["primitives"][0]["attributes"].erase("WEIGHTS_0"); });
    CHECK(lonely.ok && lonely.scene.meshes[0].joints.empty() && hasWarning(lonely, "WEIGHTS_0"));
    const GltfResult more = reload([](json& d) {
        d["meshes"][0]["primitives"][0]["attributes"]["JOINTS_1"] = d["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"];
        d["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_1"] = d["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"];
    });
    CHECK(more.ok && hasWarning(more, "more than four"));
    const GltfResult unskinnedNode = reload([](json& d) { d["nodes"][0].erase("skin"); });
    CHECK(unskinnedNode.ok);
    if (unskinnedNode.ok) {
        const SceneEval eval = evaluateSceneFull(unskinnedNode.scene, -1, 0.0f);
        CHECK(eval.instances.size() == 1 && eval.instances[0].skin == -1 && eval.joints.empty());
        CHECK(nearVec(transformPoint(eval.instances[0].world, {0, 0, 0}), {5, 0, 0}));
    }
}

void testMorphTargets() {
    const GltfResult result = loadModelFile("morph_cube.glb");
    CHECK(result.ok);
    if (!result.ok) {
        return;
    }
    const Scene& scene = result.scene;
    CHECK(scene.meshes.size() == 1 && scene.meshes[0].targets.size() == 2);
    CHECK(scene.meshes[0].targets[0].normals.empty() && scene.meshes[0].targets[1].normals.size() == 24);
    CHECK(scene.nodes[0].weights.size() == 2 && near(scene.nodes[0].weights[0], 0.25f) && near(scene.nodes[0].weights[1], 0.0f));
    const SceneStats stats = sceneStats(scene);
    CHECK(stats.morphTargets == 2);
    CHECK(scene.animations.size() == 1 && scene.animations[0].channels.size() == 1 && scene.animations[0].channels[0].path == AnimPath::Weights);

    const auto weightsAt = [&](float t) { return evaluateSceneFull(scene, 0, t).weights; };
    CHECK(near(weightsAt(0.0f)[0], 0.0f) && near(weightsAt(1.0f)[0], 1.0f) && near(weightsAt(1.0f)[1], 0.5f));
    CHECK(near(weightsAt(0.5f)[0], 0.5f) && near(weightsAt(0.5f)[1], 0.25f) && near(weightsAt(1.5f)[0], 0.5f));
    const SceneEval rest = evaluateSceneFull(scene, -1, 0.0f);
    CHECK(rest.weights.size() >= 2 && near(rest.weights[0], 0.25f) && near(rest.weights[1], 0.0f));

    const SceneEval full = evaluateSceneFull(scene, 0, 1.0f);
    const std::vector<Vec3> posed = posedPositions(scene, scene.meshes[0], full, full.instances[0]);
    float top = -10.0f;
    float bottom = 10.0f;
    for (const Vec3& p : posed) {
        top = std::max(top, p.y);
        bottom = std::min(bottom, p.y);
    }
    CHECK(near(top, 1.625f) && near(bottom, -0.625f));
    const SceneBounds liftedBounds = sceneBounds(scene, 0, 1.0f);
    const SceneBounds restBounds = sceneBounds(scene, 0, 0.0f);
    CHECK(liftedBounds.max.y > 1.5f && restBounds.max.y < 0.8f);

    const GltfResult sparse = loadModelFile("morph_sparse.glb");
    CHECK(sparse.ok && sparse.scene.meshes[0].targets.size() == 2);
    if (sparse.ok) {
        bool identical = true;
        for (size_t i = 0; i < scene.meshes[0].positions.size(); ++i) {
            identical = identical && nearVec(sparse.scene.meshes[0].targets[0].positions[i], scene.meshes[0].targets[0].positions[i], 1e-6f);
        }
        CHECK(identical);
    }

    GltfLimits oneTarget;
    oneTarget.maxMorphTargets = 1;
    const GltfResult limited = loadModelFile("morph_cube.glb", oneTarget);
    CHECK(!limited.ok && limited.error.find("morph targets") != std::string::npos);
    GltfLimits tinyMorph;
    tinyMorph.maxMorphBytes = 100;
    const GltfResult memory = loadModelFile("morph_cube.glb", tinyMorph);
    CHECK(!memory.ok && memory.error.find("morph") != std::string::npos);

    const std::vector<uint8_t> glb = writeGlb(scene);
    const GltfResult again = loadGltfMemory(glb.data(), glb.size(), std::filesystem::path());
    CHECK(again.ok && again.scene.meshes[0].targets.size() == 2 && again.scene.nodes[0].weights.size() == 2);
    CHECK(sameVertexSets(worldVertices(scene, 0, 1.0f), worldVertices(again.scene, 0, 1.0f), 1e-4f));

    const auto reload = [](const std::function<void(json&)>& edit) {
        Blob blob = splitGlb("morph_cube.glb");
        edit(blob.doc);
        return loadBlob(blob);
    };
    CHECK(!reload([](json& d) { d["accessors"][d["meshes"][0]["primitives"][0]["targets"][0]["POSITION"].get<int>()]["count"] = 5; }).ok);
    CHECK(!reload([](json& d) { d["meshes"][0]["primitives"][0]["targets"][1]["POSITION"] = 77; }).ok);
    CHECK(!reload([](json& d) { d["meshes"][0]["primitives"][0]["targets"][0] = 5; }).ok);
    CHECK(!reload([](json& d) { d["animations"][0]["samplers"][0]["output"] = 0; }).ok);
    const GltfResult fewer = reload([](json& d) { d["meshes"][0]["weights"] = json::array({0.5}); });
    CHECK(fewer.ok && fewer.scene.nodes[0].weights.size() == 1);
    const GltfResult overridden = reload([](json& d) { d["nodes"][0]["weights"] = json::array({0.9, 0.1}); });
    CHECK(overridden.ok && near(overridden.scene.nodes[0].weights[0], 0.9f));
    const GltfResult huge = reload([](json& d) {
        json many = json::array();
        for (int i = 0; i < 80; ++i) {
            many.push_back(0.1);
        }
        d["nodes"][0]["weights"] = many;
    });
    CHECK(huge.ok && huge.scene.nodes[0].weights.size() <= kSceneMaxMorphTargets);
}

void testSkinAndMorphTogether() {
    const GltfResult result = loadModelFile("skin_morph.glb");
    CHECK(result.ok);
    if (!result.ok) {
        return;
    }
    const Scene& scene = result.scene;
    CHECK(scene.skins.size() == 1 && scene.meshes[0].targets.size() == 1 && scene.animations[0].channels.size() == 2);
    const SceneEval eval = evaluateSceneFull(scene, 0, 1.0f);
    CHECK(eval.instances.size() == 1 && eval.instances[0].skin == 0 && eval.instances[0].weightCount == 1);
    const std::vector<Vec3> posed = posedPositions(scene, scene.meshes[0], eval, eval.instances[0]);
    CHECK(nearVec(posed[16], {-1.0f, 1.3f, 0.3f}, 0.002f));
    CHECK(nearVec(posed[0], {0.3f, 0.0f, 0.3f}, 0.002f));
    const std::vector<uint8_t> glb = writeGlb(scene);
    const GltfResult again = loadGltfMemory(glb.data(), glb.size(), std::filesystem::path());
    CHECK(again.ok && sameVertexSets(worldVertices(scene, 0, 0.7f), worldVertices(again.scene, 0, 0.7f), 1e-4f));
}

void testBlendAndFileLights() {
    const GltfResult nearFirst = loadModelFile("blend_near_first.glb");
    const GltfResult farFirst = loadModelFile("blend_far_first.glb");
    CHECK(nearFirst.ok && farFirst.ok);
    if (!nearFirst.ok || !farFirst.ok) {
        return;
    }
    CHECK(nearFirst.warnings.empty());
    CHECK(nearFirst.scene.materials.size() == 4 && nearFirst.scene.materials[0].alphaMode == AlphaMode::Blend && nearFirst.scene.materials[3].alphaMode == AlphaMode::Opaque);
    CHECK(sceneStats(nearFirst.scene).blendMaterials == 3);
    CHECK(nearFirst.scene.materials[1].doubleSided && !nearFirst.scene.materials[0].doubleSided);
    CHECK(farFirst.scene.materials[3].alphaMode == AlphaMode::Opaque && farFirst.scene.materials[0].alphaMode == AlphaMode::Blend);
    CHECK(near(nearFirst.scene.meshes[0].positions[0].z, 0.5f) && near(farFirst.scene.meshes[0].positions[0].z, -0.5f));
    const std::vector<uint8_t> glb = writeGlb(nearFirst.scene);
    const GltfResult again = loadGltfMemory(glb.data(), glb.size(), std::filesystem::path());
    CHECK(again.ok && again.scene.materials[0].alphaMode == AlphaMode::Blend && again.scene.materials[2].alphaMode == AlphaMode::Blend);

    const GltfResult lights = loadModelFile("lights_cameras.glb");
    CHECK(lights.ok);
    if (lights.ok) {
        const Scene& scene = lights.scene;
        CHECK(scene.lights.empty() && !scene.camera.authored && scene.cameras.empty());
        CHECK(scene.fileLights.size() == 3 && scene.fileCameras.size() == 2);
        CHECK(!scene.fileLights[0].point && nearVec(scene.fileLights[0].position, {0, 1, 0}, 1e-4f) && near(scene.fileLights[0].intensity, 3.0f));
        CHECK(near(scene.fileLights[0].color[1], 0.95f));
        CHECK(scene.fileLights[1].point && nearVec(scene.fileLights[1].position, {1, 2, 3}, 1e-4f) && near(scene.fileLights[1].intensity, 5.0f));
        CHECK(scene.fileLights[2].point && nearVec(scene.fileLights[2].position, {-1, 1, 0}, 1e-4f));
        CHECK(hasWarning(lights, "spot"));
        CHECK(!scene.fileCameras[0].orthographic && near(scene.fileCameras[0].yfov, 0.6f) && near(scene.fileCameras[0].world.m[14], 5.0f));
        CHECK(scene.fileCameras[1].orthographic && near(scene.fileCameras[1].xmag, 5.0f) && near(scene.fileCameras[1].ymag, 3.0f) && near(scene.fileCameras[1].world.m[13], 10.0f));
        CHECK(scene.fileCameras[0].name == "main");
        const SceneEval eval = evaluateSceneFull(scene, -1, 0.0f);
        CHECK(eval.instances.size() == 1);
    }
    CHECK(loadModelFile("lights_required.glb").ok);

    const auto reload = [](const std::function<void(json&)>& edit) {
        Blob blob = splitGlb("lights_cameras.glb");
        edit(blob.doc);
        return loadBlob(blob);
    };
    const GltfResult brokenLight = reload([](json& d) { d["nodes"][1]["extensions"]["KHR_lights_punctual"]["light"] = 9; });
    CHECK(brokenLight.ok && brokenLight.scene.fileLights.size() == 2);
    const GltfResult brokenCamera = reload([](json& d) { d["cameras"][0]["perspective"]["yfov"] = -1.0; });
    CHECK(brokenCamera.ok && brokenCamera.scene.fileCameras.size() == 1 && hasWarning(brokenCamera, "camera"));
    const GltfResult manyLights = reload([](json& d) {
        json nodes = d["nodes"];
        for (int i = 0; i < 40; ++i) {
            json copy = nodes[2];
            copy["name"] = "extra " + std::to_string(i);
            d["nodes"].push_back(copy);
            d["scenes"][0]["nodes"].push_back(static_cast<int>(d["nodes"].size()) - 1);
        }
    });
    CHECK(manyLights.ok && manyLights.scene.fileLights.size() == kSceneMaxFileLights);
    const GltfResult unknownLight = reload([](json& d) { d["extensions"]["KHR_lights_punctual"]["lights"][1]["type"] = "area"; });
    CHECK(unknownLight.ok && unknownLight.scene.fileLights.size() == 2 && hasWarning(unknownLight, "area"));
}

void testMeshoptModels() {
    const char* const pairs[][2] = {{"skin_arm.glb", "meshopt_exact_skin_arm.glb"}, {"skin_morph.glb", "meshopt_exact_skin_morph.glb"}, {"morph_cube.glb", "meshopt_exact_morph_cube.glb"}, {"skin_arm.glb", "meshopt_quant_skin_arm.glb"}, {"skin_morph.glb", "meshopt_quant_skin_morph.glb"}, {"morph_cube.glb", "meshopt_quant_morph_cube.glb"}};
    if (!meshoptAvailable()) {
        const GltfResult refused = loadModelFile("meshopt_exact_skin_arm.glb");
        CHECK(!refused.ok && refused.error.find("NYREXVJ_MESHOPT") != std::string::npos);
        return;
    }
    for (const auto& pair : pairs) {
        const GltfResult plain = loadModelFile(pair[0]);
        const GltfResult packed = loadModelFile(pair[1]);
        CHECK(plain.ok && packed.ok);
        if (!plain.ok || !packed.ok) {
            std::fprintf(stderr, "%s: %s\n", pair[1], packed.error.c_str());
            continue;
        }
        const bool exact = std::string(pair[1]).find("exact") != std::string::npos;
        const float animatedEps = 0.01f;
        CHECK(packed.scene.skins.size() == plain.scene.skins.size());
        CHECK(packed.scene.meshes[0].targets.size() == plain.scene.meshes[0].targets.size());
        CHECK(sceneStats(packed.scene).triangles == sceneStats(plain.scene).triangles);
        for (const float t : {0.0f, 0.5f, 1.0f, 1.7f}) {
            const int plainAnimation = plain.scene.animations.empty() ? -1 : 0;
            const int packedAnimation = packed.scene.animations.empty() ? -1 : 0;
            const float eps = exact && t == 0.0f ? 1e-4f : animatedEps;
            const bool match = sameVertexSets(worldVertices(plain.scene, plainAnimation, t), worldVertices(packed.scene, packedAnimation, t), eps);
            CHECK(match);
            if (!match) {
                std::fprintf(stderr, "%s differs at t=%.1f\n", pair[1], static_cast<double>(t));
            }
        }
    }
}

void testMeshoptFallback() {
    const GltfResult plain = loadModelFile("skin_morph.glb");
    const GltfResult withFallback = loadModelFile("meshopt_fallback_skin_morph.glb");
    CHECK(plain.ok && withFallback.ok);
    if (!plain.ok || !withFallback.ok) {
        std::fprintf(stderr, "fallback model: %s\n", withFallback.error.c_str());
        return;
    }
    CHECK(withFallback.scene.skins.size() == plain.scene.skins.size());
    CHECK(withFallback.scene.meshes[0].targets.size() == plain.scene.meshes[0].targets.size());
    for (const float t : {0.0f, 1.0f, 1.7f}) {
        CHECK(sameVertexSets(worldVertices(plain.scene, 0, t), worldVertices(withFallback.scene, 0, t), 0.002f));
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nvj_meshopt_fallback_test";
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    std::filesystem::copy_file(kModels / "meshopt_fallback_skin_morph.glb", dir / "alone.glb", error);
    const GltfResult alone = loadGltfFile(dir / "alone.glb");
    if (meshoptAvailable()) {
        CHECK(alone.ok);
    } else {
        CHECK(!alone.ok);
    }
    std::filesystem::remove_all(dir, error);
}

void testMeshoptCorruption() {
    if (!meshoptAvailable()) {
        return;
    }
    const Blob original = splitGlb("meshopt_exact_skin_morph.glb");
    CHECK(loadBlob(original).ok);
    size_t start = std::numeric_limits<size_t>::max();
    size_t end = 0;
    for (const json& view : original.doc["bufferViews"]) {
        if (view.contains("extensions")) {
            const json& ext = view["extensions"]["EXT_meshopt_compression"];
            start = std::min(start, ext.value("byteOffset", static_cast<size_t>(0)));
            end = std::max(end, ext.value("byteOffset", static_cast<size_t>(0)) + ext.value("byteLength", static_cast<size_t>(0)));
        }
    }
    CHECK(start < end && end <= original.bin.size());
    uint32_t state = 12345;
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    int rejected = 0;
    int accepted = 0;
    for (int round = 0; round < 300; ++round) {
        Blob damaged = original;
        const int flips = 1 + static_cast<int>(next() % 4);
        for (int f = 0; f < flips; ++f) {
            damaged.bin[start + next() % (end - start)] ^= static_cast<uint8_t>(1u << (next() % 8));
        }
        const GltfResult result = loadBlob(damaged);
        (result.ok ? accepted : rejected)++;
        if (result.ok) {
            std::string error;
            CHECK(validateScene(result.scene, error));
        }
    }
    CHECK(rejected + accepted == 300 && rejected > 0);

    Blob truncated = original;
    truncated.bin.resize(start + (end - start) / 2);
    truncated.doc["buffers"][0]["byteLength"] = truncated.bin.size();
    CHECK(!loadBlob(truncated).ok);

    Blob badMode = original;
    for (json& view : badMode.doc["bufferViews"]) {
        if (view.contains("extensions")) {
            view["extensions"]["EXT_meshopt_compression"]["mode"] = "SPIRAL";
            break;
        }
    }
    CHECK(!loadBlob(badMode).ok);
    Blob badCount = original;
    for (json& view : badCount.doc["bufferViews"]) {
        if (view.contains("extensions")) {
            view["extensions"]["EXT_meshopt_compression"]["count"] = 1;
            break;
        }
    }
    CHECK(!loadBlob(badCount).ok);
    GltfLimits tiny;
    tiny.maxFileBytes = 1024;
    CHECK(!loadModelFile("meshopt_exact_skin_morph.glb", tiny).ok);
}

void testDracoModels() {
    const std::filesystem::path path = kModels / "draco_skin_morph.glb";
    if (!std::filesystem::exists(path)) {
        return;
    }
    if (!dracoAvailable()) {
        const GltfResult refused = loadModelFile("draco_skin_morph.glb");
        CHECK(!refused.ok && refused.error.find("NYREXVJ_DRACO") != std::string::npos);
        return;
    }
    for (const char* name : {"skin_arm.glb", "skin_morph.glb", "morph_cube.glb"}) {
        const std::string packedName = std::string("draco_") + name;
        const GltfResult plain = loadModelFile(name);
        const GltfResult packed = loadModelFile(packedName);
        CHECK(plain.ok && packed.ok);
        if (!plain.ok || !packed.ok) {
            std::fprintf(stderr, "%s: %s\n", packedName.c_str(), packed.error.c_str());
            continue;
        }
        CHECK(sceneStats(packed.scene).triangles == sceneStats(plain.scene).triangles);
        CHECK(packed.scene.meshes[0].positions.size() == plain.scene.meshes[0].positions.size());
        CHECK(packed.scene.skins.size() == plain.scene.skins.size());
        CHECK(packed.scene.meshes[0].targets.size() == plain.scene.meshes[0].targets.size());
        for (const float t : {0.0f, 1.0f}) {
            const int plainAnimation = plain.scene.animations.empty() ? -1 : 0;
            const int packedAnimation = packed.scene.animations.empty() ? -1 : 0;
            CHECK(sameVertexSets(worldVertices(plain.scene, plainAnimation, t), worldVertices(packed.scene, packedAnimation, t), 0.01f));
        }
    }
    const Blob original = splitGlb("draco_skin_morph.glb");
    size_t start = std::numeric_limits<size_t>::max();
    size_t end = 0;
    for (const json& primitive : original.doc["meshes"][0]["primitives"]) {
        const int viewIndex = primitive["extensions"]["KHR_draco_mesh_compression"]["bufferView"].get<int>();
        const json& view = original.doc["bufferViews"][static_cast<size_t>(viewIndex)];
        start = view.value("byteOffset", static_cast<size_t>(0));
        end = start + view.value("byteLength", static_cast<size_t>(0));
    }
    uint32_t state = 777;
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    int rejected = 0;
    for (int round = 0; round < 200; ++round) {
        Blob damaged = original;
        const int flips = 1 + static_cast<int>(next() % 4);
        for (int f = 0; f < flips; ++f) {
            damaged.bin[start + next() % (end - start)] ^= static_cast<uint8_t>(1u << (next() % 8));
        }
        const GltfResult result = loadBlob(damaged);
        if (!result.ok) {
            ++rejected;
        } else {
            std::string error;
            CHECK(validateScene(result.scene, error));
        }
    }
    CHECK(rejected > 0);
    Blob truncated = original;
    truncated.bin.resize(start + (end - start) / 3);
    truncated.doc["buffers"][0]["byteLength"] = truncated.bin.size();
    CHECK(!loadBlob(truncated).ok);
}

int main() {
    testSkinning();
    testReleasedGeometryStillSkins();
    testSkinRejections();
    testMorphTargets();
    testSkinAndMorphTogether();
    testBlendAndFileLights();
    testMeshoptModels();
    testMeshoptFallback();
    testMeshoptCorruption();
    testDracoModels();
    testMath();
    testProcedural();
    testSphere();
    testAnimation();
    testRoundTrip();
    testMinimalAndNormals();
    testRejections();
    testDataUriAndFiles();
    testTextures();
    testPbrMaterials();
    testInterruption();
    testDeterministicAndSmall();
    return testing::finish();
}

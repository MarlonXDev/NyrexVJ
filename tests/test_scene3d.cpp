#include "check.hpp"

#include "scene3d/gltf.hpp"
#include "scene3d/procedural.hpp"
#include "scene3d/scene_math.hpp"
#include "scene3d/scene_model.hpp"

#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    CHECK(builtinScenes().size() == 3);
    for (const BuiltinSceneInfo& info : builtinScenes()) {
        const Scene scene = info.build();
        std::string error;
        CHECK(validateScene(scene, error));
        CHECK(scene.paletteTint);
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
    for (const BuiltinSceneInfo& info : builtinScenes()) {
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
        CHECK(colorsLoaded == colorsOriginal && colorsLoaded > 0);
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
    CHECK(!dracoResult.ok && dracoResult.error.find("KHR_draco_mesh_compression") != std::string::npos);

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
        CHECK(result.scene.materials[1].alphaMode == AlphaMode::Opaque);
        CHECK(near(result.scene.materials[1].metallic, 1.0f) && near(result.scene.materials[1].roughness, 1.0f));
        CHECK(near(result.scene.materials[2].metallic, 1.0f) && near(result.scene.materials[2].roughness, 0.0f));
        CHECK(result.scene.materials[2].emissive[0] == 1.0f);
    }
    bool blendWarning = false;
    for (const std::string& warning : result.warnings) {
        blendWarning = blendWarning || warning.find("BLEND") != std::string::npos;
    }
    CHECK(blendWarning);

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

int main() {
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

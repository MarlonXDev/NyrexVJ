#include "check.hpp"

#include "core/clip_pack.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/gltf.hpp"
#include "scene3d/procedural.hpp"
#include "scene3d/scene_doc.hpp"
#include "scene3d/scene_pack.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace nvj;

bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

std::vector<uint8_t> bytesOf(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> tinyPng() {
    return {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0};
}

std::vector<PackEntry> sampleEntries() {
    return {{"scene.json", bytesOf("{\"a\":1}")}, {"assets/models/a.glb", bytesOf("glTFxxxxxxxx")}, {"assets/textures/t.png", tinyPng()}};
}

void testContainerRoundTrip() {
    std::vector<uint8_t> file;
    std::string error;
    CHECK(writePack(sampleEntries(), file, error));
    std::vector<PackEntry> back;
    CHECK(readPack(file.data(), file.size(), back, error));
    CHECK(back.size() == 3);
    CHECK(back[0].name == "scene.json");
    CHECK(back[0].data == bytesOf("{\"a\":1}"));
    CHECK(back[2].data == tinyPng());
    std::vector<uint8_t> again;
    CHECK(writePack(back, again, error));
    CHECK(again == file);
    std::vector<uint8_t> empty;
    CHECK(writePack({}, empty, error));
    CHECK(readPack(empty.data(), empty.size(), back, error));
    CHECK(back.empty());
}

void testContainerRejections() {
    std::vector<uint8_t> file;
    std::string error;
    CHECK(writePack(sampleEntries(), file, error));
    std::vector<PackEntry> out;
    for (size_t length = 0; length < file.size(); ++length) {
        CHECK(!readPack(file.data(), length, out, error));
    }
    CHECK(!readPack(nullptr, 0, out, error));

    std::vector<uint8_t> broken = file;
    broken[0] = 'X';
    CHECK(!readPack(broken.data(), broken.size(), out, error));

    broken = file;
    broken[8] = 9;
    CHECK(!readPack(broken.data(), broken.size(), out, error));
    CHECK(error.find("newer") != std::string::npos);

    broken = file;
    broken[8] = 0;
    CHECK(!readPack(broken.data(), broken.size(), out, error));

    broken = file;
    broken.back() ^= 0x55;
    CHECK(!readPack(broken.data(), broken.size(), out, error));
    CHECK(error.find("damaged") != std::string::npos);

    broken = file;
    broken[12] = 0xFF;
    broken[13] = 0xFF;
    broken[14] = 0xFF;
    broken[15] = 0x7F;
    CHECK(!readPack(broken.data(), broken.size(), out, error));

    broken = file;
    const size_t firstSizeField = 16 + 2 + 10;
    for (int i = 0; i < 8; ++i) {
        broken[firstSizeField + static_cast<size_t>(i)] = 0xFF;
    }
    CHECK(!readPack(broken.data(), broken.size(), out, error));

    broken = file;
    const size_t firstOffsetField = firstSizeField + 8;
    broken[firstOffsetField + 7] = 0x80;
    CHECK(!readPack(broken.data(), broken.size(), out, error));

    broken = file;
    broken[firstOffsetField] = 0;
    broken[firstOffsetField + 1] = 0;
    CHECK(!readPack(broken.data(), broken.size(), out, error));

    PackLimits tight;
    tight.maxEntries = 2;
    CHECK(!readPack(file.data(), file.size(), out, error, tight));
    PackLimits small;
    small.maxFileBytes = 40;
    CHECK(!readPack(file.data(), file.size(), out, error, small));
    PackLimits tinyScene;
    tinyScene.maxSceneBytes = 3;
    CHECK(!readPack(file.data(), file.size(), out, error, tinyScene));
}

void testContainerNames() {
    CHECK(isSafePackName("scene.json"));
    CHECK(isSafePackName("assets/models/a b.glb"));
    CHECK(!isSafePackName(""));
    CHECK(!isSafePackName("/etc/passwd"));
    CHECK(!isSafePackName("../x"));
    CHECK(!isSafePackName("a/../b"));
    CHECK(!isSafePackName("a//b"));
    CHECK(!isSafePackName("a\\b"));
    CHECK(!isSafePackName("C:evil"));
    CHECK(!isSafePackName("a/"));
    CHECK(!isSafePackName("name."));
    CHECK(!isSafePackName(std::string(129, 'a')));
    CHECK(!isSafePackName(std::string("a\0b", 3)));
    std::vector<uint8_t> file;
    std::string error;
    CHECK(!writePack({{"../x", {}}}, file, error));
    CHECK(!writePack({{"a", {}}, {"A", {}}}, file, error));
    PackLimits limits;
    limits.maxEntryBytes = 4;
    CHECK(!writePack({{"a", bytesOf("12345")}}, file, error, limits));
}

SceneDoc richDoc() {
    SceneDoc doc;
    doc.name = "Rich";
    doc.exposure = 1.3f;
    doc.sky.top = {0.1f, 0.2f, 0.3f};
    doc.camera.distance = 1.4f;
    doc.camera.fovDegrees = 52.0f;
    doc.camera.bindings[0] = {true, BindSource::Kick, BindTarget::Zoom, 0.2f};
    DocObject a = makeDefaultObject(PrimitiveKind::Torus);
    a.name = "Ring";
    a.position = {1.0f, 2.0f, 3.0f};
    a.rotation = {10.0f, 20.0f, 30.0f};
    a.scale = {1.5f, 2.0f, 0.5f};
    a.repeat = {RepeatKind::Ring, 7, 3.0f, 1.0f};
    a.material.color = {0.2f, 0.4f, 0.6f};
    a.material.metallic = 0.9f;
    a.material.roughness = 0.2f;
    a.material.emissive = {1.0f, 0.5f, 0.0f};
    a.material.emissiveStrength = 2.0f;
    a.material.texture = "wood.png";
    a.material.followPalette = true;
    a.animations[0] = {true, MotionKind::Sway, 0.5f, 20.0f, 2, 0.1f};
    a.animations[1] = {true, MotionKind::Bob, 0.3f, 0.4f, 1, 0.0f};
    a.bindings[0] = {true, BindSource::High, BindTarget::Emissive, 1.5f};
    doc.objects.push_back(a);
    DocObject b = makeDefaultObject(PrimitiveKind::Model);
    b.model = "robot.glb";
    b.framing = false;
    b.visible = false;
    doc.objects.push_back(b);
    doc.lights.push_back(makeDefaultLight(false));
    DocLight p = makeDefaultLight(true);
    p.bindings[0] = {true, BindSource::Bass, BindTarget::Light, 0.7f};
    doc.lights.push_back(p);
    return doc;
}

void testDocRoundTrip() {
    const SceneDoc doc = richDoc();
    const std::string text = docToJson(doc);
    SceneDoc back;
    std::string error;
    CHECK(docFromJson(text, back, error));
    CHECK(docToJson(back) == text);
    CHECK(back.objects.size() == 2);
    CHECK(back.objects[0].repeat.kind == RepeatKind::Ring && back.objects[0].repeat.count == 7);
    CHECK(back.objects[0].animations[0].kind == MotionKind::Sway && back.objects[0].animations[0].axis == 2);
    CHECK(back.objects[0].bindings[0].target == BindTarget::Emissive);
    CHECK(back.objects[1].model == "robot.glb" && !back.objects[1].visible && !back.objects[1].framing);
    CHECK(back.lights.size() == 2 && back.lights[1].point);
    CHECK(back.lights[1].bindings[0].enabled && back.lights[1].bindings[0].target == BindTarget::Light);
    CHECK(back.camera.bindings[0].target == BindTarget::Zoom);
    CHECK(near(back.camera.fovDegrees, 52.0f));
}

void testCameras() {
    SceneDoc doc = makeDefaultDoc();
    CHECK(docCameraCount(doc) == 1);
    CHECK(!docRemoveCamera(doc, 0));
    CHECK(!docRemoveCamera(doc, 5));
    doc.camera.name = "Wide";
    doc.camera.fovDegrees = 70.0f;
    DocCamera close;
    close.distance = 0.5f;
    close.yawDegrees = 90.0f;
    close.bindings[0] = {true, BindSource::Kick, BindTarget::Zoom, 0.1f};
    const int second = docAddCamera(doc, close);
    CHECK(second == 1);
    CHECK(doc.extraCameras[0].name == "Camera 2");
    const int third = docAddCamera(doc, doc.camera);
    CHECK(third == 2);
    CHECK(docCameraCount(doc) == 3);
    CHECK(near(docCameraAt(doc, 1).distance, 0.5f));
    CHECK(near(docCameraAt(doc, 99).fovDegrees, docCameraAt(doc, 2).fovDegrees));
    CHECK(near(docCameraAt(doc, -4).fovDegrees, 70.0f));
    doc.activeCamera = 1;
    doc.cutBeats = 4.0f;

    SceneDoc back;
    std::string error;
    CHECK(docFromJson(docToJson(doc), back, error));
    CHECK(docCameraCount(back) == 3);
    CHECK(back.activeCamera == 1);
    CHECK(near(back.cutBeats, 4.0f));
    CHECK(back.camera.name == "Wide");
    CHECK(back.extraCameras[0].name == "Camera 2");
    CHECK(near(back.extraCameras[0].yawDegrees, 90.0f));
    CHECK(back.extraCameras[0].bindings[0].target == BindTarget::Zoom);
    CHECK(docToJson(back) == docToJson(doc));

    SceneDoc single = makeDefaultDoc();
    const std::string singleText = docToJson(single);
    CHECK(singleText.find("\"cameras\"") == std::string::npos);
    CHECK(singleText.find("cutBeats") == std::string::npos);
    SceneDoc legacy;
    CHECK(docFromJson(R"({"format":"nyxscene","version":1,"name":"Old","camera":{"distance":1.5,"fov":60}})", legacy, error));
    CHECK(docCameraCount(legacy) == 1);
    CHECK(near(legacy.camera.distance, 1.5f));
    CHECK(legacy.camera.name == "Camera 1");
    CHECK(legacy.activeCamera == 0 && legacy.cutBeats == 0.0f);

    SceneDoc crowded = makeDefaultDoc();
    for (size_t i = 0; i < kDocMaxCameras + 4; ++i) {
        docAddCamera(crowded, DocCamera{});
    }
    CHECK(static_cast<size_t>(docCameraCount(crowded)) == kDocMaxCameras);
    CHECK(docAddCamera(crowded, DocCamera{}) == -1);
    SceneDoc broken = crowded;
    broken.activeCamera = 99;
    broken.cutBeats = 9999.0f;
    broken.extraCameras[2].fovDegrees = 5000.0f;
    broken.extraCameras[3].name = "";
    sanitizeDoc(broken);
    CHECK(broken.activeCamera == docCameraCount(broken) - 1);
    CHECK(near(broken.cutBeats, kDocMaxCutBeats));
    CHECK(broken.extraCameras[2].fovDegrees <= 110.0f);
    CHECK(!broken.extraCameras[3].name.empty());

    SceneDoc removal = makeDefaultDoc();
    docAddCamera(removal, DocCamera{});
    docAddCamera(removal, DocCamera{});
    removal.extraCameras[0].distance = 2.0f;
    removal.extraCameras[1].distance = 3.0f;
    removal.activeCamera = 2;
    CHECK(docRemoveCamera(removal, 1));
    CHECK(docCameraCount(removal) == 2);
    CHECK(removal.activeCamera == 1);
    CHECK(near(docCameraAt(removal, 1).distance, 3.0f));
    removal.activeCamera = 1;
    CHECK(docRemoveCamera(removal, 1));
    CHECK(removal.activeCamera == 0 && docCameraCount(removal) == 1);
    docAddCamera(removal, DocCamera{});
    removal.extraCameras[0].distance = 2.5f;
    removal.activeCamera = 0;
    CHECK(docRemoveCamera(removal, 0));
    CHECK(near(removal.camera.distance, 2.5f));
    CHECK(removal.activeCamera == 0);

    SceneDoc shots = makeDefaultDoc();
    shots.camera.yawDegrees = 10.0f;
    DocCamera b;
    b.yawDegrees = 20.0f;
    docAddCamera(shots, b);
    b.yawDegrees = 30.0f;
    docAddCamera(shots, b);
    shots.activeCamera = 1;
    GltfResult plain = buildSceneFromDoc(shots, DocAssets{});
    CHECK(plain.ok);
    CHECK(plain.scene.cameras.empty());
    CHECK(near(plain.scene.camera.yawDegrees, 20.0f));
    CHECK(near(activeSceneCamera(plain.scene, 100.0).yawDegrees, 20.0f));
    shots.cutBeats = 4.0f;
    GltfResult cut = buildSceneFromDoc(shots, DocAssets{});
    CHECK(cut.ok);
    CHECK(cut.scene.cameras.size() == 3);
    CHECK(near(activeSceneCamera(cut.scene, 0.0).yawDegrees, 10.0f));
    CHECK(near(activeSceneCamera(cut.scene, 3.9).yawDegrees, 10.0f));
    CHECK(near(activeSceneCamera(cut.scene, 4.0).yawDegrees, 20.0f));
    CHECK(near(activeSceneCamera(cut.scene, 9.0).yawDegrees, 30.0f));
    CHECK(near(activeSceneCamera(cut.scene, 12.0).yawDegrees, 10.0f));
    CHECK(near(activeSceneCamera(cut.scene, -5.0).yawDegrees, 10.0f));
    CHECK(near(activeSceneCamera(cut.scene, std::nan("")).yawDegrees, 20.0f));
    std::string invalid;
    CHECK(validateScene(cut.scene, invalid));
    cut.scene.cameras[1].fovDegrees = 400.0f;
    CHECK(!validateScene(cut.scene, invalid));
}

void testDocRejections() {
    SceneDoc doc;
    std::string error;
    CHECK(!docFromJson("", doc, error));
    CHECK(!docFromJson("not json", doc, error));
    CHECK(!docFromJson("[]", doc, error));
    CHECK(!docFromJson("{\"format\":\"other\",\"version\":1}", doc, error));
    CHECK(!docFromJson("{\"format\":\"nyxscene\"}", doc, error));
    CHECK(!docFromJson("{\"format\":\"nyxscene\",\"version\":0}", doc, error));
    CHECK(!docFromJson("{\"format\":\"nyxscene\",\"version\":99}", doc, error));
    CHECK(error.find("newer") != std::string::npos);
    CHECK(!docFromJson(std::string(2000, '[') + std::string(2000, ']'), doc, error));
    CHECK(docFromJson("{\"format\":\"nyxscene\",\"version\":1,\"unknown\":{\"a\":[1,2]},\"objects\":[{\"kind\":\"sphere\",\"extra\":true}]}", doc, error));
    CHECK(doc.objects.size() == 1);

    CHECK(docFromJson("{\"format\":\"nyxscene\",\"version\":1,\"exposure\":1e99,\"camera\":{\"fov\":-5,\"distance\":0},\"objects\":[{\"kind\":\"cube\",\"scale\":[0,1e30,-0.0000001],\"position\":[1e30,0,0],\"repeat\":{\"kind\":\"ring\",\"count\":99999},\"material\":{\"roughness\":0,\"metallic\":5,\"texture\":\"../evil.png\",\"color\":[2,-1,0.5]},\"animations\":[{\"kind\":\"spin\",\"speed\":1e9}]}]}", doc, error));
    const DocObject& o = doc.objects[0];
    CHECK(doc.exposure <= 4.0f);
    CHECK(doc.camera.fovDegrees >= 15.0f && doc.camera.distance >= 0.2f);
    CHECK(std::fabs(o.scale.x) >= 0.001f && std::fabs(o.scale.y) <= 1000.0f && std::fabs(o.scale.z) >= 0.001f);
    CHECK(o.position.x <= 1000.0f);
    CHECK(o.repeat.count == kSceneMaxRepeat);
    CHECK(o.material.roughness >= 0.04f && o.material.metallic <= 1.0f);
    CHECK(o.material.texture.empty());
    CHECK(o.material.color[0] == 1.0f && o.material.color[1] == 0.0f);
    CHECK(o.animations[0].speed <= 8.0f);

    SceneDoc huge;
    huge.objects.assign(kDocMaxObjects + 30, makeDefaultObject(PrimitiveKind::Cube));
    huge.lights.assign(9, makeDefaultLight(true));
    sanitizeDoc(huge);
    CHECK(huge.objects.size() == kDocMaxObjects && huge.lights.size() == kDocMaxLights);

    SceneDoc nan;
    nan.objects.push_back(makeDefaultObject(PrimitiveKind::Cube));
    nan.objects[0].position.x = std::numeric_limits<float>::quiet_NaN();
    nan.objects[0].material.roughness = std::numeric_limits<float>::infinity();
    sanitizeDoc(nan);
    CHECK(std::isfinite(nan.objects[0].position.x) && std::isfinite(nan.objects[0].material.roughness));
}

void testAssetNames() {
    CHECK(isModelAssetName("robot.glb"));
    CHECK(isModelAssetName("My Robot.GLB"));
    CHECK(!isModelAssetName("robot.gltf"));
    CHECK(!isModelAssetName("../robot.glb"));
    CHECK(!isModelAssetName("a/robot.glb"));
    CHECK(!isModelAssetName(".hidden.glb"));
    CHECK(!isModelAssetName(""));
    CHECK(isTextureAssetName("a.png"));
    CHECK(isTextureAssetName("a.JPG"));
    CHECK(isTextureAssetName("a.jpeg"));
    CHECK(!isTextureAssetName("a.bmp"));
    CHECK(!isTextureAssetName("a.png\\x"));
    CHECK(!isTextureAssetName(std::string(97, 'a') + ".png"));
}

void testPrimitives() {
    for (int i = 0; i < kPrimitiveKindCount; ++i) {
        const PrimitiveKind kind = static_cast<PrimitiveKind>(i);
        if (kind == PrimitiveKind::Model) {
            continue;
        }
        const SceneMesh mesh = primitiveMesh(kind);
        CHECK(!mesh.positions.empty());
        CHECK(mesh.normals.size() == mesh.positions.size());
        CHECK(mesh.uvs.size() == mesh.positions.size() * 2);
        CHECK(mesh.colors.empty());
        CHECK(mesh.indices.size() % 3 == 0 && !mesh.indices.empty());
        bool outward = true;
        bool unit = true;
        float extent = 0.0f;
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const Vec3& a = mesh.positions[mesh.indices[t]];
            const Vec3& b = mesh.positions[mesh.indices[t + 1]];
            const Vec3& c = mesh.positions[mesh.indices[t + 2]];
            const Vec3 face = cross(b - a, c - a);
            const Vec3 average = mesh.normals[mesh.indices[t]] + mesh.normals[mesh.indices[t + 1]] + mesh.normals[mesh.indices[t + 2]];
            if (length(face) > 1e-9f && dot(face, average) <= 0.0f) {
                outward = false;
            }
        }
        for (size_t v = 0; v < mesh.positions.size(); ++v) {
            if (!near(length(mesh.normals[v]), 1.0f, 0.01f)) {
                unit = false;
            }
            extent = std::max(extent, length(mesh.positions[v]));
        }
        CHECK(outward);
        CHECK(unit);
        CHECK(extent > 0.3f && extent < 1.2f);
        Scene scene;
        scene.materials.push_back(SceneMaterial{});
        SceneMesh copy = mesh;
        copy.material = 0;
        scene.meshes.push_back(copy);
        SceneNode node;
        node.meshes = {0};
        scene.nodes.push_back(node);
        scene.roots = {0};
        std::string error;
        CHECK(validateScene(scene, error));
    }
    CHECK(primitiveMesh(PrimitiveKind::Cube).positions.size() == 24);
    PrimitiveKind kind = PrimitiveKind::Sphere;
    CHECK(primitiveFromKey("torusknot", kind) && kind == PrimitiveKind::TorusKnot);
    CHECK(!primitiveFromKey("teapot", kind));
}

DocAssets modelAssets(const std::vector<uint8_t>& glb) {
    DocAssets assets;
    assets.loadModel = [glb](const std::string& name, const GltfLimits& limits) -> std::shared_ptr<const GltfResult> {
        if (name != "knot.glb") {
            return nullptr;
        }
        return std::make_shared<GltfResult>(loadGltfMemory(glb.data(), glb.size(), std::filesystem::path(), limits));
    };
    assets.loadImage = [](const std::string&, std::string& error) -> std::shared_ptr<const SceneImage> {
        error = "not here";
        return nullptr;
    };
    return assets;
}

void testBuild() {
    const SceneDoc def = makeDefaultDoc();
    GltfResult result = buildSceneFromDoc(def, DocAssets{});
    CHECK(result.ok);
    CHECK(result.scene.shading == Shading::Pbr);
    CHECK(result.scene.meshes.size() == 1 && result.scene.nodes.size() == 1);
    CHECK(result.scene.lights.size() == 1);
    CHECK(result.scene.camera.authored);
    CHECK(result.scene.nodes[0].motions.size() == 1);
    CHECK(near(result.scene.materials[0].metallic, 1.0f));
    CHECK(result.scene.materials[0].baseColor[0] > result.scene.materials[0].baseColor[2]);

    SceneDoc empty;
    CHECK(!buildSceneFromDoc(empty, DocAssets{}).ok);

    SceneDoc hidden = def;
    hidden.objects[0].visible = false;
    CHECK(!buildSceneFromDoc(hidden, DocAssets{}).ok);

    SceneDoc missing = def;
    DocObject model = makeDefaultObject(PrimitiveKind::Model);
    model.model = "gone.glb";
    missing.objects.push_back(model);
    DocObject textured = makeDefaultObject(PrimitiveKind::Cube);
    textured.material.texture = "none.png";
    missing.objects.push_back(textured);
    result = buildSceneFromDoc(missing, DocAssets{});
    CHECK(result.ok);
    CHECK(result.warnings.size() == 2);
    CHECK(result.scene.meshes.size() == 2);

    SceneDoc onlyMissing;
    onlyMissing.objects.push_back(model);
    CHECK(!buildSceneFromDoc(onlyMissing, DocAssets{}).ok);

    const std::vector<uint8_t> glb = writeGlb(makeTorusKnotScene());
    SceneDoc withModel = def;
    DocObject knot = makeDefaultObject(PrimitiveKind::Model);
    knot.model = "knot.glb";
    knot.repeat = {RepeatKind::Line, 3, 1.0f, 2.0f};
    withModel.objects.push_back(knot);
    withModel.objects.push_back(knot);
    result = buildSceneFromDoc(withModel, modelAssets(glb));
    CHECK(result.ok);
    CHECK(result.scene.meshes.size() == 3);
    CHECK(result.scene.nodes.size() == 1 + 2 * 2);
    std::string error;
    CHECK(validateScene(result.scene, error));
    CHECK(evaluateScene(result.scene, result.scene.animations.empty() ? -1 : 0, 0.0f).size() == 1 + 3 + 3);

    std::atomic<bool> cancel{true};
    GltfLimits limits;
    limits.cancel = &cancel;
    result = buildSceneFromDoc(withModel, modelAssets(glb), limits);
    CHECK(!result.ok && result.interrupted);

    GltfLimits small;
    small.maxVertices = 100;
    CHECK(!buildSceneFromDoc(def, DocAssets{}, small).ok);
    GltfLimits fewNodes;
    fewNodes.maxNodes = 1;
    CHECK(!buildSceneFromDoc(withModel, modelAssets(glb), fewNodes).ok);
}

void testEvaluate() {
    Scene scene;
    scene.materials.push_back(SceneMaterial{});
    SceneMesh mesh = primitiveMesh(PrimitiveKind::Cube);
    mesh.material = 0;
    scene.meshes.push_back(mesh);
    SceneNode node;
    node.meshes = {0};
    scene.nodes.push_back(node);
    scene.roots = {0};

    auto at = [&](float t, const SceneAudio* audio = nullptr) { return evaluateScene(scene, -1, t, audio); };
    CHECK(at(0.0f).size() == 1);

    scene.nodes[0].motions = {{MotionKind::Bob, 1.0f, 2.0f, 1, 0.0f}};
    CHECK(near(at(0.25f)[0].world.m[13], 2.0f));
    CHECK(near(at(0.0f)[0].world.m[13], 0.0f));

    scene.nodes[0].motions = {{MotionKind::Orbit, 1.0f, 3.0f, 1, 0.0f}};
    CHECK(near(at(0.0f)[0].world.m[12], 3.0f) && near(at(0.25f)[0].world.m[14], 3.0f));

    scene.nodes[0].motions = {{MotionKind::Pulse, 1.0f, 0.5f, 1, 0.0f}};
    CHECK(near(at(0.25f)[0].world.m[0], 1.5f));

    scene.nodes[0].motions = {{MotionKind::Spin, 1.0f, 1.0f, 1, 0.0f}};
    const Mat4 quarter = at(0.25f)[0].world;
    CHECK(near(quarter.m[0], 0.0f, 0.01f) && near(std::fabs(quarter.m[2]), 1.0f, 0.01f));

    scene.nodes[0].motions = {{MotionKind::Sway, 1.0f, 0.5f, 2, 0.0f}};
    CHECK(near(at(0.25f)[0].world.m[1], std::sin(0.5f), 0.01f));
    scene.nodes[0].motions.clear();

    scene.nodes[0].repeat = {RepeatKind::Ring, 4, 2.0f, 1.0f};
    std::vector<SceneInstance> ring = at(0.0f);
    CHECK(ring.size() == 4);
    for (const SceneInstance& instance : ring) {
        CHECK(near(std::sqrt(instance.world.m[12] * instance.world.m[12] + instance.world.m[14] * instance.world.m[14]), 2.0f));
    }
    CHECK(near(ring[0].world.m[12], 2.0f) && near(ring[1].world.m[14], 2.0f));
    scene.nodes[0].repeat = {RepeatKind::Line, 3, 0.0f, 2.0f};
    std::vector<SceneInstance> line = at(0.0f);
    CHECK(line.size() == 3 && near(line[0].world.m[12], -2.0f) && near(line[2].world.m[12], 2.0f));
    scene.nodes[0].repeat = {RepeatKind::Ring, 100000, 1.0f, 1.0f};
    CHECK(at(0.0f).size() == static_cast<size_t>(kSceneMaxRepeat));
    scene.nodes[0].repeat = NodeRepeat{};

    scene.nodes[0].motions = {{MotionKind::Bob, 1.0f, 1.0f, 1, 0.25f}};
    scene.nodes[0].repeat = {RepeatKind::Line, 2, 0.0f, 1.0f};
    std::vector<SceneInstance> phased = at(0.0f);
    CHECK(near(phased[0].world.m[13], 0.0f) && near(phased[1].world.m[13], 1.0f));
    scene.nodes[0].motions.clear();
    scene.nodes[0].repeat = NodeRepeat{};

    scene.nodes[0].bindings = {{BindSource::Bass, BindTarget::Scale, 1.0f}, {BindSource::Kick, BindTarget::Emissive, 2.0f}, {BindSource::High, BindTarget::Lift, 3.0f}};
    SceneAudio audio;
    audio.bass = 0.5f;
    audio.kick = 1.0f;
    audio.high = 0.5f;
    const SceneInstance reactive = at(0.0f, &audio)[0];
    CHECK(near(reactive.world.m[0], 1.5f));
    CHECK(near(reactive.emissive, 2.0f));
    CHECK(near(reactive.world.m[13], 1.5f));
    CHECK(near(at(0.0f)[0].world.m[0], 1.0f));
    scene.nodes[0].bindings = {{BindSource::Mid, BindTarget::Scale, -100.0f}};
    audio.mid = 1.0f;
    CHECK(at(0.0f, &audio)[0].world.m[0] > 0.0f);

    CHECK(near(bindingSum({{BindSource::Level, BindTarget::Light, 2.0f}, {BindSource::Level, BindTarget::Zoom, 5.0f}}, BindTarget::Light, SceneAudio{0, 0, 0, 0.5f, 0}), 1.0f));
}

void testFraming() {
    SceneDoc doc;
    DocObject sphere = makeDefaultObject(PrimitiveKind::Sphere);
    doc.objects.push_back(sphere);
    DocObject floor = makeDefaultObject(PrimitiveKind::Plane);
    floor.scale = {100.0f, 1.0f, 100.0f};
    doc.objects.push_back(floor);
    GltfResult result = buildSceneFromDoc(doc, DocAssets{});
    CHECK(result.ok);
    SceneSphere framed = sceneSphere(result.scene, -1, 1);
    CHECK(framed.valid && framed.radius < 2.0f);
    doc.objects[1].framing = true;
    result = buildSceneFromDoc(doc, DocAssets{});
    framed = sceneSphere(result.scene, -1, 1);
    CHECK(framed.radius > 40.0f);
    doc.objects[0].framing = false;
    doc.objects[1].framing = false;
    result = buildSceneFromDoc(doc, DocAssets{});
    CHECK(sceneSphere(result.scene, -1, 1).valid);
}

void testChannelLoop() {
    Scene scene = makeOrbitRingsScene();
    SceneChannel channel;
    channel.node = 0;
    channel.path = AnimPath::Translation;
    channel.times = {0.0f, 1.0f};
    channel.values = {0, 0, 0, 10, 0, 0};
    channel.loop = 1.0f;
    scene.animations.clear();
    SceneAnimation animation;
    animation.duration = 4.0f;
    animation.channels = {channel};
    scene.animations.push_back(animation);
    const std::vector<SceneInstance> a = evaluateScene(scene, 0, 0.5f);
    const std::vector<SceneInstance> b = evaluateScene(scene, 0, 2.5f);
    CHECK(!a.empty() && a.size() == b.size());
    CHECK(near(a[0].world.m[12], b[0].world.m[12]));
}

std::filesystem::path scratch(const char* name) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "nvj_creator_tests" / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void testScenePack() {
    const std::filesystem::path root = scratch("pack");
    const std::vector<uint8_t> glb = writeGlb(makeTorusKnotScene());
    writeFile(root / "models" / "knot.glb", glb);
    writeFile(root / "textures" / "dot.png", tinyPng());

    SceneDoc doc = makeDefaultDoc();
    DocObject knot = makeDefaultObject(PrimitiveKind::Model);
    knot.model = "knot.glb";
    knot.material.texture = "dot.png";
    DocObject lost = makeDefaultObject(PrimitiveKind::Cube);
    lost.material.normalTexture = "lost.png";
    doc.objects.push_back(knot);
    doc.objects.push_back(lost);

    AssetSource source({root / "models", root / "textures"});
    ScenePack pack;
    std::vector<std::string> warnings;
    CHECK(source.collect(doc, pack, warnings));
    CHECK(pack.models.size() == 1 && pack.textures.size() == 1);
    CHECK(warnings.size() == 1);

    const std::filesystem::path file = root / "clips" / "test.nyxclip";
    std::string error;
    CHECK(writeScenePackFile(file, pack, error));
    CHECK(std::filesystem::exists(file));
    CHECK(!std::filesystem::exists(std::filesystem::path(file).concat(".tmp")));

    ScenePack back;
    CHECK(readScenePackFile(file, back, error));
    CHECK(docToJson(back.doc) == docToJson(pack.doc));
    CHECK(back.models.at("knot.glb") == glb);
    CHECK(back.textures.at("dot.png") == tinyPng());

    AssetSource fromPack({root / "none", root / "none"}, std::make_shared<ScenePack>(back));
    const DocAssets assets = fromPack.assets();
    const std::shared_ptr<const GltfResult> model = assets.loadModel("knot.glb", GltfLimits{});
    CHECK(model && model->ok);
    CHECK(assets.loadModel("knot.glb", GltfLimits{}) == model);
    CHECK(!assets.loadModel("other.glb", GltfLimits{})->ok);
    CHECK(!assets.loadModel("../knot.glb", GltfLimits{})->ok);
    std::string imageError;
    CHECK(!assets.loadImage("../dot.png", imageError));

    GltfResult built = buildSceneFromDoc(back.doc, assets);
    CHECK(built.ok);

    ScenePack bad = pack;
    bad.models["x.gltf"] = glb;
    std::vector<uint8_t> sink;
    CHECK(!writeScenePack(bad, sink, error));
    bad = pack;
    bad.models["fake.glb"] = bytesOf("not a model");
    CHECK(!writeScenePack(bad, sink, error));
    bad = pack;
    bad.textures["fake.png"] = bytesOf("not an image");
    CHECK(!writeScenePack(bad, sink, error));

    std::vector<uint8_t> noScene;
    CHECK(writePack({{"assets/models/knot.glb", glb}}, noScene, error));
    CHECK(!readScenePack(noScene.data(), noScene.size(), back, error));
    std::vector<uint8_t> badScene;
    CHECK(writePack({{"scene.json", bytesOf("{\"format\":\"nyxscene\"}")}}, badScene, error));
    CHECK(!readScenePack(badScene.data(), badScene.size(), back, error));

    writeFile(root / "truncated.nyxclip", std::vector<uint8_t>(20, 0));
    CHECK(!readScenePackFile(root / "truncated.nyxclip", back, error));
    CHECK(!readScenePackFile(root / "missing.nyxclip", back, error));
}

}

int main() {
    testContainerRoundTrip();
    testContainerRejections();
    testContainerNames();
    testDocRoundTrip();
    testCameras();
    testDocRejections();
    testAssetNames();
    testPrimitives();
    testBuild();
    testEvaluate();
    testFraming();
    testChannelLoop();
    testScenePack();
    return testing::finish();
}

#include "check.hpp"

#include "control/control_system.hpp"
#include "core/preset.hpp"
#include "core/settings.hpp"
#include "render/clip_library.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/gltf.hpp"
#include "scene3d/scene_doc.hpp"
#include "scene3d/scene_model.hpp"
#include "scene3d/scene_pack.hpp"
#include "scene3d/scene_world.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

using namespace nvj;

bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

bool nearVec(Vec3 a, Vec3 b, float eps = 0.001f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

Vec3 translationOf(const Mat4& m) {
    return {m.m[12], m.m[13], m.m[14]};
}

struct Clip {
    float x;
    float y;
    float z;
    float w;
};

Clip clipOf(const Mat4& m, Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13], m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14], m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}

DocObject sphereAt(const std::string& name, Vec3 position) {
    DocObject object = makeDefaultObject(PrimitiveKind::Sphere);
    object.name = name;
    object.position = position;
    return object;
}

SceneDoc flatDoc() {
    SceneDoc doc;
    doc.name = "flat";
    doc.objects.push_back(sphereAt("A", {0.0f, 0.0f, 0.0f}));
    doc.objects.push_back(sphereAt("B", {2.0f, 0.0f, 0.0f}));
    doc.objects.push_back(sphereAt("C", {0.0f, 2.0f, 0.0f}));
    sanitizeDoc(doc);
    return doc;
}

void testIds() {
    CHECK(isSafeObjectId("o1") && isSafeObjectId("a_b-C9") && isSafeObjectId(std::string(24, 'x')));
    CHECK(!isSafeObjectId("") && !isSafeObjectId(std::string(25, 'x')) && !isSafeObjectId("a b") && !isSafeObjectId("ä") && !isSafeObjectId("a/b") && !isSafeObjectId("a\"b"));

    SceneDoc doc = flatDoc();
    CHECK(doc.objects[0].id == "o1" && doc.objects[1].id == "o2" && doc.objects[2].id == "o3");
    SceneDoc again = doc;
    sanitizeDoc(again);
    CHECK(again.objects[0].id == "o1" && again.objects[1].id == "o2" && again.objects[2].id == "o3");

    SceneDoc messy;
    messy.objects.push_back(sphereAt("A", {}));
    messy.objects.push_back(sphereAt("B", {}));
    messy.objects.push_back(sphereAt("C", {}));
    messy.objects.push_back(sphereAt("D", {}));
    messy.objects[0].id = "keep";
    messy.objects[1].id = "keep";
    messy.objects[2].id = "bad id!";
    messy.objects[3].id = std::string(60, 'z');
    sanitizeDoc(messy);
    std::vector<std::string> seen;
    for (const DocObject& o : messy.objects) {
        CHECK(isSafeObjectId(o.id));
        for (const std::string& other : seen) {
            CHECK(other != o.id);
        }
        seen.push_back(o.id);
    }
    CHECK(messy.objects[0].id == "keep");

    const std::string fresh = docNewObjectId(doc);
    CHECK(isSafeObjectId(fresh) && docObjectById(doc, fresh) < 0);
    doc.objects.push_back(sphereAt("D", {}));
    doc.objects.back().id = fresh;
    CHECK(docNewObjectId(doc) != fresh);
    CHECK(docObjectById(doc, "o2") == 1 && docObjectById(doc, "") < 0 && docObjectById(doc, "nope") < 0);
}

void testHierarchyOperations() {
    SceneDoc doc = flatDoc();
    CHECK(!docUsesHierarchy(doc));
    CHECK(!docCanParent(doc, 0, 0));
    CHECK(docCanParent(doc, 1, 0));
    CHECK(docSetParent(doc, 1, 0));
    CHECK(doc.objects[1].parent == "o1" && docUsesHierarchy(doc));
    CHECK(docSetParent(doc, 2, 1));
    CHECK(docObjectDepth(doc, 0) == 0 && docObjectDepth(doc, 1) == 1 && docObjectDepth(doc, 2) == 2);
    CHECK(!docCanParent(doc, 0, 2));
    CHECK(!docCanParent(doc, 0, 1));
    CHECK(!docSetParent(doc, 0, 2));
    CHECK(doc.objects[0].parent.empty());
    CHECK(docSetParent(doc, 2, -1));
    CHECK(doc.objects[2].parent.empty());
    CHECK(!docSetParent(doc, 5, 0));
    CHECK(!docSetParent(doc, 0, 9));

    SceneDoc deep;
    for (int i = 0; i < 12; ++i) {
        deep.objects.push_back(sphereAt("N" + std::to_string(i), {}));
    }
    sanitizeDoc(deep);
    int linked = 0;
    for (int i = 1; i < 12; ++i) {
        if (!docSetParent(deep, i, i - 1)) {
            break;
        }
        ++linked;
    }
    CHECK(linked == kDocMaxDepth);
    CHECK(docObjectDepth(deep, kDocMaxDepth) == kDocMaxDepth);
    CHECK(!docCanParent(deep, kDocMaxDepth + 1, kDocMaxDepth));
    CHECK(deep.objects[static_cast<size_t>(kDocMaxDepth + 1)].parent.empty());
    CHECK(!docCanParent(deep, 0, 11));
    for (int i = 0; i < 12; ++i) {
        CHECK(docObjectDepth(deep, i) <= kDocMaxDepth);
    }

    SceneDoc order = flatDoc();
    order.objects.push_back(sphereAt("D", {}));
    sanitizeDoc(order);
    CHECK(docSetParent(order, 0, 3));
    const std::vector<int> listed = docObjectOrder(order);
    CHECK(listed.size() == 4);
    CHECK(listed == std::vector<int>({1, 2, 3, 0}));

    SceneDoc removal = flatDoc();
    CHECK(docSetParent(removal, 1, 0));
    CHECK(docSetParent(removal, 2, 1));
    docRemoveObject(removal, 1);
    CHECK(removal.objects.size() == 2);
    CHECK(removal.objects[0].name == "A" && removal.objects[1].name == "C");
    CHECK(removal.objects[1].parent == "o1");
    CHECK(near(removal.objects[1].position.y, 2.0f));
    docRemoveObject(removal, 0);
    CHECK(removal.objects.size() == 1 && removal.objects[0].parent.empty());
    docRemoveObject(removal, 7);
    CHECK(removal.objects.size() == 1);
}

void testSerialization() {
    SceneDoc flat = flatDoc();
    CHECK(docRequiredVersion(flat) == kDocBaseVersion);
    const std::string flatText = docToJson(flat);
    const nlohmann::json flatJson = nlohmann::json::parse(flatText);
    CHECK(flatJson["version"] == 1);
    for (const nlohmann::json& object : flatJson["objects"]) {
        CHECK(!object.contains("id") && !object.contains("parent"));
    }

    SceneDoc tree = flatDoc();
    CHECK(docSetParent(tree, 1, 0));
    CHECK(docRequiredVersion(tree) == kDocHierarchyVersion);
    const std::string text = docToJson(tree);
    const nlohmann::json parsed = nlohmann::json::parse(text);
    CHECK(parsed["version"] == 4);
    CHECK(parsed["objects"][0]["id"] == "o1" && parsed["objects"][1]["id"] == "o2" && parsed["objects"][2]["id"] == "o3");
    CHECK(!parsed["objects"][0].contains("parent") && parsed["objects"][1]["parent"] == "o1" && !parsed["objects"][2].contains("parent"));

    SceneDoc back;
    std::string error;
    CHECK(docFromJson(text, back, error));
    CHECK(back.notes.empty());
    CHECK(docToJson(back) == text);
    CHECK(back.objects[1].parent == "o1" && back.objects[1].id == "o2");

    ScenePack pack;
    pack.doc = tree;
    std::vector<uint8_t> bytes;
    CHECK(writeScenePack(pack, bytes, error));
    ScenePack loaded;
    CHECK(readScenePack(bytes.data(), bytes.size(), loaded, error));
    CHECK(docToJson(loaded.doc) == text);

    SceneDoc readFlat;
    CHECK(docFromJson(flatText, readFlat, error));
    CHECK(docToJson(readFlat) == flatText);
    CHECK(readFlat.objects[0].id == "o1" && readFlat.objects[2].id == "o3");
    CHECK(readFlat.notes.empty());

    nlohmann::json future = parsed;
    future["version"] = 6;
    CHECK(!docFromJson(future.dump(), back, error));
    CHECK(error.find("newer NyrexVJ") != std::string::npos);

    for (const int version : {1, 2, 3}) {
        nlohmann::json old = flatJson;
        old["version"] = version;
        SceneDoc read;
        CHECK(docFromJson(old.dump(), read, error));
        CHECK(read.objects.size() == 3 && !docUsesHierarchy(read));
        CHECK(docToJson(read) == flatText);
    }

    SceneDoc withCameraOnly = flatDoc();
    withCameraOnly.glide = 0.5f;
    CHECK(docRequiredVersion(withCameraOnly) == kDocCameraVersion);
    const nlohmann::json cameraJson = nlohmann::json::parse(docToJson(withCameraOnly));
    CHECK(cameraJson["version"] == 3);
    CHECK(!cameraJson["objects"][0].contains("id"));

    SceneDoc both = tree;
    both.glide = 0.5f;
    CHECK(docRequiredVersion(both) == kDocHierarchyVersion);
    SceneDoc bothBack;
    CHECK(docFromJson(docToJson(both), bothBack, error));
    CHECK(near(bothBack.glide, 0.5f) && bothBack.objects[1].parent == "o1");
}

void testRepair() {
    nlohmann::json base = nlohmann::json::parse(docToJson([] {
        SceneDoc doc = flatDoc();
        docSetParent(doc, 1, 0);
        return doc;
    }()));

    nlohmann::json selfParent = base;
    selfParent["objects"][0]["parent"] = "o1";
    SceneDoc read;
    std::string error;
    CHECK(docFromJson(selfParent.dump(), read, error));
    CHECK(read.objects[0].parent.empty());
    CHECK(!read.notes.empty());

    nlohmann::json missing = base;
    missing["objects"][2]["parent"] = "ghost";
    CHECK(docFromJson(missing.dump(), read, error));
    CHECK(read.objects[2].parent.empty());
    CHECK(read.notes.size() == 1);
    CHECK(read.notes[0].find("does not exist") != std::string::npos);

    nlohmann::json loop = base;
    loop["objects"][0]["parent"] = "o2";
    CHECK(docFromJson(loop.dump(), read, error));
    for (int i = 0; i < 3; ++i) {
        CHECK(docObjectDepth(read, i) <= kDocMaxDepth);
    }
    bool anyRoot = false;
    for (const DocObject& o : read.objects) {
        anyRoot = anyRoot || o.parent.empty();
    }
    CHECK(anyRoot);
    CHECK(!read.notes.empty());

    nlohmann::json chain = nlohmann::json::parse(R"({"format":"nyxscene","version":4,"name":"x","objects":[]})");
    for (int i = 0; i < 14; ++i) {
        nlohmann::json o = {{"id", "n" + std::to_string(i)}, {"name", "N"}, {"kind", "sphere"}};
        if (i > 0) {
            o["parent"] = "n" + std::to_string(i - 1);
        }
        chain["objects"].push_back(o);
    }
    CHECK(docFromJson(chain.dump(), read, error));
    for (int i = 0; i < static_cast<int>(read.objects.size()); ++i) {
        CHECK(docObjectDepth(read, i) <= kDocMaxDepth);
    }
    CHECK(!read.notes.empty());

    nlohmann::json odd = base;
    odd["objects"][1]["parent"] = 7;
    odd["objects"][2]["id"] = "o2";
    odd["objects"][0]["id"] = std::string(100, 'q');
    CHECK(docFromJson(odd.dump(), read, error));
    std::vector<std::string> ids;
    for (const DocObject& o : read.objects) {
        CHECK(isSafeObjectId(o.id));
        ids.push_back(o.id);
    }
    CHECK(ids[0] != ids[1] && ids[1] != ids[2] && ids[0] != ids[2]);
    CHECK(read.objects[1].parent.empty() || docObjectById(read, read.objects[1].parent) >= 0);
}

DocAssets modelAssets() {
    DocAssets assets;
    assets.loadModel = [](const std::string& name, const GltfLimits& limits) -> std::shared_ptr<const GltfResult> {
        const std::filesystem::path models = std::filesystem::path(NYREXVJ_TEST_DATA) / "models";
        if (name == "cube.glb") {
            return std::make_shared<GltfResult>(loadGltfFile(models / "morph_cube.glb", limits));
        }
        if (name == "arm.glb") {
            return std::make_shared<GltfResult>(loadGltfFile(models / "skin_morph.glb", limits));
        }
        return nullptr;
    };
    assets.loadImage = [](const std::string&, std::string& error) -> std::shared_ptr<const SceneImage> {
        error = "not here";
        return nullptr;
    };
    return assets;
}

SceneEval evaluate(const Scene& scene, const SceneAudio* audio = nullptr) {
    return evaluateSceneFull(scene, scene.animations.empty() ? -1 : 0, 0.0f, audio);
}

void testBuildInheritance() {
    SceneDoc doc;
    DocObject parent = sphereAt("Parent", {2.0f, 0.0f, 0.0f});
    parent.scale = {2.0f, 2.0f, 2.0f};
    DocObject child = sphereAt("Child", {1.0f, 0.0f, 0.0f});
    child.scale = {0.5f, 0.5f, 0.5f};
    DocObject grand = sphereAt("Grand", {0.0f, 1.0f, 0.0f});
    DocObject loose = sphereAt("Loose", {-3.0f, 0.0f, 0.0f});
    doc.objects = {parent, child, grand, loose};
    sanitizeDoc(doc);
    CHECK(docSetParent(doc, 1, 0));
    CHECK(docSetParent(doc, 2, 1));

    const GltfResult built = buildSceneFromDoc(doc, DocAssets{});
    CHECK(built.ok);
    CHECK(built.warnings.empty());
    std::string error;
    CHECK(validateScene(built.scene, error));
    CHECK(built.scene.roots.size() == 2);
    const SceneEval eval = evaluate(built.scene);
    CHECK(eval.instances.size() == 4);
    int found = 0;
    for (const SceneInstance& instance : eval.instances) {
        const Vec3 at = translationOf(instance.world);
        const float scaleX = std::sqrt(instance.world.m[0] * instance.world.m[0] + instance.world.m[1] * instance.world.m[1] + instance.world.m[2] * instance.world.m[2]);
        if (nearVec(at, {2.0f, 0.0f, 0.0f})) {
            CHECK(near(scaleX, 2.0f));
            ++found;
        } else if (nearVec(at, {4.0f, 0.0f, 0.0f})) {
            CHECK(near(scaleX, 1.0f));
            ++found;
        } else if (nearVec(at, {4.0f, 1.0f * 1.0f * 0.5f * 2.0f, 0.0f}, 0.01f)) {
            CHECK(near(scaleX, 1.0f));
            ++found;
        } else if (nearVec(at, {-3.0f, 0.0f, 0.0f})) {
            ++found;
        }
    }
    CHECK(found == 4);

    SceneDoc turned;
    DocObject arm = sphereAt("Arm", {0.0f, 0.0f, 0.0f});
    arm.rotation = {0.0f, 90.0f, 0.0f};
    DocObject hand = sphereAt("Hand", {1.0f, 0.0f, 0.0f});
    turned.objects = {arm, hand};
    sanitizeDoc(turned);
    CHECK(docSetParent(turned, 1, 0));
    const GltfResult turnedBuilt = buildSceneFromDoc(turned, DocAssets{});
    CHECK(turnedBuilt.ok);
    const SceneEval turnedEval = evaluate(turnedBuilt.scene);
    bool handFound = false;
    for (const SceneInstance& instance : turnedEval.instances) {
        const Vec3 at = translationOf(instance.world);
        if (std::fabs(at.z) > 0.5f) {
            CHECK(near(at.x, 0.0f, 0.01f) && near(std::fabs(at.z), 1.0f, 0.01f));
            handFound = true;
        }
    }
    CHECK(handFound);
}

void testBuildVisibility() {
    SceneDoc doc;
    doc.objects = {sphereAt("Parent", {0.0f, 0.0f, 0.0f}), sphereAt("Child", {1.0f, 0.0f, 0.0f}), sphereAt("Grand", {2.0f, 0.0f, 0.0f}), sphereAt("Other", {5.0f, 0.0f, 0.0f})};
    sanitizeDoc(doc);
    CHECK(docSetParent(doc, 1, 0));
    CHECK(docSetParent(doc, 2, 1));
    doc.objects[0].visible = false;
    const GltfResult built = buildSceneFromDoc(doc, DocAssets{});
    CHECK(built.ok);
    CHECK(evaluate(built.scene).instances.size() == 1);
    doc.objects[0].visible = true;
    doc.objects[1].visible = false;
    const GltfResult partly = buildSceneFromDoc(doc, DocAssets{});
    CHECK(partly.ok);
    CHECK(evaluate(partly.scene).instances.size() == 2);
    doc.objects[1].visible = true;
    const GltfResult all = buildSceneFromDoc(doc, DocAssets{});
    CHECK(evaluate(all.scene).instances.size() == 4);

    SceneDoc orphan;
    orphan.objects = {sphereAt("Alone", {}), sphereAt("Lost", {1.0f, 0.0f, 0.0f})};
    sanitizeDoc(orphan);
    orphan.objects[1].parent = "ghost";
    const GltfResult lost = buildSceneFromDoc(orphan, DocAssets{});
    CHECK(lost.ok);
    CHECK(lost.scene.roots.size() == 2);
    CHECK(evaluate(lost.scene).instances.size() == 2);
}

void testBindingInheritance() {
    SceneDoc doc;
    DocObject parent = sphereAt("Parent", {});
    parent.bindings[0] = {true, BindSource::Bass, BindTarget::Hue, 0.5f};
    DocObject child = sphereAt("Child", {1.0f, 0.0f, 0.0f});
    DocObject other = sphereAt("Other", {4.0f, 0.0f, 0.0f});
    doc.objects = {parent, child, other};
    sanitizeDoc(doc);
    CHECK(docSetParent(doc, 1, 0));
    const GltfResult built = buildSceneFromDoc(doc, DocAssets{});
    CHECK(built.ok);
    SceneAudio audio;
    audio.bass = 1.0f;
    audio.materials = true;
    const SceneEval eval = evaluate(built.scene, &audio);
    float childHue = 0.0f;
    float otherHue = 0.0f;
    float parentHue = 0.0f;
    for (const SceneInstance& instance : eval.instances) {
        const Vec3 at = translationOf(instance.world);
        if (nearVec(at, {1.0f, 0.0f, 0.0f})) {
            childHue = instance.hue;
        } else if (nearVec(at, {4.0f, 0.0f, 0.0f})) {
            otherHue = instance.hue;
        } else {
            parentHue = instance.hue;
        }
    }
    CHECK(std::fabs(parentHue) > 0.01f);
    CHECK(near(childHue, parentHue, 0.001f));
    CHECK(near(otherHue, 0.0f, 0.0001f));
}

void testModelReuse() {
    SceneDoc single;
    DocObject cube = makeDefaultObject(PrimitiveKind::Model);
    cube.model = "cube.glb";
    cube.name = "One";
    single.objects = {cube};
    sanitizeDoc(single);
    const GltfResult one = buildSceneFromDoc(single, modelAssets());
    CHECK(one.ok);
    const SceneStats oneStats = sceneStats(one.scene);

    SceneDoc triple;
    DocObject a = cube;
    DocObject b = cube;
    b.name = "Two";
    b.position = {2.0f, 0.0f, 0.0f};
    DocObject c = cube;
    c.name = "Three";
    c.position = {0.0f, 2.0f, 0.0f};
    c.repeat = {RepeatKind::Line, 3, 1.0f, 2.0f};
    triple.objects = {a, b, c};
    sanitizeDoc(triple);
    const GltfResult three = buildSceneFromDoc(triple, modelAssets());
    CHECK(three.ok);
    const SceneStats threeStats = sceneStats(three.scene);
    CHECK(threeStats.meshes == oneStats.meshes);
    CHECK(threeStats.vertices == oneStats.vertices);
    CHECK(three.scene.materials.size() == one.scene.materials.size());
    CHECK(three.scene.images.size() == one.scene.images.size());
    std::string error;
    CHECK(validateScene(three.scene, error));
    const SceneEval eval = evaluate(three.scene);
    CHECK(eval.instances.size() == 1 + 1 + 3);
    for (const SceneInstance& instance : eval.instances) {
        CHECK(instance.mesh >= 0 && instance.mesh < static_cast<int>(three.scene.meshes.size()));
    }

    SceneDoc mixed;
    DocObject arm = makeDefaultObject(PrimitiveKind::Model);
    arm.model = "arm.glb";
    mixed.objects = {cube, arm, cube, arm};
    mixed.objects[2].position = {2.0f, 0.0f, 0.0f};
    mixed.objects[3].position = {-2.0f, 0.0f, 0.0f};
    sanitizeDoc(mixed);
    const GltfResult mixedBuilt = buildSceneFromDoc(mixed, modelAssets());
    CHECK(mixedBuilt.ok);
    CHECK(validateScene(mixedBuilt.scene, error));
    CHECK(mixedBuilt.scene.skins.size() == 2);
    SceneDoc armOnly;
    armOnly.objects = {arm};
    sanitizeDoc(armOnly);
    const GltfResult armBuilt = buildSceneFromDoc(armOnly, modelAssets());
    SceneDoc cubeOnly;
    cubeOnly.objects = {cube};
    sanitizeDoc(cubeOnly);
    CHECK(sceneStats(mixedBuilt.scene).meshes == sceneStats(armBuilt.scene).meshes + oneStats.meshes);

    GltfLimits tight;
    tight.maxVertices = oneStats.vertices + 4;
    const GltfResult limited = buildSceneFromDoc(triple, modelAssets(), tight);
    CHECK(limited.ok);

    SceneDoc parented = triple;
    CHECK(docSetParent(parented, 1, 0));
    const GltfResult parentedBuilt = buildSceneFromDoc(parented, modelAssets());
    CHECK(parentedBuilt.ok);
    CHECK(validateScene(parentedBuilt.scene, error));
    CHECK(evaluate(parentedBuilt.scene).instances.size() == 5);
    CHECK(sceneStats(parentedBuilt.scene).vertices == oneStats.vertices);
}

void testPlacement() {
    WorldPlacement none;
    CHECK(worldPlacementIsIdentity(none));
    WorldPlacement moved;
    moved.x = 0.5f;
    CHECK(!worldPlacementIsIdentity(moved));

    const Vec3 ownerCentre{1.0f, 2.0f, 3.0f};
    const Mat4 same = worldPlacementMatrix(ownerCentre, 2.0f, ownerCentre, 2.0f, none);
    CHECK(nearVec(transformPoint(same, {4.0f, 5.0f, 6.0f}), {4.0f, 5.0f, 6.0f}));

    const Vec3 memberCentre{-10.0f, 0.0f, 5.0f};
    const Mat4 fit = worldPlacementMatrix(ownerCentre, 2.0f, memberCentre, 0.5f, none);
    CHECK(nearVec(transformPoint(fit, memberCentre), ownerCentre));
    const Vec3 edge = transformPoint(fit, memberCentre + Vec3{0.5f, 0.0f, 0.0f});
    CHECK(near(edge.x - ownerCentre.x, 2.0f));

    WorldPlacement shifted;
    shifted.x = 1.0f;
    shifted.y = -0.5f;
    shifted.z = 2.0f;
    const Mat4 offset = worldPlacementMatrix(ownerCentre, 2.0f, memberCentre, 0.5f, shifted);
    CHECK(nearVec(transformPoint(offset, memberCentre), {1.0f + 2.0f, 2.0f - 1.0f, 3.0f + 4.0f}));

    WorldPlacement turned;
    turned.yawDegrees = 90.0f;
    const Mat4 yaw = worldPlacementMatrix(ownerCentre, 1.0f, memberCentre, 1.0f, turned);
    CHECK(nearVec(transformPoint(yaw, memberCentre + Vec3{1.0f, 0.0f, 0.0f}), ownerCentre + Vec3{0.0f, 0.0f, -1.0f}));
    CHECK(nearVec(transformPoint(yaw, memberCentre + Vec3{0.0f, 1.0f, 0.0f}), ownerCentre + Vec3{0.0f, 1.0f, 0.0f}));

    WorldPlacement big;
    big.scale = 2.0f;
    const Mat4 scaled = worldPlacementMatrix(ownerCentre, 1.0f, memberCentre, 1.0f, big);
    CHECK(near(transformPoint(scaled, memberCentre + Vec3{1.0f, 0.0f, 0.0f}).x - ownerCentre.x, 2.0f));

    WorldPlacement wild;
    wild.x = 99.0f;
    wild.yawDegrees = -999.0f;
    wild.scale = 0.0f;
    const WorldPlacement clamped = clampWorldPlacement(wild);
    CHECK(clamped.x == kWorldOffsetLimit && clamped.yawDegrees == -kWorldYawLimit && clamped.scale == kWorldScaleMin);

    const Mat4 tiny = worldPlacementMatrix(ownerCentre, 0.0f, memberCentre, 0.0f, none);
    CHECK(std::isfinite(tiny.m[0]) && std::isfinite(tiny.m[12]));
}

void testFrustum() {
    const Mat4 view = lookAt({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const Mat4 projection = perspective(60.0f * 0.0174532925f, 16.0f / 9.0f, 0.1f, 20.0f);
    const FrustumPlanes frustum = frustumFromViewProjection(projection * view);
    CHECK(sphereMayBeVisible(frustum, {0.0f, 0.0f, 0.0f}, 0.5f));
    CHECK(sphereMayBeVisible(frustum, {0.0f, 0.0f, 0.0f}, 0.0f));
    CHECK(!sphereMayBeVisible(frustum, {0.0f, 0.0f, 10.0f}, 0.5f));
    CHECK(!sphereMayBeVisible(frustum, {0.0f, 0.0f, -30.0f}, 0.5f));
    CHECK(!sphereMayBeVisible(frustum, {50.0f, 0.0f, 0.0f}, 1.0f));
    CHECK(!sphereMayBeVisible(frustum, {-50.0f, 0.0f, 0.0f}, 1.0f));
    CHECK(!sphereMayBeVisible(frustum, {0.0f, 50.0f, 0.0f}, 1.0f));
    CHECK(!sphereMayBeVisible(frustum, {0.0f, -50.0f, 0.0f}, 1.0f));
    CHECK(sphereMayBeVisible(frustum, {50.0f, 0.0f, 0.0f}, 60.0f));
    const float halfWidth = std::tan(30.0f * 0.0174532925f) * 5.0f * (16.0f / 9.0f);
    CHECK(sphereMayBeVisible(frustum, {halfWidth + 0.2f, 0.0f, 0.0f}, 0.5f));
    CHECK(!sphereMayBeVisible(frustum, {halfWidth + 1.5f, 0.0f, 0.0f}, 0.5f));

    int visible = 0;
    int total = 0;
    for (int x = -20; x <= 20; ++x) {
        for (int z = -20; z <= 4; ++z) {
            const Vec3 point{static_cast<float>(x), 0.0f, static_cast<float>(z)};
            const Clip clip = clipOf(projection * view, point);
            const bool inside = clip.w > 0.0f && std::fabs(clip.x) <= clip.w && std::fabs(clip.y) <= clip.w && clip.z >= -clip.w && clip.z <= clip.w;
            ++total;
            if (inside) {
                ++visible;
                CHECK(sphereMayBeVisible(frustum, point, 0.0f));
            }
        }
    }
    CHECK(visible > 0 && visible < total);

    Mat4 doubled = scaling({2.0f, 3.0f, 1.0f});
    CHECK(near(largestAxisScale(doubled), 3.0f));
    CHECK(near(largestAxisScale(Mat4{}), 1.0f));
}

class QuietHost final : public ControlHost {
public:
    void triggerClip(int, int) override {}
    void triggerColumn(int) override {}
    void clearLayer(int) override {}
    void switchDeck(int) override {}
    void tap() override {}
    void resync() override {}
    bool blackout() const override { return false; }
    void setBlackout(bool) override {}
};

void testLayerFields() {
    const Layer fresh;
    CHECK(fresh.world == 0 && fresh.worldX == 0.0f && fresh.worldY == 0.0f && fresh.worldZ == 0.0f && fresh.worldYaw == 0.0f && fresh.worldScale == 1.0f);

    Composition plain;
    const std::string plainText = compositionToJson(plain);
    for (const char* key : {"\"world\"", "worldX", "worldY", "worldZ", "worldYaw", "worldScale"}) {
        CHECK(plainText.find(key) == std::string::npos);
    }

    Composition comp;
    comp.layers[1].world = 2;
    comp.layers[1].worldX = 1.5f;
    comp.layers[1].worldY = -0.25f;
    comp.layers[1].worldZ = 3.0f;
    comp.layers[1].worldYaw = -45.0f;
    comp.layers[1].worldScale = 0.5f;
    comp.layers[3].world = 2;
    const std::string text = compositionToJson(comp);
    const nlohmann::json json = nlohmann::json::parse(text);
    CHECK(json["layers"][1]["world"] == 2 && json["layers"][3]["world"] == 2);
    CHECK(!json["layers"][0].contains("world") && !json["layers"][3].contains("worldX") && !json["layers"][3].contains("worldScale"));
    Composition loaded;
    std::string error;
    CHECK(compositionFromJson(text, loaded, error));
    CHECK(loaded.layers[1] == comp.layers[1] && loaded.layers[3] == comp.layers[3]);
    CHECK(loaded.layers[1].world == 2 && near(loaded.layers[1].worldX, 1.5f) && near(loaded.layers[1].worldYaw, -45.0f) && near(loaded.layers[1].worldScale, 0.5f));
    CHECK(compositionToJson(loaded) == text);

    nlohmann::json wild = json;
    for (nlohmann::json& layer : wild["layers"]) {
        layer["world"] = 99;
        layer["worldX"] = 400.0f;
        layer["worldY"] = "up";
        layer["worldZ"] = -400.0f;
        layer["worldYaw"] = 9000.0f;
        layer["worldScale"] = 0.0f;
    }
    Composition clamped;
    CHECK(compositionFromJson(wild.dump(), clamped, error));
    for (const Layer& layer : clamped.layers) {
        CHECK(layer.world == kWorldCount && layer.worldX == 4.0f && layer.worldY == 0.0f && layer.worldZ == -4.0f && layer.worldYaw == 180.0f && near(layer.worldScale, 0.1f));
    }
    nlohmann::json negative = json;
    negative["layers"][0]["world"] = -3;
    negative["layers"][1]["world"] = 2.7;
    negative["layers"][2]["world"] = "two";
    Composition odd;
    CHECK(compositionFromJson(negative.dump(), odd, error));
    CHECK(odd.layers[0].world == 0 && odd.layers[1].world >= 2 && odd.layers[1].world <= 3 && odd.layers[2].world == 0);

    for (const Composition& preset : factoryPresets()) {
        for (const Layer& layer : preset.layers) {
            CHECK(layer.world == 0 && layer.worldScale == 1.0f && layer.worldX == 0.0f);
        }
    }
}

void testControls() {
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    const struct {
        const char* id;
        float low;
        float high;
    } controls[] = {{"layer/1/world-x", -4.0f, 4.0f}, {"layer/2/world-y", -4.0f, 4.0f}, {"layer/3/world-z", -4.0f, 4.0f}, {"layer/4/world-yaw", -180.0f, 180.0f}};
    for (const auto& c : controls) {
        const int index = controlTargetIndex(c.id);
        CHECK(index >= 0);
        if (index < 0) {
            continue;
        }
        CHECK(controlTargets()[static_cast<size_t>(index)].kind == ControlKind::Continuous);
        for (const float value : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            CHECK(router.set(index, value, false));
            CHECK(near(router.value(index), value, 0.001f));
        }
        CHECK(router.set(index, 0.5f, false));
    }
    CHECK(near(comp.layers[0].worldX, 0.0f) && near(comp.layers[1].worldY, 0.0f) && near(comp.layers[2].worldZ, 0.0f) && near(comp.layers[3].worldYaw, 0.0f));
    router.set(controlTargetIndex("layer/1/world-x"), 1.0f, false);
    router.set(controlTargetIndex("layer/1/world-y"), 0.0f, false);
    router.set(controlTargetIndex("layer/1/world-yaw"), 1.0f, false);
    CHECK(near(comp.layers[0].worldX, 4.0f) && near(comp.layers[0].worldY, -4.0f) && near(comp.layers[0].worldYaw, 180.0f));
    CHECK(!router.set(controlTargetIndex("layer/1/world-x"), std::numeric_limits<float>::quiet_NaN(), false));
    CHECK(near(comp.layers[0].worldX, 4.0f));
    CHECK(controlTargetIndex("layer/5/world-x") < 0);
    CHECK(controlTargetIndex("layer/1/world") < 0);
    const int last = static_cast<int>(controlTargets().size()) - 1;
    CHECK(controlTargets()[static_cast<size_t>(last)].id == "master/feedback-reset");
}

void testSetting() {
    namespace fs = std::filesystem;
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_world_settings";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", scratch.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", scratch.string().c_str(), 1);
#endif
    const auto readText = [&]() {
        std::ifstream in(scratch / "settings.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto loadWith = [&](const std::string& text) {
        std::ofstream out(scratch / "settings.json", std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        Settings result;
        loadSettings(result);
        return result;
    };
    const Settings defaults;
    CHECK(defaults.sceneWorlds);
    CHECK(saveSettings(defaults));
    CHECK(!nlohmann::json::parse(readText()).contains("sceneWorlds"));
    Settings off;
    off.sceneWorlds = false;
    CHECK(saveSettings(off));
    const nlohmann::json written = nlohmann::json::parse(readText());
    CHECK(written.contains("sceneWorlds") && written["sceneWorlds"] == false);
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(!loaded.sceneWorlds && loaded.sceneCameraMoves && loaded.sceneMaterialBindings);
    CHECK(loadWith("{}").sceneWorlds);
    CHECK(!loadWith("{\"sceneWorlds\": false}").sceneWorlds);
    CHECK(loadWith("{\"sceneWorlds\": \"no\"}").sceneWorlds);
    CHECK(loadWith("{\"sceneWorlds\": null}").sceneWorlds);
    CHECK(loadWith("{\"sceneCameraMoves\": false}").sceneWorlds);
    fs::remove_all(scratch, ec);
}

void testBuiltinsUnchanged() {
    const SceneDoc builtIn = makeDefaultDoc();
    CHECK(!docUsesHierarchy(builtIn));
    CHECK(docRequiredVersion(builtIn) == kDocBaseVersion);
    const nlohmann::json json = nlohmann::json::parse(docToJson(builtIn));
    CHECK(json["version"] == 1);
    for (const nlohmann::json& object : json["objects"]) {
        CHECK(!object.contains("id") && !object.contains("parent"));
    }
    const GltfResult built = buildSceneFromDoc(builtIn, DocAssets{});
    CHECK(built.ok && built.warnings.empty());
    CHECK(built.scene.roots.size() == builtIn.objects.size());
}

bool readModel(const char* file, std::vector<uint8_t>& bytes) {
    std::string error;
    return readFileBytes(pathFromUtf8((std::filesystem::path(NYREXVJ_TEST_DATA) / "models" / file).string()), 4u * 1024u * 1024u, bytes, error);
}

DocObject modelObject(const char* name, const char* model, Vec3 position, float scale) {
    DocObject object = makeDefaultObject(PrimitiveKind::Model);
    object.name = name;
    object.model = model;
    object.position = position;
    object.scale = {scale, scale, scale};
    return object;
}

ScenePack demoPack(const std::string& which) {
    ScenePack pack;
    std::vector<uint8_t> arm;
    std::vector<uint8_t> cube;
    readModel("skin_arm.glb", arm);
    readModel("morph_cube.glb", cube);
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    doc.name = which;
    const auto moon = [](Vec3 position) {
        DocObject object = makeDefaultObject(PrimitiveKind::Sphere);
        object.name = "Moon";
        object.position = position;
        object.scale = {0.25f, 0.25f, 0.25f};
        object.material.color = {0.95f, 0.85f, 0.3f};
        object.material.emissive = {0.9f, 0.7f, 0.1f};
        object.material.emissiveStrength = 0.5f;
        return object;
    };
    const auto spinning = [](DocObject object) {
        object.animations[0] = {true, MotionKind::Spin, 0.35f, 1.0f, 1, 0.0f};
        return object;
    };
    if (which == "World Demo Arm") {
        pack.models["skin_arm.glb"] = arm;
        DocObject first = modelObject("Arm", "skin_arm.glb", {0.0f, 0.0f, 0.0f}, 1.0f);
        DocObject second = modelObject("Arm copy", "skin_arm.glb", {1.4f, 0.0f, 0.0f}, 0.6f);
        second.rotation = {0.0f, 40.0f, 0.0f};
        doc.objects = {first, second};
        sanitizeDoc(doc);
        docSetParent(doc, 1, 0);
    } else if (which == "World Demo Cube") {
        pack.models["morph_cube.glb"] = cube;
        DocObject core = spinning(modelObject("Cube", "morph_cube.glb", {0.0f, 0.0f, 0.0f}, 1.0f));
        DocObject satellite = moon({1.6f, 0.0f, 0.0f});
        doc.objects = {core, satellite};
        sanitizeDoc(doc);
        docSetParent(doc, 1, 0);
    } else {
        pack.models["skin_arm.glb"] = arm;
        pack.models["morph_cube.glb"] = cube;
        DocObject armObject = modelObject("Arm", "skin_arm.glb", {-0.35f, 0.0f, -0.45f}, 1.0f);
        DocObject cubeObject = spinning(modelObject("Cube", "morph_cube.glb", {0.35f, 0.0f, 0.45f}, 1.0f));
        DocObject satellite = moon({1.6f, 0.0f, 0.0f});
        doc.objects = {armObject, cubeObject, satellite};
        sanitizeDoc(doc);
        docSetParent(doc, 2, 1);
    }
    pack.doc = doc;
    return pack;
}

int writeDemo(const std::filesystem::path& folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all(folder, ec);
    fs::create_directories(folder / "clips", ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", folder.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", folder.string().c_str(), 1);
#endif
    for (const char* name : {"World Demo Arm", "World Demo Cube", "Shared Scene Demo"}) {
        std::string error;
        if (!writeScenePackFile(folder / "clips" / (std::string(name) + ".nyxclip"), demoPack(name), error)) {
            std::fprintf(stderr, "could not write %s: %s\n", name, error.c_str());
            return 1;
        }
    }
    Composition comp;
    comp.name = "Shared 3D World";
    const int arm = clipIndexByName("Scene: World Demo Arm");
    const int cube = clipIndexByName("Scene: World Demo Cube");
    if (arm < 0 || cube < 0) {
        std::fprintf(stderr, "the demo clips were not found in the clip library\n");
        return 1;
    }
    comp.layers[0].cells[0] = arm;
    comp.layers[0].activeColumn = 0;
    comp.layers[0].world = 1;
    comp.layers[1].cells[0] = cube;
    comp.layers[1].activeColumn = 0;
    comp.layers[1].world = 1;
    comp.layers[1].worldX = 0.45f;
    comp.layers[1].worldZ = 0.6f;
    comp.layers[1].worldYaw = 20.0f;
    comp.layers[1].worldScale = 0.8f;
    std::string error;
    if (!saveUserPreset(comp, "Shared 3D World", error)) {
        std::fprintf(stderr, "could not write the preset: %s\n", error.c_str());
        return 1;
    }
    std::printf("demo written to %s\n", folder.string().c_str());
    return 0;
}

void testDemoFiles() {
    namespace fs = std::filesystem;
    const fs::path folder = fs::path(NYREXVJ_SOURCE_DIR) / "docs" / "examples" / "shared-3d-world";
    for (const char* name : {"World Demo Arm", "World Demo Cube", "Shared Scene Demo"}) {
        ScenePack pack;
        std::string error;
        const fs::path file = folder / (std::string(name) + ".nyxclip");
        CHECK(fs::exists(file));
        if (!fs::exists(file)) {
            continue;
        }
        CHECK(readScenePackFile(file, pack, error));
        CHECK(pack.doc.notes.empty());
        const auto source = std::make_shared<const ScenePack>(pack);
        AssetSource assets({}, source);
        const GltfResult built = buildSceneFromDoc(pack.doc, assets.assets());
        CHECK(built.ok);
        CHECK(built.warnings.empty());
        CHECK(validateScene(built.scene, error));
        CHECK(docUsesHierarchy(pack.doc));
        if (std::string(name) == "Shared Scene Demo") {
            int models = 0;
            for (const DocObject& object : pack.doc.objects) {
                models += object.kind == PrimitiveKind::Model ? 1 : 0;
            }
            CHECK(models == 2);
            const GltfResult one = buildSceneFromDoc(pack.doc, assets.assets());
            const SceneSphere sphere = sceneSphere(one.scene, -1, 8);
            CHECK(sphere.radius > 0.0f);
            const SceneBounds bounds = sceneBounds(one.scene, -1, 0.0f);
            CHECK(bounds.max.x - bounds.min.x > 0.5f);
        }
    }
    std::string text;
    std::ifstream in(folder / "Shared 3D World.json", std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    CHECK(!text.empty());
    Composition comp;
    std::string error;
    std::vector<std::string> warnings;
    CHECK(compositionFromJson(text, comp, error, &warnings));
    CHECK(comp.layers[0].world == 1 && comp.layers[1].world == 1);
    CHECK(near(comp.layers[1].worldX, 0.45f) && near(comp.layers[1].worldScale, 0.8f));
}

}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--write-demo") {
        return writeDemo(argv[2]);
    }
    testIds();
    testHierarchyOperations();
    testSerialization();
    testRepair();
    testBuildInheritance();
    testBuildVisibility();
    testBindingInheritance();
    testModelReuse();
    testPlacement();
    testFrustum();
    testLayerFields();
    testControls();
    testSetting();
    testBuiltinsUnchanged();
    testDemoFiles();
    return testing::finish();
}

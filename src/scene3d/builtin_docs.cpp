#include "scene3d/builtin_docs.hpp"

#include "scene3d/doc_build.hpp"

namespace nvj {

namespace {

DocAnimation motion(MotionKind kind, float speed, float amount, int axis = 1, float phase = 0.0f) {
    DocAnimation a;
    a.enabled = true;
    a.kind = kind;
    a.speed = speed;
    a.amount = amount;
    a.axis = axis;
    a.phase = phase;
    return a;
}

DocBinding bind(BindSource source, BindTarget target, float amount) {
    DocBinding b;
    b.enabled = true;
    b.source = source;
    b.target = target;
    b.amount = amount;
    return b;
}

DocObject floorPlane(std::array<float, 3> color, float metallic, float roughness, float y) {
    DocObject floor = makeDefaultObject(PrimitiveKind::Plane);
    floor.name = "Floor";
    floor.position = {0.0f, y, 0.0f};
    floor.scale = {14.0f, 1.0f, 14.0f};
    floor.material.color = color;
    floor.material.metallic = metallic;
    floor.material.roughness = roughness;
    return floor;
}

DocLight light(bool point, Vec3 position, std::array<float, 3> color, float intensity) {
    DocLight l = makeDefaultLight(point);
    l.position = position;
    l.color = color;
    l.intensity = intensity;
    return l;
}

SceneDoc chromeOrbs() {
    SceneDoc doc;
    doc.name = "Chrome Orbs";
    doc.sky = {{0.18f, 0.22f, 0.45f}, {0.95f, 0.90f, 0.95f}, {0.05f, 0.04f, 0.07f}, 1.1f};
    doc.camera.distance = 1.12f;
    doc.camera.pitchDegrees = 18.0f;
    doc.camera.orbitSpeed = 0.22f;
    doc.camera.bindings[0] = bind(BindSource::Kick, BindTarget::Zoom, 0.05f);

    DocObject core = makeDefaultObject(PrimitiveKind::Torus);
    core.name = "Core";
    core.scale = {1.5f, 1.5f, 1.5f};
    core.rotation = {90.0f, 0.0f, 0.0f};
    core.position = {0.0f, 0.15f, 0.0f};
    core.material.color = {1.0f, 0.2f, 0.7f};
    core.material.emissive = {1.0f, 0.15f, 0.65f};
    core.material.emissiveStrength = 1.6f;
    core.material.roughness = 0.3f;
    core.animations[0] = motion(MotionKind::Spin, 0.08f, 1.0f, 2);
    core.bindings[0] = bind(BindSource::Bass, BindTarget::Emissive, 1.4f);
    doc.objects.push_back(core);

    DocObject orbs = makeDefaultObject(PrimitiveKind::Sphere);
    orbs.name = "Orbs";
    orbs.position = {0.0f, 0.15f, 0.0f};
    orbs.scale = {0.55f, 0.55f, 0.55f};
    orbs.repeat = {RepeatKind::Ring, 8, 1.7f, 1.0f};
    orbs.material.color = {0.95f, 0.96f, 1.0f};
    orbs.material.metallic = 1.0f;
    orbs.material.roughness = 0.07f;
    orbs.animations[0] = motion(MotionKind::Bob, 0.35f, 0.14f, 1, 0.125f);
    orbs.bindings[0] = bind(BindSource::Kick, BindTarget::Scale, 0.35f);
    doc.objects.push_back(orbs);

    DocObject ring = makeDefaultObject(PrimitiveKind::Torus);
    ring.name = "Orbit ring";
    ring.position = {0.0f, 0.15f, 0.0f};
    ring.rotation = {90.0f, 0.0f, 0.0f};
    ring.scale = {3.55f, 3.55f, 3.55f};
    ring.material.color = {0.55f, 0.57f, 0.65f};
    ring.material.metallic = 1.0f;
    ring.material.roughness = 0.3f;
    ring.animations[0] = motion(MotionKind::Spin, -0.05f, 1.0f, 2);
    doc.objects.push_back(ring);

    doc.objects.push_back(floorPlane({0.02f, 0.02f, 0.03f}, 0.85f, 0.18f, -0.75f));
    doc.lights.push_back(light(false, {0.5f, 0.9f, 0.6f}, {1.0f, 0.97f, 0.92f}, 2.6f));
    DocLight cyan = light(true, {-2.6f, 1.2f, 1.8f}, {0.1f, 0.8f, 1.0f}, 26.0f);
    cyan.bindings[0] = bind(BindSource::Bass, BindTarget::Light, 1.0f);
    doc.lights.push_back(cyan);
    DocLight magenta = light(true, {2.6f, 0.8f, -1.8f}, {1.0f, 0.15f, 0.7f}, 26.0f);
    magenta.bindings[0] = bind(BindSource::Mid, BindTarget::Light, 1.0f);
    doc.lights.push_back(magenta);
    return doc;
}

SceneDoc neonCubes() {
    SceneDoc doc;
    doc.name = "Neon Cubes";
    doc.sky = {{0.02f, 0.01f, 0.06f}, {0.10f, 0.03f, 0.16f}, {0.0f, 0.0f, 0.0f}, 0.9f};
    doc.exposure = 1.1f;
    doc.camera.distance = 0.9f;
    doc.camera.pitchDegrees = 24.0f;
    doc.camera.orbitSpeed = 0.18f;
    doc.camera.pitchSway = 6.0f;

    DocObject outer = makeDefaultObject(PrimitiveKind::Cube);
    outer.name = "Outer cubes";
    outer.scale = {0.55f, 0.55f, 0.55f};
    outer.repeat = {RepeatKind::Ring, 16, 2.6f, 1.0f};
    outer.material.color = {0.05f, 0.05f, 0.08f};
    outer.material.metallic = 0.8f;
    outer.material.roughness = 0.2f;
    outer.material.emissive = {0.0f, 0.9f, 1.0f};
    outer.material.emissiveStrength = 0.1f;
    outer.animations[0] = motion(MotionKind::Bob, 0.4f, 0.3f, 1, 0.0625f);
    outer.animations[1] = motion(MotionKind::Spin, 0.1f, 1.0f, 1, 0.0f);
    outer.bindings[0] = bind(BindSource::Level, BindTarget::Emissive, 0.8f);
    outer.bindings[1] = bind(BindSource::Kick, BindTarget::Scale, 0.4f);
    doc.objects.push_back(outer);

    DocObject inner = makeDefaultObject(PrimitiveKind::Cube);
    inner.name = "Inner cubes";
    inner.scale = {0.4f, 0.4f, 0.4f};
    inner.repeat = {RepeatKind::Ring, 8, 1.3f, 1.0f};
    inner.material.color = {0.05f, 0.05f, 0.08f};
    inner.material.metallic = 0.8f;
    inner.material.roughness = 0.2f;
    inner.material.emissive = {1.0f, 0.1f, 0.75f};
    inner.material.emissiveStrength = 0.12f;
    inner.animations[0] = motion(MotionKind::Pulse, 0.5f, 0.35f, 1, 0.125f);
    inner.animations[1] = motion(MotionKind::Spin, -0.2f, 1.0f, 0, 0.0f);
    inner.bindings[0] = bind(BindSource::Bass, BindTarget::Emissive, 0.9f);
    doc.objects.push_back(inner);

    DocObject heart = makeDefaultObject(PrimitiveKind::Cube);
    heart.name = "Heart";
    heart.scale = {0.7f, 0.7f, 0.7f};
    heart.rotation = {35.0f, 0.0f, 35.0f};
    heart.material.color = {1.0f, 1.0f, 1.0f};
    heart.material.emissive = {1.0f, 1.0f, 1.0f};
    heart.material.emissiveStrength = 0.45f;
    heart.material.roughness = 0.1f;
    heart.animations[0] = motion(MotionKind::Spin, 0.15f, 1.0f, 1);
    heart.bindings[0] = bind(BindSource::Kick, BindTarget::Scale, 0.5f);
    doc.objects.push_back(heart);

    doc.objects.push_back(floorPlane({0.02f, 0.02f, 0.04f}, 0.9f, 0.12f, -1.1f));
    doc.lights.push_back(light(false, {0.3f, 0.9f, 0.4f}, {0.5f, 0.55f, 1.0f}, 1.2f));
    DocLight cyan = light(true, {0.0f, 2.2f, 0.0f}, {0.0f, 0.9f, 1.0f}, 22.0f);
    cyan.bindings[0] = bind(BindSource::Bass, BindTarget::Light, 0.8f);
    doc.lights.push_back(cyan);
    return doc;
}

SceneDoc goldKnot() {
    SceneDoc doc;
    doc.name = "Gold Knot";
    doc.sky = {{0.30f, 0.38f, 0.60f}, {1.0f, 0.82f, 0.60f}, {0.12f, 0.08f, 0.05f}, 1.2f};
    doc.camera.distance = 0.85f;
    doc.camera.pitchDegrees = 14.0f;
    doc.camera.orbitSpeed = 0.15f;

    DocObject knot = makeDefaultObject(PrimitiveKind::TorusKnot);
    knot.name = "Knot";
    knot.scale = {2.6f, 2.6f, 2.6f};
    knot.material.color = {1.0f, 0.77f, 0.34f};
    knot.material.metallic = 1.0f;
    knot.material.roughness = 0.16f;
    knot.animations[0] = motion(MotionKind::Spin, 0.07f, 1.0f, 1);
    knot.animations[1] = motion(MotionKind::Sway, 0.12f, 18.0f, 0);
    knot.bindings[0] = bind(BindSource::Bass, BindTarget::Scale, 0.12f);
    knot.bindings[1] = bind(BindSource::Kick, BindTarget::Twist, 0.35f);
    doc.objects.push_back(knot);

    DocObject pearls = makeDefaultObject(PrimitiveKind::Sphere);
    pearls.name = "Pearls";
    pearls.scale = {0.22f, 0.22f, 0.22f};
    pearls.repeat = {RepeatKind::Ring, 12, 2.0f, 1.0f};
    pearls.material.color = {0.96f, 0.96f, 1.0f};
    pearls.material.metallic = 0.0f;
    pearls.material.roughness = 0.15f;
    pearls.animations[0] = motion(MotionKind::Bob, 0.3f, 0.25f, 1, 0.0833f);
    pearls.bindings[0] = bind(BindSource::High, BindTarget::Emissive, 1.2f);
    doc.objects.push_back(pearls);

    DocObject floor = floorPlane({0.3f, 0.3f, 0.3f}, 0.0f, 0.45f, -1.6f);
    floor.material.followPalette = true;
    doc.objects.push_back(floor);
    doc.lights.push_back(light(false, {0.6f, 0.8f, 0.5f}, {1.0f, 0.93f, 0.82f}, 3.4f));
    doc.lights.push_back(light(false, {-0.7f, 0.3f, -0.6f}, {0.5f, 0.65f, 1.0f}, 1.6f));
    return doc;
}

SceneDoc pulseColumns() {
    SceneDoc doc;
    doc.name = "Pulse Columns";
    doc.sky = {{0.05f, 0.08f, 0.2f}, {0.35f, 0.15f, 0.5f}, {0.02f, 0.02f, 0.04f}, 1.0f};
    doc.camera.distance = 0.72f;
    doc.camera.pitchDegrees = 26.0f;
    doc.camera.orbitSpeed = 0.2f;
    doc.camera.bindings[0] = bind(BindSource::Bass, BindTarget::Zoom, 0.08f);

    DocObject columns = makeDefaultObject(PrimitiveKind::Cylinder);
    columns.name = "Columns";
    columns.scale = {0.32f, 2.2f, 0.32f};
    columns.repeat = {RepeatKind::Ring, 20, 2.5f, 1.0f};
    columns.material.color = {0.1f, 0.1f, 0.14f};
    columns.material.metallic = 0.9f;
    columns.material.roughness = 0.22f;
    columns.material.emissive = {0.3f, 0.5f, 1.0f};
    columns.material.emissiveStrength = 0.4f;
    columns.animations[0] = motion(MotionKind::Pulse, 0.45f, 0.28f, 1, 0.05f);
    columns.bindings[0] = bind(BindSource::Bass, BindTarget::Emissive, 3.0f);
    columns.bindings[1] = bind(BindSource::Kick, BindTarget::Lift, 0.25f);
    doc.objects.push_back(columns);

    DocObject spire = makeDefaultObject(PrimitiveKind::Cone);
    spire.name = "Spire";
    spire.scale = {0.9f, 2.6f, 0.9f};
    spire.material.color = {0.9f, 0.9f, 1.0f};
    spire.material.metallic = 0.6f;
    spire.material.roughness = 0.22f;
    spire.animations[0] = motion(MotionKind::Spin, 0.12f, 1.0f, 1);
    spire.bindings[0] = bind(BindSource::Kick, BindTarget::Scale, 0.2f);
    doc.objects.push_back(spire);

    DocObject halo = makeDefaultObject(PrimitiveKind::Torus);
    halo.name = "Halo";
    halo.position = {0.0f, 1.3f, 0.0f};
    halo.rotation = {90.0f, 0.0f, 0.0f};
    halo.scale = {1.9f, 1.9f, 1.9f};
    halo.material.color = {0.0f, 0.0f, 0.0f};
    halo.material.emissive = {0.9f, 0.3f, 1.0f};
    halo.material.emissiveStrength = 0.7f;
    halo.animations[0] = motion(MotionKind::Spin, 0.2f, 1.0f, 2);
    halo.bindings[0] = bind(BindSource::High, BindTarget::Emissive, 2.0f);
    doc.objects.push_back(halo);

    doc.objects.push_back(floorPlane({0.03f, 0.03f, 0.06f}, 0.9f, 0.15f, -1.15f));
    doc.lights.push_back(light(false, {0.4f, 0.9f, 0.5f}, {0.8f, 0.85f, 1.0f}, 2.0f));
    DocLight violet = light(true, {0.0f, 2.8f, 0.0f}, {0.7f, 0.3f, 1.0f}, 30.0f);
    violet.bindings[0] = bind(BindSource::Kick, BindTarget::Light, 0.9f);
    doc.lights.push_back(violet);
    return doc;
}

Scene build(SceneDoc (*make)()) {
    GltfResult result = buildSceneFromDoc(make(), DocAssets{});
    return std::move(result.scene);
}

}

const std::vector<BuiltinDocInfo>& builtinDocs() {
    static const std::vector<BuiltinDocInfo> docs = {
        {"Chrome Orbs Mesh", &chromeOrbs},
        {"Neon Cubes Mesh", &neonCubes},
        {"Gold Knot Mesh", &goldKnot},
        {"Pulse Columns Mesh", &pulseColumns},
    };
    return docs;
}

Scene makeChromeOrbsScene() {
    return build(&chromeOrbs);
}

Scene makeNeonCubesScene() {
    return build(&neonCubes);
}

Scene makeGoldKnotScene() {
    return build(&goldKnot);
}

Scene makePulseColumnsScene() {
    return build(&pulseColumns);
}

}

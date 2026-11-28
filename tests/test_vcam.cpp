#include "check.hpp"

#include "control/control_system.hpp"
#include "core/preset.hpp"
#include "core/settings.hpp"
#include "core/show_timeline.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/scene_doc.hpp"
#include "scene3d/scene_model.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

bool near(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) < eps;
}

bool nearVec(Vec3 a, Vec3 b, float eps = 0.001f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

bool sameVec(Vec3 a, Vec3 b) {
    return std::memcmp(&a, &b, sizeof(Vec3)) == 0;
}

SceneCamera authoredCamera(float yaw, float pitch, float distance, float fov, float speed) {
    SceneCamera camera;
    camera.authored = true;
    camera.yawDegrees = yaw;
    camera.pitchDegrees = pitch;
    camera.distance = distance;
    camera.fovDegrees = fov;
    camera.orbitSpeed = speed;
    return camera;
}

Scene shotScene(float cut, float glide, CameraGlide shape, std::initializer_list<float> beats) {
    Scene scene;
    scene.cameraCutBeats = cut;
    scene.cameraGlide = glide;
    scene.cameraGlideShape = shape;
    int i = 0;
    for (const float length : beats) {
        SceneCamera camera = authoredCamera(static_cast<float>(i) * 90.0f, 10.0f + static_cast<float>(i), 1.0f + 0.1f * static_cast<float>(i), 46.0f, 0.0f);
        camera.beats = length;
        scene.cameras.push_back(camera);
        ++i;
    }
    scene.camera = scene.cameras.front();
    return scene;
}

int indexOf(const Scene& scene, const SceneCamera* camera) {
    for (size_t i = 0; i < scene.cameras.size(); ++i) {
        if (&scene.cameras[i] == camera) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Vec3 oldEye(const SceneCamera& camera, float time, const SceneAudio& audio, Vec3 center, float radius, float zoom, float& distanceOut) {
    const float fov = camera.authored ? std::clamp(camera.fovDegrees, 15.0f, 110.0f) * 0.0174532925f : 0.8f;
    float distance = radius / std::sin(fov * 0.5f) * 1.04f / zoom;
    float yaw;
    float pitch;
    if (camera.authored) {
        distance *= camera.distance * std::clamp(1.0f - bindingSum(camera.bindings, BindTarget::Zoom, audio), 0.3f, 3.0f);
        yaw = camera.yawDegrees * 0.0174532925f + time * camera.orbitSpeed;
        pitch = (camera.pitchDegrees + camera.pitchSway * std::sin(time * 0.31f)) * 0.0174532925f;
    } else {
        distance *= 1.0f - 0.04f * audio.kick;
        yaw = time * 0.4f;
        pitch = 0.32f + 0.12f * std::sin(time * 0.31f);
    }
    distanceOut = distance;
    return center + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)} * distance;
}

void testEasing() {
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Linear, 0.25)), 0.25f));
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Smooth, 0.5)), 0.5f));
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Smooth, 0.25)), 0.15625f));
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Smooth, -3.0)), 0.0f));
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Smooth, 7.0)), 1.0f));
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Hold, 0.99)), 0.0f));
    CHECK(near(static_cast<float>(easeAlong(KeyShape::Hold, 1.0)), 1.0f));
    const std::vector<Keyframe> keys = {{0.0, 0.0f, KeyShape::Smooth}, {2.0, 10.0f, KeyShape::Linear}, {4.0, 0.0f, KeyShape::Hold}};
    CHECK(near(evaluateKeys(keys, 0.5, false), 10.0f * 0.15625f));
    CHECK(near(evaluateKeys(keys, 1.0, false), 5.0f));
    CHECK(near(evaluateKeys(keys, 3.0, false), 5.0f));
    CHECK(near(evaluateKeys(keys, 3.0, true), 10.0f));
    CHECK(near(evaluateKeys(keys, 9.0, false), 0.0f));
    for (int i = 0; i <= 20; ++i) {
        const double u = static_cast<double>(i) / 20.0;
        const std::vector<Keyframe> two = {{0.0, 2.0f, KeyShape::Smooth}, {1.0, 6.0f, KeyShape::Linear}};
        CHECK(near(evaluateKeys(two, u, false), 2.0f + 4.0f * static_cast<float>(easeAlong(KeyShape::Smooth, u)), 0.0001f));
    }
}

void testShotsStayOld() {
    const Scene scene = shotScene(4.0f, 0.0f, CameraGlide::Smooth, {0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 400; ++i) {
        const double beat = static_cast<double>(i) * 0.137;
        const SceneCamera& old = activeSceneCamera(scene, beat);
        for (const bool glide : {false, true}) {
            const CameraShot shot = cameraShotAt(scene, beat, glide);
            CHECK(shot.from == &old);
            CHECK(shot.to == shot.from);
            CHECK(shot.blend == 0.0f);
        }
    }
    CHECK(cameraShotAt(scene, std::numeric_limits<double>::quiet_NaN(), true).from == &scene.camera);
    CHECK(cameraShotAt(scene, -5.0, true).from == &scene.cameras[0]);
    Scene single;
    single.camera = authoredCamera(0.0f, 10.0f, 1.0f, 46.0f, 0.0f);
    single.cameraCutBeats = 4.0f;
    single.cameraGlide = 1.0f;
    single.cameras.push_back(single.camera);
    CHECK(cameraShotAt(single, 3.9, true).from == &single.camera);
    Scene noCut = shotScene(0.0f, 1.0f, CameraGlide::Smooth, {0.0f, 0.0f});
    CHECK(cameraShotAt(noCut, 7.0, true).from == &noCut.camera);
}

void testShotsTimed() {
    const Scene scene = shotScene(4.0f, 0.0f, CameraGlide::Smooth, {2.0f, 0.0f, 1.0f});
    const struct {
        double beat;
        int index;
    } cases[] = {{0.0, 0}, {1.9, 0}, {2.0, 1}, {5.9, 1}, {6.0, 2}, {6.99, 2}, {7.0, 0}, {8.9, 0}, {9.0, 1}, {20.5, 2}, {21.0, 0}};
    for (const auto& c : cases) {
        const CameraShot shot = cameraShotAt(scene, c.beat, true);
        CHECK(indexOf(scene, shot.from) == c.index);
        CHECK(shot.blend == 0.0f);
    }
    for (const double beat : {0.0, 3.9, 4.0, 7.9, 8.0, 11.9, 12.0}) {
        CHECK(cameraShotAt(scene, beat, false).from == &activeSceneCamera(scene, beat));
    }
}

void testGlide() {
    Scene smooth = shotScene(4.0f, 0.5f, CameraGlide::Smooth, {0.0f, 0.0f, 0.0f});
    CameraShot shot = cameraShotAt(smooth, 1.0, true);
    CHECK(indexOf(smooth, shot.from) == 0 && shot.to == shot.from && shot.blend == 0.0f);
    shot = cameraShotAt(smooth, 2.0, true);
    CHECK(shot.blend == 0.0f);
    shot = cameraShotAt(smooth, 3.0, true);
    CHECK(indexOf(smooth, shot.from) == 0 && indexOf(smooth, shot.to) == 1);
    CHECK(near(shot.blend, 0.5f));
    shot = cameraShotAt(smooth, 2.5, true);
    CHECK(near(shot.blend, static_cast<float>(easeAlong(KeyShape::Smooth, 0.25))));
    shot = cameraShotAt(smooth, 3.999, true);
    CHECK(shot.blend > 0.999f);
    shot = cameraShotAt(smooth, 11.0, true);
    CHECK(indexOf(smooth, shot.from) == 2 && indexOf(smooth, shot.to) == 0);
    CHECK(near(shot.blend, 0.5f));
    shot = cameraShotAt(smooth, 3.0, false);
    CHECK(shot.blend == 0.0f && shot.to == shot.from);

    Scene linear = smooth;
    linear.cameraGlideShape = CameraGlide::Linear;
    shot = cameraShotAt(linear, 2.5, true);
    CHECK(near(shot.blend, 0.25f));
    Scene cut = smooth;
    cut.cameraGlideShape = CameraGlide::Cut;
    shot = cameraShotAt(cut, 3.5, true);
    CHECK(shot.blend == 0.0f && shot.to == shot.from);
    Scene none = smooth;
    none.cameraGlide = 0.0f;
    shot = cameraShotAt(none, 3.5, true);
    CHECK(shot.blend == 0.0f);
    Scene whole = smooth;
    whole.cameraGlide = 1.0f;
    shot = cameraShotAt(whole, 1.0, true);
    CHECK(near(shot.blend, static_cast<float>(easeAlong(KeyShape::Smooth, 0.25))));
    shot = cameraShotAt(whole, 0.0, true);
    CHECK(shot.blend == 0.0f);

    Scene timed = shotScene(4.0f, 0.5f, CameraGlide::Linear, {2.0f, 0.0f, 1.0f});
    shot = cameraShotAt(timed, 1.5, true);
    CHECK(indexOf(timed, shot.from) == 0 && indexOf(timed, shot.to) == 1 && near(shot.blend, 0.5f));
    shot = cameraShotAt(timed, 6.5, true);
    CHECK(indexOf(timed, shot.from) == 2 && shot.to == shot.from && shot.blend == 0.0f);
    shot = cameraShotAt(timed, 6.75, true);
    CHECK(near(shot.blend, 0.5f));
}

void testPoseMatchesOldMath() {
    SceneCamera withZoom = authoredCamera(30.0f, 25.0f, 1.3f, 55.0f, 0.4f);
    withZoom.pitchSway = 6.0f;
    withZoom.bindings.push_back({BindSource::Bass, BindTarget::Zoom, 0.3f});
    SceneCamera builtin;
    const SceneCamera cameras[] = {authoredCamera(0.0f, 18.0f, 1.0f, 46.0f, 0.4f), withZoom, authoredCamera(-200.0f, -40.0f, 0.5f, 110.0f, -1.5f), builtin};
    const Vec3 center{0.3f, -0.2f, 0.7f};
    for (const SceneCamera& camera : cameras) {
        for (int step = 0; step < 60; ++step) {
            const float time = static_cast<float>(step) * 0.731f;
            SceneAudio audio;
            audio.kick = static_cast<float>(step % 5) * 0.2f;
            audio.bass = static_cast<float>(step % 7) * 0.15f;
            for (const float zoom : {1.0f, 0.5f, 2.7f}) {
                for (const bool moves : {true, false}) {
                    float oldDistance = 0.0f;
                    const Vec3 expected = oldEye(camera, time, audio, center, 1.7f, zoom, oldDistance);
                    const CameraView view = cameraView(cameraPoseOf(camera, time, audio, moves), center, 1.7f, zoom);
                    CHECK(sameVec(view.eye, expected));
                    CHECK(view.distance == oldDistance);
                    const float oldFov = camera.authored ? std::clamp(camera.fovDegrees, 15.0f, 110.0f) * 0.0174532925f : 0.8f;
                    CHECK(view.fov == oldFov);
                    CHECK(sameVec(view.target, center));
                    CHECK(sameVec(view.up, Vec3{0.0f, 1.0f, 0.0f}));
                }
            }
        }
    }
}

void testPoseFeatures() {
    SceneCamera camera = authoredCamera(0.0f, 10.0f, 1.0f, 46.0f, 0.0f);
    camera.rollDegrees = 30.0f;
    camera.move = {0.5f, -0.25f, 1.0f};
    SceneAudio quiet;
    CameraPose pose = cameraPoseOf(camera, 0.0f, quiet, true);
    CHECK(near(pose.roll, 30.0f * kSceneRadians));
    CHECK(nearVec(pose.move, {0.5f, -0.25f, 1.0f}));
    pose = cameraPoseOf(camera, 0.0f, quiet, false);
    CHECK(pose.roll == 0.0f && sameVec(pose.move, Vec3{}));

    SceneAudio loud;
    loud.bass = 1.0f;
    loud.kick = 0.5f;
    loud.beat = 1.0f;
    camera.rollDegrees = 0.0f;
    camera.move = {};
    camera.bindings = {{BindSource::Bass, BindTarget::Orbit, 1.0f}, {BindSource::Kick, BindTarget::Tilt, 1.0f}, {BindSource::Beat, BindTarget::Roll, 0.5f}, {BindSource::Bass, BindTarget::Fov, 1.0f}};
    const CameraPose base = cameraPoseOf(camera, 0.0f, quiet, true);
    pose = cameraPoseOf(camera, 0.0f, loud, true);
    CHECK(near(pose.yaw - base.yaw, kSceneOrbitPerAmount));
    CHECK(near(pose.pitch - base.pitch, 0.5f * kSceneTiltPerAmount));
    CHECK(near(pose.roll, 0.5f * kSceneTiltPerAmount));
    CHECK(near(pose.fov - base.fov, kSceneFovPerAmount));
    pose = cameraPoseOf(camera, 0.0f, loud, false);
    CHECK(near(pose.yaw, base.yaw) && near(pose.pitch, base.pitch) && pose.roll == 0.0f && near(pose.fov, base.fov));

    camera.bindings = {{BindSource::Bass, BindTarget::MoveX, 1.0f}, {BindSource::Bass, BindTarget::MoveY, -2.0f}, {BindSource::Bass, BindTarget::MoveZ, 100.0f}};
    pose = cameraPoseOf(camera, 0.0f, loud, true);
    CHECK(near(pose.move.x, 0.5f) && near(pose.move.y, -1.0f) && near(pose.move.z, kSceneMaxMove));

    camera.move = {-3.5f, 3.5f, 0.0f};
    camera.bindings = {{BindSource::Bass, BindTarget::MoveX, -100.0f}, {BindSource::Bass, BindTarget::MoveY, 100.0f}, {BindSource::Bass, BindTarget::MoveZ, -100.0f}};
    pose = cameraPoseOf(camera, 0.0f, loud, true);
    CHECK(pose.move.x == -kSceneMaxMove && pose.move.y == kSceneMaxMove && pose.move.z == -kSceneMaxMove);
    camera.move = {};

    camera.bindings = {{BindSource::Bass, BindTarget::Tilt, 50.0f}, {BindSource::Bass, BindTarget::Fov, 50.0f}};
    pose = cameraPoseOf(camera, 0.0f, loud, true);
    CHECK(pose.pitch <= kSceneMaxPitch + 1e-5f);
    CHECK(near(pose.fov, 110.0f * kSceneRadians));
    camera.bindings = {{BindSource::Bass, BindTarget::Fov, -50.0f}};
    pose = cameraPoseOf(camera, 0.0f, loud, true);
    CHECK(near(pose.fov, 15.0f * kSceneRadians));

    SceneAudio broken;
    broken.bass = std::numeric_limits<float>::quiet_NaN();
    broken.kick = std::numeric_limits<float>::infinity();
    broken.beat = std::numeric_limits<float>::quiet_NaN();
    camera.bindings = {{BindSource::Bass, BindTarget::Orbit, 1.0f}, {BindSource::Kick, BindTarget::Tilt, 1.0f}, {BindSource::Beat, BindTarget::Roll, 1.0f}, {BindSource::Bass, BindTarget::MoveX, 1.0f}, {BindSource::Bass, BindTarget::Fov, 1.0f}};
    pose = cameraPoseOf(camera, 0.0f, broken, true);
    CHECK(std::isfinite(pose.yaw) && std::isfinite(pose.pitch) && std::isfinite(pose.roll) && std::isfinite(pose.fov) && std::isfinite(pose.move.x));
    const CameraView view = cameraView(pose, {}, 1.0f, 1.0f);
    CHECK(std::isfinite(view.eye.x) && std::isfinite(view.eye.y) && std::isfinite(view.eye.z));
}

void testPoseBlend() {
    CameraPose a;
    a.yaw = 179.0f * kSceneRadians;
    a.distance = 1.0f;
    a.move = {1.0f, 0.0f, 0.0f};
    a.roll = 170.0f * kSceneRadians;
    CameraPose b;
    b.yaw = -179.0f * kSceneRadians;
    b.distance = 4.0f;
    b.move = {-1.0f, 2.0f, 0.0f};
    b.roll = -170.0f * kSceneRadians;
    b.fov = a.fov + 0.2f;
    CameraPose mid = blendCameraPose(a, b, 0.5f);
    CHECK(std::fabs(std::cos(mid.yaw) + 1.0f) < 0.001f);
    CHECK(std::fabs(std::cos(mid.roll) + 1.0f) < 0.001f);
    CHECK(near(mid.distance, 2.0f));
    CHECK(nearVec(mid.move, {0.0f, 1.0f, 0.0f}));
    CHECK(near(mid.fov, a.fov + 0.1f));
    CHECK(near(blendCameraPose(a, b, 0.0f).yaw, a.yaw));
    const CameraPose end = blendCameraPose(a, b, 1.0f);
    CHECK(std::fabs(std::sin(end.yaw - b.yaw)) < 0.001f && near(end.distance, 4.0f));
    CHECK(near(blendCameraPose(a, b, -3.0f).distance, 1.0f));
    CHECK(near(blendCameraPose(a, b, 9.0f).distance, 4.0f));
    CHECK(near(blendCameraPose(a, b, std::numeric_limits<float>::quiet_NaN()).distance, 1.0f));
}

void testPoseAt() {
    Scene scene = shotScene(4.0f, 0.5f, CameraGlide::Linear, {0.0f, 0.0f});
    scene.cameras[0].yawDegrees = 0.0f;
    scene.cameras[1].yawDegrees = 80.0f;
    scene.cameras[0].distance = 1.0f;
    scene.cameras[1].distance = 1.0f;
    scene.camera = scene.cameras[0];
    SceneAudio audio;
    CameraPose pose = cameraPoseAt(scene, 1.0, 0.0f, audio, true);
    CHECK(near(pose.yaw, 0.0f));
    pose = cameraPoseAt(scene, 3.0, 0.0f, audio, true);
    CHECK(near(pose.yaw, 40.0f * kSceneRadians));
    pose = cameraPoseAt(scene, 3.0, 0.0f, audio, false);
    CHECK(near(pose.yaw, 0.0f));
    pose = cameraPoseAt(scene, 3.999, 0.0f, audio, true);
    CHECK(near(pose.yaw, 80.0f * kSceneRadians, 0.01f));
    pose = cameraPoseAt(scene, 4.0, 0.0f, audio, true);
    CHECK(near(pose.yaw, 80.0f * kSceneRadians));
    float last = -1.0f;
    for (int i = 0; i < 400; ++i) {
        const double beat = static_cast<double>(i) * 0.01;
        const CameraPose now = cameraPoseAt(scene, beat, 0.0f, audio, true);
        if (beat >= 2.0 && beat < 4.0) {
            CHECK(now.yaw >= last - 1e-6f);
        }
        last = now.yaw;
    }
}

void testView() {
    CameraPose pose;
    pose.yaw = 0.7f;
    pose.pitch = 0.3f;
    pose.distance = 1.2f;
    pose.move = {0.5f, 0.25f, -0.75f};
    const Vec3 center{1.0f, 2.0f, 3.0f};
    CameraView view = cameraView(pose, center, 2.0f, 1.0f);
    CHECK(nearVec(view.target, {2.0f, 2.5f, 1.5f}));
    CHECK(near(length(view.eye - view.target), view.distance, 0.001f));
    CHECK(sameVec(view.up, Vec3{0.0f, 1.0f, 0.0f}));
    pose.roll = 0.0f;
    pose.move = {};
    for (const float roll : {0.4f, 1.5707963f, -2.0f, 3.1f}) {
        pose.roll = roll;
        view = cameraView(pose, center, 2.0f, 1.0f);
        const Vec3 forward = normalize(view.target - view.eye);
        CHECK(near(length(view.up), 1.0f, 0.001f));
        CHECK(std::fabs(dot(view.up, forward)) < 0.001f);
        CHECK(std::fabs(dot(normalize(cross(forward, view.up)), normalize(cross(forward, Vec3{0.0f, 1.0f, 0.0f}))) - std::cos(roll)) < 0.001f);
    }
    pose.roll = 1.5707963f;
    view = cameraView(pose, center, 2.0f, 1.0f);
    CHECK(std::fabs(view.up.y) < 0.001f);
    const CameraView zoomed = cameraView(pose, center, 2.0f, 2.0f);
    CHECK(near(zoomed.distance * 2.0f, view.distance, 0.001f));
}

SceneFileCamera fileCameraFrom(const CameraView& view, float yfov) {
    SceneFileCamera file;
    const Vec3 forward = normalize(view.target - view.eye);
    const Vec3 right = normalize(cross(forward, view.up));
    const Vec3 up = cross(right, forward);
    file.yfov = yfov;
    file.world.m = {right.x, right.y, right.z, 0.0f, up.x, up.y, up.z, 0.0f, -forward.x, -forward.y, -forward.z, 0.0f, view.eye.x, view.eye.y, view.eye.z, 1.0f};
    return file;
}

void testFileCamera() {
    const Vec3 center{0.5f, 0.1f, -0.4f};
    const float radius = 1.6f;
    const struct {
        float yaw;
        float pitch;
        float roll;
        Vec3 move;
        float distance;
        float fov;
    } cases[] = {{0.0f, 0.2f, 0.0f, {}, 1.0f, 46.0f}, {1.2f, -0.4f, 0.5f, {0.3f, -0.2f, 0.1f}, 1.7f, 60.0f}, {-2.5f, 0.9f, -1.0f, {-1.0f, 0.5f, 2.0f}, 0.6f, 30.0f}, {3.0f, 0.0f, 2.8f, {}, 2.2f, 90.0f}};
    for (const auto& c : cases) {
        CameraPose pose;
        pose.yaw = c.yaw;
        pose.pitch = c.pitch;
        pose.roll = c.roll;
        pose.move = c.move;
        pose.distance = c.distance;
        pose.fov = c.fov * kSceneRadians;
        const CameraView view = cameraView(pose, center, radius, 1.0f);
        const SceneFileCamera file = fileCameraFrom(view, pose.fov);
        const SceneCamera got = sceneCameraFromFile(file, center, radius);
        CHECK(got.authored && got.orbitSpeed == 0.0f && got.pitchSway == 0.0f);
        CHECK(near(got.fovDegrees, c.fov, 0.01f));
        const CameraPose back = cameraPoseOf(got, 0.0f, SceneAudio{}, true);
        const CameraView again = cameraView(back, center, radius, 1.0f);
        CHECK(nearVec(again.eye, view.eye, 0.01f));
        CHECK(nearVec(normalize(again.target - again.eye), normalize(view.target - view.eye), 0.001f));
        CHECK(nearVec(again.up, view.up, 0.01f));
        if (c.move.x == 0.0f && c.move.y == 0.0f && c.move.z == 0.0f) {
            CHECK(near(got.distance, c.distance, 0.002f));
            CHECK(near(got.pitchDegrees, c.pitch / kSceneRadians, 0.02f));
            CHECK(nearVec(got.move, c.move, 0.002f));
        }
    }

    CameraPose plain;
    plain.yaw = 0.8f;
    plain.pitch = 0.2f;
    SceneFileCamera scaled = fileCameraFrom(cameraView(plain, center, radius, 1.0f), plain.fov);
    for (const int column : {0, 1, 2}) {
        for (int row = 0; row < 3; ++row) {
            scaled.world.m[static_cast<size_t>(column * 4 + row)] *= 3.0f;
        }
    }
    const SceneCamera fromScaled = sceneCameraFromFile(scaled, center, radius);
    CHECK(near(fromScaled.distance, 1.0f, 0.002f));

    SceneFileCamera away;
    away.yfov = 0.8f;
    away.world.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.0f, 0.0f, 50.0f, 1};
    const SceneCamera behind = sceneCameraFromFile(away, {0.0f, 0.0f, 100.0f}, 1.0f);
    CHECK(std::isfinite(behind.distance) && behind.distance >= 0.2f && behind.distance <= 6.0f);
    CHECK(std::isfinite(behind.move.x) && std::isfinite(behind.move.y) && std::isfinite(behind.move.z));

    SceneFileCamera wild;
    wild.yfov = std::numeric_limits<float>::quiet_NaN();
    wild.world.m = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const SceneCamera tame = sceneCameraFromFile(wild, {}, 0.0f);
    CHECK(std::isfinite(tame.fovDegrees) && std::isfinite(tame.distance) && std::isfinite(tame.yawDegrees) && std::isfinite(tame.pitchDegrees) && std::isfinite(tame.rollDegrees));

    SceneFileCamera straightDown = fileCameraFrom(cameraView(CameraPose{}, center, radius, 1.0f), 0.8f);
    straightDown.world.m = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0.0f, 5.0f, 0.0f, 1};
    const SceneCamera down = sceneCameraFromFile(straightDown, {}, 1.0f);
    CHECK(std::isfinite(down.rollDegrees) && std::isfinite(down.yawDegrees));
    CHECK(down.pitchDegrees > 80.0f);
}

SceneDoc cameraDoc() {
    SceneDoc doc = makeDefaultDoc();
    doc.cutBeats = 8.0f;
    doc.glide = 0.4f;
    doc.glideShape = CameraGlide::Linear;
    doc.camera.move = {0.5f, -1.0f, 2.0f};
    doc.camera.rollDegrees = 12.0f;
    doc.camera.beats = 6.0f;
    DocCamera second;
    second.yawDegrees = 120.0f;
    second.bindings[0] = {true, BindSource::Beat, BindTarget::Orbit, 0.25f};
    second.bindings[1] = {true, BindSource::Band3, BindTarget::MoveZ, 0.5f};
    docAddCamera(doc, second);
    docCameraAt(doc, 1).bindings = second.bindings;
    docCameraAt(doc, 1).yawDegrees = 120.0f;
    return doc;
}

void testDocRoundTrip() {
    const SceneDoc doc = cameraDoc();
    CHECK(docRequiredVersion(doc) == kDocCameraVersion);
    CHECK(kDocCameraVersion == 3);
    CHECK(kDocVersion == 5);
    const std::string text = docToJson(doc);
    const nlohmann::json parsed = nlohmann::json::parse(text);
    CHECK(parsed["version"] == 3);
    CHECK(parsed["glide"] == 0.4f && parsed["glideShape"] == "linear");
    CHECK(parsed["camera"]["roll"] == 12.0f && parsed["camera"]["beats"] == 6.0f);
    CHECK(parsed["camera"]["move"].size() == 3);
    SceneDoc back;
    std::string error;
    CHECK(docFromJson(text, back, error));
    CHECK(back.notes.empty());
    CHECK(near(back.glide, 0.4f) && back.glideShape == CameraGlide::Linear && near(back.cutBeats, 8.0f));
    CHECK(nearVec(back.camera.move, {0.5f, -1.0f, 2.0f}));
    CHECK(near(back.camera.rollDegrees, 12.0f) && near(back.camera.beats, 6.0f));
    CHECK(docCameraCount(back) == 2);
    const DocCamera& second = docCameraAt(back, 1);
    CHECK(second.bindings[0].enabled && second.bindings[0].source == BindSource::Beat && second.bindings[0].target == BindTarget::Orbit && near(second.bindings[0].amount, 0.25f));
    CHECK(second.bindings[1].enabled && second.bindings[1].source == BindSource::Band3 && second.bindings[1].target == BindTarget::MoveZ);
    CHECK(docToJson(back) == text);

    for (const BindTarget target : {BindTarget::Orbit, BindTarget::Tilt, BindTarget::Roll, BindTarget::Fov, BindTarget::MoveX, BindTarget::MoveY, BindTarget::MoveZ}) {
        SceneDoc one = makeDefaultDoc();
        one.camera.bindings[0] = {true, BindSource::Bass, target, 0.5f};
        CHECK(docRequiredVersion(one) == kDocCameraVersion);
        SceneDoc read;
        CHECK(docFromJson(docToJson(one), read, error));
        CHECK(read.camera.bindings[0].enabled && read.camera.bindings[0].target == target);
        CHECK(read.notes.empty());
    }
}

void testOldFilesStayOld() {
    const SceneDoc plain = makeDefaultDoc();
    const std::string text = docToJson(plain);
    const nlohmann::json parsed = nlohmann::json::parse(text);
    CHECK(parsed["version"] == 1);
    CHECK(!parsed.contains("glide") && !parsed.contains("glideShape"));
    CHECK(!parsed["camera"].contains("move") && !parsed["camera"].contains("roll") && !parsed["camera"].contains("beats"));

    SceneDoc cuts = makeDefaultDoc();
    cuts.cutBeats = 4.0f;
    docAddCamera(cuts, DocCamera{});
    cuts.camera.bindings[0] = {true, BindSource::Kick, BindTarget::Zoom, 0.1f};
    CHECK(docRequiredVersion(cuts) == kDocBaseVersion);
    CHECK(nlohmann::json::parse(docToJson(cuts))["version"] == 1);

    SceneDoc material = makeDefaultDoc();
    material.objects[0].bindings[0] = {true, BindSource::Beat, BindTarget::Hue, 0.5f};
    CHECK(docRequiredVersion(material) == kDocBindingsVersion);

    SceneDoc glideOnly = makeDefaultDoc();
    glideOnly.glide = 0.5f;
    CHECK(docRequiredVersion(glideOnly) == kDocCameraVersion);
    SceneDoc glideZero = makeDefaultDoc();
    glideZero.glideShape = CameraGlide::Cut;
    CHECK(docRequiredVersion(glideZero) == kDocBaseVersion);
    CHECK(!nlohmann::json::parse(docToJson(glideZero)).contains("glideShape"));

    std::string error;
    SceneDoc read;
    std::string future = text;
    nlohmann::json bumped = nlohmann::json::parse(text);
    bumped["version"] = 6;
    CHECK(!docFromJson(bumped.dump(), read, error));
    CHECK(error.find("newer NyrexVJ") != std::string::npos);
    bumped["version"] = 5;
    CHECK(docFromJson(bumped.dump(), read, error));
    bumped["version"] = 3;
    CHECK(docFromJson(bumped.dump(), read, error));
    bumped["version"] = 2;
    CHECK(docFromJson(bumped.dump(), read, error));
}

void testUnknownAndCoercion() {
    SceneDoc doc = cameraDoc();
    nlohmann::json json = nlohmann::json::parse(docToJson(doc));
    json["glideShape"] = "wobble";
    json["camera"]["bindings"] = nlohmann::json::array({{{"source", "bass"}, {"target", "hue"}, {"amount", 1.0}}, {{"source", "bass"}, {"target", "orbit"}, {"amount", 0.5}}, {{"source", "bass"}, {"target", "teleport"}, {"amount", 1.0}}});
    json["objects"][0]["bindings"] = nlohmann::json::array({{{"source", "bass"}, {"target", "orbit"}, {"amount", 0.5}}, {{"source", "bass"}, {"target", "zoom"}, {"amount", 0.5}}});
    json["lights"][0]["bindings"] = nlohmann::json::array({{{"source", "bass"}, {"target", "moveX"}, {"amount", 0.5}}});
    SceneDoc read;
    std::string error;
    CHECK(docFromJson(json.dump(), read, error));
    CHECK(read.glideShape == CameraGlide::Smooth);
    CHECK(!read.notes.empty());
    bool shape = false;
    bool teleport = false;
    for (const std::string& note : read.notes) {
        shape = shape || note.find("wobble") != std::string::npos;
        teleport = teleport || note.find("teleport") != std::string::npos;
    }
    CHECK(shape && teleport);
    int misfits = 0;
    for (const std::string& note : read.notes) {
        misfits += note.find("does not fit here") != std::string::npos ? 1 : 0;
    }
    CHECK(misfits >= 3);
    CHECK(read.camera.bindings[0].target == BindTarget::Zoom || read.camera.bindings[0].target == BindTarget::Orbit);
    bool orbit = false;
    for (const DocBinding& b : read.camera.bindings) {
        CHECK(!b.enabled || bindTargetIsCamera(b.target));
        orbit = orbit || (b.enabled && b.target == BindTarget::Orbit);
    }
    CHECK(orbit);
    for (const DocBinding& b : read.objects[0].bindings) {
        CHECK(!b.enabled || (!bindTargetIsCamera(b.target) && b.target != BindTarget::Light));
    }
    for (const DocBinding& b : read.lights[0].bindings) {
        CHECK(!b.enabled || b.target == BindTarget::Light);
    }

    SceneDoc wild = makeDefaultDoc();
    wild.camera.move = {std::numeric_limits<float>::quiet_NaN(), 99.0f, -99.0f};
    wild.camera.rollDegrees = 9000.0f;
    wild.camera.beats = -3.0f;
    wild.glide = 7.0f;
    wild.glideShape = static_cast<CameraGlide>(42);
    wild.cutBeats = 4.0f;
    docAddCamera(wild, DocCamera{});
    sanitizeDoc(wild);
    CHECK(wild.camera.move.x == 0.0f && wild.camera.move.y == kSceneMaxMove && wild.camera.move.z == -kSceneMaxMove);
    CHECK(wild.camera.rollDegrees == 180.0f && wild.camera.beats == 0.0f);
    CHECK(wild.glide == 1.0f && wild.glideShape == CameraGlide::Smooth);
    SceneDoc objectOrbit = makeDefaultDoc();
    objectOrbit.objects[0].bindings[0] = {true, BindSource::Bass, BindTarget::MoveX, 1.0f};
    sanitizeDoc(objectOrbit);
    CHECK(objectOrbit.objects[0].bindings[0].target == BindTarget::Scale);
    SceneDoc cameraHue = makeDefaultDoc();
    cameraHue.camera.bindings[0] = {true, BindSource::Bass, BindTarget::Hue, 1.0f};
    sanitizeDoc(cameraHue);
    CHECK(cameraHue.camera.bindings[0].target == BindTarget::Zoom);
    SceneDoc lightOrbit = makeDefaultDoc();
    lightOrbit.lights[0].bindings[0] = {true, BindSource::Bass, BindTarget::Orbit, 1.0f};
    sanitizeDoc(lightOrbit);
    CHECK(lightOrbit.lights[0].bindings[0].target == BindTarget::Light);
    CHECK(!bindTargetIsMaterial(BindTarget::Orbit) && !bindTargetIsMaterial(BindTarget::Zoom) && bindTargetIsMaterial(BindTarget::Ripple) && bindTargetIsMaterial(BindTarget::Hue));
}

void testBuild() {
    const SceneDoc doc = cameraDoc();
    GltfResult built = buildSceneFromDoc(doc, DocAssets{});
    CHECK(built.ok);
    CHECK(near(built.scene.cameraCutBeats, 8.0f) && near(built.scene.cameraGlide, 0.4f) && built.scene.cameraGlideShape == CameraGlide::Linear);
    CHECK(built.scene.cameras.size() == 2);
    CHECK(nearVec(built.scene.cameras[0].move, {0.5f, -1.0f, 2.0f}));
    CHECK(near(built.scene.cameras[0].rollDegrees, 12.0f) && near(built.scene.cameras[0].beats, 6.0f));
    CHECK(built.scene.cameras[1].bindings.size() == 2);
    CHECK(built.scene.cameras[1].bindings[0].target == BindTarget::Orbit);

    SceneDoc oneCamera = doc;
    oneCamera.cutBeats = 0.0f;
    built = buildSceneFromDoc(oneCamera, DocAssets{});
    CHECK(built.ok && built.scene.cameras.empty() && built.scene.cameraCutBeats == 0.0f);
    CHECK(nearVec(built.scene.camera.move, {0.5f, -1.0f, 2.0f}));
    std::string error;
    CHECK(validateScene(built.scene, error));
    built.scene.camera.rollDegrees = std::numeric_limits<float>::quiet_NaN();
    CHECK(!validateScene(built.scene, error));
    built.scene.camera.rollDegrees = 0.0f;
    built.scene.camera.beats = -1.0f;
    CHECK(!validateScene(built.scene, error));
}

void testFileCamerasMerge() {
    auto model = std::make_shared<GltfResult>();
    model->ok = true;
    model->scene.materials.push_back(SceneMaterial{});
    SceneMesh mesh = primitiveMesh(PrimitiveKind::Cube);
    mesh.material = 0;
    model->scene.meshes.push_back(mesh);
    SceneNode node;
    node.meshes = {0};
    model->scene.nodes.push_back(node);
    model->scene.roots = {0};
    SceneFileCamera named;
    named.name = "Front";
    named.world = translation({0.0f, 1.0f, 5.0f});
    SceneFileCamera unnamed;
    unnamed.world = translation({2.0f, 0.0f, 0.0f});
    SceneFileCamera ortho;
    ortho.name = "Plan";
    ortho.orthographic = true;
    model->scene.fileCameras = {named, unnamed, ortho};

    SceneDoc doc;
    doc.name = "Stage";
    DocObject object = makeDefaultObject(PrimitiveKind::Model);
    object.model = "stage.glb";
    object.name = "Stage model";
    object.position = {10.0f, 0.0f, 0.0f};
    object.scale = {2.0f, 2.0f, 2.0f};
    doc.objects.push_back(object);
    DocAssets assets;
    assets.loadModel = [&](const std::string& name, const GltfLimits&) -> std::shared_ptr<const GltfResult> {
        return name == "stage.glb" ? model : nullptr;
    };
    const GltfResult built = buildSceneFromDoc(doc, assets);
    CHECK(built.ok);
    CHECK(built.scene.fileCameras.size() == 3);
    if (built.scene.fileCameras.size() == 3) {
        const SceneFileCamera& first = built.scene.fileCameras[0];
        CHECK(first.name == "Stage model: Front");
        CHECK(nearVec({first.world.m[12], first.world.m[13], first.world.m[14]}, {10.0f, 2.0f, 10.0f}));
        CHECK(built.scene.fileCameras[1].name == "Stage model");
        CHECK(nearVec({built.scene.fileCameras[1].world.m[12], built.scene.fileCameras[1].world.m[13], built.scene.fileCameras[1].world.m[14]}, {14.0f, 0.0f, 0.0f}));
        CHECK(built.scene.fileCameras[2].orthographic);
    }
    CHECK(built.scene.camera.authored);

    SceneDoc many = doc;
    for (int i = 0; i < 12; ++i) {
        many.objects.push_back(object);
    }
    const GltfResult capped = buildSceneFromDoc(many, assets);
    CHECK(capped.ok);
    CHECK(capped.scene.fileCameras.size() == kSceneMaxFileCameras);
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

void testLayerControls() {
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    const struct {
        const char* id;
        float low;
        float high;
    } controls[] = {{"layer/1/camera-orbit", -180.0f, 180.0f}, {"layer/2/camera-tilt", -60.0f, 60.0f}, {"layer/4/camera-fov", -30.0f, 30.0f}};
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
    CHECK(near(comp.layers[0].cameraOrbit, 0.0f) && near(comp.layers[1].cameraTilt, 0.0f) && near(comp.layers[3].cameraFov, 0.0f));
    router.set(controlTargetIndex("layer/1/camera-orbit"), 1.0f, false);
    router.set(controlTargetIndex("layer/1/camera-tilt"), 0.0f, false);
    router.set(controlTargetIndex("layer/1/camera-fov"), 1.0f, false);
    CHECK(near(comp.layers[0].cameraOrbit, 180.0f) && near(comp.layers[0].cameraTilt, -60.0f) && near(comp.layers[0].cameraFov, 30.0f));
    router.set(controlTargetIndex("layer/1/camera-orbit"), 9.0f, false);
    CHECK(comp.layers[0].cameraOrbit <= 180.0f + 0.01f);
    CHECK(!router.set(controlTargetIndex("layer/1/camera-orbit"), std::numeric_limits<float>::quiet_NaN(), false));
    CHECK(controlTargetIndex("layer/9/camera-orbit") < 0);
    const int last = static_cast<int>(controlTargets().size()) - 1;
    CHECK(controlTargets()[static_cast<size_t>(last)].id == "master/feedback-reset");
    CHECK(controlTargets()[static_cast<size_t>(last - 1)].id == "master/feedback-freeze");

    Composition fresh;
    const std::string defaultText = compositionToJson(fresh);
    CHECK(defaultText.find("cameraOrbit") == std::string::npos && defaultText.find("cameraTilt") == std::string::npos && defaultText.find("cameraFov") == std::string::npos);
    comp.layers[2].cameraOrbit = -75.0f;
    comp.layers[2].cameraTilt = 20.0f;
    comp.layers[2].cameraFov = -12.0f;
    const std::string text = compositionToJson(comp);
    CHECK(text.find("cameraOrbit") != std::string::npos);
    Composition loaded;
    std::string error;
    CHECK(compositionFromJson(text, loaded, error));
    CHECK(near(loaded.layers[2].cameraOrbit, -75.0f) && near(loaded.layers[2].cameraTilt, 20.0f) && near(loaded.layers[2].cameraFov, -12.0f));
    CHECK(near(loaded.layers[1].cameraOrbit, 0.0f));
    CHECK(loaded.layers[0] == comp.layers[0]);
    CHECK(!(loaded.layers[2] == fresh.layers[2]));

    nlohmann::json wild = nlohmann::json::parse(text);
    for (nlohmann::json& layer : wild["layers"]) {
        layer["cameraOrbit"] = 9000.0f;
        layer["cameraTilt"] = "up";
        layer["cameraFov"] = -9000.0f;
    }
    Composition clamped;
    CHECK(compositionFromJson(wild.dump(), clamped, error));
    CHECK(clamped.layers[0].cameraOrbit == 180.0f && clamped.layers[0].cameraTilt == 0.0f && clamped.layers[0].cameraFov == -30.0f);
}

void testPackRoundTrip() {
    ScenePack pack;
    pack.doc = cameraDoc();
    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(writeScenePack(pack, bytes, error));
    ScenePack back;
    CHECK(readScenePack(bytes.data(), bytes.size(), back, error));
    CHECK(docToJson(back.doc) == docToJson(pack.doc));
    CHECK(near(back.doc.glide, 0.4f) && nearVec(back.doc.camera.move, {0.5f, -1.0f, 2.0f}));
}

void testSetting() {
    namespace fs = std::filesystem;
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_camera_settings";
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
    CHECK(defaults.sceneCameraMoves);
    CHECK(saveSettings(defaults));
    CHECK(!nlohmann::json::parse(readText()).contains("sceneCameraMoves"));
    Settings off;
    off.sceneCameraMoves = false;
    CHECK(saveSettings(off));
    const nlohmann::json written = nlohmann::json::parse(readText());
    CHECK(written.contains("sceneCameraMoves") && written["sceneCameraMoves"] == false);
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(!loaded.sceneCameraMoves);
    CHECK(loaded.sceneMaterialBindings);
    CHECK(loadWith("{}").sceneCameraMoves);
    CHECK(!loadWith("{\"sceneCameraMoves\": false}").sceneCameraMoves);
    CHECK(loadWith("{\"sceneCameraMoves\": \"no\"}").sceneCameraMoves);
    CHECK(loadWith("{\"sceneCameraMoves\": null}").sceneCameraMoves);
    CHECK(loadWith("{\"sceneMaterialBindings\": false}").sceneCameraMoves);
    CHECK(!loadWith("{\"sceneMaterialBindings\": false}").sceneMaterialBindings);
    fs::remove_all(scratch, ec);
}

void testBuiltinsUnchanged() {
    const SceneDoc builtIn = makeDefaultDoc();
    GltfResult built = buildSceneFromDoc(builtIn, DocAssets{});
    CHECK(built.ok);
    CHECK(built.scene.cameraGlide == 0.0f && built.scene.cameras.empty());
    CHECK(sameVec(built.scene.camera.move, Vec3{}) && built.scene.camera.rollDegrees == 0.0f && built.scene.camera.beats == 0.0f);
    const SceneCamera plain;
    CHECK(plain.beats == 0.0f && plain.rollDegrees == 0.0f && sameVec(plain.move, Vec3{}) && !plain.authored);
    CHECK(static_cast<int>(BindTarget::Ripple) == 11 && static_cast<int>(BindTarget::Orbit) == 12 && kBindTargetCount == 19);
    CHECK(kBindOldTargetCount == 6);
}

}

int main() {
    testEasing();
    testShotsStayOld();
    testShotsTimed();
    testGlide();
    testPoseMatchesOldMath();
    testPoseFeatures();
    testPoseBlend();
    testPoseAt();
    testView();
    testFileCamera();
    testDocRoundTrip();
    testOldFilesStayOld();
    testUnknownAndCoercion();
    testBuild();
    testFileCamerasMerge();
    testLayerControls();
    testPackRoundTrip();
    testSetting();
    testBuiltinsUnchanged();
    return testing::finish();
}

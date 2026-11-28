#include "app/scene_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "render/scene_player.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/geometry_graph.hpp"
#include "scene3d/gltf.hpp"
#include "scene3d/procedural.hpp"
#include "scene3d/scene_doc.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int kSize = 256;

struct Shot {
    std::vector<uint8_t> pixels;
    int minX = kSize;
    int maxX = -1;
    int minY = kSize;
    int maxY = -1;
    int lit = 0;
    bool ok = false;

    int width() const { return maxX >= minX ? maxX - minX + 1 : 0; }
    int height() const { return maxY >= minY ? maxY - minY + 1 : 0; }
    double centreX() const { return (minX + maxX) * 0.5; }
    const uint8_t* at(int x, int y) const { return &pixels[(static_cast<size_t>(y) * kSize + static_cast<size_t>(x)) * 4]; }
};

struct Checker {
    json items = json::array();
    bool passed = true;

    void expect(const std::string& name, bool condition, const std::string& detail = std::string()) {
        json item;
        item["name"] = name;
        item["passed"] = condition;
        if (!detail.empty()) {
            item["detail"] = detail;
        }
        items.push_back(std::move(item));
        passed = passed && condition;
        logLine("Scene self test " + name + ": " + (condition ? "passed" : "FAILED") + (detail.empty() ? std::string() : " (" + detail + ")"));
    }
};

std::string pixelHash(const std::vector<uint8_t>& pixels) {
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t value : pixels) {
        hash = (hash ^ value) * 1099511628211ull;
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

std::string number(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", value);
    return text;
}

Shot captureFrame(Engine& engine, ScenePlayer& player, SceneFrame frame, RenderTarget& target, bool sceneCamera = false) {
    Shot shot;
    if (!player.wait()) {
        logLine("Scene self test: '" + player.label() + "' failed to load: " + player.error());
        return shot;
    }
    if (!sceneCamera) {
        frame.editor = true;
        frame.editorCamera = true;
        frame.editYaw = 0.0f;
        frame.editPitch = 0.0f;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    engine.sceneRenderer().draw(player, frame, target);
    shot.pixels.resize(static_cast<size_t>(kSize) * kSize * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, shot.pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shot.ok = glGetError() == GL_NO_ERROR;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const uint8_t* p = shot.at(x, y);
            if (p[0] > 8 || p[1] > 8 || p[2] > 8) {
                ++shot.lit;
                shot.minX = std::min(shot.minX, x);
                shot.maxX = std::max(shot.maxX, x);
                shot.minY = std::min(shot.minY, y);
                shot.maxY = std::max(shot.maxY, y);
            }
        }
    }
    return shot;
}

Shot capture(Engine& engine, ScenePlayer& player, float time, RenderTarget& target) {
    SceneFrame frame;
    frame.time = time;
    frame.editDistance = 1.0f;
    return captureFrame(engine, player, frame, target);
}

struct Tint {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

Tint meanTint(const Shot& shot) {
    Tint sum;
    int count = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const uint8_t* p = shot.at(x, y);
            if (p[0] > 8 || p[1] > 8 || p[2] > 8) {
                sum.r += p[0];
                sum.g += p[1];
                sum.b += p[2];
                ++count;
            }
        }
    }
    if (count > 0) {
        sum.r /= count;
        sum.g /= count;
        sum.b /= count;
    }
    return sum;
}

DocAssets bindingAssets() {
    DocAssets assets;
    assets.loadModel = [](const std::string&, const GltfLimits&) -> std::shared_ptr<const GltfResult> { return nullptr; };
    assets.loadImage = [](const std::string& name, std::string& error) -> std::shared_ptr<const SceneImage> {
        if (name != "ramp.png") {
            error = "not here";
            return nullptr;
        }
        auto image = std::make_shared<SceneImage>();
        image->width = 64;
        image->height = 64;
        image->rgba.resize(64u * 64u * 4u);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                uint8_t* p = &image->rgba[(static_cast<size_t>(y) * 64u + static_cast<size_t>(x)) * 4u];
                const int ramp = x * 255 / 63;
                p[0] = static_cast<uint8_t>(255 - ramp);
                p[1] = static_cast<uint8_t>(40 + (x % 16) * 8);
                p[2] = static_cast<uint8_t>(ramp);
                p[3] = 255;
            }
        }
        return image;
    };
    return assets;
}

std::unique_ptr<ScenePlayer> docScene(const SceneDoc& doc, const char* name) {
    const DocAssets assets = bindingAssets();
    return std::make_unique<ScenePlayer>(name, [doc, assets](const GltfLimits& limits) { return buildSceneFromDoc(doc, assets, limits); });
}

SceneDoc sphereDoc(std::initializer_list<DocBinding> list, bool textured) {
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    DocObject sphere = makeDefaultObject(PrimitiveKind::Sphere);
    sphere.material.color = {0.9f, 0.12f, 0.1f};
    sphere.material.metallic = 0.0f;
    sphere.material.roughness = 0.35f;
    sphere.material.emissiveStrength = 0.0f;
    if (textured) {
        sphere.material.color = {1.0f, 1.0f, 1.0f};
        sphere.material.texture = "ramp.png";
    }
    size_t i = 0;
    for (const DocBinding& binding : list) {
        sphere.bindings[i++] = binding;
    }
    doc.objects.push_back(sphere);
    return doc;
}

SceneFrame musicFrame(float kick, double beat, bool materials) {
    AudioFeatures features;
    features.kick = kick;
    features.bass = kick;
    SceneFrame frame;
    frame.time = 1.1f;
    frame.editDistance = 1.9f;
    frame.setAudio(features, beat);
    frame.materialBindings = materials;
    return frame;
}

std::unique_ptr<ScenePlayer> fileScene(const fs::path& path) {
    return std::make_unique<ScenePlayer>(path.filename().string(), [path](const GltfLimits& limits) { return loadGltfFile(path, limits); });
}

struct Centre {
    double x = 0.0;
    double y = 0.0;
};

Centre centroid(const Shot& shot) {
    Centre sum;
    int count = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const uint8_t* p = shot.at(x, y);
            if (p[0] > 8 || p[1] > 8 || p[2] > 8) {
                sum.x += x;
                sum.y += y;
                ++count;
            }
        }
    }
    if (count > 0) {
        sum.x /= count;
        sum.y /= count;
    }
    return sum;
}

SceneDoc lopsidedDoc() {
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    DocObject bar = makeDefaultObject(PrimitiveKind::Cube);
    bar.scale = {3.0f, 0.5f, 0.5f};
    bar.material.color = {0.15f, 0.8f, 0.25f};
    bar.material.metallic = 0.0f;
    bar.material.roughness = 0.5f;
    bar.material.emissiveStrength = 0.0f;
    bar.animations[0].enabled = false;
    DocObject ball = makeDefaultObject(PrimitiveKind::Sphere);
    ball.position = {1.8f, 1.1f, 0.0f};
    ball.scale = {0.7f, 0.7f, 0.7f};
    ball.material.color = {0.9f, 0.15f, 0.1f};
    ball.material.metallic = 0.0f;
    ball.material.roughness = 0.4f;
    ball.material.emissiveStrength = 0.0f;
    doc.objects.push_back(bar);
    doc.objects.push_back(ball);
    doc.camera.yawDegrees = 0.0f;
    doc.camera.pitchDegrees = 0.0f;
    doc.camera.orbitSpeed = 0.0f;
    doc.camera.pitchSway = 0.0f;
    doc.camera.distance = 1.0f;
    return doc;
}

SceneFrame stillFrame(double beat, bool moves = true) {
    SceneFrame frame;
    frame.time = 1.1f;
    frame.beat = beat;
    frame.cameraMoves = moves;
    return frame;
}

SceneFrame beatFrame(double beat) {
    SceneFrame frame;
    frame.time = 1.1f;
    frame.setAudio(AudioFeatures{}, beat);
    return frame;
}

SceneFrame loudFrame(double beat, bool moves = true) {
    AudioFeatures features;
    features.kick = 1.0f;
    features.bass = 1.0f;
    SceneFrame frame;
    frame.time = 1.1f;
    frame.setAudio(features, beat);
    frame.cameraMoves = moves;
    return frame;
}

double meanDifference(const Shot& a, const Shot& b) {
    if (a.pixels.size() != b.pixels.size() || a.pixels.empty()) {
        return 255.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        sum += std::abs(static_cast<int>(a.pixels[i]) - static_cast<int>(b.pixels[i]));
    }
    return sum / static_cast<double>(a.pixels.size());
}

}

SceneDoc ballDoc(std::array<float, 3> color, float roughness = 0.4f) {
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    DocObject sphere = makeDefaultObject(PrimitiveKind::Sphere);
    sphere.material.color = color;
    sphere.material.metallic = 0.0f;
    sphere.material.roughness = roughness;
    sphere.material.emissiveStrength = 0.0f;
    doc.objects.push_back(sphere);
    return doc;
}

DocAssets modelFileAssets(const fs::path& models) {
    DocAssets assets = bindingAssets();
    assets.loadModel = [models](const std::string& name, const GltfLimits& limits) -> std::shared_ptr<const GltfResult> {
        if (name == "cube.glb" || name == "cube_copy.glb") {
            return std::make_shared<GltfResult>(loadGltfFile(models / "morph_cube.glb", limits));
        }
        if (name == "arm.glb") {
            return std::make_shared<GltfResult>(loadGltfFile(models / "skin_arm.glb", limits));
        }
        return nullptr;
    };
    return assets;
}

std::unique_ptr<ScenePlayer> docSceneWith(const SceneDoc& doc, const char* name, const DocAssets& assets) {
    return std::make_unique<ScenePlayer>(name, [doc, assets](const GltfLimits& limits) { return buildSceneFromDoc(doc, assets, limits); });
}

SceneFrame worldFrame(int world, bool owner, WorldPlacement placement = {}) {
    SceneFrame frame;
    frame.time = 0.0f;
    frame.beat = 0.0;
    frame.world = world;
    frame.worldOwner = owner;
    frame.placement = placement;
    return frame;
}

Shot layerOver(const Shot& below, const Shot& above) {
    Shot out = below;
    for (size_t i = 0; i + 3 < out.pixels.size(); i += 4) {
        if (above.pixels[i + 3] > 0) {
            for (size_t c = 0; c < 4; ++c) {
                out.pixels[i + c] = above.pixels[i + c];
            }
        }
    }
    return out;
}

double differingShare(const Shot& a, const Shot& b) {
    if (a.pixels.size() != b.pixels.size() || a.pixels.empty()) {
        return 1.0;
    }
    size_t different = 0;
    for (size_t i = 0; i + 3 < a.pixels.size(); i += 4) {
        int largest = 0;
        for (size_t c = 0; c < 3; ++c) {
            largest = std::max(largest, std::abs(static_cast<int>(a.pixels[i + c]) - static_cast<int>(b.pixels[i + c])));
        }
        if (largest > 6) {
            ++different;
        }
    }
    return static_cast<double>(different) / static_cast<double>(a.pixels.size() / 4);
}

int dominantCount(const Shot& shot, int channel) {
    int count = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const uint8_t* p = shot.at(x, y);
            const int own = p[channel];
            const int a = p[(channel + 1) % 3];
            const int b = p[(channel + 2) % 3];
            if (own > 60 && own > a * 2 && own > b * 2) {
                ++count;
            }
        }
    }
    return count;
}

SceneDoc jointDoc(std::array<float, 3> ownerColor, std::array<float, 3> memberColor, Vec3 memberAt) {
    SceneDoc doc = ballDoc(ownerColor);
    DocObject other = doc.objects[0];
    other.material.color = memberColor;
    other.position = memberAt;
    other.framing = false;
    other.name = "Second";
    doc.objects.push_back(other);
    sanitizeDoc(doc);
    return doc;
}

void runWorldChecks(Engine& engine, const fs::path& models, RenderTarget& target, Checker& check, json& report) {
    SceneRenderer& renderer = engine.sceneRenderer();
    const std::array<float, 3> red{0.9f, 0.1f, 0.08f};
    const std::array<float, 3> green{0.1f, 0.85f, 0.15f};
    const std::array<float, 3> blue{0.1f, 0.2f, 0.95f};

    {
        auto owner = docScene(ballDoc(red), "world owner");
        const Shot isolated = captureFrame(engine, *owner, worldFrame(0, false), target, true);
        renderer.resetWorlds();
        const Shot inWorld = captureFrame(engine, *owner, worldFrame(1, true), target, true);
        check.expect("world: an owner alone looks exactly like an isolated scene", isolated.ok && inWorld.ok && isolated.lit > 500 && renderer.lastDrawShared() && pixelHash(isolated.pixels) == pixelHash(inWorld.pixels), "lit " + std::to_string(isolated.lit));
        renderer.resetWorlds();
        WorldPlacement turn;
        turn.yawDegrees = 40.0f;
        const Shot turned = captureFrame(engine, *owner, worldFrame(1, true, turn), target, true);
        check.expect("world: the owner can be moved inside its own world", turned.ok && turned.lit > 100 && renderer.lastDrawShared());
    }

    for (const float depth : {0.8f, -0.8f}) {
        auto owner = docScene(ballDoc(red), "world red");
        auto member = docScene(ballDoc(green), "world green");
        owner->wait();
        member->wait();
        const float radius = owner->fitRadius();
        const Vec3 centre = owner->fitCenter();
        WorldPlacement place;
        place.x = 0.6f;
        place.z = depth;
        const Vec3 memberAt = centre + Vec3{place.x * radius, 0.0f, place.z * radius};
        auto joint = docScene(jointDoc(red, green, memberAt), "world joint");

        renderer.resetWorlds();
        const Shot below = captureFrame(engine, *owner, worldFrame(1, true), target, true);
        const Shot above = captureFrame(engine, *member, worldFrame(1, false, place), target, true);
        const bool sharedMember = renderer.lastDrawShared();
        const Shot layered = layerOver(below, above);
        const Shot reference = captureFrame(engine, *joint, worldFrame(0, false), target, true);
        const std::string label = depth > 0.0f ? "member in front" : "member behind";
        const double share = differingShare(layered, reference);
        check.expect("world: " + label + " matches one scene drawn together", below.ok && above.ok && sharedMember && layered.lit > 500 && share < 0.004 && meanDifference(layered, reference) < 0.25, "differing share " + number(share * 100.0) + " %, mean " + number(meanDifference(layered, reference)));
        const int greens = dominantCount(layered, 1);
        const int reds = dominantCount(layered, 0);
        report["world"]["occlusion"][depth > 0.0f ? "front" : "behind"] = {{"green", greens}, {"red", reds}, {"differingShare", share}};
        if (depth > 0.0f) {
            report["world"]["frontGreens"] = greens;
        } else {
            const int frontGreens = report["world"]["frontGreens"].get<int>();
            check.expect("world: occlusion depends on real depth, not on layer order", frontGreens > greens + 200, "green pixels " + std::to_string(frontGreens) + " in front, " + std::to_string(greens) + " behind");
        }
    }

    {
        auto first = docScene(ballDoc(red), "world a");
        auto big = docScene(ballDoc(blue), "world b big");
        auto member = docScene(ballDoc(green), "world a member");
        first->wait();
        WorldPlacement place;
        place.x = 0.5f;
        place.z = 0.3f;
        WorldPlacement huge;
        huge.z = 2.0f;
        huge.scale = 3.0f;
        renderer.resetWorlds();
        captureFrame(engine, *first, worldFrame(1, true), target, true);
        const Shot alone = captureFrame(engine, *member, worldFrame(1, false, place), target, true);
        renderer.resetWorlds();
        captureFrame(engine, *first, worldFrame(1, true), target, true);
        captureFrame(engine, *big, worldFrame(2, true, huge), target, true);
        const Shot withOther = captureFrame(engine, *member, worldFrame(1, false, place), target, true);
        check.expect("world: two worlds do not share a depth buffer", alone.ok && withOther.ok && alone.lit > 100 && pixelHash(alone.pixels) == pixelHash(withOther.pixels), "lit " + std::to_string(alone.lit));
        const size_t twoWorlds = renderer.worldDepthBytes();
        check.expect("world: each world in use holds one depth buffer", twoWorlds == 2u * kSize * kSize * 4u, "bytes " + std::to_string(twoWorlds));
        report["world"]["depthBytesTwoWorlds"] = twoWorlds;
        check.expect("world: depth memory is part of the 3D depth total", renderer.depthBytes() >= twoWorlds);
        for (int i = 0; i < 400; ++i) {
            renderer.beginFrame();
        }
        check.expect("world: depth buffers of idle worlds are freed", renderer.worldDepthBytes() == 0, "bytes " + std::to_string(renderer.worldDepthBytes()));
    }

    {
        auto owner = docScene(ballDoc(red), "world size owner");
        auto member = docScene(ballDoc(green), "world size member");
        owner->wait();
        member->wait();
        RenderTarget small;
        if (small.create(192, 192)) {
            renderer.resetWorlds();
            captureFrame(engine, *owner, worldFrame(1, true), target, true);
            const size_t before = renderer.worldDepthBytes();
            while (glGetError() != GL_NO_ERROR) {
            }
            renderer.draw(*member, worldFrame(1, false), small);
            const bool shared = renderer.lastDrawShared();
            const bool clean = glGetError() == GL_NO_ERROR;
            check.expect("world: a layer with another resolution falls back to its own scene", !shared && clean && renderer.worldDepthBytes() == before);
            small.destroy();
        }
    }

    {
        auto member = docScene(ballDoc(green), "world orphan");
        ScenePlayer broken("world broken", [](const GltfLimits&) {
            GltfResult result;
            result.ok = false;
            result.error = "broken on purpose";
            return result;
        });
        broken.wait();
        const Shot isolated = captureFrame(engine, *member, worldFrame(0, false), target, true);
        renderer.resetWorlds();
        while (glGetError() != GL_NO_ERROR) {
        }
        renderer.draw(broken, worldFrame(1, true), target);
        const Shot orphan = captureFrame(engine, *member, worldFrame(1, false, WorldPlacement{}), target, true);
        check.expect("world: a broken owner leaves the other layers drawing on their own", broken.state() == SceneState::Failed && orphan.ok && !renderer.lastDrawShared() && pixelHash(isolated.pixels) == pixelHash(orphan.pixels) && glGetError() == GL_NO_ERROR);
    }

    {
        SceneDoc bound = ballDoc(green);
        bound.objects[0].bindings[0] = {true, BindSource::Kick, BindTarget::Scale, 0.8f};
        auto still = docScene(ballDoc(green), "world still");
        auto moving = docScene(bound, "world moving");
        auto owner = docScene(ballDoc(red), "world beat owner");
        owner->wait();
        WorldPlacement place;
        place.x = 0.9f;
        place.z = 0.9f;
        place.scale = 0.5f;
        SceneFrame loud = worldFrame(1, false, place);
        loud.kick = 1.0f;
        loud.bass = 1.0f;
        loud.beatPulse = 1.0f;
        SceneFrame loudOwner = worldFrame(1, true);
        loudOwner.kick = 1.0f;
        loudOwner.bass = 1.0f;
        renderer.resetWorlds();
        captureFrame(engine, *owner, loudOwner, target, true);
        const Shot quietShot = captureFrame(engine, *still, loud, target, true);
        renderer.resetWorlds();
        captureFrame(engine, *owner, loudOwner, target, true);
        const Shot movingShot = captureFrame(engine, *moving, loud, target, true);
        check.expect("world: each layer keeps its own music bindings", quietShot.ok && movingShot.ok && quietShot.lit > 50 && movingShot.lit > quietShot.lit * 1.2, "lit " + std::to_string(quietShot.lit) + " -> " + std::to_string(movingShot.lit));
    }

    {
        const DocAssets assets = modelFileAssets(models);
        const auto modelDoc = [&](bool cubeFirst) {
            SceneDoc doc = makeDefaultDoc();
            doc.objects.clear();
            DocObject cube = makeDefaultObject(PrimitiveKind::Model);
            cube.model = "cube.glb";
            cube.name = "Cube";
            cube.position = {-0.25f, 0.0f, 0.35f};
            DocObject arm = makeDefaultObject(PrimitiveKind::Model);
            arm.model = "arm.glb";
            arm.name = "Arm";
            arm.position = {0.25f, 0.0f, -0.35f};
            if (cubeFirst) {
                doc.objects = {cube, arm};
            } else {
                doc.objects = {arm, cube};
            }
            sanitizeDoc(doc);
            return doc;
        };
        auto a = docSceneWith(modelDoc(true), "models a", assets);
        auto b = docSceneWith(modelDoc(false), "models b", assets);
        const Shot first = captureFrame(engine, *a, worldFrame(0, false), target, true);
        const Shot second = captureFrame(engine, *b, worldFrame(0, false), target, true);
        SceneDoc onlyCube = modelDoc(true);
        onlyCube.objects.pop_back();
        auto solo = docSceneWith(onlyCube, "models solo", assets);
        const Shot single = captureFrame(engine, *solo, worldFrame(0, false), target, true);
        SceneDoc swapped = modelDoc(true);
        std::swap(swapped.objects[0].position.z, swapped.objects[1].position.z);
        auto behind = docSceneWith(swapped, "models swapped", assets);
        const Shot flipped = captureFrame(engine, *behind, worldFrame(0, false), target, true);
        check.expect("shared scene: two models hide each other by depth, in any object order", first.ok && second.ok && first.lit > 1000 && pixelHash(first.pixels) == pixelHash(second.pixels), "lit " + std::to_string(first.lit) + ", one model alone " + std::to_string(single.lit));
        check.expect("shared scene: moving a model in front of the other changes what is hidden", flipped.ok && pixelHash(flipped.pixels) != pixelHash(first.pixels) && meanDifference(flipped, first) > 0.5, "mean difference " + number(meanDifference(flipped, first)));

        const auto reuseDoc = [&](const char* secondName) {
            SceneDoc doc = makeDefaultDoc();
            doc.objects.clear();
            DocObject one = makeDefaultObject(PrimitiveKind::Model);
            one.model = "cube.glb";
            one.name = "One";
            one.position = {-0.3f, 0.0f, 0.0f};
            DocObject two = one;
            two.model = secondName;
            two.name = "Two";
            two.position = {0.3f, 0.1f, -0.2f};
            doc.objects = {one, two};
            sanitizeDoc(doc);
            return doc;
        };
        auto shared = docSceneWith(reuseDoc("cube.glb"), "models shared", assets);
        auto copied = docSceneWith(reuseDoc("cube_copy.glb"), "models copied", assets);
        const Shot sharedShot = captureFrame(engine, *shared, worldFrame(0, false), target, true);
        const Shot copiedShot = captureFrame(engine, *copied, worldFrame(0, false), target, true);
        const size_t sharedVertices = shared->stats().vertices;
        const size_t copiedVertices = copied->stats().vertices;
        check.expect("shared scene: a model used twice is stored once and looks the same", sharedShot.ok && copiedShot.ok && sharedVertices * 2 == copiedVertices && pixelHash(sharedShot.pixels) == pixelHash(copiedShot.pixels), "vertices " + std::to_string(sharedVertices) + " vs " + std::to_string(copiedVertices));
        report["world"]["modelReuse"] = {{"sharedVertices", sharedVertices}, {"copiedVertices", copiedVertices}, {"sharedGpuBytes", shared->stats().gpuBytes}, {"copiedGpuBytes", copied->stats().gpuBytes}};
    }

    {
        SceneDoc tree = makeDefaultDoc();
        tree.objects.clear();
        DocObject parent = makeDefaultObject(PrimitiveKind::Sphere);
        parent.name = "Parent";
        parent.position = {0.4f, 0.0f, 0.0f};
        parent.scale = {1.2f, 1.2f, 1.2f};
        parent.material.color = red;
        DocObject child = makeDefaultObject(PrimitiveKind::Sphere);
        child.name = "Child";
        child.position = {0.5f, 0.3f, 0.0f};
        child.scale = {0.6f, 0.6f, 0.6f};
        child.material.color = green;
        tree.objects = {parent, child};
        sanitizeDoc(tree);
        docSetParent(tree, 1, 0);
        SceneDoc flat = tree;
        flat.objects[1].parent.clear();
        flat.objects[1].position = {0.4f + 1.2f * 0.5f, 1.2f * 0.3f, 0.0f};
        flat.objects[1].scale = {0.72f, 0.72f, 0.72f};
        auto nested = docScene(tree, "tree nested");
        auto baked = docScene(flat, "tree baked");
        const Shot a = captureFrame(engine, *nested, worldFrame(0, false), target, true);
        const Shot b = captureFrame(engine, *baked, worldFrame(0, false), target, true);
        const double share = differingShare(a, b);
        check.expect("shared scene: a child follows its parent exactly like the baked transform", a.ok && b.ok && a.lit > 500 && share < 0.001 && meanDifference(a, b) < 0.05, "differing share " + number(share * 100.0) + " %");
    }

    {
        SceneDoc many = ballDoc(red);
        many.objects[0].repeat = {RepeatKind::Line, 40, 2.0f, 1.2f};
        auto crowd = docScene(many, "world culling");
        crowd->wait();
        SceneFrame zoomed = worldFrame(0, false);
        zoomed.scale = 3.5f;
        renderer.setCulling(false);
        const Shot plain = captureFrame(engine, *crowd, zoomed, target, true);
        const size_t drawnPlain = renderer.drawnInstances();
        const size_t culledPlain = renderer.culledInstances();
        renderer.setCulling(true);
        const Shot culled = captureFrame(engine, *crowd, zoomed, target, true);
        const size_t drawnCulled = renderer.drawnInstances();
        const size_t culledCulled = renderer.culledInstances();
        check.expect("culling: instances outside the view are skipped and the picture stays the same", plain.ok && culled.ok && culledPlain == 0 && culledCulled > 10 && drawnCulled < drawnPlain && pixelHash(plain.pixels) == pixelHash(culled.pixels), "drawn " + std::to_string(drawnPlain) + " -> " + std::to_string(drawnCulled) + ", skipped " + std::to_string(culledCulled));
        report["world"]["culling"] = {{"instances", drawnPlain}, {"drawnWithCulling", drawnCulled}, {"skipped", culledCulled}};

        size_t scenes = 0;
        size_t same = 0;
        for (const BuiltinSceneInfo& info : builtinScenes()) {
            const auto build = info.build;
            ScenePlayer player(info.name, [build](const GltfLimits&) {
                GltfResult result;
                result.ok = true;
                result.scene = build();
                return result;
            });
            for (const float zoom : {1.0f, 2.5f, 4.0f}) {
                SceneFrame frame = worldFrame(0, false);
                frame.scale = zoom;
                frame.time = 0.9f;
                frame.beat = 1.7;
                renderer.setCulling(false);
                const Shot without = captureFrame(engine, player, frame, target, true);
                renderer.setCulling(true);
                const Shot with = captureFrame(engine, player, frame, target, true);
                ++scenes;
                if (without.ok && with.ok && pixelHash(without.pixels) == pixelHash(with.pixels)) {
                    ++same;
                }
            }
        }
        check.expect("culling: every built-in scene looks the same with and without it, at three zoom levels", scenes > 0 && same == scenes, std::to_string(same) + " of " + std::to_string(scenes));
        for (const char* file : {"skin_arm.glb", "skin_morph.glb", "morph_cube.glb", "blend_far_first.glb"}) {
            auto player = fileScene(models / file);
            bool identical = true;
            for (const float zoom : {1.0f, 4.0f}) {
                SceneFrame frame = worldFrame(0, false);
                frame.scale = zoom;
                renderer.setCulling(false);
                const Shot without = captureFrame(engine, *player, frame, target, true);
                renderer.setCulling(true);
                const Shot with = captureFrame(engine, *player, frame, target, true);
                identical = identical && without.ok && with.ok && pixelHash(without.pixels) == pixelHash(with.pixels);
            }
            check.expect(std::string("culling: model ") + file + " looks the same with and without it", identical);
        }
        renderer.setCulling(true);
    }

    {
        SceneDoc many = ballDoc(red);
        many.objects[0].repeat = {RepeatKind::Ring, 48, 1.5f, 1.0f};
        std::vector<std::unique_ptr<ScenePlayer>> layers;
        for (int i = 0; i < 4; ++i) {
            SceneDoc doc = many;
            doc.objects[0].material.color = i == 0 ? red : (i == 1 ? green : (i == 2 ? blue : std::array<float, 3>{0.9f, 0.8f, 0.1f}));
            layers.push_back(docScene(doc, "world speed"));
            layers.back()->wait();
        }
        constexpr int kRuns = 90;
        double isolatedMs = 0.0;
        double sharedMs = 0.0;
        for (int round = 0; round < 3; ++round) {
            for (const bool shared : {false, true}) {
                glFinish();
                const auto begin = std::chrono::steady_clock::now();
                for (int run = 0; run < kRuns; ++run) {
                    renderer.beginFrame();
                    renderer.resetWorlds();
                    for (int i = 0; i < 4; ++i) {
                        SceneFrame frame = worldFrame(shared ? 1 : 0, i == 0);
                        frame.time = static_cast<float>(run) * 0.02f;
                        if (shared && i > 0) {
                            frame.placement.x = 0.4f * static_cast<float>(i);
                            frame.placement.z = -0.3f * static_cast<float>(i);
                        }
                        renderer.draw(*layers[static_cast<size_t>(i)], frame, target);
                    }
                }
                glFinish();
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / kRuns;
                double& best = shared ? sharedMs : isolatedMs;
                best = round == 0 ? ms : std::min(best, ms);
            }
        }
        report["world"]["frameMs"] = {{"fourIsolated", isolatedMs}, {"fourShared", sharedMs}};
        check.expect("world: four layers in one world cost about the same as four separate scenes", sharedMs < isolatedMs * 1.5 + 0.5, "isolated " + number(isolatedMs) + " ms, shared " + number(sharedMs) + " ms");
    }
}

SceneDoc graphDoc(const GeometryGraph& graph, std::array<float, 3> color = {0.9f, 0.1f, 0.08f}) {
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    GeometryGraph copy = graph;
    docAddGraph(doc, copy);
    DocObject object = makeDefaultObject(PrimitiveKind::Graph);
    object.name = "Graph object";
    object.graph = doc.graphs[0].name;
    object.material.color = color;
    object.material.metallic = 0.0f;
    object.material.roughness = 0.4f;
    object.material.emissiveStrength = 0.0f;
    doc.objects.push_back(object);
    sanitizeDoc(doc);
    return doc;
}

GeometryGraph crowdGraph(int count, bool audio) {
    GeometryGraph graph;
    graph.name = "Crowd";
    GeoNode sphere = makeGeoNode(GeoNodeType::Primitive, "n1");
    GeoNode shrink = makeGeoNode(GeoNodeType::Transform, "n2");
    shrink.inputs[0] = "n1";
    shrink.scale = {0.25f, 0.25f, 0.25f};
    GeoNode ring = makeGeoNode(GeoNodeType::Instance, "n3");
    ring.inputs[0] = "n2";
    ring.instance = count > 64 ? GeoInstanceKind::Scatter : GeoInstanceKind::Ring;
    ring.count = count;
    ring.radius = 2.0f;
    graph.nodes = {sphere, shrink, ring};
    std::string last = "n3";
    if (audio) {
        GeoNode kick = makeGeoNode(GeoNodeType::Audio, "n4");
        kick.inputs[0] = last;
        kick.source = BindSource::Kick;
        kick.target = BindTarget::Scale;
        kick.bindAmount = 0.6f;
        GeoNode ripple = makeGeoNode(GeoNodeType::Audio, "n5");
        ripple.inputs[0] = "n4";
        ripple.source = BindSource::Bass;
        ripple.target = BindTarget::Inflate;
        ripple.bindAmount = 0.4f;
        graph.nodes.push_back(kick);
        graph.nodes.push_back(ripple);
        last = "n5";
    }
    GeoNode output = makeGeoNode(GeoNodeType::Output, "n9");
    output.inputs[0] = last;
    graph.nodes.push_back(output);
    return graph;
}

GeometryGraph plainSphereGraph() {
    GeometryGraph graph;
    graph.name = "Plain";
    GeoNode sphere = makeGeoNode(GeoNodeType::Primitive, "n1");
    GeoNode output = makeGeoNode(GeoNodeType::Output, "n2");
    output.inputs[0] = "n1";
    graph.nodes = {sphere, output};
    return graph;
}

double graphFrameMs(SceneRenderer& renderer, ScenePlayer& player, RenderTarget& target, bool audio) {
    constexpr int kRuns = 60;
    double best = 0.0;
    for (int round = 0; round < 3; ++round) {
        glFinish();
        const auto begin = std::chrono::steady_clock::now();
        for (int run = 0; run < kRuns; ++run) {
            renderer.beginFrame();
            SceneFrame frame = worldFrame(0, false);
            frame.time = static_cast<float>(run) * 0.02f;
            if (audio) {
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(run) * 0.5f);
                frame.kick = pulse;
                frame.bass = pulse;
                frame.beatPulse = pulse;
            }
            renderer.draw(player, frame, target);
        }
        glFinish();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / kRuns;
        best = round == 0 ? ms : std::min(best, ms);
    }
    return best;
}

void runGeometryChecks(Engine& engine, RenderTarget& target, Checker& check, json& report) {
    SceneRenderer& renderer = engine.sceneRenderer();
    const std::array<float, 3> red{0.9f, 0.1f, 0.08f};
    geometryGraphCacheClear();

    {
        auto sphere = docScene(ballDoc(red), "geometry sphere");
        auto graph = docScene(graphDoc(plainSphereGraph(), red), "geometry plain graph");
        const Shot a = captureFrame(engine, *sphere, worldFrame(0, false), target, true);
        const Shot b = captureFrame(engine, *graph, worldFrame(0, false), target, true);
        check.expect("geometry graph: a graph with one sphere draws exactly like the sphere object", a.ok && b.ok && a.lit > 500 && pixelHash(a.pixels) == pixelHash(b.pixels), "lit " + std::to_string(a.lit) + " and " + std::to_string(b.lit));
        report["geometry"]["plainSphereHash"] = pixelHash(b.pixels);
    }

    {
        auto sphere = docScene(ballDoc(red), "geometry sphere");
        auto rock = docScene(graphDoc(makeDefaultGeometryGraph(), red), "geometry rock");
        rock->wait();
        const Shot flat = captureFrame(engine, *sphere, worldFrame(0, false), target, true);
        const Shot first = captureFrame(engine, *rock, worldFrame(0, false), target, true);
        const Shot second = captureFrame(engine, *rock, worldFrame(0, false), target, true);
        check.expect("geometry graph: the default rock draws, differs from a sphere and draws the same twice", first.ok && second.ok && first.lit > 500 && pixelHash(first.pixels) != pixelHash(flat.pixels) && pixelHash(first.pixels) == pixelHash(second.pixels), "lit " + std::to_string(first.lit));
        report["geometry"]["rockHash"] = pixelHash(first.pixels);
        report["geometry"]["rockVertices"] = rock->stats().vertices;
        report["geometry"]["rockGpuBytes"] = rock->stats().gpuBytes;
        report["geometry"]["rockLoadMs"] = rock->loadMilliseconds();
        report["geometry"]["rockUploadMs"] = rock->uploadMilliseconds();
    }

    {
        size_t gpuOne = 0;
        int litOne = 0;
        for (const int count : {1, 10, 1000}) {
            auto crowd = docScene(graphDoc(crowdGraph(count, false), red), "geometry crowd");
            crowd->wait();
            const Shot shot = captureFrame(engine, *crowd, worldFrame(0, false), target, true);
            const size_t drawn = renderer.drawnInstances();
            const size_t culled = renderer.culledInstances();
            if (count == 1) {
                gpuOne = crowd->stats().gpuBytes;
                litOne = shot.lit;
            }
            check.expect("geometry graph: " + std::to_string(count) + " instance(s) share one mesh and all are drawn or culled", shot.ok && shot.lit > 100 && (count != 10 || shot.lit < litOne / 4) && crowd->stats().meshes == 1 && crowd->stats().gpuBytes == gpuOne && drawn + culled == static_cast<size_t>(count), "drawn " + std::to_string(drawn) + ", culled " + std::to_string(culled) + ", lit " + std::to_string(shot.lit));
            report["geometry"]["instances"][std::to_string(count)] = {{"drawn", drawn}, {"culled", culled}, {"lit", shot.lit}, {"nodes", crowd->stats().nodes}, {"meshes", crowd->stats().meshes}, {"vertices", crowd->stats().vertices}, {"gpuBytes", crowd->stats().gpuBytes}, {"loadMs", crowd->loadMilliseconds()}, {"uploadMs", crowd->uploadMilliseconds()}};
        }
    }

    {
        auto reactive = docScene(graphDoc(crowdGraph(24, true), red), "geometry reactive");
        reactive->wait();
        const GeoCacheStats before = geometryGraphCacheStats();
        const Shot quiet = captureFrame(engine, *reactive, musicFrame(0.0f, 0.0, true), target, true);
        const Shot loud = captureFrame(engine, *reactive, musicFrame(1.0f, 0.0, true), target, true);
        const Shot quietAgain = captureFrame(engine, *reactive, musicFrame(0.0f, 0.0, true), target, true);
        const GeoCacheStats after = geometryGraphCacheStats();
        auto still = docScene(graphDoc(crowdGraph(24, false), red), "geometry unreactive");
        still->wait();
        const Shot stillLoud = captureFrame(engine, *still, musicFrame(1.0f, 0.0, true), target, true);
        check.expect("geometry graph: audio changes the picture and nothing is regenerated", quiet.ok && loud.ok && stillLoud.ok && pixelHash(loud.pixels) != pixelHash(stillLoud.pixels) && pixelHash(quiet.pixels) != pixelHash(loud.pixels) && pixelHash(quiet.pixels) == pixelHash(quietAgain.pixels) && after.misses == before.misses, "lit " + std::to_string(quiet.lit) + " -> " + std::to_string(loud.lit) + ", cache misses " + std::to_string(before.misses) + " -> " + std::to_string(after.misses));
        report["geometry"]["audio"] = {{"quietLit", quiet.lit}, {"loudLit", loud.lit}};
    }

    {
        GeometryGraph rock = makeDefaultGeometryGraph();
        GeoNode swell = makeGeoNode(GeoNodeType::Audio, "n6");
        swell.inputs[0] = "n3";
        swell.source = BindSource::Bass;
        swell.target = BindTarget::Inflate;
        swell.bindAmount = 0.5f;
        rock.nodes.insert(rock.nodes.begin() + 3, swell);
        rock.nodes[4].inputs[0] = "n6";
        auto deforming = docScene(graphDoc(rock, red), "geometry deform");
        deforming->wait();
        SceneFrame still = musicFrame(0.0f, 0.0, true);
        SceneFrame bass = musicFrame(0.0f, 0.0, true);
        bass.bass = 1.0f;
        const GeoCacheStats before = geometryGraphCacheStats();
        const Shot calm = captureFrame(engine, *deforming, still, target, true);
        const Shot swollen = captureFrame(engine, *deforming, bass, target, true);
        const GeoCacheStats after = geometryGraphCacheStats();
        check.expect("geometry graph: a bass reaction deforms the mesh in the shader", calm.ok && swollen.ok && pixelHash(calm.pixels) != pixelHash(swollen.pixels) && after.misses == before.misses, "lit " + std::to_string(calm.lit) + " -> " + std::to_string(swollen.lit));
    }

    {
        SceneDoc withCube = graphDoc(makeDefaultGeometryGraph(), red);
        DocObject cube = makeDefaultObject(PrimitiveKind::Cube);
        cube.position = {2.5f, 0.0f, 0.0f};
        withCube.objects.push_back(cube);
        auto on = docScene(withCube, "geometry on");
        const Shot withGraph = captureFrame(engine, *on, worldFrame(0, false), target, true);
        setGeometryGraphsEnabled(false);
        auto off = docScene(withCube, "geometry off");
        const Shot withoutGraph = captureFrame(engine, *off, worldFrame(0, false), target, true);
        setGeometryGraphsEnabled(true);
        SceneDoc cubeOnly = withCube;
        cubeOnly.objects.erase(cubeOnly.objects.begin());
        cubeOnly.graphs.clear();
        auto plain = docScene(cubeOnly, "geometry cube only");
        const Shot justCube = captureFrame(engine, *plain, worldFrame(0, false), target, true);
        bool warned = false;
        for (const std::string& w : off->warnings()) {
            warned = warned || w.compare(0, 40, "Geometry graphs are off in the settings,") == 0;
        }
        check.expect("geometry graph: with the setting off the graph object is left out, the rest draws and a warning is shown", withGraph.ok && withoutGraph.ok && withoutGraph.lit > 100 && justCube.ok && pixelHash(justCube.pixels) == pixelHash(withoutGraph.pixels) && pixelHash(withGraph.pixels) != pixelHash(withoutGraph.pixels) && warned && off->stats().meshes == 1 && on->stats().meshes == 2, "lit " + std::to_string(withGraph.lit) + " -> " + std::to_string(withoutGraph.lit));
    }

    {
        SceneDoc broken = graphDoc(makeDefaultGeometryGraph(), red);
        broken.graphs[0].nodes[1].inputs[0] = "n3";
        broken.graphs[0].nodes[2].inputs[0] = "n2";
        DocObject cube = makeDefaultObject(PrimitiveKind::Cube);
        broken.objects.push_back(cube);
        auto player = docScene(broken, "geometry loop");
        const Shot shot = captureFrame(engine, *player, worldFrame(0, false), target, true);
        bool named = false;
        for (const std::string& w : player->warnings()) {
            named = named || w.compare(0, 19, "Loop through node: ") == 0;
        }
        check.expect("geometry graph: a graph with a loop is reported and the rest of the scene still draws", shot.ok && shot.lit > 100 && named && player->stats().meshes == 1);
    }

    {
        SceneDoc repeated = ballDoc(red);
        repeated.objects[0].scale = {0.25f, 0.25f, 0.25f};
        repeated.objects[0].repeat = {RepeatKind::Ring, 64, 2.0f, 1.0f};
        auto builtIn = docScene(repeated, "geometry repeat");
        auto viaGraph = docScene(graphDoc(crowdGraph(64, false), red), "geometry 64");
        builtIn->wait();
        viaGraph->wait();
        const double repeatMs = graphFrameMs(renderer, *builtIn, target, false);
        const double graphMs = graphFrameMs(renderer, *viaGraph, target, false);
        report["geometry"]["frameMs"]["repeat64"] = repeatMs;
        report["geometry"]["frameMs"]["graph64"] = graphMs;
        check.expect("geometry graph: 64 instances cost about the same as the built-in repeat with 64 copies", graphMs < repeatMs * 1.5 + 0.5, "repeat " + number(repeatMs) + " ms, graph " + number(graphMs) + " ms");
        for (const int count : {1, 10, 1000}) {
            for (const bool audio : {false, true}) {
                auto crowd = docScene(graphDoc(crowdGraph(count, audio), red), "geometry speed");
                crowd->wait();
                const double ms = graphFrameMs(renderer, *crowd, target, audio);
                report["geometry"]["frameMs"][std::to_string(count)][audio ? "withAudio" : "plain"] = ms;
            }
        }
        report["geometry"]["frameMs"]["rock"] = [&]() {
            auto rock = docScene(graphDoc(makeDefaultGeometryGraph(), red), "geometry rock speed");
            rock->wait();
            return graphFrameMs(renderer, *rock, target, false);
        }();
        report["geometry"]["frameMs"]["sphere"] = [&]() {
            auto sphere = docScene(ballDoc(red), "geometry sphere speed");
            sphere->wait();
            return graphFrameMs(renderer, *sphere, target, false);
        }();
    }
    const GeoCacheStats stats = geometryGraphCacheStats();
    report["geometry"]["cache"] = {{"entries", stats.entries}, {"vertices", stats.vertices}, {"hits", stats.hits}, {"misses", stats.misses}};
}

int runSceneSelfTest(Engine& engine, const std::string& modelDirectory, const std::string& outputPath) {
    Checker check;
    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const fs::path models = fs::path(std::u8string(modelDirectory.begin(), modelDirectory.end()));
    if (!engine.sceneRenderer().ready() || !engine.sceneRenderer().pbrReady()) {
        logLine("Scene self test: the scene renderer is not available");
        return 1;
    }
    RenderTarget target;
    if (!target.create(kSize, kSize)) {
        logLine("Scene self test: the render target could not be created");
        return 1;
    }

    {
        auto player = fileScene(models / "skin_arm.glb");
        const Shot rest = capture(engine, *player, 0.0f, target);
        const Shot bent = capture(engine, *player, 1.0f, target);
        check.expect("skinned model draws", rest.ok && bent.ok && rest.lit > 200 && bent.lit > 200, "lit " + std::to_string(rest.lit) + "/" + std::to_string(bent.lit));
        check.expect("skinning bends the arm", bent.width() > rest.width() * 2 && bent.height() < rest.height() * 0.85, "width " + std::to_string(rest.width()) + " -> " + std::to_string(bent.width()) + ", height " + std::to_string(rest.height()) + " -> " + std::to_string(bent.height()));
        check.expect("skinned mesh ignores its node transform", rest.lit > 0 && std::abs(rest.centreX() - kSize * 0.5) < 40.0, "centre " + number(rest.centreX()));
        check.expect("skinning is deterministic", meanDifference(bent, capture(engine, *player, 1.0f, target)) < 0.001);
    }
    {
        auto player = fileScene(models / "morph_cube.glb");
        const Shot rest = capture(engine, *player, 0.0f, target);
        const Shot lifted = capture(engine, *player, 1.0f, target);
        check.expect("morph model draws", rest.ok && lifted.ok && rest.lit > 200, "lit " + std::to_string(rest.lit));
        check.expect("morph target changes the shape", rest.height() > 0 && lifted.height() > rest.height() * 1.5, "height " + std::to_string(rest.height()) + " -> " + std::to_string(lifted.height()));
    }
    {
        auto player = fileScene(models / "morph_many.glb");
        const Shot shot = capture(engine, *player, 0.0f, target);
        const double ratio = shot.width() > 0 ? static_cast<double>(shot.height()) / shot.width() : 0.0;
        check.expect("only the eight strongest morph weights apply", shot.ok && ratio > 1.55 && ratio < 2.05, "height/width " + number(ratio));
    }
    {
        auto player = fileScene(models / "skin_morph.glb");
        const Shot rest = capture(engine, *player, 0.0f, target);
        const Shot both = capture(engine, *player, 1.0f, target);
        check.expect("skin and morph together", rest.ok && both.ok && both.width() > rest.width() * 2);
    }
    {
        auto nearFirst = fileScene(models / "blend_near_first.glb");
        auto farFirst = fileScene(models / "blend_far_first.glb");
        const Shot a = capture(engine, *nearFirst, 0.0f, target);
        const Shot b = capture(engine, *farFirst, 0.0f, target);
        const double difference = meanDifference(a, b);
        check.expect("blending does not depend on the draw order", a.ok && b.ok && difference < 0.5, "difference " + number(difference));
        if (a.ok) {
            const uint8_t* centre = a.at(kSize / 2, kSize / 2);
            const uint8_t* wall = a.at(kSize / 2 + (a.maxX - a.minX) * 38 / 100, kSize / 2);
            const int tint = std::abs(static_cast<int>(centre[0]) - wall[0]) + std::abs(static_cast<int>(centre[1]) - wall[1]) + std::abs(static_cast<int>(centre[2]) - wall[2]);
            check.expect("blended planes tint the wall behind", tint > 30 && centre[0] > centre[2], "tint " + std::to_string(tint) + " centre " + std::to_string(centre[0]) + "," + std::to_string(centre[1]) + "," + std::to_string(centre[2]));
        }
    }
    {
        auto player = fileScene(models / "blend_opaque_mesh.glb");
        const Shot shot = capture(engine, *player, 0.0f, target);
        if (shot.ok) {
            const uint8_t* centre = shot.at(kSize / 2, kSize / 2);
            const bool nearWins = centre[1] > centre[0] + 40 && centre[1] > centre[2] + 40;
            check.expect("opaque parts of one blended mesh hide what lies behind them", nearWins, "centre " + std::to_string(centre[0]) + "," + std::to_string(centre[1]) + "," + std::to_string(centre[2]));
        } else {
            check.expect("opaque parts of one blended mesh hide what lies behind them", false, "no picture");
        }
    }
    {
        auto player = fileScene(models / "lights_cameras.glb");
        const Shot shot = capture(engine, *player, 0.0f, target);
        check.expect("file lights and cameras do not change the picture", shot.ok && shot.lit > 100 && player->scene().lights.empty());
    }
    {
        SceneDoc doc = makeDefaultDoc();
        doc.objects.clear();
        for (int i = 0; i < 9; ++i) {
            DocObject arm = makeDefaultObject(PrimitiveKind::Model);
            arm.model = "skin_arm.glb";
            arm.position = {static_cast<float>(i) * 3.0f, 0.0f, 0.0f};
            arm.repeat = {RepeatKind::Line, 64, 1.0f, 1.0f};
            doc.objects.push_back(arm);
        }
        DocAssets assets;
        assets.loadModel = [models](const std::string& name, const GltfLimits& limits) -> std::shared_ptr<const GltfResult> {
            return std::make_shared<GltfResult>(loadGltfFile(models / name, limits));
        };
        assets.loadImage = [](const std::string&, std::string& error) -> std::shared_ptr<const SceneImage> {
            error = "not here";
            return nullptr;
        };
        ScenePlayer crowd("crowd", [doc, assets](const GltfLimits& limits) { return buildSceneFromDoc(doc, assets, limits); });
        const Shot shot = capture(engine, crowd, 0.5f, target);
        const size_t skipped = engine.sceneRenderer().skippedSkinned();
        check.expect("joint limit skips surplus figures without errors", shot.ok && skipped == 576 - kSceneMaxJointsPerFrame / 2, "skipped " + std::to_string(skipped));
        report["crowdSkipped"] = skipped;
    }
    {
        const char* const pairs[][2] = {{"skin_morph.glb", "meshopt_quant_skin_morph.glb"}, {"skin_morph.glb", "meshopt_exact_skin_morph.glb"}, {"skin_morph.glb", "draco_skin_morph.glb"}};
        for (const auto& pair : pairs) {
            if (!fs::exists(models / pair[1])) {
                continue;
            }
            auto plain = fileScene(models / pair[0]);
            auto packed = fileScene(models / pair[1]);
            const Shot a = capture(engine, *plain, 1.0f, target);
            const Shot b = capture(engine, *packed, 1.0f, target);
            if (!b.ok && packed->state() == SceneState::Failed) {
                const bool refused = packed->error().find("NYREXVJ_") != std::string::npos;
                check.expect(std::string(pair[1]) + " is refused with a clear message", refused, packed->error());
                continue;
            }
            const double difference = meanDifference(a, b);
            check.expect(std::string(pair[1]) + " looks like the original", a.ok && b.ok && difference < 3.0, "difference " + number(difference));
        }
    }
    {
        std::vector<double> milliseconds;
        for (const char* name : {"skin_arm.glb", "morph_cube.glb", "blend_near_first.glb"}) {
            auto player = fileScene(models / name);
            capture(engine, *player, 0.0f, target);
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < 30; ++i) {
                capture(engine, *player, static_cast<float>(i) * 0.05f, target);
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / 30.0;
            report["frameMs"][name] = ms;
        }
    }
    {
        auto plain = docScene(sphereDoc({}, false), "matbind plain");
        const Shot base = captureFrame(engine, *plain, musicFrame(1.0f, 0.0, true), target);
        check.expect("material test sphere draws", base.ok && base.lit > 1500, "lit " + std::to_string(base.lit));
        const Tint baseTint = meanTint(base);
        check.expect("the plain sphere is red", baseTint.r > baseTint.g * 2.0 && baseTint.r > baseTint.b * 2.0, "tint " + number(baseTint.r) + "," + number(baseTint.g) + "," + number(baseTint.b));

        auto hue = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::Hue, 1.0f / 3.0f}}, false), "matbind hue");
        const Shot third = captureFrame(engine, *hue, musicFrame(1.0f, 0.0, true), target);
        const Tint thirdTint = meanTint(third);
        check.expect("a third of a colour turn moves red to green", third.ok && thirdTint.g > thirdTint.r * 2.0 && thirdTint.g > thirdTint.b * 2.0, "tint " + number(thirdTint.r) + "," + number(thirdTint.g) + "," + number(thirdTint.b));
        const Shot silent = captureFrame(engine, *hue, musicFrame(0.0f, 0.5, true), target);
        check.expect("without music the colour stays", meanDifference(base, silent) < 0.05 || meanDifference(captureFrame(engine, *plain, musicFrame(0.0f, 0.5, true), target), silent) < 0.05);
        auto turn = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::Hue, 1.0f}}, false), "matbind turn");
        const double whole = meanDifference(base, captureFrame(engine, *turn, musicFrame(1.0f, 0.0, true), target));
        check.expect("a whole colour turn comes back to the start", whole < 0.6, "difference " + number(whole));
        report["matbind"]["hueThirdTint"] = {thirdTint.r, thirdTint.g, thirdTint.b};

        const double offByMusic = meanDifference(base, captureFrame(engine, *hue, musicFrame(1.0f, 0.0, false), target));
        check.expect("switch off: material bindings are ignored", offByMusic < 0.001, "difference " + number(offByMusic));
        auto scaled = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::Scale, 0.5f}, {true, BindSource::Kick, BindTarget::Hue, 0.4f}}, false), "matbind scale");
        const Shot big = captureFrame(engine, *scaled, musicFrame(1.0f, 0.0, false), target);
        check.expect("switch off: size still follows the music", big.lit > base.lit * 1.6, "lit " + std::to_string(base.lit) + " -> " + std::to_string(big.lit));
        const Tint bigTint = meanTint(big);
        check.expect("switch off: colour stays red", bigTint.r > bigTint.g * 2.0, "tint " + number(bigTint.r) + "," + number(bigTint.g));

        SceneDoc shiny = sphereDoc({}, false);
        shiny.objects[0].material.metallic = 0.9f;
        shiny.objects[0].material.roughness = 0.2f;
        auto glossy = docScene(shiny, "matbind glossy");
        shiny.objects[0].bindings[0] = {true, BindSource::Kick, BindTarget::Roughness, 0.7f};
        auto dull = docScene(shiny, "matbind dull");
        const Shot sharp = captureFrame(engine, *glossy, musicFrame(1.0f, 0.0, true), target);
        const Shot rough = captureFrame(engine, *dull, musicFrame(1.0f, 0.0, true), target);
        const double roughDifference = meanDifference(sharp, rough);
        check.expect("roughness changes the shine", sharp.ok && rough.ok && roughDifference > 0.8 && std::abs(sharp.lit - rough.lit) < sharp.lit / 20, "difference " + number(roughDifference));
        report["matbind"]["roughnessDifference"] = roughDifference;

        auto flat = docScene(sphereDoc({}, true), "matbind flat");
        const Shot texturedBase = captureFrame(engine, *flat, musicFrame(1.0f, 0.0, true), target);
        auto slid = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::UvX, 0.3f}}, true), "matbind slide");
        const double slidDifference = meanDifference(texturedBase, captureFrame(engine, *slid, musicFrame(1.0f, 0.0, true), target));
        check.expect("texture shift moves the texture", texturedBase.ok && slidDifference > 3.0, "difference " + number(slidDifference));
        auto slidAround = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::UvX, 1.0f}, {true, BindSource::Kick, BindTarget::UvY, -2.0f}}, true), "matbind around");
        const double aroundDifference = meanDifference(texturedBase, captureFrame(engine, *slidAround, musicFrame(1.0f, 0.0, true), target));
        check.expect("a whole texture shift comes back to the start", aroundDifference < 0.6, "difference " + number(aroundDifference));
        report["matbind"]["uvDifference"] = slidDifference;

        auto inflated = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::Inflate, 0.5f}}, false), "matbind inflate");
        auto deflated = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::Inflate, -0.5f}}, false), "matbind deflate");
        const Shot grown = captureFrame(engine, *inflated, musicFrame(1.0f, 0.0, true), target);
        const Shot shrunk = captureFrame(engine, *deflated, musicFrame(1.0f, 0.0, true), target);
        check.expect("inflate makes the shape bigger", grown.lit > base.lit * 1.2, "lit " + std::to_string(base.lit) + " -> " + std::to_string(grown.lit));
        check.expect("negative inflate makes the shape smaller", shrunk.lit < base.lit * 0.85 && shrunk.lit > 100, "lit " + std::to_string(base.lit) + " -> " + std::to_string(shrunk.lit));
        const double calm = meanDifference(base, captureFrame(engine, *inflated, musicFrame(0.0f, 0.5, true), target));
        check.expect("inflate does nothing without music", calm < 0.05 || meanDifference(captureFrame(engine, *plain, musicFrame(0.0f, 0.5, true), target), captureFrame(engine, *inflated, musicFrame(0.0f, 0.5, true), target)) < 0.001);
        report["matbind"]["inflateLit"] = {base.lit, grown.lit, shrunk.lit};

        auto ripple = docScene(sphereDoc({{true, BindSource::Kick, BindTarget::Ripple, 0.8f}}, false), "matbind ripple");
        SceneFrame early = musicFrame(1.0f, 0.0, true);
        SceneFrame later = early;
        later.time = 1.45f;
        const Shot waveA = captureFrame(engine, *ripple, early, target);
        const Shot waveB = captureFrame(engine, *ripple, later, target);
        const Shot waveC = captureFrame(engine, *ripple, early, target);
        check.expect("ripple changes the surface", meanDifference(base, waveA) > 0.4, "difference " + number(meanDifference(base, waveA)));
        check.expect("ripple moves with time", meanDifference(waveA, waveB) > 0.2, "difference " + number(meanDifference(waveA, waveB)));
        check.expect("ripple repeats exactly at the same time", meanDifference(waveA, waveC) < 0.001);

        auto everything = docScene(sphereDoc({{true, BindSource::Band2, BindTarget::Hue, 0.2f}, {true, BindSource::Beat, BindTarget::Roughness, 0.4f}, {true, BindSource::Kick, BindTarget::Ripple, 0.3f}, {true, BindSource::Band7, BindTarget::Inflate, 0.2f}}, false), "matbind all");
        auto twin = docScene(sphereDoc({{true, BindSource::Band2, BindTarget::Hue, 0.2f}, {true, BindSource::Beat, BindTarget::Roughness, 0.4f}, {true, BindSource::Kick, BindTarget::Ripple, 0.3f}, {true, BindSource::Band7, BindTarget::Inflate, 0.2f}}, false), "matbind twin");
        bool repeatable = true;
        bool moving = false;
        std::string firstHash;
        for (int step = 0; step < 12; ++step) {
            const double beat = 0.23 * step;
            const double time = 0.31 * step;
            SceneFrame frame;
            frame.time = static_cast<float>(time);
            frame.editDistance = 1.9f;
            frame.setAudio(virtualAudio(beat, time), beat);
            const Shot one = captureFrame(engine, *everything, frame, target);
            const Shot two = captureFrame(engine, *twin, frame, target);
            repeatable = repeatable && one.ok && two.ok && pixelHash(one.pixels) == pixelHash(two.pixels);
            if (step == 0) {
                firstHash = pixelHash(one.pixels);
            } else {
                moving = moving || pixelHash(one.pixels) != firstHash;
            }
            report["matbind"]["previewHashes"][std::to_string(step)] = pixelHash(one.pixels);
        }
        check.expect("the same music gives the same picture on two players", repeatable);
        check.expect("different music moments give different pictures", moving);

        const SceneDoc unknown = [] {
            SceneDoc doc;
            std::string error;
            const std::string text = "{\"format\":\"nyxscene\",\"version\":2,\"name\":\"odd\",\"objects\":[{\"name\":\"Ball\",\"kind\":\"sphere\",\"bindings\":[{\"source\":\"band2\",\"target\":\"hue\",\"amount\":0.2},{\"source\":\"theremin\",\"target\":\"scale\",\"amount\":1},{\"source\":\"kick\",\"target\":\"sparkle\",\"amount\":1}]}]}";
            docFromJson(text, doc, error);
            return doc;
        }();
        auto odd = docScene(unknown, "matbind odd");
        const Shot oddShot = captureFrame(engine, *odd, musicFrame(1.0f, 0.0, true), target);
        int noted = 0;
        for (const std::string& warning : odd->warnings()) {
            noted += warning.find("unknown source 'theremin'") != std::string::npos || warning.find("unknown target 'sparkle'") != std::string::npos ? 1 : 0;
        }
        check.expect("unknown sources and targets are dropped with a note and the scene still draws", oddShot.ok && oddShot.lit > 300 && noted == 2, "lit " + std::to_string(oddShot.lit) + ", notes " + std::to_string(noted));

        auto manual = docScene(sphereDoc({{true, BindSource::Beat, BindTarget::UvX, 0.5f}}, true), "matbind beat");
        const Shot onBeat = captureFrame(engine, *manual, musicFrame(0.0f, 0.0, true), target);
        const Shot offBeat = captureFrame(engine, *manual, musicFrame(0.0f, 0.5, true), target);
        check.expect("the beat source follows the beat position", meanDifference(onBeat, offBeat) > 1.0, "difference " + number(meanDifference(onBeat, offBeat)));
    }
    {
        const std::string materialsOn = "bindings on";
        std::vector<double> frameMs;
        for (int mode = 0; mode < 3; ++mode) {
            SceneDoc doc = makeDefaultDoc();
            doc.objects.clear();
            for (int i = 0; i < 24; ++i) {
                DocObject sphere = makeDefaultObject(PrimitiveKind::Sphere);
                sphere.position = {static_cast<float>(i % 6) * 2.2f, static_cast<float>(i / 6) * 2.2f, 0.0f};
                if (mode > 0) {
                    sphere.bindings[0] = {true, BindSource::Band2, BindTarget::Hue, 0.3f};
                    sphere.bindings[1] = {true, BindSource::Beat, BindTarget::Roughness, 0.3f};
                    sphere.bindings[2] = {true, BindSource::Kick, BindTarget::Inflate, 0.2f};
                    sphere.bindings[3] = {true, BindSource::Band5, BindTarget::Ripple, 0.2f};
                }
                doc.objects.push_back(sphere);
            }
            auto player = docScene(doc, "matbind cost");
            captureFrame(engine, *player, musicFrame(1.0f, 0.0, mode == 2), target);
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < 40; ++i) {
                SceneFrame frame;
                frame.time = static_cast<float>(i) * 0.05f;
                frame.editDistance = 1.9f;
                frame.setAudio(virtualAudio(0.1 * i, 0.05 * i), 0.1 * i);
                frame.materialBindings = mode == 2;
                captureFrame(engine, *player, frame, target);
            }
            frameMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / 40.0);
        }
        report["matbind"]["frameMs"] = {{"noBindings", frameMs[0]}, {"bindingsOff", frameMs[1]}, {"bindingsOn", frameMs[2]}};
        {
            SceneDoc crowd = makeDefaultDoc();
            crowd.objects.clear();
            for (int i = 0; i < 64; ++i) {
                DocObject sphere = makeDefaultObject(PrimitiveKind::Cube);
                sphere.repeat = {RepeatKind::Ring, 8, 3.0f, 1.0f};
                crowd.objects.push_back(sphere);
            }
            const DocAssets assets = bindingAssets();
            GltfResult plainBuild = buildSceneFromDoc(crowd, assets);
            for (DocObject& object : crowd.objects) {
                object.bindings[0] = {true, BindSource::Band2, BindTarget::Hue, 0.3f};
                object.bindings[1] = {true, BindSource::Beat, BindTarget::Roughness, 0.3f};
                object.bindings[2] = {true, BindSource::Kick, BindTarget::Inflate, 0.2f};
                object.bindings[3] = {true, BindSource::Band5, BindTarget::Ripple, 0.2f};
            }
            GltfResult boundBuild = buildSceneFromDoc(crowd, assets);
            SceneAudio audio;
            audio.kick = 0.7f;
            audio.bands[1] = 0.4f;
            audio.bands[4] = 0.6f;
            audio.beat = 0.5f;
            double plainMicros = 0.0;
            double boundMicros = 0.0;
            size_t instances = 0;
            for (int round = 0; round < 3; ++round) {
                for (int mode = 0; mode < 2; ++mode) {
                    const Scene& scene = mode == 0 ? plainBuild.scene : boundBuild.scene;
                    const auto begin = std::chrono::steady_clock::now();
                    for (int i = 0; i < 400; ++i) {
                        instances = evaluateSceneFull(scene, -1, static_cast<float>(i) * 0.01f, &audio).instances.size();
                    }
                    const double micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() / 400.0;
                    (mode == 0 ? plainMicros : boundMicros) = round == 0 ? micros : std::min(mode == 0 ? plainMicros : boundMicros, micros);
                }
            }
            report["matbind"]["evaluateMicros"] = {{"instances", instances}, {"noBindings", plainMicros}, {"fourBindings", boundMicros}};
            check.expect("evaluating bindings stays cheap", instances == 512 && boundMicros < plainMicros * 3.0 + 200.0, "us " + number(plainMicros) + " -> " + number(boundMicros) + " for " + std::to_string(instances) + " instances");
        }
        check.expect("material bindings cost stays measured and small", frameMs[2] < frameMs[0] * 1.5 + 4.0, "ms " + number(frameMs[0]) + " / " + number(frameMs[1]) + " / " + number(frameMs[2]));
    }
    {
        const SceneDoc base = lopsidedDoc();
        auto basePlayer = docScene(base, "camera base");
        const Shot still = captureFrame(engine, *basePlayer, stillFrame(0.0), target, true);
        check.expect("camera test scene draws", still.ok && still.lit > 1500 && still.width() > still.height() * 1.5, "lit " + std::to_string(still.lit) + ", " + std::to_string(still.width()) + "x" + std::to_string(still.height()));
        const Centre stillCentre = centroid(still);

        bool identical = true;
        for (const double beat : {0.0, 1.3, 4.7, 9.9}) {
            const Shot on = captureFrame(engine, *basePlayer, stillFrame(beat, true), target, true);
            const Shot off = captureFrame(engine, *basePlayer, stillFrame(beat, false), target, true);
            identical = identical && on.ok && off.ok && pixelHash(on.pixels) == pixelHash(off.pixels);
        }
        check.expect("without camera features the switch changes nothing", identical);

        const auto variant = [&](const char* name, const std::function<void(SceneDoc&)>& edit) {
            SceneDoc doc = base;
            edit(doc);
            return docScene(doc, name);
        };

        auto moveRight = variant("camera move x", [](SceneDoc& doc) { doc.camera.move.x = 1.0f; });
        const Shot shifted = captureFrame(engine, *moveRight, stillFrame(0.0), target, true);
        const Centre shiftedCentre = centroid(shifted);
        check.expect("look-at offset moves the view sideways", shifted.ok && stillCentre.x - shiftedCentre.x > 12.0, "centre x " + number(stillCentre.x) + " -> " + number(shiftedCentre.x));
        const Shot shiftedOff = captureFrame(engine, *moveRight, stillFrame(0.0, false), target, true);
        check.expect("switch off: look-at offset is ignored", meanDifference(still, shiftedOff) < 0.001);

        auto moveUp = variant("camera move y", [](SceneDoc& doc) { doc.camera.move.y = 1.0f; });
        auto moveDown = variant("camera move y down", [](SceneDoc& doc) { doc.camera.move.y = -1.0f; });
        const Centre upCentre = centroid(captureFrame(engine, *moveUp, stillFrame(0.0), target, true));
        const Centre downCentre = centroid(captureFrame(engine, *moveDown, stillFrame(0.0), target, true));
        check.expect("look-at offset up and down move the view in opposite ways", (upCentre.y - stillCentre.y) * (downCentre.y - stillCentre.y) < 0.0 && std::abs(upCentre.y - downCentre.y) > 20.0, "centre y " + number(upCentre.y) + " / " + number(stillCentre.y) + " / " + number(downCentre.y));

        auto moveDepth = variant("camera move z", [](SceneDoc& doc) { doc.camera.move.z = 1.5f; });
        check.expect("look-at offset in depth changes the picture", meanDifference(still, captureFrame(engine, *moveDepth, stillFrame(0.0), target, true)) > 0.5);

        auto rolled = variant("camera roll", [](SceneDoc& doc) { doc.camera.rollDegrees = 90.0f; });
        const Shot turned = captureFrame(engine, *rolled, stillFrame(0.0), target, true);
        check.expect("roll turns the picture sideways", turned.ok && turned.height() > turned.width() * 1.5 && turned.lit > still.lit * 0.9 && turned.lit < still.lit * 1.1, "size " + std::to_string(turned.width()) + "x" + std::to_string(turned.height()) + ", lit " + std::to_string(still.lit) + " -> " + std::to_string(turned.lit));
        check.expect("switch off: roll is ignored", meanDifference(still, captureFrame(engine, *rolled, stillFrame(0.0, false), target, true)) < 0.001);
        const Shot editorBase = captureFrame(engine, *basePlayer, stillFrame(0.0), target);
        check.expect("the free preview camera ignores roll and look-at offset of the scene camera", editorBase.ok && meanDifference(editorBase, captureFrame(engine, *rolled, stillFrame(0.0), target)) < 0.001 && meanDifference(editorBase, captureFrame(engine, *moveRight, stillFrame(0.0), target)) < 0.001);
        auto rolledHalf = variant("camera roll half", [](SceneDoc& doc) { doc.camera.rollDegrees = 180.0f; });
        const Centre flipped = centroid(captureFrame(engine, *rolledHalf, stillFrame(0.0), target, true));
        check.expect("a half turn of roll mirrors the picture through the centre", std::abs((flipped.x - 127.5) + (stillCentre.x - 127.5)) < 2.0 && std::abs((flipped.y - 127.5) + (stillCentre.y - 127.5)) < 2.0, "centre " + number(stillCentre.x) + "," + number(stillCentre.y) + " -> " + number(flipped.x) + "," + number(flipped.y));

        const auto bound = [&](BindSource source, BindTarget goal, float amount, const char* name) {
            return variant(name, [&](SceneDoc& doc) { doc.camera.bindings[0] = {true, source, goal, amount}; });
        };
        auto turnedByMusic = bound(BindSource::Bass, BindTarget::Orbit, 1.0f, "camera orbit binding");
        auto sideCamera = variant("camera side", [](SceneDoc& doc) { doc.camera.yawDegrees = 90.0f; });
        const Shot side = captureFrame(engine, *sideCamera, loudFrame(0.0), target, true);
        const Shot orbited = captureFrame(engine, *turnedByMusic, loudFrame(0.0), target, true);
        check.expect("orbit binding turns the camera like a camera placed there", orbited.ok && meanDifference(side, orbited) < 0.05 && meanDifference(still, orbited) > 1.5, "difference " + number(meanDifference(side, orbited)));
        check.expect("orbit binding is quiet without music", meanDifference(still, captureFrame(engine, *turnedByMusic, stillFrame(0.0), target, true)) < 0.001);
        check.expect("switch off: orbit binding is ignored", meanDifference(captureFrame(engine, *basePlayer, loudFrame(0.0, false), target, true), captureFrame(engine, *turnedByMusic, loudFrame(0.0, false), target, true)) < 0.001);

        auto tiltedByMusic = bound(BindSource::Kick, BindTarget::Tilt, 1.0f, "camera tilt binding");
        auto highCamera = variant("camera high", [](SceneDoc& doc) { doc.camera.pitchDegrees = 45.0f; });
        const double tiltDifference = meanDifference(captureFrame(engine, *highCamera, loudFrame(0.0), target, true), captureFrame(engine, *tiltedByMusic, loudFrame(0.0), target, true));
        check.expect("tilt binding raises the camera like a camera placed there", tiltDifference < 0.05, "difference " + number(tiltDifference));

        auto widenedByMusic = bound(BindSource::Bass, BindTarget::Fov, 1.0f, "camera fov binding");
        auto wideCamera = variant("camera wide", [](SceneDoc& doc) { doc.camera.fovDegrees = 76.0f; });
        const double fovDifference = meanDifference(captureFrame(engine, *wideCamera, loudFrame(0.0), target, true), captureFrame(engine, *widenedByMusic, loudFrame(0.0), target, true));
        check.expect("field of view binding widens the lens like a camera set there", fovDifference < 0.05 && fovDifference + 0.0 < meanDifference(still, captureFrame(engine, *widenedByMusic, loudFrame(0.0), target, true)), "difference " + number(fovDifference));

        auto rollByMusic = bound(BindSource::Beat, BindTarget::Roll, 2.0f, "camera roll binding");
        auto moveByMusic = bound(BindSource::Bass, BindTarget::MoveX, 2.0f, "camera move binding");
        check.expect("roll binding follows the beat", meanDifference(captureFrame(engine, *rollByMusic, beatFrame(0.0), target, true), captureFrame(engine, *rollByMusic, beatFrame(0.5), target, true)) > 1.0);
        const Centre movedByMusic = centroid(captureFrame(engine, *moveByMusic, loudFrame(0.0), target, true));
        check.expect("move binding shifts the view", stillCentre.x - movedByMusic.x > 12.0, "centre x " + number(stillCentre.x) + " -> " + number(movedByMusic.x));

        SceneFrame layerOrbit = stillFrame(0.0);
        layerOrbit.cameraOrbit = 90.0f;
        const Shot layerSide = captureFrame(engine, *basePlayer, layerOrbit, target, true);
        const Shot sideStill = captureFrame(engine, *sideCamera, stillFrame(0.0), target, true);
        check.expect("layer orbit control turns the camera like a camera placed there", layerSide.ok && meanDifference(sideStill, layerSide) < 0.001, "difference " + number(meanDifference(sideStill, layerSide)));
        layerOrbit.cameraMoves = false;
        check.expect("switch off: layer orbit control is ignored", meanDifference(still, captureFrame(engine, *basePlayer, layerOrbit, target, true)) < 0.001);
        SceneFrame layerTilt = stillFrame(0.0);
        layerTilt.cameraTilt = 45.0f;
        check.expect("layer tilt control raises the camera like a camera placed there", meanDifference(captureFrame(engine, *highCamera, stillFrame(0.0), target, true), captureFrame(engine, *basePlayer, layerTilt, target, true)) < 0.001);
        SceneFrame layerFov = stillFrame(0.0);
        layerFov.cameraFov = 30.0f;
        check.expect("layer field of view control widens the lens like a camera set there", meanDifference(captureFrame(engine, *wideCamera, stillFrame(0.0), target, true), captureFrame(engine, *basePlayer, layerFov, target, true)) < 0.05);

        const auto twoCameras = [&](float glide, CameraGlide shape, float firstBeats) {
            SceneDoc doc = base;
            doc.cutBeats = 4.0f;
            doc.glide = glide;
            doc.glideShape = shape;
            doc.camera.beats = firstBeats;
            DocCamera second = doc.camera;
            second.name = "Camera 2";
            second.yawDegrees = 90.0f;
            second.beats = 0.0f;
            docAddCamera(doc, second);
            docCameraAt(doc, 0).beats = firstBeats;
            return docScene(doc, "camera glide");
        };
        auto cuts = twoCameras(0.0f, CameraGlide::Smooth, 0.0f);
        auto gliding = twoCameras(1.0f, CameraGlide::Linear, 0.0f);
        const Shot first = captureFrame(engine, *cuts, stillFrame(1.0), target, true);
        const Shot second = captureFrame(engine, *cuts, stillFrame(5.0), target, true);
        check.expect("hard cut shows the first camera, then the second", meanDifference(still, first) < 0.001 && meanDifference(sideStill, second) < 0.001 && meanDifference(first, second) > 1.5);
        const double jumpCut = meanDifference(captureFrame(engine, *cuts, stillFrame(3.999), target, true), captureFrame(engine, *cuts, stillFrame(4.001), target, true));
        const double jumpGlide = meanDifference(captureFrame(engine, *gliding, stillFrame(3.999), target, true), captureFrame(engine, *gliding, stillFrame(4.001), target, true));
        check.expect("gliding removes the jump at the cut", jumpCut > 1.5 && jumpGlide < 0.3, "difference at the cut " + number(jumpCut) + " -> " + number(jumpGlide));
        auto halfWay = variant("camera halfway", [](SceneDoc& doc) { doc.camera.yawDegrees = 45.0f; });
        const double halfDifference = meanDifference(captureFrame(engine, *halfWay, stillFrame(0.0), target, true), captureFrame(engine, *gliding, stillFrame(2.0), target, true));
        check.expect("a linear glide is halfway between the cameras in the middle of the shot", halfDifference < 0.3, "difference " + number(halfDifference));
        const double offGlide = meanDifference(first, captureFrame(engine, *gliding, stillFrame(2.0, false), target, true));
        check.expect("switch off: no glide, the old hard cut stays", offGlide < 0.001 && meanDifference(second, captureFrame(engine, *gliding, stillFrame(5.0, false), target, true)) < 0.3, "difference " + number(offGlide));
        auto timed = twoCameras(0.0f, CameraGlide::Smooth, 2.0f);
        check.expect("shot length keeps a camera for its own beats", meanDifference(still, captureFrame(engine, *timed, stillFrame(1.9), target, true)) < 0.001 && meanDifference(sideStill, captureFrame(engine, *timed, stillFrame(2.1), target, true)) < 0.001);
        check.expect("switch off: shot lengths are ignored", meanDifference(still, captureFrame(engine, *timed, stillFrame(2.1, false), target, true)) < 0.001);

        bool repeatable = true;
        bool moving = false;
        auto busy = variant("camera busy", [](SceneDoc& doc) {
            doc.cutBeats = 4.0f;
            doc.glide = 0.5f;
            doc.camera.move = {0.4f, 0.2f, 0.0f};
            doc.camera.rollDegrees = 8.0f;
            doc.camera.orbitSpeed = 0.2f;
            doc.camera.bindings[0] = {true, BindSource::Bass, BindTarget::Orbit, 0.1f};
            doc.camera.bindings[1] = {true, BindSource::Band3, BindTarget::MoveY, 0.3f};
            DocCamera other = doc.camera;
            other.yawDegrees = 120.0f;
            other.rollDegrees = -20.0f;
            docAddCamera(doc, other);
        });
        auto busyTwin = variant("camera busy twin", [](SceneDoc& doc) {
            doc.cutBeats = 4.0f;
            doc.glide = 0.5f;
            doc.camera.move = {0.4f, 0.2f, 0.0f};
            doc.camera.rollDegrees = 8.0f;
            doc.camera.orbitSpeed = 0.2f;
            doc.camera.bindings[0] = {true, BindSource::Bass, BindTarget::Orbit, 0.1f};
            doc.camera.bindings[1] = {true, BindSource::Band3, BindTarget::MoveY, 0.3f};
            DocCamera other = doc.camera;
            other.yawDegrees = 120.0f;
            other.rollDegrees = -20.0f;
            docAddCamera(doc, other);
        });
        std::string firstHash;
        for (int step = 0; step < 16; ++step) {
            const double beat = 0.55 * step;
            const double time = 0.31 * step;
            SceneFrame frame;
            frame.time = static_cast<float>(time);
            frame.setAudio(virtualAudio(beat, time), beat);
            const Shot one = captureFrame(engine, *busy, frame, target, true);
            const Shot two = captureFrame(engine, *busyTwin, frame, target, true);
            repeatable = repeatable && one.ok && two.ok && pixelHash(one.pixels) == pixelHash(two.pixels);
            if (step == 0) {
                firstHash = pixelHash(one.pixels);
            } else {
                moving = moving || pixelHash(one.pixels) != firstHash;
            }
            report["camera"]["previewHashes"][std::to_string(step)] = pixelHash(one.pixels);
        }
        check.expect("the same music and beats give the same camera on two players", repeatable);
        check.expect("the camera moves from moment to moment", moving);

        const fs::path cameraModel = models / "lights_cameras.glb";
        if (fs::exists(cameraModel)) {
            SceneDoc doc = makeDefaultDoc();
            doc.objects.clear();
            DocObject triangle = makeDefaultObject(PrimitiveKind::Model);
            triangle.model = "lights_cameras.glb";
            triangle.material.emissiveStrength = 0.0f;
            doc.objects.push_back(triangle);
            DocAssets assets;
            assets.loadModel = [models](const std::string& name, const GltfLimits& limits) -> std::shared_ptr<const GltfResult> {
                return std::make_shared<GltfResult>(loadGltfFile(models / name, limits));
            };
            assets.loadImage = [](const std::string&, std::string& error) -> std::shared_ptr<const SceneImage> {
                error = "not here";
                return nullptr;
            };
            ScenePlayer plain("camera file", [doc, assets](const GltfLimits& limits) { return buildSceneFromDoc(doc, assets, limits); });
            plain.wait();
            const std::vector<SceneFileCamera>& found = plain.scene().fileCameras;
            check.expect("cameras of a model file are found with their names", found.size() == 2 && found[0].name.find("main") != std::string::npos && !found[0].orthographic && found[1].orthographic, "found " + std::to_string(found.size()));
            if (found.size() == 2) {
                SceneDoc withFile = doc;
                withFile.camera = docCameraFromScene(sceneCameraFromFile(found[0], plain.fitCenter(), plain.fitRadius()), "main");
                withFile.camera.orbitSpeed = 0.0f;
                ScenePlayer used("camera file used", [withFile, assets](const GltfLimits& limits) { return buildSceneFromDoc(withFile, assets, limits); });
                const Shot view = captureFrame(engine, used, stillFrame(0.0), target, true);
                const bool placed = view.ok && view.lit > 300 && std::abs(view.minX - 128) <= 8 && view.width() >= 68 && view.width() <= 98 && view.height() >= 68 && view.height() <= 98;
                check.expect("an imported model camera shows the model as the file's camera would", placed, "x " + std::to_string(view.minX) + ".." + std::to_string(view.maxX) + ", y " + std::to_string(view.minY) + ".." + std::to_string(view.maxY));
                report["camera"]["fileCameraBox"] = {view.minX, view.maxX, view.minY, view.maxY};
            }
        } else {
            logLine("Scene self test: lights_cameras.glb is not in the model folder, the model camera check is skipped");
        }

        {
            std::vector<double> frameMs;
            for (int mode = 0; mode < 3; ++mode) {
                SceneDoc doc = base;
                doc.cutBeats = 4.0f;
                for (int i = 0; i < 7; ++i) {
                    DocCamera extra = doc.camera;
                    extra.yawDegrees = 45.0f * static_cast<float>(i + 1);
                    extra.beats = static_cast<float>(1 + i % 3);
                    extra.rollDegrees = 3.0f * static_cast<float>(i);
                    extra.move = {0.1f * static_cast<float>(i), 0.0f, 0.0f};
                    docAddCamera(doc, extra);
                }
                if (mode > 0) {
                    doc.glide = 0.6f;
                    doc.camera.bindings[0] = {true, BindSource::Bass, BindTarget::Orbit, 0.2f};
                    doc.camera.bindings[1] = {true, BindSource::Band3, BindTarget::MoveX, 0.2f};
                    doc.camera.bindings[2] = {true, BindSource::Beat, BindTarget::Roll, 0.2f};
                }
                auto player = docScene(doc, "camera cost");
                captureFrame(engine, *player, stillFrame(0.0, mode == 2), target, true);
                const auto begin = std::chrono::steady_clock::now();
                for (int i = 0; i < 60; ++i) {
                    SceneFrame frame;
                    frame.time = static_cast<float>(i) * 0.05f;
                    frame.setAudio(virtualAudio(0.2 * i, 0.05 * i), 0.2 * i);
                    frame.cameraMoves = mode != 1;
                    captureFrame(engine, *player, frame, target, true);
                }
                frameMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / 60.0);
            }
            report["camera"]["frameMs"] = {{"eightCamerasMovesOff", frameMs[1]}, {"eightCuts", frameMs[0]}, {"eightGlidingWithBindings", frameMs[2]}};
            check.expect("eight gliding cameras cost stays measured and small", frameMs[2] < frameMs[0] * 1.5 + 4.0, "ms " + number(frameMs[0]) + " / " + number(frameMs[1]) + " / " + number(frameMs[2]));
        }
        {
            SceneDoc doc = base;
            doc.cutBeats = 4.0f;
            doc.glide = 0.6f;
            doc.camera.bindings[0] = {true, BindSource::Bass, BindTarget::Orbit, 0.2f};
            doc.camera.bindings[1] = {true, BindSource::Band3, BindTarget::MoveX, 0.2f};
            doc.camera.bindings[2] = {true, BindSource::Beat, BindTarget::Roll, 0.2f};
            for (int i = 0; i < 7; ++i) {
                DocCamera extra = doc.camera;
                extra.yawDegrees = 45.0f * static_cast<float>(i + 1);
                docAddCamera(doc, extra);
            }
            const GltfResult built = buildSceneFromDoc(doc, bindingAssets());
            SceneAudio audio;
            audio.bass = 0.8f;
            audio.bands[2] = 0.5f;
            audio.beat = 0.4f;
            double best = 1e9;
            float sink = 0.0f;
            for (int round = 0; round < 5; ++round) {
                const auto begin = std::chrono::steady_clock::now();
                for (int i = 0; i < 20000; ++i) {
                    const CameraPose pose = cameraPoseAt(built.scene, 0.013 * i, 0.01f * static_cast<float>(i), audio, true);
                    const CameraView view = cameraView(pose, {}, 1.0f, 1.0f);
                    sink += view.eye.x;
                }
                best = std::min(best, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() / 20000.0);
            }
            report["camera"]["poseMicros"] = best;
            check.expect("working out the camera pose takes a few microseconds", best < 25.0 && std::isfinite(sink), "us " + number(best));
        }
    }
    {
        size_t checked = 0;
        for (const BuiltinSceneInfo& info : builtinScenes()) {
            const auto build = info.build;
            ScenePlayer player(info.name, [build](const GltfLimits&) {
                GltfResult result;
                result.ok = true;
                result.scene = build();
                return result;
            });
            const Shot a = capture(engine, player, 0.7f, target);
            const Shot b = capture(engine, player, 0.7f, target);
            for (const float time : {0.0f, 0.7f, 2.3f}) {
                report["builtinHashes"][info.name][number(time)] = pixelHash(capture(engine, player, time, target).pixels);
                SceneFrame frame;
                frame.time = time;
                frame.beat = static_cast<double>(time) * 2.0;
                report["builtinCameraHashes"][info.name][number(time)] = pixelHash(captureFrame(engine, player, frame, target, true).pixels);
            }
            check.expect(std::string("built-in scene ") + info.name + " draws the same twice", a.ok && a.lit > 300 && meanDifference(a, b) < 0.001);
            ++checked;
        }
        report["builtinScenes"] = checked;
    }
    runWorldChecks(engine, models, target, check, report);
    runGeometryChecks(engine, target, check, report);
    target.destroy();

    report["checks"] = std::move(check.items);
    report["passed"] = check.passed;
    const std::string text = report.dump(2);
    logLine("Scene self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return check.passed ? 0 : 1;
}

}

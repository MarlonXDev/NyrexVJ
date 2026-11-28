#include "app/scene_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "render/scene_player.hpp"
#include "scene3d/doc_build.hpp"
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

Shot captureFrame(Engine& engine, ScenePlayer& player, SceneFrame frame, RenderTarget& target) {
    Shot shot;
    if (!player.wait()) {
        logLine("Scene self test: '" + player.label() + "' failed to load: " + player.error());
        return shot;
    }
    frame.editor = true;
    frame.editorCamera = true;
    frame.editYaw = 0.0f;
    frame.editPitch = 0.0f;
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
            }
            check.expect(std::string("built-in scene ") + info.name + " draws the same twice", a.ok && a.lit > 300 && meanDifference(a, b) < 0.001);
            ++checked;
        }
        report["builtinScenes"] = checked;
    }
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

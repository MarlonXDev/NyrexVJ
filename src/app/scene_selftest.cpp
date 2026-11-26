#include "app/scene_selftest.hpp"

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

Shot capture(Engine& engine, ScenePlayer& player, float time, RenderTarget& target) {
    Shot shot;
    if (!player.wait()) {
        logLine("Scene self test: '" + player.label() + "' failed to load: " + player.error());
        return shot;
    }
    SceneFrame frame;
    frame.time = time;
    frame.editor = true;
    frame.editorCamera = true;
    frame.editYaw = 0.0f;
    frame.editPitch = 0.0f;
    frame.editDistance = 1.0f;
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

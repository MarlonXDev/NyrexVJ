#include "check.hpp"

#include "audio/audio_features.hpp"
#include "core/clip_pack.hpp"
#include "core/settings.hpp"
#include "scene3d/builtin_docs.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/procedural.hpp"
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
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

using namespace nvj;

bool near(float a, float b, float eps = 0.0005f) {
    return std::fabs(a - b) < eps;
}

Scene oneCube() {
    Scene scene;
    scene.materials.push_back(SceneMaterial{});
    SceneMesh mesh = primitiveMesh(PrimitiveKind::Cube);
    mesh.material = 0;
    scene.meshes.push_back(mesh);
    SceneNode node;
    node.meshes = {0};
    scene.nodes.push_back(node);
    scene.roots = {0};
    return scene;
}

SceneInstance first(const Scene& scene, const SceneAudio* audio) {
    const std::vector<SceneInstance> out = evaluateScene(scene, -1, 0.0f, audio);
    CHECK(out.size() == 1);
    return out.empty() ? SceneInstance{} : out[0];
}

bool sameBytes(const SceneInstance& a, const SceneInstance& b) {
    return std::memcmp(&a.world, &b.world, sizeof(Mat4)) == 0 && a.emissive == b.emissive && a.hue == b.hue && a.roughness == b.roughness && a.uvX == b.uvX && a.uvY == b.uvY && a.inflate == b.inflate && a.ripple == b.ripple;
}

void testBeatPulse() {
    CHECK(near(beatPulse(0.0), 1.0f));
    CHECK(near(beatPulse(3.0), 1.0f));
    CHECK(near(beatPulse(0.5), std::exp(-2.5f)));
    CHECK(near(beatPulse(7.25), std::exp(-1.25f)));
    CHECK(near(beatPulse(-0.3), std::exp(-3.5f)));
    CHECK(beatPulse(std::numeric_limits<double>::quiet_NaN()) == 0.0f);
    CHECK(beatPulse(std::numeric_limits<double>::infinity()) == 0.0f);
    CHECK(beatPulse(-std::numeric_limits<double>::infinity()) == 0.0f);
    float previous = 2.0f;
    for (int i = 0; i < 100; ++i) {
        const float value = beatPulse(0.01 * i);
        CHECK(value <= previous && value > 0.0f && value <= 1.0f);
        previous = value;
    }
    CHECK(near(beatPulse(1.0e12), 1.0f));
}

void testSpectrumBands() {
    float flat[kSpectrumBins];
    std::fill(flat, flat + kSpectrumBins, 0.5f);
    std::array<float, kSceneBands> bands = spectrumBands(flat, kSpectrumBins);
    for (float v : bands) {
        CHECK(near(v, 0.5f));
    }

    float lowOnly[kSpectrumBins] = {};
    for (int i = 0; i < 16; ++i) {
        lowOnly[i] = 1.0f;
    }
    bands = spectrumBands(lowOnly, kSpectrumBins);
    CHECK(near(bands[0], 1.0f));
    for (int i = 1; i < kSceneBands; ++i) {
        CHECK(near(bands[static_cast<size_t>(i)], 0.0f));
    }

    float highOnly[kSpectrumBins] = {};
    for (int i = 112; i < 128; ++i) {
        highOnly[i] = 0.25f;
    }
    bands = spectrumBands(highOnly, kSpectrumBins);
    CHECK(near(bands[7], 0.25f) && near(bands[6], 0.0f));

    float rough[kSpectrumBins];
    for (int i = 0; i < kSpectrumBins; ++i) {
        rough[i] = i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1 ? 7.0f : -4.0f);
    }
    bands = spectrumBands(rough, kSpectrumBins);
    for (float v : bands) {
        CHECK(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
    }

    bands = spectrumBands(nullptr, kSpectrumBins);
    for (float v : bands) {
        CHECK(v == 0.0f);
    }
    bands = spectrumBands(flat, 3);
    for (float v : bands) {
        CHECK(v == 0.0f);
    }
    bands = spectrumBands(flat, 8);
    for (float v : bands) {
        CHECK(near(v, 0.5f));
    }
    bands = spectrumBands(flat, 20);
    for (float v : bands) {
        CHECK(near(v, 0.5f));
    }
    bands = spectrumBands(flat, -5);
    for (float v : bands) {
        CHECK(v == 0.0f);
    }
}

void testSourcesReachTargets() {
    Scene scene = oneCube();
    SceneAudio audio;
    audio.bass = 0.1f;
    audio.mid = 0.2f;
    audio.high = 0.3f;
    audio.level = 0.4f;
    audio.kick = 0.5f;
    audio.beat = 0.6f;
    for (int i = 0; i < kSceneBands; ++i) {
        audio.bands[static_cast<size_t>(i)] = 0.05f * static_cast<float>(i + 1);
    }
    const float expected[kBindSourceCount] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f};
    for (int source = 0; source < kBindSourceCount; ++source) {
        scene.nodes[0].bindings = {{static_cast<BindSource>(source), BindTarget::Scale, 1.0f}};
        CHECK(near(first(scene, &audio).world.m[0], 1.0f + expected[source]));
        scene.nodes[0].bindings = {{static_cast<BindSource>(source), BindTarget::Hue, 1.0f}};
        CHECK(near(first(scene, &audio).hue, expected[source]));
    }
}

void testMaterialTargets() {
    Scene scene = oneCube();
    SceneAudio audio;
    audio.bass = 0.5f;
    audio.kick = 1.0f;
    audio.bands[2] = 0.4f;

    SceneInstance plain = first(scene, &audio);
    CHECK(plain.hue == 0.0f && plain.roughness == 0.0f && plain.uvX == 0.0f && plain.uvY == 0.0f && plain.inflate == 0.0f && plain.ripple == 0.0f);

    scene.nodes[0].bindings = {{BindSource::Bass, BindTarget::Hue, 0.5f}, {BindSource::Kick, BindTarget::Roughness, -0.25f}, {BindSource::Band3, BindTarget::UvX, 2.0f}, {BindSource::Kick, BindTarget::UvY, -1.5f}};
    SceneInstance a = first(scene, &audio);
    CHECK(near(a.hue, 0.25f) && near(a.roughness, -0.25f) && near(a.uvX, 0.8f) && near(a.uvY, -1.5f));
    CHECK(near(a.world.m[0], 1.0f) && a.emissive == 0.0f);

    scene.nodes[0].bindings = {{BindSource::Kick, BindTarget::Inflate, 0.5f}, {BindSource::Bass, BindTarget::Ripple, 1.0f}};
    SceneInstance b = first(scene, &audio);
    CHECK(near(b.inflate, 0.5f) && near(b.ripple, 0.5f));

    scene.nodes[0].bindings = {{BindSource::Kick, BindTarget::Hue, 0.5f}, {BindSource::Kick, BindTarget::Hue, 0.25f}};
    CHECK(near(first(scene, &audio).hue, 0.75f));

    scene.nodes[0].bindings = {{BindSource::Kick, BindTarget::Hue, 1000.0f}, {BindSource::Kick, BindTarget::Roughness, -1000.0f}, {BindSource::Kick, BindTarget::UvX, 1000.0f}, {BindSource::Kick, BindTarget::UvY, -1000.0f}};
    SceneInstance big = first(scene, &audio);
    CHECK(near(big.hue, kSceneMaxHueTurns) && near(big.roughness, -1.0f) && near(big.uvX, kSceneMaxUvShift) && near(big.uvY, -kSceneMaxUvShift));
    scene.nodes[0].bindings = {{BindSource::Kick, BindTarget::Inflate, 1000.0f}, {BindSource::Kick, BindTarget::Ripple, -1000.0f}};
    SceneInstance deep = first(scene, &audio);
    CHECK(near(deep.inflate, kSceneMaxDeform) && near(deep.ripple, -kSceneMaxDeform));

    SceneAudio ugly;
    ugly.bass = std::numeric_limits<float>::quiet_NaN();
    ugly.kick = std::numeric_limits<float>::infinity();
    ugly.bands[0] = -std::numeric_limits<float>::infinity();
    scene.nodes[0].bindings = {{BindSource::Bass, BindTarget::Hue, 1.0f}, {BindSource::Kick, BindTarget::Roughness, 1.0f}, {BindSource::Band1, BindTarget::UvX, 1.0f}, {BindSource::Beat, BindTarget::Inflate, 1.0f}};
    const SceneInstance safe = first(scene, &ugly);
    CHECK(std::isfinite(safe.hue) && std::isfinite(safe.roughness) && std::isfinite(safe.uvX) && std::isfinite(safe.inflate));
    CHECK(std::fabs(safe.hue) <= kSceneMaxHueTurns && std::fabs(safe.roughness) <= 1.0f && std::fabs(safe.uvX) <= kSceneMaxUvShift);
    for (float v : safe.world.m) {
        CHECK(std::isfinite(v));
    }
}

void testMaterialSwitch() {
    Scene scene = oneCube();
    scene.nodes[0].bindings = {{BindSource::Kick, BindTarget::Scale, 1.0f}, {BindSource::Kick, BindTarget::Emissive, 2.0f}, {BindSource::Kick, BindTarget::Hue, 1.0f}, {BindSource::Kick, BindTarget::Inflate, 1.0f}};
    SceneAudio audio;
    audio.kick = 1.0f;
    const SceneInstance on = first(scene, &audio);
    CHECK(near(on.hue, 1.0f) && near(on.inflate, 1.0f));
    audio.materials = false;
    const SceneInstance off = first(scene, &audio);
    CHECK(off.hue == 0.0f && off.inflate == 0.0f && off.roughness == 0.0f && off.uvX == 0.0f && off.uvY == 0.0f && off.ripple == 0.0f);
    CHECK(near(off.world.m[0], 2.0f) && near(off.emissive, 2.0f));
    CHECK(near(off.world.m[0], on.world.m[0]) && near(off.emissive, on.emissive));
    const SceneInstance none = first(scene, nullptr);
    CHECK(none.hue == 0.0f && none.inflate == 0.0f && near(none.world.m[0], 1.0f));
}

void testInheritance() {
    Scene scene = oneCube();
    scene.nodes[0].meshes.clear();
    SceneNode child;
    child.meshes = {0};
    scene.nodes.push_back(child);
    scene.nodes[0].children = {1};
    scene.nodes[0].bindings = {{BindSource::Kick, BindTarget::Hue, 0.5f}};
    scene.nodes[1].bindings = {{BindSource::Kick, BindTarget::Roughness, 0.5f}};
    SceneAudio audio;
    audio.kick = 1.0f;
    const SceneInstance inner = first(scene, &audio);
    CHECK(near(inner.roughness, 0.5f));
    CHECK(near(inner.hue, 0.5f));
    audio.materials = false;
    const SceneInstance blocked = first(scene, &audio);
    CHECK(blocked.hue == 0.0f && blocked.roughness == 0.0f);
}

void testReproducible() {
    Scene scene = oneCube();
    scene.nodes[0].bindings = {{BindSource::Band2, BindTarget::Hue, 1.5f}, {BindSource::Beat, BindTarget::Roughness, 0.4f}, {BindSource::Kick, BindTarget::Ripple, 0.7f}, {BindSource::Band7, BindTarget::UvX, 3.0f}};
    for (int step = 0; step < 40; ++step) {
        const double beat = 0.137 * step;
        const double time = 0.0711 * step;
        SceneAudio one;
        SceneAudio two;
        for (SceneAudio* target : {&one, &two}) {
            const AudioFeatures features = virtualAudio(beat, time);
            target->bass = features.bass;
            target->mid = features.mid;
            target->high = features.high;
            target->level = features.level;
            target->kick = features.kick;
            target->beat = beatPulse(beat);
            target->bands = spectrumBands(features.spectrum.data(), kSpectrumBins);
        }
        CHECK(sameBytes(first(scene, &one), first(scene, &two)));
    }
    SceneAudio early;
    SceneAudio late;
    const AudioFeatures a = virtualAudio(0.0, 0.0);
    const AudioFeatures b = virtualAudio(0.5, 1.3);
    early.bands = spectrumBands(a.spectrum.data(), kSpectrumBins);
    late.bands = spectrumBands(b.spectrum.data(), kSpectrumBins);
    early.beat = beatPulse(0.0);
    late.beat = beatPulse(0.5);
    CHECK(!sameBytes(first(scene, &early), first(scene, &late)));
}

SceneDoc docWith(std::initializer_list<DocBinding> list) {
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    DocObject o = makeDefaultObject(PrimitiveKind::Sphere);
    size_t i = 0;
    for (const DocBinding& b : list) {
        o.bindings[i++] = b;
    }
    doc.objects.push_back(o);
    return doc;
}

void testDocKeys() {
    const SceneDoc doc = docWith({{true, BindSource::Band5, BindTarget::Hue, 0.5f}, {true, BindSource::Beat, BindTarget::Roughness, -0.5f}, {true, BindSource::Band8, BindTarget::UvX, 2.0f}, {true, BindSource::High, BindTarget::Ripple, 0.25f}});
    const std::string text = docToJson(doc);
    for (const char* key : {"band5", "beat", "band8", "hue", "roughness", "uvx", "ripple"}) {
        CHECK(text.find(std::string("\"") + key + "\"") != std::string::npos);
    }
    CHECK(text.find("\"version\":2") != std::string::npos || text.find("\"version\": 2") != std::string::npos);
    SceneDoc back;
    std::string error;
    CHECK(docFromJson(text, back, error));
    CHECK(back.notes.empty());
    CHECK(docToJson(back) == text);
    CHECK(back.objects.size() == 1);
    const DocBindings& got = back.objects[0].bindings;
    CHECK(got[0].enabled && got[0].source == BindSource::Band5 && got[0].target == BindTarget::Hue && near(got[0].amount, 0.5f));
    CHECK(got[1].source == BindSource::Beat && got[1].target == BindTarget::Roughness && near(got[1].amount, -0.5f));
    CHECK(got[2].source == BindSource::Band8 && got[2].target == BindTarget::UvX);
    CHECK(got[3].source == BindSource::High && got[3].target == BindTarget::Ripple);
    for (int s = 0; s < kBindSourceCount; ++s) {
        for (int t = 0; t < kBindTargetCount - 0; ++t) {
            const BindTarget target = static_cast<BindTarget>(t);
            if (target == BindTarget::Light || target == BindTarget::Zoom || bindTargetIsCamera(target)) {
                continue;
            }
            const SceneDoc one = docWith({{true, static_cast<BindSource>(s), target, 0.5f}});
            SceneDoc again;
            CHECK(docFromJson(docToJson(one), again, error));
            CHECK(again.objects[0].bindings[0].source == static_cast<BindSource>(s) && again.objects[0].bindings[0].target == target);
        }
    }
}

void testOldFilesStayOld() {
    const SceneDoc old = docWith({{true, BindSource::Kick, BindTarget::Scale, 0.5f}, {true, BindSource::High, BindTarget::Emissive, 1.0f}});
    const std::string text = docToJson(old);
    CHECK(text.find("\"version\":1") != std::string::npos || text.find("\"version\": 1") != std::string::npos);
    CHECK(docRequiredVersion(old) == kDocBaseVersion);
    SceneDoc back;
    std::string error;
    CHECK(docFromJson(text, back, error));
    CHECK(back.notes.empty());

    CHECK(docRequiredVersion(docWith({{true, BindSource::Beat, BindTarget::Scale, 0.5f}})) == kDocBindingsVersion);
    CHECK(docRequiredVersion(docWith({{true, BindSource::Kick, BindTarget::Hue, 0.5f}})) == kDocBindingsVersion);
    CHECK(docRequiredVersion(docWith({{true, BindSource::Kick, BindTarget::Scale, 0.5f}, {true, BindSource::Kick, BindTarget::Lift, 0.5f}, {true, BindSource::Kick, BindTarget::Twist, 0.5f}})) == kDocBindingsVersion);
    CHECK(docRequiredVersion(docWith({{false, BindSource::Band3, BindTarget::Hue, 0.5f}})) == kDocBaseVersion);

    const std::string future = "{\"format\":\"nyxscene\",\"version\":5,\"name\":\"x\",\"objects\":[]}";
    CHECK(!docFromJson(future, back, error));
    CHECK(error.find("newer") != std::string::npos);

    SceneDoc withDefault;
    CHECK(docRequiredVersion(withDefault) == kDocBaseVersion);
}

void testTemplatesStayOld() {
    CHECK(!builtinDocs().empty());
    for (const BuiltinDocInfo& info : builtinDocs()) {
        const SceneDoc doc = info.make();
        const std::string text = docToJson(doc);
        CHECK(docRequiredVersion(doc) == kDocBaseVersion);
        CHECK(text.find("\"version\":1") != std::string::npos || text.find("\"version\": 1") != std::string::npos);
        SceneDoc back;
        std::string error;
        CHECK(docFromJson(text, back, error));
        CHECK(back.notes.empty());
        CHECK(docToJson(back) == text);
    }
}

std::string objectJson(const std::string& bindings) {
    return "{\"format\":\"nyxscene\",\"version\":2,\"name\":\"x\",\"objects\":[{\"name\":\"Ball\",\"kind\":\"sphere\",\"bindings\":" + bindings + "}]}";
}

void testUnknownKeys() {
    SceneDoc doc;
    std::string error;
    CHECK(docFromJson(objectJson("[{\"source\":\"bass\",\"target\":\"hue\",\"amount\":1},{\"source\":\"theremin\",\"target\":\"scale\",\"amount\":1},{\"source\":\"kick\",\"target\":\"sparkle\",\"amount\":1},{\"target\":\"scale\",\"amount\":1},{\"source\":\"kick\",\"amount\":1},7,\"text\",null,{\"source\":\"band9\",\"target\":\"scale\"}]"), doc, error));
    CHECK(doc.objects.size() == 1);
    const DocBindings& got = doc.objects[0].bindings;
    CHECK(got[0].enabled && got[0].source == BindSource::Bass && got[0].target == BindTarget::Hue);
    CHECK(!got[1].enabled && !got[2].enabled && !got[3].enabled);
    CHECK(doc.notes.size() == 8);
    bool sawSource = false;
    bool sawTarget = false;
    for (const std::string& note : doc.notes) {
        CHECK(note.find("object 'Ball'") != std::string::npos);
        sawSource = sawSource || note.find("unknown source 'theremin'") != std::string::npos;
        sawTarget = sawTarget || note.find("unknown target 'sparkle'") != std::string::npos;
    }
    CHECK(sawSource && sawTarget);

    CHECK(docFromJson(objectJson("[{\"source\":\"kick\",\"target\":\"scale\",\"amount\":1},{\"source\":\"kick\",\"target\":\"lift\",\"amount\":1},{\"source\":\"kick\",\"target\":\"twist\",\"amount\":1},{\"source\":\"kick\",\"target\":\"emissive\",\"amount\":1},{\"source\":\"kick\",\"target\":\"hue\",\"amount\":1},{\"source\":\"kick\",\"target\":\"ripple\",\"amount\":1}]"), doc, error));
    CHECK(doc.objects[0].bindings[3].target == BindTarget::Emissive);
    CHECK(doc.notes.size() == 1 && doc.notes[0].find("more than 4") != std::string::npos);

    CHECK(docFromJson(objectJson("{\"not\":\"an array\"}"), doc, error));
    CHECK(doc.notes.empty() && !doc.objects[0].bindings[0].enabled);
    CHECK(docFromJson(objectJson("[]"), doc, error));
    CHECK(doc.notes.empty());

    std::string nasty = "[{\"source\":\"";
    nasty += std::string(5000, 'x');
    nasty += "\\u0007\\u001b[31m\",\"target\":\"scale\"}]";
    CHECK(docFromJson(objectJson(nasty), doc, error));
    CHECK(doc.notes.size() == 1 && doc.notes[0].size() < 200);
    for (const char c : doc.notes[0]) {
        CHECK(static_cast<unsigned char>(c) >= 0x20 && c != 0x7f);
    }

    std::string many = "[";
    for (int i = 0; i < 200; ++i) {
        many += std::string(i == 0 ? "" : ",") + "{\"source\":\"nope" + std::to_string(i) + "\",\"target\":\"scale\"}";
    }
    many += "]";
    CHECK(docFromJson(objectJson(many), doc, error));
    CHECK(doc.notes.size() <= kDocMaxNotes + 1);
}

void testOwnerCoercion() {
    SceneDoc doc;
    std::string error;
    const std::string text = "{\"format\":\"nyxscene\",\"version\":2,\"name\":\"x\",\"objects\":[{\"name\":\"Ball\",\"kind\":\"sphere\",\"bindings\":[{\"source\":\"kick\",\"target\":\"light\",\"amount\":1},{\"source\":\"kick\",\"target\":\"zoom\",\"amount\":1}]}],"
                             "\"lights\":[{\"name\":\"Lamp\",\"bindings\":[{\"source\":\"band2\",\"target\":\"hue\",\"amount\":1}]}],\"camera\":{\"bindings\":[{\"source\":\"beat\",\"target\":\"scale\",\"amount\":1}]}}";
    CHECK(docFromJson(text, doc, error));
    CHECK(doc.objects[0].bindings[0].target == BindTarget::Scale);
    CHECK(doc.objects[0].bindings[1].target == BindTarget::Scale);
    if (!doc.lights.empty()) {
        CHECK(doc.lights[0].bindings[0].target == BindTarget::Light && doc.lights[0].bindings[0].source == BindSource::Band2);
    }
    CHECK(doc.camera.bindings[0].target == BindTarget::Zoom && doc.camera.bindings[0].source == BindSource::Beat);
    CHECK(doc.notes.size() >= 3);

    SceneDoc light = makeDefaultDoc();
    light.lights.clear();
    DocLight lamp = makeDefaultLight(true);
    lamp.bindings[0] = {true, BindSource::Band1, BindTarget::Hue, 1.0f};
    light.lights.push_back(lamp);
    SceneDoc round;
    CHECK(docFromJson(docToJson(light), round, error));
    CHECK(round.lights.size() == 1 && round.lights[0].bindings[0].target == BindTarget::Light && round.lights[0].bindings[0].source == BindSource::Band1);
}

void testBuildCarriesBindings() {
    SceneDoc doc = docWith({{true, BindSource::Band4, BindTarget::Hue, 0.5f}, {true, BindSource::Kick, BindTarget::Inflate, 0.25f}, {false, BindSource::Bass, BindTarget::Roughness, 1.0f}, {true, BindSource::Beat, BindTarget::UvY, 0.0f}});
    GltfResult built = buildSceneFromDoc(doc, DocAssets{});
    CHECK(built.ok);
    size_t found = 0;
    for (const SceneNode& node : built.scene.nodes) {
        if (!node.bindings.empty()) {
            CHECK(node.bindings.size() == 2);
            CHECK(node.bindings[0].source == BindSource::Band4 && node.bindings[0].target == BindTarget::Hue);
            CHECK(node.bindings[1].source == BindSource::Kick && node.bindings[1].target == BindTarget::Inflate);
            ++found;
        }
    }
    CHECK(found == 1);

    SceneDoc withNotes = doc;
    withNotes.notes = {"object 'Ball', binding 2 uses the unknown source 'x' and was ignored"};
    built = buildSceneFromDoc(withNotes, DocAssets{});
    CHECK(built.ok);
    bool warned = false;
    for (const std::string& warning : built.warnings) {
        warned = warned || warning.find("unknown source 'x'") != std::string::npos;
    }
    CHECK(warned);
}

void testPackRoundTrip() {
    const SceneDoc doc = docWith({{true, BindSource::Band6, BindTarget::Ripple, 0.5f}, {true, BindSource::Beat, BindTarget::Hue, 2.0f}});
    ScenePack pack;
    pack.doc = doc;
    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(writeScenePack(pack, bytes, error));
    ScenePack back;
    CHECK(readScenePack(bytes.data(), bytes.size(), back, error));
    CHECK(docToJson(back.doc) == docToJson(doc));
    CHECK(back.doc.objects[0].bindings[0].source == BindSource::Band6 && back.doc.objects[0].bindings[1].target == BindTarget::Hue);
    std::vector<uint8_t> again;
    CHECK(writeScenePack(back, again, error));
    CHECK(again == bytes);
}

void testVirtualAudioBands() {
    const AudioFeatures a = virtualAudio(1.25, 3.5);
    const AudioFeatures b = virtualAudio(1.25, 3.5);
    CHECK(a.spectrum == b.spectrum);
    const std::array<float, kSceneBands> bandsA = spectrumBands(a.spectrum.data(), kSpectrumBins);
    const std::array<float, kSceneBands> bandsB = spectrumBands(b.spectrum.data(), kSpectrumBins);
    CHECK(bandsA == bandsB);
    float sum = 0.0f;
    float spread = 0.0f;
    for (float v : bandsA) {
        CHECK(v >= 0.0f && v <= 1.0f);
        sum += v;
    }
    for (int i = 1; i < kSceneBands; ++i) {
        spread += std::fabs(bandsA[static_cast<size_t>(i)] - bandsA[static_cast<size_t>(i - 1)]);
    }
    CHECK(sum > 0.5f && spread > 0.05f);
}

void testSetting() {
    namespace fs = std::filesystem;
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_matbind_settings";
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
    CHECK(defaults.sceneMaterialBindings);
    CHECK(saveSettings(defaults));
    CHECK(!nlohmann::json::parse(readText()).contains("sceneMaterialBindings"));
    Settings off;
    off.sceneMaterialBindings = false;
    CHECK(saveSettings(off));
    const nlohmann::json written = nlohmann::json::parse(readText());
    CHECK(written.contains("sceneMaterialBindings") && written["sceneMaterialBindings"] == false);
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(!loaded.sceneMaterialBindings);
    CHECK(loadWith("{}").sceneMaterialBindings);
    CHECK(loadWith("{\"sceneMaterialBindings\": true}").sceneMaterialBindings);
    CHECK(!loadWith("{\"sceneMaterialBindings\": false}").sceneMaterialBindings);
    CHECK(loadWith("{\"sceneMaterialBindings\": \"no\"}").sceneMaterialBindings);
    CHECK(loadWith("{\"sceneMaterialBindings\": 0}").sceneMaterialBindings);
    CHECK(loadWith("{\"sceneMaterialBindings\": null}").sceneMaterialBindings);
    CHECK(loadWith("{\"graphOptimizer\": false}").sceneMaterialBindings);
    fs::remove_all(scratch, ec);
}

}

int main() {
    testSetting();
    testBeatPulse();
    testSpectrumBands();
    testSourcesReachTargets();
    testMaterialTargets();
    testMaterialSwitch();
    testInheritance();
    testReproducible();
    testDocKeys();
    testOldFilesStayOld();
    testTemplatesStayOld();
    testUnknownKeys();
    testOwnerCoercion();
    testBuildCarriesBindings();
    testPackRoundTrip();
    testVirtualAudioBands();
    return testing::finish();
}

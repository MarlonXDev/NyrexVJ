#include <algorithm>
#include "render/clip_library.hpp"
#include "core/composition.hpp"
#include "core/edit_history.hpp"
#include "core/frame_pacer.hpp"
#include "platform/platform.hpp"
#include "core/frame_stats.hpp"
#include "core/modulation.hpp"
#include "core/novelty.hpp"
#include "render/effect_library.hpp"
#include "core/preset.hpp"
#include "core/recovery.hpp"
#include "core/render_policy.hpp"
#include "core/settings.hpp"
#include "core/sha256.hpp"
#include "update/update_logic.hpp"
#include "update/updater.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NVJ_TEST_ASSET "NyrexVJ.exe"
#else
#define NVJ_TEST_ASSET "NyrexVJ-linux-x64"
#endif

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, const char* expression, int line) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAILED line %d: %s\n", line, expression);
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void testSha256() {
    CHECK(nvj::sha256Hex(std::string()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(nvj::sha256Hex(std::string("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(nvj::sha256Hex(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::string million(1000000, 'a');
    CHECK(nvj::sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    nvj::Sha256 streaming;
    streaming.update(reinterpret_cast<const unsigned char*>("a"), 1);
    streaming.update(reinterpret_cast<const unsigned char*>("bc"), 2);
    CHECK(streaming.finalHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void testVersions() {
    nvj::Version a = nvj::parseVersion("v0.2.0");
    nvj::Version b = nvj::parseVersion("0.2.0-dev");
    nvj::Version c = nvj::parseVersion("0.10.1");
    nvj::Version d = nvj::parseVersion("1.0");
    CHECK(a.valid && a.major == 0 && a.minor == 2 && a.patch == 0 && !a.dev);
    CHECK(b.valid && b.dev);
    CHECK(c.valid && c.minor == 10 && c.patch == 1);
    CHECK(d.valid && d.major == 1 && d.minor == 0 && d.patch == 0);
    CHECK(!nvj::parseVersion("nightly").valid);
    CHECK(!nvj::parseVersion("").valid);
    CHECK(nvj::compareVersions(b, a) < 0);
    CHECK(nvj::compareVersions(a, b) > 0);
    CHECK(nvj::compareVersions(a, a) == 0);
    CHECK(nvj::compareVersions(a, c) < 0);
    CHECK(nvj::compareVersions(c, d) < 0);
    CHECK(nvj::versionToString(b) == "0.2.0-dev");
    CHECK(nvj::parseBuildNumber("Nightly build 57 (abc1234)") == 57);
    CHECK(nvj::parseBuildNumber("Build #12") == 12);
    CHECK(nvj::parseBuildNumber("no number here") == 0);
}

void testProgramDetection() {
    std::vector<unsigned char> data(1024, 0);
    CHECK(!nvj::looksLikeProgram(data));
    CHECK(!nvj::looksLikeProgram({}));
#ifdef _WIN32
    data[0] = 'M';
    data[1] = 'Z';
    CHECK(nvj::looksLikeProgram(data));
    data[0] = 0x7F;
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    CHECK(!nvj::looksLikeProgram(data));
#else
    data[0] = 0x7F;
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    CHECK(nvj::looksLikeProgram(data));
    data[0] = 'M';
    data[1] = 'Z';
    CHECK(!nvj::looksLikeProgram(data));
    CHECK(!nvj::looksLikeProgram(std::vector<unsigned char>{0x7F, 'E', 'L'}));
#endif
    CHECK(std::string(nvj::kWindowsAssetName) == "NyrexVJ.exe");
    CHECK(std::string(nvj::kLinuxAssetName) == "NyrexVJ-linux-x64");
}

void testReleaseParsing() {
    const char* json = R"({
        "tag_name": "v0.3.0",
        "name": "NyrexVJ 0.3.0",
        "body": "Notes",
        "html_url": "https://github.com/MarlonXDev/NyrexVJ/releases/tag/v0.3.0",
        "draft": false,
        "prerelease": false,
        "assets": [
            {"name": ")" NVJ_TEST_ASSET R"(", "browser_download_url": "https://example.test/)" NVJ_TEST_ASSET R"("},
            {"name": "SHA256SUMS.txt", "browser_download_url": "https://example.test/SHA256SUMS.txt"},
            {"name": "other.zip", "browser_download_url": "https://example.test/other.zip"}
        ]
    })";
    nvj::ReleaseInfo release;
    std::string error;
    CHECK(nvj::parseRelease(json, release, error));
    CHECK(release.tag == "v0.3.0");
    CHECK(release.exeUrl == "https://example.test/" NVJ_TEST_ASSET);
    CHECK(release.sumsUrl == "https://example.test/SHA256SUMS.txt");
    CHECK(release.version.valid && release.version.minor == 3);

    nvj::CurrentBuild current;
    current.version = nvj::parseVersion("0.2.0");
    CHECK(nvj::isUpdateAvailable(release, nvj::UpdateChannel::Stable, current));
    current.version = nvj::parseVersion("0.3.0");
    CHECK(!nvj::isUpdateAvailable(release, nvj::UpdateChannel::Stable, current));
    current.version = nvj::parseVersion("0.3.0-dev");
    CHECK(nvj::isUpdateAvailable(release, nvj::UpdateChannel::Stable, current));

    const char* nightly = R"json({"tag_name":"nightly","name":"Nightly build 58 (abc1234)","prerelease":true,
        "assets":[{"name":")json" NVJ_TEST_ASSET R"json(","browser_download_url":"https://example.test/n.exe"}]})json";
    nvj::ReleaseInfo night;
    CHECK(nvj::parseRelease(nightly, night, error));
    CHECK(night.buildNumber == 58);
    nvj::CurrentBuild local;
    local.buildNumber = 57;
    CHECK(nvj::isUpdateAvailable(night, nvj::UpdateChannel::Nightly, local));
    local.buildNumber = 58;
    CHECK(!nvj::isUpdateAvailable(night, nvj::UpdateChannel::Nightly, local));

    nvj::ReleaseInfo bad;
    CHECK(!nvj::parseRelease("not json", bad, error));
    CHECK(!nvj::parseRelease("{\"message\":\"Not Found\"}", bad, error));
    CHECK(!nvj::parseRelease("[]", bad, error));

    nvj::ReleaseInfo noAsset;
    CHECK(nvj::parseRelease("{\"tag_name\":\"v9.0.0\"}", noAsset, error));
    CHECK(!nvj::isUpdateAvailable(noAsset, nvj::UpdateChannel::Stable, current));
}

void testHashFile() {
    const std::string hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    std::string found;
    CHECK(nvj::findHash(hash + "  " NVJ_TEST_ASSET "\r\n", NVJ_TEST_ASSET, found) && found == hash);
    CHECK(nvj::findHash("deadbeef  nope.txt\n" + hash + " *" NVJ_TEST_ASSET "\n", NVJ_TEST_ASSET, found) && found == hash);
    std::string upper;
    for (char c : hash) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    CHECK(nvj::findHash(upper + "  " NVJ_TEST_ASSET, NVJ_TEST_ASSET, found) && found == hash);
    CHECK(!nvj::findHash(hash + "  other.exe\n", NVJ_TEST_ASSET, found));
    CHECK(!nvj::findHash("", NVJ_TEST_ASSET, found));
}

void testLibraries() {
    const auto& clips = nvj::clipLibrary();
    const auto& effects = nvj::effectLibrary();
    CHECK(clips.size() >= 16);
    CHECK(effects.size() >= 20);
    std::set<std::string> clipNames;
    for (const auto& clip : clips) {
        CHECK((clip.isScene() || clip.isMedia()) ? clip.body.empty() : !clip.body.empty());
        CHECK(!clip.isScene() || clip.is3d);
        clipNames.insert(clip.name);
    }
    CHECK(clipNames.size() == clips.size());
    for (int slot = 0; slot < nvj::kSpoutInputSlots; ++slot) {
        const int index = nvj::clipIndexByName("Spout Input " + std::to_string(slot + 1));
        CHECK(index >= 0);
        if (index >= 0) {
            const nvj::ClipDef& clip = clips[static_cast<size_t>(index)];
            CHECK(clip.kind == nvj::ClipKind::SpoutSource);
            CHECK(clip.sceneIndex == slot);
            CHECK(!clip.isScene());
            CHECK(!clip.is3d);
            CHECK(!clip.body.empty());
        }
    }
    CHECK(nvj::clipIndexByName("Spout Input 5") == -1);
    for (int slot = 0; slot < nvj::kLiveInputSlots; ++slot) {
        const struct {
            const char* prefix;
            nvj::ClipKind kind;
        } live[] = {{"NDI Input ", nvj::ClipKind::NdiSource}, {"Camera ", nvj::ClipKind::CameraSource}};
        for (const auto& entry : live) {
            const int index = nvj::clipIndexByName(entry.prefix + std::to_string(slot + 1));
            CHECK(index >= 0);
            if (index >= 0) {
                const nvj::ClipDef& clip = clips[static_cast<size_t>(index)];
                CHECK(clip.kind == entry.kind);
                CHECK(clip.sceneIndex == slot);
                CHECK(clip.isLive());
                CHECK(!clip.isScene() && !clip.isMedia() && !clip.is3d);
                CHECK(!clip.body.empty());
            }
        }
    }
    CHECK(nvj::clipIndexByName("NDI Input 5") == -1);
    CHECK(nvj::clipIndexByName("Camera 5") == -1);
    std::set<std::string> effectNames;
    for (const auto& effect : effects) {
        CHECK(effect.paramCount() >= 1);
        CHECK(effect.body.find("vec4 effect(") != std::string::npos);
        effectNames.insert(effect.name);
    }
    CHECK(effectNames.size() == effects.size());
    CHECK(nvj::clipIndexByName("plasma") == 0);
    CHECK(nvj::clipIndexByName("does not exist") == -1);
    CHECK(nvj::effectIndexByName("glow") >= 0);
    CHECK(nvj::effectIndexByName("Tunnel Map") >= 0);
    nvj::EffectSlot slot;
    nvj::setEffect(slot, nvj::effectIndexByName("Trails"));
    CHECK(slot.effect >= 0 && slot.enabled);
    nvj::setEffect(slot, -1);
    CHECK(slot.effect == -1);
}

bool sameComposition(const nvj::Composition& a, const nvj::Composition& b) {
    return nvj::compositionToJson(a) == nvj::compositionToJson(b);
}

void testPresets() {
    const auto& presets = nvj::factoryPresets();
    CHECK(presets.size() >= 6);
    std::set<std::string> names;
    for (const nvj::Composition& preset : presets) {
        names.insert(preset.name);
        std::vector<std::string> warnings;
        nvj::Composition loaded;
        std::string error;
        CHECK(nvj::compositionFromJson(nvj::compositionToJson(preset), loaded, error, &warnings));
        CHECK(warnings.empty());
        CHECK(sameComposition(preset, loaded));
        bool anyActive = false;
        for (const nvj::Layer& layer : preset.layers) {
            for (int cell : layer.cells) {
                CHECK(cell >= 0);
            }
            anyActive = anyActive || layer.activeColumn >= 0;
        }
        CHECK(anyActive);
        for (const nvj::EffectSlot& slot : preset.master.fx) {
            for (float value : slot.params) {
                CHECK(value >= 0.0f && value <= 1.0f);
            }
        }
    }
    CHECK(names.size() == presets.size());
    CHECK(nvj::factoryPresetIndex("Neon Club") == 0);
    CHECK(nvj::factoryPresetIndex("Nope") == -1);

    nvj::Composition broken;
    std::string error;
    std::vector<std::string> warnings;
    const char* text = R"({"layers":[{"cells":["Plasma","Nonexistent",null],"active":1,"opacity":5,"blend":"Add","fx":[{"effect":"Bogus"},{"effect":"Glow","params":[2,-1,0.5]}]}],"bpm":9999})";
    CHECK(nvj::compositionFromJson(text, broken, error, &warnings));
    CHECK(warnings.size() == 2);
    CHECK(broken.bpm == 240.0f);
    CHECK(broken.layers[0].opacity == 1.0f);
    CHECK(broken.layers[0].blend == nvj::BlendAdd);
    CHECK(broken.layers[0].activeColumn == -1);
    CHECK(broken.layers[0].fx[0].effect == -1);
    CHECK(broken.layers[0].fx[1].effect >= 0);
    CHECK(broken.layers[0].fx[1].params[0] == 1.0f);
    CHECK(broken.layers[0].fx[1].params[1] == 0.0f);
    CHECK(!nvj::compositionFromJson("garbage", broken, error));
    CHECK(!nvj::compositionFromJson("{}", broken, error));
}

void testPlaybackPersistence() {
    nvj::Composition comp = nvj::factoryPresets()[0];
    const std::string plain = nvj::compositionToJson(comp);
    CHECK(plain.find("\"playback\"") == std::string::npos);

    nvj::ClipPlayback range;
    range.in = 1.5;
    range.out = 4.25;
    range.loop = nvj::LoopMode::PingPong;
    range.speed = -2.0f;
    range.syncBeats = 4.0f;
    comp.layers[1].playback[2] = range;
    comp.layers[3].playback[7].loop = nvj::LoopMode::OnceStop;

    const std::string text = nvj::compositionToJson(comp);
    CHECK(text.find("\"playback\"") != std::string::npos);
    nvj::Composition loaded;
    std::string error;
    std::vector<std::string> warnings;
    CHECK(nvj::compositionFromJson(text, loaded, error, &warnings));
    CHECK(warnings.empty());
    CHECK(loaded.layers[1].playback[2] == range);
    CHECK(loaded.layers[3].playback[7].loop == nvj::LoopMode::OnceStop);
    CHECK(loaded.layers[0].playback[0].isDefault());
    CHECK(loaded.layers[1].playback[3].isDefault());
    CHECK(sameComposition(comp, loaded));

    const char* hostile = R"({"layers":[{"cells":["Plasma"],"playback":[{"in":-5,"out":1e30,"loop":"Bogus","speed":99,"beats":-3},"text",null,7]}]})";
    nvj::Composition clamped;
    CHECK(nvj::compositionFromJson(hostile, clamped, error, &warnings));
    const nvj::ClipPlayback& safe = clamped.layers[0].playback[0];
    CHECK(safe.in == 0.0);
    CHECK(safe.out <= 1.0e7);
    CHECK(safe.loop == nvj::LoopMode::Loop);
    CHECK(safe.speed == nvj::kMaxPlaySpeed);
    CHECK(safe.syncBeats == 0.0f);
    CHECK(clamped.layers[0].playback[1].isDefault());
    CHECK(clamped.layers[0].playback[2].isDefault());
    CHECK(clamped.layers[0].playback[3].isDefault());

    const char* older = R"({"layers":[{"cells":["Plasma","Plasma"],"active":0}]})";
    nvj::Composition legacy;
    CHECK(nvj::compositionFromJson(older, legacy, error, &warnings));
    for (const nvj::ClipPlayback& item : legacy.layers[0].playback) {
        CHECK(item.isDefault());
    }
}

void testDecks() {
    nvj::Composition comp = nvj::factoryPresets()[0];
    CHECK(comp.activeDeck == 0);
    CHECK(nvj::usedDeckCount(comp) == 1);
    CHECK(!nvj::selectDeck(comp, 0));
    CHECK(!nvj::selectDeck(comp, nvj::kDeckCount));
    CHECK(!nvj::selectDeck(comp, -1));
    comp.layers[0].opacity = 0.37f;
    comp.master.level = 0.5f;
    CHECK(nvj::selectDeck(comp, 3));
    CHECK(comp.activeDeck == 3 && nvj::usedDeckCount(comp) == 2);
    CHECK(comp.layers[0].activeColumn == -1);
    CHECK(comp.master.level == 0.5f);
    comp.layers[1].opacity = 0.2f;
    comp.deckNames[3] = "Second";
    CHECK(nvj::deckDisplayName(comp, 3) == "Second");
    CHECK(nvj::deckDisplayName(comp, 5) == "Deck 6");
    CHECK(nvj::selectDeck(comp, 0));
    CHECK(comp.layers[0].opacity == 0.37f);
    CHECK(nvj::nextUsedDeck(comp, 0, 1) == 3);
    CHECK(nvj::nextUsedDeck(comp, 3, 1) == 0);
    CHECK(nvj::nextUsedDeck(comp, 0, -1) == 3);

    nvj::Composition loaded;
    std::string error;
    std::vector<std::string> warnings;
    CHECK(nvj::compositionFromJson(nvj::compositionToJson(comp), loaded, error, &warnings));
    CHECK(warnings.empty());
    CHECK(sameComposition(comp, loaded));
    CHECK(loaded.activeDeck == 0 && loaded.deckUsed[3] && !loaded.deckUsed[4]);
    CHECK(loaded.decks[3].layers[1].opacity == 0.2f);
    CHECK(loaded.deckNames[3] == "Second");

    nvj::DeckSettings settings;
    settings.seconds = 2.0f;
    CHECK(nvj::transitionSeconds(settings, 120.0f) == 2.0f);
    settings.syncBeats = true;
    settings.beats = 4;
    CHECK(std::fabs(nvj::transitionSeconds(settings, 120.0f) - 2.0f) < 1e-4f);
    CHECK(std::fabs(nvj::transitionSeconds(settings, 60.0f) - 4.0f) < 1e-4f);

    comp.deckSettings.transition = nvj::TransitionRadial;
    comp.deckSettings.seconds = 3.5f;
    comp.deckSettings.syncBeats = true;
    comp.deckSettings.beats = 8;
    comp.deckSettings.autoCycle = true;
    comp.deckSettings.cycleBeats = 32;
    CHECK(nvj::compositionFromJson(nvj::compositionToJson(comp), loaded, error));
    CHECK(loaded.deckSettings.transition == nvj::TransitionRadial);
    CHECK(loaded.deckSettings.seconds == 3.5f && loaded.deckSettings.syncBeats && loaded.deckSettings.beats == 8);
    CHECK(loaded.deckSettings.autoCycle && loaded.deckSettings.cycleBeats == 32);

    comp.activeDeck = 99;
    comp.deckSettings.transition = 99;
    comp.deckSettings.seconds = 99.0f;
    comp.deckSettings.beats = 0;
    comp.deckSettings.cycleBeats = 1;
    nvj::normalizeDecks(comp);
    CHECK(comp.activeDeck == nvj::kDeckCount - 1 && comp.deckUsed[static_cast<size_t>(nvj::kDeckCount - 1)]);
    CHECK(comp.deckSettings.transition == nvj::TransitionTypeCount - 1);
    CHECK(comp.deckSettings.seconds == 10.0f && comp.deckSettings.beats == 1 && comp.deckSettings.cycleBeats == 4);

    for (int t = 0; t < nvj::TransitionTypeCount; ++t) {
        CHECK(nvj::transitionFromName(nvj::transitionName(t)) == t);
    }
    CHECK(nvj::transitionFromName("Unknown") == nvj::TransitionFade);
    CHECK(nvj::reactModeFromName("Gate") == nvj::ReactGate);
    CHECK(nvj::reactModeFromName("Unknown") == nvj::ReactParameter);

    nvj::Composition gate = nvj::factoryPresets()[0];
    gate.master.fx[2].reactBand = nvj::BandKick;
    gate.master.fx[2].reactMode = nvj::ReactGate;
    gate.master.fx[2].reactAmount = 0.7f;
    CHECK(nvj::compositionFromJson(nvj::compositionToJson(gate), loaded, error));
    CHECK(loaded.master.fx[2].reactMode == nvj::ReactGate && loaded.master.fx[2].reactBand == nvj::BandKick);
    CHECK(sameComposition(gate, loaded));

    const std::vector<int>& bank = nvj::factoryBank();
    CHECK(bank.size() == static_cast<size_t>(nvj::kDeckCount));
    std::set<int> unique(bank.begin(), bank.end());
    CHECK(unique.size() == bank.size());
    for (int index : bank) {
        CHECK(index >= 0 && index < static_cast<int>(nvj::factoryPresets().size()));
    }
    CHECK(nvj::factoryPresetIndex("Milky Red") >= 0);
    CHECK(nvj::factoryPresetIndex("Crystal Cavern") >= 0);
    CHECK(nvj::factoryPresetIndex("Crystal Lines") >= 0);
    CHECK(nvj::clipIndexByName("Milky Wave") >= 0);
    CHECK(nvj::effectIndexByName("Line Art") >= 0);
    CHECK(nvj::effectIndexByName("Mirror Fold") >= 0);
}

void testPresetFiles() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_test_data";
    std::filesystem::remove_all(dir);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    CHECK(nvj::sanitizePresetName("  a/b:c*?.  ") == "a_b_c__");
    CHECK(nvj::sanitizePresetName("   ").empty());
    CHECK(nvj::sanitizePresetName(std::string(200, 'x')).size() == 64);

    nvj::Composition comp = nvj::defaultComposition();
    std::string error;
    CHECK(nvj::listUserPresets().empty());
    CHECK(nvj::saveUserPreset(comp, "My Look", error));
    CHECK(nvj::saveUserPreset(comp, "Another", error));
    CHECK(!nvj::saveUserPreset(comp, "   ", error));
    std::vector<std::string> names = nvj::listUserPresets();
    CHECK(names.size() >= 2);
    CHECK(names.front() == "Another");
    nvj::Composition loaded;
    CHECK(nvj::loadUserPreset("My Look", loaded, error));
    CHECK(loaded.name == "My Look");
    CHECK(loaded.layers[0].activeColumn == comp.layers[0].activeColumn);
    CHECK(!nvj::loadUserPreset("Missing", loaded, error));
    CHECK(nvj::deleteUserPreset("My Look"));
    CHECK(!nvj::deleteUserPreset("My Look"));

    CHECK(!nvj::loadSession(loaded));
    CHECK(nvj::saveSession(comp));
    CHECK(nvj::loadSession(loaded));
    CHECK(sameComposition(comp, loaded));

    nvj::Settings settings;
    CHECK(!nvj::loadSettings(settings));
    settings.checkUpdates = false;
    settings.channel = nvj::UpdateChannel::Stable;
    settings.outputs[0].monitor = 2;
    settings.autoInstall = false;
    settings.vsync = false;
    settings.openOutputOnStart = true;
    settings.showStatusBar = false;
    settings.uiScale = 1.25f;
    settings.fpsLimit = 90;
    settings.videoDecoder = "hardware";
    settings.hapDirect = false;
    settings.videoGpuConvert = false;
    settings.convertMode = "automatic";
    settings.convertFormat = "hap_q";
    settings.convertCacheMb = 2048;
    settings.ffmpegHintDismissed = true;
    CHECK(nvj::saveSettings(settings));
    nvj::Settings back;
    CHECK(nvj::loadSettings(back));
    CHECK(back.convertMode == "automatic" && back.convertFormat == "hap_q" && back.convertCacheMb == 2048 && back.ffmpegHintDismissed);
    CHECK(!back.checkUpdates && back.channel == nvj::UpdateChannel::Stable && back.outputs[0].monitor == 2);
    CHECK(!back.autoInstall && !back.vsync && back.openOutputOnStart && !back.showStatusBar && back.uiScale == 1.25f);
    CHECK(back.fpsLimit == 90 && back.videoDecoder == "hardware" && !back.hapDirect && !back.videoGpuConvert);

    nvj::Settings defaults;
    CHECK(defaults.fpsLimit == 60 && defaults.videoDecoder == "auto" && defaults.hapDirect && defaults.videoGpuConvert);
    CHECK(defaults.convertMode == "ask" && defaults.convertFormat == "auto" && defaults.convertCacheMb == 4096 && !defaults.ffmpegHintDismissed);
    settings.videoDecoder = "nonsense";
    settings.fpsLimit = 77;
    settings.convertMode = "sometimes";
    settings.convertFormat = "dxv";
    settings.convertCacheMb = 5;
    CHECK(nvj::saveSettings(settings));
    nvj::Settings repaired;
    CHECK(nvj::loadSettings(repaired));
    CHECK(repaired.videoDecoder == "auto" && repaired.fpsLimit == 90);
    CHECK(repaired.convertMode == "ask" && repaired.convertFormat == "auto" && repaired.convertCacheMb == 256);

    CHECK(defaults.design == 1 && defaults.theme == "studio");
    {
        std::ofstream legacy(dir / "settings.json", std::ios::binary | std::ios::trunc);
        legacy << R"({"theme":"orange","language":"de"})";
    }
    nvj::Settings legacySettings;
    CHECK(nvj::loadSettings(legacySettings));
    CHECK(legacySettings.design == 0 && legacySettings.theme == "orange" && legacySettings.language == "de");
    settings.design = 1;
    settings.theme = "studio";
    CHECK(nvj::saveSettings(settings));
    nvj::Settings designBack;
    CHECK(nvj::loadSettings(designBack));
    CHECK(designBack.design == 1 && designBack.theme == "studio");

    std::filesystem::remove_all(dir);
}

bool waitIdle(nvj::Updater& updater) {
    for (int i = 0; i < 1000; ++i) {
        if (!updater.busy()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

std::vector<unsigned char> fakeBinary(size_t size, unsigned char fill) {
    std::vector<unsigned char> data(size, fill);
#ifdef _WIN32
    data[0] = 'M';
    data[1] = 'Z';
#else
    data[0] = 0x7F;
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
#endif
    return data;
}

struct FakeServer {
    std::string releaseJson;
    std::vector<unsigned char> exe;
    std::string sums;
    int apiStatus = 200;
    bool replaced = false;
    std::vector<unsigned char> installed;
};

nvj::UpdaterBackend makeBackend(FakeServer* server) {
    nvj::UpdaterBackend backend;
    backend.get = [server](const std::string& url, nvj::platform::HttpResult& result, const nvj::platform::ProgressFn& progress) {
        result = nvj::platform::HttpResult{};
        if (url.find("api.github.com") != std::string::npos) {
            if (server->apiStatus != 200) {
                result.status = server->apiStatus;
                result.error = "HTTP " + std::to_string(server->apiStatus);
                return false;
            }
            result.body.assign(server->releaseJson.begin(), server->releaseJson.end());
        } else if (url == "https://example.test/SHA256SUMS.txt") {
            result.body.assign(server->sums.begin(), server->sums.end());
        } else if (url == "https://example.test/" NVJ_TEST_ASSET) {
            if (progress) {
                progress(server->exe.size() / 2, server->exe.size());
            }
            result.body = server->exe;
        } else {
            result.status = 404;
            result.error = "HTTP 404";
            return false;
        }
        result.status = 200;
        return true;
    };
    backend.replace = [server](const std::vector<unsigned char>& data, std::string&) {
        server->installed = data;
        server->replaced = true;
        return true;
    };
    return backend;
}

std::string releaseJsonWithAssets(const std::string& tag, bool withSums) {
    std::string json = "{\"tag_name\":\"" + tag + "\",\"name\":\"Release\",\"assets\":[{\"name\":\"" NVJ_TEST_ASSET "\",\"browser_download_url\":\"https://example.test/" NVJ_TEST_ASSET "\"}";
    if (withSums) {
        json += ",{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"https://example.test/SHA256SUMS.txt\"}";
    }
    json += "]}";
    return json;
}

void testUpdater() {
    {
        FakeServer server;
        server.releaseJson = releaseJsonWithAssets("v0.0.1", true);
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.setChannel(nvj::UpdateChannel::Stable);
        updater.checkAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::UpToDate);
    }
    {
        FakeServer server;
        server.releaseJson = releaseJsonWithAssets("v9.0.0", true);
        server.exe = fakeBinary(700 * 1024, 7);
        server.sums = nvj::sha256Hex(server.exe) + "  " NVJ_TEST_ASSET "\n";
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.setChannel(nvj::UpdateChannel::Stable);
        updater.checkAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::Available);
        CHECK(updater.status().release.tag == "v9.0.0");
        updater.installAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::ReadyToRestart);
        CHECK(server.replaced);
        CHECK(server.installed == server.exe);
    }
    {
        FakeServer server;
        server.releaseJson = releaseJsonWithAssets("v9.0.0", true);
        server.exe = fakeBinary(700 * 1024, 7);
        server.sums = nvj::sha256Hex(fakeBinary(700 * 1024, 8)) + "  " NVJ_TEST_ASSET "\n";
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.setChannel(nvj::UpdateChannel::Stable);
        updater.checkAsync();
        CHECK(waitIdle(updater));
        updater.installAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::Failed);
        CHECK(!server.replaced);
    }
    {
        FakeServer server;
        server.releaseJson = releaseJsonWithAssets("v9.0.0", false);
        server.exe = fakeBinary(700 * 1024, 7);
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.setChannel(nvj::UpdateChannel::Stable);
        updater.checkAsync();
        CHECK(waitIdle(updater));
        updater.installAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::Failed);
        CHECK(!server.replaced);
    }
    {
        FakeServer server;
        server.releaseJson = releaseJsonWithAssets("v9.0.0", true);
        server.exe = fakeBinary(700 * 1024, 7);
        server.exe[0] = 'X';
        server.sums = nvj::sha256Hex(server.exe) + "  " NVJ_TEST_ASSET "\n";
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.setChannel(nvj::UpdateChannel::Stable);
        updater.checkAsync();
        CHECK(waitIdle(updater));
        updater.installAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::Failed);
        CHECK(!server.replaced);
    }
    {
        FakeServer server;
        server.releaseJson = releaseJsonWithAssets("v9.0.0", true);
        server.exe = fakeBinary(1000, 7);
        server.sums = nvj::sha256Hex(server.exe) + "  " NVJ_TEST_ASSET "\n";
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.setChannel(nvj::UpdateChannel::Stable);
        updater.checkAsync();
        CHECK(waitIdle(updater));
        updater.installAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::Failed);
        CHECK(!server.replaced);
    }
    {
        FakeServer server;
        server.apiStatus = 404;
        nvj::Updater updater;
        updater.setBackend(makeBackend(&server));
        updater.checkAsync();
        CHECK(waitIdle(updater));
        CHECK(updater.status().state == nvj::UpdateState::Failed);
        CHECK(updater.status().message.find("private") != std::string::npos);
        updater.installAsync();
        CHECK(waitIdle(updater));
        CHECK(!server.replaced);
    }
    {
        nvj::Updater updater;
        nvj::UpdaterBackend backend;
        backend.supported = false;
        updater.setBackend(backend);
        updater.checkAsync();
        CHECK(updater.status().state == nvj::UpdateState::Unsupported);
    }
}

}

void testRecovery() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "nyrexvj_recovery_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const nvj::ProcessAliveFn nobody = [](unsigned long) { return false; };
    const nvj::ProcessAliveFn everybody = [](unsigned long) { return true; };

    CHECK(!nvj::inspectPreviousSession(dir, 100, nobody).crashed);
    CHECK(nvj::beginSession(dir, 4242, "1.2.3", 1700000000));
    nvj::PreviousSession crashed = nvj::inspectPreviousSession(dir, 100, nobody);
    CHECK(crashed.crashed && !crashed.otherInstance);
    CHECK(crashed.version == "1.2.3" && crashed.startedAt == 1700000000);
    CHECK(crashed.crashReport.empty());
    nvj::PreviousSession running = nvj::inspectPreviousSession(dir, 100, everybody);
    CHECK(running.otherInstance && !running.crashed);
    CHECK(nvj::inspectPreviousSession(dir, 4242, everybody).crashed);

    CHECK(nvj::writeFileAtomic(nvj::crashReportPath(dir), "NyrexVJ 1.2.3\nreason: test\n"));
    CHECK(nvj::writeFileAtomic(nvj::previousLogPath(dir), "a\nb\nc\nd\ne\n"));
    crashed = nvj::inspectPreviousSession(dir, 100, nobody);
    CHECK(crashed.crashReport.find("reason: test") != std::string::npos);
    CHECK(crashed.logTail == "a\nb\nc\nd\ne");
    CHECK(!fs::exists(nvj::crashReportPath(dir)));
    CHECK(nvj::inspectPreviousSession(dir, 100, nobody).crashReport.empty());

    nvj::endSession(dir, 1);
    CHECK(nvj::inspectPreviousSession(dir, 100, nobody).crashed);
    nvj::endSession(dir, 4242);
    CHECK(!nvj::inspectPreviousSession(dir, 100, nobody).crashed);
    nvj::endSession(dir, 4242);

    CHECK(nvj::writeFileAtomic(dir / "unit.txt", "first"));
    CHECK(nvj::writeFileAtomic(dir / "unit.txt", "second"));
    std::string text;
    CHECK(nvj::readWholeFile(dir / "unit.txt", text) && text == "second");
    CHECK(!fs::exists(dir / "unit.txt.tmp"));
    CHECK(!nvj::readWholeFile(dir / "missing.txt", text));

    CHECK(nvj::listAutosaves(dir).empty());
    for (int i = 0; i < 15; ++i) {
        CHECK(nvj::writeRotatingAutosave(dir, "save " + std::to_string(i), 1700000000 + i * 100, 10));
    }
    std::vector<nvj::AutosaveEntry> entries = nvj::listAutosaves(dir);
    CHECK(entries.size() == 10);
    CHECK(entries.front().timestamp == 1700000000 + 14 * 100);
    CHECK(entries.back().timestamp == 1700000000 + 5 * 100);
    for (size_t i = 1; i < entries.size(); ++i) {
        CHECK(entries[i - 1].timestamp > entries[i].timestamp);
        CHECK(!entries[i].latest);
    }
    CHECK(nvj::readWholeFile(entries.front().path, text) && text == "save 14");

    CHECK(nvj::writeFileAtomic(dir / "autosave.json", "{}"));
    entries = nvj::listAutosaves(dir);
    CHECK(entries.size() == 11);
    CHECK(entries.front().latest && entries.front().bytes == 2);
    const long long nowSeconds = static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    CHECK(entries.front().timestamp >= nowSeconds - 120 && entries.front().timestamp <= nowSeconds + 120);
    CHECK(nvj::writeRotatingAutosave(dir, "x", 1700099999, 3));
    CHECK(nvj::listAutosaves(dir).size() == 4);

    CHECK(!nvj::hasCreatorRecovery(dir));
    CHECK(nvj::writeCreatorRecovery(dir, "{\"scene\":1}"));
    CHECK(nvj::hasCreatorRecovery(dir));
    CHECK(nvj::readCreatorRecovery(dir, text) && text == "{\"scene\":1}");
    nvj::clearCreatorRecovery(dir);
    CHECK(!nvj::hasCreatorRecovery(dir));
    nvj::clearCreatorRecovery(dir);

    CHECK(nvj::tailLines("1\n2\n3\n4\n", 2) == "3\n4");
    CHECK(nvj::tailLines("1\n2", 5) == "1\n2");
    CHECK(nvj::tailLines("", 3).empty());
    CHECK(nvj::tailLines("x\n", 0).empty());
    CHECK(nvj::formatTimestamp(1700000000).size() == 19);

    nvj::Composition comp = nvj::defaultComposition();
    comp.name = "Recovered";
    CHECK(nvj::saveSession(comp));
    nvj::Composition loaded;
    CHECK(nvj::loadSession(loaded));
    CHECK(loaded.name == "Recovered");

    fs::remove_all(dir, ec);
}

void testNovelty() {
    const char* clips[] = {"Neon Sigil", "Neon Lattice", "Neon Corridor", "Chaos Smoke", "Tesseract", "Laser Web", "Mandelbox Fly", "Prism Bloom"};
    const char* effects[] = {"Kaleido Neon", "Mandala Fold", "Hue Feedback", "Star Burst", "Chrome Liquid", "Image Terrain", "Hyperspace", "Shatter"};
    const char* presets[] = {"Hyperdrive", "Neon Cathedral", "Chrome Meltdown", "Shatter Storm", "Mandala Machine", "Terrain Flight", "Tesseract Club", "Feedback Galaxy"};
    for (const char* name : clips) {
        CHECK(nvj::clipIndexByName(name) >= 0);
        CHECK(nvj::isNewClipIn(name, "0.8.0"));
        CHECK(nvj::isNewClipIn(name, "0.8.4-dev"));
        CHECK(nvj::isNewClipIn(name, "v0.8.1"));
        CHECK(!nvj::isNewClipIn(name, "0.9.0"));
        CHECK(!nvj::isNewClipIn(name, "0.7.2"));
        CHECK(!nvj::isNewClipIn(name, "1.8.0"));
        CHECK(!nvj::isNewClipIn(name, ""));
        CHECK(!nvj::isNewClipIn(name, "garbage"));
    }
    for (const char* name : effects) {
        CHECK(nvj::effectIndexByName(name) >= 0);
        CHECK(nvj::isNewEffectIn(name, "0.8.0"));
        CHECK(!nvj::isNewEffectIn(name, "0.9.0"));
    }
    for (const char* name : presets) {
        CHECK(nvj::factoryPresetIndex(name) >= 0);
        CHECK(nvj::isNewPresetIn(name, "0.8.2"));
        CHECK(!nvj::isNewPresetIn(name, "0.9.0"));
    }
    const char* sceneClips[] = {"Chrome Orbs Mesh", "Neon Cubes Mesh", "Gold Knot Mesh", "Pulse Columns Mesh"};
    for (const char* name : sceneClips) {
        CHECK(nvj::clipIndexByName(name) >= 0);
        CHECK(nvj::isNewClipIn(name, "0.9.0"));
        CHECK(nvj::isNewClipIn(name, "0.9.3-dev"));
        CHECK(!nvj::isNewClipIn(name, "0.8.0"));
        CHECK(!nvj::isNewClipIn(name, "0.10.0"));
        CHECK(!nvj::isNewClipIn(name, "1.9.0"));
    }
    CHECK(!nvj::isNewClipIn("Plasma", "0.8.0"));
    CHECK(!nvj::isNewEffectIn("Glow", "0.8.0"));
    CHECK(!nvj::isNewPresetIn("Neon Club", "0.8.0"));
    CHECK(!nvj::isNewClipIn("Hyperdrive", "0.8.0"));
    CHECK(!nvj::isNewPresetIn("Neon Sigil", "0.8.0"));
}

void testBlendModes() {
    CHECK(nvj::BlendModeCount == 14);
    std::vector<std::string> names;
    for (int i = 0; i < nvj::BlendModeCount; ++i) {
        const std::string name = nvj::blendName(i);
        CHECK(!name.empty());
        CHECK(std::find(names.begin(), names.end(), name) == names.end());
        names.push_back(name);
        CHECK(nvj::blendFromName(name) == i);
    }
    CHECK(nvj::blendFromName("soft light") == nvj::BlendSoftLight);
    CHECK(nvj::blendFromName("unknown") == nvj::BlendNormal);
    CHECK(std::string(nvj::blendName(-1)) == "Normal");
    CHECK(std::string(nvj::blendName(nvj::BlendModeCount)) == "Normal");

    nvj::Composition comp;
    for (int i = 0; i < nvj::kLayerCount; ++i) {
        comp.layers[static_cast<size_t>(i)].blend = nvj::BlendOverlay + i;
    }
    nvj::Composition loaded;
    std::string error;
    CHECK(nvj::compositionFromJson(nvj::compositionToJson(comp), loaded, error));
    for (int i = 0; i < nvj::kLayerCount; ++i) {
        CHECK(loaded.layers[static_cast<size_t>(i)].blend == nvj::BlendOverlay + i);
    }
}

void testFramePacer() {
    CHECK(nvj::clampFpsLimit(0) == 0);
    CHECK(nvj::clampFpsLimit(-5) == 0);
    CHECK(nvj::clampFpsLimit(30) == 30);
    CHECK(nvj::clampFpsLimit(59) == 60);
    CHECK(nvj::clampFpsLimit(100) == 90);
    CHECK(nvj::clampFpsLimit(500) == 144);
    CHECK(nvj::clampFpsLimit(1) == 30);
    CHECK(nvj::fpsLimitIndex(0) == 5);
    CHECK(nvj::fpsLimitIndex(144) == 4);
    CHECK(nvj::fpsLimitIndex(60) == 1);

    nvj::FramePacer unlimited;
    CHECK(!unlimited.limited());
    CHECK(unlimited.nextDeadline(5.0) == 5.0);
    CHECK(unlimited.nextDeadline(5.5) == 5.5);

    nvj::FramePacer pacer;
    pacer.setTargetFps(60);
    CHECK(pacer.limited() && pacer.targetFps() == 60);
    CHECK(std::fabs(pacer.period() - 1.0 / 60.0) < 1e-12);
    double previous = pacer.nextDeadline(10.0);
    CHECK(std::fabs(previous - (10.0 + 1.0 / 60.0)) < 1e-9);
    for (int i = 0; i < 200; ++i) {
        const double next = pacer.nextDeadline(previous - 0.004 + 0.0007 * static_cast<double>(i % 3));
        CHECK(std::fabs((next - previous) - 1.0 / 60.0) < 1e-9);
        previous = next;
    }

    nvj::FramePacer late;
    late.setTargetFps(120);
    const double first = late.nextDeadline(1.0);
    const double slow = late.nextDeadline(first + 0.080);
    CHECK(slow == first + 0.080);
    const double after = late.nextDeadline(slow + 0.001);
    CHECK(std::fabs(after - (slow + 1.0 / 120.0)) < 1e-9);
    for (int i = 0; i < 20; ++i) {
        CHECK(late.nextDeadline(after) >= after);
    }

    nvj::FramePacer change;
    change.setTargetFps(30);
    const double start = change.nextDeadline(2.0);
    change.setTargetFps(30);
    CHECK(std::fabs(change.nextDeadline(start - 0.01) - (start + 1.0 / 30.0)) < 1e-9);
    change.setTargetFps(144);
    CHECK(std::fabs(change.nextDeadline(7.0) - (7.0 + 1.0 / 144.0)) < 1e-9);
    change.setTargetFps(0);
    CHECK(!change.limited());
    CHECK(change.nextDeadline(9.0) == 9.0);

    nvj::FramePacer live;
    live.setTargetFps(120);
    const auto begin = std::chrono::steady_clock::now();
    constexpr int kFrames = 60;
    for (int i = 0; i < kFrames; ++i) {
        const double current = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        const double deadline = live.nextDeadline(current);
        if (deadline > current) {
            nvj::platform::sleepPrecise(deadline - current);
        }
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    CHECK(elapsed >= static_cast<double>(kFrames) / 120.0 * 0.999);
    CHECK(elapsed <= static_cast<double>(kFrames) / 120.0 * 1.4);
}

void testFrameStats() {
    nvj::FrameStats empty(8);
    CHECK(empty.size() == 0);
    CHECK(empty.capacity() == 8);
    CHECK(empty.last() == 0.0);
    CHECK(empty.summary().count == 0);
    CHECK(empty.overBudgetFraction(10.0) == 0.0);
    CHECK(empty.ordered().empty());

    nvj::FrameStats stats(100);
    for (int i = 1; i <= 100; ++i) {
        stats.add(static_cast<double>(i));
    }
    const nvj::FrameSummary summary = stats.summary();
    CHECK(summary.count == 100);
    CHECK(std::fabs(summary.avg - 50.5) < 1e-9);
    CHECK(summary.min == 1.0);
    CHECK(summary.max == 100.0);
    CHECK(summary.p50 >= 49.0 && summary.p50 <= 51.0);
    CHECK(summary.p95 >= 94.0 && summary.p95 <= 96.0);
    CHECK(summary.p99 >= 98.0 && summary.p99 <= 100.0);
    CHECK(summary.min <= summary.p50 && summary.p50 <= summary.p95 && summary.p95 <= summary.p99 && summary.p99 <= summary.max);
    CHECK(std::fabs(stats.overBudgetFraction(90.0) - 0.10) < 1e-9);
    CHECK(stats.overBudgetFraction(100.0) == 0.0);
    CHECK(stats.last() == 100.0);

    nvj::FrameStats wrap(4);
    for (int i = 1; i <= 10; ++i) {
        wrap.add(static_cast<double>(i));
    }
    CHECK(wrap.size() == 4);
    const std::vector<float> ordered = wrap.ordered();
    CHECK(ordered.size() == 4);
    CHECK(ordered[0] == 7.0f && ordered[1] == 8.0f && ordered[2] == 9.0f && ordered[3] == 10.0f);
    CHECK(wrap.summary().min == 7.0);
    CHECK(wrap.summary().max == 10.0);
    wrap.clear();
    CHECK(wrap.size() == 0);
    CHECK(wrap.summary().count == 0);
    nvj::FrameStats zero(0);
    zero.add(3.0);
    CHECK(zero.size() == 1 && zero.last() == 3.0);

    CHECK(nvj::clampBudgetFps(1) == nvj::kMinBudgetFps);
    CHECK(nvj::clampBudgetFps(100000) == nvj::kMaxBudgetFps);
    CHECK(nvj::clampBudgetFps(60) == 60);
    CHECK(std::fabs(nvj::budgetMilliseconds(60) - 1000.0 / 60.0) < 1e-9);
    CHECK(std::fabs(nvj::budgetMilliseconds(0) - 1000.0 / nvj::kMinBudgetFps) < 1e-9);
}

void testRenderPolicy() {
    CHECK(!nvj::layerVisible(0.0f));
    CHECK(!nvj::layerVisible(nvj::kHiddenOpacity));
    CHECK(nvj::layerVisible(nvj::kHiddenOpacity + 0.0005f));
    CHECK(nvj::layerVisible(1.0f));

    CHECK(nvj::clampQualityMode(-4) == nvj::QualityFull);
    CHECK(nvj::clampQualityMode(99) == nvj::QualityAuto);
    CHECK(nvj::fixedQualityScale(nvj::QualityFull) == 1.0f);
    CHECK(nvj::fixedQualityScale(nvj::QualityBalanced) == 0.75f);
    CHECK(nvj::fixedQualityScale(nvj::QualityPerformance) == 0.5f);
    CHECK(nvj::fixedQualityScale(nvj::QualityAuto) == 1.0f);

    CHECK(nvj::scaledDimension(1920, 1.0f) == 1920);
    CHECK(nvj::scaledDimension(1920, 0.75f) == 1440);
    CHECK(nvj::scaledDimension(1080, 0.75f) == 810);
    CHECK(nvj::scaledDimension(1080, 0.5f) == 540);
    CHECK(nvj::scaledDimension(1080, 0.875f) % 2 == 0);
    CHECK(nvj::scaledDimension(0, 0.5f) == 0);
    CHECK(nvj::scaledDimension(50, 0.5f) == 50);
    CHECK(nvj::scaledDimension(4000, 0.01f) == 64);

    nvj::QualityController controller;
    const double budget = 16.0;
    CHECK(controller.scale() == 1.0f && controller.steps() == 0);
    CHECK(controller.update(0.0, budget) == 1.0f);
    CHECK(controller.update(30.0, 0.0) == 1.0f);
    for (int i = 0; i < 10; ++i) {
        controller.update(budget * 0.5, budget);
    }
    CHECK(controller.steps() == 0);
    int guard = 0;
    while (controller.steps() < 1 && guard++ < 100) {
        controller.update(budget * 2.0, budget);
    }
    CHECK(controller.steps() == 1);
    CHECK(controller.scale() == 0.875f);
    CHECK(guard >= 20);
    const int stepAfterFirst = controller.steps();
    for (int i = 0; i < 30; ++i) {
        controller.update(budget * 2.0, budget);
    }
    CHECK(controller.steps() == stepAfterFirst);
    for (int i = 0; i < 2000; ++i) {
        controller.update(budget * 3.0, budget);
    }
    CHECK(controller.steps() == 4);
    CHECK(controller.scale() == 0.5f);
    for (int i = 0; i < 4000; ++i) {
        controller.update(budget * 0.05, budget);
    }
    CHECK(controller.steps() == 0);
    CHECK(controller.scale() == 1.0f);
    controller.update(budget * 3.0, budget);
    controller.reset();
    CHECK(controller.steps() == 0 && controller.scale() == 1.0f);

    nvj::QualityController steady;
    for (int i = 0; i < 1000; ++i) {
        steady.update(budget * 0.75, budget);
    }
    CHECK(steady.steps() == 0);

    nvj::IdleRelease idle(5);
    bool released = false;
    for (int i = 0; i < 4; ++i) {
        released = released || idle.update(false);
    }
    CHECK(!released);
    CHECK(idle.update(false));
    CHECK(idle.update(false));
    CHECK(!idle.update(true));
    CHECK(idle.idleFrames() == 0);
    idle.update(false);
    idle.reset();
    CHECK(idle.idleFrames() == 0);

    nvj::ProfileTable table;
    const auto frame = [&](double a, double b, bool withB) {
        table.beginFrame();
        nvj::ProfileSample first;
        first.kind = nvj::PassKind::Effect;
        first.layer = 1;
        first.slot = 1;
        first.name = "Glow";
        first.ms = a;
        table.add(first);
        if (withB) {
            nvj::ProfileSample second;
            second.kind = nvj::PassKind::Composite;
            second.layer = 1;
            second.ms = b;
            table.add(second);
        }
        table.endFrame();
    };
    frame(4.0, 1.0, true);
    CHECK(table.rows().size() == 2);
    CHECK(table.frames() == 1);
    CHECK(std::fabs(table.lastTotalMs() - 5.0) < 1e-9);
    CHECK(std::fabs(table.averageTotalMs() - 5.0) < 1e-9);
    for (int i = 0; i < 40; ++i) {
        frame(4.0, 1.0, true);
    }
    const std::vector<nvj::ProfileRow> sorted = table.sorted();
    CHECK(sorted.size() == 2);
    CHECK(sorted[0].name == "Glow");
    CHECK(sorted[0].averageMs > sorted[1].averageMs);
    CHECK(std::fabs(sorted[0].share + sorted[1].share - 1.0) < 1e-9);
    CHECK(std::fabs(sorted[0].share - 0.8) < 1e-6);
    frame(8.0, 0.0, false);
    CHECK(table.rows().size() == 2);
    CHECK(table.rows()[1].missing == 1);
    CHECK(table.rows()[1].lastMs == 0.0);
    for (int i = 0; i < 100; ++i) {
        frame(4.0, 0.0, false);
    }
    CHECK(table.rows().size() == 1);
    table.clear();
    CHECK(table.rows().empty() && table.frames() == 0);
}

void testRenderQualitySetting() {
    nvj::Settings defaults;
    CHECK(defaults.renderQuality == nvj::QualityFull);

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_quality_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    nvj::Settings settings;
    settings.renderQuality = nvj::QualityAuto;
    CHECK(nvj::saveSettings(settings));
    nvj::Settings loaded;
    CHECK(nvj::loadSettings(loaded));
    CHECK(loaded.renderQuality == nvj::QualityAuto);

    const auto rewrite = [&](const std::string& value) {
        std::ofstream out(dir / "settings.json", std::ios::trunc);
        out << "{\"renderQuality\": " << value << "}";
    };
    rewrite("42");
    nvj::Settings high;
    CHECK(nvj::loadSettings(high));
    CHECK(high.renderQuality == nvj::QualityAuto);
    rewrite("-3");
    nvj::Settings low;
    CHECK(nvj::loadSettings(low));
    CHECK(low.renderQuality == nvj::QualityFull);
    rewrite("\"fast\"");
    nvj::Settings wrong;
    CHECK(nvj::loadSettings(wrong));
    CHECK(wrong.renderQuality == nvj::QualityFull);
    std::filesystem::remove_all(dir, ec);
}

void testRenderCost() {
    nvj::Composition empty;
    for (nvj::Layer& layer : empty.layers) {
        layer.activeColumn = -1;
    }
    const nvj::RenderCost none = nvj::estimateRenderCost(empty);
    CHECK(none.layers() == 0);
    CHECK(none.total() == none.finals + none.masterEffects);

    nvj::Composition comp;
    for (nvj::Layer& layer : comp.layers) {
        layer.activeColumn = -1;
    }
    comp.layers[0].cells[0] = 0;
    comp.layers[0].activeColumn = 0;
    comp.layers[1].cells[0] = 1;
    comp.layers[1].activeColumn = 0;
    const nvj::RenderCost two = nvj::estimateRenderCost(comp);
    CHECK(two.layers() == 2);
    CHECK(two.composites == 2);
    CHECK(two.total() > none.total());

    comp.layers[1].bypass = true;
    CHECK(nvj::estimateRenderCost(comp).layers() == 1);
    comp.layers[1].bypass = false;
    comp.layers[0].solo = true;
    CHECK(nvj::estimateRenderCost(comp).layers() == 1);
    comp.layers[0].solo = false;

    comp.layers[0].fx[0].effect = 0;
    comp.layers[0].fx[0].enabled = true;
    const nvj::RenderCost withEffect = nvj::estimateRenderCost(comp);
    CHECK(withEffect.layerEffects == 1);
    CHECK(withEffect.total() == two.total() + 1);
    comp.layers[0].fx[0].enabled = false;
    CHECK(nvj::estimateRenderCost(comp).layerEffects == 0);

    for (const nvj::Composition& look : nvj::factoryPresets()) {
        const nvj::RenderCost cost = nvj::estimateRenderCost(look);
        CHECK(cost.total() <= 40);
        CHECK(cost.layers() <= nvj::kLayerCount);
    }
}

void settleHistory(nvj::EditHistory& history, const nvj::Composition& comp) {
    history.observe(comp, 0.1, false);
    history.observe(comp, 0.5, false);
}

void testEditHistory() {
    nvj::Composition comp;
    nvj::EditHistory history;
    history.reset(comp);
    CHECK(!history.canUndo());
    CHECK(!history.canRedo());
    CHECK(!history.undo(comp, 10));

    comp.layers[0].opacity = 0.25f;
    history.observe(comp, 0.1, false);
    CHECK(history.canUndo());
    CHECK(history.undoCount() == 0);
    history.observe(comp, 0.1, true);
    history.observe(comp, 0.5, true);
    CHECK(history.undoCount() == 0);
    history.observe(comp, 0.5, false);
    CHECK(history.undoCount() == 1);

    comp.layers[0].activeColumn = 3;
    comp.bpm = 99.0f;
    settleHistory(history, comp);
    CHECK(history.undoCount() == 1);

    CHECK(history.undo(comp, 10));
    CHECK(comp.layers[0].opacity == 1.0f);
    CHECK(comp.layers[0].activeColumn == 3);
    CHECK(comp.bpm == 99.0f);
    CHECK(history.canRedo());
    CHECK(!history.canUndo());
    CHECK(history.redo(comp, 10));
    CHECK(comp.layers[0].opacity == 0.25f);
    CHECK(!history.redo(comp, 10));

    comp.layers[1].blend = nvj::BlendOverlay;
    settleHistory(history, comp);
    CHECK(history.undoCount() == 2);
    CHECK(history.undo(comp, 10));
    CHECK(history.canRedo());
    comp.layers[2].speed = 2.0f;
    settleHistory(history, comp);
    CHECK(!history.canRedo());
    CHECK(history.undoCount() == 2);

    comp.layers[3].opacity = 0.5f;
    CHECK(history.undo(comp, 10));
    CHECK(comp.layers[3].opacity == 1.0f);
    CHECK(comp.layers[2].speed == 2.0f);
    CHECK(history.redo(comp, 10));
    CHECK(comp.layers[3].opacity == 0.5f);

    nvj::Composition cells;
    nvj::EditHistory cellHistory;
    cellHistory.reset(cells);
    cells.layers[0].cells[0] = 9;
    settleHistory(cellHistory, cells);
    cells.layers[0].cells[0] = 1;
    settleHistory(cellHistory, cells);
    CHECK(cellHistory.undo(cells, 5));
    CHECK(cells.layers[0].cells[0] == -1);
    CHECK(cellHistory.undo(cells, 5));
    CHECK(cells.layers[0].cells[0] == -1);

    nvj::Composition decks;
    nvj::EditHistory deckHistory;
    decks.deckUsed[1] = true;
    deckHistory.reset(decks);
    CHECK(nvj::selectDeck(decks, 1));
    settleHistory(deckHistory, decks);
    CHECK(deckHistory.undoCount() == 0);
    decks.layers[0].opacity = 0.1f;
    settleHistory(deckHistory, decks);
    CHECK(deckHistory.undoCount() == 1);
    CHECK(nvj::selectDeck(decks, 0));
    settleHistory(deckHistory, decks);
    CHECK(deckHistory.undoCount() == 1);
    CHECK(deckHistory.undo(decks, 10));
    CHECK(nvj::selectDeck(decks, 1));
    CHECK(decks.layers[0].opacity == 1.0f);

    nvj::Composition many;
    nvj::EditHistory manyHistory;
    manyHistory.reset(many);
    for (int i = 0; i < 80; ++i) {
        many.master.level = 0.1f + 0.01f * static_cast<float>(i);
        settleHistory(manyHistory, many);
    }
    CHECK(manyHistory.undoCount() == nvj::EditHistory::kLimit);
    int steps = 0;
    while (manyHistory.undo(many, 10)) {
        ++steps;
    }
    CHECK(steps == static_cast<int>(nvj::EditHistory::kLimit));
}

void testModulation() {
    for (int shape = 0; shape < nvj::LfoShapeCount; ++shape) {
        CHECK(nvj::lfoShapeFromName(nvj::lfoShapeName(shape)) == shape);
        for (int rate = 0; rate < nvj::kLfoRateCount; ++rate) {
            nvj::Lfo lfo;
            lfo.shape = shape;
            lfo.rate = rate;
            float lowest = 1.0f;
            float highest = 0.0f;
            for (int i = 0; i < 400; ++i) {
                const float value = nvj::lfoValue(lfo, 1, 0.173 * i);
                CHECK(value >= 0.0f && value <= 1.0f);
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
            CHECK(highest - lowest > 0.3f);
        }
    }

    nvj::Lfo sine;
    sine.shape = nvj::LfoSine;
    sine.rate = 4;
    CHECK(std::fabs(nvj::lfoValue(sine, 0, 0.0) - 0.0f) < 1e-5f);
    CHECK(std::fabs(nvj::lfoValue(sine, 0, 2.0) - 1.0f) < 1e-5f);
    CHECK(std::fabs(nvj::lfoValue(sine, 0, 1.0) - 0.5f) < 1e-5f);
    CHECK(std::fabs(nvj::lfoValue(sine, 0, 0.7) - nvj::lfoValue(sine, 0, 0.7 + 4.0)) < 1e-4f);
    sine.phase = 0.5f;
    CHECK(std::fabs(nvj::lfoValue(sine, 0, 0.0) - 1.0f) < 1e-5f);

    nvj::Lfo saw;
    saw.shape = nvj::LfoSaw;
    saw.rate = 3;
    CHECK(std::fabs(nvj::lfoValue(saw, 0, 0.5) - 0.25f) < 1e-5f);
    CHECK(std::fabs(nvj::lfoValue(saw, 0, 2.5) - 0.25f) < 1e-5f);
    CHECK(nvj::lfoValue(saw, 0, -0.5) >= 0.0f);

    nvj::Lfo square;
    square.shape = nvj::LfoSquare;
    square.rate = 3;
    CHECK(nvj::lfoValue(square, 0, 0.5) == 1.0f);
    CHECK(nvj::lfoValue(square, 0, 1.5) == 0.0f);

    nvj::Lfo stepped;
    stepped.shape = nvj::LfoRandom;
    stepped.rate = 2;
    CHECK(nvj::lfoValue(stepped, 0, 0.2) == nvj::lfoValue(stepped, 0, 0.9));
    CHECK(nvj::lfoValue(stepped, 0, 0.2) != nvj::lfoValue(stepped, 1, 0.2));
    CHECK(nvj::lfoValue(stepped, 0, 0.2) == nvj::lfoValue(stepped, 0, 0.2));

    nvj::Lfo smooth;
    smooth.shape = nvj::LfoSmooth;
    smooth.rate = 2;
    CHECK(std::fabs(nvj::lfoValue(smooth, 3, 0.9999) - nvj::lfoValue(smooth, 3, 1.0001)) < 0.01f);
    CHECK(nvj::lfoValue(sine, 0, std::nan("")) == 0.0f);

    nvj::AudioFeatures audio;
    audio.bass = 0.7f;
    audio.kick = 0.2f;
    nvj::Modulators modulators;
    modulators.macros[2] = 0.6f;
    modulators.macros[7] = 4.0f;
    modulators.lfos[1].shape = nvj::LfoSaw;
    modulators.lfos[1].rate = 3;
    CHECK(nvj::modulationSource(nvj::BandOff, audio, modulators, 1.0, 4) == 0.0f);
    CHECK(nvj::modulationSource(nvj::BandBass, audio, modulators, 1.0, 4) == 0.7f);
    CHECK(nvj::modulationSource(nvj::BandKick, audio, modulators, 1.0, 4) == 0.2f);
    CHECK(nvj::modulationSource(nvj::BandMacro1 + 2, audio, modulators, 1.0, 4) == 0.6f);
    CHECK(nvj::modulationSource(nvj::BandMacro8, audio, modulators, 1.0, 4) == 1.0f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandLfo2, audio, modulators, 0.5, 4) - 0.25f) < 1e-5f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandBeat, audio, modulators, 7.25, 4) - 0.25f) < 1e-5f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandBar, audio, modulators, 6.0, 4) - 0.5f) < 1e-5f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandBar, audio, modulators, 6.0, 3) - 0.0f) < 1e-5f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandBar, audio, modulators, 7.0, 3) - 1.0f / 3.0f) < 1e-5f);
    CHECK(nvj::modulationSource(nvj::BandBar, audio, modulators, 7.0, 100) == 7.0f / 8.0f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandTimecodeSecond, audio, modulators, 1.0, 4, 12.25) - 0.25f) < 1e-5f);
    CHECK(std::fabs(nvj::modulationSource(nvj::BandTimecodeMinute, audio, modulators, 1.0, 4, 90.0) - 0.5f) < 1e-5f);
    CHECK(nvj::modulationSource(nvj::BandTimecodeSecond, audio, modulators, 1.0, 4) == 0.0f);
    CHECK(nvj::modulationSource(nvj::BandTimecodeMinute, audio, modulators, 1.0, 4, -1.0) == 0.0f);
    CHECK(nvj::modulationSource(nvj::BandTimecodeMinute, audio, modulators, 1.0, 4, std::nan("")) == 0.0f);
    CHECK(nvj::audioBandFromName("TC minute") == nvj::BandTimecodeMinute);
    CHECK(nvj::audioBandFromName(nvj::audioBandName(nvj::BandTimecodeSecond)) == nvj::BandTimecodeSecond);
    CHECK(nvj::modulationSource(-4, audio, modulators, 1.0, 4) == 0.0f);
    CHECK(nvj::modulationSource(nvj::AudioBandCount, audio, modulators, 1.0, 4) == 0.0f);
    CHECK(nvj::modulationSource(nvj::BandBeat, audio, modulators, std::nan(""), 4) == 0.0f);
    CHECK(nvj::isAudioSource(nvj::BandMid) && !nvj::isAudioSource(nvj::BandLfo1) && !nvj::isAudioSource(nvj::BandOff));
    CHECK(nvj::clampBeatsPerBar(0) == nvj::kMinBeatsPerBar && nvj::clampBeatsPerBar(99) == nvj::kMaxBeatsPerBar && nvj::clampBeatsPerBar(5) == 5);

    for (int band = 0; band < nvj::AudioBandCount; ++band) {
        CHECK(nvj::audioBandFromName(nvj::audioBandName(band)) == band);
    }

    nvj::Composition comp = nvj::defaultComposition();
    comp.beatsPerBar = 6;
    comp.modulators.macros[1] = 0.35f;
    comp.modulators.lfos[2].shape = nvj::LfoSmooth;
    comp.modulators.lfos[2].rate = 6;
    comp.modulators.lfos[2].phase = 0.25f;
    comp.layers[0].mods[0].band = nvj::BandLfo3;
    comp.layers[0].mods[1].band = nvj::BandMacro2;
    comp.master.mods[0].band = nvj::BandBar;
    std::string error;
    nvj::Composition loaded;
    CHECK(nvj::compositionFromJson(nvj::compositionToJson(comp), loaded, error));
    CHECK(loaded.beatsPerBar == 6);
    CHECK(loaded.modulators.macros[1] == 0.35f);
    CHECK(loaded.modulators.lfos[2].shape == nvj::LfoSmooth);
    CHECK(loaded.modulators.lfos[2].rate == 6);
    CHECK(loaded.modulators.lfos[2].phase == 0.25f);
    CHECK(loaded.layers[0].mods[0].band == nvj::BandLfo3);
    CHECK(loaded.layers[0].mods[1].band == nvj::BandMacro2);
    CHECK(loaded.master.mods[0].band == nvj::BandBar);
    CHECK(sameComposition(comp, loaded));

    nvj::Composition withoutNew;
    withoutNew.beatsPerBar = 7;
    withoutNew.modulators.macros[0] = 0.9f;
    CHECK(nvj::compositionFromJson("{\"layers\":[]}", withoutNew, error));
    CHECK(withoutNew.beatsPerBar == 4);
    CHECK(withoutNew.modulators.macros[0] == 0.0f);
    CHECK(withoutNew.modulators.lfos[0].rate == 4);

    nvj::Composition tampered;
    CHECK(nvj::compositionFromJson("{\"layers\":[],\"beatsPerBar\":99,\"modulators\":{\"macros\":[5,-3,\"x\"],\"lfos\":[{\"shape\":\"Nonsense\",\"rate\":99,\"phase\":7}]}}", tampered, error));
    CHECK(tampered.beatsPerBar == nvj::kMaxBeatsPerBar);
    CHECK(tampered.modulators.macros[0] == 1.0f && tampered.modulators.macros[1] == 0.0f);
    CHECK(tampered.modulators.lfos[0].rate >= 0 && tampered.modulators.lfos[0].rate < nvj::kLfoRateCount);
    CHECK(tampered.modulators.lfos[0].phase >= 0.0f && tampered.modulators.lfos[0].phase <= 1.0f);
    CHECK(tampered.modulators.lfos[0].shape >= 0 && tampered.modulators.lfos[0].shape < nvj::LfoShapeCount);

    nvj::Composition edited = nvj::defaultComposition();
    nvj::EditHistory history;
    history.reset(edited);
    edited.modulators.lfos[0].shape = nvj::LfoSquare;
    edited.beatsPerBar = 3;
    settleHistory(history, edited);
    CHECK(history.undoCount() == 1);
    edited.modulators.macros[0] = 0.8f;
    settleHistory(history, edited);
    CHECK(history.undoCount() == 1);
    CHECK(history.undo(edited, 10));
    CHECK(edited.modulators.lfos[0].shape == nvj::LfoSine);
    CHECK(edited.beatsPerBar == 4);
    CHECK(edited.modulators.macros[0] == 0.8f);
    CHECK(history.redo(edited, 10));
    CHECK(edited.modulators.lfos[0].shape == nvj::LfoSquare && edited.beatsPerBar == 3);
}

int main() {
    testSha256();
    testVersions();
    testProgramDetection();
    testReleaseParsing();
    testHashFile();
    testLibraries();
    testPresets();
    testPlaybackPersistence();
    testBlendModes();
    testFrameStats();
    testFramePacer();
    testRenderCost();
    testRenderPolicy();
    testRenderQualitySetting();
    testEditHistory();
    testModulation();
    testDecks();
    testPresetFiles();
    testNovelty();
    testRecovery();
    testUpdater();
    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

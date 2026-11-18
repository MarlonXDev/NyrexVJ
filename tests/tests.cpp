#include <algorithm>
#include "render/clip_library.hpp"
#include "core/composition.hpp"
#include "core/edit_history.hpp"
#include "core/novelty.hpp"
#include "render/effect_library.hpp"
#include "core/preset.hpp"
#include "core/settings.hpp"
#include "core/sha256.hpp"
#include "update/update_logic.hpp"
#include "update/updater.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

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

void testReleaseParsing() {
    const char* json = R"({
        "tag_name": "v0.3.0",
        "name": "NyrexVJ 0.3.0",
        "body": "Notes",
        "html_url": "https://github.com/MarlonXDev/NyrexVJ/releases/tag/v0.3.0",
        "draft": false,
        "prerelease": false,
        "assets": [
            {"name": "NyrexVJ.exe", "browser_download_url": "https://example.test/NyrexVJ.exe"},
            {"name": "SHA256SUMS.txt", "browser_download_url": "https://example.test/SHA256SUMS.txt"},
            {"name": "other.zip", "browser_download_url": "https://example.test/other.zip"}
        ]
    })";
    nvj::ReleaseInfo release;
    std::string error;
    CHECK(nvj::parseRelease(json, release, error));
    CHECK(release.tag == "v0.3.0");
    CHECK(release.exeUrl == "https://example.test/NyrexVJ.exe");
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
        "assets":[{"name":"NyrexVJ.exe","browser_download_url":"https://example.test/n.exe"}]})json";
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
    CHECK(nvj::findHash(hash + "  NyrexVJ.exe\r\n", "NyrexVJ.exe", found) && found == hash);
    CHECK(nvj::findHash("deadbeef  nope.txt\n" + hash + " *NyrexVJ.exe\n", "NyrexVJ.exe", found) && found == hash);
    std::string upper;
    for (char c : hash) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    CHECK(nvj::findHash(upper + "  NyrexVJ.exe", "NyrexVJ.exe", found) && found == hash);
    CHECK(!nvj::findHash(hash + "  other.exe\n", "NyrexVJ.exe", found));
    CHECK(!nvj::findHash("", "NyrexVJ.exe", found));
}

void testLibraries() {
    const auto& clips = nvj::clipLibrary();
    const auto& effects = nvj::effectLibrary();
    CHECK(clips.size() >= 16);
    CHECK(effects.size() >= 20);
    std::set<std::string> clipNames;
    for (const auto& clip : clips) {
        CHECK(clip.isScene() ? clip.body.empty() : !clip.body.empty());
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
    settings.outputMonitor = 2;
    settings.autoInstall = false;
    settings.vsync = false;
    settings.openOutputOnStart = true;
    settings.showStatusBar = false;
    settings.uiScale = 1.25f;
    CHECK(nvj::saveSettings(settings));
    nvj::Settings back;
    CHECK(nvj::loadSettings(back));
    CHECK(!back.checkUpdates && back.channel == nvj::UpdateChannel::Stable && back.outputMonitor == 2);
    CHECK(!back.autoInstall && !back.vsync && back.openOutputOnStart && !back.showStatusBar && back.uiScale == 1.25f);

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
    data[0] = 'M';
    data[1] = 'Z';
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
        } else if (url == "https://example.test/NyrexVJ.exe") {
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
    std::string json = "{\"tag_name\":\"" + tag + "\",\"name\":\"Release\",\"assets\":[{\"name\":\"NyrexVJ.exe\",\"browser_download_url\":\"https://example.test/NyrexVJ.exe\"}";
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
        server.sums = nvj::sha256Hex(server.exe) + "  NyrexVJ.exe\n";
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
        server.sums = nvj::sha256Hex(fakeBinary(700 * 1024, 8)) + "  NyrexVJ.exe\n";
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
        server.sums = nvj::sha256Hex(server.exe) + "  NyrexVJ.exe\n";
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
        server.sums = nvj::sha256Hex(server.exe) + "  NyrexVJ.exe\n";
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

int main() {
    testSha256();
    testVersions();
    testReleaseParsing();
    testHashFile();
    testLibraries();
    testPresets();
    testBlendModes();
    testEditHistory();
    testDecks();
    testPresetFiles();
    testNovelty();
    testUpdater();
    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

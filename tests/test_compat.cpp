#include "check.hpp"

#include "control/midi_map.hpp"
#include "core/composition.hpp"
#include "core/cue_list.hpp"
#include "core/output_map.hpp"
#include "core/preset.hpp"
#include "core/settings.hpp"
#include "core/show_timeline.hpp"
#include "core/venue_setup.hpp"
#include "render/clip_library.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef NYREXVJ_TEST_DATA
#error NYREXVJ_TEST_DATA is not defined
#endif

namespace fs = std::filesystem;

namespace {

const char* const kNewest = "0.50.0";

fs::path compatDirectory() {
    return fs::path(NYREXVJ_TEST_DATA) / "compat";
}

std::string readText(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool writeText(const fs::path& file, const std::string& text) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << text;
    return static_cast<bool>(stream);
}

std::string frozen(const std::string& kind) {
    return kind + "_v" + kNewest + ".json";
}

nlohmann::json parsed(const std::string& text) {
    return nlohmann::json::parse(text, nullptr, false);
}

void useDataDir(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

nvj::Composition makeComposition() {
    nvj::Composition comp;
    comp.name = "Compat Fixture";
    comp.bpm = 132.0f;
    comp.beatsPerBar = 3;
    comp.layers[0].setCell(0, nvj::clipIndexByName("Plasma"));
    comp.layers[0].setCell(1, nvj::clipIndexByName("Tunnel"));
    comp.layers[0].activeColumn = 0;
    comp.layers[0].opacity = 0.75f;
    comp.layers[1].setCell(0, nvj::clipIndexByName("Kaleido"));
    comp.layers[1].bypass = true;
    comp.activeDeck = 0;
    return comp;
}

nvj::Settings makeSettings() {
    nvj::Settings settings;
    settings.checkUpdates = false;
    settings.vsync = false;
    settings.fpsLimit = 60;
    settings.particleLimit = 4096;
    settings.showStatusBar = false;
    settings.convertMode = "automatic";
    settings.design = 1;
    settings.theme = "studio";
    return settings;
}

nvj::OutputSet makeVenue() {
    nvj::OutputSet set = nvj::defaultOutputs();
    set[1].enabled = true;
    set[1].fullscreen = true;
    set[1].monitor = 2;
    set[1].name = "Left wall";
    set[1].map.slice = {0.0f, 0.0f, 0.55f, 1.0f};
    set[1].map.blend.right = 0.1f;
    set[1].color.saturation = 0.8f;
    return set;
}

std::vector<nvj::Cue> makeCues() {
    return {{1.5, "master/level", 0.5f}, {10.0, "deck/next", 1.0f}, {20.25, "master/blackout", 0.0f}};
}

nvj::ShowTimeline makeTimeline() {
    nvj::ShowTimeline timeline;
    const int lane = nvj::addLane(timeline, "master/level");
    nvj::insertKey(timeline, static_cast<size_t>(lane), {0.0, 0.0f, nvj::KeyShape::Linear});
    nvj::insertKey(timeline, static_cast<size_t>(lane), {8.0, 1.0f, nvj::KeyShape::Smooth});
    nvj::insertKey(timeline, static_cast<size_t>(lane), {16.0, 0.25f, nvj::KeyShape::Hold});
    nvj::setLoop(timeline, true, 4.0, 12.0);
    return timeline;
}

std::vector<nvj::MidiBinding> makeBindings() {
    std::vector<nvj::MidiBinding> bindings(2);
    bindings[0].kind = nvj::MidiKind::ControlChange;
    bindings[0].channel = 1;
    bindings[0].number = 7;
    bindings[0].target = "master/level";
    bindings[0].low = 0.0f;
    bindings[0].high = 1.0f;
    bindings[1].kind = nvj::MidiKind::Note;
    bindings[1].channel = 2;
    bindings[1].number = 36;
    bindings[1].target = "deck/next";
    return bindings;
}

std::string settingsText(const fs::path& scratch) {
    useDataDir(scratch);
    nvj::saveSettings(makeSettings());
    return readText(scratch / "settings.json");
}

int writeFixtures(const fs::path& directory) {
    std::error_code ec;
    fs::create_directories(directory, ec);
    bool ok = true;
    ok = writeText(directory / frozen("composition"), nvj::compositionToJson(makeComposition())) && ok;
    ok = writeText(directory / frozen("settings"), settingsText(fs::temp_directory_path() / "nyrexvj_compat_write")) && ok;
    ok = writeText(directory / frozen("venue"), nvj::venueToJson("Fixture Venue", makeVenue()).dump(2)) && ok;
    ok = writeText(directory / frozen("cues"), nvj::cuesToJson(makeCues())) && ok;
    ok = writeText(directory / frozen("timeline"), nvj::timelineToJson(makeTimeline())) && ok;
    ok = writeText(directory / frozen("midi-profile"), nvj::profileToJson("Fixture Controller", makeBindings())) && ok;
    return ok ? 0 : 1;
}

void testComposition() {
    const std::string text = readText(compatDirectory() / frozen("composition"));
    CHECK(!text.empty());
    nvj::Composition loaded;
    std::string error;
    std::vector<std::string> warnings;
    CHECK(nvj::compositionFromJson(text, loaded, error, &warnings));
    CHECK(warnings.empty());
    CHECK(loaded.name == "Compat Fixture");
    CHECK(loaded.bpm == 132.0f);
    CHECK(loaded.beatsPerBar == 3);
    CHECK(loaded.layers[0].cells[0] == nvj::clipIndexByName("Plasma"));
    CHECK(loaded.layers[0].cells[1] == nvj::clipIndexByName("Tunnel"));
    CHECK(loaded.layers[0].activeColumn == 0);
    CHECK(loaded.layers[0].opacity == 0.75f);
    CHECK(loaded.layers[1].cells[0] == nvj::clipIndexByName("Kaleido"));
    CHECK(loaded.layers[1].bypass);
    CHECK(parsed(nvj::compositionToJson(loaded)) == parsed(text));
    CHECK(parsed(nvj::compositionToJson(makeComposition())) == parsed(text));
}

void testCompositionOlderShapes() {
    nvj::Composition loaded;
    std::string error;
    std::vector<std::string> warnings;
    const std::string minimal = readText(compatDirectory() / "composition_older_minimal.json");
    CHECK(!minimal.empty());
    CHECK(nvj::compositionFromJson(minimal, loaded, error, &warnings));
    CHECK(loaded.layers[0].cells[0] == nvj::clipIndexByName("Plasma"));
    CHECK(loaded.layers[0].cells[1] == nvj::clipIndexByName("Plasma"));
    CHECK(loaded.bpm == 128.0f);
    CHECK(loaded.activeDeck == 0);
    for (const nvj::ClipPlayback& item : loaded.layers[0].playback) {
        CHECK(item.isDefault());
    }
    nvj::Composition withUnknown;
    nlohmann::json doc = parsed(readText(compatDirectory() / frozen("composition")));
    doc["somethingFromTheFuture"] = {{"a", 1}, {"b", {1, 2, 3}}};
    doc["layers"][0]["alsoNew"] = "x";
    CHECK(nvj::compositionFromJson(doc.dump(), withUnknown, error, &warnings));
    CHECK(withUnknown.name == "Compat Fixture");
    CHECK(withUnknown.layers[0].opacity == 0.75f);
    CHECK(!nvj::compositionFromJson("{\"layers\":[", withUnknown, error));
    CHECK(!error.empty());
    CHECK(!nvj::compositionFromJson("[]", withUnknown, error));
    CHECK(!nvj::compositionFromJson("{\"name\":\"no layers\"}", withUnknown, error));
}

void testSettings() {
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_compat_settings";
    useDataDir(scratch);
    const std::string text = readText(compatDirectory() / frozen("settings"));
    CHECK(!text.empty());
    CHECK(writeText(scratch / "settings.json", text));
    nvj::Settings loaded;
    CHECK(nvj::loadSettings(loaded));
    CHECK(!loaded.checkUpdates);
    CHECK(!loaded.vsync);
    CHECK(loaded.fpsLimit == 60);
    CHECK(loaded.particleLimit == 4096);
    CHECK(!loaded.showStatusBar);
    CHECK(loaded.convertMode == "automatic");
    CHECK(parsed(settingsText(scratch)) == parsed(text));

    useDataDir(scratch);
    CHECK(writeText(scratch / "settings.json", readText(compatDirectory() / "settings_older.json")));
    nvj::Settings older;
    CHECK(nvj::loadSettings(older));
    CHECK(older.fpsLimit == 90);
    CHECK(older.checkUpdates);
    CHECK(older.vsync);
    CHECK(older.convertMode == "ask");
    CHECK(older.particleLimit == nvj::Settings().particleLimit);
    CHECK(older.showStatusBar);

    useDataDir(scratch);
    CHECK(writeText(scratch / "settings.json", "{ this is not json"));
    nvj::Settings broken;
    CHECK(!nvj::loadSettings(broken));
    CHECK(broken.fpsLimit == nvj::Settings().fpsLimit);
    useDataDir(scratch);
    CHECK(!nvj::loadSettings(broken));
}

void testVenue() {
    const std::string text = readText(compatDirectory() / frozen("venue"));
    CHECK(!text.empty());
    nvj::OutputSet outputs;
    std::string name;
    std::string error;
    CHECK(nvj::venueFromJson(parsed(text), name, outputs, error));
    CHECK(name == "Fixture Venue");
    CHECK(outputs[0].enabled);
    CHECK(outputs[1].enabled && outputs[1].fullscreen);
    CHECK(outputs[1].monitor == 2);
    CHECK(outputs[1].name == "Left wall");
    CHECK(outputs[1].map.slice[2] == 0.55f);
    CHECK(outputs[1].map.blend.right == 0.1f);
    CHECK(outputs[1].color.saturation == 0.8f);
    CHECK(nvj::venueToJson(name, outputs) == parsed(text));
    CHECK(nvj::venueToJson("Fixture Venue", makeVenue()) == parsed(text));

    nvj::OutputSet noVersion;
    std::string noVersionName;
    const nlohmann::json older = parsed(readText(compatDirectory() / "venue_no_version.json"));
    CHECK(older.is_object() && !older.contains("version"));
    CHECK(nvj::venueFromJson(older, noVersionName, noVersion, error));
    CHECK(noVersionName == "Old Venue");
    CHECK(noVersion[0].enabled);

    nvj::OutputSet refused;
    std::string refusedName;
    CHECK(!nvj::venueFromJson(parsed(readText(compatDirectory() / "venue_newer.json")), refusedName, refused, error));
    CHECK(error == "This venue setup was made with a newer version of NyrexVJ");
    CHECK(!nvj::venueFromJson(parsed("{\"format\":\"something-else\",\"version\":1}"), refusedName, refused, error));
    CHECK(!nvj::venueFromJson(parsed("{\"format\":\"nyrexvj-venue\",\"version\":0,\"outputs\":[{}]}"), refusedName, refused, error));
    CHECK(!nvj::venueFromJson(parsed("{\"format\":\"nyrexvj-venue\",\"version\":1,\"outputs\":[]}"), refusedName, refused, error));
}

void testCues() {
    const std::string text = readText(compatDirectory() / frozen("cues"));
    CHECK(!text.empty());
    std::vector<nvj::Cue> cues;
    CHECK(nvj::cuesFromJson(text, cues));
    CHECK(cues.size() == 3);
    if (cues.size() == 3) {
        CHECK(cues[0].seconds == 1.5 && cues[0].target == "master/level" && cues[0].value == 0.5f);
        CHECK(cues[2].seconds == 20.25 && cues[2].target == "master/blackout" && cues[2].value == 0.0f);
    }
    CHECK(parsed(nvj::cuesToJson(cues)) == parsed(text));
    CHECK(parsed(nvj::cuesToJson(makeCues())) == parsed(text));

    std::vector<nvj::Cue> older;
    CHECK(nvj::cuesFromJson(readText(compatDirectory() / "cues_older.json"), older));
    CHECK(older.size() == 2);
    if (older.size() == 2) {
        CHECK(older[0].value == 1.0f);
        CHECK(older[1].value == 1.0f);
    }
    std::vector<nvj::Cue> none;
    CHECK(!nvj::cuesFromJson("not json", none));
    CHECK(!nvj::cuesFromJson("{\"cues\":5}", none));
}

void testTimeline() {
    const std::string text = readText(compatDirectory() / frozen("timeline"));
    CHECK(!text.empty());
    nvj::ShowTimeline loaded;
    CHECK(nvj::timelineFromJson(text, loaded));
    CHECK(loaded.lanes.size() == 1);
    if (loaded.lanes.size() == 1 && loaded.lanes[0].keys.size() == 3) {
        CHECK(loaded.lanes[0].target == "master/level");
        CHECK(loaded.lanes[0].keys[1].seconds == 8.0 && loaded.lanes[0].keys[1].value == 1.0f);
        CHECK(loaded.lanes[0].keys[1].shape == nvj::KeyShape::Smooth);
        CHECK(loaded.lanes[0].keys[2].shape == nvj::KeyShape::Hold);
    } else {
        CHECK(false);
    }
    CHECK(loaded.loop.enabled && loaded.loop.start == 4.0 && loaded.loop.end == 12.0);
    CHECK(parsed(nvj::timelineToJson(loaded)) == parsed(text));
    CHECK(parsed(nvj::timelineToJson(makeTimeline())) == parsed(text));

    nvj::ShowTimeline older;
    CHECK(nvj::timelineFromJson(readText(compatDirectory() / "timeline_older.json"), older));
    CHECK(older.lanes.size() == 1);
    CHECK(older.lanes.size() == 1 && older.lanes[0].keys.size() == 2);
    if (older.lanes.size() == 1 && older.lanes[0].keys.size() == 2) {
        CHECK(older.lanes[0].keys[0].shape == nvj::KeyShape::Linear);
        CHECK(!older.lanes[0].muted);
    }
    CHECK(!older.loop.enabled);
    nvj::ShowTimeline none;
    CHECK(!nvj::timelineFromJson("", none));
    CHECK(!nvj::timelineFromJson("{\"lanes\":3}", none));
}

void testMidiProfile() {
    const std::string text = readText(compatDirectory() / frozen("midi-profile"));
    CHECK(!text.empty());
    std::vector<nvj::MidiBinding> bindings;
    std::string error;
    CHECK(nvj::profileFromJson(text, bindings, error));
    CHECK(bindings.size() == 2);
    if (bindings.size() == 2) {
        CHECK(bindings[0].kind == nvj::MidiKind::ControlChange && bindings[0].channel == 1 && bindings[0].number == 7);
        CHECK(bindings[0].target == "master/level");
        CHECK(bindings[1].kind == nvj::MidiKind::Note && bindings[1].number == 36);
    }
    CHECK(parsed(nvj::profileToJson("Fixture Controller", bindings)) == parsed(text));
    CHECK(parsed(nvj::profileToJson("Fixture Controller", makeBindings())) == parsed(text));

    std::vector<nvj::MidiBinding> refused;
    CHECK(!nvj::profileFromJson(readText(compatDirectory() / "midi-profile_newer.json"), refused, error));
    CHECK(error == "The profile was written by a newer version of NyrexVJ");
    CHECK(refused.empty());
    CHECK(!nvj::profileFromJson("{\"format\":\"nyrexvj-midi-profile\"}", refused, error));
    CHECK(error == "The profile has no valid version");
    CHECK(!nvj::profileFromJson("garbage", refused, error));
}

void testFrozenFilesAreComplete() {
    const char* kinds[] = {"composition", "settings", "venue", "cues", "timeline", "midi-profile"};
    for (const char* kind : kinds) {
        CHECK(fs::exists(compatDirectory() / frozen(kind)));
    }
    CHECK(fs::exists(compatDirectory() / "README.md"));
}

}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--write-fixtures") {
        return writeFixtures(argv[2]);
    }
    testFrozenFilesAreComplete();
    testComposition();
    testCompositionOlderShapes();
    testSettings();
    testVenue();
    testCues();
    testTimeline();
    testMidiProfile();
    return testing::finish();
}

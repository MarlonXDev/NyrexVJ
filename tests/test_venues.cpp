#include "check.hpp"

#include "core/output_map.hpp"
#include "core/venue_setup.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

nvj::OutputSet sampleSet() {
    nvj::OutputSet set = nvj::defaultOutputs();
    set[1].enabled = true;
    set[1].fullscreen = true;
    set[1].monitor = 2;
    set[1].name = "Left wall";
    set[1].map.slice = {0.0f, 0.0f, 0.55f, 1.0f};
    set[1].map.blend.right = 0.1f;
    set[1].map.blend.blackLevel = 0.03f;
    set[1].color.saturation = 0.8f;
    nvj::addSlice(set[1]);
    set[1].slices[0].map.mask = nvj::maskEllipse(0.5f, 0.5f, 0.3f, 0.3f, 16);
    nvj::resizeWarp(set[2].map, 6, 4);
    set[2].map.warp[7] = {0.02f, 0.03f};
    set[0].map.corners[2] = {0.95f, 0.9f};
    return set;
}

fs::path scratchDirectory(const char* name) {
    const fs::path path = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
    return path;
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

void testNames() {
    CHECK(nvj::sanitizeVenueName("Club Berlin") == "Club Berlin");
    CHECK(nvj::sanitizeVenueName("  Club  ") == "Club");
    CHECK(nvj::sanitizeVenueName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK(nvj::sanitizeVenueName("../../etc/passwd") == "_.._etc_passwd");
    CHECK(nvj::sanitizeVenueName("...") == "");
    CHECK(nvj::sanitizeVenueName("") == "");
    CHECK(nvj::sanitizeVenueName("CON") == "CON_");
    CHECK(nvj::sanitizeVenueName("nul.txt") == "nul.txt_");
    CHECK(nvj::sanitizeVenueName("lpt1") == "lpt1_");
    CHECK(nvj::sanitizeVenueName("console") == "console");
    CHECK(nvj::sanitizeVenueName(std::string(200, 'a')).size() == 60);
    CHECK(nvj::sanitizeVenueName(std::string("tab\there")) == "tab_here");
    CHECK(nvj::sanitizeVenueName("Stage 1.") == "Stage 1");
}

void testJsonRoundTrip() {
    const nvj::OutputSet set = sampleSet();
    const nlohmann::json doc = nvj::venueToJson("Test", set);
    CHECK(doc["format"] == "nyrexvj-venue" && doc["version"] == 1 && doc["name"] == "Test");
    CHECK(doc["outputs"].size() == nvj::kMaxOutputs);
    nvj::OutputSet back;
    std::string name;
    std::string error;
    CHECK(nvj::venueFromJson(doc, name, back, error));
    CHECK(name == "Test" && back == set);

    nlohmann::json off = doc;
    off["outputs"][0]["enabled"] = false;
    CHECK(nvj::venueFromJson(off, name, back, error) && back[0].enabled);

    nlohmann::json shortList = doc;
    shortList["outputs"] = nlohmann::json::array({doc["outputs"][1]});
    CHECK(nvj::venueFromJson(shortList, name, back, error));
    CHECK(back[0].name == "Left wall" && back[1] == nvj::defaultOutputSpec(1));
}

void testRejects() {
    nvj::OutputSet out = nvj::defaultOutputs();
    std::string name;
    std::string error;
    const nlohmann::json good = nvj::venueToJson("X", sampleSet());

    CHECK(!nvj::venueFromJson(nlohmann::json::array(), name, out, error) && !error.empty());
    CHECK(!nvj::venueFromJson(nlohmann::json::object(), name, out, error));
    nlohmann::json wrong = good;
    wrong["format"] = "other";
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong = good;
    wrong["format"] = 5;
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong = good;
    wrong["version"] = nvj::kVenueVersion + 1;
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    CHECK(error.find("newer") != std::string::npos);
    wrong["version"] = 0;
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong["version"] = "1";
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong = good;
    wrong.erase("outputs");
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong["outputs"] = nlohmann::json::array();
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong["outputs"] = "x";
    CHECK(!nvj::venueFromJson(wrong, name, out, error));
    wrong["outputs"] = nlohmann::json::array({3, 4});
    CHECK(!nvj::venueFromJson(wrong, name, out, error));

    nlohmann::json noVersion = good;
    noVersion.erase("version");
    CHECK(nvj::venueFromJson(noVersion, name, out, error));
    nlohmann::json noName = good;
    noName.erase("name");
    CHECK(nvj::venueFromJson(noName, name, out, error) && name.empty());
    nlohmann::json evilName = good;
    evilName["name"] = "../x";
    CHECK(nvj::venueFromJson(evilName, name, out, error) && name.find('/') == std::string::npos);
    nlohmann::json extra = good;
    for (int i = 0; i < 10; ++i) {
        extra["outputs"].push_back(good["outputs"][0]);
    }
    CHECK(nvj::venueFromJson(extra, name, out, error));
}

void testFiles() {
    const fs::path dir = scratchDirectory("nyrexvj_venue_files");
    const nvj::OutputSet set = sampleSet();
    std::string error;
    CHECK(nvj::listVenues(dir).empty());
    CHECK(!nvj::saveVenue(dir, "   ", set, error) && !error.empty());
    CHECK(nvj::saveVenue(dir, "Club Berlin", set, error));
    CHECK(nvj::saveVenue(dir, "arena", set, error));
    CHECK(nvj::saveVenue(dir, "Zeta", set, error));
    const std::vector<std::string> names = nvj::listVenues(dir);
    CHECK(names.size() == 3 && names[0] == "arena" && names[1] == "Club Berlin" && names[2] == "Zeta");
    CHECK(nvj::venueExists(dir, "Club Berlin") && !nvj::venueExists(dir, "Nope") && !nvj::venueExists(dir, ""));
    writeText(dir / "notes.txt", "x");
    CHECK(nvj::listVenues(dir).size() == 3);

    nvj::OutputSet loaded;
    CHECK(nvj::loadVenue(dir, "Club Berlin", loaded, error) && loaded == set);
    CHECK(!nvj::loadVenue(dir, "Nope", loaded, error) && !error.empty());
    CHECK(!nvj::loadVenue(dir, "", loaded, error));

    nvj::OutputSet changed = set;
    changed[1].name = "Changed";
    CHECK(nvj::saveVenue(dir, "Club Berlin", changed, error));
    CHECK(nvj::loadVenue(dir, "Club Berlin", loaded, error) && loaded[1].name == "Changed");
    CHECK(nvj::listVenues(dir).size() == 3);

    CHECK(nvj::duplicateVenue(dir, "Zeta", "Zeta", error));
    CHECK(nvj::venueExists(dir, "Zeta 2"));
    CHECK(nvj::duplicateVenue(dir, "Zeta", "", error));
    CHECK(nvj::venueExists(dir, "Zeta copy"));
    CHECK(!nvj::duplicateVenue(dir, "Missing", "Foo", error));
    CHECK(nvj::loadVenue(dir, "Zeta 2", loaded, error) && loaded == set);

    CHECK(nvj::deleteVenue(dir, "Zeta 2", error));
    CHECK(!nvj::venueExists(dir, "Zeta 2"));
    CHECK(!nvj::deleteVenue(dir, "Zeta 2", error));
    CHECK(!nvj::deleteVenue(dir, "", error));

    CHECK(nvj::uniqueVenueName(dir, "Fresh") == "Fresh");
    CHECK(nvj::uniqueVenueName(dir, "Zeta") == "Zeta 2");
    CHECK(nvj::uniqueVenueName(dir, "***") == "___");
    CHECK(nvj::uniqueVenueName(dir, "") == "Venue");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testUnicodeNames() {
    const fs::path dir = scratchDirectory("nyrexvj_venue_unicode");
    const nvj::OutputSet set = sampleSet();
    std::string error;
    const std::string name = "Caf\xC3\xA9 Z\xC3\xBCrich \xE2\x80\x93 Halle 2";
    CHECK(nvj::sanitizeVenueName(name) == name);
    CHECK(nvj::saveVenue(dir, name, set, error));
    const std::vector<std::string> names = nvj::listVenues(dir);
    CHECK(names.size() == 1 && names[0] == name);
    CHECK(nvj::venueExists(dir, name));
    nvj::OutputSet loaded;
    CHECK(nvj::loadVenue(dir, names[0], loaded, error) && loaded == set);
    CHECK(nvj::duplicateVenue(dir, name, std::string(), error));
    CHECK(nvj::listVenues(dir).size() == 2);
    CHECK(nvj::deleteVenue(dir, name, error) && nvj::listVenues(dir).size() == 1);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testExportImport() {
    const fs::path dir = scratchDirectory("nyrexvj_venue_library");
    const fs::path other = scratchDirectory("nyrexvj_venue_exchange");
    const nvj::OutputSet set = sampleSet();
    std::string error;
    CHECK(nvj::saveVenue(dir, "Hall", set, error));

    CHECK(nvj::exportVenue(dir, "Hall", other / "hall-copy.json", error));
    CHECK(fs::exists(other / "hall-copy.json"));
    CHECK(nvj::exportVenue(dir, "Hall", other, error));
    CHECK(fs::exists(other / "Hall.json"));
    CHECK(nvj::exportVenue(dir, "Hall", other / "nested" / "deeper" / "x", error));
    CHECK(fs::exists(other / "nested" / "deeper" / "x.json"));
    CHECK(!nvj::exportVenue(dir, "Missing", other / "m.json", error));
    CHECK(!nvj::exportVenue(dir, "Hall", fs::path(), error));

    const fs::path library2 = scratchDirectory("nyrexvj_venue_library2");
    std::string imported;
    CHECK(nvj::importVenue(library2, other / "hall-copy.json", imported, error));
    CHECK(imported == "Hall");
    nvj::OutputSet loaded;
    CHECK(nvj::loadVenue(library2, imported, loaded, error) && loaded == set);
    CHECK(nvj::importVenue(library2, other / "hall-copy.json", imported, error) && imported == "Hall 2");
    CHECK(nvj::listVenues(library2).size() == 2);

    writeText(other / "renamed-file.json", nvj::venueToJson("", set).dump());
    CHECK(nvj::importVenue(library2, other / "renamed-file.json", imported, error) && imported == "renamed-file");

    CHECK(!nvj::importVenue(library2, other / "missing.json", imported, error));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove_all(other, ec);
    fs::remove_all(library2, ec);
}

void testDamagedFiles() {
    const fs::path dir = scratchDirectory("nyrexvj_venue_damaged");
    nvj::OutputSet out = nvj::defaultOutputs();
    std::string name;
    std::string error;

    writeText(dir / "empty.json", "");
    CHECK(!nvj::loadVenueFile(dir / "empty.json", name, out, error));
    writeText(dir / "garbage.json", "{{{{ not json");
    CHECK(!nvj::loadVenueFile(dir / "garbage.json", name, out, error) && !error.empty());
    writeText(dir / "binary.json", std::string("\x00\x01\x02\xff\xfe", 5));
    CHECK(!nvj::loadVenueFile(dir / "binary.json", name, out, error));
    writeText(dir / "array.json", "[1,2,3]");
    CHECK(!nvj::loadVenueFile(dir / "array.json", name, out, error));
    writeText(dir / "foreign.json", "{\"format\":\"something\",\"version\":1,\"outputs\":[{}]}");
    CHECK(!nvj::loadVenueFile(dir / "foreign.json", name, out, error));
    writeText(dir / "newer.json", "{\"format\":\"nyrexvj-venue\",\"version\":99,\"outputs\":[{}]}");
    CHECK(!nvj::loadVenueFile(dir / "newer.json", name, out, error));
    writeText(dir / "huge.json", std::string(nvj::kMaxVenueFileBytes + 10, ' '));
    CHECK(!nvj::loadVenueFile(dir / "huge.json", name, out, error));
    CHECK(!nvj::loadVenueFile(dir / "absent.json", name, out, error));
    CHECK(!nvj::loadVenueFile(dir, name, out, error));
    writeText(dir / "minimal.json", "{\"format\":\"nyrexvj-venue\",\"outputs\":[{}]}");
    CHECK(nvj::loadVenueFile(dir / "minimal.json", name, out, error) && name == "minimal" && out[0].enabled);

    const nvj::OutputSet before = out;
    CHECK(!nvj::loadVenueFile(dir / "garbage.json", name, out, error));
    CHECK(out == before);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}

int main() {
    testNames();
    testJsonRoundTrip();
    testRejects();
    testFiles();
    testUnicodeNames();
    testExportImport();
    testDamagedFiles();
    return testing::finish();
}

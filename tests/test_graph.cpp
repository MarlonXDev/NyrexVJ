#include "check.hpp"

#include "core/composition.hpp"
#include "core/graph_plan.hpp"
#include "core/settings.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace {

using namespace nvj;
namespace fs = std::filesystem;

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

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void testSettings() {
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_graph_settings";
    useDataDir(scratch);

    const Settings defaults;
    CHECK(defaults.graphOptimizer);
    CHECK(saveSettings(defaults));
    const nlohmann::json plain = nlohmann::json::parse(readText(scratch / "settings.json"));
    CHECK(!plain.contains("graphOptimizer"));

    Settings off;
    off.graphOptimizer = false;
    CHECK(saveSettings(off));
    const nlohmann::json written = nlohmann::json::parse(readText(scratch / "settings.json"));
    CHECK(written.contains("graphOptimizer") && written["graphOptimizer"] == false);
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(!loaded.graphOptimizer);

    const auto loadWith = [&](const std::string& text) {
        writeText(scratch / "settings.json", text);
        Settings result;
        loadSettings(result);
        return result;
    };
    CHECK(loadWith("{}").graphOptimizer);
    CHECK(loadWith("{\"graphOptimizer\": true}").graphOptimizer);
    CHECK(!loadWith("{\"graphOptimizer\": false}").graphOptimizer);
    CHECK(loadWith("{\"graphOptimizer\": \"no\"}").graphOptimizer);
    CHECK(loadWith("{\"graphOptimizer\": 0}").graphOptimizer);
    CHECK(loadWith("{\"graphOptimizer\": null}").graphOptimizer);

    std::error_code ec;
    fs::remove_all(scratch, ec);
}

void testNeutral() {
    const ColorGrade neutral;
    CHECK(finalIsNeutral(neutral, 1.0f, false, false));
    CHECK(!finalIsNeutral(neutral, 1.0f, true, false));
    CHECK(!finalIsNeutral(neutral, 1.0f, false, true));
    CHECK(!finalIsNeutral(neutral, 1.0f, true, true));
    CHECK(!finalIsNeutral(neutral, 0.0f, false, false));
    CHECK(!finalIsNeutral(neutral, 0.999f, false, false));
    CHECK(!finalIsNeutral(neutral, 1.001f, false, false));
    CHECK(!finalIsNeutral(neutral, 0.9999999f, false, false));

    ColorGrade grade;
    grade.hue = 0.00001f;
    CHECK(!finalIsNeutral(grade, 1.0f, false, false));
    grade = ColorGrade{};
    grade.saturation = 0.99999f;
    CHECK(!finalIsNeutral(grade, 1.0f, false, false));
    grade = ColorGrade{};
    grade.brightness = -0.00001f;
    CHECK(!finalIsNeutral(grade, 1.0f, false, false));
    grade = ColorGrade{};
    grade.contrast = 1.00001f;
    CHECK(!finalIsNeutral(grade, 1.0f, false, false));
    grade = ColorGrade{};
    grade.hue = 0.5f;
    grade.saturation = 0.0f;
    CHECK(!finalIsNeutral(grade, 1.0f, false, false));

    for (const float level : {0.0f, 0.25f, 0.5f, 0.75f, 0.99f}) {
        CHECK(!finalIsNeutral(neutral, level, false, false));
    }
}

void testChains() {
    std::array<EffectSlot, kEffectSlots> slots{};
    CHECK(chainIsEmpty(slots, 100));
    CHECK(chainIsEmpty(slots, 0));

    for (int s = 0; s < kEffectSlots; ++s) {
        std::array<EffectSlot, kEffectSlots> one{};
        one[static_cast<size_t>(s)].effect = 3;
        one[static_cast<size_t>(s)].enabled = true;
        CHECK(!chainIsEmpty(one, 100));
        CHECK(!chainIsEmpty(one, 4));
        CHECK(chainIsEmpty(one, 3));
        CHECK(chainIsEmpty(one, 0));
        one[static_cast<size_t>(s)].enabled = false;
        CHECK(chainIsEmpty(one, 100));
        one[static_cast<size_t>(s)].enabled = true;
        one[static_cast<size_t>(s)].effect = -1;
        CHECK(chainIsEmpty(one, 100));
        one[static_cast<size_t>(s)].effect = 0;
        CHECK(!chainIsEmpty(one, 1));
        one[static_cast<size_t>(s)].effect = 99;
        CHECK(!chainIsEmpty(one, 100));
        CHECK(chainIsEmpty(one, 99));
    }

    std::array<EffectSlot, kEffectSlots> mixed{};
    mixed[0].effect = 5;
    mixed[0].enabled = false;
    mixed[1].effect = -1;
    mixed[2].effect = 500;
    mixed[3].effect = 7;
    mixed[3].enabled = true;
    CHECK(chainIsEmpty(mixed, 6));
    CHECK(!chainIsEmpty(mixed, 8));
}

void testRoutes() {
    for (const bool optimizer : {false, true}) {
        for (const bool neutral : {false, true}) {
            for (const bool emptyMaster : {false, true}) {
                for (const bool colour : {false, true}) {
                    const DeckRoute route = planDeckRoute(optimizer, neutral, emptyMaster, colour);
                    const bool plain = neutral && emptyMaster;
                    if (!optimizer) {
                        CHECK(route == DeckRoute::Final);
                    } else if (colour) {
                        CHECK(route == (plain ? DeckRoute::Direct : DeckRoute::ViaScratch));
                    } else {
                        CHECK(route == (plain ? DeckRoute::Land : DeckRoute::Final));
                    }
                }
            }
        }
    }
}

void testParity() {
    for (int swaps = 0; swaps <= 64; ++swaps) {
        const int landing = 7;
        const int other = 9;
        int accum = resultStaysInFirst(swaps) ? landing : other;
        int spare = resultStaysInFirst(swaps) ? other : landing;
        for (int i = 0; i < swaps; ++i) {
            std::swap(accum, spare);
        }
        CHECK(accum == landing);
        CHECK(spare == other);
    }
    CHECK(resultStaysInFirst(0));
    CHECK(!resultStaysInFirst(1));
    CHECK(resultStaysInFirst(2));
    CHECK(!resultStaysInFirst(7));
}

void testStats() {
    const GraphStats stats;
    CHECK(stats.frames == 0);
    CHECK(stats.finalSkipped == 0);
    CHECK(stats.copiesSkipped == 0);
}

}

int main() {
    testSettings();
    testNeutral();
    testChains();
    testRoutes();
    testParity();
    testStats();
    return testing::finish();
}

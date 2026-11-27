#include "check.hpp"

#include "control/control_targets.hpp"
#include "core/composition.hpp"
#include "core/novelty.hpp"
#include "core/node_compile.hpp"
#include "core/node_graph.hpp"
#include "core/settings.hpp"
#include "render/effect_library.hpp"
#include "render/engine.hpp"
#include "render/shader_source.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

std::uint64_t fnv(const std::string& text) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void testSettings() {
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_feedback_settings";
    useDataDir(scratch);

    const Settings defaults;
    CHECK(defaults.feedbackBudgetMb == 256);
    CHECK(defaults.feedbackReuse);
    CHECK(saveSettings(defaults));
    nlohmann::json plain = nlohmann::json::parse(readText(scratch / "settings.json"));
    CHECK(!plain.contains("feedbackBudgetMb"));
    CHECK(!plain.contains("feedbackReuse"));

    Settings changed;
    changed.feedbackBudgetMb = 512;
    changed.feedbackReuse = false;
    CHECK(saveSettings(changed));
    nlohmann::json written = nlohmann::json::parse(readText(scratch / "settings.json"));
    CHECK(written.value("feedbackBudgetMb", 0) == 512);
    CHECK(written.contains("feedbackReuse") && written["feedbackReuse"] == false);
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(loaded.feedbackBudgetMb == 512);
    CHECK(!loaded.feedbackReuse);

    changed.feedbackBudgetMb = 0;
    CHECK(saveSettings(changed));
    Settings off;
    CHECK(loadSettings(off));
    CHECK(off.feedbackBudgetMb == 0);

    const auto loadWith = [&](const std::string& text) {
        writeText(scratch / "settings.json", text);
        Settings result;
        loadSettings(result);
        return result;
    };
    CHECK(loadWith("{\"feedbackBudgetMb\": 10}").feedbackBudgetMb == 64);
    CHECK(loadWith("{\"feedbackBudgetMb\": 99999}").feedbackBudgetMb == 8192);
    CHECK(loadWith("{\"feedbackBudgetMb\": -5}").feedbackBudgetMb == 0);
    CHECK(loadWith("{\"feedbackBudgetMb\": \"big\"}").feedbackBudgetMb == 256);
    CHECK(loadWith("{\"feedbackBudgetMb\": 12.5}").feedbackBudgetMb == 256);
    CHECK(loadWith("{\"feedbackReuse\": \"yes\"}").feedbackReuse);
    CHECK(loadWith("{}").feedbackBudgetMb == 256);
    CHECK(loadWith("{}").feedbackReuse);

    std::error_code ec;
    fs::remove_all(scratch, ec);
}

class RecordingHost final : public ControlHost {
public:
    void triggerClip(int, int) override {}
    void triggerColumn(int) override {}
    void clearLayer(int) override {}
    void switchDeck(int) override {}
    void tap() override {}
    void resync() override {}
    bool blackout() const override { return false; }
    void setBlackout(bool) override {}
    bool feedbackFrozen() const override { return frozen; }
    void setFeedbackFrozen(bool on) override { frozen = on; ++changes; }
    void resetFeedback() override { ++resets; }

    bool frozen = false;
    int changes = 0;
    int resets = 0;
};

class BareHost final : public ControlHost {
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

void testControlTargets() {
    const std::vector<ControlTarget>& targets = controlTargets();
    const int freeze = controlTargetIndex("master/feedback-freeze");
    const int reset = controlTargetIndex("master/feedback-reset");
    CHECK(freeze >= 0 && reset >= 0);
    CHECK(freeze == static_cast<int>(targets.size()) - 2);
    CHECK(reset == static_cast<int>(targets.size()) - 1);
    CHECK(targets[static_cast<size_t>(freeze)].kind == ControlKind::Toggle);
    CHECK(targets[static_cast<size_t>(freeze)].item == ControlItem::FeedbackFreeze);
    CHECK(targets[static_cast<size_t>(reset)].kind == ControlKind::Trigger);
    CHECK(targets[static_cast<size_t>(reset)].item == ControlItem::FeedbackReset);
    CHECK(controlTargetIndex("master/level") == 0);
    CHECK(controlTargetIndex("master/blackout") == 1);
    CHECK(controlTargetIndex("master/tap") == 3);
    CHECK(controlTargetIndex("master/resync") == 4);

    Composition comp;
    RecordingHost host;
    ControlRouter router(comp, host);
    CHECK(!router.state(freeze));
    CHECK(router.set(freeze, 1.0f, false));
    CHECK(host.frozen);
    CHECK(router.state(freeze));
    CHECK(router.toggle(freeze));
    CHECK(!host.frozen);
    CHECK(!router.state(freeze));
    CHECK(host.changes == 2);
    CHECK(!router.toggle(reset));
    CHECK(host.resets == 0);
    CHECK(router.set(reset, 1.0f, false));
    CHECK(host.resets == 1);
    CHECK(router.set(reset, 1.0f, false));
    CHECK(host.resets == 2);

    BareHost bare;
    ControlRouter quiet(comp, bare);
    CHECK(quiet.set(freeze, 1.0f, false));
    CHECK(!quiet.state(freeze));
    CHECK(quiet.set(reset, 1.0f, false));
}

void testEffects() {
    const std::vector<EffectDef>& library = effectLibrary();
    const char* names[] = {"Time Echo", "Motion Smear", "Feedback Transform", "Accumulate"};
    const int glass = effectIndexByName("Prism Glass");
    CHECK(glass >= 0);
    int previous = glass;
    for (const char* name : names) {
        const int index = effectIndexByName(name);
        CHECK(index >= 0);
        if (index < 0) {
            continue;
        }
        const EffectDef& def = library[static_cast<size_t>(index)];
        CHECK(def.usesPrevious);
        CHECK(!def.is3d);
        CHECK(def.paramCount() == 4);
        CHECK(index == previous + 1);
        previous = index;
        for (const float value : def.defaults) {
            CHECK(value >= 0.0f && value <= 1.0f);
        }
        const std::string& source = effectFragmentSource(index);
        CHECK(contains(source, "uniform float uDelta;"));
        CHECK(contains(source, "uniform sampler2D uPrev;"));
        CHECK(contains(source, "fbKeep("));
        CHECK(contains(source, "fbRound("));
        CHECK(isNewEffectIn(name, "0.66.0"));
        CHECK(!isNewEffectIn(name, "0.65.0"));
        CHECK(!isNewEffectIn(name, "0.67.0"));
    }
    for (size_t i = 0; i < library.size(); ++i) {
        for (size_t j = i + 1; j < library.size(); ++j) {
            CHECK(library[i].name != library[j].name);
        }
    }

    const int trails = effectIndexByName("Trails");
    const int hue = effectIndexByName("Hue Feedback");
    CHECK(trails >= 0 && hue >= 0);
    CHECK(fnv(library[static_cast<size_t>(trails)].body) == 0x9d978c4ea30029a0ull);
    CHECK(fnv(library[static_cast<size_t>(hue)].body) == 0x1be913d0574a9380ull);
    CHECK(library[static_cast<size_t>(trails)].usesPrevious);
    CHECK(library[static_cast<size_t>(hue)].usesPrevious);

    int feedbackEffects = 0;
    for (const EffectDef& def : library) {
        feedbackEffects += def.usesPrevious ? 1 : 0;
    }
    CHECK(feedbackEffects == 6);
}

void testBudgetMath() {
    const unsigned long long full = 1920ull * 1080ull * 4ull;
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, full) == 1.0f);
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, full - 1) == 0.75f);
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, full / 2) == 0.5f);
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, full / 4) == 0.5f);
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, full / 16) == 0.25f);
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, full / 16 - 1) == 0.0f);
    CHECK(fitShaderScale(1.0f, 1920, 1080, 4, 1, 0) == 0.0f);

    FeedbackStats stats;
    stats.liveBytes = 100;
    stats.pooledBytes = 28;
    CHECK(stats.total() == 128);
    CHECK(kDefaultFeedbackBudgetBytes == 256ull * 1024ull * 1024ull);
    CHECK(kFeedbackPoolMax >= 4);
    GpuMemory memory;
    memory.feedback = 1000;
    memory.targets = 24;
    CHECK(memory.total() == 1024);
}

void testGraphs() {
    GraphDocument feedback;
    const int first = feedback.addNode("previous", 0, 0);
    const int second = feedback.addNode("previous", 0, 100);
    const int mix = feedback.addNode("max", 100, 0);
    const int out = feedback.addNode("output", 200, 0);
    CHECK(feedback.connect(first, 0, mix, 0).empty());
    CHECK(feedback.connect(second, 0, mix, 1).empty());
    CHECK(feedback.connect(mix, 0, out, 0).empty());
    const CompiledGraph compiled = compileGraph(feedback);
    CHECK(compiled.ok);
    CHECK(compiled.usesPrevious);
    CHECK(contains(compiled.glsl, "uPrev"));

    GraphDocument chain;
    const int origin = chain.addNode("previous", 0, 0);
    int last = origin;
    int firstMax = -1;
    const int depth = static_cast<int>(kMaxGraphNodes) - 2;
    for (int i = 1; i < depth; ++i) {
        const int node = chain.addNode("max", static_cast<float>(i), 0);
        CHECK(node >= 0);
        if (node < 0) {
            break;
        }
        if (firstMax < 0) {
            firstMax = node;
        }
        CHECK(chain.connect(last, 0, node, 0).empty());
        last = node;
    }
    const int result = chain.addNode("output", 0, 100);
    CHECK(result >= 0);
    CHECK(chain.addNode("max", 0, 0) >= 0);
    CHECK(chain.addNode("max", 0, 0) < 0);
    CHECK(chain.connect(last, 0, result, 0).empty());
    const auto begin = std::chrono::steady_clock::now();
    const CompiledGraph deep = compileGraph(chain);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    CHECK(deep.ok);
    CHECK(deep.usesPrevious);
    CHECK(seconds < 5.0);

    CHECK(!chain.connect(last, 0, firstMax, 1).empty());
    CHECK(contains(chain.connect(last, 0, firstMax, 1), "loop"));
    GraphDocument forced = chain;
    forced.links.push_back({last, 0, firstMax, 1});
    const auto loopBegin = std::chrono::steady_clock::now();
    const CompiledGraph looped = compileGraph(forced);
    const double loopSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - loopBegin).count();
    CHECK(!looped.ok);
    CHECK(contains(looped.firstError(), "loop"));
    CHECK(loopSeconds < 5.0);
    CHECK(!chain.connect(origin, 0, origin, 0).empty());
}

}

int main() {
    testSettings();
    testControlTargets();
    testEffects();
    testBudgetMath();
    testGraphs();
    return testing::finish();
}

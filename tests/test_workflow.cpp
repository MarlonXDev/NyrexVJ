#include "check.hpp"
#include "control/control_system.hpp"
#include "control/control_targets.hpp"
#include "control/midi_feedback.hpp"
#include "control/midi_map.hpp"
#include "control/midi_output.hpp"
#include "core/composition.hpp"
#include "core/launch_quantizer.hpp"
#include "core/library_meta.hpp"
#include "core/log.hpp"
#include "core/settings.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace nvj;

namespace {

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

std::filesystem::path scratchDirectory(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / (name + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return dir;
}

void useDataDir(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

void testTags() {
    CHECK(normalizeTag("  Intro Loop ") == "intro-loop");
    CHECK(normalizeTag("#Bass") == "bass");
    CHECK(normalizeTag("a\"b\\c") == "abc");
    CHECK(normalizeTag("   ").empty());
    CHECK(normalizeTag("###").empty());
    CHECK(normalizeTag(std::string(60, 'x')).size() == kMaxTagLength);
    const std::string umlauts = normalizeTag("ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ");
    CHECK(umlauts.size() <= kMaxTagLength);
    CHECK(umlauts.size() % 2 == 0);
}

void testMeta() {
    LibraryMeta meta;
    CHECK(!meta.favorite("Plasma"));
    meta.setFavorite("Plasma", true);
    CHECK(meta.favorite("Plasma"));
    CHECK(meta.dirty());
    CHECK(meta.size() == 1);
    meta.setFavorite("Plasma", false);
    CHECK(!meta.favorite("Plasma"));
    CHECK(meta.size() == 0);

    CHECK(meta.addTag("Plasma", "Intro"));
    CHECK(!meta.addTag("Plasma", "intro"));
    CHECK(!meta.addTag("Plasma", "  "));
    CHECK(meta.tags("Plasma").size() == 1);
    for (int i = 0; i < 20; ++i) {
        meta.addTag("Plasma", "tag" + std::to_string(i));
    }
    CHECK(meta.tags("Plasma").size() == kMaxTagsPerClip);
    CHECK(meta.removeTag("Plasma", "INTRO"));
    CHECK(!meta.removeTag("Plasma", "intro"));
    CHECK(meta.tags("Plasma").size() == kMaxTagsPerClip - 1);
    CHECK(meta.tags("Unknown").empty());

    meta.addTag("Tunnel", "bass");
    meta.addTag("Tunnel", "intro");
    const std::vector<std::string> all = meta.allTags();
    CHECK(!all.empty());
    for (size_t i = 1; i < all.size(); ++i) {
        CHECK(all[i - 1] < all[i]);
    }

    LibraryMeta big;
    for (size_t i = 0; i < kMaxMetaClips + 50; ++i) {
        big.setFavorite("clip" + std::to_string(i), true);
    }
    CHECK(big.size() == kMaxMetaClips);
    CHECK(!big.addTag("one more", "x"));
    big.setFavorite(std::string(300, 'n'), true);
    CHECK(big.size() == kMaxMetaClips);

    LibraryMeta renamed;
    renamed.setFavorite("Old", true);
    renamed.addTag("Old", "x");
    renamed.rename("Old", "New");
    CHECK(!renamed.favorite("Old") && renamed.favorite("New"));
    CHECK(renamed.tags("New").size() == 1);
    renamed.setFavorite("Other", true);
    renamed.rename("New", "Other");
    CHECK(renamed.favorite("New"));
}

void testQuery() {
    LibraryMeta meta;
    meta.setFavorite("Plasma Wave", true);
    meta.addTag("Plasma Wave", "Intro");
    meta.addTag("Tunnel Run", "intro");
    meta.addTag("Tunnel Run", "bass");

    CHECK(meta.matches("Plasma Wave", parseLibraryQuery("")));
    CHECK(meta.matches("Plasma Wave", parseLibraryQuery("plasma")));
    CHECK(meta.matches("Plasma Wave", parseLibraryQuery("PLASMA wave")));
    CHECK(!meta.matches("Plasma Wave", parseLibraryQuery("plasma tunnel")));
    CHECK(meta.matches("Tunnel Run", parseLibraryQuery("tag:intro")));
    CHECK(meta.matches("Tunnel Run", parseLibraryQuery("tag:int")));
    CHECK(!meta.matches("Tunnel Run", parseLibraryQuery("tag:introx")));
    CHECK(meta.matches("Tunnel Run", parseLibraryQuery("tag:intro tag:bass")));
    CHECK(!meta.matches("Plasma Wave", parseLibraryQuery("tag:intro tag:bass")));
    CHECK(meta.matches("Plasma Wave", parseLibraryQuery("is:fav")));
    CHECK(!meta.matches("Tunnel Run", parseLibraryQuery("is:fav")));
    CHECK(meta.matches("Tunnel Run", parseLibraryQuery("run bass")));
    CHECK(meta.matches("Plasma Wave", parseLibraryQuery("tag:INTRO plasma")));
    CHECK(meta.matches("Unknown clip", parseLibraryQuery("clip")));
    CHECK(!meta.matches("Unknown clip", parseLibraryQuery("is:fav")));
    const LibraryQuery empty = parseLibraryQuery("tag:   is:fav");
    CHECK(empty.favoritesOnly);
    CHECK(empty.words.empty());
}

void testPersistence() {
    const std::filesystem::path dir = scratchDirectory("nvj_meta_");
    const std::filesystem::path file = dir / "library_meta.json";
    std::string error;

    LibraryMeta none;
    CHECK(loadLibraryMetaFile(none, file, error));
    CHECK(none.size() == 0);

    LibraryMeta meta;
    meta.setFavorite("Plasma", true);
    meta.addTag("Plasma", "Intro");
    meta.addTag("Tunnel", "bass");
    CHECK(saveLibraryMetaFile(meta, file, error));
    CHECK(!std::filesystem::exists(dir / "library_meta.json.tmp"));

    LibraryMeta back;
    CHECK(loadLibraryMetaFile(back, file, error));
    CHECK(back.favorite("Plasma"));
    CHECK(back.tags("Plasma").size() == 1 && back.tags("Plasma")[0] == "intro");
    CHECK(back.tags("Tunnel").size() == 1);
    CHECK(!back.dirty());
    CHECK(back.toJson() == meta.toJson());

    {
        std::ofstream out(file, std::ios::trunc);
        out << "{ this is not json";
    }
    LibraryMeta broken;
    broken.setFavorite("Keep", true);
    CHECK(!loadLibraryMetaFile(broken, file, error));
    CHECK(!std::filesystem::exists(file));
    CHECK(std::filesystem::exists(dir / "library_meta.broken.json"));

    LibraryMeta newer;
    CHECK(!newer.fromJson("{\"format\":\"nyrexvj-library\",\"version\":2,\"clips\":{}}", error));
    CHECK(error.find("newer") != std::string::npos);
    CHECK(!newer.fromJson("{\"format\":\"other\",\"version\":1,\"clips\":{}}", error));
    CHECK(!newer.fromJson("{\"format\":\"nyrexvj-library\",\"version\":0,\"clips\":{}}", error));
    CHECK(!newer.fromJson("{\"format\":\"nyrexvj-library\",\"version\":1}", error));
    CHECK(!newer.fromJson(std::string(kMaxMetaFileBytes + 1, ' '), error));

    LibraryMeta tolerant;
    CHECK(tolerant.fromJson("{\"format\":\"nyrexvj-library\",\"version\":1,\"future\":1,\"clips\":{\"A\":{\"favorite\":true,\"tags\":[\"x\",\"X\",3,\"  \",\"y\"],\"more\":1},\"\":{\"favorite\":true},\"B\":5,\"C\":{}}}", error));
    CHECK(tolerant.size() == 1);
    CHECK(tolerant.favorite("A"));
    CHECK(tolerant.tags("A").size() == 2);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

Composition sampleComposition() {
    Composition comp;
    comp.layers[0].setCell(0, 3);
    comp.layers[0].setCell(1, 4);
    comp.layers[1].setCell(0, 3);
    comp.layers[0].playback[0].speed = 2.0f;
    comp.layers[0].playback[0].in = 1.5;
    comp.layers[0].fx[0].effect = 5;
    comp.layers[0].opacity = 0.4f;
    comp.layers[0].blend = BlendAdd;
    comp.decks[2].layers[3].setCell(7, 3);
    comp.decks[2].layers[3].playback[7].speed = -1.0f;
    comp.deckUsed[2] = true;
    return comp;
}

void testReplace() {
    Composition comp = sampleComposition();
    const int changed = replaceClipEverywhere(comp, 3, 9, true);
    CHECK(changed == 3);
    CHECK(comp.layers[0].cells[0] == 9);
    CHECK(comp.layers[1].cells[0] == 9);
    CHECK(comp.layers[0].cells[1] == 4);
    CHECK(comp.decks[2].layers[3].cells[7] == 9);
    CHECK(comp.layers[0].playback[0].speed == 2.0f);
    CHECK(comp.layers[0].playback[0].in == 1.5);
    CHECK(comp.decks[2].layers[3].playback[7].speed == -1.0f);
    CHECK(comp.layers[0].fx[0].effect == 5);
    CHECK(comp.layers[0].opacity == 0.4f);
    CHECK(comp.layers[0].blend == BlendAdd);

    Composition reset = sampleComposition();
    CHECK(replaceClipEverywhere(reset, 3, 9, false) == 3);
    CHECK(reset.layers[0].cells[0] == 9);
    CHECK(reset.layers[0].playback[0].isDefault());
    CHECK(reset.layers[0].fx[0].effect == 5);

    Composition same = sampleComposition();
    CHECK(replaceClipEverywhere(same, 3, 3, true) == 0);
    CHECK(replaceClipEverywhere(same, -1, 3, true) == 0);
    CHECK(replaceClipEverywhere(same, 3, -1, true) == 0);
    CHECK(replaceClipEverywhere(same, 77, 3, true) == 0);
    CHECK(same.layers[0].cells[0] == 3);

    Composition active = sampleComposition();
    active.activeDeck = 2;
    active.layers[2].setCell(2, 3);
    active.decks[2].layers[2].setCell(2, 3);
    CHECK(replaceClipEverywhere(active, 3, 8, true) == 3);
    CHECK(active.layers[2].cells[2] == 8);
    CHECK(active.decks[2].layers[2].cells[2] == 8);
}

QuantizeClock clockAt(double beat, double bpm = 120.0, int bar = 4, bool usable = true) {
    QuantizeClock clock;
    clock.beat = beat;
    clock.bpm = bpm;
    clock.beatsPerBar = bar;
    clock.usable = usable;
    return clock;
}

void testQuantizerBasics() {
    CHECK(quantizeGridFromName("beat") == QuantizeGrid::Beat);
    CHECK(quantizeGridFromName("4bars") == QuantizeGrid::FourBars);
    CHECK(quantizeGridFromName("nonsense") == QuantizeGrid::Off);
    CHECK(quantizeGridFromName(nullptr) == QuantizeGrid::Off);
    for (int i = 0; i < kQuantizeGridCount; ++i) {
        const QuantizeGrid grid = static_cast<QuantizeGrid>(i);
        CHECK(quantizeGridFromName(quantizeGridName(grid)) == grid);
    }
    CHECK(quantizeGridBeats(QuantizeGrid::Off, 4) == 0.0);
    CHECK(quantizeGridBeats(QuantizeGrid::Beat, 4) == 1.0);
    CHECK(quantizeGridBeats(QuantizeGrid::Bar, 3) == 3.0);
    CHECK(quantizeGridBeats(QuantizeGrid::TwoBars, 4) == 8.0);
    CHECK(quantizeGridBeats(QuantizeGrid::FourBars, 4) == 16.0);

    LaunchQuantizer off;
    std::vector<LaunchRequest> now;
    CHECK(!off.request({LaunchKind::Clip, 1, 2}, clockAt(2.3), 0.0, now));
    CHECK(now.size() == 1 && now[0].a == 1 && now[0].b == 2);
    CHECK(off.pending() == 0);
    CHECK(off.executedImmediately() == 1);

    LaunchQuantizer beat;
    beat.setGrid(QuantizeGrid::Beat);
    now.clear();
    CHECK(beat.request({LaunchKind::Clip, 0, 3}, clockAt(2.3), 10.0, now));
    CHECK(now.empty());
    CHECK(beat.pending() == 1);
    std::vector<LaunchRequest> due;
    beat.update(clockAt(2.9), 10.3, due);
    CHECK(due.empty());
    beat.update(clockAt(3.0), 10.35, due);
    CHECK(due.size() == 1 && due[0].b == 3);
    CHECK(beat.pending() == 0);
    CHECK(beat.executedOnBeat() == 1);

    now.clear();
    CHECK(!beat.request({LaunchKind::Column, 4, 0}, clockAt(5.0), 12.0, now));
    CHECK(now.size() == 1);
    CHECK(beat.executedOnBeat() == 2);

    LaunchQuantizer bar;
    bar.setGrid(QuantizeGrid::Bar);
    now.clear();
    CHECK(bar.request({LaunchKind::Deck, 5, 0}, clockAt(5.5), 0.0, now));
    due.clear();
    bar.update(clockAt(7.99), 1.0, due);
    CHECK(due.empty());
    bar.update(clockAt(8.0), 1.1, due);
    CHECK(due.size() == 1 && due[0].kind == LaunchKind::Deck && due[0].a == 5);
    CHECK(bar.secondsToNext(clockAt(0.0)) < 0.0);

    LaunchQuantizer three;
    three.setGrid(QuantizeGrid::Bar);
    now.clear();
    CHECK(three.request({LaunchKind::Clip, 0, 0}, clockAt(4.2, 120.0, 3), 0.0, now));
    due.clear();
    three.update(clockAt(5.9, 120.0, 3), 1.0, due);
    CHECK(due.empty());
    three.update(clockAt(6.0, 120.0, 3), 1.2, due);
    CHECK(due.size() == 1);

    LaunchQuantizer four;
    four.setGrid(QuantizeGrid::FourBars);
    now.clear();
    CHECK(four.request({LaunchKind::Clip, 0, 0}, clockAt(1.0), 0.0, now));
    CHECK(near(four.secondsToNext(clockAt(1.0)), 7.5, 1e-9));
    due.clear();
    four.update(clockAt(15.99), 7.0, due);
    CHECK(due.empty());
    four.update(clockAt(16.01), 7.6, due);
    CHECK(due.size() == 1);
}

void testQuantizerSlots() {
    LaunchQuantizer q;
    q.setGrid(QuantizeGrid::Bar);
    std::vector<LaunchRequest> now;
    CHECK(q.request({LaunchKind::Clip, 0, 1}, clockAt(0.5), 0.0, now));
    CHECK(q.request({LaunchKind::Clip, 0, 2}, clockAt(0.6), 0.1, now));
    CHECK(q.pending() == 1);
    CHECK(q.replaced() == 1);
    CHECK(q.request({LaunchKind::Clip, 1, 5}, clockAt(0.7), 0.2, now));
    CHECK(q.pending() == 2);
    CHECK(q.request({LaunchKind::Column, 3, 0}, clockAt(0.8), 0.3, now));
    CHECK(q.request({LaunchKind::Column, 4, 0}, clockAt(0.9), 0.4, now));
    CHECK(q.pending() == 3);
    CHECK(q.request({LaunchKind::Deck, 1, 0}, clockAt(1.0), 0.5, now));
    CHECK(q.request({LaunchKind::Deck, 2, 0}, clockAt(1.1), 0.6, now));
    CHECK(q.pending() == 4);
    std::vector<LaunchRequest> due;
    q.update(clockAt(4.0), 1.0, due);
    CHECK(due.size() == 4);
    CHECK(due[0].kind == LaunchKind::Clip && due[0].a == 0 && due[0].b == 2);
    CHECK(due[1].kind == LaunchKind::Clip && due[1].a == 1);
    CHECK(due[2].kind == LaunchKind::Column && due[2].a == 4);
    CHECK(due[3].kind == LaunchKind::Deck && due[3].a == 2);

    LaunchQuantizer full;
    full.setGrid(QuantizeGrid::FourBars);
    now.clear();
    for (size_t i = 0; i < kMaxPendingLaunches; ++i) {
        CHECK(full.request({LaunchKind::Clip, static_cast<int>(i), 0}, clockAt(0.5), 0.0, now));
    }
    CHECK(full.pending() == kMaxPendingLaunches);
    CHECK(now.empty());
    CHECK(!full.request({LaunchKind::Clip, 99, 0}, clockAt(0.5), 0.0, now));
    CHECK(now.size() == 1 && now[0].a == 99);
    CHECK(full.pending() == kMaxPendingLaunches);
    full.cancelAll();
    CHECK(full.pending() == 0);
}

void testQuantizerClockLoss() {
    LaunchQuantizer q;
    q.setGrid(QuantizeGrid::Bar);
    std::vector<LaunchRequest> now;
    CHECK(q.request({LaunchKind::Clip, 0, 1}, clockAt(0.5), 0.0, now));
    CHECK(q.request({LaunchKind::Deck, 3, 0}, clockAt(0.6), 0.1, now));
    std::vector<LaunchRequest> due;
    q.update(clockAt(1.0, 120.0, 4, false), 0.5, due);
    CHECK(due.size() == 2);
    CHECK(q.pending() == 0);
    CHECK(q.executedAfterClockLoss() == 2);

    now.clear();
    CHECK(!q.request({LaunchKind::Clip, 0, 1}, clockAt(2.0, 120.0, 4, false), 1.0, now));
    CHECK(now.size() == 1);
    CHECK(q.executedAfterClockLoss() == 3);

    now.clear();
    CHECK(!q.request({LaunchKind::Clip, 0, 1}, clockAt(2.0, 0.0), 1.0, now));
    CHECK(!q.request({LaunchKind::Clip, 0, 1}, clockAt(std::nan(""), 120.0), 1.0, now));
    CHECK(now.size() == 2);

    CHECK(q.request({LaunchKind::Clip, 2, 1}, clockAt(2.0), 2.0, now));
    due.clear();
    q.update(clockAt(2.0, 120.0, 4, true), 2.0, due);
    CHECK(due.empty());
    q.update(clockAt(3.0, 120.0, 4, false), 2.5, due);
    CHECK(due.size() == 1);

    LaunchQuantizer frozen;
    frozen.setGrid(QuantizeGrid::Beat);
    now.clear();
    CHECK(frozen.request({LaunchKind::Clip, 0, 0}, clockAt(2.3), 10.0, now));
    due.clear();
    frozen.update(clockAt(2.3), 10.5, due);
    CHECK(due.empty());
    frozen.update(clockAt(2.3), 11.1, due);
    CHECK(due.size() == 1);
    CHECK(frozen.executedAfterTimeout() == 1);

    LaunchQuantizer jump;
    jump.setGrid(QuantizeGrid::Bar);
    now.clear();
    CHECK(jump.request({LaunchKind::Clip, 0, 0}, clockAt(5.5), 0.0, now));
    due.clear();
    jump.update(clockAt(0.5), 0.1, due);
    CHECK(due.empty());
    jump.update(clockAt(3.9), 1.5, due);
    CHECK(due.empty());
    jump.update(clockAt(4.0), 1.6, due);
    CHECK(due.size() == 1);

    LaunchQuantizer switchedOff;
    switchedOff.setGrid(QuantizeGrid::TwoBars);
    now.clear();
    CHECK(switchedOff.request({LaunchKind::Column, 1, 0}, clockAt(0.5), 0.0, now));
    switchedOff.setGrid(QuantizeGrid::Off);
    due.clear();
    switchedOff.update(clockAt(0.6), 0.1, due);
    CHECK(due.size() == 1);
}

void testSettings() {
    const std::filesystem::path dir = scratchDirectory("nvj_workflow_settings_");
    useDataDir(dir);
    Settings defaults;
    CHECK(defaults.launchQuantize == "off");
    CHECK(!defaults.midiFeedback);
    CHECK(defaults.midiOutDevice.empty());

    {
        std::ofstream file(dir / "settings.json", std::ios::trunc);
        file << "{\"language\":\"de\",\"fpsLimit\":30}";
    }
    Settings old;
    CHECK(loadSettings(old));
    CHECK(old.launchQuantize == "off");
    CHECK(!old.midiFeedback);
    CHECK(old.fpsLimit == 30);

    Settings wanted;
    wanted.launchQuantize = "2bars";
    wanted.midiFeedback = true;
    wanted.midiOutDevice = "My Controller";
    CHECK(saveSettings(wanted));
    Settings back;
    CHECK(loadSettings(back));
    CHECK(back.launchQuantize == "2bars");
    CHECK(back.midiFeedback);
    CHECK(back.midiOutDevice == "My Controller");

    {
        std::ofstream file(dir / "settings.json", std::ios::trunc);
        file << "{\"launchQuantize\":\"sometimes\",\"midiFeedback\":\"yes\",\"midiOutDevice\":7}";
    }
    Settings odd;
    CHECK(loadSettings(odd));
    CHECK(odd.launchQuantize == "off");
    CHECK(!odd.midiFeedback);
    CHECK(odd.midiOutDevice.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

class QuietHost final : public ControlHost {
public:
    void triggerClip(int, int) override {}
    void triggerColumn(int) override {}
    void clearLayer(int) override {}
    void switchDeck(int) override {}
    void tap() override {}
    void resync() override {}
    bool blackout() const override { return dark; }
    void setBlackout(bool on) override { dark = on; }
    bool dark = false;
};

struct Sent {
    std::vector<MidiMessage> messages;
    bool failing = false;
};

class RecordingOutput final : public MidiOutput {
public:
    explicit RecordingOutput(Sent& sent) : sent_(sent) {}
    std::vector<std::string> listDevices() override { return {"Pad One", "Pad Two"}; }
    bool open(const std::string& name, std::string& error) override {
        if (name != "Pad One" && name != "Pad Two") {
            error = "The MIDI output device was not found";
            return false;
        }
        open_ = true;
        return true;
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    bool send(const MidiMessage& message) override {
        if (sent_.failing) {
            return false;
        }
        sent_.messages.push_back(message);
        return true;
    }

private:
    Sent& sent_;
    bool open_ = false;
};

MidiBinding noteBinding(const std::string& target, int number, int channel = 1) {
    MidiBinding binding;
    binding.kind = MidiKind::Note;
    binding.channel = channel;
    binding.number = number;
    binding.target = target;
    return binding;
}

MidiBinding ccBinding(const std::string& target, int number, float low = 0.0f, float high = 1.0f) {
    MidiBinding binding;
    binding.kind = MidiKind::ControlChange;
    binding.channel = 1;
    binding.number = number;
    binding.target = target;
    binding.low = low;
    binding.high = high;
    return binding;
}

void testLit() {
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    const int clip = controlTargetIndex("layer/1/clip/3");
    const int other = controlTargetIndex("layer/1/clip/4");
    const int column = controlTargetIndex("column/3");
    const int emptyColumn = controlTargetIndex("column/5");
    const int deck = controlTargetIndex("deck/1");
    const int deckTwo = controlTargetIndex("deck/2");
    const int bypass = controlTargetIndex("layer/2/bypass");
    const int blackout = controlTargetIndex("master/blackout");
    CHECK(clip >= 0 && other >= 0 && column >= 0 && emptyColumn >= 0 && deck >= 0 && deckTwo >= 0 && bypass >= 0 && blackout >= 0);

    CHECK(!router.lit(clip));
    CHECK(!router.lit(column));
    comp.layers[0].cells[2] = 0;
    comp.layers[0].activeColumn = 2;
    CHECK(router.lit(clip));
    CHECK(!router.lit(other));
    CHECK(router.lit(column));
    CHECK(!router.lit(emptyColumn));
    comp.layers[0].activeColumn = 3;
    CHECK(!router.lit(clip));
    comp.layers[0].activeColumn = 2;
    comp.layers[0].cells[2] = -1;
    CHECK(!router.lit(clip));

    CHECK(router.lit(deck) == (comp.activeDeck == 0));
    CHECK(router.lit(deckTwo) == (comp.activeDeck == 1));
    CHECK(!router.lit(-1));
    CHECK(!router.lit(1000000));

    CHECK(!router.lit(bypass));
    comp.layers[1].bypass = true;
    CHECK(router.lit(bypass));
    CHECK(!router.lit(blackout));
    host.dark = true;
    CHECK(router.lit(blackout));
}

void testFeedbackCompute() {
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    MidiFeedback feedback;
    comp.layers[0].cells[0] = 0;
    comp.layers[0].activeColumn = 0;
    std::vector<MidiBinding> bindings{noteBinding("layer/1/clip/1", 36), noteBinding("layer/1/clip/2", 37), ccBinding("layer/1/opacity", 7)};
    comp.layers[0].opacity = 1.0f;

    std::vector<MidiMessage> out;
    feedback.compute(bindings, router, out);
    CHECK(out.size() == 3);
    CHECK(out[0].status == 0x90 && out[0].data1 == 36 && out[0].data2 == 127);
    CHECK(out[1].status == 0x90 && out[1].data1 == 37 && out[1].data2 == 0);
    CHECK(out[2].status == 0xB0 && out[2].data1 == 7 && out[2].data2 == 127);
    CHECK(feedback.tracked() == 3);

    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.empty());

    comp.layers[0].activeColumn = 1;
    comp.layers[0].cells[1] = 0;
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.size() == 2);
    CHECK(out[0].data1 == 36 && out[0].data2 == 0);
    CHECK(out[1].data1 == 37 && out[1].data2 == 127);

    comp.layers[0].opacity = 0.5f;
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.size() == 1);
    CHECK(out[0].status == 0xB0 && out[0].data2 >= 63 && out[0].data2 <= 64);

    comp.layers[0].opacity = 0.25f;
    out.clear();
    feedback.compute(bindings, router, out, false);
    CHECK(out.empty());
    feedback.compute(bindings, router, out, true);
    CHECK(out.size() == 1);

    feedback.reset();
    CHECK(feedback.tracked() == 0);
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.size() == 3);

    bindings.pop_back();
    bindings.pop_back();
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(feedback.tracked() == 1);
}

void testFeedbackRangeAndChannels() {
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    MidiFeedback feedback;
    std::vector<MidiBinding> bindings;
    bindings.push_back(ccBinding("layer/1/opacity", 10, 0.0f, 0.5f));
    MidiBinding same = ccBinding("layer/2/opacity", 10);
    bindings.push_back(same);
    MidiBinding channelTwo = noteBinding("layer/3/bypass", 20, 2);
    bindings.push_back(channelTwo);
    MidiBinding flat = ccBinding("layer/3/opacity", 11, 0.4f, 0.4f);
    bindings.push_back(flat);
    MidiBinding broken = noteBinding("no/such/target", 5);
    bindings.push_back(broken);
    MidiBinding noteOnFader = noteBinding("layer/4/opacity", 6);
    bindings.push_back(noteOnFader);

    comp.layers[0].opacity = 0.5f;
    comp.layers[2].bypass = true;
    std::vector<MidiMessage> out;
    feedback.compute(bindings, router, out);
    CHECK(out.size() == 2);
    CHECK(out[0].status == 0xB0 && out[0].data1 == 10 && out[0].data2 == 127);
    CHECK(out[1].status == 0x91 && out[1].data1 == 20 && out[1].data2 == 127);

    comp.layers[0].opacity = 1.0f;
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.empty());
}

void testFeedbackLimit() {
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    MidiFeedback feedback;
    std::vector<MidiBinding> bindings;
    for (int i = 0; i < 100; ++i) {
        bindings.push_back(noteBinding("master/blackout", i));
    }
    host.dark = true;
    std::vector<MidiMessage> out;
    feedback.compute(bindings, router, out);
    CHECK(out.size() == kMaxFeedbackPerUpdate);
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.size() == 100 - kMaxFeedbackPerUpdate);
    out.clear();
    feedback.compute(bindings, router, out);
    CHECK(out.empty());
}

void testProfileNames() {
    CHECK(sanitizeProfileName("My Pad 1") == "My Pad 1");
    CHECK(sanitizeProfileName("../../etc/passwd") == "etcpasswd");
    CHECK(sanitizeProfileName("a/b\\c") == "abc");
    CHECK(sanitizeProfileName("  ..hidden.. ") == "hidden");
    CHECK(sanitizeProfileName("CON").empty());
    CHECK(sanitizeProfileName("nul.txt").empty());
    CHECK(sanitizeProfileName("COM1").empty());
    CHECK(sanitizeProfileName("console") == "console");
    CHECK(sanitizeProfileName("///").empty());
    CHECK(sanitizeProfileName("").empty());
    CHECK(sanitizeProfileName(std::string(200, 'x')).size() == 48);
    CHECK(sanitizeProfileName("Größe") == "Größe");
    const std::string longUtf8 = std::string(47, 'a') + "ö";
    const std::string cut = sanitizeProfileName(longUtf8);
    CHECK(cut == std::string(47, 'a'));
}

void testProfileJson() {
    std::vector<MidiBinding> bindings{noteBinding("layer/1/clip/1", 36), ccBinding("layer/1/opacity", 7, 0.1f, 0.9f)};
    const std::string text = profileToJson("Test", bindings);
    std::vector<MidiBinding> back;
    std::string error;
    CHECK(profileFromJson(text, back, error));
    CHECK(back.size() == 2);
    CHECK(back[0].target == "layer/1/clip/1" && back[0].number == 36 && back[0].kind == MidiKind::Note);
    CHECK(back[1].kind == MidiKind::ControlChange && near(back[1].low, 0.1, 0.001) && near(back[1].high, 0.9, 0.001));

    std::vector<MidiBinding> keep{noteBinding("master/blackout", 1)};
    CHECK(!profileFromJson("not json", keep, error));
    CHECK(keep.size() == 1);
    CHECK(!profileFromJson("{\"format\":\"other\",\"version\":1,\"bindings\":[]}", keep, error));
    CHECK(!profileFromJson("{\"format\":\"nyrexvj-midi-profile\",\"bindings\":[]}", keep, error));
    CHECK(!profileFromJson("{\"format\":\"nyrexvj-midi-profile\",\"version\":1}", keep, error));
    CHECK(profileFromJson("{\"format\":\"nyrexvj-midi-profile\",\"version\":1,\"bindings\":[]}", keep, error));
    CHECK(keep.empty());

    keep.push_back(noteBinding("master/blackout", 1));
    CHECK(!profileFromJson("{\"format\":\"nyrexvj-midi-profile\",\"version\":2,\"bindings\":[]}", keep, error));
    CHECK(error.find("newer") != std::string::npos);
    CHECK(keep.size() == 1);

    std::string huge = "{\"format\":\"nyrexvj-midi-profile\",\"version\":1,\"bindings\":[],\"pad\":\"";
    huge += std::string(kMaxProfileBytes, 'x');
    huge += "\"}";
    CHECK(!profileFromJson(huge, keep, error));
    CHECK(keep.size() == 1);

    std::vector<MidiBinding> many;
    for (size_t i = 0; i < kMaxProfileBindings + 1; ++i) {
        many.push_back(noteBinding("master/blackout", static_cast<int>(i % 128), 1 + static_cast<int>((i / 128) % 16)));
    }
    const std::string manyText = profileToJson("Many", many);
    if (manyText.size() <= kMaxProfileBytes) {
        CHECK(!profileFromJson(manyText, keep, error));
        CHECK(keep.size() == 1);
    }

    std::vector<MidiBinding> mixed;
    const std::string withBad = "{\"format\":\"nyrexvj-midi-profile\",\"version\":1,\"bindings\":[{\"type\":\"note\",\"channel\":1,\"number\":999,\"target\":\"master/blackout\"},{\"type\":\"note\",\"channel\":1,\"number\":4,\"target\":\"master/blackout\"},{\"type\":\"cc\",\"channel\":1,\"number\":4,\"target\":\"gone\"}]}";
    CHECK(profileFromJson(withBad, mixed, error));
    CHECK(mixed.size() == 1 && mixed[0].number == 4);
}

void testControlSystemProfiles() {
    const std::filesystem::path dir = scratchDirectory("nvj_profiles_");
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    Composition comp;
    QuietHost host;
    ControlSystem system(comp, host);
    system.bindings().push_back(noteBinding("layer/1/clip/1", 36));
    system.bindings().push_back(ccBinding("layer/1/opacity", 7));

    std::string error;
    CHECK(system.listProfiles().empty());
    CHECK(!system.saveProfile("", error));
    CHECK(!system.saveProfile("..", error));
    CHECK(system.saveProfile("../Launchpad", error));
    CHECK(system.saveProfile("Second", error));
    const std::vector<std::string> names = system.listProfiles();
    CHECK(names.size() == 2);
    CHECK(names[0] == "Launchpad" && names[1] == "Second");
    CHECK(std::filesystem::exists(ControlSystem::profileDirectory() / "Launchpad.json"));
    CHECK(!std::filesystem::exists(dir.parent_path() / "Launchpad.json"));

    system.bindings().clear();
    CHECK(system.loadProfile("Launchpad", error));
    CHECK(system.bindings().size() == 2);

    system.bindings().push_back(noteBinding("master/blackout", 1));
    CHECK(!system.loadProfile("../Launchpad", error));
    CHECK(!system.loadProfile("Missing", error));
    CHECK(system.bindings().size() == 3);

    {
        std::ofstream broken(ControlSystem::profileDirectory() / "Broken.json", std::ios::trunc);
        broken << "{ nope";
    }
    CHECK(!system.loadProfile("Broken", error));
    CHECK(system.bindings().size() == 3);
    {
        std::ofstream future(ControlSystem::profileDirectory() / "Future.json", std::ios::trunc);
        future << "{\"format\":\"nyrexvj-midi-profile\",\"version\":9,\"bindings\":[]}";
    }
    CHECK(!system.loadProfile("Future", error));
    CHECK(error.find("newer") != std::string::npos);
    CHECK(system.bindings().size() == 3);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void testControlSystemFeedback() {
    const std::filesystem::path dir = scratchDirectory("nvj_feedback_");
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    Composition comp;
    QuietHost host;
    ControlSystem system(comp, host);
    Sent sent;
    system.useMidiOutput(std::make_unique<RecordingOutput>(sent));
    system.bindings().push_back(noteBinding("master/blackout", 40));
    system.bindings().push_back(ccBinding("layer/1/opacity", 7));

    ControlConfig config;
    config.midiFeedback = false;
    config.midiOutDevice = "Pad One";
    system.configure(config);
    system.update(0.1);
    CHECK(sent.messages.empty());
    CHECK(!system.midiOutOpen());

    config.midiFeedback = true;
    system.configure(config);
    CHECK(system.midiOutOpen());
    system.update(0.5);
    CHECK(sent.messages.size() == 2);
    CHECK(system.feedbackSent() == 2);
    system.update(0.5);
    CHECK(sent.messages.size() == 2);

    host.dark = true;
    system.update(0.1);
    CHECK(sent.messages.size() == 3);
    CHECK(sent.messages.back().data1 == 40 && sent.messages.back().data2 == 127);

    MidiMessage incoming;
    incoming.status = 0xB0;
    incoming.data1 = 7;
    incoming.data2 = 64;
    system.injectMidi(incoming);
    system.update(0.01);
    const size_t afterTouch = sent.messages.size();
    comp.layers[0].opacity = 0.2f;
    system.update(0.05);
    CHECK(sent.messages.size() == afterTouch);
    system.update(0.5);
    CHECK(sent.messages.size() == afterTouch + 1);

    config.midiOutDevice = "Missing Device";
    system.configure(config);
    CHECK(!system.midiOutOpen());
    CHECK(!system.midiOutError().empty());
    const size_t closedCount = sent.messages.size();
    host.dark = false;
    system.update(0.5);
    CHECK(sent.messages.size() == closedCount);

    config.midiOutDevice = "Pad Two";
    system.configure(config);
    CHECK(system.midiOutOpen());
    system.update(0.5);
    CHECK(sent.messages.size() > closedCount);

    sent.failing = true;
    host.dark = true;
    system.update(0.5);
    CHECK(!system.midiOutOpen());
    CHECK(!system.midiOutError().empty());

    config.midiFeedback = false;
    system.configure(config);
    CHECK(!system.midiOutOpen());
    CHECK(system.midiOutError().empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void printCosts() {
    if (std::getenv("NYREXVJ_WORKFLOW_BENCH") == nullptr) {
        return;
    }
    using clock = std::chrono::steady_clock;
    Composition comp;
    QuietHost host;
    ControlRouter router(comp, host);
    MidiFeedback feedback;
    std::vector<MidiBinding> bindings;
    for (int i = 0; i < 128; ++i) {
        bindings.push_back(noteBinding("layer/" + std::to_string(1 + i % 4) + "/clip/" + std::to_string(1 + i % 8), i, 1 + i / 64));
    }
    for (int i = 0; i < 32; ++i) {
        bindings.push_back(ccBinding("layer/" + std::to_string(1 + i % 4) + "/opacity", i, 0.0f, 1.0f));
    }
    std::vector<MidiMessage> out;
    feedback.compute(bindings, router, out);
    const int rounds = 20000;
    auto start = clock::now();
    for (int i = 0; i < rounds; ++i) {
        out.clear();
        feedback.compute(bindings, router, out);
    }
    const double idle = std::chrono::duration<double, std::micro>(clock::now() - start).count() / rounds;
    start = clock::now();
    for (int i = 0; i < rounds; ++i) {
        comp.layers[0].activeColumn = i % 8;
        comp.layers[0].cells[static_cast<size_t>(i % 8)] = 0;
        comp.layers[0].opacity = static_cast<float>(i % 100) / 100.0f;
        out.clear();
        feedback.compute(bindings, router, out);
    }
    const double busy = std::chrono::duration<double, std::micro>(clock::now() - start).count() / rounds;
    std::printf("feedback: 160 mappings, nothing changed %.2f us, state changing every frame %.2f us\n", idle, busy);

    LaunchQuantizer quantizer;
    quantizer.setGrid(QuantizeGrid::Bar);
    QuantizeClock beat;
    beat.usable = true;
    beat.beat = 0.5;
    std::vector<LaunchRequest> immediate;
    std::vector<LaunchRequest> due;
    for (int i = 0; i < 32; ++i) {
        LaunchRequest request;
        request.kind = LaunchKind::Clip;
        request.a = i % 4;
        request.b = i % 8;
        request.b = i % 8;
        quantizer.request(request, beat, 0.0, immediate);
    }
    start = clock::now();
    for (int i = 0; i < rounds; ++i) {
        beat.beat = 0.5 + 0.00005 * i;
        quantizer.update(beat, 0.00001 * i, due);
        due.clear();
    }
    const double waiting = std::chrono::duration<double, std::micro>(clock::now() - start).count() / rounds;
    std::printf("quantizer: update with %zu launches waiting %.3f us\n", quantizer.pending(), waiting);

    LibraryMeta meta;
    for (int i = 0; i < 4000; ++i) {
        const std::string name = "clip " + std::to_string(i);
        meta.setFavorite(name, i % 7 == 0);
        meta.addTag(name, "tag" + std::to_string(i % 20));
    }
    const LibraryQuery query = parseLibraryQuery("is:fav tag:tag7 clip");
    size_t hits = 0;
    start = clock::now();
    for (int round = 0; round < 200; ++round) {
        for (int i = 0; i < 4000; ++i) {
            hits += meta.matches("clip " + std::to_string(i), query) ? 1 : 0;
        }
    }
    const double search = std::chrono::duration<double, std::micro>(clock::now() - start).count() / 200;
    std::printf("library search: 4000 clips with tags, favorites and words %.1f us (hits per round %zu)\n", search, hits / 200);
}

}

int main() {
    testTags();
    testMeta();
    testQuery();
    testPersistence();
    testReplace();
    testQuantizerBasics();
    testQuantizerSlots();
    testQuantizerClockLoss();
    testSettings();
    testLit();
    testFeedbackCompute();
    testFeedbackRangeAndChannels();
    testFeedbackLimit();
    testProfileNames();
    testProfileJson();
    testControlSystemProfiles();
    testControlSystemFeedback();
    printCosts();
    return testing::finish();
}

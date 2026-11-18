#include "check.hpp"

#include "control/control_system.hpp"
#include "control/osc_out.hpp"
#include "core/settings.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace nvj;

class FakeHost final : public ControlHost {
public:
    void triggerClip(int layer, int column) override { clips.emplace_back(layer, column); }
    void triggerColumn(int column) override { columns.push_back(column); }
    void clearLayer(int layer) override { cleared.push_back(layer); }
    void switchDeck(int index) override { decks.push_back(index); }
    void tap() override { ++taps; }
    void resync() override { ++resyncs; }
    bool blackout() const override { return blackoutOn; }
    void setBlackout(bool on) override { blackoutOn = on; }

    std::vector<std::pair<int, int>> clips;
    std::vector<int> columns;
    std::vector<int> cleared;
    std::vector<int> decks;
    int taps = 0;
    int resyncs = 0;
    bool blackoutOn = false;
};

bool near(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

int index(const std::string& id) {
    return controlTargetIndex(id);
}

std::vector<unsigned char> bytes(const OscMessage& message) {
    return encodeOscMessage(message);
}

OscMessage message(const std::string& address, std::vector<OscArgument> arguments = {}) {
    OscMessage result;
    result.address = address;
    result.arguments = std::move(arguments);
    return result;
}

const OscMessage* find(const std::vector<OscMessage>& list, const std::string& address) {
    for (const OscMessage& item : list) {
        if (item.address == address) {
            return &item;
        }
    }
    return nullptr;
}

void testRouterValues() {
    Composition comp;
    FakeHost host;
    ControlRouter router(comp, host);
    const char* ids[] = {"master/level", "master/bpm", "master/hue", "master/saturation", "master/brightness", "master/contrast", "layer/1/opacity", "layer/2/speed", "layer/3/scale", "layer/1/fx/2/param/3", "layer/4/hue", "macro/1", "macro/5", "macro/8"};
    for (const char* id : ids) {
        for (const float value : {0.0f, 0.25f, 0.5f, 1.0f}) {
            CHECK(router.set(index(id), value, false));
            CHECK(near(router.value(index(id)), value));
        }
    }
    CHECK(router.set(index("layer/1/bypass"), 1.0f, false));
    CHECK(near(router.value(index("layer/1/bypass")), 1.0f));
    CHECK(router.set(index("layer/1/bypass"), 0.0f, false));
    CHECK(near(router.value(index("layer/1/bypass")), 0.0f));
    CHECK(near(router.value(index("master/tap")), 0.0f));
    CHECK(near(router.value(-1), 0.0f));
    CHECK(near(router.value(100000), 0.0f));
}

void testOscOutCompose() {
    CHECK(validOscParamName("glow"));
    CHECK(validOscParamName("Layer-1_a"));
    CHECK(!validOscParamName(""));
    CHECK(!validOscParamName("a b"));
    CHECK(!validOscParamName("a/b"));
    CHECK(!validOscParamName(std::string(33, 'a')));
    CHECK(validOscParamName(std::string(32, 'a')));

    Composition comp;
    FakeHost host;
    ControlRouter router(comp, host);
    router.set(index("layer/1/opacity"), 0.75f, false);
    router.set(index("master/level"), 0.5f, false);

    OscOutConfig config;
    config.enabled = true;
    config.version = "9.9.9";
    config.inputPort = 9010;
    config.params = {{"opa", "layer/1/opacity"}, {"lvl", "master/level"}, {"bad name", "master/level"}, {"unknown", "no/such/target"}, {"trig", "master/tap"}, {"opa", "layer/2/opacity"}, {"ok", "layer/2/opacity"}};
    OscSender sender;
    sender.configure(config);
    CHECK(sender.parameterCount() == 3);

    AudioFeatures audio;
    audio.bass = 0.4f;
    audio.mid = 2.0f;
    audio.high = -1.0f;
    audio.level = 0.9f;
    audio.kick = 0.1f;
    audio.beatCount = 5;
    for (int i = 0; i < kSpectrumBins; ++i) {
        audio.spectrum[static_cast<size_t>(i)] = i < 4 ? 0.8f : 0.0f;
    }
    OscOutFrame frame;
    frame.audio = audio;
    frame.bpm = 128.0f;
    frame.beat = 3.25;

    std::vector<OscMessage> first = sender.compose(frame, router, true);
    const OscMessage* a = find(first, "/nyrex/out/audio");
    CHECK(a != nullptr && a->arguments.size() == 5);
    if (a != nullptr && a->arguments.size() == 5) {
        CHECK(near(a->arguments[0].number, 0.4f));
        CHECK(near(a->arguments[1].number, 1.0f));
        CHECK(near(a->arguments[2].number, 0.0f));
        CHECK(near(a->arguments[3].number, 0.9f));
        CHECK(near(a->arguments[4].number, 0.1f));
    }
    const OscMessage* spectrum = find(first, "/nyrex/out/spectrum");
    CHECK(spectrum != nullptr && spectrum->arguments.size() == static_cast<size_t>(kOscSpectrumBands));
    if (spectrum != nullptr && spectrum->arguments.size() == static_cast<size_t>(kOscSpectrumBands)) {
        CHECK(near(spectrum->arguments[0].number, 0.8f));
        CHECK(near(spectrum->arguments[1].number, 0.0f));
    }
    const OscMessage* clock = find(first, "/nyrex/out/clock");
    CHECK(clock != nullptr && clock->arguments.size() == 3);
    if (clock != nullptr && clock->arguments.size() == 3) {
        CHECK(near(clock->arguments[0].number, 128.0f));
        CHECK(clock->arguments[1].type == OscArgument::Type::Int && static_cast<int>(clock->arguments[1].number) == 3);
        CHECK(near(clock->arguments[2].number, 0.25f));
    }
    const OscMessage* opa = find(first, "/nyrex/out/param/opa");
    CHECK(opa != nullptr && opa->arguments.size() == 1 && near(opa->arguments[0].number, 0.75f));
    const OscMessage* lvl = find(first, "/nyrex/out/param/lvl");
    CHECK(lvl != nullptr && near(lvl->arguments[0].number, 0.5f));
    CHECK(find(first, "/nyrex/out/param/ok") != nullptr);
    CHECK(find(first, "/nyrex/out/param/trig") == nullptr);
    CHECK(find(first, "/nyrex/out/beat") == nullptr);
    CHECK(find(first, "/nyrex/out/onset") == nullptr);

    frame.beat = 3.9;
    CHECK(find(sender.compose(frame, router, true), "/nyrex/out/beat") == nullptr);
    frame.beat = 4.05;
    audio.beatCount = 6;
    frame.audio = audio;
    std::vector<OscMessage> events = sender.compose(frame, router, false);
    CHECK(events.size() == 2);
    const OscMessage* beat = find(events, "/nyrex/out/beat");
    CHECK(beat != nullptr && beat->arguments.size() == 2);
    if (beat != nullptr && beat->arguments.size() == 2) {
        CHECK(static_cast<int>(beat->arguments[0].number) == 4);
        CHECK(static_cast<int>(beat->arguments[1].number) == 0);
    }
    const OscMessage* onset = find(events, "/nyrex/out/onset");
    CHECK(onset != nullptr && onset->arguments.size() == 1 && static_cast<int>(onset->arguments[0].number) == 6);
    CHECK(sender.compose(frame, router, false).empty());
    frame.beat = 1.0;
    CHECK(sender.compose(frame, router, false).empty());
    frame.beat = 2.1;
    CHECK(find(sender.compose(frame, router, false), "/nyrex/out/beat") != nullptr);

    std::vector<OscMessage> greeting = sender.hello();
    CHECK(greeting.size() == 1 && greeting[0].address == "/nyrex/out/hello" && greeting[0].arguments.size() == 4);
    if (greeting[0].arguments.size() == 4) {
        CHECK(greeting[0].arguments[0].text == "NyrexVJ");
        CHECK(static_cast<int>(greeting[0].arguments[1].number) == kOscSchema);
        CHECK(greeting[0].arguments[2].text == "9.9.9");
        CHECK(static_cast<int>(greeting[0].arguments[3].number) == 9010);
    }
    std::vector<OscMessage> goodbye = sender.bye();
    CHECK(goodbye.size() == 1 && goodbye[0].address == "/nyrex/out/bye");

    std::vector<OscMessage> decoded;
    std::vector<unsigned char> packet = encodeOscBundle(first);
    CHECK(parseOscPacket(packet.data(), packet.size(), decoded));
    CHECK(decoded.size() == first.size());

    OscOutConfig full = config;
    full.params.clear();
    for (int i = 0; i < 30; ++i) {
        full.params.push_back({"p" + std::to_string(i), "master/level"});
    }
    sender.configure(full);
    CHECK(sender.parameterCount() == kOscMaxParams);
    full.spectrum = false;
    sender.configure(full);
    CHECK(find(sender.compose(frame, router, true), "/nyrex/out/spectrum") == nullptr);
}

void testOscOutSettings() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_oscout_test_data";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    Settings settings;
    CHECK(!settings.oscOutEnabled);
    CHECK(settings.oscOutHost == "127.0.0.1");
    CHECK(settings.oscOutPort == 9011);
    CHECK(settings.oscOutRate == 60);
    CHECK(settings.oscOutSpectrum);
    settings.oscOutEnabled = true;
    settings.oscOutHost = "192.168.1.20";
    settings.oscOutPort = 7000;
    settings.oscOutRate = 30;
    settings.oscOutSpectrum = false;
    settings.oscOutParams = {{"glow", "layer/1/opacity"}, {"speed", "layer/2/speed"}};
    settings.spoutInputs[1] = "Unreal Main";
    CHECK(saveSettings(settings));
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(loaded.oscOutEnabled);
    CHECK(loaded.oscOutHost == "192.168.1.20");
    CHECK(loaded.oscOutPort == 7000);
    CHECK(loaded.oscOutRate == 30);
    CHECK(!loaded.oscOutSpectrum);
    CHECK(loaded.oscOutParams.size() == 2 && loaded.oscOutParams[0].name == "glow" && loaded.oscOutParams[1].target == "layer/2/speed");
    CHECK(loaded.spoutInputs[0].empty() && loaded.spoutInputs[1] == "Unreal Main");
    std::filesystem::remove_all(dir, ec);
}

void testOscParser() {
    {
        std::vector<OscMessage> parsed;
        const auto data = bytes(message("/nyrex/layer/1/opacity", {OscArgument::fromFloat(0.25f)}));
        CHECK(parseOscPacket(data.data(), data.size(), parsed));
        CHECK(parsed.size() == 1);
        CHECK(parsed[0].address == "/nyrex/layer/1/opacity");
        CHECK(parsed[0].arguments.size() == 1);
        CHECK(parsed[0].arguments[0].type == OscArgument::Type::Float);
        CHECK(near(parsed[0].arguments[0].number, 0.25f));
    }
    {
        std::vector<OscMessage> parsed;
        const auto data = bytes(message("/a", {OscArgument::fromInt(-7), OscArgument::fromBool(true), OscArgument::fromBool(false), OscArgument::fromText("hello"), OscArgument()}));
        CHECK(parseOscPacket(data.data(), data.size(), parsed));
        CHECK(parsed.size() == 1);
        CHECK(parsed[0].arguments.size() == 5);
        CHECK(parsed[0].arguments[0].type == OscArgument::Type::Int);
        CHECK(near(parsed[0].arguments[0].number, -7.0f));
        CHECK(parsed[0].arguments[1].type == OscArgument::Type::True);
        CHECK(parsed[0].arguments[2].type == OscArgument::Type::False);
        CHECK(parsed[0].arguments[3].text == "hello");
        CHECK(parsed[0].arguments[4].type == OscArgument::Type::Nil);
    }
    {
        std::vector<OscMessage> parsed;
        const auto data = encodeOscBundle({message("/one", {OscArgument::fromFloat(1.0f)}), message("/two"), message("/three", {OscArgument::fromInt(3)})});
        CHECK(parseOscPacket(data.data(), data.size(), parsed));
        CHECK(parsed.size() == 3);
        CHECK(parsed[1].address == "/two");
        CHECK(parsed[1].arguments.empty());
        CHECK(parsed[2].arguments[0].type == OscArgument::Type::Int);
    }
    {
        std::vector<OscMessage> parsed;
        const unsigned char plain[] = {'/', 'x', 0, 0};
        CHECK(parseOscPacket(plain, sizeof(plain), parsed));
        CHECK(parsed.size() == 1 && parsed[0].arguments.empty());
    }
    {
        std::vector<OscMessage> parsed;
        auto data = bytes(message("/nyrex/master/level", {OscArgument::fromFloat(0.5f)}));
        CHECK(!parseOscPacket(data.data(), data.size() - 2, parsed));
        CHECK(parsed.empty());
        const unsigned char noSlash[] = {'a', 'b', 0, 0};
        CHECK(!parseOscPacket(noSlash, sizeof(noSlash), parsed));
        const unsigned char unterminated[] = {'/', 'a', 'b', 'c'};
        CHECK(!parseOscPacket(unterminated, sizeof(unterminated), parsed));
        const unsigned char badTag[] = {'/', 'a', 0, 0, ',', 'q', 0, 0};
        CHECK(!parseOscPacket(badTag, sizeof(badTag), parsed));
        CHECK(!parseOscPacket(nullptr, 0, parsed));
        unsigned char brokenBundle[] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 200, '/', 'a', 0, 0};
        CHECK(!parseOscPacket(brokenBundle, sizeof(brokenBundle), parsed));
        CHECK(parsed.empty());
    }
    {
        std::vector<OscMessage> parsed;
        auto data = bytes(message("/x", {OscArgument::fromFloat(1.0f)}));
        for (size_t cut = 0; cut < data.size(); ++cut) {
            parsed.clear();
            parseOscPacket(data.data(), cut, parsed);
        }
        CHECK(true);
    }
    CHECK(describeOsc(message("/a", {OscArgument::fromFloat(0.5f), OscArgument::fromBool(true)})) == "/a 0.50 true");
}

void testTargets() {
    const auto& targets = controlTargets();
    CHECK(targets.size() > 200);
    std::set<std::string> ids;
    for (size_t i = 0; i < targets.size(); ++i) {
        ids.insert(targets[i].id);
        CHECK(controlTargetIndex(targets[i].id) == static_cast<int>(i));
    }
    CHECK(ids.size() == targets.size());
    CHECK(index("master/level") >= 0);
    CHECK(index("macro/1") >= 0 && index("macro/8") >= 0);
    CHECK(index("macro/0") < 0 && index("macro/9") < 0);
    CHECK(index("layer/4/fx/4/param/4") >= 0);
    CHECK(index("master/fx/1/enable") >= 0);
    CHECK(index("deck/16") >= 0);
    CHECK(index("deck/17") < 0);
    CHECK(index("layer/5/opacity") < 0);
    CHECK(index("layer/1/clip/8") >= 0);
    CHECK(index("") < 0);
    CHECK(targets[static_cast<size_t>(index("master/blackout"))].kind == ControlKind::Toggle);
    CHECK(targets[static_cast<size_t>(index("column/3"))].kind == ControlKind::Trigger);
    CHECK(targets[static_cast<size_t>(index("layer/2/speed"))].kind == ControlKind::Continuous);
}

void testRouter() {
    Composition comp;
    FakeHost host;
    ControlRouter router(comp, host);

    CHECK(router.set(index("layer/2/opacity"), 0.5f, false));
    CHECK(near(comp.layers[1].opacity, 0.5f));
    router.set(index("layer/2/opacity"), 3.0f, false);
    CHECK(near(comp.layers[1].opacity, 1.0f));
    router.set(index("layer/2/opacity"), -1.0f, false);
    CHECK(near(comp.layers[1].opacity, 0.0f));
    router.set(index("layer/2/opacity"), std::nanf(""), false);
    CHECK(near(comp.layers[1].opacity, 0.0f));

    router.set(index("layer/1/speed"), 0.0f, false);
    CHECK(near(comp.layers[0].speed, 0.1f));
    router.set(index("layer/1/speed"), 1.0f, false);
    CHECK(near(comp.layers[0].speed, 4.0f));
    router.set(index("layer/3/scale"), 0.0f, false);
    CHECK(near(comp.layers[2].scale, 0.25f));
    router.set(index("layer/3/scale"), 1.0f, false);
    CHECK(near(comp.layers[2].scale, 4.0f));
    router.set(index("master/bpm"), 0.0f, false);
    CHECK(near(comp.bpm, 40.0f));
    router.set(index("master/bpm"), 1.0f, false);
    CHECK(near(comp.bpm, 240.0f));
    router.set(index("master/hue"), 0.0f, false);
    CHECK(near(comp.master.grade.hue, -0.5f));
    router.set(index("layer/4/saturation"), 1.0f, false);
    CHECK(near(comp.layers[3].grade.saturation, 2.0f));
    router.set(index("layer/4/contrast"), 0.0f, false);
    CHECK(near(comp.layers[3].grade.contrast, 0.5f));
    router.set(index("master/level"), 0.3f, false);
    CHECK(near(comp.master.level, 0.3f));
    router.set(index("layer/2/fx/3/param/2"), 0.8f, false);
    CHECK(near(comp.layers[1].fx[2].params[1], 0.8f));
    router.set(index("master/fx/4/param/4"), 0.1f, false);
    CHECK(near(comp.master.fx[3].params[3], 0.1f));

    router.set(index("layer/1/bypass"), 1.0f, false);
    CHECK(comp.layers[0].bypass);
    router.set(index("layer/1/bypass"), 0.2f, false);
    CHECK(!comp.layers[0].bypass);
    router.set(index("layer/2/solo"), 0.9f, false);
    CHECK(comp.layers[1].solo);
    router.set(index("master/fx/2/enable"), 0.0f, false);
    CHECK(!comp.master.fx[1].enabled);
    CHECK(router.toggle(index("master/fx/2/enable")));
    CHECK(comp.master.fx[1].enabled);
    CHECK(router.toggle(index("master/blackout")));
    CHECK(host.blackoutOn);
    CHECK(router.toggle(index("master/blackout")));
    CHECK(!host.blackoutOn);
    CHECK(!router.toggle(index("master/level")));
    CHECK(router.state(index("master/fx/2/enable")));

    router.set(index("column/3"), 1.0f, true);
    CHECK(host.columns.size() == 1 && host.columns[0] == 2);
    router.set(index("column/3"), 1.0f, true);
    CHECK(host.columns.size() == 1);
    router.set(index("column/3"), 0.0f, true);
    router.set(index("column/3"), 1.0f, true);
    CHECK(host.columns.size() == 2);
    router.set(index("column/3"), 1.0f, false);
    router.set(index("column/3"), 1.0f, false);
    CHECK(host.columns.size() == 4);
    router.set(index("column/3"), 0.2f, false);
    CHECK(host.columns.size() == 4);

    router.set(index("layer/3/clip/5"), 1.0f, false);
    CHECK(host.clips.size() == 1 && host.clips[0].first == 2 && host.clips[0].second == 4);
    router.set(index("layer/2/clear"), 1.0f, false);
    CHECK(host.cleared.size() == 1 && host.cleared[0] == 1);
    router.set(index("master/tap"), 1.0f, false);
    router.set(index("master/resync"), 1.0f, false);
    CHECK(host.taps == 1 && host.resyncs == 1);
    router.set(index("deck/7"), 1.0f, false);
    CHECK(host.decks.size() == 1 && host.decks[0] == 6);

    comp.deckUsed.fill(false);
    comp.deckUsed[0] = true;
    comp.deckUsed[3] = true;
    comp.deckUsed[9] = true;
    comp.activeDeck = 3;
    router.set(index("deck/next"), 1.0f, false);
    CHECK(host.decks.back() == 9);
    router.set(index("deck/previous"), 1.0f, false);
    CHECK(host.decks.back() == 0);
    CHECK(!router.set(-1, 1.0f, false));
    CHECK(!router.set(100000, 1.0f, false));
}

void testMidi() {
    CHECK(!decodeMidi({0xF8, 0, 0}).valid);
    CHECK(!decodeMidi({0xC0, 5, 0}).valid);
    MidiEvent cc = decodeMidi({0xB2, 7, 127});
    CHECK(cc.valid && cc.kind == MidiKind::ControlChange && cc.channel == 3 && cc.number == 7 && near(cc.value, 1.0f));
    MidiEvent on = decodeMidi({0x90, 36, 100});
    CHECK(on.valid && on.kind == MidiKind::Note && on.channel == 1 && near(on.value, 1.0f));
    CHECK(near(decodeMidi({0x90, 36, 0}).value, 0.0f));
    CHECK(near(decodeMidi({0x80, 36, 64}).value, 0.0f));
    CHECK(describeMidi({0xB0, 7, 64}) == "CC 7  ch 1  64");

    Composition comp;
    FakeHost host;
    ControlRouter router(comp, host);
    std::vector<MidiBinding> bindings;
    MidiBinding fader;
    fader.kind = MidiKind::ControlChange;
    fader.channel = 0;
    fader.number = 7;
    fader.target = "layer/1/opacity";
    bindings.push_back(fader);
    MidiBinding inverted = fader;
    inverted.number = 8;
    inverted.target = "layer/2/opacity";
    inverted.low = 1.0f;
    inverted.high = 0.0f;
    bindings.push_back(inverted);
    MidiBinding channelTwo = fader;
    channelTwo.number = 9;
    channelTwo.channel = 2;
    channelTwo.target = "master/level";
    bindings.push_back(channelTwo);
    MidiBinding pad;
    pad.kind = MidiKind::Note;
    pad.number = 40;
    pad.target = "column/2";
    bindings.push_back(pad);
    MidiBinding hold;
    hold.kind = MidiKind::Note;
    hold.number = 41;
    hold.target = "layer/1/bypass";
    bindings.push_back(hold);
    MidiBinding broken = fader;
    broken.number = 50;
    broken.target = "nonsense";
    bindings.push_back(broken);

    CHECK(applyMidi(bindings, decodeMidi({0xB5, 7, 127}), router));
    CHECK(near(comp.layers[0].opacity, 1.0f));
    CHECK(applyMidi(bindings, decodeMidi({0xB0, 7, 0}), router));
    CHECK(near(comp.layers[0].opacity, 0.0f));
    CHECK(applyMidi(bindings, decodeMidi({0xB0, 8, 127}), router));
    CHECK(near(comp.layers[1].opacity, 0.0f));
    CHECK(applyMidi(bindings, decodeMidi({0xB0, 8, 0}), router));
    CHECK(near(comp.layers[1].opacity, 1.0f));
    comp.master.level = 0.0f;
    CHECK(!applyMidi(bindings, decodeMidi({0xB0, 9, 127}), router));
    CHECK(near(comp.master.level, 0.0f));
    CHECK(applyMidi(bindings, decodeMidi({0xB1, 9, 127}), router));
    CHECK(near(comp.master.level, 1.0f));
    CHECK(applyMidi(bindings, decodeMidi({0x90, 40, 30}), router));
    CHECK(applyMidi(bindings, decodeMidi({0x90, 40, 30}), router));
    CHECK(host.columns.size() == 1 && host.columns[0] == 1);
    applyMidi(bindings, decodeMidi({0x80, 40, 0}), router);
    applyMidi(bindings, decodeMidi({0x90, 40, 99}), router);
    CHECK(host.columns.size() == 2);
    applyMidi(bindings, decodeMidi({0x90, 41, 100}), router);
    CHECK(comp.layers[0].bypass);
    applyMidi(bindings, decodeMidi({0x80, 41, 0}), router);
    CHECK(!comp.layers[0].bypass);
    CHECK(!applyMidi(bindings, decodeMidi({0xB0, 50, 127}), router));
    CHECK(!applyMidi(bindings, decodeMidi({0xB0, 99, 127}), router));

    const std::string json = bindingsToJson(bindings);
    std::vector<MidiBinding> loaded;
    CHECK(bindingsFromJson(json, loaded));
    CHECK(loaded.size() == bindings.size() - 1);
    CHECK(loaded[1].target == "layer/2/opacity" && near(loaded[1].low, 1.0f) && near(loaded[1].high, 0.0f));
    CHECK(loaded[2].channel == 2);
    CHECK(loaded[3].kind == MidiKind::Note && loaded[3].number == 40);
    CHECK(!bindingsFromJson("not json", loaded));
    CHECK(!bindingsFromJson("{\"bindings\": 5}", loaded));
    CHECK(bindingsFromJson("{\"bindings\":[{\"type\":\"cc\",\"channel\":99,\"number\":1,\"target\":\"master/level\"},{\"type\":\"cc\",\"channel\":1,\"number\":300,\"target\":\"master/level\"},{\"type\":\"pitch\",\"channel\":1,\"number\":1,\"target\":\"master/level\"},{\"type\":\"cc\",\"channel\":1,\"number\":1,\"target\":\"master/level\",\"low\":-4,\"high\":9}]}", loaded));
    CHECK(loaded.size() == 1 && near(loaded[0].low, 0.0f) && near(loaded[0].high, 1.0f));
}

void testLearnAndFiles() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_control_test_data";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif

    Composition comp;
    FakeHost host;
    {
        ControlSystem system(comp, host);
        system.configure(ControlConfig());
        CHECK(!system.oscListening());
        CHECK(!system.midiRunning());
        CHECK(!system.learning());

        system.startLearn(-1, "layer/1/opacity");
        CHECK(system.learning());
        system.injectMidi({0x80, 10, 0});
        system.update(0.016);
        CHECK(system.learning());
        system.injectMidi({0xB3, 21, 64});
        system.update(0.016);
        CHECK(!system.learning());
        CHECK(system.bindings().size() == 1);
        CHECK(system.bindings()[0].kind == MidiKind::ControlChange);
        CHECK(system.bindings()[0].channel == 4 && system.bindings()[0].number == 21);
        CHECK(system.bindings()[0].target == "layer/1/opacity");
        CHECK(near(comp.layers[0].opacity, 1.0f));
        CHECK(system.midiCount() == 2);
        CHECK(system.lastMidi() == "CC 21  ch 4  64");

        system.injectMidi({0xB3, 21, 0});
        system.update(0.016);
        CHECK(near(comp.layers[0].opacity, 0.0f));

        system.startLearn(0, "layer/1/opacity");
        system.injectMidi({0x90, 60, 90});
        system.update(0.016);
        CHECK(system.bindings().size() == 1);
        CHECK(system.bindings()[0].kind == MidiKind::Note && system.bindings()[0].number == 60);

        system.startLearn(-1, "master/level");
        system.cancelLearn();
        CHECK(!system.learning());
        system.markDirty();
        system.update(1.0);
    }
    {
        ControlSystem system(comp, host);
        CHECK(system.loadBindings());
        CHECK(system.bindings().size() == 1);
        CHECK(system.bindings()[0].number == 60);
    }

    Settings settings;
    settings.oscEnabled = true;
    settings.oscPort = 12345;
    settings.oscAllowRemote = true;
    settings.midiDevice = "Test Pad";
    CHECK(saveSettings(settings));
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(loaded.oscEnabled && loaded.oscPort == 12345 && loaded.oscAllowRemote && loaded.midiDevice == "Test Pad");
}

#ifndef _WIN32
void testUdp() {
    Composition comp;
    FakeHost host;
    ControlSystem system(comp, host);
    ControlConfig config;
    config.oscEnabled = true;
    int port = 0;
    for (int candidate = 19600; candidate < 19700 && !system.oscListening(); ++candidate) {
        config.oscPort = candidate;
        system.configure(config);
        port = candidate;
    }
    CHECK(system.oscListening());
    CHECK(system.oscPort() == port);

    const auto send = [&](const std::vector<unsigned char>& data) {
        CHECK(UdpSocket::sendLoopback(port, data.data(), static_cast<int>(data.size())));
    };
    send(encodeOscMessage(message("/nyrex/layer/1/opacity", {OscArgument::fromFloat(0.25f)})));
    send(encodeOscBundle({message("/nyrex/master/level", {OscArgument::fromFloat(0.5f)}), message("/nyrex/deck/2"), message("/nyrex/layer/2/bypass", {OscArgument::fromBool(true)}), message("/nyrex/master/blackout"), message("/nyrex/column/4", {OscArgument::fromInt(1)})}));
    const unsigned char garbage[] = {1, 2, 3};
    send(std::vector<unsigned char>(garbage, garbage + 3));
    send(encodeOscMessage(message("/other/thing", {OscArgument::fromFloat(1.0f)})));
    send(encodeOscMessage(message("/nyrex/layer/9/opacity", {OscArgument::fromFloat(1.0f)})));
    send(encodeOscMessage(message("/nyrex/master/level", {OscArgument::fromText("loud")})));

    for (int attempt = 0; attempt < 100 && system.oscCount() < 9; ++attempt) {
        system.update(0.016);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(system.oscCount() == 9);
    CHECK(near(comp.layers[0].opacity, 0.25f));
    CHECK(near(comp.master.level, 0.5f));
    CHECK(host.decks.size() == 1 && host.decks[0] == 1);
    CHECK(comp.layers[1].bypass);
    CHECK(host.blackoutOn);
    CHECK(host.columns.size() == 1 && host.columns[0] == 3);

    ControlSystem second(comp, host);
    ControlConfig clash;
    clash.oscEnabled = true;
    clash.oscPort = port;
    second.configure(clash);
    CHECK(second.oscListening() || !second.oscError().empty());

    config.oscEnabled = false;
    system.configure(config);
    CHECK(!system.oscListening());
}

void testOscOutUdp() {
    Composition comp;
    FakeHost host;
    ControlRouter router(comp, host);
    router.set(index("layer/1/opacity"), 0.6f, false);

    UdpSocket listener;
    std::string error;
    int port = 0;
    for (int candidate = 19700; candidate < 19800 && !listener.isOpen(); ++candidate) {
        if (listener.open(candidate, false, error)) {
            port = candidate;
        }
    }
    CHECK(listener.isOpen());

    OscOutConfig config;
    config.enabled = true;
    config.host = "localhost";
    config.port = port;
    config.rate = 120;
    config.version = "1.2.3";
    config.params = {{"opa", "layer/1/opacity"}};
    OscSender sender;
    sender.configure(config);
    CHECK(sender.sending());
    CHECK(sender.error().empty());

    OscOutFrame frame;
    frame.bpm = 140.0f;
    frame.audio.bass = 0.5f;
    std::vector<OscMessage> all;
    for (int step = 0; step < 6; ++step) {
        frame.beat = 1.0 + step * 0.4;
        sender.update(0.01, frame, router);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        unsigned char buffer[2048];
        for (int n = listener.receive(buffer, sizeof(buffer)); n > 0; n = listener.receive(buffer, sizeof(buffer))) {
            std::vector<OscMessage> decoded;
            CHECK(parseOscPacket(buffer, static_cast<size_t>(n), decoded));
            all.insert(all.end(), decoded.begin(), decoded.end());
        }
    }
    CHECK(sender.packets() >= 3);
    CHECK(find(all, "/nyrex/out/hello") != nullptr);
    const OscMessage* audio = find(all, "/nyrex/out/audio");
    CHECK(audio != nullptr && audio->arguments.size() == 5 && near(audio->arguments[0].number, 0.5f));
    const OscMessage* clock = find(all, "/nyrex/out/clock");
    CHECK(clock != nullptr && clock->arguments.size() == 3 && near(clock->arguments[0].number, 140.0f));
    const OscMessage* opa = find(all, "/nyrex/out/param/opa");
    CHECK(opa != nullptr && opa->arguments.size() == 1 && near(opa->arguments[0].number, 0.6f));
    CHECK(find(all, "/nyrex/out/beat") != nullptr);

    config.enabled = false;
    sender.configure(config);
    CHECK(!sender.sending());
    bool bye = false;
    for (int attempt = 0; attempt < 20 && !bye; ++attempt) {
        unsigned char buffer[2048];
        const int n = listener.receive(buffer, sizeof(buffer));
        if (n > 0) {
            std::vector<OscMessage> decoded;
            parseOscPacket(buffer, static_cast<size_t>(n), decoded);
            bye = find(decoded, "/nyrex/out/bye") != nullptr;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(bye);

    OscOutConfig bad;
    bad.enabled = true;
    bad.host = "not-an-address";
    OscSender broken;
    broken.configure(bad);
    CHECK(!broken.sending());
    CHECK(!broken.error().empty());
    bad.host = "127.0.0.1";
    bad.port = 0;
    broken.configure(bad);
    CHECK(!broken.sending());
    CHECK(!broken.error().empty());
}
#endif

}

int main() {
    testOscParser();
    testTargets();
    testRouter();
    testRouterValues();
    testOscOutCompose();
    testOscOutSettings();
    testMidi();
    testLearnAndFiles();
#ifndef _WIN32
    testUdp();
    testOscOutUdp();
#endif
    return testing::finish();
}

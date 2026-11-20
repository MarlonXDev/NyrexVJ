#include "check.hpp"

#include "Core/NyrexOsc.h"
#include "Core/NyrexState.h"
#include "control/control_system.hpp"
#include "control/osc.hpp"
#include "control/osc_out.hpp"
#include "core/settings.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeHost final : public nvj::ControlHost {
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

bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

std::vector<std::uint8_t> bundleBytes(const std::vector<nvj::OscMessage>& messages) {
    const std::vector<unsigned char> raw = nvj::encodeOscBundle(messages);
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

NyrexCore::OscPacketMessage plain(const std::string& address, std::vector<NyrexCore::OscValue> values) {
    NyrexCore::OscPacketMessage message;
    message.address = address;
    message.values = std::move(values);
    return message;
}

std::vector<std::uint8_t> encoded(const NyrexCore::OscPacketMessage& message) {
    std::vector<std::uint8_t> bytes;
    CHECK(NyrexCore::encodeMessage(message, bytes));
    return bytes;
}

void testSenderOutputIsUnderstood() {
    nvj::Composition comp;
    FakeHost host;
    nvj::ControlRouter router(comp, host);
    router.set(nvj::controlTargetIndex("layer/1/opacity"), 0.75f, false);
    router.set(nvj::controlTargetIndex("master/level"), 0.5f, false);

    nvj::OscOutConfig config;
    config.enabled = true;
    config.version = "9.9.9";
    config.inputPort = 9010;
    config.params = {{"opa", "layer/1/opacity"}, {"lvl", "master/level"}};
    nvj::OscSender sender;
    sender.configure(config);

    nvj::AudioFeatures audio;
    audio.bass = 0.4f;
    audio.mid = 0.5f;
    audio.high = 0.6f;
    audio.level = 0.9f;
    audio.kick = 0.1f;
    audio.beatCount = 5;
    for (int i = 0; i < nvj::kSpectrumBins; ++i) {
        audio.spectrum[static_cast<size_t>(i)] = i < 8 ? 0.8f : 0.2f;
    }
    nvj::OscOutFrame frame;
    frame.audio = audio;
    frame.bpm = 128.0f;
    frame.beat = 3.25;

    NyrexCore::NyrexState state;
    NyrexCore::NyrexEvents events;

    const std::vector<std::uint8_t> greeting = bundleBytes(sender.hello());
    NyrexCore::applyPacket(state, greeting.data(), greeting.size(), events);
    CHECK(events.hello);
    CHECK(state.helloSeen && state.schemaSupported);
    CHECK(state.appName == "NyrexVJ");
    CHECK(state.appVersion == "9.9.9");
    CHECK(state.schema == nvj::kOscSchema);
    CHECK(state.inputPort == 9010);

    const std::vector<std::uint8_t> tick = bundleBytes(sender.compose(frame, router, true));
    NyrexCore::applyPacket(state, tick.data(), tick.size(), events);
    CHECK(near(state.bass, 0.4f) && near(state.mid, 0.5f) && near(state.high, 0.6f) && near(state.level, 0.9f) && near(state.kick, 0.1f));
    CHECK(near(state.spectrum[0], 0.8f) && near(state.spectrum[31], 0.2f));
    CHECK(near(state.bpm, 128.0f) && state.beat == 3 && near(state.beatPhase, 0.25f));
    CHECK(state.parameters.size() == 2);
    CHECK(near(state.parameters["opa"], 0.75f) && near(state.parameters["lvl"], 0.5f));

    frame.beat = 4.05;
    audio.beatCount = 6;
    frame.audio = audio;
    const std::vector<std::uint8_t> eventPacket = bundleBytes(sender.compose(frame, router, false));
    NyrexCore::applyPacket(state, eventPacket.data(), eventPacket.size(), events);
    CHECK(events.beats.size() == 1 && events.beats[0].first == 4 && events.beats[0].second == 0);
    CHECK(events.onsets.size() == 1 && events.onsets[0] == 6);
    CHECK(state.onsetCount == 6 && state.beat == 4);

    const std::vector<std::uint8_t> farewell = bundleBytes(sender.bye());
    NyrexCore::applyPacket(state, farewell.data(), farewell.size(), events);
    CHECK(events.bye);
}

void testPluginMessagesReachNyrexVJ() {
    std::vector<std::uint8_t> bytes = encoded(plain("/nyrex/master/level", {NyrexCore::OscValue::fromNumber(0.5f)}));
    std::vector<nvj::OscMessage> parsed;
    CHECK(nvj::parseOscPacket(bytes.data(), bytes.size(), parsed));
    CHECK(parsed.size() == 1 && parsed[0].address == "/nyrex/master/level");
    CHECK(parsed.size() == 1 && parsed[0].arguments.size() == 1 && near(parsed[0].arguments[0].number, 0.5f));

    bytes = encoded(plain(NyrexCore::kTapAddress, {}));
    parsed.clear();
    CHECK(nvj::parseOscPacket(bytes.data(), bytes.size(), parsed));
    CHECK(parsed.size() == 1 && parsed[0].address == "/nyrex/master/tap" && parsed[0].arguments.empty());

    bytes = encoded(plain("/nyrex/x", {NyrexCore::OscValue::fromInteger(-7), NyrexCore::OscValue::fromText("hello")}));
    parsed.clear();
    CHECK(nvj::parseOscPacket(bytes.data(), bytes.size(), parsed));
    CHECK(parsed.size() == 1 && parsed[0].arguments.size() == 2);
    CHECK(parsed.size() == 1 && parsed[0].arguments.size() == 2 && static_cast<int>(parsed[0].arguments[0].number) == -7 && parsed[0].arguments[1].text == "hello");

    CHECK(NyrexCore::validControlAddress(NyrexCore::kMasterLevelAddress));
    CHECK(nvj::controlTargetIndex("master/level") >= 0);
    CHECK(nvj::controlTargetIndex("master/tap") >= 0);
    for (int column = 1; column <= 8; ++column) {
        const std::string address = NyrexCore::columnAddress(column);
        CHECK(NyrexCore::validControlAddress(address));
        CHECK(nvj::controlTargetIndex(address.substr(7)) >= 0);
    }
    for (int deck = 1; deck <= 16; ++deck) {
        const std::string address = NyrexCore::deckAddress(deck);
        CHECK(NyrexCore::validControlAddress(address));
        CHECK(nvj::controlTargetIndex(address.substr(7)) >= 0);
    }
    for (int layer = 1; layer <= 4; ++layer) {
        const std::string address = NyrexCore::layerOpacityAddress(layer);
        CHECK(NyrexCore::validControlAddress(address));
        CHECK(nvj::controlTargetIndex(address.substr(7)) >= 0);
    }
    CHECK(NyrexCore::columnAddress(0).empty() && NyrexCore::columnAddress(9).empty());
    CHECK(NyrexCore::deckAddress(0).empty() && NyrexCore::deckAddress(17).empty());
    CHECK(NyrexCore::layerOpacityAddress(0).empty() && NyrexCore::layerOpacityAddress(5).empty());
}

void testControlAddresses() {
    CHECK(NyrexCore::validControlAddress("/nyrex/layer/2/fx/3/param/1"));
    CHECK(!NyrexCore::validControlAddress(""));
    CHECK(!NyrexCore::validControlAddress("/nyrex/"));
    CHECK(!NyrexCore::validControlAddress("nyrex/master/level"));
    CHECK(!NyrexCore::validControlAddress("/other/master/level"));
    CHECK(!NyrexCore::validControlAddress("/nyrex/out/audio"));
    CHECK(!NyrexCore::validControlAddress("/nyrex/master/level/"));
    CHECK(!NyrexCore::validControlAddress("/nyrex//master"));
    CHECK(!NyrexCore::validControlAddress("/nyrex/master level"));
    CHECK(!NyrexCore::validControlAddress("/nyrex/master/*"));
    CHECK(!NyrexCore::validControlAddress("/nyrex/" + std::string(200, 'a')));
}

void testStateRules() {
    using NyrexCore::OscValue;
    NyrexCore::NyrexState state;
    NyrexCore::NyrexEvents events;

    NyrexCore::applyMessage(state, plain("/nyrex/out/audio", {OscValue::fromNumber(std::numeric_limits<float>::quiet_NaN()), OscValue::fromNumber(-3.0f), OscValue::fromNumber(7.0f), OscValue::fromInteger(1), OscValue::fromNumber(0.5f)}), events);
    CHECK(state.bass == 0.0f && state.mid == 0.0f && state.high == 1.0f && state.level == 1.0f && near(state.kick, 0.5f));

    NyrexCore::applyMessage(state, plain("/nyrex/out/audio", {OscValue::fromNumber(0.9f)}), events);
    CHECK(state.bass == 0.0f);
    NyrexCore::applyMessage(state, plain("/nyrex/out/audio", {OscValue::fromText("x"), OscValue::fromNumber(0.1f), OscValue::fromNumber(0.1f), OscValue::fromNumber(0.1f), OscValue::fromNumber(0.1f)}), events);
    CHECK(state.bass == 0.0f && state.kick == 0.5f);

    NyrexCore::applyMessage(state, plain("/nyrex/out/audio", {OscValue::fromNumber(0.1f), OscValue::fromNumber(0.2f), OscValue::fromNumber(0.3f), OscValue::fromNumber(0.4f), OscValue::fromNumber(0.5f), OscValue::fromNumber(0.6f), OscValue::fromText("later")}), events);
    CHECK(near(state.bass, 0.1f) && near(state.kick, 0.5f));

    NyrexCore::applyMessage(state, plain("/nyrex/out/unknown", {OscValue::fromNumber(1.0f)}), events);
    NyrexCore::applyMessage(state, plain("/other/audio", {OscValue::fromNumber(1.0f)}), events);
    CHECK(near(state.bass, 0.1f));

    NyrexCore::applyMessage(state, plain("/nyrex/out/clock", {OscValue::fromNumber(std::numeric_limits<float>::infinity()), OscValue::fromInteger(3), OscValue::fromNumber(0.5f)}), events);
    CHECK(state.bpm == 0.0f);
    NyrexCore::applyMessage(state, plain("/nyrex/out/clock", {OscValue::fromNumber(120.0f), OscValue::fromInteger(9), OscValue::fromNumber(2.0f)}), events);
    CHECK(near(state.bpm, 120.0f) && state.beat == 9 && state.beatPhase == 1.0f);

    NyrexCore::applyMessage(state, plain("/nyrex/out/spectrum", {OscValue::fromNumber(0.5f)}), events);
    CHECK(state.spectrum[0] == 0.0f);

    NyrexCore::applyMessage(state, plain("/nyrex/out/param/ok_1-x", {OscValue::fromInteger(1)}), events);
    NyrexCore::applyMessage(state, plain("/nyrex/out/param/bad name", {OscValue::fromNumber(1.0f)}), events);
    NyrexCore::applyMessage(state, plain("/nyrex/out/param/", {OscValue::fromNumber(1.0f)}), events);
    NyrexCore::applyMessage(state, plain("/nyrex/out/param/" + std::string(40, 'a'), {OscValue::fromNumber(1.0f)}), events);
    NyrexCore::applyMessage(state, plain("/nyrex/out/param/empty", {}), events);
    CHECK(state.parameters.size() == 1 && state.parameters["ok_1-x"] == 1.0f);
    for (int i = 0; i < 200; ++i) {
        NyrexCore::applyMessage(state, plain("/nyrex/out/param/p" + std::to_string(i), {OscValue::fromNumber(0.5f)}), events);
    }
    CHECK(state.parameters.size() == NyrexCore::kMaxParameters);
    NyrexCore::applyMessage(state, plain("/nyrex/out/param/ok_1-x", {OscValue::fromNumber(0.25f)}), events);
    CHECK(near(state.parameters["ok_1-x"], 0.25f));

    for (int i = 0; i < 1000; ++i) {
        NyrexCore::applyMessage(state, plain("/nyrex/out/beat", {OscValue::fromInteger(i), OscValue::fromInteger(i % 4)}), events);
        NyrexCore::applyMessage(state, plain("/nyrex/out/onset", {OscValue::fromInteger(i)}), events);
    }
    CHECK(events.beats.size() == NyrexCore::kMaxEventsPerApply && events.onsets.size() == NyrexCore::kMaxEventsPerApply);
    CHECK(state.beat == 999 && state.onsetCount == 999);
}

void testHelloRules() {
    using NyrexCore::OscValue;
    NyrexCore::NyrexState state;
    NyrexCore::NyrexEvents events;

    NyrexCore::applyMessage(state, plain("/nyrex/out/hello", {OscValue::fromText("SomethingElse"), OscValue::fromInteger(1)}), events);
    CHECK(!events.hello && !state.helloSeen);
    NyrexCore::applyMessage(state, plain("/nyrex/out/hello", {OscValue::fromText("NyrexVJ")}), events);
    CHECK(!events.hello);
    NyrexCore::applyMessage(state, plain("/nyrex/out/hello", {OscValue::fromText("NyrexVJ"), OscValue::fromText("1")}), events);
    CHECK(!events.hello);

    NyrexCore::applyMessage(state, plain("/nyrex/out/hello", {OscValue::fromText("NyrexVJ"), OscValue::fromInteger(2), OscValue::fromText("9.0.0"), OscValue::fromInteger(70000)}), events);
    CHECK(events.hello && state.helloSeen && !state.schemaSupported && state.schema == 2);
    CHECK(state.appVersion == "9.0.0" && state.inputPort == 0);
    NyrexCore::applyMessage(state, plain("/nyrex/out/audio", {OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f)}), events);
    NyrexCore::applyMessage(state, plain("/nyrex/out/beat", {OscValue::fromInteger(1), OscValue::fromInteger(1)}), events);
    NyrexCore::applyMessage(state, plain("/nyrex/out/param/x", {OscValue::fromNumber(1.0f)}), events);
    CHECK(state.bass == 0.0f && events.beats.empty() && state.parameters.empty());

    NyrexCore::applyMessage(state, plain("/nyrex/out/hello", {OscValue::fromText("NyrexVJ"), OscValue::fromInteger(1)}), events);
    CHECK(state.schemaSupported);
    NyrexCore::applyMessage(state, plain("/nyrex/out/audio", {OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f), OscValue::fromNumber(1.0f)}), events);
    CHECK(state.bass == 1.0f);
}

void testRoundTrip() {
    using NyrexCore::OscValue;
    const NyrexCore::OscPacketMessage original = plain("/nyrex/test/value", {OscValue::fromInteger(-5), OscValue::fromNumber(1.5f), OscValue::fromText("abc"), OscValue::fromText(""), OscValue::fromText("abcd")});
    const std::vector<std::uint8_t> bytes = encoded(original);
    CHECK(bytes.size() % 4 == 0);
    std::vector<NyrexCore::OscPacketMessage> decoded;
    CHECK(NyrexCore::decodePacket(bytes.data(), bytes.size(), decoded));
    CHECK(decoded.size() == 1 && decoded[0].address == original.address && decoded[0].values.size() == 5);
    if (decoded.size() == 1 && decoded[0].values.size() == 5) {
        CHECK(decoded[0].values[0].kind == OscValue::Kind::Integer && decoded[0].values[0].integer == -5);
        CHECK(decoded[0].values[1].kind == OscValue::Kind::Number && decoded[0].values[1].number == 1.5f);
        CHECK(decoded[0].values[2].text == "abc" && decoded[0].values[3].text.empty() && decoded[0].values[4].text == "abcd");
    }

    std::vector<std::uint8_t> sink;
    CHECK(!NyrexCore::encodeMessage(plain("", {}), sink));
    CHECK(!NyrexCore::encodeMessage(plain("nyrex", {}), sink));
    CHECK(!NyrexCore::encodeMessage(plain(std::string("/a\0b", 4), {}), sink));
    CHECK(!NyrexCore::encodeMessage(plain("/a", {OscValue::fromText(std::string("x\0y", 3))}), sink));
    CHECK(!NyrexCore::encodeMessage(plain("/a", {OscValue::fromText(std::string(600, 'x'))}), sink));
    CHECK(!NyrexCore::encodeMessage(plain("/" + std::string(600, 'x'), {}), sink));
    std::vector<NyrexCore::OscValue> many(NyrexCore::kMaxArguments + 1, OscValue::fromInteger(1));
    CHECK(!NyrexCore::encodeMessage(plain("/a", many), sink));
    CHECK(sink.empty());
}

void testDecoderLimits() {
    using NyrexCore::OscValue;
    std::vector<NyrexCore::OscPacketMessage> out;
    CHECK(!NyrexCore::decodePacket(nullptr, 4, out));
    const std::uint8_t one = 1;
    CHECK(!NyrexCore::decodePacket(&one, 0, out));
    CHECK(!NyrexCore::decodePacket(&one, 1, out));

    const std::vector<std::uint8_t> good = encoded(plain("/nyrex/out/onset", {OscValue::fromInteger(4)}));
    CHECK(NyrexCore::decodePacket(good.data(), good.size(), out) && out.size() == 1);
    for (size_t length = 0; length < good.size(); ++length) {
        std::vector<NyrexCore::OscPacketMessage> partial;
        const bool ok = NyrexCore::decodePacket(good.data(), length, partial);
        CHECK(ok ? partial.size() == 1 : partial.empty());
    }

    std::vector<nvj::OscMessage> nested;
    for (int i = 0; i < 3; ++i) {
        nvj::OscMessage m;
        m.address = "/nyrex/out/onset";
        m.arguments.push_back(nvj::OscArgument::fromInt(i));
        nested.push_back(m);
    }
    const std::vector<std::uint8_t> bundle = bundleBytes(nested);
    out.clear();
    CHECK(NyrexCore::decodePacket(bundle.data(), bundle.size(), out) && out.size() == 3);
    for (size_t length = 0; length < bundle.size(); ++length) {
        std::vector<NyrexCore::OscPacketMessage> partial;
        const bool ok = NyrexCore::decodePacket(bundle.data(), length, partial);
        CHECK(ok ? partial.size() < 3 : partial.empty());
    }

    std::vector<std::uint8_t> deep = good;
    for (int level = 0; level < NyrexCore::kMaxBundleDepth + 2; ++level) {
        std::vector<std::uint8_t> wrapped = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
        const std::uint32_t length = static_cast<std::uint32_t>(deep.size());
        wrapped.push_back(static_cast<std::uint8_t>(length >> 24));
        wrapped.push_back(static_cast<std::uint8_t>(length >> 16));
        wrapped.push_back(static_cast<std::uint8_t>(length >> 8));
        wrapped.push_back(static_cast<std::uint8_t>(length));
        wrapped.insert(wrapped.end(), deep.begin(), deep.end());
        deep = wrapped;
        out.clear();
        const bool ok = NyrexCore::decodePacket(deep.data(), deep.size(), out);
        CHECK(ok == (level + 1 <= NyrexCore::kMaxBundleDepth));
        CHECK(ok ? out.size() == 1 : out.empty());
    }

    std::vector<std::uint8_t> huge(NyrexCore::kMaxPacketBytes + 1, 0);
    CHECK(!NyrexCore::decodePacket(huge.data(), huge.size(), out));

    std::vector<std::uint8_t> manyElements = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    for (size_t i = 0; i < NyrexCore::kMaxMessagesPerPacket + 5; ++i) {
        manyElements.insert(manyElements.end(), {0, 0, 0, static_cast<std::uint8_t>(good.size())});
        manyElements.insert(manyElements.end(), good.begin(), good.end());
    }
    out.clear();
    CHECK(!NyrexCore::decodePacket(manyElements.data(), manyElements.size(), out));
    CHECK(out.empty());

    std::vector<std::uint8_t> badTag = {'/', 'a', 0, 0, ',', 'z', 0, 0};
    CHECK(!NyrexCore::decodePacket(badTag.data(), badTag.size(), out));
    std::vector<std::uint8_t> noTags = {'/', 'a', 0, 0};
    CHECK(NyrexCore::decodePacket(noTags.data(), noTags.size(), out));
    std::vector<std::uint8_t> unterminated = {'/', 'a', 'b', 'c'};
    CHECK(!NyrexCore::decodePacket(unterminated.data(), unterminated.size(), out));
    std::vector<std::uint8_t> badAddress = {'a', 'b', 'c', 0};
    CHECK(!NyrexCore::decodePacket(badAddress.data(), badAddress.size(), out));
    std::vector<std::uint8_t> blobTooLong = {'/', 'a', 0, 0, ',', 'b', 0, 0, 0x7F, 0xFF, 0xFF, 0xFF};
    CHECK(!NyrexCore::decodePacket(blobTooLong.data(), blobTooLong.size(), out));
    std::vector<std::uint8_t> skipped = {'/', 'a', 0, 0, ',', 'h', 'i', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 9};
    out.clear();
    CHECK(NyrexCore::decodePacket(skipped.data(), skipped.size(), out) && out.size() == 1 && out[0].values.size() == 1 && out[0].values[0].integer == 9);
}

void testFuzz() {
    using NyrexCore::OscValue;
    std::vector<nvj::OscMessage> messages;
    nvj::OscMessage audio;
    audio.address = "/nyrex/out/audio";
    for (int i = 0; i < 5; ++i) {
        audio.arguments.push_back(nvj::OscArgument::fromFloat(0.1f * static_cast<float>(i)));
    }
    messages.push_back(audio);
    nvj::OscMessage hello;
    hello.address = "/nyrex/out/hello";
    hello.arguments = {nvj::OscArgument::fromText("NyrexVJ"), nvj::OscArgument::fromInt(1), nvj::OscArgument::fromText("1.0"), nvj::OscArgument::fromInt(9010)};
    messages.push_back(hello);
    const std::vector<std::uint8_t> seed = bundleBytes(messages);

    std::uint32_t random = 12345;
    auto next = [&random]() {
        random = random * 1664525u + 1013904223u;
        return random >> 8;
    };
    for (int round = 0; round < 5000; ++round) {
        std::vector<std::uint8_t> data = seed;
        const int edits = 1 + static_cast<int>(next() % 6);
        for (int e = 0; e < edits; ++e) {
            data[next() % data.size()] = static_cast<std::uint8_t>(next());
        }
        if (next() % 4 == 0) {
            data.resize(next() % (data.size() + 1));
        }
        NyrexCore::NyrexState state;
        NyrexCore::NyrexEvents events;
        NyrexCore::applyPacket(state, data.data(), data.size(), events);
        CHECK(state.bass >= 0.0f && state.bass <= 1.0f);
        CHECK(state.parameters.size() <= NyrexCore::kMaxParameters);
        CHECK(std::isfinite(state.bpm));
    }
}

}

int main() {
    testSenderOutputIsUnderstood();
    testPluginMessagesReachNyrexVJ();
    testControlAddresses();
    testStateRules();
    testHelloRules();
    testRoundTrip();
    testDecoderLimits();
    testFuzz();
    return testing::finish();
}

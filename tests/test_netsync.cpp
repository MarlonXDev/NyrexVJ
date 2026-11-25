#include "check.hpp"
#include "core/netsync.hpp"
#include "core/netsync_link.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace nvj::netsync;

namespace {

std::string toHex(const Bytes& bytes) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

Bytes bytesOf(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

std::uint64_t splitmix(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

Key testKey(std::uint8_t seed) {
    Key key{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(seed * 31 + i * 7 + 1);
    }
    return key;
}

void testHmac() {
    const Bytes key1(20, 0x0b);
    const Bytes data1 = bytesOf("Hi There");
    CHECK(toHex(hmacSha256(key1.data(), key1.size(), data1.data(), data1.size())) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const Bytes key2 = bytesOf("Jefe");
    const Bytes data2 = bytesOf("what do ya want for nothing?");
    CHECK(toHex(hmacSha256(key2.data(), key2.size(), data2.data(), data2.size())) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    const Bytes key3(20, 0xaa);
    const Bytes data3(50, 0xdd);
    CHECK(toHex(hmacSha256(key3.data(), key3.size(), data3.data(), data3.size())) == "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    const Bytes key6(131, 0xaa);
    const Bytes data6 = bytesOf("Test Using Larger Than Block-Size Key - Hash Key First");
    CHECK(toHex(hmacSha256(key6.data(), key6.size(), data6.data(), data6.size())) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    const Bytes empty;
    CHECK(hmacSha256(key2.data(), key2.size(), empty.data(), 0).size() == 32);
    const std::uint8_t a[3] = {1, 2, 3};
    const std::uint8_t b[3] = {1, 2, 3};
    const std::uint8_t c[3] = {1, 2, 4};
    CHECK(constantTimeEqual(a, b, 3));
    CHECK(!constantTimeEqual(a, c, 3));
}

void testKeys() {
    Key key{};
    CHECK(keyIsZero(key));
    const Key a = testKey(3);
    const std::string hex = keyToHex(a);
    CHECK(hex.size() == 64);
    CHECK(keyFromHex(hex, key));
    CHECK(key == a);
    CHECK(!keyIsZero(key));
    std::string upper = hex;
    for (char& c : upper) {
        if (c >= 'a' && c <= 'f') {
            c = static_cast<char>(c - 32);
        }
    }
    Key other{};
    CHECK(keyFromHex(upper, other));
    CHECK(other == a);
    CHECK(!keyFromHex(hex.substr(1), other));
    CHECK(!keyFromHex(hex + "0", other));
    std::string bad = hex;
    bad[10] = 'g';
    CHECK(!keyFromHex(bad, other));
    CHECK(!keyFromHex("", other));
    CHECK(other == a);
}

void testWriterReader() {
    Writer w;
    w.u8(200);
    w.u16(51234);
    w.u32(4000000000u);
    w.u64(0x0123456789abcdefull);
    w.f32(-1.25f);
    w.f64(3.141592653589793);
    w.str("Gr\xc3\xbc\xc3\x9f" "e");
    Reader r(w.bytes().data(), w.size());
    CHECK(r.u8() == 200);
    CHECK(r.u16() == 51234);
    CHECK(r.u32() == 4000000000u);
    CHECK(r.u64() == 0x0123456789abcdefull);
    CHECK(r.f32() == -1.25f);
    CHECK(r.f64() == 3.141592653589793);
    CHECK(r.str() == "Gr\xc3\xbc\xc3\x9f" "e");
    CHECK(r.good() && r.atEnd());
    CHECK(r.u8() == 0);
    CHECK(!r.good());
    Reader empty(nullptr, 0);
    CHECK(empty.u32() == 0);
    CHECK(!empty.good());
    const std::uint8_t cut[] = {5, 'a', 'b'};
    Reader shortString(cut, sizeof(cut));
    CHECK(shortString.str().empty());
    CHECK(!shortString.good());
    std::string longText(200, 'x');
    Writer longWriter;
    longWriter.str(longText);
    CHECK(longWriter.size() == kMaxNameBytes + 1);
    const std::string tricky = std::string(kMaxNameBytes - 1, 'a') + "\xc3\xbc" + "zzz";
    const std::string limited = limitName(tricky);
    CHECK(limited.size() == kMaxNameBytes - 1);
    CHECK(limitName("short") == "short");
    const std::uint8_t oversized[] = {200, 'a'};
    Reader tooLong(oversized, sizeof(oversized));
    tooLong.str();
    CHECK(!tooLong.good());
}

void testPackets() {
    const Key key = testKey(1);
    const Key wrong = testKey(2);
    Bytes body = {1, 2, 3, 4, 5};
    const Bytes packet = encodePacket(key, 1, Type::Ping, 0xaabbccddeeff0011ull, 77, body);
    CHECK(packet.size() == kHeaderBytes + body.size() + kTagBytes);
    Header header;
    Bytes decoded;
    CHECK(decodePacket(key, packet.data(), packet.size(), header, decoded) == Decode::Ok);
    CHECK(header.version == 1);
    CHECK(header.type == Type::Ping);
    CHECK(header.session == 0xaabbccddeeff0011ull);
    CHECK(header.seq == 77);
    CHECK(decoded == body);
    CHECK(decodePacket(wrong, packet.data(), packet.size(), header, decoded) == Decode::BadAuth);
    int authFailures = 0;
    int otherFailures = 0;
    for (size_t i = 0; i < packet.size(); ++i) {
        Bytes copy = packet;
        copy[i] ^= 0x01;
        const Decode result = decodePacket(key, copy.data(), copy.size(), header, decoded);
        CHECK(result != Decode::Ok);
        if (result == Decode::BadAuth) {
            ++authFailures;
        } else {
            ++otherFailures;
        }
    }
    CHECK(authFailures > 0);
    CHECK(otherFailures > 0);
    CHECK(decodePacket(key, packet.data(), 10, header, decoded) == Decode::TooShort);
    CHECK(decodePacket(key, packet.data(), 0, header, decoded) == Decode::TooShort);
    Bytes longer = packet;
    longer.push_back(0);
    CHECK(decodePacket(key, longer.data(), longer.size(), header, decoded) == Decode::BadLength);
    Bytes shorter(packet.begin(), packet.end() - 1);
    CHECK(decodePacket(key, shorter.data(), shorter.size(), header, decoded) != Decode::Ok);
    Bytes huge(kMaxPacket + 1, 0);
    CHECK(decodePacket(key, huge.data(), huge.size(), header, decoded) == Decode::TooLong);
    Bytes oversized(kMaxPacket, 0);
    CHECK(encodePacket(key, 1, Type::State, 1, 1, oversized).empty());
    Bytes biggest(kMaxPacket - kHeaderBytes - kTagBytes, 7);
    const Bytes max = encodePacket(key, 1, Type::State, 1, 1, biggest);
    CHECK(max.size() == kMaxPacket);
    CHECK(decodePacket(key, max.data(), max.size(), header, decoded) == Decode::Ok);
    CHECK(decoded == biggest);
    const Bytes none = encodePacket(key, 1, Type::Bye, 1, 1, Bytes());
    CHECK(decodePacket(key, none.data(), none.size(), header, decoded) == Decode::Ok);
    CHECK(decoded.empty());
    std::uint64_t seed = 99;
    for (int i = 0; i < 4000; ++i) {
        Bytes random(splitmix(seed) % 200);
        for (std::uint8_t& b : random) {
            b = static_cast<std::uint8_t>(splitmix(seed));
        }
        if (random.size() > 4 && i % 2 == 0) {
            random[0] = 'N';
            random[1] = 'V';
            random[2] = 'J';
            random[3] = 'S';
        }
        CHECK(decodePacket(key, random.data(), random.size(), header, decoded) != Decode::Ok);
    }
}

void testMessages() {
    Hello hello;
    hello.protocolMin = 1;
    hello.protocolMax = 3;
    hello.schema = 0x1122334455667788ull;
    hello.nonce = 42;
    hello.name = "Stage left";
    hello.appVersion = "0.48.0";
    Hello hello2;
    CHECK(decodeBody(encodeBody(hello), hello2));
    CHECK(hello2.protocolMin == 1 && hello2.protocolMax == 3 && hello2.schema == hello.schema && hello2.nonce == 42 && hello2.name == hello.name && hello2.appVersion == hello.appVersion);

    Welcome welcome;
    welcome.protocol = 1;
    welcome.nonce = 42;
    welcome.schema = 9;
    welcome.compat = Compat::ClockOnly;
    welcome.name = "Main";
    welcome.appVersion = "0.48.0";
    welcome.masterTime = 12.5;
    Welcome welcome2;
    CHECK(decodeBody(encodeBody(welcome), welcome2));
    CHECK(welcome2.compat == Compat::ClockOnly && welcome2.masterTime == 12.5 && welcome2.nonce == 42 && welcome2.name == "Main");
    Bytes badCompat = encodeBody(welcome);
    badCompat[1 + 8 + 8] = 7;
    CHECK(!decodeBody(badCompat, welcome2));

    StateMsg state;
    state.seq = 9;
    for (int i = 0; i < 160; ++i) {
        state.values.push_back(static_cast<float>(i) * 0.01f);
    }
    StateMsg state2;
    CHECK(decodeBody(encodeBody(state), state2));
    CHECK(state2.seq == 9 && state2.values == state.values);
    Bytes truncated = encodeBody(state);
    truncated.pop_back();
    CHECK(!decodeBody(truncated, state2));
    StateMsg nan = state;
    nan.values[3] = std::nanf("");
    CHECK(!decodeBody(encodeBody(nan), state2));
    StateMsg inf = state;
    inf.values[5] = INFINITY;
    CHECK(!decodeBody(encodeBody(inf), state2));
    StateMsg tooMany;
    tooMany.values.assign(kMaxStateValues + 1, 0.5f);
    CHECK(!decodeBody(encodeBody(tooMany), state2));

    DeltaMsg delta;
    delta.base = 4;
    delta.next = 5;
    delta.items = {{3, 0.5f}, {150, 1.0f}};
    DeltaMsg delta2;
    CHECK(decodeBody(encodeBody(delta), delta2));
    CHECK(delta2.base == 4 && delta2.next == 5 && delta2.items == delta.items);
    DeltaMsg bigDelta;
    for (size_t i = 0; i < kMaxItemsPerDelta + 1; ++i) {
        bigDelta.items.emplace_back(static_cast<std::uint16_t>(i), 0.1f);
    }
    CHECK(!decodeBody(encodeBody(bigDelta), delta2));

    ClockMsg clock;
    clock.masterTime = 100.25;
    clock.beat = 321.5;
    clock.timecode = 12.0;
    clock.bpm = 127.5f;
    clock.beatsPerBar = 4;
    clock.flags = kClockRunning | kClockTimecode;
    clock.timecodeRate = 2;
    clock.jumps = 7;
    clock.stateSeq = 55;
    clock.cues = 3;
    ClockMsg clock2;
    CHECK(decodeBody(encodeBody(clock), clock2));
    CHECK(clock2.masterTime == 100.25 && clock2.beat == 321.5 && clock2.bpm == 127.5f && clock2.flags == clock.flags && clock2.jumps == 7 && clock2.stateSeq == 55 && clock2.cues == 3);
    ClockMsg zero = clock;
    zero.bpm = 0.0f;
    CHECK(!decodeBody(encodeBody(zero), clock2));
    ClockMsg nanClock = clock;
    nanClock.beat = std::nan("");
    CHECK(!decodeBody(encodeBody(nanClock), clock2));

    PingMsg ping;
    ping.t0 = 5.5;
    ping.number = 8;
    ping.stateSeq = 9;
    PingMsg ping2;
    CHECK(decodeBody(encodeBody(ping), ping2));
    CHECK(ping2.t0 == 5.5 && ping2.number == 8 && ping2.stateSeq == 9);
    PongMsg pong;
    pong.t0 = 5.5;
    pong.masterReceive = 100.0;
    pong.masterSend = 100.001;
    pong.number = 8;
    PongMsg pong2;
    CHECK(decodeBody(encodeBody(pong), pong2));
    CHECK(pong2.masterSend == 100.001 && pong2.number == 8);
    ResyncMsg resync;
    resync.reason = 2;
    resync.haveSeq = 77;
    ResyncMsg resync2;
    CHECK(decodeBody(encodeBody(resync), resync2));
    CHECK(resync2.reason == 2 && resync2.haveSeq == 77);
    ByeMsg bye;
    bye.reason = 3;
    ByeMsg bye2;
    CHECK(decodeBody(encodeBody(bye), bye2));
    CHECK(bye2.reason == 3);
    RejectMsg reject;
    reject.code = RejectCode::Full;
    reject.text = "Too many";
    RejectMsg reject2;
    CHECK(decodeBody(encodeBody(reject), reject2));
    CHECK(reject2.code == RejectCode::Full && reject2.text == "Too many");
    Bytes zeroCode = encodeBody(reject);
    zeroCode[0] = 0;
    CHECK(!decodeBody(zeroCode, reject2));
    zeroCode[0] = 99;
    CHECK(!decodeBody(zeroCode, reject2));

    std::uint64_t seed = 7;
    for (int i = 0; i < 6000; ++i) {
        Bytes random(splitmix(seed) % 90);
        for (std::uint8_t& b : random) {
            b = static_cast<std::uint8_t>(splitmix(seed));
        }
        Hello h;
        Welcome w;
        StateMsg s;
        DeltaMsg d;
        ClockMsg c;
        PingMsg p;
        PongMsg o;
        ResyncMsg r;
        ManifestMsg m;
        ByeMsg b;
        RejectMsg j;
        decodeBody(random, h);
        decodeBody(random, w);
        decodeBody(random, s);
        decodeBody(random, d);
        decodeBody(random, c);
        decodeBody(random, p);
        decodeBody(random, o);
        decodeBody(random, r);
        decodeBody(random, m);
        decodeBody(random, b);
        decodeBody(random, j);
    }
}

void testNegotiate() {
    CHECK(negotiate(1, 1, 5, 5).compat == Compat::Full);
    CHECK(negotiate(1, 1, 5, 5).protocol == 1);
    CHECK(negotiate(1, 3, 5, 5).protocol == kProtocol);
    CHECK(negotiate(1, 1, 5, 5).protocol == 1);
    CHECK(negotiate(2, 2, 5, 5).protocol == 2);
    CHECK(negotiate(2, 2, 5, 5).compat == Compat::Full);
    CHECK(negotiate(0, 1, 5, 5).compat == Compat::Full);
    CHECK(negotiate(1, 1, 5, 6).compat == Compat::ClockOnly);
    CHECK(negotiate(kProtocol + 1, kProtocol + 2, 5, 5).compat == Compat::Refused);
    CHECK(negotiate(3, 1, 5, 5).compat == Compat::Refused);
    CHECK(negotiate(0, 0, 5, 5).compat == Compat::Refused);
    const std::vector<std::string> a = {"master/level", "layer/1/opacity"};
    const std::vector<std::string> b = {"master/level", "layer/1/opacity", "layer/1/speed"};
    const std::vector<std::string> c = {"layer/1/opacity", "master/level"};
    CHECK(schemaHash(a) == schemaHash(a));
    CHECK(schemaHash(a) != schemaHash(b));
    CHECK(schemaHash(a) != schemaHash(c));
    CHECK(schemaHash({}) != schemaHash(a));
}

Manifest sampleManifest() {
    Manifest m;
    m.deck = 2;
    for (int i = 0; i < kDecks; ++i) {
        m.deckUsed[static_cast<size_t>(i)] = i < 4;
        m.deckNames[static_cast<size_t>(i)] = i < 4 ? "Deck " + std::to_string(i + 1) : std::string();
    }
    for (int i = 0; i < kCells; ++i) {
        m.cells[static_cast<size_t>(i)] = i % 3 == 0 ? std::string() : "Clip " + std::to_string(i);
    }
    return m;
}

void testManifest() {
    const Manifest source = sampleManifest();
    const std::vector<ManifestMsg> messages = manifestMessages(source);
    CHECK(messages.size() == 5);
    Manifest received;
    for (const ManifestMsg& message : messages) {
        const Bytes body = encodeBody(message);
        CHECK(body.size() + kHeaderBytes + kTagBytes <= kMaxPacket);
        ManifestMsg decoded;
        CHECK(decodeBody(body, decoded));
        applyManifest(received, decoded);
    }
    CHECK(received == source);
    bool allKnown = true;
    for (int i = 0; i < kCells; ++i) {
        allKnown = allKnown && received.cellKnown[static_cast<size_t>(i)];
    }
    for (int i = 0; i < kDecks; ++i) {
        allKnown = allKnown && received.deckKnown[static_cast<size_t>(i)];
    }
    CHECK(allKnown);
    CHECK(manifestFingerprint(source) == manifestFingerprint(received));
    Manifest changed = source;
    changed.cells[5] = "Other";
    CHECK(manifestFingerprint(changed) != manifestFingerprint(source));
    changed = source;
    changed.deck = 3;
    CHECK(manifestFingerprint(changed) != manifestFingerprint(source));
    changed = source;
    changed.deckNames[1] = "Renamed";
    CHECK(manifestFingerprint(changed) != manifestFingerprint(source));

    Manifest next = source;
    next.deck = 5;
    next.cells[1] = "Only one";
    for (int i = 0; i < kCells; ++i) {
        if (i != 1) {
            next.cells[static_cast<size_t>(i)].clear();
        }
    }
    for (const ManifestMsg& message : manifestMessages(next)) {
        applyManifest(received, message);
    }
    CHECK(received.deck == 5);
    CHECK(received.cells[1] == "Only one");
    CHECK(received.cells[2].empty());

    Manifest partial;
    ManifestMsg late;
    late.kind = ManifestKind::Cells;
    late.deck = 1;
    late.entries.push_back({200, 1, "Out of range"});
    late.entries.push_back({3, 1, "Ok"});
    applyManifest(partial, late);
    CHECK(partial.cells[3] == "Ok");
    CHECK(partial.cellKnown[3]);
    CHECK(!partial.cellKnown[4]);
    ManifestMsg many;
    many.entries.assign(17, ManifestEntry{});
    ManifestMsg out;
    CHECK(!decodeBody(encodeBody(many), out));
}

void testEstimator() {
    ClockEstimator estimator;
    CHECK(!estimator.ready());
    std::uint64_t seed = 5;
    auto uniform = [&]() { return static_cast<double>(splitmix(seed) >> 11) / 9007199254740992.0; };
    const double base = 12.345;
    const double drift = 80e-6;
    double worst = 0.0;
    for (int i = 0; i < 40; ++i) {
        const double t = 10.0 + i * 1.0;
        const double up = 0.002 + uniform() * 0.012;
        const double down = 0.002 + uniform() * 0.012;
        const double trueOffset = base + drift * (t - 10.0);
        const double rtt = up + down;
        const double midOffset = trueOffset + (up - down) / 2.0;
        estimator.add(t + rtt / 2.0, midOffset, rtt);
        if (i >= 12) {
            const double predicted = estimator.offsetAt(t + 0.5);
            const double actual = base + drift * (t + 0.5 - 10.0);
            worst = std::max(worst, std::fabs(predicted - actual));
        }
    }
    CHECK(estimator.ready());
    CHECK(worst < 0.004);
    CHECK(std::fabs(estimator.driftPpm()) <= 200.0 + 1e-6);
    CHECK(estimator.roundTrip() > 0.003 && estimator.roundTrip() < 0.012);

    ClockEstimator quiet;
    std::uint64_t quietSeed = 9;
    auto quietUniform = [&]() { return static_cast<double>(splitmix(quietSeed) >> 11) / 9007199254740992.0; };
    for (int i = 0; i < 40; ++i) {
        const double t = 10.0 + i * 1.0;
        const double up = 0.0004 + quietUniform() * 0.0003;
        const double down = 0.0004 + quietUniform() * 0.0003;
        const double trueOffset = base + drift * (t - 10.0);
        quiet.add(t + (up + down) / 2.0, trueOffset + (up - down) / 2.0, up + down);
    }
    CHECK(std::fabs(quiet.driftPpm() - 80.0) < 25.0);

    ClockEstimator still;
    std::uint64_t stillSeed = 21;
    auto stillUniform = [&]() { return static_cast<double>(splitmix(stillSeed) >> 11) / 9007199254740992.0; };
    int invented = 0;
    for (int run = 0; run < 50; ++run) {
        still.reset();
        for (int i = 0; i < 40; ++i) {
            const double t = 10.0 + i * 1.0;
            const double up = 0.0004 + stillUniform() * 0.0008;
            const double down = 0.0004 + stillUniform() * 0.0008;
            still.add(t + (up + down) / 2.0, base + (up - down) / 2.0, up + down);
        }
        if (std::fabs(still.driftPpm()) > 40.0) {
            ++invented;
        }
    }
    CHECK(invented <= 3);

    ClockEstimator outliers;
    for (int i = 0; i < 30; ++i) {
        const double t = 100.0 + i;
        if (i % 3 == 0) {
            outliers.add(t, 5.0 + 0.1, 0.25);
        } else {
            outliers.add(t, 5.0, 0.004);
        }
    }
    CHECK(std::fabs(outliers.offsetAt(130.0) - 5.0) < 0.002);

    ClockEstimator invalid;
    invalid.add(1.0, std::nan(""), 0.01);
    invalid.add(1.0, 1.0, -0.5);
    invalid.add(INFINITY, 1.0, 0.01);
    CHECK(invalid.samples() == 0);
    ClockEstimator few;
    few.add(1.0, 2.0, 0.01);
    few.add(2.0, 2.0, 0.01);
    CHECK(!few.ready());
    few.add(3.0, 2.0, 0.01);
    CHECK(few.ready());
    CHECK(std::fabs(few.offsetAt(10.0) - 2.0) < 1e-9);
    few.reset();
    CHECK(few.samples() == 0);

    ClockEstimator steep;
    for (int i = 0; i < 30; ++i) {
        steep.add(static_cast<double>(i), 1.0 + 0.01 * i, 0.002);
    }
    CHECK(std::fabs(steep.driftPpm()) <= 200.0 + 1e-6);
}

constexpr size_t kState = 40;
constexpr double kMasterBase = 1000.0;
constexpr double kStep = 1.0 / 60.0;
constexpr double kTick = 0.001;

struct Node {
    std::unique_ptr<Follower> follower;
    LossyLink up;
    LossyLink down;
    Peer peer;
    double drift = 0.0;
    double base = 5.0;
    std::uint64_t rng = 1;
    double nextFrame = 0.0;
    double period = 1.0 / 60.0;
    double jitter = 0.002;
    std::vector<Bytes> sent;
    std::vector<Bytes> received;
    std::vector<std::vector<Change>> history;
    bool recordPackets = false;
};

struct Sim {
    Key key = testKey(1);
    std::uint64_t schema = 77;
    std::uint64_t rng = 1234;
    double time = 0.0;
    double bpm = 128.0;
    double beatAtZero = 8.0;
    size_t stateSize = kState;
    MasterFrame frame;
    std::unique_ptr<Master> master;
    std::vector<std::unique_ptr<Node>> nodes;
    bool masterReachable = true;
    double masterNext = 0.0031;
    double masterJitter = 0.001;

    explicit Sim(size_t size = kState) : stateSize(size) {
        frame.state.assign(stateSize, 0.0f);
        frame.manifest = sampleManifest();
        makeMaster();
    }

    double noise() {
        return (static_cast<double>(splitmix(rng) >> 11) / 9007199254740992.0 - 0.5) * 2.0;
    }

    void makeMaster() {
        MasterConfig config;
        config.key = key;
        config.name = "Main";
        config.appVersion = "0.48.0";
        config.schema = schema;
        config.stateSize = stateSize;
        config.random = [this]() { return splitmix(rng); };
        master = std::make_unique<Master>(config);
    }

    Node& addFollower(const LinkConfig& link = LinkConfig(), double drift = 0.0, double base = 5.0, std::uint64_t followerSchema = 0, bool wrongKey = false) {
        auto node = std::make_unique<Node>();
        FollowerConfig config;
        config.key = wrongKey ? testKey(9) : key;
        config.name = "Follower " + std::to_string(nodes.size() + 1);
        config.appVersion = "0.48.0";
        config.schema = followerSchema != 0 ? followerSchema : schema;
        config.stateSize = stateSize;
        Node* raw = node.get();
        raw->rng = 100 + nodes.size();
        config.random = [raw]() { return splitmix(raw->rng); };
        node->follower = std::make_unique<Follower>(config);
        LinkConfig up = link;
        up.seed = 11 + nodes.size();
        LinkConfig down = link;
        down.seed = 211 + nodes.size();
        node->up = LossyLink(up);
        node->down = LossyLink(down);
        node->peer = Peer{0x7f000001u, static_cast<std::uint16_t>(40000 + nodes.size())};
        node->drift = drift;
        node->base = base;
        node->nextFrame = time + 0.0047 * static_cast<double>(nodes.size() + 1);
        node->follower->start(localNow(*node));
        nodes.push_back(std::move(node));
        return *nodes.back();
    }

    double masterNow() const { return kMasterBase + time; }
    double localNow(const Node& node) const { return time * (1.0 + node.drift) + node.base; }
    double beatAt(double t) const { return beatAtZero + t * bpm / 60.0; }

    void masterFrame() {
        for (auto& node : nodes) {
            for (const Bytes& packet : node->up.pop(time)) {
                if (masterReachable) {
                    master->receive(node->peer, packet.data(), packet.size(), masterNow());
                }
            }
        }
        if (frame.clock.running) {
            frame.clock.beat = beatAt(time);
        }
        frame.clock.bpm = bpm;
        master->update(masterNow(), frame);
        for (const Datagram& datagram : master->takeOutgoing()) {
            for (auto& node : nodes) {
                if (node->peer == datagram.peer) {
                    node->down.push(datagram.data, time);
                }
            }
        }
    }

    void followerFrame(Node& node) {
        const double now = localNow(node);
        for (const Bytes& packet : node.down.pop(time)) {
            if (node.recordPackets) {
                node.received.push_back(packet);
            }
            node.follower->receive(packet.data(), packet.size(), now);
        }
        node.follower->update(now);
        std::vector<Change> changes = node.follower->takeChanges();
        if (!changes.empty()) {
            node.history.push_back(changes);
        }
        for (const Bytes& packet : node.follower->takeOutgoing()) {
            if (node.recordPackets) {
                node.sent.push_back(packet);
            }
            node.up.push(packet, time);
        }
    }

    void tick() {
        time += kTick;
        if (time >= masterNext) {
            masterFrame();
            masterNext += kStep + noise() * masterJitter;
        }
        for (auto& node : nodes) {
            if (time >= node->nextFrame) {
                followerFrame(*node);
                node->nextFrame += node->period + noise() * node->jitter;
            }
        }
    }

    void run(double seconds) {
        const int ticks = static_cast<int>(seconds / kTick + 0.5);
        for (int i = 0; i < ticks; ++i) {
            tick();
        }
    }

    bool inSync(const Node& node) const {
        return node.follower->hasState() && node.follower->state() == frame.state;
    }

    void setValue(size_t index, float value) {
        frame.state[index] = value;
    }
};

void testConnect() {
    Sim sim;
    Node& node = sim.addFollower();
    CHECK(node.follower->phase() == FollowerPhase::Connecting);
    sim.run(0.5);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    sim.run(1.5);
    CHECK(node.follower->hasState());
    CHECK(node.follower->compat() == Compat::Full);
    CHECK(node.follower->masterName() == "Main");
    CHECK(node.follower->masterVersion() == "0.48.0");
    const std::vector<FollowerInfo> list = sim.master->followers();
    CHECK(list.size() == 1);
    CHECK(list[0].live);
    CHECK(list[0].name == "Follower 1");
    CHECK(list[0].compat == Compat::Full);
    const ClockView view = node.follower->clock(sim.localNow(node));
    CHECK(view.valid);
    CHECK(view.locked);
    CHECK(std::fabs(view.beat - sim.beatAt(sim.time)) < 0.03);
    CHECK(std::fabs(view.bpm - 128.0) < 1e-3);
    CHECK(view.running);
    const Manifest& manifest = node.follower->manifest();
    CHECK(manifest == sim.frame.manifest);
    CHECK(node.follower->stats().pongs >= 3);
    CHECK(sim.master->stats().badAuth == 0);
    CHECK(sim.master->stats().malformed == 0);
    CHECK(node.follower->stats().malformed == 0);
}

void testStateChanges() {
    Sim sim;
    Node& node = sim.addFollower();
    for (size_t i = 0; i < kState; ++i) {
        sim.frame.state[i] = static_cast<float>(i) / 100.0f;
    }
    sim.run(2.0);
    CHECK(sim.inSync(node));
    size_t firstItems = 0;
    for (const auto& batch : node.history) {
        firstItems += batch.size();
    }
    CHECK(firstItems == kState);
    node.history.clear();
    sim.setValue(3, 0.9f);
    sim.run(0.3);
    CHECK(sim.inSync(node));
    CHECK(node.history.size() == 1);
    CHECK(node.history[0].size() == 1);
    CHECK(node.history[0][0].index == 3);
    CHECK(node.history[0][0].value == 0.9f);
    node.history.clear();
    sim.setValue(3, 0.9f);
    sim.run(1.0);
    CHECK(node.history.empty());
    for (int round = 0; round < 40; ++round) {
        sim.setValue(static_cast<size_t>(round) % kState, static_cast<float>(round) * 0.013f);
        sim.run(0.05);
    }
    sim.run(1.0);
    CHECK(sim.inSync(node));
    for (size_t i = 0; i < kState; ++i) {
        sim.frame.state[i] = 1.0f - static_cast<float>(i) / 50.0f;
    }
    sim.run(0.5);
    CHECK(sim.inSync(node));
    CHECK(node.follower->stats().gaps == 0);
    CHECK(node.follower->stats().deltasApplied > 40);
}

void testLossAndJitter() {
    for (int scenario = 0; scenario < 4; ++scenario) {
        Sim sim;
        LinkConfig link;
        link.loss = scenario == 0 ? 0.05 : scenario == 1 ? 0.2 : scenario == 2 ? 0.3 : 0.15;
        link.duplicate = 0.05;
        link.minDelay = 0.001;
        link.maxDelay = scenario == 3 ? 0.12 : 0.04;
        link.burstLoss = scenario == 2 ? 0.3 : 0.0;
        Node& node = sim.addFollower(link, 30e-6);
        sim.run(10.0);
        CHECK(node.follower->phase() == FollowerPhase::Live);
        std::uint64_t seed = 2024 + static_cast<std::uint64_t>(scenario);
        for (int round = 0; round < 300; ++round) {
            const size_t index = splitmix(seed) % kState;
            sim.setValue(index, static_cast<float>(splitmix(seed) % 1000) / 1000.0f);
            sim.run(0.03);
        }
        sim.run(8.0);
        CHECK(sim.inSync(node));
        CHECK(node.up.dropped() + node.down.dropped() > 20);
        CHECK(node.follower->stats().duplicates + node.follower->stats().replays > 0);
        CHECK(node.follower->stats().malformed == 0);
        CHECK(sim.master->stats().malformed == 0);
        CHECK(node.follower->stats().gaps + node.follower->stats().resyncs > 0 || scenario == 0);
    }
}

void testNoStaleValues() {
    Sim sim;
    LinkConfig link;
    link.loss = 0.1;
    link.minDelay = 0.0;
    link.maxDelay = 0.2;
    Node& node = sim.addFollower(link);
    sim.run(6.0);
    for (int i = 1; i <= 200; ++i) {
        sim.setValue(7, static_cast<float>(i));
        sim.run(0.04);
    }
    float last = 0.0f;
    bool monotone = true;
    for (const auto& batch : node.history) {
        for (const Change& change : batch) {
            if (change.index == 7) {
                if (change.value < last) {
                    monotone = false;
                }
                last = change.value;
            }
        }
    }
    CHECK(monotone);
    sim.run(8.0);
    CHECK(sim.inSync(node));
    CHECK(node.follower->state()[7] == 200.0f);
}

void testOutageAndReconnect() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.setValue(1, 0.25f);
    sim.run(3.0);
    CHECK(sim.inSync(node));
    node.up.setDown(true);
    node.down.setDown(true);
    sim.setValue(1, 0.75f);
    sim.setValue(2, 0.5f);
    sim.run(4.0);
    CHECK(node.follower->phase() == FollowerPhase::Lost);
    CHECK(node.follower->state()[1] == 0.25f);
    CHECK(!node.follower->clock(sim.localNow(node)).valid);
    sim.run(3.0);
    CHECK(sim.master->followers().empty());
    node.history.clear();
    node.up.setDown(false);
    node.down.setDown(false);
    sim.run(8.0);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    CHECK(sim.inSync(node));
    size_t items = 0;
    for (const auto& batch : node.history) {
        items += batch.size();
    }
    CHECK(items == 2);
    CHECK(node.follower->clock(sim.localNow(node)).valid);
    CHECK(sim.master->followers().size() == 1);
}

void testMasterRestart() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.setValue(4, 0.4f);
    sim.run(3.0);
    CHECK(sim.inSync(node));
    sim.makeMaster();
    sim.setValue(4, 0.8f);
    sim.run(6.0);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    CHECK(sim.inSync(node));
    CHECK(node.follower->stats().rejects > 0);
    CHECK(sim.master->stats().unknownSession > 0);
    CHECK(node.follower->clock(sim.localNow(node)).valid);

    Sim quiet;
    Node& second = quiet.addFollower();
    quiet.run(3.0);
    quiet.master->shutdown(quiet.masterNow());
    for (const Datagram& datagram : quiet.master->takeOutgoing()) {
        second.down.push(datagram.data, quiet.time);
    }
    quiet.masterReachable = false;
    quiet.run(0.2);
    CHECK(second.follower->phase() == FollowerPhase::Lost);
    quiet.masterReachable = true;
    quiet.makeMaster();
    quiet.run(6.0);
    CHECK(second.follower->phase() == FollowerPhase::Live);
}

void testSeveralFollowers() {
    Sim sim;
    for (int i = 0; i < kMaxFollowers; ++i) {
        LinkConfig link;
        link.loss = 0.05;
        link.minDelay = 0.001;
        link.maxDelay = 0.015;
        sim.addFollower(link, (i - 4) * 10e-6, 3.0 + i);
    }
    Node& extra = sim.addFollower();
    for (size_t i = 0; i < kState; ++i) {
        sim.setValue(i, static_cast<float>(i) * 0.02f);
    }
    sim.run(8.0);
    int live = 0;
    for (size_t i = 0; i < kMaxFollowers; ++i) {
        live += sim.inSync(*sim.nodes[i]) ? 1 : 0;
    }
    CHECK(live == kMaxFollowers);
    CHECK(sim.master->followers().size() == kMaxFollowers);
    CHECK(extra.follower->phase() == FollowerPhase::Connecting);
    CHECK(extra.follower->lastReject() == RejectCode::Full);
    CHECK(!extra.follower->hasState());
    sim.nodes[0]->follower->stop(sim.localNow(*sim.nodes[0]));
    for (const Bytes& packet : sim.nodes[0]->follower->takeOutgoing()) {
        sim.master->receive(sim.nodes[0]->peer, packet.data(), packet.size(), sim.masterNow());
    }
    CHECK(sim.master->followers().size() == kMaxFollowers - 1);
    sim.run(12.0);
    CHECK(extra.follower->phase() == FollowerPhase::Live);
    CHECK(sim.inSync(extra));
}

void testSecurity() {
    Sim sim;
    Node& good = sim.addFollower();
    good.recordPackets = true;
    Node& wrong = sim.addFollower(LinkConfig(), 0.0, 5.0, 0, true);
    wrong.recordPackets = true;
    sim.setValue(5, 0.5f);
    sim.run(5.0);
    CHECK(good.follower->phase() == FollowerPhase::Live);
    CHECK(wrong.follower->phase() == FollowerPhase::Connecting);
    CHECK(!wrong.follower->hasState());
    CHECK(sim.master->stats().badAuth > 0);
    CHECK(sim.master->followers().size() == 1);
    CHECK(wrong.follower->stats().sent > 0);
    CHECK(wrong.received.empty());

    const std::uint64_t replaysBefore = sim.master->stats().replays;
    CHECK(!good.sent.empty());
    for (const Bytes& packet : good.sent) {
        sim.master->receive(good.peer, packet.data(), packet.size(), sim.masterNow());
    }
    CHECK(sim.master->stats().replays > replaysBefore);
    CHECK(sim.master->followers().size() == 1);

    sim.setValue(5, 0.9f);
    sim.run(1.0);
    CHECK(good.follower->state()[5] == 0.9f);
    const std::uint64_t followerReplays = good.follower->stats().replays + good.follower->stats().duplicates;
    for (const Bytes& packet : good.received) {
        good.follower->receive(packet.data(), packet.size(), sim.localNow(good));
    }
    CHECK(good.follower->stats().replays + good.follower->stats().duplicates > followerReplays);
    CHECK(good.follower->state()[5] == 0.9f);
    CHECK(sim.inSync(good));

    Bytes forged = good.received.back();
    forged[forged.size() - 1] ^= 0xff;
    const std::uint64_t authBefore = good.follower->stats().badAuth;
    good.follower->receive(forged.data(), forged.size(), sim.localNow(good));
    CHECK(good.follower->stats().badAuth == authBefore + 1);
    forged = good.received.back();
    forged[kHeaderBytes] ^= 0x10;
    good.follower->receive(forged.data(), forged.size(), sim.localNow(good));
    CHECK(good.follower->stats().badAuth == authBefore + 2);

    std::uint64_t seed = 31;
    for (int i = 0; i < 3000; ++i) {
        Bytes random(splitmix(seed) % 300);
        for (std::uint8_t& b : random) {
            b = static_cast<std::uint8_t>(splitmix(seed));
        }
        good.follower->receive(random.data(), random.size(), sim.localNow(good));
        sim.master->receive(Peer{0x0a000001u, 5000}, random.data(), random.size(), sim.masterNow());
    }
    CHECK(sim.inSync(good));
    CHECK(sim.master->followers().size() == 1);

    const std::uint64_t limitedBefore = sim.master->stats().limited;
    const Peer flooder{0x0a000002u, 6000};
    for (int i = 0; i < 5000; ++i) {
        sim.master->receive(flooder, good.sent[0].data(), good.sent[0].size(), sim.masterNow());
    }
    CHECK(sim.master->stats().limited > limitedBefore + 3000);
    CHECK(sim.master->followers().size() == 1);
}

void testHelloReplay() {
    Sim sim;
    Node& node = sim.addFollower();
    node.recordPackets = true;
    sim.run(3.0);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    CHECK(!node.sent.empty());
    const Bytes hello = node.sent.front();
    Header header;
    Bytes body;
    CHECK(decodePacket(sim.key, hello.data(), hello.size(), header, body) == Decode::Ok);
    CHECK(header.type == Type::Hello);
    const std::uint64_t before = sim.master->stats().replays;
    const std::vector<FollowerInfo> list = sim.master->followers();
    sim.master->receive(Peer{0x0a000009u, 7777}, hello.data(), hello.size(), sim.masterNow());
    CHECK(sim.master->stats().replays == before + 1);
    CHECK(sim.master->followers().size() == list.size());
    CHECK(sim.master->followers()[0].peer == node.peer);
    sim.master->takeOutgoing();
}

void testVersionAndSchema() {
    Sim sim;
    Node& other = sim.addFollower(LinkConfig(), 0.0, 5.0, 999);
    Node& normal = sim.addFollower();
    sim.setValue(2, 0.6f);
    sim.run(4.0);
    CHECK(other.follower->phase() == FollowerPhase::Live);
    CHECK(other.follower->compat() == Compat::ClockOnly);
    CHECK(!other.follower->hasState());
    CHECK(other.follower->clock(sim.localNow(other)).valid);
    CHECK(other.follower->stats().statesApplied == 0);
    CHECK(normal.follower->compat() == Compat::Full);
    CHECK(sim.inSync(normal));
    CHECK(other.follower->manifest() == sim.frame.manifest);
    const std::vector<FollowerInfo> list = sim.master->followers();
    CHECK(list.size() == 2);
    CHECK(list[0].compat == Compat::ClockOnly);
    CHECK(list[0].live);

    Sim old;
    FollowerConfig config;
    config.key = old.key;
    config.name = "Future";
    config.appVersion = "9.0.0";
    config.schema = old.schema;
    config.stateSize = kState;
    std::uint64_t seed = 3;
    config.random = [&seed]() { return splitmix(seed); };
    Follower future(config);
    future.start(0.0);
    Hello hello;
    hello.protocolMin = kProtocol + 1;
    hello.protocolMax = kProtocol + 3;
    hello.schema = old.schema;
    hello.nonce = 5;
    hello.name = "Future";
    const Bytes packet = encodePacket(old.key, 1, Type::Hello, 4242, 1, encodeBody(hello));
    old.master->receive(Peer{0x7f000001u, 9999}, packet.data(), packet.size(), old.masterNow());
    CHECK(old.master->stats().refused == 1);
    CHECK(old.master->followers().empty());
    std::vector<Datagram> reply = old.master->takeOutgoing();
    CHECK(reply.size() == 1);
    Header header;
    Bytes body;
    CHECK(decodePacket(old.key, reply[0].data.data(), reply[0].data.size(), header, body) == Decode::Ok);
    CHECK(header.type == Type::Reject);
    RejectMsg reject;
    CHECK(decodeBody(body, reject));
    CHECK(reject.code == RejectCode::Version);
    CHECK(header.session == 4242);
    CHECK(header.seq == 1);

    Sim client;
    Node& node = client.addFollower();
    client.run(0.1);
    CHECK(node.follower->phase() != FollowerPhase::Idle);
    RejectMsg forgedReject;
    forgedReject.code = RejectCode::UnknownSession;
    forgedReject.text = "x";
    const Bytes stale = encodePacket(client.key, 1, Type::Reject, 1, 1, encodeBody(forgedReject));
    const std::uint64_t rejectsBefore = node.follower->stats().rejects;
    node.follower->receive(stale.data(), stale.size(), client.localNow(node));
    CHECK(node.follower->stats().rejects == rejectsBefore);
}

void testClockSync() {
    for (int scenario = 0; scenario < 3; ++scenario) {
        Sim sim;
        LinkConfig link;
        link.minDelay = scenario == 0 ? 0.001 : 0.002;
        link.maxDelay = scenario == 0 ? 0.004 : scenario == 1 ? 0.020 : 0.060;
        link.loss = scenario == 2 ? 0.1 : 0.0;
        Node& node = sim.addFollower(link, scenario == 1 ? 50e-6 : -40e-6, 2.0 + scenario);
        sim.run(12.0);
        double worst = 0.0;
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < 20 * 100; ++i) {
            sim.run(0.01);
            const ClockView view = node.follower->clock(sim.localNow(node));
            CHECK(view.valid);
            const double error = std::fabs(view.beat - sim.beatAt(sim.time));
            worst = std::max(worst, error);
            sum += error;
            ++count;
        }
        const double beatSeconds = 60.0 / sim.bpm;
        std::printf("clock sync scenario %d: mean error %.2f ms, worst %.2f ms, drift %.1f ppm, jitter %.2f ms\n", scenario, sum / count * beatSeconds * 1000.0, worst * beatSeconds * 1000.0, node.follower->clock(sim.localNow(node)).driftPpm, node.follower->clock(sim.localNow(node)).jitter * 1000.0);
        CHECK(worst * beatSeconds < (scenario == 0 ? 0.008 : scenario == 1 ? 0.030 : 0.060));
        CHECK(sum / count * beatSeconds < (scenario == 0 ? 0.004 : scenario == 1 ? 0.015 : 0.030));
    }

    Sim sim;
    Node& node = sim.addFollower();
    sim.run(4.0);
    sim.bpm = 140.0;
    sim.run(1.0);
    CHECK(std::fabs(node.follower->clock(sim.localNow(node)).bpm - 140.0) < 1e-3);
    CHECK(!node.follower->takeJump());
    sim.frame.clock.jumps = 1;
    sim.run(0.5);
    CHECK(node.follower->takeJump());
    CHECK(!node.follower->takeJump());
    sim.frame.clock.running = false;
    sim.run(0.5);
    const double frozen = node.follower->clock(sim.localNow(node)).beat;
    sim.run(1.0);
    CHECK(std::fabs(node.follower->clock(sim.localNow(node)).beat - frozen) < 1e-6);
    CHECK(!node.follower->clock(sim.localNow(node)).running);
    sim.frame.clock.timecodeValid = true;
    sim.frame.clock.timecode = 100.0;
    sim.frame.clock.timecodeRate = 2;
    sim.frame.clock.cues = 5;
    sim.run(0.5);
    const ClockView view = node.follower->clock(sim.localNow(node));
    CHECK(view.timecodeValid);
    CHECK(view.cues == 5);
    CHECK(view.timecodeRate == 2);
    CHECK(std::fabs(view.timecode - 100.0) < 0.05);
}

void testClockStale() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.run(4.0);
    CHECK(node.follower->clock(sim.localNow(node)).valid);
    node.down.setDown(true);
    node.up.setDown(true);
    sim.run(2.5);
    CHECK(!node.follower->clock(sim.localNow(node)).valid);
}

void testClockOnlyKeepsRunning() {
    Sim sim;
    Node& node = sim.addFollower(LinkConfig(), 0.0, 5.0, 4321);
    sim.run(3.0);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    const std::uint64_t states = node.follower->stats().statesApplied;
    sim.setValue(0, 0.5f);
    sim.run(2.0);
    CHECK(node.follower->stats().statesApplied == states);
    CHECK(node.follower->stats().deltasApplied == 0);
    CHECK(node.follower->clock(sim.localNow(node)).valid);
}

void testResyncOnGap() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.run(3.0);
    CHECK(sim.inSync(node));
    node.down.setDown(true);
    sim.setValue(1, 0.5f);
    sim.run(0.2);
    node.down.setDown(false);
    sim.setValue(2, 0.6f);
    sim.run(0.2);
    sim.setValue(3, 0.7f);
    sim.run(2.0);
    CHECK(sim.inSync(node));
    CHECK(node.follower->stats().gaps > 0 || node.follower->stats().resyncs > 0);
    CHECK(sim.master->stats().resyncs > 0);
}

void testTailLoss() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.run(3.0);
    node.down.setDown(true);
    sim.setValue(6, 0.33f);
    sim.run(0.1);
    node.down.setDown(false);
    sim.run(3.0);
    CHECK(sim.inSync(node));
}

void testMasterBye() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.run(3.0);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    sim.master->shutdown(sim.masterNow());
    for (const Datagram& datagram : sim.master->takeOutgoing()) {
        node.down.push(datagram.data, sim.time);
    }
    sim.masterReachable = false;
    sim.run(0.1);
    CHECK(node.follower->phase() == FollowerPhase::Lost);
    CHECK(node.follower->hasState());
}

void testFollowerStop() {
    Sim sim;
    Node& node = sim.addFollower();
    sim.run(3.0);
    node.follower->stop(sim.localNow(node));
    CHECK(node.follower->phase() == FollowerPhase::Idle);
    for (const Bytes& packet : node.follower->takeOutgoing()) {
        node.up.push(packet, sim.time);
    }
    sim.run(0.2);
    CHECK(sim.master->followers().empty());
    node.follower->update(sim.localNow(node));
    CHECK(node.follower->takeOutgoing().empty());
    const std::vector<Change> none = node.follower->takeChanges();
    CHECK(none.empty());
    node.follower->start(sim.localNow(node));
    sim.run(3.0);
    CHECK(node.follower->phase() == FollowerPhase::Live);
    CHECK(sim.inSync(node));
}

void testLargeChange() {
    Sim sim(160);
    Node& node = sim.addFollower();
    sim.run(3.0);
    for (size_t i = 0; i < 160; ++i) {
        sim.setValue(i, static_cast<float>(i + 1));
    }
    sim.run(0.5);
    CHECK(sim.inSync(node));
    CHECK(node.follower->stats().statesApplied >= 2);
    const std::uint64_t states = node.follower->stats().statesApplied;
    const std::uint64_t deltas = node.follower->stats().deltasApplied;
    for (size_t i = 0; i < 70; ++i) {
        sim.setValue(i, -static_cast<float>(i + 1));
    }
    sim.run(0.5);
    CHECK(sim.inSync(node));
    CHECK(node.follower->stats().statesApplied == states);
    CHECK(node.follower->stats().deltasApplied == deltas + 2);
    sim.frame.state[1] = std::nanf("");
    sim.run(1.0);
    CHECK(node.follower->state()[1] == -2.0f);
    CHECK(node.follower->stats().malformed == 0);
}

void testPacketSizes() {
    Sim sim;
    LinkConfig link;
    Node& node = sim.addFollower(link);
    node.recordPackets = true;
    for (size_t i = 0; i < kState; ++i) {
        sim.setValue(i, static_cast<float>(i) * 0.5f + 0.1f);
    }
    sim.run(6.0);
    size_t biggest = 0;
    for (const Bytes& packet : node.received) {
        biggest = std::max(biggest, packet.size());
    }
    CHECK(biggest > 0);
    CHECK(biggest <= kMaxPacket);
    StateMsg full;
    full.values.assign(kMaxStateValues, 0.25f);
    const Bytes body = encodeBody(full);
    CHECK(!encodePacket(testKey(1), 1, Type::State, 1, 1, body).empty());
    StateMsg decoded;
    CHECK(decodeBody(body, decoded));
}

}

void testFailoverMessages() {
    ClockMsg clock;
    clock.masterTime = 12.5;
    clock.beat = 33.0;
    clock.bpm = 126.0f;
    clock.extended = true;
    clock.epoch = 7;
    clock.health = kHealthDecoder | kHealthOutput;
    clock.showHash = 0x1122334455667788ull;
    ClockMsg clockBack;
    CHECK(decodeBody(encodeBody(clock), clockBack));
    CHECK(clockBack.extended);
    CHECK(clockBack.epoch == 7);
    CHECK(clockBack.health == (kHealthDecoder | kHealthOutput));
    CHECK(clockBack.showHash == 0x1122334455667788ull);
    clock.extended = false;
    ClockMsg plain;
    CHECK(decodeBody(encodeBody(clock), plain));
    CHECK(!plain.extended);
    CHECK(plain.epoch == 0);
    Bytes cut = encodeBody(clock);
    cut.push_back(1);
    CHECK(!decodeBody(cut, plain));

    PingMsg ping;
    ping.t0 = 4.0;
    ping.number = 3;
    ping.extended = true;
    ping.readiness = kReadinessDegraded;
    ping.health = kHealthOverloaded;
    ping.showHash = 99;
    PingMsg pingBack;
    CHECK(decodeBody(encodeBody(ping), pingBack));
    CHECK(pingBack.extended && pingBack.readiness == kReadinessDegraded && pingBack.health == kHealthOverloaded && pingBack.showHash == 99);
    ping.extended = false;
    CHECK(decodeBody(encodeBody(ping), pingBack));
    CHECK(!pingBack.extended);
    ping.extended = true;
    ping.readiness = 9;
    CHECK(!decodeBody(encodeBody(ping), pingBack));

    TakeoverMsg takeover;
    takeover.epoch = 5;
    takeover.port = 9030;
    takeover.name = "Standby PC";
    TakeoverMsg takeoverBack;
    CHECK(decodeBody(encodeBody(takeover), takeoverBack));
    CHECK(takeoverBack.epoch == 5 && takeoverBack.port == 9030 && takeoverBack.name == "Standby PC");
    takeover.epoch = 0;
    CHECK(!decodeBody(encodeBody(takeover), takeoverBack));
    takeover.epoch = 5;
    takeover.port = 0;
    CHECK(!decodeBody(encodeBody(takeover), takeoverBack));
}

void testFailoverReports() {
    Sim sim;
    sim.frame.clock.health = kHealthDecoder;
    sim.frame.clock.showHash = 0xabcdefull;
    Node& node = sim.addFollower();
    node.follower->setReport(kReadinessReady, kHealthOutput, 0x777);
    sim.run(3.0);
    CHECK(node.follower->protocol() == 2);
    const PrimaryView view = node.follower->primary();
    CHECK(view.known);
    CHECK(view.epoch == 1);
    CHECK(view.health == kHealthDecoder);
    CHECK(view.showHash == 0xabcdefull);
    CHECK(!view.byeSeen);
    const std::vector<FollowerInfo> list = sim.master->followers();
    CHECK(list.size() == 1);
    CHECK(list[0].reports);
    CHECK(list[0].readiness == kReadinessReady);
    CHECK(list[0].health == kHealthOutput);
    CHECK(list[0].showHash == 0x777);
    node.follower->setReport(kReadinessNotReady, 0, 0x778);
    sim.run(2.5);
    CHECK(sim.master->followers()[0].readiness == kReadinessNotReady);
    CHECK(sim.master->followers()[0].showHash == 0x778);
    sim.frame.clock.health = 0;
    sim.run(1.0);
    CHECK(node.follower->primary().health == 0);
    node.up.setDown(true);
    node.down.setDown(true);
    sim.run(4.0);
    CHECK(node.follower->phase() == FollowerPhase::Lost);
    CHECK(node.follower->primary().known);
    CHECK(node.follower->primary().epoch == 1);
}

void testFailoverOldPeer() {
    Sim sim;
    Hello hello;
    hello.protocolMin = 1;
    hello.protocolMax = 1;
    hello.schema = sim.schema;
    hello.nonce = 7;
    hello.name = "Old";
    const Peer peer{0x7f000001u, 50001};
    const Bytes helloPacket = encodePacket(sim.key, 1, Type::Hello, 555, 1, encodeBody(hello));
    sim.master->receive(peer, helloPacket.data(), helloPacket.size(), sim.masterNow());
    std::vector<Datagram> out = sim.master->takeOutgoing();
    CHECK(out.size() == 1);
    Header header;
    Bytes body;
    CHECK(decodePacket(sim.key, out[0].data.data(), out[0].data.size(), header, body) == Decode::Ok);
    CHECK(header.type == Type::Welcome);
    CHECK(header.version == 1);
    Welcome welcome;
    CHECK(decodeBody(body, welcome));
    CHECK(welcome.protocol == 1);
    PingMsg ping;
    ping.t0 = 1.0;
    ping.number = 1;
    const Bytes pingPacket = encodePacket(sim.key, 1, Type::Ping, 555, 2, encodeBody(ping));
    sim.master->receive(peer, pingPacket.data(), pingPacket.size(), sim.masterNow());
    bool sawClock = false;
    for (int i = 0; i < 12; ++i) {
        sim.master->update(sim.masterNow() + 0.15 * static_cast<double>(i + 1), sim.frame);
        for (const Datagram& datagram : sim.master->takeOutgoing()) {
            Header h;
            Bytes b;
            if (decodePacket(sim.key, datagram.data.data(), datagram.data.size(), h, b) != Decode::Ok) {
                continue;
            }
            CHECK(h.version == 1);
            if (h.type == Type::Clock) {
                ClockMsg clock;
                CHECK(decodeBody(b, clock));
                CHECK(!clock.extended);
                sawClock = true;
            }
        }
    }
    CHECK(sawClock);
    CHECK(!sim.master->followers().empty());
    CHECK(!sim.master->followers()[0].reports);
    CHECK(sim.master->stats().malformed == 0);
}

void testFailoverTakeover() {
    Sim sim;
    const Peer other{0x0a000002u, 9030};
    TakeoverMsg message;
    message.epoch = 2;
    message.port = 9030;
    message.name = "Standby";
    Bytes packet = encodePacket(sim.key, kProtocol, Type::Takeover, 0, 1, encodeBody(message));
    sim.master->receive(other, packet.data(), packet.size(), sim.masterNow());
    std::vector<TakeoverNotice> notices = sim.master->takeTakeovers();
    CHECK(notices.size() == 1);
    CHECK(notices[0].epoch == 2 && notices[0].port == 9030 && notices[0].name == "Standby");
    CHECK(notices[0].peer == other);
    CHECK(sim.master->takeOutgoing().empty());

    sim.master->receive(other, packet.data(), packet.size(), sim.masterNow() + 1.0);
    CHECK(sim.master->takeTakeovers().empty());

    message.epoch = 1;
    packet = encodePacket(sim.key, kProtocol, Type::Takeover, 0, 2, encodeBody(message));
    sim.master->receive(other, packet.data(), packet.size(), sim.masterNow() + 2.0);
    CHECK(sim.master->takeTakeovers().empty());

    message.epoch = 3;
    const Bytes forged = encodePacket(testKey(9), kProtocol, Type::Takeover, 0, 3, encodeBody(message));
    sim.master->receive(other, forged.data(), forged.size(), sim.masterNow() + 3.0);
    CHECK(sim.master->takeTakeovers().empty());
    CHECK(sim.master->stats().badAuth == 1);
    CHECK(sim.master->takeOutgoing().empty());

    Bytes broken = encodePacket(sim.key, kProtocol, Type::Takeover, 0, 4, Bytes{1, 2, 3});
    sim.master->receive(other, broken.data(), broken.size(), sim.masterNow() + 4.0);
    CHECK(sim.master->takeTakeovers().empty());

    const Bytes later = encodePacket(sim.key, kProtocol, Type::Takeover, 0, 5, encodeBody(message));
    sim.master->receive(other, later.data(), later.size(), sim.masterNow() + 5.0);
    notices = sim.master->takeTakeovers();
    CHECK(notices.size() == 1 && notices[0].epoch == 3);

    MasterConfig config;
    config.key = sim.key;
    config.schema = sim.schema;
    config.stateSize = kState;
    config.epoch = 4;
    Master newer(config);
    newer.receive(other, later.data(), later.size(), 10.0);
    CHECK(newer.takeTakeovers().empty());
    CHECK(newer.epoch() == 4);

    Sim announcer;
    announcer.master->announceTakeover(other, 9, 9031, "New leader");
    std::vector<Datagram> out = announcer.master->takeOutgoing();
    CHECK(out.size() == 1);
    CHECK(out[0].peer == other);
    Header header;
    Bytes body;
    CHECK(decodePacket(announcer.key, out[0].data.data(), out[0].data.size(), header, body) == Decode::Ok);
    CHECK(header.type == Type::Takeover);
    Sim receiver;
    receiver.master->receive(other, out[0].data.data(), out[0].data.size(), 1.0);
    notices = receiver.master->takeTakeovers();
    CHECK(notices.size() == 1 && notices[0].epoch == 9 && notices[0].port == 9031 && notices[0].name == "New leader");
}

void testFailoverNoReplyLoop() {
    Sim sim;
    const Peer stranger{0x0a000009u, 5000};
    RejectMsg reject;
    reject.code = RejectCode::UnknownSession;
    reject.text = "x";
    const Bytes packet = encodePacket(sim.key, kProtocol, Type::Reject, 31, 1, encodeBody(reject));
    sim.master->receive(stranger, packet.data(), packet.size(), 5.0);
    CHECK(sim.master->takeOutgoing().empty());
    CHECK(sim.master->stats().stray == 1);
    ByeMsg bye;
    const Bytes byePacket = encodePacket(sim.key, kProtocol, Type::Bye, 31, 2, encodeBody(bye));
    sim.master->receive(stranger, byePacket.data(), byePacket.size(), 6.0);
    CHECK(sim.master->takeOutgoing().empty());
    PingMsg ping;
    const Bytes pingPacket = encodePacket(sim.key, kProtocol, Type::Ping, 31, 3, encodeBody(ping));
    sim.master->receive(stranger, pingPacket.data(), pingPacket.size(), 7.0);
    CHECK(sim.master->takeOutgoing().size() == 1);
}

void testFailoverBye() {
    for (std::uint8_t reason : {kByeEnded, kByeSuperseded}) {
        Sim sim;
        Node& node = sim.addFollower();
        sim.run(3.0);
        CHECK(node.follower->phase() == FollowerPhase::Live);
        CHECK(!node.follower->primary().byeSeen);
        sim.master->shutdown(sim.masterNow(), reason);
        for (const Datagram& datagram : sim.master->takeOutgoing()) {
            node.down.push(datagram.data, sim.time);
        }
        sim.masterReachable = false;
        sim.run(0.2);
        CHECK(node.follower->phase() == FollowerPhase::Lost);
        CHECK(node.follower->primary().byeSeen);
        CHECK(node.follower->primary().byeReason == reason);
    }
}

int main() {
    testHmac();
    testKeys();
    testWriterReader();
    testPackets();
    testMessages();
    testNegotiate();
    testManifest();
    testEstimator();
    testConnect();
    testStateChanges();
    testLossAndJitter();
    testNoStaleValues();
    testOutageAndReconnect();
    testMasterRestart();
    testSeveralFollowers();
    testSecurity();
    testHelloReplay();
    testVersionAndSchema();
    testClockSync();
    testClockStale();
    testClockOnlyKeepsRunning();
    testResyncOnGap();
    testTailLoss();
    testMasterBye();
    testFollowerStop();
    testLargeChange();
    testPacketSizes();
    testFailoverMessages();
    testFailoverReports();
    testFailoverOldPeer();
    testFailoverTakeover();
    testFailoverNoReplyLoop();
    testFailoverBye();
    return testing::finish();
}

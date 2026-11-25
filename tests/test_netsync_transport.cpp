#include "check.hpp"

#include "control/netsync_key.hpp"
#include "control/netsync_transport.hpp"
#include "core/log.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

using namespace nvj;
using namespace nvj::netsync;

constexpr size_t kState = 40;
constexpr std::uint64_t kSchema = 0x5eed5eed5eedull;

double steady() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Key makeKey(std::uint8_t seed) {
    Key key{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(seed + i * 7);
    }
    return key;
}

TransportConfig masterConfig(int port, const Key& key) {
    TransportConfig config;
    config.role = Role::Master;
    config.port = port;
    config.allowRemote = false;
    config.name = "master-pc";
    config.appVersion = "test";
    config.key = key;
    config.schema = kSchema;
    config.stateSize = kState;
    return config;
}

TransportConfig followerConfig(int port, const Key& key, const std::string& name = "follower-pc") {
    TransportConfig config;
    config.role = Role::Follower;
    config.port = port;
    config.host = "127.0.0.1";
    config.name = name;
    config.appVersion = "test";
    config.key = key;
    config.schema = kSchema;
    config.stateSize = kState;
    return config;
}

struct Show {
    std::vector<float> values = std::vector<float>(kState, 0.0f);
    double bpm = 120.0;
    double start = steady();
    MasterFrame frame() const {
        MasterFrame result;
        result.state = values;
        result.clock.bpm = bpm;
        result.clock.beat = (steady() - start) * bpm / 60.0;
        result.clock.running = true;
        return result;
    }
    double beat() const { return (steady() - start) * bpm / 60.0; }
};

struct Relay {
    UdpSocket front;
    UdpSocket back;
    std::uint16_t masterPort = 0;
    std::uint32_t followerAddress = 0;
    std::uint16_t followerPort = 0;
    bool haveFollower = false;
    bool paused = false;
    double loss = 0.0;
    std::uint64_t seed = 12345;
    std::uint64_t passed = 0;
    std::uint64_t dropped = 0;

    bool open(int port) {
        std::string error;
        masterPort = static_cast<std::uint16_t>(port);
        return front.openExclusive(0, false, error) && back.openExclusive(0, false, error);
    }

    bool drop() {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return static_cast<double>(seed % 10000) / 10000.0 < loss;
    }

    void pump() {
        unsigned char buffer[2048];
        for (int i = 0; i < 256; ++i) {
            std::uint32_t address = 0;
            std::uint16_t port = 0;
            const int count = front.receiveFrom(buffer, sizeof(buffer), address, port);
            if (count <= 0) {
                break;
            }
            followerAddress = address;
            followerPort = port;
            haveFollower = true;
            if (paused || drop()) {
                ++dropped;
                continue;
            }
            ++passed;
            back.sendTo(0x7f000001u, masterPort, buffer, count);
        }
        for (int i = 0; i < 256; ++i) {
            std::uint32_t address = 0;
            std::uint16_t port = 0;
            const int count = back.receiveFrom(buffer, sizeof(buffer), address, port);
            if (count <= 0) {
                break;
            }
            if (!haveFollower || paused || drop()) {
                ++dropped;
                continue;
            }
            ++passed;
            front.sendTo(followerAddress, followerPort, buffer, count);
        }
    }
};

template <typename Done>
bool runUntil(double seconds, Transport* master, Show* show, std::vector<Transport*> followers, Relay* relay, Done done) {
    const double end = steady() + seconds;
    while (steady() < end) {
        const double now = steady();
        if (relay != nullptr) {
            relay->pump();
        }
        if (master != nullptr) {
            const MasterFrame frame = show->frame();
            master->update(now, &frame);
        }
        for (Transport* follower : followers) {
            follower->update(now, nullptr);
        }
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done();
}

bool inSync(const Transport& follower, const Show& show) {
    const Follower* f = follower.follower();
    return f != nullptr && f->phase() == FollowerPhase::Live && f->hasState() && f->state() == show.values;
}

void testNames() {
    CHECK(std::string(roleName(Role::Off)) == "off");
    CHECK(std::string(roleName(Role::Master)) == "master");
    CHECK(std::string(roleName(Role::Follower)) == "follower");
    CHECK(roleFromName("master") == Role::Master);
    CHECK(roleFromName("follower") == Role::Follower);
    CHECK(roleFromName("nonsense") == Role::Off);
    CHECK(roleFromName("") == Role::Off);
}

void testConnectAndTrack() {
    const Key key = makeKey(1);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    CHECK(master.running());
    CHECK(master.localPort() > 0);
    Transport follower;
    follower.configure(followerConfig(master.localPort(), key), steady());
    CHECK(follower.running());
    CHECK(runUntil(4.0, &master, &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
    CHECK(master.master()->followers().size() == 1);
    CHECK(master.master()->followers()[0].name == "follower-pc");
    CHECK(follower.follower()->masterName() == "master-pc");
    show.values[3] = 0.25f;
    show.values[17] = 0.75f;
    CHECK(runUntil(2.0, &master, &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
    for (int i = 0; i < 20; ++i) {
        show.values[static_cast<size_t>(i)] = static_cast<float>(i) / 20.0f;
    }
    CHECK(runUntil(2.0, &master, &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
    runUntil(3.0, &master, &show, {&follower}, nullptr, [] { return false; });
    const ClockView view = follower.follower()->clock(steady());
    CHECK(view.valid);
    CHECK(std::fabs(view.bpm - 120.0) < 0.01);
    CHECK(std::fabs(view.beat - show.beat()) < 0.1);
    CHECK(follower.follower()->stats().badAuth == 0);
    CHECK(follower.stats().datagramsIn > 20 && follower.stats().datagramsOut > 5);
    CHECK(follower.stats().sendFailures == 0 && master.stats().sendFailures == 0);
    CHECK(master.stats().bytesOut > 0 && master.stats().bytesIn > 0);
}

void testWrongKey() {
    Show show;
    Transport master;
    master.configure(masterConfig(0, makeKey(1)), steady());
    Transport follower;
    follower.configure(followerConfig(master.localPort(), makeKey(2)), steady());
    runUntil(2.5, &master, &show, {&follower}, nullptr, [] { return false; });
    CHECK(master.master()->followers().empty());
    CHECK(master.master()->stats().badAuth > 0);
    CHECK(follower.follower()->phase() != FollowerPhase::Live);
    CHECK(!follower.follower()->hasState());
}

void testForeignSender() {
    const Key key = makeKey(3);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    Transport follower;
    follower.configure(followerConfig(master.localPort(), key), steady());
    CHECK(runUntil(4.0, &master, &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
    UdpSocket intruder;
    std::string error;
    CHECK(intruder.openExclusive(0, false, error));
    unsigned char junk[64];
    for (size_t i = 0; i < sizeof(junk); ++i) {
        junk[i] = static_cast<unsigned char>(i * 3);
    }
    for (int i = 0; i < 10; ++i) {
        intruder.sendTo(0x7f000001u, static_cast<std::uint16_t>(follower.localPort()), junk, sizeof(junk));
    }
    runUntil(0.3, &master, &show, {&follower}, nullptr, [] { return false; });
    CHECK(follower.stats().foreign >= 10);
    CHECK(inSync(follower, show));
    CHECK(follower.follower()->stats().badAuth == 0);
    for (int i = 0; i < 10; ++i) {
        intruder.sendTo(0x7f000001u, static_cast<std::uint16_t>(master.localPort()), junk, sizeof(junk));
    }
    runUntil(0.3, &master, &show, {&follower}, nullptr, [] { return false; });
    CHECK(master.master()->stats().badAuth + master.master()->stats().malformed >= 10);
    CHECK(master.master()->followers().size() == 1);
    CHECK(inSync(follower, show));
}

void testLossyRelay() {
    const Key key = makeKey(4);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    Relay relay;
    CHECK(relay.open(master.localPort()));
    relay.loss = 0.25;
    Transport follower;
    follower.configure(followerConfig(relay.front.localPort(), key), steady());
    CHECK(runUntil(10.0, &master, &show, {&follower}, &relay, [&] { return inSync(follower, show); }));
    int converged = 0;
    for (int round = 0; round < 6; ++round) {
        for (size_t i = 0; i < 12; ++i) {
            show.values[(i * 3 + static_cast<size_t>(round)) % kState] = static_cast<float>(round * 13 + static_cast<int>(i)) / 100.0f;
        }
        if (runUntil(5.0, &master, &show, {&follower}, &relay, [&] { return inSync(follower, show); })) {
            ++converged;
        }
    }
    CHECK(converged == 6);
    CHECK(relay.dropped > 20);
    const ClockView view = follower.follower()->clock(steady());
    CHECK(view.valid);
    CHECK(std::fabs(view.beat - show.beat()) < 0.15);
}

void testOutageAndReconnect() {
    const Key key = makeKey(5);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    Relay relay;
    CHECK(relay.open(master.localPort()));
    Transport follower;
    follower.configure(followerConfig(relay.front.localPort(), key), steady());
    CHECK(runUntil(5.0, &master, &show, {&follower}, &relay, [&] { return inSync(follower, show); }));
    relay.paused = true;
    show.values[5] = 0.9f;
    CHECK(runUntil(6.0, &master, &show, {&follower}, &relay, [&] { return follower.follower()->phase() == FollowerPhase::Lost; }));
    show.values[6] = 0.4f;
    const double lostAt = steady();
    runUntil(1.0, &master, &show, {&follower}, &relay, [] { return false; });
    relay.paused = false;
    CHECK(runUntil(14.0, &master, &show, {&follower}, &relay, [&] { return inSync(follower, show); }));
    CHECK(steady() - lostAt < 15.0);
    CHECK(master.master()->followers().size() == 1);
    CHECK(runUntil(4.0, &master, &show, {&follower}, &relay, [&] { return follower.follower()->clock(steady()).valid; }));
}

void testMasterRestart() {
    const Key key = makeKey(6);
    Show show;
    auto master = std::make_unique<Transport>();
    master->configure(masterConfig(0, key), steady());
    const int port = master->localPort();
    Transport follower;
    follower.configure(followerConfig(port, key), steady());
    CHECK(runUntil(4.0, master.get(), &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
    master->shutdown(steady());
    master.reset();
    runUntil(0.3, nullptr, &show, {&follower}, nullptr, [&] { return follower.follower()->phase() == FollowerPhase::Lost; });
    CHECK(follower.follower()->phase() == FollowerPhase::Lost);
    show.values[9] = 0.123f;
    master = std::make_unique<Transport>();
    master->configure(masterConfig(port, key), steady());
    CHECK(master->running());
    CHECK(runUntil(14.0, master.get(), &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
}

void testTwoFollowers() {
    const Key key = makeKey(7);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    Transport a;
    Transport b;
    a.configure(followerConfig(master.localPort(), key, "a"), steady());
    b.configure(followerConfig(master.localPort(), key, "b"), steady());
    CHECK(runUntil(5.0, &master, &show, {&a, &b}, nullptr, [&] { return inSync(a, show) && inSync(b, show); }));
    CHECK(master.master()->followers().size() == 2);
    show.values[1] = 0.5f;
    CHECK(runUntil(2.0, &master, &show, {&a, &b}, nullptr, [&] { return inSync(a, show) && inSync(b, show); }));
    a.shutdown(steady());
    show.values[2] = 0.6f;
    CHECK(runUntil(2.0, &master, &show, {&b}, nullptr, [&] { return inSync(b, show); }));
}

void testVersionAndSchemaMismatch() {
    const Key key = makeKey(8);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    TransportConfig other = followerConfig(master.localPort(), key);
    other.schema = kSchema + 1;
    Transport follower;
    follower.configure(other, steady());
    CHECK(runUntil(4.0, &master, &show, {&follower}, nullptr, [&] { return follower.follower()->phase() == FollowerPhase::Live; }));
    CHECK(follower.follower()->compat() == Compat::ClockOnly);
    CHECK(!follower.follower()->hasState());
    runUntil(2.5, &master, &show, {&follower}, nullptr, [] { return false; });
    CHECK(follower.follower()->clock(steady()).valid);
    CHECK(follower.follower()->stats().statesApplied == 0);
}

void testConfigErrors() {
    const Key key = makeKey(9);
    Transport follower;
    TransportConfig bad = followerConfig(9030, key);
    bad.host = "not an address";
    follower.configure(bad, steady());
    CHECK(!follower.running());
    CHECK(!follower.error().empty());
    TransportConfig noKey = masterConfig(0, Key{});
    Transport keyless;
    keyless.configure(noKey, steady());
    CHECK(!keyless.running());
    CHECK(!keyless.error().empty());
    Transport first;
    first.configure(masterConfig(0, key), steady());
    Transport second;
    second.configure(masterConfig(first.localPort(), key), steady());
    CHECK(!second.running());
    CHECK(!second.error().empty());
    Transport off;
    off.configure(TransportConfig{}, steady());
    CHECK(!off.running());
    CHECK(off.error().empty());
    CHECK(off.role() == Role::Off);
    follower.configure(followerConfig(first.localPort(), key), steady());
    CHECK(follower.running());
    CHECK(follower.error().empty());
    follower.configure(TransportConfig{}, steady());
    CHECK(!follower.running());
}

void testReconfigureRestarts() {
    const Key key = makeKey(10);
    Show show;
    Transport master;
    master.configure(masterConfig(0, key), steady());
    Transport follower;
    follower.configure(followerConfig(master.localPort(), key), steady());
    CHECK(runUntil(4.0, &master, &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
    follower.configure(followerConfig(master.localPort(), makeKey(11)), steady());
    runUntil(1.5, &master, &show, {&follower}, nullptr, [] { return false; });
    CHECK(follower.follower()->phase() != FollowerPhase::Live);
    follower.configure(followerConfig(master.localPort(), key), steady());
    CHECK(runUntil(6.0, &master, &show, {&follower}, nullptr, [&] { return inSync(follower, show); }));
}

void useTempDataDir() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("nvj-netsync-key-" + std::to_string(static_cast<long long>(steady() * 1000.0)));
    std::filesystem::create_directories(dir);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
}

void testKeyStorage() {
    useTempDataDir();
    Key key{};
    CHECK(!loadKey(key));
    bool created = false;
    CHECK(ensureKey(key, created));
    CHECK(created);
    CHECK(!keyIsZero(key));
    CHECK(saveKey(key));
    Key again{};
    CHECK(loadKey(again));
    CHECK(again == key);
    Key third{};
    created = true;
    CHECK(ensureKey(third, created));
    CHECK(!created);
    CHECK(third == key);
#ifndef _WIN32
    struct stat info {};
    CHECK(::stat(keyPath().c_str(), &info) == 0);
    CHECK((info.st_mode & 0077) == 0);
#endif
    const std::string fingerprint = keyFingerprint(key);
    CHECK(fingerprint.size() == 8);
    CHECK(fingerprint != keyToHex(key).substr(0, 8));
    CHECK(keyFingerprint(Key{}).empty());
    Key other{};
    CHECK(generateKey(other));
    CHECK(other != key);
    CHECK(keyFingerprint(other) != fingerprint);
    Key imported{};
    const std::string hex = keyToHex(other);
    CHECK(importKey(hex, imported) && imported == other);
    std::string spaced = "  ";
    for (size_t i = 0; i < hex.size(); i += 8) {
        spaced += hex.substr(i, 8) + " ";
    }
    spaced += "\n";
    std::string upper = spaced;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    Key fromSpaced{};
    CHECK(importKey(upper, fromSpaced) && fromSpaced == other);
    Key untouched = key;
    CHECK(!importKey("", untouched) && untouched == key);
    CHECK(!importKey(hex.substr(0, 62), untouched) && untouched == key);
    CHECK(!importKey(hex + "00", untouched) && untouched == key);
    CHECK(!importKey(std::string(64, 'g'), untouched) && untouched == key);
    CHECK(!importKey(std::string(64, '0'), untouched) && untouched == key);
    {
        std::ofstream file(keyPath(), std::ios::trunc);
        file << "not a key\n";
    }
    Key broken{};
    CHECK(!loadKey(broken));
    CHECK(ensureKey(broken, created));
    CHECK(created);
    CHECK(!saveKey(Key{}));
}

}

int main() {
    testNames();
    testKeyStorage();
    testConnectAndTrack();
    testWrongKey();
    testForeignSender();
    testTwoFollowers();
    testVersionAndSchemaMismatch();
    testConfigErrors();
    testReconfigureRestarts();
    testLossyRelay();
    testOutageAndReconnect();
    testMasterRestart();
    return testing::finish();
}

#include "app/netsync_selftest.hpp"

#include "app/app.hpp"
#include "control/netsync_key.hpp"
#include "control/netsync_show.hpp"
#include "control/netsync_transport.hpp"
#include "control/sync_system.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "render/clip_library.hpp"
#include "render/engine.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using namespace netsync;
namespace fs = std::filesystem;

double monotonicNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Report {
    json items = json::array();
    int failed = 0;
    int passed = 0;

    void check(bool ok, const std::string& name, const std::string& detail = std::string()) {
        json item;
        item["name"] = name;
        item["ok"] = ok;
        if (!detail.empty()) {
            item["detail"] = detail;
        }
        items.push_back(item);
        if (ok) {
            ++passed;
        } else {
            ++failed;
            logLine("Network sync self test FAILED: " + name + (detail.empty() ? std::string() : " (" + detail + ")"));
        }
    }
};

class ReadHost final : public ShowHost {
public:
    ReadHost(Engine& engine, ControlSystem& control) : engine_(engine), control_(control) {}

    float controlValue(int target) const override { return control_.router().value(target); }
    void setControl(int, float) override {}
    void triggerClip(int, int) override {}
    void clearLayer(int) override {}
    bool switchDeck(int) override { return false; }
    std::uint32_t triggerCount(int layer) const override { return engine_.triggerCount(layer); }
    std::string clipName(int clip) const override {
        const std::vector<ClipDef>& library = clipLibrary();
        return clip >= 0 && static_cast<size_t>(clip) < library.size() ? library[static_cast<size_t>(clip)].name : std::string();
    }
    int clipIndex(const std::string& name) const override {
        const std::vector<ClipDef>& library = clipLibrary();
        for (size_t i = 0; i < library.size(); ++i) {
            if (limitName(library[i].name) == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

private:
    Engine& engine_;
    ControlSystem& control_;
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
    std::uint64_t seed = 99991;
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

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

size_t stateIndex(const std::string& id) {
    const std::vector<std::string>& ids = ShowLayout::get().ids;
    for (size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] == id) {
            return i - 1;
        }
    }
    return static_cast<size_t>(-1);
}

struct ErrorStats {
    double sum = 0.0;
    double worst = 0.0;
    int count = 0;
    void add(double milliseconds) {
        const double value = std::fabs(milliseconds);
        sum += value;
        worst = std::max(worst, value);
        ++count;
    }
    double mean() const { return count > 0 ? sum / count : 0.0; }
};

struct PureMaster {
    Transport transport;
    MasterFrame frame;
    double beatOrigin = 0.0;
    double beatStart = 0.0;
    double bpm = 126.0;
    bool running = true;

    double beatAt(double at) const { return beatOrigin + (running ? (at - beatStart) * bpm / 60.0 : 0.0); }

    void setClock(double now, double newBpm, bool run) {
        beatOrigin = beatAt(now);
        beatStart = now;
        bpm = newBpm;
        running = run;
    }

    void jump(double now, double beats) {
        beatOrigin = beatAt(now) + beats;
        beatStart = now;
        ++frame.clock.jumps;
    }

    bool start(const Key& key, const std::string& name, double now, std::string& error) {
        TransportConfig config;
        config.role = Role::Master;
        config.port = 0;
        config.name = name;
        config.appVersion = "selftest";
        config.key = key;
        config.schema = ShowLayout::get().schema;
        config.stateSize = ShowLayout::get().layout.total();
        transport.configure(config, now);
        error = transport.error();
        return transport.running();
    }

    void step(double now) {
        frame.clock.beat = beatAt(now);
        frame.clock.bpm = bpm;
        frame.clock.beatsPerBar = 4;
        frame.clock.running = running;
        transport.update(now, &frame);
    }

    void set(const std::string& id, float value) {
        const size_t index = stateIndex(id);
        if (index < frame.state.size()) {
            frame.state[index] = value;
        }
    }

    void trigger(int layer, int column) {
        const ShowLayout& show = ShowLayout::get();
        frame.state[show.layout.column(layer)] = static_cast<float>(column);
        const std::uint32_t next = (static_cast<std::uint32_t>(frame.state[show.layout.trigger(layer)]) + 1) & kTriggerCounterMask;
        frame.state[show.layout.trigger(layer)] = static_cast<float>(next);
    }

    void clear(int layer) { frame.state[ShowLayout::get().layout.column(layer)] = -1.0f; }
    void deck(int index) {
        frame.state[ShowLayout::get().layout.deck()] = static_cast<float>(index);
        frame.manifest.deck = index;
    }
};

struct Rig {
    App& app;
    Engine& engine;
    NetSyncSystem& sys;
    SyncSystem& sync;
    ReadHost host;
    double last = monotonicNow();
    double frameSleep = 0.006;
    double lastAfter = 0.0;
    ErrorStats clockError;
    bool measureClock = false;
    PureMaster* clockMaster = nullptr;
    double updateSum = 0.0;
    double updateWorst = 0.0;
    long updates = 0;

    void resetUpdateCost() {
        updateSum = 0.0;
        updateWorst = 0.0;
        updates = 0;
    }

    json updateCost() const {
        return {{"meanMicroseconds", updates > 0 ? updateSum / static_cast<double>(updates) * 1.0e6 : 0.0}, {"worstMicroseconds", updateWorst * 1.0e6}, {"calls", updates}};
    }

    explicit Rig(App& target) : app(target), engine(target.engine()), sys(target.netSync()), sync(target.syncSystem()), host(target.engine(), target.control()) {}

    template <typename Peer, typename Done>
    bool run(double seconds, Peer peer, Done done) {
        const double end = monotonicNow() + seconds;
        while (monotonicNow() < end) {
            const double now = monotonicNow();
            const double dt = std::clamp(now - last, 0.0005, 0.1);
            last = now;
            sys.update(dt, now);
            const double spent = monotonicNow() - now;
            updateSum += spent;
            updateWorst = std::max(updateWorst, spent);
            ++updates;
            SyncConfig config;
            config.mode = sys.effectiveSyncMode(SyncMode::Manual);
            config.cues = !sys.suppressesAutomation();
            sync.configure(config);
            sync.update(dt, {}, engine.beatClock(), now);
            peer(now);
            engine.update(dt);
            const double after = monotonicNow();
            sys.afterEngineUpdate(after, dt);
            lastAfter = after;
            if (measureClock && clockMaster != nullptr && sys.followingClock() && engine.beatClock().active() == SyncSource::Network) {
                const double error = engine.beat() - clockMaster->beatAt(after);
                clockError.add(error * 60.0 / clockMaster->bpm * 1000.0);
            }
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(frameSleep));
        }
        return done();
    }
};

void fillClips(Composition& comp) {
    const size_t library = clipLibrary().size();
    for (int layer = 0; layer < kLayerCount; ++layer) {
        for (int column = 0; column < 4; ++column) {
            comp.layers[static_cast<size_t>(layer)].cells[static_cast<size_t>(column)] = static_cast<int>((static_cast<size_t>(layer) * 4 + static_cast<size_t>(column)) % library);
        }
        comp.layers[static_cast<size_t>(layer)].activeColumn = -1;
    }
}

void prepare(Rig& rig) {
    Composition base = factoryPresets().front();
    fillClips(base);
    rig.engine.applyComposition(base);
    rig.engine.paused = false;
    rig.engine.blackout = false;
}

void testDefaults(Rig& rig, Report& report, json& out) {
    report.check(!rig.sys.running() && rig.sys.role() == Role::Off, "the network sync is off by default");
    rig.sys.configure(NetSyncSettings{});
    rig.sys.update(1.0 / 60.0, monotonicNow());
    report.check(!rig.sys.running() && rig.sys.localPort() == 0, "with the role off no port is opened");
    report.check(!rig.sys.suppressesAutomation() && !rig.sys.followingClock(), "with the role off nothing is paused or followed");
    report.check(!rig.sys.importKey(std::string(63, 'a')) && !rig.sys.importKey(std::string(64, 'z')), "keys of the wrong length or with wrong characters are refused");
    const std::string fixed = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    report.check(rig.sys.importKey(fixed) && rig.sys.keyHex() == fixed, "a pasted key is accepted");
    const std::string check = rig.sys.keyFingerprint();
    report.check(!check.empty() && fixed.find(check) == std::string::npos, "the check code is not a part of the key");
    out["keyCheckCode"] = check;
}

void testFollower(Rig& rig, Report& report, json& out) {
    prepare(rig);
    rig.resetUpdateCost();
    PureMaster master;
    Key key{};
    importKey(rig.sys.keyHex(), key);
    std::string error;
    report.check(master.start(key, "peer-master", monotonicNow(), error) && master.transport.localPort() > 0, "the peer master opens a local port", error);
    buildShowState(rig.engine.comp, rig.host, master.frame.state);
    master.frame.manifest = buildManifest(rig.engine.comp, rig.host);
    master.setClock(monotonicNow(), 126.0, true);

    Relay relay;
    report.check(relay.open(master.transport.localPort()), "the lossy relay opens");

    NetSyncSettings settings;
    settings.role = Role::Follower;
    settings.port = relay.front.localPort();
    settings.host = "127.0.0.1";
    settings.name = "app-follower";
    rig.sys.configure(settings);

    auto step = [&](double now) {
        relay.pump();
        master.step(now);
    };
    auto live = [&] { return rig.sys.follower() != nullptr && rig.sys.follower()->phase() == FollowerPhase::Live && rig.sys.follower()->hasState(); };

    report.check(!rig.sys.followingClock(), "the clock is not followed before the connection exists");
    const bool connected = rig.run(10.0, step, live);
    report.check(connected, "the follower connects and gets the show state");
    report.check(rig.sys.running() && rig.sys.role() == Role::Follower, "the program reports the follower role");
    report.check(rig.sys.suppressesAutomation(), "the local timeline and cues are paused while following the controls");
    report.check(rig.sys.master() == nullptr, "a follower has no master part and opens no listening role");

    master.set("layer/2/opacity", 0.3f);
    master.set("master/level", 0.6f);
    master.set("macro/3", 0.8f);
    master.set("layer/1/fx/2/param/3", 0.15f);
    master.set("master/blackout", 1.0f);
    auto controlsArrived = [&] {
        return near(rig.engine.comp.layers[1].opacity, 0.3, 0.002) && near(rig.engine.comp.master.level, 0.6, 0.002) && near(rig.engine.comp.modulators.macros[2], 0.8, 0.002) && near(rig.engine.comp.layers[0].fx[1].params[2], 0.15, 0.002) && rig.engine.blackout;
    };
    report.check(rig.run(4.0, step, controlsArrived), "control changes of the master reach the follower");
    master.set("master/blackout", 0.0f);
    report.check(rig.run(4.0, step, [&] { return !rig.engine.blackout; }), "blackout of the master is mirrored and released");

    master.trigger(1, 2);
    report.check(rig.run(4.0, step, [&] { return rig.engine.comp.layers[1].activeColumn == 2; }), "a clip started on the master starts on the follower");
    const std::uint32_t before = rig.engine.triggerCount(1);
    master.trigger(1, 2);
    report.check(rig.run(4.0, step, [&] { return rig.engine.triggerCount(1) > before; }), "starting the same clip again restarts it on the follower");
    master.clear(1);
    report.check(rig.run(4.0, step, [&] { return rig.engine.comp.layers[1].activeColumn == -1; }), "clearing a layer on the master clears it on the follower");

    rig.engine.comp.deckUsed[2] = true;
    rig.engine.comp.decks[2] = emptyDeck();
    master.frame.manifest.deckUsed[2] = true;
    master.frame.manifest.deckNames[2] = limitName(deckDisplayName(rig.engine.comp, 2));
    master.deck(2);
    master.set("layer/1/opacity", 0.4f);
    report.check(rig.run(5.0, step, [&] { return rig.engine.comp.activeDeck == 2 && near(rig.engine.comp.layers[0].opacity, 0.4, 0.002); }), "a deck switch on the master switches the follower and the controls land in the new deck");
    master.deck(0);
    report.check(rig.run(5.0, step, [&] { return rig.engine.comp.activeDeck == 0; }), "switching back follows too");
    const auto switched = rig.sys.applyStats().decksSwitched;
    report.check(switched >= 2, "the deck switches were counted", std::to_string(switched));

    const std::string ghost = "Video: this-file-is-only-on-the-master.mp4";
    master.frame.manifest.cells[3 * kColumns + 5] = ghost;
    auto hasMissing = [&] {
        for (const Mismatch& mismatch : rig.sys.mismatches()) {
            if (mismatch.kind == MismatchKind::MissingClip && mismatch.layer == 3 && mismatch.column == 5 && mismatch.name == ghost) {
                return true;
            }
        }
        return false;
    };
    report.check(rig.run(5.0, step, hasMissing), "a clip that only exists on the master is reported as missing");
    rig.engine.comp.layers[3].cells[5] = 1;
    const auto skippedBefore = rig.sys.applyStats().clipsSkipped;
    master.trigger(3, 5);
    rig.run(2.0, step, [] { return false; });
    report.check(rig.sys.applyStats().clipsSkipped > skippedBefore && rig.engine.comp.layers[3].activeColumn != 5, "a clip with another name is not started");
    out["follower"]["mismatches"] = rig.sys.report()["mismatches"];
    master.frame.manifest.cells[3 * kColumns + 5] = std::string();
    master.clear(3);
    rig.run(1.0, step, [] { return false; });

    rig.measureClock = true;
    rig.clockMaster = &master;
    rig.clockError = ErrorStats{};
    rig.run(6.0, step, [] { return false; });
    rig.measureClock = false;
    const ClockView view = rig.sys.follower()->clock(rig.lastAfter);
    report.check(rig.engine.beatClock().active() == SyncSource::Network, "the beat clock takes the network as its source");
    report.check(near(rig.engine.beatClock().bpm(), 126.0, 0.1), "the follower takes the tempo of the master", std::to_string(rig.engine.beatClock().bpm()));
    report.check(rig.clockError.count > 50, "the clock error was measured over many frames", std::to_string(rig.clockError.count));
    const std::string errorText = std::to_string(rig.clockError.mean()) + " ms mean, " + std::to_string(rig.clockError.worst) + " ms worst";
    report.check(rig.clockError.mean() < 25.0 && rig.clockError.worst < 100.0, "the beat stays within a frame or two of the master", errorText);
    out["follower"]["clockErrorMs"] = {{"mean", rig.clockError.mean()}, {"worst", rig.clockError.worst}, {"samples", rig.clockError.count}};
    out["follower"]["clock"] = {{"offsetMs", view.offset * 1000.0}, {"roundTripMs", view.roundTrip * 1000.0}, {"jitterMs", view.jitter * 1000.0}, {"driftPpm", view.driftPpm}};

    master.setClock(monotonicNow(), 140.0, true);
    report.check(rig.run(5.0, step, [&] { return near(rig.engine.beatClock().bpm(), 140.0, 0.1); }), "a tempo change of the master arrives");
    master.jump(monotonicNow(), 40.0);
    report.check(rig.run(5.0, step, [&] { return near(rig.engine.beat(), master.beatAt(monotonicNow()), 0.5); }), "a jump of the master beat is followed");

    master.setClock(monotonicNow(), 140.0, false);
    report.check(rig.run(4.0, step, [&] { return rig.engine.paused; }), "pause of the master pauses the follower");
    master.setClock(monotonicNow(), 140.0, true);
    report.check(rig.run(4.0, step, [&] { return !rig.engine.paused; }), "play of the master plays the follower");

    relay.loss = 0.25;
    const double sweepStart = monotonicNow();
    auto sweep = [&](double now) {
        step(now);
        master.set("layer/3/opacity", 0.5f + 0.4f * static_cast<float>(std::sin((now - sweepStart) * 4.0)));
    };
    rig.run(4.0, sweep, [] { return false; });
    master.set("layer/3/opacity", 0.7f);
    report.check(rig.run(8.0, step, [&] { return near(rig.engine.comp.layers[2].opacity, 0.7, 0.003); }), "after a fader sweep under 25 percent loss the final value is exact");
    for (int round = 0; round < 4; ++round) {
        const float value = 0.1f + 0.2f * static_cast<float>(round);
        master.set("layer/3/opacity", value);
        report.check(rig.run(8.0, step, [&] { return near(rig.engine.comp.layers[2].opacity, value, 0.003); }), "with 25 percent packet loss change " + std::to_string(round + 1) + " still arrives");
    }
    out["follower"]["relayDropped"] = relay.dropped;
    report.check(relay.dropped > 20, "the relay really dropped packets", std::to_string(relay.dropped));
    relay.loss = 0.0;

    const float held = rig.engine.comp.layers[2].opacity;
    relay.paused = true;
    report.check(rig.run(10.0, step, [&] { return rig.sys.follower()->phase() == FollowerPhase::Lost; }), "a network outage is noticed");
    master.set("layer/3/opacity", 0.95f);
    const double beatDuringOutage = rig.engine.beat();
    rig.run(1.0, step, [] { return false; });
    report.check(near(rig.engine.comp.layers[2].opacity, held, 0.0001), "during the outage the last values are held");
    report.check(rig.engine.beat() > beatDuringOutage && !rig.engine.paused, "during the outage the beat keeps running at the last tempo");
    report.check(rig.sys.suppressesAutomation(), "the local timeline stays paused during the outage");
    relay.paused = false;
    report.check(rig.run(20.0, step, [&] { return live() && near(rig.engine.comp.layers[2].opacity, 0.95, 0.003); }), "after the outage the follower reconnects and catches up");

    out["follower"]["updateCost"] = rig.updateCost();
    out["follower"]["report"] = rig.sys.report();

    rig.sys.configure(NetSyncSettings{});
    rig.sys.update(1.0 / 60.0, monotonicNow());
    report.check(!rig.sys.running() && rig.sys.localPort() == 0, "turning the sync off closes the port");
    report.check(!rig.sys.suppressesAutomation(), "turning the sync off gives the timeline back");
    master.transport.shutdown(monotonicNow());
}

void testForeignKey(Rig& rig, Report& report) {
    prepare(rig);
    Key other{};
    for (size_t i = 0; i < other.size(); ++i) {
        other[i] = static_cast<std::uint8_t>(200 - i);
    }
    PureMaster master;
    std::string error;
    master.start(other, "stranger", monotonicNow(), error);
    buildShowState(rig.engine.comp, rig.host, master.frame.state);
    master.frame.manifest = buildManifest(rig.engine.comp, rig.host);
    master.set("master/level", 0.11f);
    master.setClock(monotonicNow(), 120.0, true);
    NetSyncSettings settings;
    settings.role = Role::Follower;
    settings.port = master.transport.localPort();
    settings.host = "127.0.0.1";
    rig.sys.configure(settings);
    const float level = rig.engine.comp.master.level;
    rig.run(3.0, [&](double now) { master.step(now); }, [] { return false; });
    report.check(rig.sys.follower() != nullptr && rig.sys.follower()->phase() != FollowerPhase::Live && !rig.sys.follower()->hasState(), "a master with another key is never accepted");
    report.check(master.transport.master() != nullptr && master.transport.master()->followers().empty() && master.transport.master()->stats().badAuth > 0, "the master with another key refuses and counts the packets");
    report.check(near(rig.engine.comp.master.level, level, 1.0e-6), "nothing from a master with another key reaches the show");
    rig.sys.configure(NetSyncSettings{});
    rig.sys.update(1.0 / 60.0, monotonicNow());
    master.transport.shutdown(monotonicNow());
}

void testMaster(Rig& rig, Report& report, json& out) {
    prepare(rig);
    rig.resetUpdateCost();
    rig.engine.comp.bpm = 124.0f;
    Key key{};
    importKey(rig.sys.keyHex(), key);
    NetSyncSettings settings;
    settings.role = Role::Master;
    settings.port = 0;
    settings.allowRemote = false;
    settings.name = "app-master";
    rig.sys.configure(settings);
    rig.sys.update(1.0 / 60.0, monotonicNow());
    report.check(rig.sys.running() && rig.sys.role() == Role::Master && rig.sys.localPort() > 0, "the master opens its port", rig.sys.error());
    report.check(!rig.sys.suppressesAutomation(), "a master keeps its own timeline and cues");

    Transport peer;
    TransportConfig config;
    config.role = Role::Follower;
    config.port = rig.sys.localPort();
    config.host = "127.0.0.1";
    config.name = "peer-follower";
    config.appVersion = "selftest";
    config.key = key;
    config.schema = ShowLayout::get().schema;
    config.stateSize = ShowLayout::get().layout.total();
    peer.configure(config, monotonicNow());
    auto step = [&](double now) { peer.update(now, nullptr); };
    auto peerLive = [&] { return peer.follower() != nullptr && peer.follower()->phase() == FollowerPhase::Live && peer.follower()->hasState(); };
    auto value = [&](const std::string& id) { return peer.follower()->state()[stateIndex(id)]; };
    const ShowLayout& show = ShowLayout::get();

    report.check(rig.run(10.0, step, peerLive), "a follower connects to the master");
    report.check(rig.sys.master() != nullptr && rig.sys.master()->followers().size() == 1 && rig.sys.master()->followers()[0].name == "peer-follower", "the master lists the follower by name");

    rig.app.control().fireTarget("layer/2/opacity", 0.35f);
    rig.app.control().fireTarget("master/level", 0.55f);
    rig.app.control().fireTarget("macro/4", 0.9f);
    rig.app.control().fireTarget("layer/3/fx/1/param/2", 0.7f);
    auto mirrored = [&] {
        return near(value("layer/2/opacity"), 0.35, 0.002) && near(value("master/level"), 0.55, 0.002) && near(value("macro/4"), 0.9, 0.002) && near(value("layer/3/fx/1/param/2"), 0.7, 0.002);
    };
    report.check(rig.run(4.0, step, mirrored), "control changes on the master reach the follower");
    rig.run(1.0, step, [] { return false; });
    std::vector<float> expected;
    buildShowState(rig.engine.comp, rig.host, expected);
    int mismatched = 0;
    const std::vector<float>& got = peer.follower()->state();
    for (size_t i = 0; i < show.layout.controls && i < got.size(); ++i) {
        if (std::fabs(got[i] - expected[i]) > 0.002f) {
            ++mismatched;
        }
    }
    report.check(got.size() == expected.size() && mismatched == 0, "after a settle time every synced control matches on both sides", std::to_string(mismatched) + " differ");
    out["master"]["controlsCompared"] = show.layout.controls;

    const std::uint64_t idleBefore = rig.sys.transport().stats().bytesOut;
    const std::uint64_t idleDatagrams = rig.sys.transport().stats().datagramsOut;
    const double idleStart = monotonicNow();
    rig.run(6.0, step, [] { return false; });
    const double idleSeconds = monotonicNow() - idleStart;
    out["master"]["idleBytesPerSecond"] = static_cast<double>(rig.sys.transport().stats().bytesOut - idleBefore) / idleSeconds;
    out["master"]["idleDatagramsPerSecond"] = static_cast<double>(rig.sys.transport().stats().datagramsOut - idleDatagrams) / idleSeconds;

    const std::uint64_t sweepBefore = rig.sys.transport().stats().bytesOut;
    const std::uint64_t sweepDatagrams = rig.sys.transport().stats().datagramsOut;
    const double sweepStart = monotonicNow();
    rig.run(6.0, [&](double now) {
        step(now);
        const float phase = static_cast<float>((now - sweepStart) * 3.0);
        rig.app.control().fireTarget("layer/3/opacity", 0.5f + 0.4f * std::sin(phase));
        rig.app.control().fireTarget("master/level", 0.5f + 0.4f * std::cos(phase));
        rig.app.control().fireTarget("macro/2", 0.5f + 0.4f * std::sin(phase * 0.7f));
    }, [] { return false; });
    const double sweepSeconds = monotonicNow() - sweepStart;
    out["master"]["sweepBytesPerSecond"] = static_cast<double>(rig.sys.transport().stats().bytesOut - sweepBefore) / sweepSeconds;
    out["master"]["sweepDatagramsPerSecond"] = static_cast<double>(rig.sys.transport().stats().datagramsOut - sweepDatagrams) / sweepSeconds;
    report.check(near(value("layer/3/opacity"), rig.engine.comp.layers[2].opacity, 0.15), "while three faders move the follower stays close to the master");

    rig.engine.trigger(0, 1);
    report.check(rig.run(4.0, step, [&] { return static_cast<int>(std::lround(peer.follower()->state()[show.layout.column(0)])) == 1; }), "a clip started on the master shows up on the follower");
    rig.engine.clearLayer(0);
    report.check(rig.run(4.0, step, [&] { return static_cast<int>(std::lround(peer.follower()->state()[show.layout.column(0)])) == -1; }), "a cleared layer on the master shows up on the follower");

    rig.engine.comp.deckUsed[1] = true;
    rig.engine.comp.decks[1] = emptyDeck();
    rig.engine.switchDeck(1);
    report.check(rig.run(4.0, step, [&] { return static_cast<int>(std::lround(peer.follower()->state()[show.layout.deck()])) == 1; }), "a deck switch on the master reaches the follower");
    rig.engine.switchDeck(0);
    report.check(rig.run(4.0, step, [&] { return static_cast<int>(std::lround(peer.follower()->state()[show.layout.deck()])) == 0; }), "switching back reaches the follower");
    report.check(peer.follower()->manifest().deck == 0 && peer.follower()->manifest().cellKnown[0] && !peer.follower()->manifest().cells[1 * kColumns + 0].empty(), "the follower learns the clip names of the master but never receives media");

    rig.engine.comp.bpm = 133.0f;
    auto bpmArrived = [&] {
        const ClockView view = peer.follower()->clock(monotonicNow());
        return view.valid && near(view.bpm, 133.0, 0.1);
    };
    report.check(rig.run(5.0, step, bpmArrived), "the tempo of the master reaches the follower");
    rig.engine.paused = true;
    report.check(rig.run(4.0, step, [&] { const ClockView view = peer.follower()->clock(monotonicNow()); return view.valid && !view.running; }), "pause on the master reaches the follower");
    rig.engine.paused = false;
    report.check(rig.run(4.0, step, [&] { return peer.follower()->clock(monotonicNow()).running; }), "play on the master reaches the follower");

    rig.run(4.0, step, [] { return false; });
    ErrorStats beatError;
    for (int i = 0; i < 80; ++i) {
        rig.run(0.04, step, [] { return false; });
        const double now = monotonicNow();
        const ClockView view = peer.follower()->clock(now);
        if (view.valid && view.running) {
            const double masterBeat = rig.engine.beat() + (now - rig.lastAfter) * rig.engine.beatClock().bpm() / 60.0;
            beatError.add((view.beat - masterBeat) * 60.0 / std::max(view.bpm, 1.0) * 1000.0);
        }
    }
    const std::string beatText = std::to_string(beatError.mean()) + " ms mean, " + std::to_string(beatError.worst) + " ms worst";
    out["master"]["peerBeatErrorMs"] = {{"mean", beatError.mean()}, {"worst", beatError.worst}, {"samples", beatError.count}};
    report.check(beatError.count > 40 && beatError.mean() < 30.0 && beatError.worst < 120.0, "the beat seen by the follower is within a few frames of the master", beatText);

    out["master"]["updateCost"] = rig.updateCost();
    out["master"]["report"] = rig.sys.report();
    rig.sys.configure(NetSyncSettings{});
    rig.sys.update(1.0 / 60.0, monotonicNow());
    report.check(!rig.sys.running() && rig.sys.localPort() == 0, "turning the master off closes its port");
    peer.shutdown(monotonicNow());
}

}

int runNetSyncSelfTest(App& app, const std::string& outputPath) {
    Report report;
    json out;
    out["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    Rig rig(app);
    const Composition saved = rig.engine.comp;
    Key previous{};
    const bool hadKey = loadKey(previous);
    testDefaults(rig, report, out);
    testFollower(rig, report, out);
    testForeignKey(rig, report);
    testMaster(rig, report, out);
    rig.sys.shutdown(monotonicNow());
    rig.engine.paused = false;
    rig.engine.blackout = false;
    rig.engine.applyComposition(saved);
    if (hadKey) {
        rig.sys.importKey(keyToHex(previous));
    } else {
        std::error_code ec;
        fs::remove(keyPath(), ec);
    }
    out["items"] = report.items;
    out["passed"] = report.passed;
    out["failed"] = report.failed;
    const std::string text = out.dump(2);
    logLine("Network sync self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream file(outputPath);
        file << text << '\n';
    }
    return report.failed == 0 ? 0 : 1;
}

}

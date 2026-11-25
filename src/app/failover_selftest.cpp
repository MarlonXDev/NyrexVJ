#include "app/failover_selftest.hpp"

#include "app/app.hpp"
#include "control/netsync_key.hpp"
#include "control/netsync_show.hpp"
#include "control/netsync_transport.hpp"
#include "control/sync_system.hpp"
#include "core/failover.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "core/settings.hpp"
#include "render/clip_library.hpp"
#include "render/engine.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

void setDataDirectory(const std::string& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void restoreDataDirectory(const std::string& previous, bool existed) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", existed ? previous.c_str() : "");
#else
    if (existed) {
        setenv("NYREXVJ_DATA_DIR", previous.c_str(), 1);
    } else {
        unsetenv("NYREXVJ_DATA_DIR");
    }
#endif
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
            logLine("Failover self test FAILED: " + name + (detail.empty() ? std::string() : " (" + detail + ")"));
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

struct PureMaster {
    Transport transport;
    MasterFrame frame;
    double beatOrigin = 0.0;
    double beatStart = 0.0;
    double bpm = 126.0;
    std::uint32_t epoch = 1;
    std::uint8_t health = 0;
    std::uint64_t showHash = 0;
    bool frozen = false;
    std::string name = "peer-primary";

    double beatAt(double at) const { return beatOrigin + (at - beatStart) * bpm / 60.0; }

    bool start(const Key& key, const std::string& label, std::uint32_t leaderEpoch, int port, double now, std::string& error) {
        name = label;
        epoch = leaderEpoch;
        TransportConfig config;
        config.role = Role::Master;
        config.port = port;
        config.name = label;
        config.appVersion = "selftest";
        config.key = key;
        config.schema = ShowLayout::get().schema;
        config.stateSize = ShowLayout::get().layout.total();
        config.epoch = leaderEpoch;
        transport.configure(config, now);
        error = transport.error();
        beatStart = now;
        return transport.running();
    }

    void step(double now) {
        if (frozen) {
            return;
        }
        frame.clock.beat = beatAt(now);
        frame.clock.bpm = bpm;
        frame.clock.beatsPerBar = 4;
        frame.clock.running = true;
        frame.clock.epoch = epoch;
        frame.clock.health = health;
        frame.clock.showHash = showHash;
        transport.update(now, &frame);
    }

    FollowerInfo follower() const {
        const netsync::Master* master = transport.master();
        if (master == nullptr) {
            return FollowerInfo{};
        }
        const std::vector<FollowerInfo> list = master->followers();
        return list.empty() ? FollowerInfo{} : list.front();
    }
};

struct Rig {
    App& app;
    Engine& engine;
    NetSyncSystem& sys;
    FailoverSystem& fo;
    SyncSystem& sync;
    ReadHost host;
    FailoverEnv env;
    double last = monotonicNow();
    double frameSleep = 0.006;
    double updateSum = 0.0;
    double updateWorst = 0.0;
    long updates = 0;

    explicit Rig(App& target) : app(target), engine(target.engine()), sys(target.netSync()), fo(target.failover()), sync(target.syncSystem()), host(target.engine(), target.control()) {
        env.outputsWanted = 1;
        env.outputsOpen = 1;
        env.snapshotAge = 5.0;
    }

    template <typename Peer, typename Done>
    bool run(double seconds, Peer peer, Done done) {
        const double end = monotonicNow() + seconds;
        while (monotonicNow() < end) {
            const double now = monotonicNow();
            const double dt = std::clamp(now - last, 0.0005, 0.1);
            last = now;
            fo.update(now, dt, env);
            const double spent = monotonicNow() - now;
            updateSum += spent;
            updateWorst = std::max(updateWorst, spent);
            ++updates;
            sys.update(dt, now);
            SyncConfig config;
            config.mode = sys.effectiveSyncMode(SyncMode::Manual);
            config.cues = !sys.suppressesAutomation();
            sync.configure(config);
            sync.update(dt, {}, engine.beatClock(), now);
            peer(now);
            engine.update(dt);
            engine.render();
            sys.afterEngineUpdate(monotonicNow(), dt);
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(frameSleep));
        }
        return done();
    }

    bool follows(const PureMaster& master) const {
        return sys.follower() != nullptr && sys.follower()->phase() == FollowerPhase::Live && sys.follower()->hasState() && master.follower().live;
    }
};

double brightness(Engine& engine) {
    const GLuint texture = engine.outputTexture();
    if (texture == 0) {
        return 0.0;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    int width = 0;
    int height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width <= 0 || height <= 0) {
        return 0.0;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    double total = 0.0;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            total += static_cast<double>(pixels[i]) + static_cast<double>(pixels[i + 1]) + static_cast<double>(pixels[i + 2]);
        }
        total /= static_cast<double>(pixels.size() / 4u) * 3.0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return total;
}

int freePort() {
    UdpSocket probe;
    std::string error;
    if (!probe.openExclusive(0, false, error)) {
        return 0;
    }
    const int port = probe.localPort();
    probe.close();
    return port;
}

void prepare(Rig& rig) {
    Composition base = factoryPresets().front();
    const size_t library = clipLibrary().size();
    for (int layer = 0; layer < kLayerCount; ++layer) {
        for (int column = 0; column < 4; ++column) {
            base.layers[static_cast<size_t>(layer)].cells[static_cast<size_t>(column)] = static_cast<int>((static_cast<size_t>(layer) * 4 + static_cast<size_t>(column)) % library);
        }
        base.layers[static_cast<size_t>(layer)].activeColumn = -1;
    }
    const int plasma = clipIndexByName("Plasma");
    base.layers[0].cells[0] = plasma;
    base.layers[0].opacity = 1.0f;
    base.master.level = 1.0f;
    rig.engine.applyComposition(base);
    rig.engine.setResolutionIndex(0);
    rig.engine.paused = false;
    rig.engine.blackout = false;
    rig.engine.holdOutput = false;
    rig.engine.trigger(0, 0);
}

void primeMaster(Rig& rig, PureMaster& master) {
    buildShowState(rig.engine.comp, rig.host, master.frame.state);
    master.frame.manifest = buildManifest(rig.engine.comp, rig.host);
}

NetSyncSettings followerSettings(int port) {
    NetSyncSettings settings;
    settings.role = Role::Follower;
    settings.port = port;
    settings.host = "127.0.0.1";
    settings.name = "app-standby";
    return settings;
}

FailoverSettings standbySettings(bool automatic, int leaderPort) {
    FailoverSettings settings;
    settings.enabled = true;
    settings.autoTakeover = automatic;
    settings.lostAfter = 3.0;
    settings.holdOutputs = true;
    settings.leaderPort = leaderPort;
    return settings;
}

const failover::Check* find(const failover::Report& report, const std::string& id) {
    return report.find(id);
}

bool level(const failover::Report& report, const std::string& id, failover::Level expected) {
    const failover::Check* check = find(report, id);
    return check != nullptr && check->level == expected;
}

void stepDownBy(Rig& rig, PureMaster& next, std::uint32_t epoch, Report& report, const std::string& label) {
    std::string error;
    const Key key = [&] {
        Key value{};
        importKey(rig.sys.keyHex(), value);
        return value;
    }();
    report.check(next.start(key, label, epoch, 0, monotonicNow(), error), label + " opens its port", error);
    primeMaster(rig, next);
    next.showHash = rig.fo.showHash();
}

void testDefaults(Rig& rig, Report& report) {
    const Settings defaults;
    report.check(!defaults.failoverEnabled && !defaults.failoverAutoTakeover && defaults.failoverHoldOutputs, "hot standby and the automatic takeover are off by default");
    report.check(defaults.failoverLostAfter >= 3.0f && defaults.failoverLeaderPort == 0, "the default loss time and the leader port are set");
    report.check(!rig.fo.enabled() && !rig.engine.holdOutput, "with hot standby off nothing is held back");
    rig.fo.configure(FailoverSettings{});
    rig.fo.update(monotonicNow(), 1.0 / 60.0, rig.env);
    report.check(rig.fo.state() == failover::State::Idle && !rig.engine.holdOutput, "with hot standby off the state stays idle");
    rig.engine.holdOutput = true;
    rig.fo.update(monotonicNow(), 1.0 / 60.0, rig.env);
    report.check(!rig.engine.holdOutput, "turning hot standby off releases a held output");
}

}

int runFailoverSelfTest(App& app, const std::string& outputPath) {
    Report report;
    json out;
    out["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    Rig rig(app);
    const Composition saved = rig.engine.comp;
    const char* current = std::getenv("NYREXVJ_DATA_DIR");
    const bool hadDirectory = current != nullptr;
    const std::string previousDirectory = hadDirectory ? current : "";
    const fs::path root = fs::temp_directory_path() / "nyrexvj_failover_selftest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    setDataDirectory(root.string());

    testDefaults(rig, report);
    const std::string fixedKey = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    rig.sys.importKey(fixedKey);
    Key key{};
    importKey(fixedKey, key);

    prepare(rig);
    const int leaderPort = freePort();
    report.check(leaderPort > 0, "a free port for the leader exists");

    PureMaster primary;
    std::string error;
    report.check(primary.start(key, "peer-primary", 1, 0, monotonicNow(), error) && primary.transport.localPort() > 0, "the simulated primary opens its port", error);
    primeMaster(rig, primary);

    rig.run(0.5, [&](double now) { primary.step(now); }, [] { return false; });
    rig.sys.configure(followerSettings(primary.transport.localPort()));
    rig.fo.configure(standbySettings(true, leaderPort));
    rig.run(0.3, [&](double now) { primary.step(now); }, [&] { return rig.fo.showHash() != 0; });
    primary.showHash = rig.fo.showHash();
    auto stepPrimary = [&](double now) {
        primary.step(now);
        primary.showHash = rig.fo.showHash();
    };

    const double beforeHold = brightness(rig.engine);
    report.check(rig.run(12.0, stepPrimary, [&] { return rig.fo.state() == failover::State::Following; }), "the standby follows the primary");
    report.check(rig.sys.role() == Role::Follower && rig.fo.standby(), "the standby has the follower role");
    report.check(rig.run(12.0, stepPrimary, [&] { return rig.fo.report().overall == failover::Level::Ok; }), "the standby reports itself ready", std::to_string(rig.fo.report().fails) + " fails " + std::to_string(rig.fo.report().warns) + " warnings");
    for (const char* id : {"link", "version", "clock", "state", "assets", "files", "show", "outputs", "decoders", "primary", "snapshot"}) {
        report.check(level(rig.fo.report(), id, failover::Level::Ok), std::string("readiness check ") + id + " is ok");
    }
    report.check(rig.engine.holdOutput, "the output of the standby is held");
    rig.run(0.5, stepPrimary, [] { return false; });
    const double heldBrightness = brightness(rig.engine);
    report.check(heldBrightness < 0.5, "the held output is really black", std::to_string(heldBrightness));
    out["brightness"]["beforeStandby"] = beforeHold;
    out["brightness"]["heldAsStandby"] = heldBrightness;
    rig.run(3.0, stepPrimary, [&] { return primary.follower().reports && primary.follower().readiness == kReadinessReady && primary.follower().showHash == rig.fo.showHash(); });
    const FollowerInfo seen = primary.follower();
    report.check(seen.reports && seen.readiness == kReadinessReady && seen.showHash == rig.fo.showHash() && seen.name == "app-standby", "the primary sees the readiness report of the standby");
    const StandbyView view = rig.fo.standbyView(monotonicNow());
    report.check(!view.present, "a standby has no standby view of its own");

    primary.showHash = rig.fo.showHash() ^ 0x5a5aULL;
    report.check(rig.run(5.0, [&](double now) { primary.step(now); }, [&] { return level(rig.fo.report(), "show", failover::Level::Fail); }), "a different show on the primary makes the standby not ready");
    report.check(rig.fo.report().overall == failover::Level::Fail, "a failed check makes the overall readiness fail");
    report.check(rig.run(8.0, stepPrimary, [&] { return rig.fo.report().overall == failover::Level::Ok; }), "readiness recovers when the show matches again");

    report.check(rig.run(25.0, stepPrimary, [&] { return rig.fo.machine().readySeconds(monotonicNow()) >= 10.5; }), "the standby stays ready for more than ten seconds");
    out["readySecondsBeforeLoss"] = rig.fo.machine().readySeconds(monotonicNow());

    const double silentAt = monotonicNow();
    primary.frozen = true;
    const bool leader = rig.run(3.0 + 6.0, [](double) {}, [&] { return rig.fo.state() == failover::State::Leader; });
    const double leaderAfter = monotonicNow() - silentAt;
    report.check(leader, "the standby takes over by itself after the primary fell silent");
    const failover::Measure measure = rig.fo.machine().measure();
    report.check(measure.taken && measure.automatic && !measure.forced, "the takeover is recorded as automatic");
    report.check(measure.detectionSeconds >= 2.9 && measure.detectionSeconds <= 4.5, "the loss was detected after the configured time", std::to_string(measure.detectionSeconds));
    report.check(leaderAfter < 3.0 + 3.0, "the leader is up within the loss time plus three seconds", std::to_string(leaderAfter));
    report.check(measure.readinessBeforeLoss != failover::Level::Fail && measure.readySecondsBeforeLoss >= 10.0, "the measure records the readiness before the loss", std::to_string(measure.readySecondsBeforeLoss));
    report.check(measure.stateAgeAtTakeover < 6.0, "the state of the new leader is not older than the detection time", std::to_string(measure.stateAgeAtTakeover));
    report.check(rig.run(3.0, [](double) {}, [&] { return rig.sys.running() && rig.sys.role() == Role::Master && rig.sys.localPort() == leaderPort; }), "the leader listens on its own port", std::to_string(rig.sys.localPort()));
    out["automaticTakeover"] = {{"silentToLeaderSeconds", leaderAfter}, {"detectionSeconds", measure.detectionSeconds}, {"decisionSeconds", measure.decisionSeconds}, {"stateAgeSeconds", measure.stateAgeAtTakeover}, {"servingAfterDecisionSeconds", rig.fo.switchSeconds()}};
    report.check(rig.sys.epoch() == 2, "the leader epoch is one higher than the old one", std::to_string(rig.sys.epoch()));
    report.check(!rig.engine.holdOutput, "the leader releases its output");
    rig.run(1.0, [](double) {}, [] { return false; });
    const double liveBrightness = brightness(rig.engine);
    report.check(liveBrightness > 2.0, "the output of the leader shows the picture", std::to_string(liveBrightness));
    out["brightness"]["leader"] = liveBrightness;
    report.check(rig.fo.switchSeconds() >= 0.0 && rig.fo.switchSeconds() < 1.0, "the leader serves within one second of the decision", std::to_string(rig.fo.switchSeconds()));

    Transport probe;
    TransportConfig probeConfig;
    probeConfig.role = Role::Follower;
    probeConfig.port = leaderPort;
    probeConfig.host = "127.0.0.1";
    probeConfig.name = "probe";
    probeConfig.appVersion = "selftest";
    probeConfig.key = key;
    probeConfig.schema = ShowLayout::get().schema;
    probeConfig.stateSize = ShowLayout::get().layout.total();
    probe.configure(probeConfig, monotonicNow());
    auto probeStep = [&](double now) { probe.update(now, nullptr); };
    auto probeLive = [&] { return probe.follower() != nullptr && probe.follower()->phase() == FollowerPhase::Live && probe.follower()->hasState(); };
    report.check(rig.run(10.0, probeStep, probeLive), "a PC can follow the new leader");
    report.check(probe.follower() != nullptr && probe.follower()->primary().known && probe.follower()->primary().epoch == 2, "the new leader announces its epoch");

    primary.frozen = false;
    std::vector<TakeoverNotice> notices;
    report.check(rig.run(10.0, [&](double now) {
        primary.step(now);
        probeStep(now);
        if (primary.transport.master() != nullptr) {
            const std::vector<TakeoverNotice> fresh = primary.transport.master()->takeTakeovers();
            notices.insert(notices.end(), fresh.begin(), fresh.end());
        }
    }, [&] { return !notices.empty(); }), "the old primary receives the takeover notice when it is reachable again");
    if (!notices.empty()) {
        report.check(notices.front().epoch == 2 && notices.front().port == leaderPort && notices.front().name == "app-standby" && notices.front().peer.address == 0x7f000001u, "the notice carries epoch, port and name of the leader");
    }
    primary.transport.shutdown(monotonicNow());

    PureMaster second;
    stepDownBy(rig, second, 3, report, "peer-second");
    const int secondPort = second.transport.localPort();
    bool gotBye = false;
    std::uint8_t byeReason = 99;
    auto announce = [&](double now) {
        second.step(now);
        probeStep(now);
        if (probe.follower() != nullptr && probe.follower()->primary().byeSeen) {
            gotBye = true;
            byeReason = probe.follower()->primary().byeReason;
        }
    };
    rig.run(0.3, announce, [] { return false; });
    bool sent = false;
    const bool steppedDown = rig.run(12.0, [&](double now) {
        announce(now);
        if (!sent && second.transport.master() != nullptr) {
            sent = true;
            second.transport.master()->announceTakeover(Peer{0x7f000001u, static_cast<std::uint16_t>(leaderPort)}, 3, static_cast<std::uint16_t>(secondPort), "peer-second");
        }
    }, [&] { return rig.sys.role() == Role::Follower && rig.sys.epoch() == 3; });
    report.check(steppedDown, "a leader with a higher epoch makes the old leader step down");
    report.check(rig.run(10.0, announce, [&] { return rig.follows(second); }), "the leader that stepped down follows the new leader");
    report.check(rig.engine.holdOutput, "the PC that stepped down holds its output again");
    rig.run(0.5, announce, [] { return false; });
    const double steppedBrightness = brightness(rig.engine);
    report.check(steppedBrightness < 0.5, "the output of the PC that stepped down is black", std::to_string(steppedBrightness));
    report.check(gotBye && byeReason == kByeSuperseded, "followers of the old leader are told that it was superseded", std::to_string(byeReason));

    rig.fo.rearm();
    report.check(rig.run(25.0, [&](double now) {
        second.step(now);
        second.showHash = rig.fo.showHash();
    }, [&] { return rig.fo.machine().readySeconds(monotonicNow()) >= 10.5 && rig.fo.state() == failover::State::Following; }), "the standby becomes ready again behind the new primary");
    second.transport.shutdown(monotonicNow());
    const double byeAt = monotonicNow();
    bool tookOver = false;
    rig.run(6.0, [](double) {}, [&] {
        tookOver = rig.fo.state() == failover::State::Leader;
        return tookOver;
    });
    report.check(!tookOver && monotonicNow() - byeAt >= 5.5, "after a clean close of the primary there is no automatic takeover");
    report.check(rig.fo.state() == failover::State::PrimaryEnded && rig.fo.machine().autoBlockedByEnd(), "the state says the primary was closed on purpose");
    report.check(rig.fo.requestTakeover(monotonicNow()), "a takeover can be requested by hand");
    report.check(!rig.fo.confirmTakeover(monotonicNow()), "the confirmation is refused right away");
    rig.run(0.7, [](double) {}, [] { return false; });
    report.check(rig.fo.confirmTakeover(monotonicNow()), "the confirmation after a short wait is accepted");
    report.check(rig.run(4.0, [](double) {}, [&] { return rig.fo.state() == failover::State::Leader && rig.sys.role() == Role::Master; }), "the PC becomes the leader after the confirmation");
    report.check(!rig.fo.machine().measure().automatic && rig.sys.epoch() == 4, "the takeover by hand is recorded as manual", std::to_string(rig.sys.epoch()));
    report.check(!rig.engine.holdOutput, "the output is released after the takeover by hand");
    probe.shutdown(monotonicNow());

    PureMaster third;
    stepDownBy(rig, third, 5, report, "peer-third");
    const int thirdPort = third.transport.localPort();
    bool thirdSent = false;
    auto thirdStep = [&](double now) {
        third.step(now);
        third.showHash = rig.fo.showHash();
        if (!thirdSent && third.transport.master() != nullptr) {
            thirdSent = true;
            third.transport.master()->announceTakeover(Peer{0x7f000001u, static_cast<std::uint16_t>(leaderPort)}, 5, static_cast<std::uint16_t>(thirdPort), "peer-third");
        }
    };
    rig.run(0.3, thirdStep, [] { return false; });
    report.check(rig.run(12.0, thirdStep, [&] { return rig.sys.role() == Role::Follower && rig.follows(third); }), "the standby follows a third primary after stepping down");
    rig.fo.rearm();
    report.check(rig.run(25.0, thirdStep, [&] { return rig.fo.machine().readySeconds(monotonicNow()) >= 10.5; }), "the standby is ready with the automatic takeover armed");

    const fs::path mediaFile = root / "media" / "failprobe.png";
    fs::create_directories(mediaFile.parent_path(), ec);
    {
        std::ofstream file(mediaFile, std::ios::binary);
        file << "this is not a picture";
    }
    rescanLibrary();
    rig.engine.syncLibrary();
    const int broken = clipIndexByName(imageClipName("failprobe"));
    report.check(broken >= 0, "a clip with a broken file is added to the library");
    if (broken >= 0) {
        rig.engine.comp.layers[1].cells[0] = broken;
        rig.engine.trigger(1, 0);
    }
    report.check(rig.run(10.0, thirdStep, [&] { return rig.fo.decoderFaults() > 0; }), "a playing clip that cannot be decoded is noticed");
    report.check(!rig.fo.faultNames().empty() && rig.fo.faultNames().front() == imageClipName("failprobe"), "the failed clip is named");
    report.check(rig.run(5.0, thirdStep, [&] { return rig.fo.report().overall == failover::Level::Fail; }), "the failed decoder makes the standby not ready");
    report.check(level(rig.fo.report(), "decoders", failover::Level::Fail), "the decoder check names the fault");
    report.check(rig.run(5.0, thirdStep, [&] {
        const FollowerInfo info = third.follower();
        return (info.health & kHealthDecoder) != 0 && info.readiness == kReadinessNotReady;
    }), "the primary is told about the decoder fault of the standby");
    third.frozen = true;
    bool autoDespiteFault = false;
    rig.run(3.0 + 3.0, [](double) {}, [&] {
        autoDespiteFault = rig.fo.state() == failover::State::Leader;
        return autoDespiteFault;
    });
    report.check(!autoDespiteFault && rig.fo.state() == failover::State::PrimaryLost, "a standby with a decoder fault does not take over by itself");
    report.check(rig.fo.requestTakeover(monotonicNow()), "the takeover by hand is still possible");
    rig.run(0.7, [](double) {}, [] { return false; });
    report.check(rig.fo.confirmTakeover(monotonicNow()) && rig.run(4.0, [](double) {}, [&] { return rig.fo.state() == failover::State::Leader; }), "the takeover by hand works with a known fault");
    report.check(rig.fo.machine().measure().readinessBeforeLoss == failover::Level::Fail, "the measure records that the standby was not ready");
    third.transport.shutdown(monotonicNow());

    bool eventsHaveTakeover = false;
    bool eventsHaveStepDown = false;
    for (const FailoverEvent& event : rig.fo.events()) {
        eventsHaveTakeover = eventsHaveTakeover || event.kind == "takeover-automatic" || event.kind == "takeover-manual";
        eventsHaveStepDown = eventsHaveStepDown || event.kind == "stepped-down";
    }
    report.check(eventsHaveTakeover && eventsHaveStepDown, "the event log lists takeovers and the step down");
    out["failover"] = rig.fo.json(monotonicNow());
    out["updateCost"] = {{"meanMicroseconds", rig.updates > 0 ? rig.updateSum / static_cast<double>(rig.updates) * 1.0e6 : 0.0}, {"worstMicroseconds", rig.updateWorst * 1.0e6}, {"calls", rig.updates}};

    rig.fo.configure(FailoverSettings{});
    rig.fo.update(monotonicNow(), 1.0 / 60.0, rig.env);
    rig.sys.configure(NetSyncSettings{});
    rig.sys.update(1.0 / 60.0, monotonicNow());
    report.check(!rig.sys.running() && !rig.engine.holdOutput, "turning everything off closes the port and releases the output");
    rig.sys.shutdown(monotonicNow());
    rig.engine.paused = false;
    rig.engine.blackout = false;
    rig.engine.holdOutput = false;
    rig.engine.applyComposition(saved);
    restoreDataDirectory(previousDirectory, hadDirectory);
    fs::remove_all(root, ec);

    out["items"] = report.items;
    out["passed"] = report.passed;
    out["failed"] = report.failed;
    const std::string text = out.dump(2);
    logLine("Failover self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream file(outputPath);
        file << text << '\n';
    }
    return report.failed == 0 ? 0 : 1;
}

}

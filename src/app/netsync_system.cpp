#include "app/netsync_system.hpp"

#include "control/netsync_key.hpp"
#include "core/log.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef NYREXVJ_VERSION
#define NYREXVJ_VERSION "dev"
#endif

namespace nvj {

class EngineShowHost final : public netsync::ShowHost {
public:
    EngineShowHost(Engine& engine, ControlSystem& control) : engine_(engine), control_(control) {}

    float controlValue(int target) const override { return control_.router().value(target); }
    void setControl(int target, float value) override { control_.setTarget(target, value); }
    void triggerClip(int layer, int column) override { engine_.trigger(layer, column); }
    void clearLayer(int layer) override { engine_.clearLayer(layer); }
    bool switchDeck(int deck) override { return engine_.switchDeck(deck); }
    std::uint32_t triggerCount(int layer) const override { return engine_.triggerCount(layer); }
    std::string clipName(int clip) const override {
        const std::vector<ClipDef>& library = clipLibrary();
        if (clip < 0 || static_cast<size_t>(clip) >= library.size()) {
            return std::string();
        }
        return library[static_cast<size_t>(clip)].name;
    }
    int clipIndex(const std::string& name) const override {
        const std::vector<ClipDef>& library = clipLibrary();
        for (size_t i = 0; i < library.size(); ++i) {
            if (netsync::limitName(library[i].name) == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

private:
    Engine& engine_;
    ControlSystem& control_;
};

NetSyncSystem::NetSyncSystem(Engine& engine, ControlSystem& control, SyncSystem& sync) : engine_(engine), control_(control), sync_(sync), host_(std::make_unique<EngineShowHost>(engine, control)) {}

NetSyncSystem::~NetSyncSystem() = default;

void NetSyncSystem::configure(const NetSyncSettings& settings) {
    wanted_ = settings;
}

bool NetSyncSystem::ensureKey() {
    if (haveKey_) {
        return true;
    }
    bool created = false;
    if (!netsync::ensureKey(key_, created)) {
        return false;
    }
    haveKey_ = true;
    if (created && !netsync::saveKey(key_)) {
        warning_ = "The key could not be saved and will change at the next start";
        logLine("Network sync: the key file could not be written to " + netsync::keyPath());
    }
    return true;
}

std::string NetSyncSystem::keyHex() const {
    return haveKey_ ? netsync::keyToHex(key_) : std::string();
}

std::string NetSyncSystem::keyFingerprint() const {
    return haveKey_ ? netsync::keyFingerprint(key_) : std::string();
}

bool NetSyncSystem::importKey(const std::string& text) {
    netsync::Key parsed{};
    if (!netsync::importKey(text, parsed)) {
        return false;
    }
    key_ = parsed;
    haveKey_ = true;
    warning_.clear();
    if (!netsync::saveKey(key_)) {
        warning_ = "The key could not be saved and will change at the next start";
        logLine("Network sync: the key file could not be written to " + netsync::keyPath());
    }
    return true;
}

bool NetSyncSystem::regenerateKey() {
    netsync::Key fresh{};
    if (!netsync::generateKey(fresh)) {
        error_ = "Could not create a secure key for the network sync";
        return false;
    }
    key_ = fresh;
    haveKey_ = true;
    warning_.clear();
    if (!netsync::saveKey(key_)) {
        warning_ = "The key could not be saved and will change at the next start";
        logLine("Network sync: the key file could not be written to " + netsync::keyPath());
    }
    return true;
}

void NetSyncSystem::noteCues(size_t count) {
    cues_ += static_cast<std::uint32_t>(count);
}

void NetSyncSystem::afterEngineUpdate(double now, double dt) {
    advanceStamp_ = now;
    lastAdvanceDt_ = dt;
    if (recording_ && samples_.size() < 200000) {
        const double wall = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        samples_.push_back({wall, engine_.beat(), engine_.beatClock().bpm(), engine_.paused ? 1.0 : 0.0});
    }
}

bool NetSyncSystem::suppressesAutomation() const {
    return transport_.role() == netsync::Role::Follower && wanted_.follow.controls;
}

bool NetSyncSystem::followingClock() const {
    const netsync::Follower* follower = transport_.follower();
    return follower != nullptr && wanted_.follow.clock && follower->phase() == netsync::FollowerPhase::Live && follower->clock(advanceStamp_ > 0.0 ? advanceStamp_ : 0.0).valid;
}

SyncMode NetSyncSystem::effectiveSyncMode(SyncMode user) const {
    if (transport_.role() == netsync::Role::Follower && wanted_.follow.clock && user == SyncMode::Manual) {
        return SyncMode::Network;
    }
    return user;
}

void NetSyncSystem::clearFollowerState() {
    show_.reset();
    followApplied_ = false;
    manifestSeen_ = ~0ull;
    nextEvaluate_ = 0.0;
    lastRunning_ = -1;
    if (networkTimecodeActive_) {
        sync_.clearNetworkTimecode();
        networkTimecodeActive_ = false;
    }
}

void NetSyncSystem::update(double dt, double now) {
    error_.clear();
    netsync::TransportConfig config;
    config.role = wanted_.role;
    if (config.role != netsync::Role::Off && !ensureKey()) {
        config.role = netsync::Role::Off;
        error_ = "Could not create a secure key for the network sync";
    }
    config.port = wanted_.port;
    config.allowRemote = wanted_.allowRemote;
    config.host = wanted_.host;
    config.name = netsync::limitName(wanted_.name.empty() ? std::string("NyrexVJ") : wanted_.name);
    config.appVersion = NYREXVJ_VERSION;
    config.key = haveKey_ ? key_ : netsync::Key{};
    config.schema = netsync::ShowLayout::get().schema;
    config.stateSize = netsync::ShowLayout::get().layout.total();
    transport_.configure(config, now);
    if (transport_.generation() != generation_) {
        generation_ = transport_.generation();
        clearFollowerState();
        haveLastBeat_ = false;
        nextManifest_ = 0.0;
    }
    if (transport_.role() == netsync::Role::Master && transport_.running()) {
        updateMaster(dt, now);
    } else if (transport_.role() == netsync::Role::Follower && transport_.running()) {
        updateFollower(now);
    } else {
        transport_.update(now, nullptr);
        if (transport_.role() == netsync::Role::Off) {
            clearFollowerState();
        }
    }
}

void NetSyncSystem::updateMaster(double dt, double now) {
    (void)dt;
    netsync::buildShowState(engine_.comp, *host_, frame_.state);
    const BeatClock& clock = engine_.beatClock();
    const bool running = !engine_.paused;
    const double bpm = clock.bpm();
    const double raw = clock.beat();
    if (haveLastBeat_) {
        const double expected = running ? lastAdvanceDt_ * bpm / 60.0 : 0.0;
        if (std::fabs((raw - lastBeat_) - expected) > 0.35 + 0.3 * expected) {
            ++jumps_;
        }
    }
    lastBeat_ = raw;
    haveLastBeat_ = true;
    const double since = advanceStamp_ > 0.0 ? std::clamp(now - advanceStamp_, 0.0, 0.25) : 0.0;
    netsync::ClockData& data = frame_.clock;
    data.beat = raw + (running ? since * bpm / 60.0 : 0.0);
    data.bpm = bpm;
    data.beatsPerBar = engine_.comp.beatsPerBar;
    data.running = running;
    data.locked = clock.locked() && clock.active() != SyncSource::Manual;
    data.timecodeValid = sync_.timecodeActive() && sync_.timecodeSource() != TimecodeSource::Network;
    data.timecode = data.timecodeValid ? sync_.timecodeSeconds() : 0.0;
    data.timecodeRate = data.timecodeValid ? static_cast<int>(sync_.timecode().rate) : 0;
    data.jumps = static_cast<std::uint16_t>(jumps_ & 0xffff);
    data.cues = cues_;
    if (now >= nextManifest_) {
        frame_.manifest = netsync::buildManifest(engine_.comp, *host_);
        nextManifest_ = now + 0.25;
    }
    transport_.update(now, &frame_);
}

void NetSyncSystem::updateFollower(double now) {
    transport_.update(now, nullptr);
    netsync::Follower* follower = transport_.follower();
    if (follower == nullptr) {
        return;
    }
    const netsync::Follow follow = wanted_.follow;
    const std::vector<netsync::Change> changes = follower->takeChanges();
    if (follower->hasState() && (!changes.empty() || !followApplied_ || !(follow == lastFollow_))) {
        show_.apply(changes, follower->state(), follow, engine_.comp, *host_);
        followApplied_ = true;
        lastFollow_ = follow;
    }
    if (follower->manifestVersion() != manifestSeen_ || now >= nextEvaluate_) {
        show_.evaluate(follower->manifest(), engine_.comp, *host_);
        manifestSeen_ = follower->manifestVersion();
        nextEvaluate_ = now + 1.0;
    }
    const netsync::ClockView view = advanceStamp_ > 0.0 ? follower->clock(advanceStamp_) : netsync::ClockView{};
    if (follow.clock && view.valid && follower->phase() == netsync::FollowerPhase::Live) {
        SyncObservation observation;
        observation.source = SyncSource::Network;
        observation.hasPhase = true;
        observation.beat = view.beat;
        observation.modulus = 0;
        observation.hasTempo = true;
        observation.bpm = view.bpm;
        observation.age = 0.0;
        observation.jump = follower->takeJump();
        engine_.beatClock().report(observation);
        const int running = view.running ? 1 : 0;
        if (running != lastRunning_) {
            engine_.paused = !view.running;
            lastRunning_ = running;
            ++pausesMirrored_;
        }
        if (view.timecodeValid) {
            const int rate = std::clamp(view.timecodeRate, 0, 3);
            sync_.reportNetworkTimecode(view.timecode, 0.0, static_cast<FrameRate>(rate));
            networkTimecodeActive_ = true;
        } else if (networkTimecodeActive_) {
            sync_.clearNetworkTimecode();
            networkTimecodeActive_ = false;
        }
    } else {
        if (networkTimecodeActive_) {
            sync_.clearNetworkTimecode();
            networkTimecodeActive_ = false;
        }
        lastRunning_ = -1;
    }
}

void NetSyncSystem::shutdown(double now) {
    transport_.shutdown(now);
    clearFollowerState();
}

namespace {

const char* phaseName(netsync::FollowerPhase phase) {
    switch (phase) {
    case netsync::FollowerPhase::Connecting:
        return "connecting";
    case netsync::FollowerPhase::Syncing:
        return "syncing";
    case netsync::FollowerPhase::Live:
        return "live";
    case netsync::FollowerPhase::Lost:
        return "lost";
    default:
        return "idle";
    }
}

const char* compatName(netsync::Compat compat) {
    switch (compat) {
    case netsync::Compat::Full:
        return "full";
    case netsync::Compat::ClockOnly:
        return "clock-only";
    default:
        return "refused";
    }
}

nlohmann::json statsJson(const netsync::Stats& s) {
    nlohmann::json j;
    j["received"] = s.received;
    j["sent"] = s.sent;
    j["badAuth"] = s.badAuth;
    j["malformed"] = s.malformed;
    j["stray"] = s.stray;
    j["replays"] = s.replays;
    j["refused"] = s.refused;
    j["limited"] = s.limited;
    j["unknownSession"] = s.unknownSession;
    j["duplicates"] = s.duplicates;
    j["gaps"] = s.gaps;
    j["resyncs"] = s.resyncs;
    j["statesApplied"] = s.statesApplied;
    j["deltasApplied"] = s.deltasApplied;
    j["clockBeacons"] = s.clockBeacons;
    j["pongs"] = s.pongs;
    j["rejects"] = s.rejects;
    return j;
}

}

nlohmann::json NetSyncSystem::report() const {
    nlohmann::json j;
    j["role"] = netsync::roleName(transport_.role());
    j["running"] = transport_.running();
    j["error"] = error();
    j["warning"] = warning_;
    j["port"] = transport_.localPort();
    j["protocol"] = netsync::kProtocol;
    j["schema"] = netsync::ShowLayout::get().schema;
    j["stateValues"] = netsync::ShowLayout::get().layout.total();
    const netsync::TransportStats& t = transport_.stats();
    j["transport"] = {{"datagramsIn", t.datagramsIn}, {"datagramsOut", t.datagramsOut}, {"bytesIn", t.bytesIn}, {"bytesOut", t.bytesOut}, {"sendFailures", t.sendFailures}, {"foreign", t.foreign}};
    if (const netsync::Master* m = transport_.master()) {
        j["stats"] = statsJson(m->stats());
        nlohmann::json list = nlohmann::json::array();
        for (const netsync::FollowerInfo& info : m->followers()) {
            nlohmann::json f;
            f["name"] = info.name;
            f["version"] = info.appVersion;
            f["compat"] = compatName(info.compat);
            f["live"] = info.live;
            f["rttMs"] = info.rtt * 1000.0;
            f["packetsIn"] = info.packetsIn;
            f["packetsOut"] = info.packetsOut;
            f["reportedSeq"] = info.reportedSeq;
            f["knownSeq"] = info.knownSeq;
            list.push_back(std::move(f));
        }
        j["followers"] = std::move(list);
    }
    if (const netsync::Follower* f = transport_.follower()) {
        j["stats"] = statsJson(f->stats());
        j["phase"] = phaseName(f->phase());
        j["compat"] = compatName(f->compat());
        j["masterName"] = f->masterName();
        j["masterVersion"] = f->masterVersion();
        j["hasState"] = f->hasState();
        j["stateSeq"] = f->stateSeq();
        const netsync::ClockView view = advanceStamp_ > 0.0 ? f->clock(advanceStamp_) : netsync::ClockView{};
        j["clock"] = {{"valid", view.valid}, {"locked", view.locked}, {"running", view.running}, {"offsetMs", view.offset * 1000.0}, {"roundTripMs", view.roundTrip * 1000.0}, {"jitterMs", view.jitter * 1000.0}, {"driftPpm", view.driftPpm}, {"ageMs", view.age * 1000.0}, {"beat", view.beat}, {"bpm", view.bpm}};
        const netsync::ApplyStats& a = show_.stats();
        j["applied"] = {{"controls", a.controls}, {"clipsStarted", a.clipsStarted}, {"clipsCleared", a.clipsCleared}, {"clipsSkipped", a.clipsSkipped}, {"decksSwitched", a.decksSwitched}, {"decksSkipped", a.decksSkipped}, {"rejected", a.rejected}};
        nlohmann::json list = nlohmann::json::array();
        for (const netsync::Mismatch& mismatch : show_.mismatches()) {
            nlohmann::json entry;
            entry["layer"] = mismatch.layer;
            entry["column"] = mismatch.column;
            entry["deck"] = mismatch.deck;
            entry["name"] = mismatch.name;
            entry["local"] = mismatch.local;
            switch (mismatch.kind) {
            case netsync::MismatchKind::MissingClip:
                entry["kind"] = "missing-clip";
                break;
            case netsync::MismatchKind::DifferentClip:
                entry["kind"] = "different-clip";
                break;
            case netsync::MismatchKind::MissingDeck:
                entry["kind"] = "missing-deck";
                break;
            default:
                entry["kind"] = "different-deck";
                break;
            }
            list.push_back(std::move(entry));
        }
        j["mismatches"] = std::move(list);
    }
    return j;
}

}

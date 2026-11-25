#include "app/failover_system.hpp"

#include "core/composition.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace nvj {

namespace {

std::string formatIpv4(std::uint32_t address) {
    return std::to_string((address >> 24) & 255u) + "." + std::to_string((address >> 16) & 255u) + "." + std::to_string((address >> 8) & 255u) + "." + std::to_string(address & 255u);
}

std::string clipNameAt(int clip) {
    const std::vector<ClipDef>& library = clipLibrary();
    if (clip < 0 || static_cast<size_t>(clip) >= library.size()) {
        return std::string();
    }
    return library[static_cast<size_t>(clip)].name;
}

const char* levelName(failover::Level level) {
    switch (level) {
    case failover::Level::Ok:
        return "ok";
    case failover::Level::Warn:
        return "warn";
    default:
        return "fail";
    }
}

}

FailoverSystem::FailoverSystem(Engine& engine, NetSyncSystem& net) : engine_(engine), net_(net) {}

void FailoverSystem::configure(const FailoverSettings& settings) {
    settings_ = settings;
    failover::Thresholds thresholds = machine_.thresholds();
    thresholds.lostAfter = settings.lostAfter;
    machine_.configure(thresholds);
}

bool FailoverSystem::standby() const {
    return settings_.enabled && net_.role() == netsync::Role::Follower;
}

bool FailoverSystem::primary() const {
    return settings_.enabled && net_.role() == netsync::Role::Master;
}

void FailoverSystem::log(double now, const std::string& kind, const std::string& detail) {
    FailoverEvent event;
    event.time = now;
    event.kind = kind;
    event.detail = detail;
    events_.push_back(std::move(event));
    if (events_.size() > 200) {
        events_.erase(events_.begin(), events_.begin() + 50);
    }
}

void FailoverSystem::shutdown() {
    engine_.holdOutput = false;
    net_.setFailoverReport(0, 0, 0);
}

void FailoverSystem::refreshLocal(double now, const FailoverEnv& env) {
    outputsWanted_ = env.outputsWanted;
    outputsOpen_ = env.outputsOpen;
    faultNames_.clear();
    decoderFaults_ = engine_.playingFaults(&faultNames_);
    localHealth_ = 0;
    if (decoderFaults_ > 0) {
        localHealth_ |= netsync::kHealthDecoder;
    }
    if (env.outputsWanted > 0 && env.outputsOpen == 0) {
        localHealth_ |= netsync::kHealthOutput;
    }
    if (now >= nextHash_) {
        nextHash_ = now + 1.0;
        const Composition& comp = engine_.comp;
        const DeckData active = captureDeck(comp);
        std::vector<std::string> lines;
        for (int d = 0; d < kDeckCount; ++d) {
            const size_t di = static_cast<size_t>(d);
            if (!comp.deckUsed[di]) {
                continue;
            }
            lines.push_back("deck:" + std::to_string(d) + ":" + comp.deckNames[di]);
            const DeckData& data = d == comp.activeDeck ? active : comp.decks[di];
            for (size_t l = 0; l < data.layers.size(); ++l) {
                for (size_t c = 0; c < data.layers[l].cells.size(); ++c) {
                    lines.push_back("cell:" + std::to_string(d) + ":" + std::to_string(l) + ":" + std::to_string(c) + ":" + clipNameAt(data.layers[l].cells[c]));
                }
            }
        }
        showHash_ = failover::showFingerprint(lines);
    }
    if (now >= nextFiles_) {
        nextFiles_ = now + 10.0;
        std::set<int> used;
        const Composition& comp = engine_.comp;
        const DeckData active = captureDeck(comp);
        for (int d = 0; d < kDeckCount; ++d) {
            const size_t di = static_cast<size_t>(d);
            if (!comp.deckUsed[di]) {
                continue;
            }
            const DeckData& data = d == comp.activeDeck ? active : comp.decks[di];
            for (const Layer& layer : data.layers) {
                for (int clip : layer.cells) {
                    if (clip >= 0) {
                        used.insert(clip);
                    }
                }
            }
        }
        const std::vector<ClipDef>& library = clipLibrary();
        int missing = 0;
        for (int clip : used) {
            if (static_cast<size_t>(clip) >= library.size()) {
                continue;
            }
            const std::string& path = library[static_cast<size_t>(clip)].path;
            std::error_code ec;
            if (!path.empty() && !std::filesystem::exists(std::filesystem::u8path(path), ec)) {
                ++missing;
            }
        }
        missingFiles_ = missing;
    }
}

void FailoverSystem::update(double now, double dt, const FailoverEnv& env) {
    frameSeconds_ = dt;
    if (!settings_.enabled) {
        if (engine_.holdOutput) {
            engine_.holdOutput = false;
        }
        net_.setFailoverReport(0, 0, 0);
        failover::Inputs idle;
        machine_.update(now, idle);
        return;
    }
    refreshLocal(now, env);
    const netsync::Role role = net_.role();
    if (role == netsync::Role::Follower) {
        updateStandby(now, env);
    } else if (role == netsync::Role::Master) {
        updatePrimary(now);
    } else {
        failover::Inputs in;
        in.enabled = true;
        machine_.update(now, in);
        net_.setFailoverReport(0, localHealth_, showHash_);
    }
    engine_.holdOutput = settings_.holdOutputs && role == netsync::Role::Follower && machine_.state() != failover::State::Leader;
    if (machine_.state() != lastState_) {
        log(now, "state", failover::stateId(machine_.state()));
        lastState_ = machine_.state();
    }
}

void FailoverSystem::updateStandby(double now, const FailoverEnv& env) {
    (void)env;
    const netsync::Follower* follower = net_.follower();
    failover::Facts facts;
    failover::Inputs in;
    in.enabled = true;
    in.standby = true;
    in.autoArmed = settings_.autoTakeover;
    netsync::PrimaryView primaryView;
    if (follower != nullptr) {
        primaryView = follower->primary();
        const bool live = follower->phase() == netsync::FollowerPhase::Live;
        const netsync::ClockView clock = follower->clock(now);
        facts.linkLive = live;
        facts.compatFull = follower->compat() == netsync::Compat::Full;
        facts.clockReady = follower->estimator().ready();
        facts.roundTrip = clock.roundTrip;
        facts.jitter = clock.jitter;
        facts.hasState = follower->hasState();
        facts.stateAge = follower->lastHeard() > 0.0 ? std::max(now - follower->lastHeard(), 0.0) : 1e9;
        in.linkLive = live;
        in.primaryAge = facts.stateAge;
        in.byeSeen = primaryView.byeSeen;
        in.byeReason = primaryView.byeReason;
        in.primaryHealth = primaryView.health;
    }
    for (const netsync::Mismatch& m : net_.mismatches()) {
        if (m.kind == netsync::MismatchKind::MissingClip) {
            ++facts.missingClips;
        } else {
            ++facts.otherDifferences;
        }
    }
    facts.localMissingFiles = missingFiles_;
    facts.showHashKnown = primaryView.known && primaryView.showHash != 0;
    facts.showHashMatches = facts.showHashKnown && primaryView.showHash == showHash_;
    facts.outputsWanted = outputsWanted_;
    facts.outputsOpen = outputsOpen_;
    facts.decoderFaults = decoderFaults_;
    facts.primaryHealth = primaryView.health;
    facts.sessionSnapshotAge = env.snapshotAge;
    report_ = failover::evaluateReadiness(facts, machine_.thresholds());
    in.readiness = report_.overall;
    in.localOk = decoderFaults_ == 0 && (outputsWanted_ == 0 || outputsOpen_ > 0);
    net_.setFailoverReport(report_.wire, localHealth_, showHash_);
    if (!haveLevel_ || report_.overall != lastLevel_) {
        log(now, "readiness", levelName(report_.overall));
        lastLevel_ = report_.overall;
        haveLevel_ = true;
    }
    machine_.update(now, in);
    const failover::Decision decision = machine_.takeDecision();
    if (decision.action == failover::Action::TakeOver) {
        executeTakeover(now, decision);
    }
}

void FailoverSystem::executeTakeover(double now, const failover::Decision& decision) {
    const NetSyncSettings eff = net_.effective();
    const netsync::Follower* follower = net_.follower();
    std::uint32_t seen = net_.epoch();
    if (follower != nullptr && follower->primary().known) {
        seen = std::max(seen, follower->primary().epoch);
    }
    const std::uint32_t epoch = seen + 1;
    announceHost_ = eff.host;
    announcePort_ = eff.port;
    announceEpoch_ = epoch;
    announceActive_ = true;
    announceNext_ = now;
    announceCount_ = 0;
    const int leaderPort = settings_.leaderPort >= 1024 && settings_.leaderPort <= 65535 ? settings_.leaderPort : eff.port;
    net_.becomeLeader(epoch, leaderPort);
    machine_.tookOver(now);
    engine_.holdOutput = false;
    servingPending_ = true;
    confirmedAt_ = now;
    switchSeconds_ = 0.0;
    leaderLogged_ = false;
    log(now, decision.automatic ? "takeover-automatic" : (decision.primaryAlive ? "takeover-forced" : "takeover-manual"), "epoch " + std::to_string(epoch) + ", state age " + std::to_string(machine_.measure().stateAgeAtTakeover) + " s");
}

void FailoverSystem::updatePrimary(double now) {
    failover::Inputs in;
    in.enabled = true;
    in.standby = false;
    machine_.update(now, in);
    net_.setFailoverReport(0, localHealth_, showHash_);
    if (servingPending_ && net_.running()) {
        servingPending_ = false;
        switchSeconds_ = now - confirmedAt_;
        log(now, "serving", "epoch " + std::to_string(net_.epoch()) + ", " + std::to_string(switchSeconds_) + " s after the decision");
    }
    const std::vector<netsync::TakeoverNotice> notices = net_.takeTakeovers();
    for (const netsync::TakeoverNotice& notice : notices) {
        if (notice.epoch > net_.epoch()) {
            executeStepDown(now, notice);
            return;
        }
    }
    if (!announceActive_) {
        return;
    }
    const netsync::Master* master = net_.master();
    if (master != nullptr) {
        std::uint32_t address = 0;
        if (UdpSocket::parseIpv4(announceHost_, address)) {
            for (const netsync::FollowerInfo& info : master->followers()) {
                if (info.live && info.peer.address == address) {
                    announceActive_ = false;
                    log(now, "old-primary-joined", announceHost_ + " now follows as standby");
                    return;
                }
            }
        }
    }
    if (now >= announceNext_) {
        net_.announceTakeover(announceHost_, announcePort_, announceEpoch_);
        ++announceCount_;
        announceNext_ = now + std::min(5.0, 0.5 + 0.5 * announceCount_);
    }
}

void FailoverSystem::executeStepDown(double now, const netsync::TakeoverNotice& notice) {
    const std::string host = formatIpv4(notice.peer.address);
    net_.stepDown(host, notice.port, notice.epoch);
    machine_.steppedDown(now);
    announceActive_ = false;
    servingPending_ = false;
    haveLevel_ = false;
    engine_.holdOutput = settings_.holdOutputs;
    log(now, "stepped-down", notice.name + " at " + host + " took over with epoch " + std::to_string(notice.epoch));
}

bool FailoverSystem::requestTakeover(double now) {
    if (!standby()) {
        return false;
    }
    return machine_.requestTakeover(now);
}

bool FailoverSystem::confirmTakeover(double now) {
    return standby() && machine_.confirmTakeover(now);
}

void FailoverSystem::cancelTakeover() {
    machine_.cancelRequest();
}

void FailoverSystem::rearm() {
    machine_.rearm();
}

StandbyView FailoverSystem::standbyView(double now) const {
    StandbyView view;
    const netsync::Master* master = net_.master();
    if (master == nullptr) {
        return view;
    }
    for (const netsync::FollowerInfo& info : master->followers()) {
        if (!info.live) {
            continue;
        }
        view.present = true;
        view.name = info.name;
        view.reports = info.reports;
        view.readiness = failover::levelFromWire(info.readiness);
        view.health = info.health;
        view.showKnown = info.reports && info.showHash != 0;
        view.showMatches = view.showKnown && info.showHash == showHash_;
        view.age = info.reports ? std::max(now - info.reportedAt, 0.0) : 0.0;
        break;
    }
    return view;
}

nlohmann::json FailoverSystem::json(double now) const {
    nlohmann::json j;
    j["enabled"] = settings_.enabled;
    j["autoTakeover"] = settings_.autoTakeover;
    j["holdOutputs"] = settings_.holdOutputs;
    j["lostAfter"] = machine_.thresholds().lostAfter;
    j["role"] = netsync::roleName(net_.role());
    j["state"] = failover::stateId(machine_.state());
    j["epoch"] = net_.epoch();
    j["outputHeld"] = engine_.holdOutput;
    j["localHealth"] = localHealth_;
    j["decoderFaults"] = decoderFaults_;
    j["faultNames"] = faultNames_;
    j["showHash"] = showHash_;
    j["missingFiles"] = missingFiles_;
    j["autoUsed"] = machine_.autoUsed();
    j["autoBlockedByEnd"] = machine_.autoBlockedByEnd();
    j["readySeconds"] = machine_.readySeconds(now);
    j["announcing"] = announceActive_;
    j["announceCount"] = announceCount_;
    j["switchSeconds"] = switchSeconds_;
    j["frameSeconds"] = frameSeconds_;
    nlohmann::json checks = nlohmann::json::array();
    for (const failover::Check& check : report_.checks) {
        nlohmann::json c;
        c["id"] = check.id;
        c["level"] = levelName(check.level);
        c["a"] = check.a;
        c["b"] = check.b;
        checks.push_back(c);
    }
    j["readiness"] = {{"overall", levelName(report_.overall)}, {"fails", report_.fails}, {"warns", report_.warns}, {"checks", checks}};
    const failover::Measure& m = machine_.measure();
    j["measure"] = {{"taken", m.taken}, {"automatic", m.automatic}, {"forced", m.forced}, {"detectionSeconds", m.detectionSeconds}, {"decisionSeconds", m.decisionSeconds}, {"stateAgeAtTakeover", m.stateAgeAtTakeover}, {"readinessBeforeLoss", levelName(m.readinessBeforeLoss)}, {"readySecondsBeforeLoss", m.readySecondsBeforeLoss}};
    const StandbyView view = standbyView(now);
    j["standby"] = {{"present", view.present}, {"reports", view.reports}, {"name", view.name}, {"readiness", levelName(view.readiness)}, {"health", view.health}, {"showMatches", view.showMatches}};
    nlohmann::json events = nlohmann::json::array();
    for (const FailoverEvent& e : events_) {
        events.push_back({{"time", e.time}, {"kind", e.kind}, {"detail", e.detail}});
    }
    j["events"] = events;
    return j;
}

}

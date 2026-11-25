#pragma once

#include "app/netsync_system.hpp"
#include "core/failover.hpp"
#include "render/engine.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

struct FailoverSettings {
    bool enabled = false;
    bool autoTakeover = false;
    double lostAfter = 5.0;
    bool holdOutputs = true;
    int leaderPort = 0;
    bool operator==(const FailoverSettings&) const = default;
};

struct FailoverEnv {
    int outputsWanted = 0;
    int outputsOpen = 0;
    double snapshotAge = -1.0;
};

struct FailoverEvent {
    double time = 0.0;
    std::string kind;
    std::string detail;
};

struct StandbyView {
    bool present = false;
    bool reports = false;
    std::string name;
    failover::Level readiness = failover::Level::Fail;
    std::uint8_t health = 0;
    bool showKnown = false;
    bool showMatches = false;
    double age = 0.0;
};

class FailoverSystem {
public:
    FailoverSystem(Engine& engine, NetSyncSystem& net);

    void configure(const FailoverSettings& settings);
    void update(double now, double dt, const FailoverEnv& env);
    void shutdown();

    bool enabled() const { return settings_.enabled; }
    bool standby() const;
    bool primary() const;
    failover::State state() const { return machine_.state(); }
    const failover::Report& report() const { return report_; }
    const failover::Machine& machine() const { return machine_; }
    const FailoverSettings& settings() const { return settings_; }
    bool requestTakeover(double now);
    bool confirmTakeover(double now);
    void cancelTakeover();
    void rearm();
    std::uint8_t localHealth() const { return localHealth_; }
    int decoderFaults() const { return decoderFaults_; }
    const std::vector<std::string>& faultNames() const { return faultNames_; }
    const std::vector<FailoverEvent>& events() const { return events_; }
    StandbyView standbyView(double now) const;
    bool announcing() const { return announceActive_; }
    const std::string& announceHost() const { return announceHost_; }
    std::uint64_t showHash() const { return showHash_; }
    double switchSeconds() const { return switchSeconds_; }
    nlohmann::json json(double now) const;

private:
    void refreshLocal(double now, const FailoverEnv& env);
    void updateStandby(double now, const FailoverEnv& env);
    void updatePrimary(double now);
    void executeTakeover(double now, const failover::Decision& decision);
    void executeStepDown(double now, const netsync::TakeoverNotice& notice);
    void log(double now, const std::string& kind, const std::string& detail);

    Engine& engine_;
    NetSyncSystem& net_;
    FailoverSettings settings_;
    failover::Machine machine_;
    failover::Report report_;
    failover::State lastState_ = failover::State::Idle;
    failover::Level lastLevel_ = failover::Level::Fail;
    bool haveLevel_ = false;
    std::vector<FailoverEvent> events_;
    std::uint8_t localHealth_ = 0;
    int decoderFaults_ = 0;
    std::vector<std::string> faultNames_;
    std::uint64_t showHash_ = 0;
    double nextHash_ = 0.0;
    int missingFiles_ = 0;
    double nextFiles_ = 0.0;
    int outputsWanted_ = 0;
    int outputsOpen_ = 0;
    bool announceActive_ = false;
    std::string announceHost_;
    int announcePort_ = 0;
    double announceNext_ = 0.0;
    int announceCount_ = 0;
    std::uint32_t announceEpoch_ = 0;
    bool servingPending_ = false;
    double switchSeconds_ = 0.0;
    double confirmedAt_ = 0.0;
    double frameSeconds_ = 0.0;
    bool leaderLogged_ = false;
};

}

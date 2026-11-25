#pragma once

#include "control/control_system.hpp"
#include "control/netsync_show.hpp"
#include "control/netsync_transport.hpp"
#include "control/sync_system.hpp"
#include "render/engine.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct NetSyncSettings {
    netsync::Role role = netsync::Role::Off;
    int port = netsync::kDefaultPort;
    bool allowRemote = false;
    std::string host = "127.0.0.1";
    std::string name;
    netsync::Follow follow;
    bool operator==(const NetSyncSettings&) const = default;
};

class EngineShowHost;

class NetSyncSystem {
public:
    NetSyncSystem(Engine& engine, ControlSystem& control, SyncSystem& sync);
    ~NetSyncSystem();
    NetSyncSystem(const NetSyncSystem&) = delete;
    NetSyncSystem& operator=(const NetSyncSystem&) = delete;

    void configure(const NetSyncSettings& settings);
    void update(double dt, double now);
    void afterEngineUpdate(double now, double dt);
    void noteCues(size_t count);
    void shutdown(double now);

    netsync::Role role() const { return transport_.role(); }
    bool running() const { return transport_.running(); }
    const std::string& error() const { return error_.empty() ? transport_.error() : error_; }
    const std::string& warning() const { return warning_; }
    int localPort() const { return transport_.localPort(); }
    const netsync::Transport& transport() const { return transport_; }
    const netsync::Follower* follower() const { return transport_.follower(); }
    const netsync::Master* master() const { return transport_.master(); }
    const std::vector<netsync::Mismatch>& mismatches() const { return show_.mismatches(); }
    const netsync::ApplyStats& applyStats() const { return show_.stats(); }
    const NetSyncSettings& settings() const { return wanted_; }

    bool suppressesAutomation() const;
    SyncMode effectiveSyncMode(SyncMode user) const;
    bool followingClock() const;

    std::string keyHex() const;
    std::string keyFingerprint() const;
    bool importKey(const std::string& text);
    bool regenerateKey();

    nlohmann::json report() const;

    void recordSamples(bool on) { recording_ = on; }
    const std::vector<std::array<double, 4>>& samples() const { return samples_; }

private:
    bool ensureKey();
    void updateMaster(double dt, double now);
    void updateFollower(double now);
    void clearFollowerState();

    Engine& engine_;
    ControlSystem& control_;
    SyncSystem& sync_;
    std::unique_ptr<EngineShowHost> host_;
    netsync::Transport transport_;
    netsync::ShowFollower show_;
    NetSyncSettings wanted_;
    netsync::Key key_{};
    bool haveKey_ = false;
    std::string error_;
    std::string warning_;
    netsync::MasterFrame frame_;
    double nextManifest_ = 0.0;
    double advanceStamp_ = 0.0;
    double lastAdvanceDt_ = 0.0;
    double lastBeat_ = 0.0;
    bool haveLastBeat_ = false;
    std::uint64_t jumps_ = 0;
    std::uint32_t cues_ = 0;
    unsigned generation_ = 0;
    netsync::Follow lastFollow_;
    bool followApplied_ = false;
    std::uint64_t manifestSeen_ = ~0ull;
    double nextEvaluate_ = 0.0;
    int lastRunning_ = -1;
    bool networkTimecodeActive_ = false;
    std::uint64_t pausesMirrored_ = 0;
    bool recording_ = false;
    std::vector<std::array<double, 4>> samples_;
};

}

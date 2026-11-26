#pragma once

#include "NyrexOsc.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NyrexCore {

constexpr double kConnectionTimeoutSeconds = 5.0;
constexpr double kPingTimeoutSeconds = 2.0;
constexpr std::int32_t kMaxPingId = 16777215;
constexpr std::size_t kLatencyWindow = 64;
constexpr std::size_t kMaxPendingPings = 16;

enum class LinkState : std::uint8_t { Stopped, Waiting, Connected, Lost, Ended, Incompatible };

struct LinkChange {
    bool connected = false;
    bool disconnected = false;
    bool incompatible = false;
};

struct LinkStats {
    LinkState state = LinkState::Stopped;
    std::uint64_t packets = 0;
    std::uint64_t hellos = 0;
    std::uint32_t connections = 0;
    std::uint32_t losses = 0;
    double secondsSinceLastPacket = -1.0;
    double secondsSinceLastHello = -1.0;
    double connectedSeconds = 0.0;
    double longestGapSeconds = 0.0;
    double packetsPerSecond = 0.0;
};

class LinkMonitor {
public:
    explicit LinkMonitor(double timeoutSeconds = kConnectionTimeoutSeconds);

    void setListening(bool listening, double now);
    void onPacket(double now);
    void onHello(double now, bool schemaSupported);
    void onBye(double now);
    LinkChange evaluate(double now);

    LinkState state() const { return state_; }
    bool connected() const { return state_ == LinkState::Connected; }
    LinkStats stats(double now) const;

private:
    void refreshRate(double now);

    double timeout_ = kConnectionTimeoutSeconds;
    bool listening_ = false;
    bool wire_ = false;
    bool everConnected_ = false;
    bool ended_ = false;
    bool incompatible_ = false;
    bool incompatibleReported_ = false;
    bool havePacket_ = false;
    bool haveHello_ = false;
    LinkState state_ = LinkState::Stopped;
    double lastPacket_ = 0.0;
    double lastHello_ = 0.0;
    double connectedAt_ = 0.0;
    double longestGap_ = 0.0;
    double windowStart_ = 0.0;
    std::uint32_t windowPackets_ = 0;
    double rate_ = 0.0;
    std::uint64_t packets_ = 0;
    std::uint64_t hellos_ = 0;
    std::uint32_t connections_ = 0;
    std::uint32_t losses_ = 0;
};

struct LatencyStats {
    bool valid = false;
    std::uint32_t sent = 0;
    std::uint32_t received = 0;
    std::uint32_t lost = 0;
    std::uint32_t ignored = 0;
    std::size_t samples = 0;
    double lastMs = 0.0;
    double averageMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double p95Ms = 0.0;
};

class LatencyTracker {
public:
    explicit LatencyTracker(double timeoutSeconds = kPingTimeoutSeconds);

    bool due(double now, double intervalSeconds) const;
    std::int32_t beginPing(double now);
    bool onPong(std::int32_t id, double now);
    void expire(double now);
    void reset();
    LatencyStats stats() const;

private:
    struct Pending {
        std::int32_t id = 0;
        double sentAt = 0.0;
    };

    double timeout_ = kPingTimeoutSeconds;
    std::int32_t nextId_ = 1;
    bool haveSent_ = false;
    double lastSent_ = 0.0;
    std::vector<Pending> pending_;
    std::vector<double> window_;
    std::size_t cursor_ = 0;
    std::uint32_t sent_ = 0;
    std::uint32_t received_ = 0;
    std::uint32_t lost_ = 0;
    std::uint32_t ignored_ = 0;
    double last_ = 0.0;
};

bool encodePing(std::int32_t id, std::vector<std::uint8_t>& out);
const char* linkStateName(LinkState state);

}

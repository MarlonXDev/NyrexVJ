#pragma once

#include "control/udp_socket.hpp"
#include "core/netsync.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nvj::netsync {

enum class Role { Off, Master, Follower };

const char* roleName(Role role);
Role roleFromName(const std::string& name);

struct TransportConfig {
    Role role = Role::Off;
    int port = kDefaultPort;
    bool allowRemote = false;
    std::string host = "127.0.0.1";
    std::string name;
    std::string appVersion;
    Key key{};
    std::uint64_t schema = 0;
    size_t stateSize = 0;
    int maxFollowers = kMaxFollowers;
    bool operator==(const TransportConfig&) const = default;
};

struct TransportStats {
    std::uint64_t datagramsIn = 0;
    std::uint64_t datagramsOut = 0;
    std::uint64_t bytesIn = 0;
    std::uint64_t bytesOut = 0;
    std::uint64_t sendFailures = 0;
    std::uint64_t foreign = 0;
};

class Transport {
public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    void configure(const TransportConfig& config, double now);
    void update(double now, const MasterFrame* frame);
    void shutdown(double now);

    Role role() const { return active_.role; }
    bool running() const { return running_; }
    int localPort() const { return socket_.localPort(); }
    const std::string& error() const { return error_; }
    const TransportConfig& config() const { return active_; }
    Master* master() { return master_.get(); }
    const Master* master() const { return master_.get(); }
    Follower* follower() { return follower_.get(); }
    const Follower* follower() const { return follower_.get(); }
    const TransportStats& stats() const { return stats_; }
    unsigned generation() const { return generation_; }

private:
    void start(double now);
    void stop(double now);
    void pump(double now);
    void flush();

    TransportConfig wanted_;
    TransportConfig active_;
    bool configured_ = false;
    bool running_ = false;
    bool failed_ = false;
    unsigned generation_ = 0;
    double nextRetry_ = 0.0;
    std::string error_;
    UdpSocket socket_;
    Peer masterPeer_;
    std::unique_ptr<Master> master_;
    std::unique_ptr<Follower> follower_;
    TransportStats stats_;
};

}

#pragma once

#include "control/api_router.hpp"
#include "control/tcp_socket.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace nvj::api {

struct ServerConfig {
    int port = 9020;
    bool allowRemote = false;
    size_t maxConnections = 32;
    size_t maxPerAddress = 8;
    double headerSeconds = 5.0;
    double requestSeconds = 10.0;
    double commandSeconds = 1.0;
    double drainSeconds = 1.0;
    int idlePollMs = 50;
};

struct ServerStats {
    std::uint64_t accepted = 0;
    std::uint64_t refused = 0;
    std::uint64_t timeouts = 0;
    int connections = 0;
};

class Server {
public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(const ServerConfig& config, Router& router, std::string& error);
    void stop();
    bool running() const { return running_.load(); }
    int port() const { return port_.load(); }
    ServerStats stats() const;

private:
    struct Connection;
    void run(ServerConfig config, Router* router);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> port_{0};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> refused_{0};
    std::atomic<std::uint64_t> timeouts_{0};
    std::atomic<int> connections_{0};
    TcpListener listener_;
};

}

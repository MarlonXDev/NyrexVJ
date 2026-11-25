#include "control/api_server.hpp"

#include "platform/platform.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace nvj::api {

namespace {

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

enum class Phase { Reading, Waiting, Writing, Draining };

struct Server::Connection {
    std::unique_ptr<TcpConnection> tcp;
    HttpRequestParser parser;
    Phase phase = Phase::Reading;
    double accepted = 0.0;
    double drainStarted = 0.0;
    double ticketStarted = 0.0;
    std::shared_ptr<Ticket> ticket;
    std::function<void(const Result&, HttpResponse&)> finish;
    std::string out;
    size_t sent = 0;
    bool dead = false;
};

Server::Server() = default;

Server::~Server() {
    stop();
}

bool Server::start(const ServerConfig& config, Router& router, std::string& error) {
    stop();
    if (!listener_.open(config.port, config.allowRemote, error)) {
        return false;
    }
    port_ = listener_.port();
    router.setPort(port_.load());
    accepted_ = 0;
    refused_ = 0;
    timeouts_ = 0;
    connections_ = 0;
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread([this, config, &router] { run(config, &router); });
    return true;
}

void Server::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    listener_.close();
    running_ = false;
    port_ = 0;
    connections_ = 0;
}

ServerStats Server::stats() const {
    ServerStats out;
    out.accepted = accepted_.load();
    out.refused = refused_.load();
    out.timeouts = timeouts_.load();
    out.connections = connections_.load();
    return out;
}

void Server::run(ServerConfig config, Router* router) {
    platform::setCurrentThreadBackground(true);
    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<TcpPollEntry> entries;
    std::unordered_map<std::uint32_t, size_t> perAddress;

    auto begin = [&](Connection& connection, HttpResponse response) {
        connection.out = serializeHttpResponse(response);
        connection.sent = 0;
        connection.phase = Phase::Writing;
    };

    auto handleRequest = [&](Connection& connection, double now) {
        Reply reply = router->handle(connection.parser.request(), connection.tcp->peer().address, now);
        if (reply.ticket) {
            connection.ticket = std::move(reply.ticket);
            connection.finish = std::move(reply.finish);
            connection.ticketStarted = now;
            connection.phase = Phase::Waiting;
            return;
        }
        begin(connection, std::move(reply.response));
    };

    while (!stopRequested_.load()) {
        entries.clear();
        TcpPollEntry listening;
        listening.listener = &listener_;
        entries.push_back(listening);
        bool waiting = false;
        for (const auto& connection : connections) {
            TcpPollEntry entry;
            entry.connection = connection->tcp.get();
            entry.wantWrite = connection->phase == Phase::Writing;
            entries.push_back(entry);
            waiting = waiting || connection->phase == Phase::Waiting;
        }
        const size_t polled = connections.size();
        tcpPoll(entries, waiting ? 2 : config.idlePollMs);
        if (stopRequested_.load()) {
            break;
        }
        const double now = steadySeconds();

        if (entries[0].readable) {
            for (int i = 0; i < 16; ++i) {
                std::unique_ptr<TcpConnection> accepted = listener_.accept();
                if (!accepted) {
                    break;
                }
                const std::uint32_t address = accepted->peer().address;
                const auto known = perAddress.find(address);
                if (connections.size() >= config.maxConnections || (known != perAddress.end() && known->second >= config.maxPerAddress)) {
                    ++refused_;
                    continue;
                }
                ++accepted_;
                ++perAddress[address];
                auto connection = std::make_unique<Connection>();
                connection->tcp = std::move(accepted);
                connection->accepted = now;
                connections.push_back(std::move(connection));
            }
        }

        for (size_t i = 0; i < polled; ++i) {
            Connection& connection = *connections[i];
            const TcpPollEntry& entry = entries[i + 1];
            if (entry.broken && connection.phase != Phase::Writing) {
                connection.dead = true;
                continue;
            }
            switch (connection.phase) {
            case Phase::Reading: {
                if (entry.readable) {
                    unsigned char buffer[4096];
                    for (int round = 0; round < 4 && connection.phase == Phase::Reading && !connection.dead; ++round) {
                        const int got = connection.tcp->receive(buffer, sizeof(buffer));
                        if (got < 0) {
                            connection.dead = true;
                            break;
                        }
                        if (got == 0) {
                            break;
                        }
                        const HttpParseState state = connection.parser.feed(reinterpret_cast<const char*>(buffer), static_cast<size_t>(got));
                        if (state == HttpParseState::Failed) {
                            begin(connection, router->parseError(connection.parser.errorStatus(), connection.parser.errorText()));
                        } else if (state == HttpParseState::Complete) {
                            handleRequest(connection, now);
                        }
                    }
                }
                if (connection.phase == Phase::Reading && !connection.dead) {
                    const double age = now - connection.accepted;
                    const double limit = connection.parser.headersComplete() ? config.requestSeconds : config.headerSeconds;
                    if (age > limit) {
                        ++timeouts_;
                        HttpResponse response = errorResponse(408, "timeout", "The request took too long");
                        begin(connection, std::move(response));
                    }
                }
                break;
            }
            case Phase::Waiting: {
                Result result;
                if (connection.ticket->poll(result)) {
                    HttpResponse response;
                    connection.finish(result, response);
                    begin(connection, std::move(response));
                } else if (now - connection.ticketStarted > config.commandSeconds) {
                    if (connection.ticket->cancel()) {
                        begin(connection, router->timeoutResponse());
                    } else if (now - connection.ticketStarted > config.commandSeconds + 2.0) {
                        begin(connection, router->timeoutResponse());
                    }
                }
                break;
            }
            case Phase::Writing: {
                while (connection.sent < connection.out.size()) {
                    const int chunk = static_cast<int>(std::min<size_t>(connection.out.size() - connection.sent, 16384));
                    const int wrote = connection.tcp->send(connection.out.data() + connection.sent, chunk);
                    if (wrote < 0) {
                        connection.dead = true;
                        break;
                    }
                    if (wrote == 0) {
                        break;
                    }
                    connection.sent += static_cast<size_t>(wrote);
                }
                if (!connection.dead && connection.sent >= connection.out.size()) {
                    connection.tcp->shutdownWrite();
                    connection.phase = Phase::Draining;
                    connection.drainStarted = now;
                } else if (!connection.dead && now - connection.accepted > config.requestSeconds + 5.0) {
                    ++timeouts_;
                    connection.dead = true;
                }
                break;
            }
            case Phase::Draining: {
                unsigned char buffer[1024];
                for (int round = 0; round < 8; ++round) {
                    const int got = connection.tcp->receive(buffer, sizeof(buffer));
                    if (got < 0) {
                        connection.dead = true;
                        break;
                    }
                    if (got == 0) {
                        break;
                    }
                }
                if (now - connection.drainStarted > config.drainSeconds) {
                    connection.dead = true;
                }
                break;
            }
            }
        }

        for (size_t i = 0; i < connections.size();) {
            if (connections[i]->dead) {
                const std::uint32_t address = connections[i]->tcp->peer().address;
                auto it = perAddress.find(address);
                if (it != perAddress.end() && --it->second == 0) {
                    perAddress.erase(it);
                }
                connections.erase(connections.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        connections_ = static_cast<int>(connections.size());
    }

    for (auto& connection : connections) {
        if (connection->ticket) {
            connection->ticket->cancel();
        }
    }
    connections.clear();
    connections_ = 0;
    running_ = false;
}

}

#pragma once

#include "control/api_hub.hpp"
#include "control/api_security.hpp"
#include "core/http_message.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nvj::api {

struct RouterConfig {
    int port = 9020;
    bool allowRemote = false;
    std::string token;
    RateLimitConfig limits;
};

struct PresetAccess {
    std::function<std::vector<std::string>()> listUser;
    std::function<bool(const std::string& name, std::string& text, std::string& error)> readUser;
};

PresetAccess defaultPresetAccess();

struct Reply {
    HttpResponse response;
    std::shared_ptr<Ticket> ticket;
    std::function<void(const Result&, HttpResponse&)> finish;
};

struct Stats {
    std::atomic<std::uint64_t> requests{0};
    std::atomic<std::uint64_t> authFailures{0};
    std::atomic<std::uint64_t> limited{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> badRequests{0};
    std::atomic<std::uint64_t> commands{0};
    std::atomic<std::uint64_t> busy{0};
};

class Router {
public:
    Router(Hub& hub, const RouterConfig& config, PresetAccess presets = defaultPresetAccess());

    Reply handle(const HttpRequest& request, std::uint32_t peerAddress, double now);
    HttpResponse parseError(int status, const std::string& message);
    HttpResponse timeoutResponse();
    Stats& stats() { return stats_; }
    void setPort(int port) { config_.port = port; }

private:
    Reply dispatch(const HttpRequest& request);
    Reply route(const std::string& method, const std::vector<std::string>& segments, const HttpRequest& request);
    Reply controlRoute(const std::string& method, const std::vector<std::string>& rest, const HttpRequest& request);
    Reply layerRoute(const std::string& method, const std::vector<std::string>& rest, const HttpRequest& request);
    Reply deckRoute(const std::string& method, const std::vector<std::string>& rest);
    Reply presetRoute(const std::string& method, const std::vector<std::string>& rest, const HttpRequest& request);
    Reply controlCommand(const std::string& id, float value);
    Reply submit(const Command& command, std::function<void(const Result&, HttpResponse&)> finish);

    Hub& hub_;
    RouterConfig config_;
    PresetAccess presets_;
    RateLimiter limiter_;
    Stats stats_;
};

HttpResponse jsonResponse(int status, const std::string& body);
HttpResponse errorResponse(int status, const std::string& code, const std::string& message);
void applySecurityHeaders(HttpResponse& response);

}

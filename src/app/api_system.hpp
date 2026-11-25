#pragma once

#include "control/api_hub.hpp"
#include "control/api_router.hpp"
#include "control/api_server.hpp"
#include "control/control_system.hpp"
#include "core/composition.hpp"
#include "render/engine.hpp"
#include "render/output_manager.hpp"

#include <functional>
#include <memory>
#include <string>

namespace nvj {

struct ApiConfig {
    bool enabled = false;
    int port = 9020;
    bool allowRemote = false;
    bool operator==(const ApiConfig&) const = default;
};

class ApiSystem {
public:
    using LoadFn = std::function<void(const Composition&, const std::string&)>;

    ApiSystem(Engine& engine, ControlSystem& control, OutputManager& outputs, LoadFn load);
    ~ApiSystem();
    ApiSystem(const ApiSystem&) = delete;
    ApiSystem& operator=(const ApiSystem&) = delete;

    void configure(const ApiConfig& config);
    void update(double dt, float fps);
    void shutdown();
    void restart();
    bool regenerateToken();

    bool running() const { return server_.running(); }
    int port() const { return server_.port(); }
    bool allowRemote() const { return active_.allowRemote; }
    const std::string& error() const { return error_; }
    const std::string& warning() const { return warning_; }
    const std::string& token() const { return token_; }
    std::string browserUrl() const;
    api::ServerStats serverStats() const { return server_.stats(); }
    const api::Stats* routerStats() const { return router_ ? &router_->stats() : nullptr; }
    size_t queued() const { return hub_.queued(); }
    std::uint64_t applied() const { return applied_; }

    std::shared_ptr<const api::Snapshot> buildSnapshot(float fps);
    api::Result execute(const api::Command& command);
    api::Hub& hub() { return hub_; }

private:
    bool ensureToken();
    void stopServer();

    Engine& engine_;
    ControlSystem& control_;
    OutputManager& outputs_;
    LoadFn load_;
    api::Hub hub_;
    std::unique_ptr<api::Router> router_;
    api::Server server_;
    ApiConfig wanted_;
    ApiConfig active_;
    bool attempted_ = false;
    bool restartRequested_ = false;
    std::string token_;
    std::string error_;
    std::string warning_;
    std::shared_ptr<const api::Library> library_;
    std::uint64_t revision_ = 0;
    std::uint64_t applied_ = 0;
    double publishTimer_ = 0.0;
};

}

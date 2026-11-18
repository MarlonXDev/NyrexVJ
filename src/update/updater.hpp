#pragma once

#include "platform/platform.hpp"
#include "update/update_logic.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nvj {

enum class UpdateState {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    ReadyToRestart,
    Failed,
    Unsupported
};

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    std::string message;
    ReleaseInfo release;
    float progress = 0.0f;
};

struct UpdaterBackend {
    std::function<bool(const std::string&, platform::HttpResult&, const platform::ProgressFn&)> get;
    std::function<bool(const std::vector<unsigned char>&, std::string&)> replace;
    bool supported = true;
};

UpdaterBackend platformBackend();

class Updater {
public:
    Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    ~Updater();

    void setBackend(UpdaterBackend backend);
    void setChannel(UpdateChannel channel);
    UpdateChannel channel() const;
    void checkAsync();
    void installAsync();
    UpdateStatus status() const;
    bool busy() const { return busy_.load(); }

private:
    void joinWorker();
    void set(UpdateState state, const std::string& message);
    void runCheck(UpdateChannel channel, UpdaterBackend backend);
    void runInstall(ReleaseInfo release, UpdaterBackend backend);

    mutable std::mutex mutex_;
    UpdateStatus status_;
    UpdateChannel channel_ = UpdateChannel::Nightly;
    UpdaterBackend backend_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
};

}

#pragma once

#include "core/live_source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct NdiSourceInfo {
    std::string name;
    std::string url;
};

bool pickNdiSource(const std::vector<NdiSourceInfo>& list, const std::string& wanted, const std::string& excluded, NdiSourceInfo& out);

class NdiFinder {
public:
    static NdiFinder& instance();

    bool acquire();
    void release();
    std::vector<NdiSourceInfo> sources() const;
    bool running() const;
    std::string error() const;

private:
    NdiFinder();
    ~NdiFinder();
    NdiFinder(const NdiFinder&) = delete;
    NdiFinder& operator=(const NdiFinder&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

using NdiReceiverStatus = LiveStatus;

class NdiReceiver : public LiveSource {
public:
    NdiReceiver();
    ~NdiReceiver() override;
    NdiReceiver(const NdiReceiver&) = delete;
    NdiReceiver& operator=(const NdiReceiver&) = delete;

    bool start(const std::string& wanted, const std::string& excluded, const ReconnectPolicy& policy = ReconnectPolicy());
    void stop() override;
    bool running() const override;
    bool take(LiveFrame& out) override;
    LiveStatus status() const override;
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

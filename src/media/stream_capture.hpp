#pragma once

#include "core/live_source.hpp"
#include "core/stream_url.hpp"

#include <memory>
#include <string>

namespace nvj {

struct StreamSupport {
    bool compiled = false;
    bool ffmpeg = false;
    std::string problem;
};

StreamSupport streamSupport();

class StreamCapture : public LiveSource {
public:
    StreamCapture();
    ~StreamCapture() override;
    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;

    bool start(const std::string& url, stream::Profile profile, const ReconnectPolicy& policy = ReconnectPolicy());
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

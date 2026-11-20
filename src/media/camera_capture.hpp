#pragma once

#include "core/live_source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct CameraDevice {
    std::string id;
    std::string name;
};

struct CameraMode {
    int width = 1280;
    int height = 720;
    int fps = 30;
};

struct CameraSupport {
    bool compiled = false;
    bool ffmpeg = false;
    bool devices = false;
    std::string problem;
};

CameraSupport cameraSupport();
std::vector<CameraDevice> listCameras();
bool isTestPatternDevice(const std::string& id);

class CameraCapture : public LiveSource {
public:
    CameraCapture();
    ~CameraCapture() override;
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    bool start(const std::string& device, const CameraMode& mode, const ReconnectPolicy& policy = ReconnectPolicy());
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

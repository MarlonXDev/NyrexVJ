#include "check.hpp"

#include "media/camera_capture.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

bool waitFor(const std::function<bool()>& condition, double seconds) {
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000.0));
    while (Clock::now() < end) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return condition();
}

nvj::ReconnectPolicy fastPolicy() {
    nvj::ReconnectPolicy policy;
    policy.firstDelay = 0.05;
    policy.growth = 1.5;
    policy.maxDelay = 0.2;
    policy.lostAfter = 0.5;
    policy.reconnectAfter = 1.0;
    policy.connectTimeout = 3.0;
    return policy;
}

bool near(int value, int expected, int tolerance) {
    return std::abs(value - expected) <= tolerance;
}

bool takeFrame(nvj::CameraCapture& camera, nvj::LiveFrame& frame, double seconds) {
    return waitFor([&]() { return camera.take(frame); }, seconds);
}

void testColor(const std::string& graph, int width, int height, int y, int u, int v, int tolerance) {
    nvj::CameraCapture camera;
    CHECK(camera.start("lavfi:" + graph, nvj::CameraMode{}, fastPolicy()));
    nvj::LiveFrame frame;
    CHECK(takeFrame(camera, frame, 10.0));
    CHECK(frame.format == nvj::LiveFormat::Uyvy);
    CHECK(frame.width == width && frame.height == height);
    CHECK(frame.stride == width * 2);
    CHECK(frame.data.size() == static_cast<size_t>(width) * 2u * static_cast<size_t>(height));
    if (frame.data.size() >= 4) {
        const size_t at = (static_cast<size_t>(height / 2) * static_cast<size_t>(width) + static_cast<size_t>(width / 2 & ~1)) * 2u;
        const int pu = frame.data[at];
        const int py0 = frame.data[at + 1];
        const int pv = frame.data[at + 2];
        const int py1 = frame.data[at + 3];
        if (!(near(pu, u, tolerance) && near(py0, y, tolerance) && near(pv, v, tolerance) && near(py1, y, tolerance))) {
            std::fprintf(stderr, "%s: got U=%d Y0=%d V=%d Y1=%d expected U=%d Y=%d V=%d\n", graph.c_str(), pu, py0, pv, py1, u, y, v);
        }
        CHECK(near(pu, u, tolerance));
        CHECK(near(py0, y, tolerance));
        CHECK(near(pv, v, tolerance));
        CHECK(near(py1, y, tolerance));
    }
    camera.stop();
}

void testPattern() {
    nvj::CameraCapture camera;
    nvj::LiveFrame frame;
    CHECK(!camera.running());
    CHECK(!camera.take(frame));
    CHECK(camera.status().state == nvj::LiveState::Idle);
    CHECK(camera.start("lavfi:testsrc2=size=320x180:rate=25", nvj::CameraMode{}, fastPolicy()));
    CHECK(camera.running());
    CHECK(waitFor([&]() { return camera.status().state == nvj::LiveState::Live; }, 10.0));
    CHECK(takeFrame(camera, frame, 5.0));
    CHECK(frame.format == nvj::LiveFormat::Uyvy);
    CHECK(frame.width == 320 && frame.height == 180);
    CHECK(frame.stride == 640);
    CHECK(frame.data.size() == 640u * 180u);
    CHECK(frame.capturedAt > 0.0);
    CHECK(nvj::steadySeconds() - frame.capturedAt < 2.0);
    bool varies = false;
    for (size_t i = 2; i < frame.data.size(); i += 2) {
        varies = varies || frame.data[i] != frame.data[0];
    }
    CHECK(varies);
    CHECK(waitFor([&]() { return camera.status().fps > 12.0 && camera.status().fps < 40.0; }, 8.0));
    const nvj::LiveStatus status = camera.status();
    CHECK(status.width == 320 && status.height == 180);
    CHECK(status.frames >= 2);
    CHECK(status.attempts == 0);
    CHECK(status.error.empty());
    CHECK(status.source == "lavfi:testsrc2=size=320x180:rate=25");

    const auto begin = Clock::now();
    camera.stop();
    CHECK(std::chrono::duration<double>(Clock::now() - begin).count() < 2.0);
    CHECK(!camera.running());
    CHECK(camera.status().state == nvj::LiveState::Idle);
    camera.stop();

    CHECK(camera.start("lavfi:testsrc2=size=160x90:rate=25", nvj::CameraMode{}, fastPolicy()));
    CHECK(waitFor([&]() { return camera.status().state == nvj::LiveState::Live; }, 10.0));
    CHECK(waitFor([&]() {
        nvj::LiveFrame next;
        if (!camera.take(next)) {
            return false;
        }
        frame = next;
        return frame.width == 160;
    }, 5.0));
    CHECK(frame.height == 90);
}

void testMissingDevice() {
    nvj::CameraCapture camera;
    CHECK(!camera.start("", nvj::CameraMode{}, fastPolicy()));
    CHECK(!camera.error().empty());
    CHECK(camera.start("/dev/video-does-not-exist", nvj::CameraMode{}, fastPolicy()));
    CHECK(waitFor([&]() { return camera.status().attempts >= 3; }, 8.0));
    CHECK(camera.status().state == nvj::LiveState::Searching);
    CHECK(!camera.status().error.empty());
    nvj::LiveFrame frame;
    CHECK(!camera.take(frame));
    const auto begin = Clock::now();
    camera.stop();
    CHECK(std::chrono::duration<double>(Clock::now() - begin).count() < 2.0);
}

void testListing() {
    const std::vector<nvj::CameraDevice> list = nvj::listCameras();
    bool pattern = false;
    for (const nvj::CameraDevice& device : list) {
        CHECK(!device.id.empty());
        CHECK(!device.name.empty());
        pattern = pattern || nvj::isTestPatternDevice(device.id);
    }
    CHECK(pattern);
    CHECK(nvj::isTestPatternDevice("lavfi:color=c=red"));
    CHECK(!nvj::isTestPatternDevice("/dev/video0"));
    CHECK(!nvj::isTestPatternDevice(""));
}

}

int main() {
    const nvj::CameraSupport support = nvj::cameraSupport();
    if (!support.compiled || !support.ffmpeg || !support.devices) {
        std::printf("camera tests skipped: %s\n", support.problem.c_str());
        nvj::CameraCapture camera;
        nvj::LiveFrame frame;
        CHECK(!camera.start("/dev/video0", nvj::CameraMode{}, nvj::ReconnectPolicy()));
        CHECK(!camera.error().empty());
        CHECK(!camera.running());
        CHECK(!camera.take(frame));
        CHECK(nvj::listCameras().empty() || !support.compiled);
        return testing::finish();
    }
    testListing();
    testPattern();
    testMissingDevice();
    testColor("color=c=red:size=64x64:rate=25,format=rgb24", 64, 64, 82, 90, 240, 4);
    testColor("color=c=red:size=1280x720:rate=25,format=rgb24", 1280, 720, 63, 102, 240, 4);
    testColor("color=c=lime:size=64x64:rate=25,format=rgb24", 64, 64, 145, 54, 34, 4);
    testColor("color=c=lime:size=1280x720:rate=25,format=rgb24", 1280, 720, 173, 42, 26, 4);
    testColor("color=c=white:size=64x64:rate=25,format=rgb24", 64, 64, 235, 128, 128, 3);
    testColor("color=c=black:size=64x64:rate=25,format=rgb24", 64, 64, 16, 128, 128, 3);
    testColor("color=c=red:size=64x64:rate=25,format=yuvj444p", 64, 64, 82, 90, 240, 6);
    testColor("color=c=black:size=64x64:rate=25", 64, 64, 16, 128, 128, 3);
    return testing::finish();
}

#include "check.hpp"

#include "core/live_source.hpp"
#include "core/ndi_receiver.hpp"
#include "core/ndi_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

extern "C" {
void fake_ndi_set_sources(const char* names);
void fake_ndi_set_stream(int on, int width, int height, uint32_t fourCC, int fps, int padding);
}

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
    policy.lostAfter = 0.3;
    policy.reconnectAfter = 1.0;
    policy.connectTimeout = 0.6;
    return policy;
}

void testRuntimeWithOnlyCaptureV3() {
    nvj::ndi::setLibraryOverride(NYREXVJ_FAKE_NDI_V3_PATH);
    nvj::ndi::recheck();
    CHECK(nvj::ndi::available());
    const nvj::ndi::RuntimeInfo info = nvj::ndi::runtimeInfo();
    CHECK(info.library == NYREXVJ_FAKE_NDI_V3_PATH);
    CHECK(info.version.empty());
    CHECK(info.captureV3);
    nvj::ndi::Api api;
    std::string error;
    CHECK(nvj::ndi::loadForReceiving(api, error));
    CHECK(api.recvCapture != nullptr);
}

void testReceiveThroughCaptureV3() {
    fake_ndi_set_sources("Cam (V3)");
    fake_ndi_set_stream(1, 64, 36, nvj::ndi::fourCC('U', 'Y', 'V', 'Y'), 120, 16);
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("Cam (V3)", "NyrexVJ", fastPolicy()));
    nvj::LiveFrame frame;
    CHECK(waitFor([&]() { return receiver.take(frame); }, 5.0));
    CHECK(frame.width == 64 && frame.height == 36);
    CHECK(frame.format == nvj::LiveFormat::Uyvy);
    CHECK(receiver.status().state == nvj::LiveState::Live);
    receiver.stop();
    fake_ndi_set_stream(0, 0, 0, 0, 30, 0);
}

}

int main() {
    testRuntimeWithOnlyCaptureV3();
    testReceiveThroughCaptureV3();
    return testing::finish();
}

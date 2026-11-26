#include "check.hpp"

#include "core/live_source.hpp"
#include "core/ndi_receiver.hpp"
#include "core/ndi_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void fake_ndi_set_sources(const char* names);
void fake_ndi_set_stream(int on, int width, int height, uint32_t fourCC, int fps, int padding);
void fake_ndi_push_burst(int count);
void fake_ndi_set_counter(int value);
void fake_ndi_inject_error();
void fake_ndi_set_refuse_create(int value);
void fake_ndi_set_recv_connections(int value);
int fake_ndi_finder_created();
int fake_ndi_finder_destroyed();
int fake_ndi_recv_created();
int fake_ndi_recv_destroyed();
int fake_ndi_recv_alive();
int fake_ndi_captured();
int fake_ndi_freed();
int fake_ndi_bad_frees();
int fake_ndi_outstanding();
int fake_ndi_counter();
int fake_ndi_last_color_format();
int fake_ndi_last_bandwidth();
int fake_ndi_last_allow_fields();
int fake_ndi_last_recv_name(char* out, int size);
int fake_ndi_last_receiver_name(char* out, int size);
}

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t fourCC(char a, char b, char c, char d) {
    return nvj::ndi::fourCC(a, b, c, d);
}

const uint32_t kUyvy = fourCC('U', 'Y', 'V', 'Y');
const uint32_t kUyva = fourCC('U', 'Y', 'V', 'A');
const uint32_t kRgba = fourCC('R', 'G', 'B', 'A');
const uint32_t kBgra = fourCC('B', 'G', 'R', 'A');

uint8_t pattern(int counter, int y, int index) {
    return static_cast<uint8_t>(counter * 7 + y * 3 + index * 5);
}

bool waitFor(const std::function<bool()>& condition, double seconds) {
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000.0));
    while (Clock::now() < end) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return condition();
}

void pause(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

std::string recvName() {
    char buffer[256] = {};
    fake_ndi_last_recv_name(buffer, static_cast<int>(sizeof(buffer)));
    return buffer;
}

std::string receiverName() {
    char buffer[256] = {};
    fake_ndi_last_receiver_name(buffer, static_cast<int>(sizeof(buffer)));
    return buffer;
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

bool isLive(const nvj::NdiReceiver& receiver) {
    return receiver.status().state == nvj::LiveState::Live;
}

bool frameMatches(const nvj::LiveFrame& frame, int counter, size_t rowBytes) {
    if (frame.data.size() != rowBytes * static_cast<size_t>(frame.height)) {
        return false;
    }
    for (int y = 0; y < frame.height; ++y) {
        for (size_t i = 0; i < rowBytes; ++i) {
            if (frame.data[static_cast<size_t>(y) * rowBytes + i] != pattern(counter, y, static_cast<int>(i))) {
                return false;
            }
        }
    }
    return true;
}

int counterFromFirstByte(uint8_t first) {
    return (static_cast<int>(first) * 183) & 255;
}

void resetFake() {
    fake_ndi_set_stream(0, 0, 0, 0, 30, 0);
    fake_ndi_set_sources("");
    fake_ndi_set_refuse_create(0);
    fake_ndi_set_counter(0);
}

void testPick() {
    std::vector<nvj::NdiSourceInfo> list = {{"PC (NyrexVJ)", "u1"}, {"PC (OBS)", "u2"}, {"Laptop (Cam)", "u3"}};
    nvj::NdiSourceInfo out;
    CHECK(nvj::pickNdiSource(list, "Laptop (Cam)", "NyrexVJ", out));
    CHECK(out.name == "Laptop (Cam)" && out.url == "u3");
    CHECK(!nvj::pickNdiSource(list, "Missing (Cam)", "NyrexVJ", out));
    CHECK(!nvj::pickNdiSource(list, "pc (obs)", "NyrexVJ", out));
    CHECK(nvj::pickNdiSource(list, "PC (NyrexVJ)", "NyrexVJ", out));
    CHECK(nvj::pickNdiSource(list, "", "NyrexVJ", out));
    CHECK(out.name == "PC (OBS)" && out.url == "u2");
    CHECK(nvj::pickNdiSource(list, "", "", out));
    CHECK(out.name == "PC (NyrexVJ)");
    std::vector<nvj::NdiSourceInfo> own = {{"PC (NyrexVJ)", "u1"}};
    CHECK(!nvj::pickNdiSource(own, "", "NyrexVJ", out));
    CHECK(!nvj::pickNdiSource({}, "", "NyrexVJ", out));
    CHECK(!nvj::pickNdiSource({}, "Any", "", out));
    std::vector<nvj::NdiSourceInfo> unnamed = {{"", "u"}, {"Real (A)", "v"}};
    CHECK(nvj::pickNdiSource(unnamed, "", "", out));
    CHECK(out.name == "Real (A)");
}

void testMissingLibrary() {
    nvj::ndi::setLibraryOverride("/nonexistent/path/libndi_missing.so");
    nvj::ndi::recheck();
    nvj::NdiReceiver receiver;
    CHECK(!receiver.start("Any", "NyrexVJ"));
    CHECK(!receiver.running());
    CHECK(!receiver.error().empty());
    CHECK(receiver.status().state == nvj::LiveState::Idle);
    nvj::LiveFrame frame;
    CHECK(!receiver.take(frame));
    CHECK(!nvj::NdiFinder::instance().acquire());
    CHECK(!nvj::NdiFinder::instance().error().empty());
    CHECK(!nvj::NdiFinder::instance().running());
    CHECK(nvj::NdiFinder::instance().sources().empty());
}

void loadFake() {
    nvj::ndi::setLibraryOverride(NYREXVJ_FAKE_NDI_PATH);
    nvj::ndi::recheck();
}

void testDiscovery() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("B (Cam)\nA (Cam)\nPC (NyrexVJ)");
    const int created = fake_ndi_finder_created();
    const int destroyed = fake_ndi_finder_destroyed();
    nvj::NdiFinder& finder = nvj::NdiFinder::instance();
    CHECK(finder.acquire());
    CHECK(waitFor([&]() { return finder.sources().size() == 3; }, 5.0));
    const std::vector<nvj::NdiSourceInfo> list = finder.sources();
    CHECK(list.size() == 3);
    if (list.size() == 3) {
        CHECK(list[0].name == "A (Cam)");
        CHECK(list[1].name == "B (Cam)");
        CHECK(list[2].name == "PC (NyrexVJ)");
        CHECK(list[0].url == "ndi://A (Cam)");
    }
    CHECK(finder.running());
    CHECK(fake_ndi_finder_created() == created + 1);

    CHECK(finder.acquire());
    CHECK(fake_ndi_finder_created() == created + 1);
    fake_ndi_set_sources("A (Cam)");
    CHECK(waitFor([&]() { return finder.sources().size() == 1; }, 5.0));
    fake_ndi_set_sources("");
    CHECK(waitFor([&]() { return finder.sources().empty(); }, 5.0));
    fake_ndi_set_sources("Late (Cam)");
    CHECK(waitFor([&]() { return finder.sources().size() == 1 && finder.sources()[0].name == "Late (Cam)"; }, 5.0));

    finder.release();
    CHECK(finder.running());
    CHECK(fake_ndi_finder_destroyed() == destroyed);
    finder.release();
    CHECK(!finder.running());
    CHECK(fake_ndi_finder_destroyed() == destroyed + 1);
    CHECK(finder.sources().empty());
    finder.release();
    CHECK(fake_ndi_finder_destroyed() == destroyed + 1);
}

void testReceiveByName() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("B (Cam)\nA (Cam)\nPC (NyrexVJ)");
    fake_ndi_set_stream(1, 64, 36, kUyvy, 120, 16);
    const int created = fake_ndi_recv_created();
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("A (Cam)", "NyrexVJ", fastPolicy()));
    CHECK(receiver.running());
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(fake_ndi_recv_created() == created + 1);
    CHECK(recvName() == "A (Cam)");
    CHECK(receiverName() == "NyrexVJ");
    CHECK(fake_ndi_last_color_format() == 3);
    CHECK(fake_ndi_last_bandwidth() == 100);
    CHECK(fake_ndi_last_allow_fields() == 0);
    fake_ndi_set_recv_connections(2);

    nvj::LiveFrame frame;
    CHECK(waitFor([&]() { return receiver.take(frame); }, 5.0));
    CHECK(frame.format == nvj::LiveFormat::Uyvy);
    CHECK(frame.width == 64 && frame.height == 36);
    CHECK(frame.stride == 128);
    CHECK(frame.data.size() == 128u * 36u);
    CHECK(frame.id >= 1);
    CHECK(frame.capturedAt > 0.0);
    CHECK(frameMatches(frame, counterFromFirstByte(frame.data[0]), 128));
    CHECK(nvj::steadySeconds() - frame.capturedAt < 2.0);

    const nvj::NdiReceiverStatus status = receiver.status();
    CHECK(status.state == nvj::LiveState::Live);
    CHECK(status.source == "A (Cam)");
    CHECK(status.width == 64 && status.height == 36);
    CHECK(status.frames >= 1);
    CHECK(status.attempts == 0);
    CHECK(status.error.empty());
    CHECK(waitFor([&]() { return receiver.status().connections == 2; }, 2.0));
    CHECK(waitFor([&]() { return receiver.status().fps > 30.0; }, 5.0));
    CHECK(receiver.status().fps < 400.0);

    receiver.stop();
    CHECK(!receiver.running());
    CHECK(receiver.status().state == nvj::LiveState::Idle);
    CHECK(fake_ndi_recv_alive() == 0);
    CHECK(fake_ndi_outstanding() == 0);
    CHECK(fake_ndi_captured() == fake_ndi_freed());
    CHECK(fake_ndi_bad_frees() == 0);
    resetFake();
}

void testNewestOnly() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    fake_ndi_set_stream(1, 32, 8, kUyvy, 50, 0);
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    fake_ndi_set_stream(0, 32, 8, kUyvy, 50, 0);
    pause(150);
    nvj::LiveFrame frame;
    while (receiver.take(frame)) {
    }
    const unsigned long long before = receiver.status().dropped;
    const unsigned long long framesBefore = receiver.status().frames;
    fake_ndi_set_counter(1000);
    fake_ndi_push_burst(5);
    CHECK(waitFor([&]() { return fake_ndi_counter() == 1005; }, 5.0));
    CHECK(waitFor([&]() { return receiver.status().frames == framesBefore + 1; }, 5.0));
    CHECK(waitFor([&]() { return receiver.take(frame); }, 5.0));
    CHECK(frame.width == 32 && frame.height == 8);
    CHECK(frameMatches(frame, 1005, 64));
    CHECK(!receiver.take(frame));
    CHECK(receiver.status().dropped >= before + 4);
    receiver.stop();
    CHECK(fake_ndi_outstanding() == 0);
    CHECK(fake_ndi_captured() == fake_ndi_freed());
    CHECK(fake_ndi_bad_frees() == 0);
    resetFake();
}

void testReconnect() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    const int created = fake_ndi_recv_created();
    const int destroyed = fake_ndi_recv_destroyed();
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(fake_ndi_recv_created() == created + 1);

    fake_ndi_set_stream(0, 16, 4, kUyvy, 100, 0);
    CHECK(waitFor([&]() { return receiver.status().state == nvj::LiveState::SignalLost; }, 5.0));
    CHECK(waitFor([&]() { return fake_ndi_recv_destroyed() == destroyed + 1; }, 5.0));
    CHECK(waitFor([&]() { return fake_ndi_recv_created() >= created + 2; }, 5.0));
    CHECK(receiver.status().state != nvj::LiveState::Live);

    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(receiver.status().attempts == 0);
    const int afterLoss = fake_ndi_recv_created();

    fake_ndi_inject_error();
    CHECK(waitFor([&]() { return fake_ndi_recv_created() > afterLoss; }, 5.0));
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(fake_ndi_recv_alive() == 1);

    receiver.stop();
    CHECK(fake_ndi_recv_alive() == 0);
    CHECK(fake_ndi_recv_created() == fake_ndi_recv_destroyed() - destroyed + created);
    CHECK(fake_ndi_outstanding() == 0);
    CHECK(fake_ndi_captured() == fake_ndi_freed());
    resetFake();
}

void testSourceDisappears() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)\nCam (B)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));

    fake_ndi_set_sources("Cam (B)");
    fake_ndi_set_stream(0, 16, 4, kUyvy, 100, 0);
    const int destroyed = fake_ndi_recv_destroyed();
    CHECK(waitFor([&]() { return fake_ndi_recv_destroyed() == destroyed + 1; }, 6.0));
    pause(200);
    const int created = fake_ndi_recv_created();
    CHECK(waitFor([&]() { return receiver.status().attempts >= 3; }, 5.0));
    CHECK(fake_ndi_recv_created() == created);
    CHECK(fake_ndi_recv_alive() == 0);
    CHECK(receiver.status().state == nvj::LiveState::Reconnecting);

    fake_ndi_set_sources("Cam (A)\nCam (B)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(recvName() == "Cam (A)");
    receiver.stop();
    CHECK(fake_ndi_recv_alive() == 0);
    resetFake();
}

void testExcludeOwnSender() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("PC (NyrexVJ)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    const int created = fake_ndi_recv_created();
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("", "NyrexVJ", fastPolicy()));
    pause(500);
    CHECK(fake_ndi_recv_created() == created);
    CHECK(receiver.status().state == nvj::LiveState::Searching);
    CHECK(receiver.status().attempts >= 1);

    fake_ndi_set_sources("PC (NyrexVJ)\nPC (OBS)");
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(recvName() == "PC (OBS)");
    CHECK(receiver.status().source == "PC (OBS)");
    receiver.stop();
    resetFake();
}

void testFormats() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    nvj::NdiReceiver receiver;
    fake_ndi_set_stream(1, 16, 4, kBgra, 100, 0);
    CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return receiver.status().unsupported >= 3; }, 5.0));
    CHECK(receiver.status().state != nvj::LiveState::Live);
    CHECK(receiver.status().error.find("BGRA") != std::string::npos);
    nvj::LiveFrame frame;
    CHECK(!receiver.take(frame));

    fake_ndi_set_stream(1, 16, 4, kUyva, 100, 8);
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(waitFor([&]() { return receiver.take(frame); }, 5.0));
    CHECK(frame.format == nvj::LiveFormat::Uyvy);
    CHECK(frame.stride == 32 && frame.data.size() == 32u * 4u);
    CHECK(frameMatches(frame, counterFromFirstByte(frame.data[0]), 32));
    CHECK(receiver.status().error.empty());

    fake_ndi_set_stream(1, 10, 6, kRgba, 100, 12);
    CHECK(waitFor([&]() {
        nvj::LiveFrame next;
        if (!receiver.take(next)) {
            return false;
        }
        if (next.format != nvj::LiveFormat::Rgba) {
            return false;
        }
        frame = next;
        return true;
    }, 5.0));
    CHECK(frame.width == 10 && frame.height == 6);
    CHECK(frame.stride == 40 && frame.data.size() == 40u * 6u);
    CHECK(frameMatches(frame, counterFromFirstByte(frame.data[0]), 40));

    receiver.stop();
    CHECK(fake_ndi_outstanding() == 0);
    CHECK(fake_ndi_captured() == fake_ndi_freed());
    CHECK(fake_ndi_bad_frees() == 0);
    resetFake();
}

void testOddWidthRejected() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    fake_ndi_set_stream(1, 15, 4, kUyvy, 100, 0);
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return receiver.status().unsupported >= 2; }, 5.0));
    CHECK(receiver.status().state != nvj::LiveState::Live);
    nvj::LiveFrame frame;
    CHECK(!receiver.take(frame));
    receiver.stop();
    CHECK(fake_ndi_outstanding() == 0);
    resetFake();
}

void testRefusedCreate() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    fake_ndi_set_refuse_create(1);
    nvj::NdiReceiver receiver;
    CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return receiver.status().attempts >= 3; }, 5.0));
    CHECK(receiver.status().state == nvj::LiveState::Searching);
    CHECK(!receiver.status().error.empty());
    CHECK(fake_ndi_recv_alive() == 0);
    fake_ndi_set_refuse_create(0);
    CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
    CHECK(receiver.status().error.empty());
    receiver.stop();
    resetFake();
}

void testStopIsFast() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 100, 0);
    const int finderCreated = fake_ndi_finder_created();
    const int finderDestroyed = fake_ndi_finder_destroyed();
    {
        nvj::NdiReceiver receiver;
        CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
        CHECK(waitFor([&]() { return isLive(receiver); }, 5.0));
        fake_ndi_set_stream(0, 16, 4, kUyvy, 100, 0);
        pause(50);
        const auto begin = Clock::now();
        receiver.stop();
        const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        CHECK(seconds < 1.0);
        CHECK(!receiver.running());
        receiver.stop();
        CHECK(receiver.start("Cam (A)", "NyrexVJ", fastPolicy()));
        CHECK(receiver.running());
    }
    CHECK(fake_ndi_recv_alive() == 0);
    CHECK(fake_ndi_finder_created() == fake_ndi_finder_destroyed() - finderDestroyed + finderCreated);
    CHECK(!nvj::NdiFinder::instance().running());
    resetFake();
}

void testTwoReceiversShareFinder() {
    loadFake();
    resetFake();
    fake_ndi_set_sources("Cam (A)");
    fake_ndi_set_stream(1, 16, 4, kUyvy, 200, 0);
    const int finderCreated = fake_ndi_finder_created();
    const int finderDestroyed = fake_ndi_finder_destroyed();
    nvj::NdiReceiver first;
    nvj::NdiReceiver second;
    CHECK(first.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(second.start("Cam (A)", "NyrexVJ", fastPolicy()));
    CHECK(waitFor([&]() { return isLive(first) && isLive(second); }, 5.0));
    CHECK(fake_ndi_finder_created() == finderCreated + 1);
    CHECK(fake_ndi_recv_alive() == 2);
    first.stop();
    CHECK(fake_ndi_finder_destroyed() == finderDestroyed);
    CHECK(nvj::NdiFinder::instance().running());
    CHECK(isLive(second));
    CHECK(fake_ndi_recv_alive() == 1);
    second.stop();
    CHECK(fake_ndi_finder_destroyed() == finderDestroyed + 1);
    CHECK(fake_ndi_recv_alive() == 0);
    CHECK(fake_ndi_outstanding() == 0);
    CHECK(fake_ndi_captured() == fake_ndi_freed());
    resetFake();
}

}

void testRuntimeVersion() {
    loadFake();
    const nvj::ndi::RuntimeInfo info = nvj::ndi::runtimeInfo();
    CHECK(info.version == "NDI SDK FAKE 6.0.1 (test library)");
    CHECK(info.library == NYREXVJ_FAKE_NDI_PATH);
    CHECK(!info.captureV3);
    CHECK(nvj::ndi::sanitizeVersion(nullptr).empty());
    CHECK(nvj::ndi::sanitizeVersion("NDI 6.0") == "NDI 6.0");
    CHECK(nvj::ndi::sanitizeVersion("a\x01\x1b[31mb\n c  ") == "a[31mb  c");
    CHECK(nvj::ndi::sanitizeVersion("caf\xc3\xa9") == "caf");
    CHECK(nvj::ndi::sanitizeVersion(std::string(500, 'x').c_str()).size() == 160u);
}

int main() {
    testPick();
    testMissingLibrary();
    testRuntimeVersion();
    testDiscovery();
    testReceiveByName();
    testNewestOnly();
    testReconnect();
    testSourceDisappears();
    testExcludeOwnSender();
    testFormats();
    testOddWidthRejected();
    testRefusedCreate();
    testStopIsFast();
    testTwoReceiversShareFinder();
    return testing::finish();
}

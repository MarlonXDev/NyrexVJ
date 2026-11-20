#include "check.hpp"

#include "audio/audio_input.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

extern "C" {
void fake_pulse_reset();
void fake_pulse_fail_open(int value);
void fake_pulse_fail_read_after(int reads);
void fake_pulse_fail_context(int value);
int fake_pulse_opened();
int fake_pulse_freed();
int fake_pulse_device_is_null();
const char* fake_pulse_device();
int fake_pulse_rate();
int fake_pulse_channels();
int fake_pulse_format();
uint32_t fake_pulse_fragsize();
}

namespace {

std::vector<float> collect(nvj::AudioInput& input, size_t wanted, int timeoutMs) {
    std::vector<float> all;
    std::vector<float> block;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (all.size() < wanted && std::chrono::steady_clock::now() < end) {
        input.drain(block);
        all.insert(all.end(), block.begin(), block.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return all;
}

int zeroCrossings(const std::vector<float>& samples) {
    int count = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i - 1] < 0.0f) != (samples[i] < 0.0f)) {
            ++count;
        }
    }
    return count;
}

void testMissingLibrary() {
    setenv("NYREXVJ_PULSE_LIB", "/nonexistent/libpulse_missing.so.0", 1);
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    CHECK(input->listDevices().empty());
    std::string error;
    CHECK(!input->start("", error));
    CHECK(error.find("PulseAudio") != std::string::npos);
    CHECK(!input->running());
    std::vector<float> samples = {1.0f};
    input->drain(samples);
    CHECK(samples.empty());
}

void testDevices() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    const std::vector<nvj::AudioDevice> devices = input->listDevices();
    CHECK(devices.size() == 5);
    if (devices.size() == 5) {
        CHECK(devices[0].id == "@DEFAULT_SOURCE@");
        CHECK(!devices[0].loopback);
        CHECK(devices[1].id == "@DEFAULT_MONITOR@");
        CHECK(devices[1].loopback);
        CHECK(devices[2].id == "alsa_input.pci-0000_00_1f.3.analog-stereo");
        CHECK(devices[2].name == "Built-in Audio Analog Stereo");
        CHECK(!devices[2].loopback);
        CHECK(devices[3].loopback);
        CHECK(devices[4].name == devices[4].id);
    }
}

void testContextFailure() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    fake_pulse_fail_context(1);
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    const std::vector<nvj::AudioDevice> devices = input->listDevices();
    CHECK(devices.size() == 2);
}

void testCapture() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    std::string error;
    CHECK(input->start("alsa_input.pci-0000_00_1f.3.analog-stereo", error));
    CHECK(error.empty());
    CHECK(input->running());
    CHECK(input->sampleRate() == 48000);
    CHECK(fake_pulse_opened() == 1);
    CHECK(std::string(fake_pulse_device()) == "alsa_input.pci-0000_00_1f.3.analog-stereo");
    CHECK(fake_pulse_rate() == 48000);
    CHECK(fake_pulse_channels() == 1);
    CHECK(fake_pulse_format() == 5);
    CHECK(fake_pulse_fragsize() == 480 * sizeof(float));
    const std::vector<float> samples = collect(*input, 9600, 3000);
    CHECK(samples.size() >= 9600);
    float peak = 0.0f;
    for (float value : samples) {
        peak = std::max(peak, std::fabs(value));
    }
    CHECK(peak > 0.45f && peak < 0.55f);
    const size_t used = std::min<size_t>(samples.size(), 9600);
    const std::vector<float> first(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(used));
    const double seconds = static_cast<double>(used) / 48000.0;
    const double frequency = zeroCrossings(first) / 2.0 / seconds;
    CHECK(frequency > 420.0 && frequency < 460.0);
    input->stop();
    CHECK(!input->running());
    CHECK(fake_pulse_freed() == 1);
    std::vector<float> after;
    input->drain(after);
    CHECK(after.empty());
}

void testDefaultDevices() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    std::string error;
    CHECK(input->start("@DEFAULT_SOURCE@", error));
    CHECK(fake_pulse_device_is_null() == 1);
    input->stop();
    CHECK(input->start("@DEFAULT_MONITOR@", error));
    CHECK(fake_pulse_device_is_null() == 0);
    CHECK(std::string(fake_pulse_device()) == "@DEFAULT_MONITOR@");
    input->stop();
    CHECK(input->start("", error));
    CHECK(fake_pulse_device_is_null() == 1);
    input->stop();
    CHECK(fake_pulse_opened() == 3);
    CHECK(fake_pulse_freed() == 3);
}

void testOpenFailure() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    fake_pulse_fail_open(1);
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    std::string error;
    CHECK(!input->start("anything", error));
    CHECK(error.find("Could not open") != std::string::npos);
    CHECK(error.find("Connection refused") != std::string::npos);
    CHECK(!input->running());
}

void testReadFailure() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    fake_pulse_fail_read_after(5);
    std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
    std::string error;
    CHECK(input->start("", error));
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (input->running() && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!input->running());
    input->stop();
    CHECK(fake_pulse_freed() == 1);
    fake_pulse_fail_read_after(-1);
    CHECK(input->start("", error));
    CHECK(input->running());
    input->stop();
}

void testRestartAndDestructor() {
    setenv("NYREXVJ_PULSE_LIB", NYREXVJ_FAKE_PULSE_PATH, 1);
    fake_pulse_reset();
    {
        std::unique_ptr<nvj::AudioInput> input = nvj::createAudioInput();
        std::string error;
        CHECK(input->start("", error));
        CHECK(input->start("", error));
        CHECK(input->running());
        CHECK(fake_pulse_opened() == 2);
        CHECK(fake_pulse_freed() == 1);
    }
    CHECK(fake_pulse_freed() == 2);
}

}

int main() {
    testMissingLibrary();
    testDevices();
    testContextFailure();
    testCapture();
    testDefaultDevices();
    testOpenFailure();
    testReadFailure();
    testRestartAndDestructor();
    return testing::finish();
}

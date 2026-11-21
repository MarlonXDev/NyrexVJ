#include "check.hpp"

#include "control/midi_input.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

extern "C" {
void fake_alsa_reset();
void fake_alsa_fail_open(int value);
void fake_alsa_push(int type, int channel, int a, int b, int value);
int fake_alsa_opened();
int fake_alsa_closed();
int fake_alsa_connected_client();
int fake_alsa_connected_port();
int fake_alsa_ports_created();
int fake_alsa_ports_deleted();
const char* fake_alsa_client_name();
}

namespace {

constexpr int kNoteOn = 6;
constexpr int kNoteOff = 7;
constexpr int kController = 10;
constexpr int kProgram = 11;
constexpr int kChannelPress = 12;
constexpr int kPitchBend = 13;
constexpr int kClock = 36;
constexpr int kStart = 30;
constexpr int kContinue = 31;
constexpr int kStop = 32;
constexpr int kSongPosition = 20;
constexpr int kQuarterFrame = 22;
constexpr int kTick = 37;

void useFake() {
    setenv("NYREXVJ_ALSA_LIB", NYREXVJ_FAKE_ALSA_PATH, 1);
    fake_alsa_reset();
}

std::vector<nvj::MidiMessage> collect(nvj::MidiInput& input, size_t wanted, int timeoutMs) {
    std::vector<nvj::MidiMessage> all;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (all.size() < wanted && std::chrono::steady_clock::now() < end) {
        input.drain(all);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return all;
}

void testMissingLibrary() {
    setenv("NYREXVJ_ALSA_LIB", "/nonexistent/libasound_missing.so.2", 1);
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    CHECK(input->listDevices().empty());
    std::string error;
    CHECK(!input->start("anything", error));
    CHECK(error.find("ALSA") != std::string::npos);
    CHECK(!input->running());
    std::vector<nvj::MidiMessage> messages;
    input->drain(messages);
    CHECK(messages.empty());
}

void testDevices() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    const std::vector<std::string> names = input->listDevices();
    CHECK(names.size() == 3);
    if (names.size() == 3) {
        CHECK(names[0] == "Midi Through:Midi Through Port-0");
        CHECK(names[1] == "Test Controller:Test Controller MIDI 1");
        CHECK(names[2] == "Test Controller:Test Controller MIDI 1 #2");
    }
    CHECK(fake_alsa_opened() == 1);
    CHECK(fake_alsa_closed() == 1);
}

void testListFailure() {
    useFake();
    fake_alsa_fail_open(1);
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    CHECK(input->listDevices().empty());
    std::string error;
    CHECK(!input->start("", error));
    CHECK(error.find("sequencer") != std::string::npos);
    CHECK(!input->running());
}

void testConnect() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    std::string error;
    CHECK(input->start("Test Controller:Test Controller MIDI 1 #2", error));
    CHECK(input->running());
    CHECK(fake_alsa_connected_client() == 21);
    CHECK(fake_alsa_connected_port() == 0);
    CHECK(std::string(fake_alsa_client_name()) == "NyrexVJ");
    CHECK(fake_alsa_ports_created() == 1);
    input->stop();
    CHECK(!input->running());
    CHECK(fake_alsa_ports_deleted() == 1);
    CHECK(fake_alsa_closed() == fake_alsa_opened());
}

void testFirstDeviceWhenEmpty() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    std::string error;
    CHECK(input->start("", error));
    CHECK(fake_alsa_connected_client() == 14);
    input->stop();
}

void testUnknownDevice() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    std::string error;
    CHECK(!input->start("Does Not Exist", error));
    CHECK(error.find("not found") != std::string::npos);
    CHECK(!input->running());
    CHECK(fake_alsa_closed() == fake_alsa_opened());
    CHECK(fake_alsa_ports_created() == 0);
}

void testTranslation() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    std::string error;
    CHECK(input->start("Test Controller:Test Controller MIDI 1", error));
    fake_alsa_push(kNoteOn, 2, 60, 100, 0);
    fake_alsa_push(kNoteOff, 2, 60, 0, 0);
    fake_alsa_push(kController, 0, 7, 0, 64);
    fake_alsa_push(kClock, 0, 0, 0, 0);
    fake_alsa_push(kController, 5, 1, 0, 200);
    fake_alsa_push(kPitchBend, 1, 0, 0, -8192);
    fake_alsa_push(kPitchBend, 1, 0, 0, 8191);
    fake_alsa_push(kProgram, 3, 0, 0, 5);
    fake_alsa_push(kChannelPress, 4, 0, 0, 90);
    const std::vector<nvj::MidiMessage> messages = collect(*input, 9, 2000);
    CHECK(messages.size() == 9);
    if (messages.size() == 9) {
        CHECK(messages[0].status == 0x92 && messages[0].data1 == 60 && messages[0].data2 == 100);
        CHECK(messages[1].status == 0x82 && messages[1].data1 == 60 && messages[1].data2 == 0);
        CHECK(messages[2].status == 0xB0 && messages[2].data1 == 7 && messages[2].data2 == 64);
        CHECK(messages[3].status == 0xF8);
        CHECK(messages[4].status == 0xB5 && messages[4].data1 == 1 && messages[4].data2 == 127);
        CHECK(messages[5].status == 0xE1 && messages[5].data1 == 0 && messages[5].data2 == 0);
        CHECK(messages[6].status == 0xE1 && messages[6].data1 == 127 && messages[6].data2 == 127);
        CHECK(messages[7].status == 0xC3 && messages[7].data1 == 5);
        CHECK(messages[8].status == 0xD4 && messages[8].data1 == 90);
    }
    input->stop();
}

void testSystemMessages() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    std::string error;
    CHECK(input->start("Test Controller:Test Controller MIDI 1", error));
    const double before = nvj::midiNow();
    fake_alsa_push(kStart, 0, 0, 0, 0);
    fake_alsa_push(kClock, 0, 0, 0, 0);
    fake_alsa_push(kTick, 0, 0, 0, 0);
    fake_alsa_push(kStop, 0, 0, 0, 0);
    fake_alsa_push(kContinue, 0, 0, 0, 0);
    fake_alsa_push(kSongPosition, 0, 0, 0, 300);
    fake_alsa_push(kSongPosition, 0, 0, 0, 99999);
    fake_alsa_push(kQuarterFrame, 0, 0, 0, 0x35);
    fake_alsa_push(kQuarterFrame, 0, 0, 0, 0x7A);
    fake_alsa_push(kNoteOn, 1, 64, 90, 0);
    const std::vector<nvj::MidiMessage> messages = collect(*input, 9, 2000);
    const double after = nvj::midiNow();
    CHECK(messages.size() == 9);
    if (messages.size() == 9) {
        CHECK(messages[0].status == 0xFA);
        CHECK(messages[1].status == 0xF8);
        CHECK(messages[2].status == 0xFC);
        CHECK(messages[3].status == 0xFB);
        CHECK(messages[4].status == 0xF2 && messages[4].data1 == (300 & 0x7F) && messages[4].data2 == ((300 >> 7) & 0x7F));
        CHECK(messages[5].status == 0xF2 && messages[5].data1 == 0x7F && messages[5].data2 == 0x7F);
        CHECK(messages[6].status == 0xF1 && messages[6].data1 == 0x35);
        CHECK(messages[7].status == 0xF1 && messages[7].data1 == 0x7A);
        CHECK(messages[8].status == 0x91 && messages[8].data1 == 64);
        bool stamped = true;
        double previous = before;
        for (const nvj::MidiMessage& message : messages) {
            if (message.time < previous || message.time > after) {
                stamped = false;
            }
            previous = message.time;
        }
        CHECK(stamped);
        for (const nvj::MidiMessage& message : messages) {
            CHECK(nvj::isSystemMidi(message) == (message.status >= 0xF0));
        }
    }
    input->stop();
}

void testLateEvent() {
    useFake();
    std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
    std::string error;
    CHECK(input->start("", error));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::vector<nvj::MidiMessage> none;
    input->drain(none);
    CHECK(none.empty());
    fake_alsa_push(kNoteOn, 0, 48, 90, 0);
    const std::vector<nvj::MidiMessage> messages = collect(*input, 1, 1000);
    CHECK(messages.size() == 1);
    input->stop();
    std::vector<nvj::MidiMessage> after;
    input->drain(after);
    CHECK(after.empty());
}

void testRestartAndDestructor() {
    useFake();
    {
        std::unique_ptr<nvj::MidiInput> input = nvj::createMidiInput();
        std::string error;
        CHECK(input->start("", error));
        CHECK(input->start("", error));
        CHECK(input->running());
        CHECK(fake_alsa_ports_created() == 2);
        CHECK(fake_alsa_ports_deleted() == 1);
    }
    CHECK(fake_alsa_ports_deleted() == 2);
    CHECK(fake_alsa_closed() == fake_alsa_opened());
}

}

int main() {
    testMissingLibrary();
    testDevices();
    testListFailure();
    testConnect();
    testFirstDeviceWhenEmpty();
    testUnknownDevice();
    testTranslation();
    testSystemMessages();
    testLateEvent();
    testRestartAndDestructor();
    return testing::finish();
}

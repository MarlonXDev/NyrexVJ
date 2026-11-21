#include "check.hpp"
#include "control/sync_system.hpp"
#include "ltc_signal.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

double circular(double a, double b, double modulus) {
    double d = std::fmod(a - b, modulus);
    if (d < 0.0) {
        d += modulus;
    }
    return std::min(d, modulus - d);
}

nvj::MidiMessage message(int status, int data1, int data2, double time) {
    nvj::MidiMessage m;
    m.status = status;
    m.data1 = data1;
    m.data2 = data2;
    m.time = time;
    return m;
}

struct Rig {
    nvj::SyncSystem system;
    nvj::BeatClock clock;
    double now = 1000.0;
    const double dt = 1.0 / 60.0;
    std::vector<nvj::MidiMessage> scheduled;

    void step() {
        now += dt;
        std::vector<nvj::MidiMessage> due;
        std::vector<nvj::MidiMessage> later;
        for (const nvj::MidiMessage& m : scheduled) {
            (m.time <= now ? due : later).push_back(m);
        }
        scheduled.swap(later);
        system.update(dt, due, clock, now);
        clock.advance(dt, true);
    }

    void run(double seconds) {
        const double end = now + seconds;
        while (now < end) {
            step();
        }
    }
};

void testMidiClock() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Auto;
    rig.system.configure(config);
    const double bpm = 126.0;
    const double tick = 60.0 / (bpm * 24.0);
    const double start = rig.now + 0.1;
    rig.scheduled.push_back(message(0xFA, 0, 0, start));
    for (int i = 0; i < 24 * 80; ++i) {
        rig.scheduled.push_back(message(0xF8, 0, 0, start + 0.001 + i * tick));
    }
    rig.run(30.0);
    CHECK(rig.clock.active() == nvj::SyncSource::MidiClock);
    CHECK(rig.clock.locked());
    CHECK(near(rig.clock.bpm(), bpm, 0.8));
    CHECK(rig.system.midiClock().running());
    CHECK(rig.system.midiClock().positionKnown());
    CHECK(!rig.system.timecodeActive());
    const double elapsed = rig.now - (start + 0.001);
    const double expected = std::fmod(elapsed * bpm / 60.0, 4.0);
    std::printf("sync system midi clock offset %.3f beats\n", circular(rig.clock.beat(), expected, 4.0));
    CHECK(circular(rig.clock.beat(), expected, 4.0) < 0.2);
    rig.scheduled.clear();
    rig.run(2.5);
    CHECK(rig.clock.active() != nvj::SyncSource::MidiClock);
}

void testModeFollowsConfig() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Manual;
    config.latencyMs = 40.0;
    rig.system.configure(config);
    rig.step();
    CHECK(rig.clock.mode() == nvj::SyncMode::Manual);
    CHECK(near(rig.clock.latency(), 0.04, 1.0e-9));
    const double tick = 60.0 / (126.0 * 24.0);
    for (int i = 0; i < 24 * 8; ++i) {
        rig.scheduled.push_back(message(0xF8, 0, 0, rig.now + 0.05 + i * tick));
    }
    rig.run(4.0);
    CHECK(rig.clock.active() == nvj::SyncSource::Manual);
    CHECK(near(rig.clock.bpm(), 128.0, 1.0e-6));
    config.mode = nvj::SyncMode::Auto;
    config.latencyMs = -20.0;
    rig.system.configure(config);
    rig.step();
    CHECK(rig.clock.mode() == nvj::SyncMode::Auto);
    CHECK(near(rig.clock.latency(), -0.02, 1.0e-9));
}

void feedMtc(Rig& rig, nvj::FrameRate rate, long long firstFrame, int sequences, double startTime) {
    const double fps = nvj::exactFps(rate);
    for (int s = 0; s < sequences; ++s) {
        const nvj::Timecode t = nvj::timecodeFromFrames(firstFrame + 2LL * s, rate);
        const std::array<int, 8> data = ltctest::quarterFrames(t);
        for (int k = 0; k < 8; ++k) {
            const double time = startTime + (static_cast<double>(s) * 8.0 + k) * (1.0 / (4.0 * fps));
            rig.scheduled.push_back(message(0xF1, data[static_cast<size_t>(k)], 0, time));
        }
    }
}

void testMtc() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Auto;
    rig.system.configure(config);
    const nvj::FrameRate rate = nvj::FrameRate::Fps25;
    const long long first = nvj::timecodeToFrames(nvj::Timecode{0, 10, 0, 0, rate});
    const double startTime = rig.now + 0.2;
    feedMtc(rig, rate, first, 100, startTime);
    rig.run(1.0);
    CHECK(rig.system.timecodeActive());
    CHECK(rig.system.timecodeSource() == nvj::TimecodeSource::Mtc);
    const double expectedSeconds = 600.0 + (rig.now - startTime);
    std::printf("mtc seconds %.3f expected about %.3f\n", rig.system.timecodeSeconds(), expectedSeconds);
    CHECK(near(rig.system.timecodeSeconds(), expectedSeconds, 0.12));
    CHECK(rig.system.timecode().minutes == 10);
    CHECK(rig.system.timecode().rate == rate);
    CHECK(rig.system.mtc().dropped() == 0);
    rig.scheduled.clear();
    rig.run(3.0);
    CHECK(!rig.system.timecodeActive());
}

void testLtcAndPriority() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Auto;
    rig.system.configure(config);
    ltctest::Options options;
    const nvj::FrameRate rate = nvj::FrameRate::Fps30;
    const nvj::Timecode start{1, 0, 0, 0, rate};
    ltctest::Generator generator(options, start);
    const long long mtcFirst = nvj::timecodeToFrames(nvj::Timecode{5, 0, 0, 0, rate});
    feedMtc(rig, rate, mtcFirst, 200, rig.now);
    const size_t chunk = static_cast<size_t>(options.sampleRate / 60);
    for (int frame = 0; frame < 120; ++frame) {
        const std::vector<float> samples = generator.render(chunk);
        rig.system.pushLtcSamples(samples.data(), samples.size(), options.sampleRate);
        rig.step();
    }
    CHECK(rig.system.timecodeActive());
    CHECK(rig.system.timecodeSource() == nvj::TimecodeSource::Ltc);
    CHECK(rig.system.timecode().hours == 1);
    CHECK(rig.system.timecode().rate == rate);
    const double expected = 3600.0 + 2.0;
    std::printf("ltc seconds %.3f expected about %.3f\n", rig.system.timecodeSeconds(), expected);
    CHECK(near(rig.system.timecodeSeconds(), expected, 0.1));
    CHECK(rig.system.ltc().frames() > 40);
    CHECK(rig.system.ltcTracker().running());
    CHECK(rig.system.mtcTracker().running());
    for (int frame = 0; frame < 60; ++frame) {
        rig.step();
    }
    CHECK(rig.system.timecodeSource() == nvj::TimecodeSource::Mtc);
}

void testTimecodeDrivesClock() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Timecode;
    rig.system.configure(config);
    rig.clock.setTempo(120.0);
    ltctest::Options options;
    const nvj::Timecode start{0, 0, 10, 0, nvj::FrameRate::Fps25};
    ltctest::Generator generator(options, start);
    const size_t chunk = static_cast<size_t>(options.sampleRate / 60);
    for (int frame = 0; frame < 60 * 8; ++frame) {
        const std::vector<float> samples = generator.render(chunk);
        rig.system.pushLtcSamples(samples.data(), samples.size(), options.sampleRate);
        rig.step();
    }
    CHECK(rig.clock.active() == nvj::SyncSource::Timecode);
    CHECK(rig.clock.locked());
    const double expected = rig.system.timecodeSeconds() * 2.0;
    std::printf("timecode beat %.3f expected %.3f\n", rig.clock.beat(), expected);
    CHECK(near(rig.clock.beat(), expected, 0.15));
    CHECK(near(rig.clock.bpm(), 120.0, 1.0e-6));
}

void testCues() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Manual;
    rig.system.configure(config);
    nvj::Cue one;
    one.seconds = 11.0;
    one.target = "tap";
    nvj::Cue two;
    two.seconds = 12.5;
    two.target = "master/level";
    two.value = 0.25f;
    rig.system.cues() = {one, two};
    ltctest::Options options;
    const nvj::Timecode start{0, 0, 10, 0, nvj::FrameRate::Fps25};
    ltctest::Generator generator(options, start);
    const size_t chunk = static_cast<size_t>(options.sampleRate / 60);
    std::vector<std::string> fired;
    for (int frame = 0; frame < 60 * 5; ++frame) {
        const std::vector<float> samples = generator.render(chunk);
        rig.system.pushLtcSamples(samples.data(), samples.size(), options.sampleRate);
        rig.step();
        for (const nvj::Cue& cue : rig.system.firedCues()) {
            fired.push_back(cue.target);
        }
    }
    CHECK(fired.size() == 2);
    CHECK(fired.size() == 2 && fired[0] == "tap" && fired[1] == "master/level");
    config.cues = false;
    rig.system.configure(config);
    rig.step();
    CHECK(!rig.system.cueRunner().tracking());
}

void testCuePersistence() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nvj_sync_cues";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dir.string().c_str(), 1);
#endif
    {
        nvj::SyncSystem system;
        CHECK(!system.loadCues());
        nvj::Cue cue;
        cue.seconds = 90.5;
        cue.target = "deck/next";
        system.cues().push_back(cue);
        nvj::Cue early;
        early.seconds = 3.0;
        early.target = "tap";
        system.cues().push_back(early);
        system.markCuesDirty();
        nvj::BeatClock clock;
        system.update(1.0, {}, clock, 5.0);
    }
    nvj::SyncSystem again;
    CHECK(again.loadCues());
    CHECK(again.cues().size() == 2);
    CHECK(again.cues().size() == 2 && again.cues()[0].target == "tap" && near(again.cues()[1].seconds, 90.5, 1.0e-9));
}

void testDjLinkConfig() {
    Rig rig;
    nvj::SyncConfig config;
    config.mode = nvj::SyncMode::Auto;
    config.djLink = true;
    config.djPort = 52895;
    rig.system.configure(config);
    CHECK(rig.system.djLinkListening());
    CHECK(rig.system.djLinkError().empty());
    config.djPlayer = 3;
    rig.system.configure(config);
    CHECK(rig.system.djLink().player() == 3);
    config.djLink = false;
    rig.system.configure(config);
    CHECK(!rig.system.djLinkListening());
}

void testLtcInputErrors() {
    Rig rig;
    nvj::SyncConfig config;
    config.ltcDevice = "no-such-device-id";
    rig.system.configure(config);
    rig.step();
    CHECK(!rig.system.ltcRunning());
    config.ltcDevice.clear();
    rig.system.configure(config);
    CHECK(rig.system.ltcError().empty());
    rig.system.pushLtcSamples(nullptr, 10, 48000);
    rig.system.pushLtcSamples(nullptr, 0, 48000);
    float one = 0.0f;
    rig.system.pushLtcSamples(&one, 1, 0);
    CHECK(!rig.system.timecodeActive());
}

}

int main() {
    testMidiClock();
    testModeFollowsConfig();
    testMtc();
    testLtcAndPriority();
    testTimecodeDrivesClock();
    testCues();
    testCuePersistence();
    testDjLinkConfig();
    testLtcInputErrors();
    return testing::finish();
}

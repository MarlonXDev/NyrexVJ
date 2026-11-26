#include "check.hpp"

#include "core/settings.hpp"
#include "core/stoppable_thread.hpp"
#include "core/stream_clock.hpp"
#include "core/stream_url.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <thread>

namespace {

using namespace nvj::stream;

bool accepted(const std::string& text) {
    return parseUrl(text).ok;
}

bool refused(const std::string& text) {
    const ParseResult result = parseUrl(text);
    return !result.ok && !result.error.empty();
}

std::string optionValue(const OpenPlan& plan, const std::string& key) {
    for (const auto& option : plan.options) {
        if (option.first == key) {
            return option.second;
        }
    }
    return "<none>";
}

void testAllowedAddresses() {
    CHECK(accepted("rtsp://192.168.1.20/stream1"));
    CHECK(accepted("rtsp://192.168.1.20:8554/live/main"));
    CHECK(accepted("RTSP://camera.local/stream"));
    CHECK(accepted("rtsp://admin:secret@192.168.1.20/cam/realmonitor?channel=1&subtype=0"));
    CHECK(accepted("rtsp://[fe80::1]:554/s"));
    CHECK(accepted("srt://203.0.113.5:9000"));
    CHECK(accepted("srt://encoder.example.org:9000?mode=caller&latency=200000"));
    CHECK(accepted("srt://:9000?mode=listener"));
    CHECK(accepted("srt://0.0.0.0:9000?mode=listener&passphrase=0123456789ab"));
    CHECK(accepted("srt://host:9000?streamid=live%2Fmain"));
    CHECK(accepted("udp://239.1.1.1:5000"));
    CHECK(accepted("udp://@:5000"));
    CHECK(accepted("udp://:5000?fifo_size=7168&overrun_nonfatal=1"));
    CHECK(accepted("  udp://127.0.0.1:5000  "));
    CHECK(accepted("udp://239.1.1.1:5000?localaddr=192.168.1.5"));
    CHECK(accepted("udp://239.1.1.1:5000?localaddr=fe80::1"));
}

void testRefusedAddresses() {
    CHECK(refused(""));
    CHECK(refused("   "));
    CHECK(refused("rtsp"));
    CHECK(refused("file:///etc/passwd"));
    CHECK(refused("concat:a.ts|b.ts"));
    CHECK(refused("subfile,,start,0,end,0,,:/etc/passwd"));
    CHECK(refused("pipe:0"));
    CHECK(refused("http://example.org/live.m3u8"));
    CHECK(refused("https://example.org/live.m3u8"));
    CHECK(refused("tcp://127.0.0.1:5000"));
    CHECK(refused("rtmp://example.org/live/stream"));
    CHECK(refused("rtsps://example.org/stream"));
    CHECK(refused("unix:///tmp/socket"));
    CHECK(refused("data:text/plain;base64,AAAA"));
    CHECK(refused("rtsp://"));
    CHECK(refused("rtsp:///stream"));
    CHECK(refused("rtsp://host/stream with space"));
    CHECK(refused("rtsp://host/stream\nrtsp://other"));
    CHECK(refused(std::string("rtsp://host/stream") + '\0' + "x"));
    CHECK(refused("rtsp://host/stream\x01"));
    CHECK(refused("rtsp://h\xc3\xb6st/stream"));
    CHECK(refused("rtsp://host/..//etc"));
    CHECK(refused("rtsp://host/a#frag"));
    CHECK(refused("rtsp://host:0/s"));
    CHECK(refused("rtsp://host:65536/s"));
    CHECK(refused("rtsp://host:abc/s"));
    CHECK(refused("rtsp://user:p@ss@host/s"));
    CHECK(refused("rtsp://ho st/s"));
    CHECK(refused("rtsp://host;rm/s"));
    CHECK(refused("srt://user:pass@host:9000"));
    CHECK(refused("srt://host"));
    CHECK(refused("srt://host:9000/path"));
    CHECK(refused("srt://:9000"));
    CHECK(refused("srt://host:9000?mode=rendezvous"));
    CHECK(refused("srt://host:9000?passphrase=short"));
    CHECK(refused("srt://host:9000?passphrase=" + std::string(65, 'a')));
    CHECK(refused("srt://host:9000?latency=-5"));
    CHECK(refused("srt://host:9000?latency=abc"));
    CHECK(refused("srt://host:9000?latency=99999999999"));
    CHECK(refused("srt://host:9000?unknown=1"));
    CHECK(refused("srt://host:9000?mode=caller&mode=listener"));
    CHECK(refused("srt://host:9000?streamid="));
    CHECK(refused("srt://host:9000?streamid=" + std::string(513, 'a')));
    CHECK(refused("udp://user@host:5000"));
    CHECK(refused("udp://host"));
    CHECK(refused("udp://host:5000/path"));
    CHECK(refused("udp://host:5000?localaddr=a%20b"));
    CHECK(refused("udp://host:5000?localaddr=example"));
    CHECK(refused("udp://host:5000?localaddr="));
    CHECK(refused("udp://host:5000?fifo_size=1"));
    CHECK(refused("udp://host:5000?listen=1"));
    CHECK(refused("rtsp://host/" + std::string(1100, 'a')));
}

void testFields() {
    const ParseResult rtsp = parseUrl("rtsp://admin:s3cret@cam.local:8554/live?ch=1");
    CHECK(rtsp.ok);
    CHECK(rtsp.url.scheme == Scheme::Rtsp);
    CHECK(rtsp.url.host == "cam.local");
    CHECK(rtsp.url.port == 8554);
    CHECK(rtsp.url.user == "admin");
    CHECK(rtsp.url.password == "s3cret");
    CHECK(rtsp.url.hasCredentials);
    CHECK(rtsp.url.path == "/live?ch=1");
    CHECK(!rtsp.url.listener);

    const ParseResult plain = parseUrl("rtsp://cam.local");
    CHECK(plain.ok && plain.url.path == "/" && plain.url.port == 0 && !plain.url.hasCredentials);

    const ParseResult srtCaller = parseUrl("srt://host:9000");
    CHECK(srtCaller.ok && !srtCaller.url.listener && srtCaller.url.port == 9000);

    const ParseResult srtListener = parseUrl("srt://:9000?mode=listener");
    CHECK(srtListener.ok && srtListener.url.listener && srtListener.url.host.empty());

    const ParseResult udp = parseUrl("udp://@:5000");
    CHECK(udp.ok && udp.url.listener && udp.url.host.empty() && udp.url.port == 5000);

    const ParseResult v6 = parseUrl("rtsp://[fe80::1]:554/s");
    CHECK(v6.ok && v6.url.host == "[fe80::1]" && v6.url.port == 554);
}

void testMasking() {
    CHECK(maskUrl("rtsp://admin:s3cret@cam/live") == "rtsp://admin:****@cam/live");
    CHECK(maskUrl("rtsp://admin@cam/live") == "rtsp://admin@cam/live");
    CHECK(maskUrl("rtsp://cam/live?user=a&password=hunter2&ch=1") == "rtsp://cam/live?user=a&password=****&ch=1");
    CHECK(maskUrl("srt://host:9000?passphrase=0123456789ab&latency=120000") == "srt://host:9000?passphrase=****&latency=120000");
    CHECK(maskUrl("srt://host:9000?streamid=secretstream") == "srt://host:9000?streamid=****");
    CHECK(maskUrl("rtsp://cam/live?token=abc").find("abc") == std::string::npos);
    CHECK(maskUrl("rtsp://a:b:c@cam/").find("b:c") == std::string::npos);
    CHECK(maskUrl("rtsp://x:p@ss@cam/").find("ss") == std::string::npos);
    CHECK(maskUrl("udp://@:5000") == "udp://@:5000");
    CHECK(maskUrl("not a url") == "not a url");
    const std::string control = maskUrl("rtsp://cam/\x01\x1b[31m");
    for (char c : control) {
        CHECK(static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7f);
    }
    CHECK(maskUrl(std::string(2000, 'a')).size() <= 303u);
    const std::string leaked = maskUrl("rtsp://admin:topsecret@cam/live?passphrase=abcdefghijkl");
    CHECK(leaked.find("topsecret") == std::string::npos);
    CHECK(leaked.find("abcdefghijkl") == std::string::npos);
}

void testPlans() {
    const ParseResult rtsp = parseUrl("rtsp://admin:s3cret@cam.local/live");
    const OpenPlan rtspPlan = makeOpenPlan(rtsp.url, Profile::Balanced);
    CHECK(rtspPlan.url == "rtsp://admin:s3cret@cam.local/live");
    CHECK(!rtspPlan.listener);
    CHECK(optionValue(rtspPlan, "protocol_whitelist") == "rtsp,rtp,udp,tcp");
    CHECK(optionValue(rtspPlan, "rtsp_transport") == "tcp");
    CHECK(optionValue(rtspPlan, "allowed_media_types") == "video");
    CHECK(optionValue(rtspPlan, "max_streams") == std::to_string(kMaxStreams));
    CHECK(optionValue(rtspPlan, "timeout") == "5000000");

    const ParseResult srt = parseUrl("srt://:9000?mode=listener");
    const OpenPlan low = makeOpenPlan(srt.url, Profile::LowLatency);
    const OpenPlan balanced = makeOpenPlan(srt.url, Profile::Balanced);
    const OpenPlan smooth = makeOpenPlan(srt.url, Profile::Smooth);
    CHECK(low.listener && balanced.listener && smooth.listener);
    CHECK(low.waitsForPeer);
    CHECK(low.url == "srt://:9000?mode=listener&latency=120000&connect_timeout=4000&timeout=5000000");
    CHECK(balanced.url.find("latency=250000") != std::string::npos);
    CHECK(smooth.url.find("latency=800000") != std::string::npos);
    CHECK(optionValue(low, "protocol_whitelist") == "srt");
    CHECK(optionValue(low, "fflags") == "nobuffer");
    CHECK(optionValue(low, "flags") == "low_delay");
    CHECK(optionValue(smooth, "fflags") == "<none>");
    CHECK(optionValue(low, "rtsp_transport") == "<none>");
    CHECK(std::stoll(optionValue(low, "probesize")) < std::stoll(optionValue(smooth, "probesize")));
    CHECK(std::stoll(optionValue(low, "analyzeduration")) < std::stoll(optionValue(smooth, "analyzeduration")));
    CHECK(std::stoll(optionValue(low, "max_delay")) < std::stoll(optionValue(smooth, "max_delay")));

    const ParseResult own = parseUrl("srt://host:9000?latency=300000&passphrase=0123456789ab");
    const OpenPlan ownPlan = makeOpenPlan(own.url, Profile::LowLatency);
    CHECK(ownPlan.url.find("latency=300000") != std::string::npos);
    CHECK(ownPlan.url.find("latency=120000") == std::string::npos);
    CHECK(ownPlan.url.find("passphrase=0123456789ab") != std::string::npos);
    CHECK(!ownPlan.listener);

    const ParseResult udp = parseUrl("udp://@:5000");
    const OpenPlan udpPlan = makeOpenPlan(udp.url, Profile::Smooth);
    CHECK(udpPlan.listener);
    CHECK(!udpPlan.waitsForPeer);
    CHECK(!rtspPlan.waitsForPeer);
    CHECK(udpPlan.url == "udp://0.0.0.0:5000?fifo_size=28672&overrun_nonfatal=1&timeout=5000000");
    CHECK(optionValue(udpPlan, "protocol_whitelist") == "udp");

    const ParseResult multicast = parseUrl("udp://239.1.1.1:5000?fifo_size=1000");
    const OpenPlan multicastPlan = makeOpenPlan(multicast.url, Profile::Balanced);
    CHECK(multicastPlan.url == "udp://239.1.1.1:5000?fifo_size=1000&overrun_nonfatal=1&timeout=5000000");
}

void testProfiles() {
    CHECK(profileFromInt(-4) == Profile::LowLatency);
    CHECK(profileFromInt(0) == Profile::LowLatency);
    CHECK(profileFromInt(1) == Profile::Balanced);
    CHECK(profileFromInt(2) == Profile::Smooth);
    CHECK(profileFromInt(99) == Profile::Smooth);
    CHECK(profileSpec(Profile::LowLatency).lagLimitSeconds < profileSpec(Profile::Balanced).lagLimitSeconds);
    CHECK(profileSpec(Profile::Balanced).lagLimitSeconds < profileSpec(Profile::Smooth).lagLimitSeconds);
    CHECK(profileSpec(Profile::LowLatency).srtLatencyMicros < profileSpec(Profile::Smooth).srtLatencyMicros);
}

void testLimits() {
    for (const char* name : {"h264", "hevc", "av1", "mjpeg", "mpeg2video", "vp8", "vp9"}) {
        CHECK(codecAllowed(name));
    }
    for (const char* name : {"", "mpeg4", "msmpeg4v3", "wmv3", "rawvideo", "png", "gif", "jpeg2000", "prores", "dvvideo", "cinepak", "H264", "h264 "}) {
        CHECK(!codecAllowed(name));
    }
    CHECK(pictureSizeAllowed(1920, 1080));
    CHECK(pictureSizeAllowed(3840, 2160));
    CHECK(pictureSizeAllowed(7680, 4320));
    CHECK(pictureSizeAllowed(16, 16));
    CHECK(pictureSizeAllowed(8192, 4608));
    CHECK(!pictureSizeAllowed(8192, 8192));
    CHECK(!pictureSizeAllowed(15, 1080));
    CHECK(!pictureSizeAllowed(1920, 15));
    CHECK(!pictureSizeAllowed(8193, 100));
    CHECK(!pictureSizeAllowed(0, 0));
    CHECK(!pictureSizeAllowed(-1, 1080));
    CHECK(!pictureSizeAllowed(65535, 65535));
}

void feed(LagTracker& tracker, double wallStart, double streamStart, double wallRate, double seconds, double step, bool& resynced) {
    for (double t = 0.0; t <= seconds + 1e-9; t += step) {
        if (tracker.observe(wallStart + t * wallRate, streamStart + t)) {
            resynced = true;
        }
    }
}

void testLagSteady() {
    LagTracker tracker(1.0, 0.5);
    bool resynced = false;
    feed(tracker, 100.0, 5.0, 1.0, 120.0, 1.0 / 30.0, resynced);
    CHECK(!resynced);
    CHECK(tracker.lagSeconds() < 0.01);
    CHECK(tracker.resyncs() == 0u);
}

void testLagBurstRecovers() {
    LagTracker tracker(1.0, 0.5);
    bool resynced = false;
    double wall = 100.0;
    double stream = 0.0;
    for (int i = 0; i < 90; ++i) {
        resynced = tracker.observe(wall, stream) || resynced;
        wall += 1.0 / 30.0;
        stream += 1.0 / 30.0;
    }
    wall += 0.8;
    for (int i = 0; i < 60; ++i) {
        resynced = tracker.observe(wall, stream) || resynced;
        stream += 1.0 / 30.0;
        wall += 1.0 / 120.0;
    }
    CHECK(!resynced);
    for (int i = 0; i < 90; ++i) {
        resynced = tracker.observe(wall, stream) || resynced;
        wall += 1.0 / 30.0;
        stream += 1.0 / 30.0;
    }
    CHECK(!resynced);
    CHECK(tracker.resyncs() == 0u);
}

void testLagSlowDecoderResyncs() {
    LagTracker tracker(1.0, 0.5);
    bool resynced = false;
    double firstResync = -1.0;
    const double wallRate = 1.25;
    for (double t = 0.0; t < 60.0; t += 1.0 / 30.0) {
        if (tracker.observe(100.0 + t * wallRate, t)) {
            resynced = true;
            if (firstResync < 0.0) {
                firstResync = t * wallRate;
            }
        }
    }
    CHECK(resynced);
    CHECK(firstResync > 4.0 && firstResync < 6.5);
    CHECK(tracker.resyncs() >= 10u);
    CHECK(tracker.resyncs() <= 15u);
}

void testLagDriftTolerated() {
    for (double drift : {0.0005, -0.0005, 0.0002, -0.0002}) {
        LagTracker tracker(1.0, 0.5);
        bool resynced = false;
        feed(tracker, 100.0, 0.0, 1.0 + drift, 3600.0, 1.0 / 25.0, resynced);
        CHECK(!resynced);
    }
}

void testLagJumpResets() {
    LagTracker tracker(1.0, 0.5);
    bool resynced = false;
    feed(tracker, 100.0, 0.0, 1.0, 10.0, 1.0 / 30.0, resynced);
    feed(tracker, 110.0, 5000.0, 1.0, 10.0, 1.0 / 30.0, resynced);
    feed(tracker, 120.0, 3.0, 1.0, 10.0, 1.0 / 30.0, resynced);
    CHECK(!resynced);
    CHECK(tracker.lagSeconds() < 0.05);
    tracker.observe(50.0, 3.0);
    CHECK(tracker.lagSeconds() < 0.05);
}

void testLagLimitScales() {
    LagTracker tight(0.4, 0.2);
    LagTracker loose(2.0, 0.2);
    bool tightResync = false;
    bool looseResync = false;
    for (double t = 0.0; t < 3.0; t += 1.0 / 30.0) {
        tightResync = tight.observe(100.0 + t * 1.3, t) || tightResync;
        looseResync = loose.observe(100.0 + t * 1.3, t) || looseResync;
    }
    CHECK(tightResync);
    CHECK(!looseResync);
}

void testJitter() {
    JitterMeter steady;
    for (int i = 0; i < 300; ++i) {
        steady.observe(10.0 + i / 30.0, i / 30.0);
    }
    CHECK(steady.jitterSeconds() < 1e-6);

    JitterMeter rough;
    unsigned seed = 7;
    double wall = 10.0;
    for (int i = 0; i < 600; ++i) {
        seed = seed * 1103515245u + 12345u;
        const double noise = ((seed >> 16) % 1000) / 1000.0 * 0.010;
        wall = 10.0 + i / 30.0 + noise;
        rough.observe(wall, i / 30.0);
    }
    CHECK(rough.jitterSeconds() > 0.002 && rough.jitterSeconds() < 0.012);

    JitterMeter jumpy;
    jumpy.observe(1.0, 0.0);
    jumpy.observe(1.03, 9999.0);
    CHECK(jumpy.jitterSeconds() == 0.0);
    jumpy.reset();
    CHECK(jumpy.jitterSeconds() == 0.0);
}

}

void setDataDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void testSettings() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_stream_settings";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    setDataDirectory(dir);

    nvj::Settings defaults;
    CHECK(!defaults.streamInputEnabled);
    CHECK(defaults.streamProfile == 1);
    for (const std::string& address : defaults.streamInputs) {
        CHECK(address.empty());
    }
    CHECK(nvj::saveSettings(defaults));
    const nlohmann::json plain = nlohmann::json::parse(readText(dir / "settings.json"));
    for (const char* key : {"streamInputEnabled", "streamInputs", "streamProfile"}) {
        CHECK(!plain.contains(key));
    }

    nvj::Settings changed;
    changed.streamInputEnabled = true;
    changed.streamProfile = 2;
    changed.streamInputs[0] = "srt://host:9000?mode=caller";
    changed.streamInputs[2] = "rtsp://user:secret@192.168.1.20/live";
    CHECK(nvj::saveSettings(changed));
    const std::string file = readText(dir / "settings.json");
    CHECK(file.find("rtsp://user:secret@192.168.1.20/live") != std::string::npos);
    nvj::Settings loaded;
    CHECK(nvj::loadSettings(loaded));
    CHECK(loaded.streamInputEnabled);
    CHECK(loaded.streamProfile == 2);
    CHECK(loaded.streamInputs[0] == "srt://host:9000?mode=caller");
    CHECK(loaded.streamInputs[1].empty());
    CHECK(loaded.streamInputs[2] == "rtsp://user:secret@192.168.1.20/live");
    CHECK(loaded.streamInputs[3].empty());

    writeText(dir / "settings.json", R"({"streamInputEnabled":true,"streamProfile":7,"streamInputs":["udp://@:5000",5,null,"srt://h:1"]})");
    nvj::Settings repaired;
    CHECK(nvj::loadSettings(repaired));
    CHECK(repaired.streamInputEnabled);
    CHECK(repaired.streamProfile == 1);
    CHECK(repaired.streamInputs[0] == "udp://@:5000");
    CHECK(repaired.streamInputs[1].empty());
    CHECK(repaired.streamInputs[2].empty());
    CHECK(repaired.streamInputs[3] == "srt://h:1");

    writeText(dir / "settings.json", R"({"streamInputEnabled":"yes","streamProfile":"low","streamInputs":"udp://@:5000"})");
    nvj::Settings wrongTypes;
    CHECK(nvj::loadSettings(wrongTypes));
    CHECK(!wrongTypes.streamInputEnabled);
    CHECK(wrongTypes.streamProfile == 1);
    CHECK(wrongTypes.streamInputs[0].empty());

    const std::string longAddress = "udp://@:5000?" + std::string(1100, 'a');
    nlohmann::json tooLong;
    tooLong["streamInputs"] = nlohmann::json::array({longAddress, "udp://@:6000"});
    writeText(dir / "settings.json", tooLong.dump());
    nvj::Settings limited;
    CHECK(nvj::loadSettings(limited));
    CHECK(limited.streamInputs[0].empty());
    CHECK(limited.streamInputs[1] == "udp://@:6000");

    std::filesystem::remove_all(dir, ec);
}

void testStoredAddressIsChecked() {
    CHECK(refused("file:///etc/passwd"));
    CHECK(refused("srt://host:9000?mode=caller&unknown_option=1"));
    CHECK(accepted("udp://@192.168.1.5:5000"));
    const ParseResult named = parseUrl("udp://@192.168.1.5:5000");
    const OpenPlan plan = makeOpenPlan(named.url, Profile::Balanced);
    CHECK(plan.url.find("192.168.1.5:5000") != std::string::npos);
    CHECK(plan.url.find("0.0.0.0") == std::string::npos);
    const ParseResult open = parseUrl("udp://@:5000");
    CHECK(makeOpenPlan(open.url, Profile::Balanced).url.find("0.0.0.0:5000") != std::string::npos);
}

double secondsSince(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Gate {
    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    std::atomic<bool> ended{false};
};

void waitFor(const std::atomic<bool>& flag, double seconds) {
    const auto begin = std::chrono::steady_clock::now();
    while (!flag.load() && secondsSince(begin) < seconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void testStoppableThread() {
    nvj::StoppableThread idle;
    CHECK(!idle.running());
    CHECK(idle.stop(0.1));

    const std::shared_ptr<Gate> polite = std::make_shared<Gate>();
    nvj::StoppableThread worker;
    worker.start([polite]() {
        polite->started = true;
        while (!polite->release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        polite->ended = true;
    });
    CHECK(worker.running());
    waitFor(polite->started, 2.0);
    polite->release = true;
    CHECK(worker.stop(2.0));
    CHECK(polite->ended.load());
    CHECK(!worker.running());

    const std::shared_ptr<Gate> stuck = std::make_shared<Gate>();
    worker.start([stuck]() {
        stuck->started = true;
        while (!stuck->release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        stuck->ended = true;
    });
    waitFor(stuck->started, 2.0);
    const auto begin = std::chrono::steady_clock::now();
    CHECK(!worker.stop(0.3));
    const double waited = secondsSince(begin);
    CHECK(waited >= 0.25);
    CHECK(waited < 2.0);
    CHECK(!worker.running());
    CHECK(!stuck->ended.load());

    const std::shared_ptr<Gate> next = std::make_shared<Gate>();
    worker.start([next]() {
        next->started = true;
        next->ended = true;
    });
    CHECK(worker.stop(2.0));
    CHECK(next->ended.load());
    CHECK(!stuck->ended.load());

    stuck->release = true;
    waitFor(stuck->ended, 2.0);
    CHECK(stuck->ended.load());

    const std::shared_ptr<Gate> quick = std::make_shared<Gate>();
    worker.start([quick]() { quick->ended = true; });
    waitFor(quick->ended, 2.0);
    const auto instant = std::chrono::steady_clock::now();
    CHECK(worker.stop(5.0));
    CHECK(secondsSince(instant) < 1.0);
}

int main() {
    testAllowedAddresses();
    testRefusedAddresses();
    testFields();
    testMasking();
    testPlans();
    testProfiles();
    testLimits();
    testLagSteady();
    testLagBurstRecovers();
    testLagSlowDecoderResyncs();
    testLagDriftTolerated();
    testLagJumpResets();
    testLagLimitScales();
    testJitter();
    testSettings();
    testStoredAddressIsChecked();
    testStoppableThread();
    return testing::finish();
}

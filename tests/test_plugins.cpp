#include "check.hpp"

#include "core/crash_handler.hpp"
#include "core/log.hpp"
#include "core/plugin_config.hpp"
#include "core/plugin_host.hpp"
#include "core/plugin_manifest.hpp"
#include "core/recovery.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define NVJ_DUP _dup
#define NVJ_DUP2 _dup2
#define NVJ_CLOSE _close
#define NVJ_FILENO _fileno
#else
#include <unistd.h>
#define NVJ_DUP dup
#define NVJ_DUP2 dup2
#define NVJ_CLOSE close
#define NVJ_FILENO fileno
#endif

namespace {

using namespace nvj;
namespace fs = std::filesystem;

std::string gSelf;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void pause(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("nvj_plugin_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

const char* libraryFileName() {
#ifdef _WIN32
    return "plugin.dll";
#else
    return "plugin.so";
#endif
}

std::string manifestText(const std::string& id, unsigned major, unsigned minor) {
    return std::string("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"") + id + "\",\"name\":\"Test " + id + "\",\"pluginVersion\":\"1.0.0\",\"api\":{\"major\":" + std::to_string(major) + ",\"minor\":" + std::to_string(minor) + "},\"library\":{\"linux-x64\":\"plugin.so\",\"windows-x64\":\"plugin.dll\"}}";
}

void writeText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

fs::path installPlugin(const fs::path& dataDir, const std::string& folder, const std::string& library, const std::string& id, unsigned major = 1, unsigned minor = 0) {
    const fs::path dir = pluginsFolder(dataDir) / folder;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::copy_file(library, dir / libraryFileName(), fs::copy_options::overwrite_existing, ec);
    writeText(dir / "plugin.json", manifestText(id, major, minor));
    return dir;
}

void enablePlugins(const fs::path& dataDir, const std::vector<std::string>& ids) {
    PluginConfig config = readPluginConfig(dataDir);
    for (const std::string& id : ids) {
        config.setEnabled(id, true);
    }
    writePluginConfig(dataDir, config);
}

bool waitFor(PluginHost& host, const std::function<bool()>& done, double seconds) {
    const double end = now() + seconds;
    while (now() < end) {
        host.update();
        if (done()) {
            return true;
        }
        pause(2);
    }
    host.update();
    return done();
}

PluginState stateOf(const PluginHost& host, const std::string& id) {
    const PluginInfo* info = host.find(id);
    return info != nullptr ? info->state : PluginState::Incompatible;
}

std::string messageOf(const PluginHost& host, const std::string& id) {
    const PluginInfo* info = host.find(id);
    return info != nullptr ? info->message : std::string();
}

bool settled(const PluginHost& host, const std::string& id) {
    const PluginState state = stateOf(host, id);
    return state != PluginState::Loading && state != PluginState::NotEnabled;
}

struct LoadResult {
    PluginState state = PluginState::NotEnabled;
    std::string message;
};

LoadResult loadBroken(const std::string& name, const std::string& library) {
    const fs::path dir = freshDir("load_" + name);
    installPlugin(dir, "broken", library, "broken");
    enablePlugins(dir, {"broken"});
    PluginHost host(dir);
    waitFor(host, [&]() { return settled(host, "broken"); }, 3.0);
    LoadResult result;
    result.state = stateOf(host, "broken");
    result.message = messageOf(host, "broken");
    return result;
}

struct StderrCapture {
    int saved = -1;
    std::FILE* file = nullptr;
    fs::path path;

    explicit StderrCapture(const fs::path& target) : path(target) {
        std::fflush(stderr);
        saved = NVJ_DUP(NVJ_FILENO(stderr));
        file = std::fopen(path.string().c_str(), "wb");
        if (file != nullptr) {
            NVJ_DUP2(NVJ_FILENO(file), NVJ_FILENO(stderr));
        }
    }

    std::string finish() {
        std::fflush(stderr);
        if (saved >= 0) {
            NVJ_DUP2(saved, NVJ_FILENO(stderr));
            NVJ_CLOSE(saved);
            saved = -1;
        }
        if (file != nullptr) {
            std::fclose(file);
            file = nullptr;
        }
        std::string text;
        readWholeFile(path, text);
        return text;
    }
};

size_t countLines(const std::string& text, const std::string& needle) {
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

void testManifest() {
    const fs::path dir = freshDir("manifest");
    const fs::path base = pluginsFolder(dir);
    writeText(base / "good" / "plugin.json", manifestText("good", 1, 0));
    writeText(base / "good" / "plugin.so", "x");
    writeText(base / "good" / "plugin.dll", "x");
    writeText(base / "wrongmajor" / "plugin.json", manifestText("wrongmajor", 2, 0));
    writeText(base / "wrongmajor" / "plugin.so", "x");
    writeText(base / "wrongmajor" / "plugin.dll", "x");
    writeText(base / "newerminor" / "plugin.json", manifestText("newerminor", 1, 1));
    writeText(base / "newerminor" / "plugin.so", "x");
    writeText(base / "newerminor" / "plugin.dll", "x");
    writeText(base / "nolibrary" / "plugin.json", manifestText("nolibrary", 1, 0));
    writeText(base / "badid" / "plugin.json", manifestText("Bad Id", 1, 0));
    writeText(base / "notjson" / "plugin.json", "this is not json");
    writeText(base / "empty" / "readme.txt", "no manifest here");

    const std::vector<PluginEntry> entries = scanPlugins(base);
    CHECK(entries.size() == 6);
    auto find = [&](const std::string& folder) -> const PluginEntry* {
        for (const PluginEntry& entry : entries) {
            if (entry.directory.filename().string() == folder) {
                return &entry;
            }
        }
        return nullptr;
    };
    CHECK(find("good") != nullptr && find("good")->usable());
    CHECK(find("good") != nullptr && find("good")->manifest.id == "good");
    CHECK(find("good") != nullptr && find("good")->manifest.name == "Test good");
    CHECK(find("good") != nullptr && find("good")->manifest.version == "1.0.0");
    CHECK(find("wrongmajor") != nullptr && find("wrongmajor")->problem == PluginProblem::WrongMajor);
    CHECK(find("newerminor") != nullptr && find("newerminor")->problem == PluginProblem::NewerMinor);
    CHECK(find("nolibrary") != nullptr && find("nolibrary")->problem == PluginProblem::MissingLibrary);
    CHECK(find("badid") != nullptr && find("badid")->problem == PluginProblem::BadId);
    CHECK(find("notjson") != nullptr && find("notjson")->problem == PluginProblem::NotJson);
    CHECK(find("empty") == nullptr);

    const std::vector<PluginEntry> newerHost = scanPlugins(base, 1, 3);
    for (const PluginEntry& entry : newerHost) {
        if (entry.directory.filename() == "newerminor") {
            CHECK(entry.usable());
        }
        if (entry.directory.filename() == "wrongmajor") {
            CHECK(entry.problem == PluginProblem::WrongMajor);
        }
    }
    const std::vector<PluginEntry> olderPlugin = scanPlugins(base, 1, 0);
    CHECK(olderPlugin.size() == 6);

    PluginEntry slash = parseManifest("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"x\",\"name\":\"X\",\"api\":{\"major\":1,\"minor\":0},\"library\":{\"linux-x64\":\"../evil.so\",\"windows-x64\":\"..\\\\evil.dll\"}}", base / "x", 1, 0, "linux-x64");
    CHECK(slash.problem == PluginProblem::BadLibraryName);
    PluginEntry newer = parseManifest("{\"format\":\"nyrexvj-plugin\",\"version\":2,\"id\":\"x\",\"name\":\"X\"}", base / "x", 1, 0, "linux-x64");
    CHECK(newer.problem == PluginProblem::NewerFormat);
    PluginEntry otherSystem = parseManifest("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"x\",\"name\":\"X\",\"api\":{\"major\":1,\"minor\":0},\"library\":{\"macos-arm64\":\"x.dylib\"}}", base / "x", 1, 0, "linux-x64");
    CHECK(otherSystem.problem == PluginProblem::NoLibrary);
    PluginEntry big = parseManifest(std::string(kMaxManifestBytes + 10, ' '), base / "x", 1, 0, "linux-x64");
    CHECK(big.problem == PluginProblem::TooLarge);

    CHECK(validPluginId("my-plugin_1.2"));
    CHECK(!validPluginId(""));
    CHECK(!validPluginId("-start"));
    CHECK(!validPluginId("Upper"));
    CHECK(!validPluginId("a/b"));
    CHECK(!validPluginId(std::string(kMaxPluginIdLength + 1, 'a')));

    std::string example;
    if (readWholeFile(NVJ_PLUGIN_EXAMPLE_MANIFEST, example)) {
        const PluginEntry shipped = parseManifest(example, fs::path(NVJ_PLUGIN_EXAMPLE_MANIFEST).parent_path(), 1, 0, "linux-x64");
        CHECK(shipped.manifest.id == "nyrexvj-example");
        CHECK(shipped.manifest.apiMajor == 1 && shipped.manifest.apiMinor == 0);
        CHECK(shipped.manifest.library == "nyrexvj_example.so");
    } else {
        CHECK(false);
    }
}

void testConfigAndSafeList() {
    PluginConfig config;
    config.setEnabled("a", true);
    config.setEnabled("b", true);
    config.setEnabled("a", true);
    config.setEnabled("c", false);
    CHECK(config.enabled.size() == 2);
    CHECK(config.isEnabled("a") && config.isEnabled("b") && !config.isEnabled("c"));
    config.setEnabled("a", false);
    CHECK(!config.isEnabled("a"));
    config.modulation[0] = "b/pulse";
    config.modulation[7] = "b/other";
    config.frames[3] = "b/plasma";
    config.parameters["b/pulse"] = {0.1f, 0.2f, 0.3f, 0.4f};
    const PluginConfig round = pluginConfigFromJson(pluginConfigToJson(config));
    CHECK(round.enabled == config.enabled);
    CHECK(round.modulation == config.modulation);
    CHECK(round.frames == config.frames);
    CHECK(round.parameters.size() == 1);
    CHECK(std::fabs(round.parametersFor("b/pulse")[2] - 0.3f) < 1.0e-6f);
    CHECK(std::fabs(round.parametersFor("missing/key")[0] - 0.5f) < 1.0e-6f);

    CHECK(pluginConfigFromJson("garbage").enabled.empty());
    CHECK(pluginConfigFromJson("{\"format\":\"other\",\"version\":1,\"enabled\":[\"a\"]}").enabled.empty());
    CHECK(pluginConfigFromJson("{\"format\":\"nyrexvj-plugin-settings\",\"version\":2,\"enabled\":[\"a\"]}").enabled.empty());
    const PluginConfig messy = pluginConfigFromJson("{\"format\":\"nyrexvj-plugin-settings\",\"version\":1,\"enabled\":[\"ok\",\"../bad\",5,\"Also Bad\"],\"modulation\":[\"a/b\",\"nope\",\"../x/y\"],\"frames\":[3],\"parameters\":{\"a/b\":[2.5,-1,\"x\",0.25],\"bad key\":[0.1]}}");
    CHECK(messy.enabled.size() == 1 && messy.enabled[0] == "ok");
    CHECK(messy.modulation[0] == "a/b" && messy.modulation[1].empty() && messy.modulation[2].empty());
    CHECK(messy.frames[0].empty());
    CHECK(messy.parameters.size() == 1);
    const std::array<float, kPluginParameters> clamped = messy.parametersFor("a/b");
    CHECK(clamped[0] == 1.0f && clamped[1] == 0.0f && std::fabs(clamped[2] - 0.5f) < 1.0e-6f && std::fabs(clamped[3] - 0.25f) < 1.0e-6f);

    std::string key = pluginSourceKey("plug", "src");
    std::string pluginId;
    std::string sourceId;
    CHECK(key == "plug/src");
    CHECK(splitPluginSourceKey(key, pluginId, sourceId) && pluginId == "plug" && sourceId == "src");
    CHECK(!splitPluginSourceKey("nosplit", pluginId, sourceId));
    CHECK(!splitPluginSourceKey("/src", pluginId, sourceId));
    CHECK(!splitPluginSourceKey("plug/", pluginId, sourceId));
    CHECK(!splitPluginSourceKey("Plug/src", pluginId, sourceId));

    const fs::path dir = freshDir("config");
    CHECK(readPluginConfig(dir).enabled.empty());
    CHECK(writePluginConfig(dir, config));
    CHECK(readPluginConfig(dir).enabled == config.enabled);
    CHECK(readPluginSafeList(dir).empty());
    CHECK(addToPluginSafeList(dir, {"one", "two", "Bad Id", "one"}, "test reason", 42));
    std::vector<PluginSafeEntry> safe = readPluginSafeList(dir);
    CHECK(safe.size() == 2);
    CHECK(inPluginSafeList(safe, "one") && inPluginSafeList(safe, "two") && !inPluginSafeList(safe, "three"));
    CHECK(safe.size() == 2 && safe[0].reason == "test reason" && safe[0].time == 42);
    CHECK(removeFromPluginSafeList(dir, "one"));
    safe = readPluginSafeList(dir);
    CHECK(safe.size() == 1 && safe[0].id == "two");
    CHECK(removeFromPluginSafeList(dir, "never-there"));
    CHECK(pluginSafeListFromJson("garbage").empty());
    CHECK(pluginSafeListFromJson("{\"format\":\"x\",\"disabled\":[{\"id\":\"a\"}]}").empty());
}

void testCrashMarkers() {
    const int first = crashMarkerAcquire("alpha");
    const int second = crashMarkerAcquire("beta");
    CHECK(first >= 0 && second >= 0 && first != second);
    CHECK(crashMarkerActive().empty());
    crashMarkerEnter(first);
    std::vector<std::string> active = crashMarkerActive();
    CHECK(active.size() == 1 && active[0] == "alpha");
    crashMarkerEnter(second);
    CHECK(crashMarkerActive().size() == 2);
    crashMarkerLeave(first);
    active = crashMarkerActive();
    CHECK(active.size() == 1 && active[0] == "beta");
    crashMarkerLeave(second);
    CHECK(crashMarkerActive().empty());
    crashMarkerEnter(-1);
    crashMarkerLeave(-1);
    crashMarkerEnter(kCrashMarkerSlots);
    crashMarkerRelease(-1);
    crashMarkerRelease(first);
    crashMarkerRelease(second);
    const int reused = crashMarkerAcquire(std::string(200, 'x'));
    CHECK(reused >= 0);
    crashMarkerEnter(reused);
    active = crashMarkerActive();
    CHECK(active.size() == 1 && active[0].size() == static_cast<size_t>(kCrashMarkerIdLength - 1));
    crashMarkerRelease(reused);
    CHECK(crashMarkerActive().empty());

    std::vector<int> slots;
    for (int i = 0; i < kCrashMarkerSlots + 4; ++i) {
        slots.push_back(crashMarkerAcquire("fill"));
    }
    const int taken = static_cast<int>(std::count_if(slots.begin(), slots.end(), [](int slot) { return slot >= 0; }));
    CHECK(taken == kCrashMarkerSlots);
    for (int slot : slots) {
        crashMarkerRelease(slot);
    }
    CHECK(crashMarkerAcquire("again") >= 0);
    for (int i = 0; i < kCrashMarkerSlots; ++i) {
        crashMarkerRelease(i);
    }

    const std::vector<std::string> ids = pluginsInCrashReport("NyrexVJ 1\nreason: x\nplugin: alpha\r\nplugin: beta \nplugin: alpha\nplugin: \nplugins: no\n");
    CHECK(ids.size() == 2 && ids[0] == "alpha" && ids[1] == "beta");
    CHECK(pluginsInCrashReport("").empty());
    CHECK(pluginsInCrashReport("no plugin lines\n").empty());
}

void testExamplePlugin() {
    const fs::path dir = freshDir("example");
    installPlugin(dir, "example", NVJ_PLUGIN_EXAMPLE, "nyrexvj-example");
    PluginHost idle(dir);
    CHECK(idle.plugins().size() == 1);
    CHECK(stateOf(idle, "nyrexvj-example") == PluginState::NotEnabled);
    CHECK(idle.runningInstances() == 0 && !idle.loading());
    idle.update();
    pause(100);
    idle.update();
    CHECK(stateOf(idle, "nyrexvj-example") == PluginState::NotEnabled);

    enablePlugins(dir, {"nyrexvj-example"});
    PluginHost blocked(dir, false);
    pause(100);
    blocked.update();
    CHECK(stateOf(blocked, "nyrexvj-example") == PluginState::NotEnabled);
    CHECK(!blocked.allowLoading());
    blocked.setEnabled("nyrexvj-example", true);
    pause(100);
    blocked.update();
    CHECK(stateOf(blocked, "nyrexvj-example") == PluginState::NotEnabled);

    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "nyrexvj-example") == PluginState::Loaded; }, 3.0));
    const PluginInfo* info = host.find("nyrexvj-example");
    CHECK(info != nullptr);
    if (info == nullptr) {
        return;
    }
    CHECK(info->sources.size() == 2);
    if (info->sources.size() == 2) {
        CHECK(info->sources[0].id == "pulse" && info->sources[0].kind == kPluginSourceKindModulation && info->sources[0].parameterCount == 2);
        CHECK(info->sources[0].key == "nyrexvj-example/pulse");
        CHECK(info->sources[0].parameterNames[0] == "Rate" && info->sources[0].parameterNames[1] == "Kick mix");
        CHECK(info->sources[1].id == "plasma" && info->sources[1].kind == kPluginSourceKindFrame && info->sources[1].parameterCount == 3);
    }
    CHECK(host.findSource("nyrexvj-example/pulse") != nullptr);
    CHECK(host.findSource("nyrexvj-example/missing") == nullptr);
    CHECK(host.findSource("garbage") == nullptr);

    const std::string pulse = "nyrexvj-example/pulse";
    CHECK(host.modulation(0) == 0.0f);
    host.assignModulation(0, pulse);
    host.setParameter(pulse, 0, 0.8f);
    host.setParameter(pulse, 1, 0.0f);
    float low = 2.0f;
    float high = -1.0f;
    const double start = now();
    while (now() - start < 0.6) {
        host.update();
        const float value = host.modulation(0);
        if (now() - start > 0.15) {
            low = std::min(low, value);
            high = std::max(high, value);
        }
        pause(3);
    }
    CHECK(low >= 0.0f && high <= 1.0f);
    CHECK(high - low > 0.3f);
    PluginRunStats stats = host.modulationStats(0);
    CHECK(stats.running && !stats.failed);
    CHECK(stats.calls > 5);
    CHECK(stats.maxCallMs < kPluginModulationBudget * 1000.0);
    CHECK(host.runningInstances() == 1);

    host.setParameter(pulse, 1, 1.0f);
    PluginHostSnapshot snapshot;
    snapshot.kick = 0.8f;
    host.publish(snapshot);
    pause(150);
    for (int i = 0; i < 5; ++i) {
        host.update();
        CHECK(std::fabs(host.modulation(0) - 0.8f) < 0.01f);
        pause(20);
    }
    snapshot.kick = 0.1f;
    host.publish(snapshot);
    pause(150);
    CHECK(std::fabs(host.modulation(0) - 0.1f) < 0.01f);

    host.setParameter(pulse, 5, 0.5f);
    host.setParameter(pulse, -1, 0.5f);
    host.setParameter(pulse, 0, std::nanf(""));
    host.assignModulation(99, pulse);
    host.assignModulation(-1, pulse);
    CHECK(host.modulation(99) == 0.0f && host.modulation(-1) == 0.0f);
    CHECK(host.modulation(5) == 0.0f);

    std::string error;
    CHECK(host.openFrameSource("nyrexvj-example/pulse", error) == nullptr && !error.empty());
    CHECK(host.openFrameSource("nyrexvj-example/missing", error) == nullptr);
    CHECK(host.openFrameSource("", error) == nullptr);
    CHECK(host.openFrameSource("nobody/plasma", error) == nullptr);

    std::unique_ptr<LiveSource> source = host.openFrameSource("nyrexvj-example/plasma", error);
    CHECK(source != nullptr);
    if (!source) {
        return;
    }
    LiveFrame first;
    const double wait = now() + 3.0;
    bool got = false;
    while (now() < wait && !got) {
        host.update();
        got = source->take(first);
        pause(5);
    }
    CHECK(got);
    CHECK(first.format == LiveFormat::Rgba && first.width == kPluginFrameWidth && first.height == kPluginFrameHeight);
    CHECK(first.stride == kPluginFrameWidth * 4);
    CHECK(first.data.size() == static_cast<size_t>(kPluginFrameWidth) * kPluginFrameHeight * 4);
    if (first.data.size() == static_cast<size_t>(kPluginFrameWidth) * kPluginFrameHeight * 4) {
        bool opaque = true;
        bool varied = false;
        for (size_t i = 3; i < first.data.size(); i += 4 * 997) {
            opaque = opaque && first.data[i] == 255;
        }
        for (size_t i = 0; i < first.data.size() - 4; i += 4 * 1013) {
            varied = varied || first.data[i] != first.data[0] || first.data[i + 1] != first.data[1];
        }
        CHECK(opaque);
        CHECK(varied);
    }
    LiveFrame second;
    got = false;
    const double again = now() + 3.0;
    while (now() < again && !got) {
        host.update();
        got = source->take(second);
        pause(5);
    }
    CHECK(got && second.id > first.id);
    LiveStatus status = source->status();
    CHECK(status.state == LiveState::Live);
    CHECK(status.frames >= 2);
    const double settle = now() + 4.0;
    while (now() < settle && source->status().frames < 12) {
        host.update();
        pause(10);
    }
    status = source->status();
    CHECK(status.frames >= 12);
    CHECK(status.fps > 5.0 && status.fps < 60.0);
    CHECK(status.width == kPluginFrameWidth && status.height == kPluginFrameHeight);
    CHECK(contains(status.source, "Test nyrexvj-example") && contains(status.source, "Plasma"));
    CHECK(source->running());
    source->stop();
    CHECK(!source->running());
    source.reset();
    host.assignModulation(0, "");
    CHECK(host.modulation(0) == 0.0f);
    pause(100);
    host.update();
    CHECK(host.runningInstances() == 0);

    host.assignModulation(1, pulse);
    host.modulation(1);
    CHECK(waitFor(host, [&]() { return host.runningInstances() == 1; }, 1.0));
    const double idleStart = now();
    CHECK(waitFor(host, [&]() { return host.runningInstances() == 0; }, kPluginIdleSeconds + 2.0));
    CHECK(now() - idleStart > kPluginIdleSeconds - 0.3);
    host.modulation(1);
    CHECK(waitFor(host, [&]() { return host.runningInstances() == 1; }, 1.0));

    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
    CHECK(host.abandonedInstances() == 0);
    CHECK(stateOf(host, "nyrexvj-example") == PluginState::Loaded);

    host.setEnabled("nyrexvj-example", false);
    CHECK(stateOf(host, "nyrexvj-example") == PluginState::NotEnabled);
    CHECK(!readPluginConfig(dir).isEnabled("nyrexvj-example"));
    host.setEnabled("nyrexvj-example", true);
    CHECK(waitFor(host, [&]() { return stateOf(host, "nyrexvj-example") == PluginState::Loaded; }, 3.0));
    CHECK(readPluginConfig(dir).isEnabled("nyrexvj-example"));
    CHECK(readPluginConfig(dir).modulation[1] == pulse);
}

void testCompatibilityBeforeCode() {
    const fs::path dir = freshDir("compat");
    installPlugin(dir, "major", NVJ_PLUGIN_CRASHLOAD, "major-two", 2, 0);
    installPlugin(dir, "minor", NVJ_PLUGIN_CRASHLOAD, "minor-newer", 1, 1);
    installPlugin(dir, "fine", NVJ_PLUGIN_EXAMPLE, "fine-one", 1, 0);
    enablePlugins(dir, {"major-two", "minor-newer"});
    PluginHost host(dir);
    pause(150);
    host.update();
    CHECK(stateOf(host, "major-two") == PluginState::Incompatible);
    CHECK(stateOf(host, "minor-newer") == PluginState::Incompatible);
    CHECK(contains(messageOf(host, "major-two"), "major"));
    CHECK(contains(messageOf(host, "minor-newer"), "newer"));
    CHECK(!host.loading());
    host.setEnabled("major-two", true);
    pause(100);
    host.update();
    CHECK(stateOf(host, "major-two") == PluginState::Incompatible);
    CHECK(stateOf(host, "fine-one") == PluginState::NotEnabled);
    CHECK(pluginThreadsAlive() == 0);
}

void testBrokenLoads() {
    struct Case {
        const char* name;
        const char* library;
        const char* message;
    };
    const Case cases[] = {
        {"mismatch", NVJ_PLUGIN_MISMATCH, "reports API 2.0"},
        {"idmismatch", NVJ_PLUGIN_IDMISMATCH, "id differs"},
        {"entryfail", NVJ_PLUGIN_ENTRYFAIL, "entry function returned an error"},
        {"noentry", NVJ_PLUGIN_NOENTRY, "no nyrexvj_plugin_entry"},
        {"duplicate", NVJ_PLUGIN_DUPLICATE, "two sources"},
        {"toomany", NVJ_PLUGIN_TOOMANY, "9 sources"},
        {"nomodulation", NVJ_PLUGIN_NOMODULATION, "no modulation function"},
    };
    for (const Case& item : cases) {
        const LoadResult result = loadBroken(item.name, item.library);
        CHECK(result.state == PluginState::Failed);
        CHECK(contains(result.message, item.message));
    }

    const fs::path dir = freshDir("notalibrary");
    const fs::path folder = pluginsFolder(dir) / "garbage";
    writeText(folder / "plugin.json", manifestText("garbage", 1, 0));
    writeText(folder / "plugin.so", "this is not a shared library");
    writeText(folder / "plugin.dll", "this is not a shared library");
    enablePlugins(dir, {"garbage"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return settled(host, "garbage"); }, 3.0));
    CHECK(stateOf(host, "garbage") == PluginState::Failed);
    CHECK(contains(messageOf(host, "garbage"), "could not be loaded"));
}

void testSlowAndFailing() {
    const fs::path dir = freshDir("runtime");
    installPlugin(dir, "broken", NVJ_PLUGIN_SLOW, "broken");
    installPlugin(dir, "good", NVJ_PLUGIN_EXAMPLE, "nyrexvj-example");
    enablePlugins(dir, {"broken", "nyrexvj-example"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return settled(host, "broken") && settled(host, "nyrexvj-example"); }, 3.0));
    CHECK(stateOf(host, "broken") == PluginState::Loaded);
    host.assignModulation(0, "broken/mod");
    host.assignModulation(1, "nyrexvj-example/pulse");
    host.setParameter("nyrexvj-example/pulse", 1, 0.0f);
    const double start = now();
    float lowest = 2.0f;
    float highest = -1.0f;
    while (now() - start < 2.0 && stateOf(host, "broken") != PluginState::Failed) {
        host.update();
        host.modulation(0);
        const float value = host.modulation(1);
        if (now() - start > 0.3) {
            lowest = std::min(lowest, value);
            highest = std::max(highest, value);
        }
        pause(3);
    }
    CHECK(stateOf(host, "broken") == PluginState::Failed);
    CHECK(contains(messageOf(host, "broken"), "time budget"));
    CHECK(host.modulation(0) == 0.0f);
    for (int i = 0; i < 30; ++i) {
        host.update();
        const float value = host.modulation(1);
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
        pause(10);
    }
    CHECK(stateOf(host, "nyrexvj-example") == PluginState::Loaded);
    CHECK(highest - lowest > 0.1f);
    CHECK(!host.modulationStats(1).failed);
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testFailingCalls() {
    {
        const fs::path dir = freshDir("fail");
        installPlugin(dir, "broken", NVJ_PLUGIN_FAIL, "broken");
        enablePlugins(dir, {"broken"});
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        host.assignModulation(0, "broken/mod");
        CHECK(waitFor(host, [&]() { host.modulation(0); return stateOf(host, "broken") == PluginState::Failed; }, 3.0));
        CHECK(contains(messageOf(host, "broken"), "returned an error"));
        CHECK(host.modulation(0) == 0.0f);
        CHECK(host.runningInstances() == 0);
        std::string error;
        CHECK(host.openFrameSource("broken/img", error) == nullptr);
        CHECK(!error.empty());
        host.setEnabled("broken", true);
        pause(100);
        host.update();
        CHECK(stateOf(host, "broken") == PluginState::Failed);
        CHECK(host.waitForThreads(3.0) == 0);
    }
    {
        const fs::path dir = freshDir("createfail");
        installPlugin(dir, "broken", NVJ_PLUGIN_CREATEFAIL, "broken");
        enablePlugins(dir, {"broken"});
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        host.assignModulation(0, "broken/mod");
        CHECK(waitFor(host, [&]() { host.modulation(0); return stateOf(host, "broken") == PluginState::Failed; }, 3.0));
        CHECK(contains(messageOf(host, "broken"), "create returned an error"));
        CHECK(host.waitForThreads(3.0) == 0);
    }
    {
        const fs::path dir = freshDir("range");
        installPlugin(dir, "broken", NVJ_PLUGIN_RANGE, "broken");
        enablePlugins(dir, {"broken"});
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        host.assignModulation(0, "broken/mod");
        bool inRange = true;
        float last = -1.0f;
        const double start = now();
        while (now() - start < 0.8) {
            host.update();
            last = host.modulation(0);
            inRange = inRange && std::isfinite(last) && last >= 0.0f && last <= 1.0f;
            pause(3);
        }
        CHECK(inRange);
        CHECK(std::fabs(last - 0.25f) < 1.0e-6f);
        CHECK(stateOf(host, "broken") == PluginState::Loaded);
        CHECK(!host.modulationStats(0).failed);
    }
}

void testFrameSourceGuards() {
    struct Case {
        const char* name;
        const char* library;
        const char* message;
    };
    const Case cases[] = {
        {"overflow", NVJ_PLUGIN_OVERFLOW, "outside of the image"},
        {"imagechanged", NVJ_PLUGIN_IMAGECHANGED, "changed the image description"},
        {"slowframe", NVJ_PLUGIN_SLOWFRAME, "time budget"},
    };
    for (const Case& item : cases) {
        const fs::path dir = freshDir(std::string("frame_") + item.name);
        installPlugin(dir, "broken", item.library, "broken");
        enablePlugins(dir, {"broken"});
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        std::string error;
        std::unique_ptr<LiveSource> source = host.openFrameSource("broken/img", error);
        CHECK(source != nullptr);
        if (!source) {
            continue;
        }
        CHECK(waitFor(host, [&]() { return !source->running(); }, 3.0));
        host.update();
        const LiveStatus status = source->status();
        CHECK(status.state == LiveState::SignalLost);
        CHECK(contains(status.error, item.message));
        LiveFrame frame;
        CHECK(!source->take(frame));
        CHECK(stateOf(host, "broken") == PluginState::Failed);
        source.reset();
        host.update();
        CHECK(host.waitForThreads(3.0) == 0);
    }
}

void testHangStaysResponsive() {
    const fs::path dir = freshDir("hang");
    installPlugin(dir, "broken", NVJ_PLUGIN_HANG, "broken");
    enablePlugins(dir, {"broken"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
    host.assignModulation(0, "broken/mod");
    double longest = 0.0;
    const double start = now();
    while (now() - start < 1.0) {
        const double begin = now();
        host.update();
        host.modulation(0);
        longest = std::max(longest, now() - begin);
        pause(2);
    }
    CHECK(longest < 0.1);
    CHECK(stateOf(host, "broken") == PluginState::Failed);
    CHECK(contains(messageOf(host, "broken"), "time budget"));
    CHECK(host.abandonedInstances() == 1);
    CHECK(host.modulation(0) == 0.0f);
    CHECK(host.runningInstances() == 0);
    const double stopBegin = now();
    host.stopAll();
    CHECK(now() - stopBegin < 0.1);
    CHECK(pluginThreadsAlive() >= 1);
}

void testSlowLoadStaysResponsive() {
    const fs::path dir = freshDir("loadslow");
    installPlugin(dir, "broken", NVJ_PLUGIN_LOADSLOW, "broken");
    enablePlugins(dir, {"broken"});
    double longest = 0.0;
    const double begin = now();
    PluginHost host(dir);
    longest = std::max(longest, now() - begin);
    CHECK(stateOf(host, "broken") == PluginState::Loading);
    CHECK(host.loading());
    const double start = now();
    while (now() - start < kPluginLoadBudget + 1.0 && stateOf(host, "broken") == PluginState::Loading) {
        const double tick = now();
        host.update();
        longest = std::max(longest, now() - tick);
        pause(5);
    }
    CHECK(longest < 0.1);
    CHECK(stateOf(host, "broken") == PluginState::Failed);
    CHECK(contains(messageOf(host, "broken"), "did not finish"));
    CHECK(!host.loading());
    CHECK(host.abandonedInstances() == 1);
}

void testLogLimit() {
    const fs::path dir = freshDir("logspam");
    installPlugin(dir, "broken", NVJ_PLUGIN_LOGSPAM, "broken");
    enablePlugins(dir, {"broken"});
    StderrCapture capture(dir / "stderr.txt");
    {
        PluginHost host(dir);
        waitFor(host, [&]() { return settled(host, "broken"); }, 3.0);
        host.assignModulation(0, "broken/mod");
        const double start = now();
        while (now() - start < 1.2) {
            host.update();
            host.modulation(0);
            pause(3);
        }
        host.stopAll();
        host.waitForThreads(2.0);
    }
    const std::string output = capture.finish();
    const size_t lines = countLines(output, "plugin broken info: spam");
    CHECK(lines >= 10);
    CHECK(lines <= 2 * static_cast<size_t>(kPluginLogsPerSecond) + 5);
}

int crashChild(const std::string& dataDir) {
    const fs::path dir(dataDir);
    std::error_code ec;
    fs::create_directories(recoveryDirectory(dir), ec);
    installCrashHandler(crashReportPath(dir).string(), "test");
    PluginHost host(dir);
    waitFor(host, [&]() { return settled(host, "broken"); }, 3.0);
    host.assignModulation(0, "broken/mod");
    const double start = now();
    while (now() - start < 4.0) {
        host.update();
        host.modulation(0);
        pause(3);
    }
    return 3;
}

int runChild(const fs::path& dir) {
    std::string command = "\"" + gSelf + "\" --crash-child \"" + dir.string() + "\"";
#ifdef _WIN32
    command = "\"" + command + "\"";
#endif
    return std::system(command.c_str());
}

void testCrashLeadsToSafeMode() {
    struct Case {
        const char* name;
        const char* library;
    };
    const Case cases[] = {
        {"crash", NVJ_PLUGIN_CRASH},
        {"crashcreate", NVJ_PLUGIN_CRASHCREATE},
        {"crashload", NVJ_PLUGIN_CRASHLOAD},
    };
    for (const Case& item : cases) {
        const fs::path dir = freshDir(std::string("child_") + item.name);
        installPlugin(dir, "broken", item.library, "broken");
        enablePlugins(dir, {"broken"});
        const int code = runChild(dir);
        CHECK(code != 0);
        std::string report;
        CHECK(readWholeFile(crashReportPath(dir), report));
        const std::vector<std::string> ids = pluginsInCrashReport(report);
        CHECK(ids.size() == 1 && ids[0] == "broken");

        CHECK(readPluginSafeList(dir).empty());
        const std::vector<std::string> recorded = recordPluginCrashes(dir, report, 1234);
        CHECK(recorded.size() == 1 && recorded[0] == "broken");
        const std::vector<PluginSafeEntry> safe = readPluginSafeList(dir);
        CHECK(safe.size() == 1 && safe[0].id == "broken" && safe[0].time == 1234);

        const int threadsBefore = pluginThreadsAlive();
        PluginHost host(dir);
        CHECK(readPluginConfig(dir).isEnabled("broken"));
        CHECK(stateOf(host, "broken") == PluginState::SafeMode);
        CHECK(!host.loading());
        host.assignModulation(0, "broken/mod");
        pause(150);
        host.update();
        CHECK(host.modulation(0) == 0.0f);
        CHECK(host.runningInstances() == 0);
        CHECK(pluginThreadsAlive() == threadsBefore);
        host.setEnabled("broken", true);
        pause(100);
        host.update();
        CHECK(stateOf(host, "broken") == PluginState::SafeMode);
        CHECK(!host.loading());

        host.clearSafeMode("broken");
        CHECK(stateOf(host, "broken") == PluginState::NotEnabled);
        CHECK(readPluginSafeList(dir).empty());
        CHECK(!readPluginConfig(dir).isEnabled("broken"));
        if (std::string(item.name) == "crashload") {
            continue;
        }
        host.setEnabled("broken", true);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        CHECK(stateOf(host, "broken") == PluginState::Loaded);
        host.stopAll();
        host.waitForThreads(2.0);
    }

    const fs::path dir = freshDir("child_none");
    installPlugin(dir, "broken", NVJ_PLUGIN_CRASH, "broken");
    CHECK(recordPluginCrashes(dir, "NyrexVJ 1\nreason: x\n", 1).empty());
    CHECK(readPluginSafeList(dir).empty());
}

}

int main(int argc, char** argv) {
    gSelf = fs::absolute(argv[0]).string();
    if (argc >= 3 && std::string(argv[1]) == "--crash-child") {
        return crashChild(argv[2]);
    }
    testManifest();
    testConfigAndSafeList();
    testCrashMarkers();
    testCompatibilityBeforeCode();
    testBrokenLoads();
    testExamplePlugin();
    testSlowAndFailing();
    testFailingCalls();
    testFrameSourceGuards();
    testLogLimit();
    testCrashLeadsToSafeMode();
    testSlowLoadStaysResponsive();
    testHangStaysResponsive();
    return testing::finish();
}

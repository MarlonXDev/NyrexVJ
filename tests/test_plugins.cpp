#include "check.hpp"

#include "core/crash_handler.hpp"
#include "core/log.hpp"
#include "core/plugin_config.hpp"
#include "core/plugin_host.hpp"
#include "core/plugin_manifest.hpp"
#include "core/recovery.hpp"

#include "nyrexvj_plugin.h"

extern "C" {
#include "plugins/abi_entry.h"
size_t abi_v1_0_count(void);
const AbiEntry* abi_v1_0_entry(size_t index);
size_t abi_current_count(void);
const AbiEntry* abi_current_entry(size_t index);
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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

fs::path installPlugin(const fs::path& dataDir, const std::string& folder, const std::string& library, const std::string& id, unsigned major = 1, unsigned minor = kPluginApiMinor) {
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
    writeText(base / "newerminor" / "plugin.json", manifestText("newerminor", 1, kPluginApiMinor + 1));
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
        const PluginEntry shipped = parseManifest(example, fs::path(NVJ_PLUGIN_EXAMPLE_MANIFEST).parent_path(), 1, kPluginApiMinor, "linux-x64");
        CHECK(shipped.manifest.id == "nyrexvj-example");
        CHECK(shipped.manifest.apiMajor == 1 && shipped.manifest.apiMinor == kPluginApiMinor);
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


void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
    SetEnvironmentVariableA(name, value[0] != '\0' ? value : nullptr);
#else
    setenv(name, value, 1);
#endif
}

struct EnvGuard {
    std::string name;

    EnvGuard(const char* variable, const char* value) : name(variable) { setEnv(variable, value); }
    ~EnvGuard() { setEnv(name.c_str(), ""); }
};

const char* kProbeNames[8] = {"entries", "overlap", "destroys", "live", "cap_known", "cap_unknown", "limit", "fail_text"};

void assignProbeSlots(PluginHost& host) {
    for (int i = 0; i < 8; ++i) {
        host.assignModulation(i, std::string("probe/") + kProbeNames[i]);
    }
}

bool probeIs(PluginHost& host, int slot, float expected, double seconds = 2.0) {
    return waitFor(host, [&]() { return std::fabs(host.modulation(slot) - expected) < 0.001f; }, seconds);
}

std::string manifestWith(const std::string& id, unsigned major, unsigned minor, const std::string& requiresJson) {
    return std::string("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"") + id + "\",\"name\":\"Test " + id + "\",\"api\":{\"major\":" + std::to_string(major) + ",\"minor\":" + std::to_string(minor) + "}," + (requiresJson.empty() ? std::string() : "\"requires\":" + requiresJson + ",") + "\"library\":{\"linux-x64\":\"plugin.so\",\"windows-x64\":\"plugin.dll\"}}";
}

fs::path dummyPluginFolder(const std::string& name) {
    const fs::path dir = freshDir(name);
    writeText(dir / "plugin.so", "x");
    writeText(dir / "plugin.dll", "x");
    return dir;
}

void testAbiLayout() {
    std::map<std::string, AbiEntry> before;
    std::map<std::string, AbiEntry> after;
    for (size_t i = 0; i < abi_v1_0_count(); ++i) {
        const AbiEntry* entry = abi_v1_0_entry(i);
        before[entry->name] = *entry;
    }
    for (size_t i = 0; i < abi_current_count(); ++i) {
        const AbiEntry* entry = abi_current_entry(i);
        after[entry->name] = *entry;
    }
    CHECK(before.size() > 50);
    for (const auto& item : before) {
        const auto found = after.find(item.first);
        CHECK(found != after.end());
        if (found == after.end()) {
            continue;
        }
        if (item.first == "NvjHost sizeof") {
            CHECK(found->second.offset > item.second.offset);
            continue;
        }
        CHECK(found->second.offset == item.second.offset);
        CHECK(found->second.size == item.second.size);
    }
    if (sizeof(void*) == 8) {
        CHECK(before["NvjHost sizeof"].offset == 56);
        CHECK(after["NvjHost sizeof"].offset == 80);
        CHECK(after["NvjHost.get_parameter"].offset == 48);
        CHECK(after["NvjHost.has_capability"].offset == 56);
        CHECK(after["NvjHost.get_limits"].offset == 64);
        CHECK(after["NvjHost.set_error"].offset == 72);
        CHECK(after["NvjPlugin sizeof"].offset == 88);
        CHECK(after["NvjPlugin.source_count"].offset == 40);
        CHECK(after["NvjPlugin.get_source"].offset == 48);
        CHECK(after["NvjPlugin.frame"].offset == 80);
        CHECK(after["NvjSourceInfo sizeof"].offset == 64);
        CHECK(after["NvjSourceInfo.parameter_names"].offset == 32);
        CHECK(after["NvjImage sizeof"].offset == 24);
        CHECK(after["NvjImage.data"].offset == 16);
        CHECK(after["NvjAudio sizeof"].offset == 24);
        CHECK(after["NvjBeat sizeof"].offset == 24);
        CHECK(after["NvjLimits sizeof"].offset == 56);
    }
    CHECK(sizeof(NvjHost) == after["NvjHost sizeof"].offset);
    CHECK(sizeof(NvjLimits) == after["NvjLimits sizeof"].offset);
    CHECK(kPluginApiMajor == NYREXVJ_PLUGIN_API_MAJOR);
    CHECK(kPluginApiMinor == NYREXVJ_PLUGIN_API_MINOR);
    CHECK(static_cast<unsigned>(kPluginParameters) == NYREXVJ_PLUGIN_MAX_PARAMETERS);
    CHECK(kPluginFrameWidth <= static_cast<int>(NYREXVJ_PLUGIN_MAX_WIDTH) && kPluginFrameHeight <= static_cast<int>(NYREXVJ_PLUGIN_MAX_HEIGHT));
    CHECK(kPluginSourceKindModulation == NVJ_SOURCE_MODULATION && kPluginSourceKindFrame == NVJ_SOURCE_FRAME);
    CHECK(NVJ_OK == 0 && NVJ_FAILED == 1);
}

void testCapabilityList() {
    const std::vector<std::string>& list = pluginHostCapabilities();
    CHECK(list.size() == 9);
    for (const std::string& name : list) {
        CHECK(validCapabilityName(name));
        CHECK(pluginHostHasCapability(name));
    }
    CHECK(!pluginHostHasCapability("isolation.process"));
    CHECK(!pluginHostHasCapability("vendor.future"));
    CHECK(!pluginHostHasCapability(""));
    CHECK(!pluginHostHasCapability("HOST.LOG"));
    CHECK(validCapabilityName("a") && validCapabilityName("host.log") && validCapabilityName("a-b_c.d"));
    CHECK(!validCapabilityName("") && !validCapabilityName(".a") && !validCapabilityName("a.") && !validCapabilityName("-a") && !validCapabilityName("a b") && !validCapabilityName("A") && !validCapabilityName("a/b"));
    CHECK(!validCapabilityName(std::string(kMaxCapabilityLength + 1, 'a')));
    CHECK(validCapabilityName(std::string(kMaxCapabilityLength, 'a')));
}

void testRequires() {
    const fs::path dir = dummyPluginFolder("requires");
    const auto parse = [&](const std::string& requiresJson) {
        return parseManifest(manifestWith("req", 1, kPluginApiMinor, requiresJson), dir, 1, kPluginApiMinor, "linux-x64");
    };
    PluginEntry none = parse("");
    CHECK(none.usable() && none.manifest.requiredCapabilities.empty());
    PluginEntry empty = parse("[]");
    CHECK(empty.usable() && empty.manifest.requiredCapabilities.empty());
    PluginEntry known = parse("[\"host.audio\",\"source.frame\",\"host.audio\"]");
    CHECK(known.usable());
    CHECK(known.manifest.requiredCapabilities.size() == 2);
    PluginEntry future = parse("[\"host.audio\",\"vendor.future\",\"vendor.other\"]");
    CHECK(future.problem == PluginProblem::MissingCapability);
    CHECK(contains(future.detail, "vendor.future") && contains(future.detail, "vendor.other") && !contains(future.detail, "host.audio"));
    CHECK(parse("\"host.audio\"").problem == PluginProblem::BadRequires);
    CHECK(parse("{\"a\":1}").problem == PluginProblem::BadRequires);
    CHECK(parse("[5]").problem == PluginProblem::BadRequires);
    CHECK(parse("[null]").problem == PluginProblem::BadRequires);
    CHECK(parse("[\"\"]").problem == PluginProblem::BadRequires);
    CHECK(parse("[\"Host.Audio\"]").problem == PluginProblem::BadRequires);
    CHECK(parse("[\"a b\"]").problem == PluginProblem::BadRequires);
    CHECK(parse("[\"" + std::string(kMaxCapabilityLength + 1, 'a') + "\"]").problem == PluginProblem::BadRequires);
    std::string many = "[";
    for (size_t i = 0; i < kMaxPluginRequires + 1; ++i) {
        many += std::string(i == 0 ? "" : ",") + "\"host.log\"";
    }
    CHECK(parse(many + "]").problem == PluginProblem::BadRequires);
    std::string limit = "[";
    for (size_t i = 0; i < kMaxPluginRequires; ++i) {
        limit += std::string(i == 0 ? "" : ",") + "\"host.log\"";
    }
    CHECK(parse(limit + "]").usable());
    CHECK(std::string(pluginProblemName(PluginProblem::MissingCapability)).size() > 10);
    CHECK(std::string(pluginProblemName(PluginProblem::BadRequires)).size() > 10);

    const fs::path data = freshDir("requires_host");
    const fs::path folder = pluginsFolder(data) / "future";
    fs::create_directories(folder);
    fs::copy_file(NVJ_PLUGIN_CRASHLOAD, folder / libraryFileName(), fs::copy_options::overwrite_existing);
    writeText(folder / "plugin.json", manifestWith("future", 1, kPluginApiMinor, "[\"vendor.future\"]"));
    enablePlugins(data, {"future"});
    PluginHost host(data);
    pause(150);
    host.update();
    CHECK(stateOf(host, "future") == PluginState::Incompatible);
    CHECK(host.find("future")->errorKind == PluginErrorKind::Incompatible);
    CHECK(contains(messageOf(host, "future"), "vendor.future"));
    host.setEnabled("future", true);
    pause(100);
    host.update();
    CHECK(stateOf(host, "future") == PluginState::Incompatible);
    CHECK(!host.loading());
    CHECK(pluginThreadsAlive() == 0);
}

void testVersionMatrix() {
    const fs::path dir = dummyPluginFolder("matrix");
    const unsigned hosts[][2] = {{1, 0}, {1, 1}, {1, 2}, {2, 0}};
    const unsigned plugins[][2] = {{0, 0}, {1, 0}, {1, 1}, {1, 2}, {1, 65535}, {2, 0}, {2, 1}, {3, 0}};
    int usable = 0;
    int total = 0;
    for (const auto& host : hosts) {
        for (const auto& plugin : plugins) {
            const PluginEntry entry = parseManifest(manifestWith("matrix", plugin[0], plugin[1], ""), dir, host[0], host[1], "linux-x64");
            PluginProblem expected = PluginProblem::None;
            if (plugin[0] == 0) {
                expected = PluginProblem::BadApi;
            } else if (plugin[0] != host[0]) {
                expected = PluginProblem::WrongMajor;
            } else if (plugin[1] > host[1]) {
                expected = PluginProblem::NewerMinor;
            }
            CHECK(entry.problem == expected);
            ++total;
            usable += entry.usable() ? 1 : 0;
        }
    }
    CHECK(total == 32);
    CHECK(usable == 7);
    CHECK(parseManifest(manifestWith("matrix", 1, 70000, ""), dir, 1, 1, "linux-x64").problem == PluginProblem::BadApi);
    CHECK(parseManifest("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"m\",\"name\":\"M\",\"api\":{\"major\":1,\"minor\":-1},\"library\":{\"linux-x64\":\"plugin.so\"}}", dir, 1, 1, "linux-x64").problem == PluginProblem::BadApi);
    CHECK(parseManifest("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"m\",\"name\":\"M\",\"api\":{\"major\":1.5,\"minor\":0},\"library\":{\"linux-x64\":\"plugin.so\"}}", dir, 1, 1, "linux-x64").problem == PluginProblem::BadApi);
}

void testLibraryAndManifestMustAgree() {
    const fs::path dir = freshDir("agree");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe", 1, 0);
    enablePlugins(dir, {"probe"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return settled(host, "probe"); }, 3.0));
    CHECK(stateOf(host, "probe") == PluginState::Failed);
    CHECK(host.find("probe")->errorKind == PluginErrorKind::Incompatible);
    CHECK(contains(messageOf(host, "probe"), "plugin.json says 1.0"));
}

void testLegacyPlugin() {
    const fs::path dir = freshDir("legacy");
    installPlugin(dir, "legacy", NVJ_PLUGIN_LEGACY, "nyrexvj-legacy", 1, 0);
    enablePlugins(dir, {"nyrexvj-legacy"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "nyrexvj-legacy") == PluginState::Loaded; }, 3.0));
    const PluginInfo* info = host.find("nyrexvj-legacy");
    CHECK(info != nullptr && info->entry.manifest.apiMinor == 0 && info->sources.size() == 2);
    CHECK(host.findSource("nyrexvj-legacy/level") != nullptr && host.findSource("nyrexvj-legacy/tone") != nullptr);
    host.assignModulation(0, "nyrexvj-legacy/level");
    host.setParameter("nyrexvj-legacy/level", 0, 1.0f);
    PluginHostSnapshot snapshot;
    snapshot.level = 0.5f;
    host.publish(snapshot);
    CHECK(waitFor(host, [&]() { return std::fabs(host.modulation(0) - 0.75f) < 0.001f; }, 2.0));
    host.setParameter("nyrexvj-legacy/level", 0, 0.0f);
    CHECK(waitFor(host, [&]() { return std::fabs(host.modulation(0) - 0.25f) < 0.001f; }, 2.0));
    host.setParameter("nyrexvj-legacy/tone", 0, 1.0f);
    std::string error;
    std::unique_ptr<LiveSource> source = host.openFrameSource("nyrexvj-legacy/tone", error);
    CHECK(source != nullptr);
    if (source != nullptr) {
        LiveFrame frame;
        bool got = false;
        const double end = now() + 3.0;
        while (now() < end && !got) {
            host.update();
            got = source->take(frame);
            pause(5);
        }
        CHECK(got && frame.width == kPluginFrameWidth && frame.height == kPluginFrameHeight);
        CHECK(got && frame.data.size() == static_cast<size_t>(kPluginFrameWidth) * kPluginFrameHeight * 4);
        CHECK(got && frame.data[0] == 255 && frame.data[3] == 255);
        source->stop();
    }
    CHECK(host.runnerStats("nyrexvj-legacy/tone").calls > 0);
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
    CHECK(host.abandonedInstances() == 0);
}

void testCapabilityQueries() {
    const fs::path dir = freshDir("capabilities");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"probe"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    assignProbeSlots(host);
    CHECK(probeIs(host, 4, 1.0f));
    CHECK(probeIs(host, 5, 0.0f));
    CHECK(probeIs(host, 6, static_cast<float>(kPluginFrameWidth) / 4096.0f));
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testPluginErrorText() {
    const fs::path dir = freshDir("errortext");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"probe"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    assignProbeSlots(host);
    CHECK(waitFor(host, [&]() { host.modulation(7); return stateOf(host, "probe") == PluginState::Failed; }, 3.0));
    CHECK(host.find("probe")->errorKind == PluginErrorKind::CallFailed);
    CHECK(contains(messageOf(host, "probe"), "modulation returned an error"));
    CHECK(contains(messageOf(host, "probe"), "the plugin says: the probe refuses to go on"));
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testPluginErrorTextLimits() {
    const fs::path dir = freshDir("errortext_long");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"probe"});
    EnvGuard guard("NVJ_PROBE_LONG_TEXT", "1");
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    assignProbeSlots(host);
    CHECK(waitFor(host, [&]() { host.modulation(7); return stateOf(host, "probe") == PluginState::Failed; }, 3.0));
    const std::string message = messageOf(host, "probe");
    const std::string marker = "(the plugin says: ";
    const size_t begin = message.find(marker);
    CHECK(begin != std::string::npos);
    if (begin != std::string::npos) {
        const size_t from = begin + marker.size();
        const size_t end = message.rfind(')');
        CHECK(end != std::string::npos && end > from);
        const std::string said = end != std::string::npos && end > from ? message.substr(from, end - from) : std::string();
        CHECK(said.size() == kPluginMaxErrorText);
        CHECK(said.compare(0, 5, "abcde") == 0);
    }
    bool control = false;
    for (const char c : message) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7f) {
            control = true;
        }
    }
    CHECK(!control);
    CHECK(message.size() < kPluginMaxErrorText + 200);
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

struct BrokenCase {
    const char* name;
    const char* library;
    PluginErrorKind kind;
    const char* message;
};

void testTableGuardsAndErrorKinds() {
    const BrokenCase cases[] = {
        {"tableoverflow", NVJ_PLUGIN_TABLEOVERFLOW, PluginErrorKind::Guard, "wrote behind the plugin table"},
        {"tablesize", NVJ_PLUGIN_TABLESIZE, PluginErrorKind::Guard, "reports a table of"},
        {"sourceoverflow", NVJ_PLUGIN_SOURCEOVERFLOW, PluginErrorKind::Guard, "wrote behind the source description"},
        {"entrytext", NVJ_PLUGIN_ENTRYTEXT, PluginErrorKind::Load, "the plugin says: no license file found"},
        {"entryfail", NVJ_PLUGIN_ENTRYFAIL, PluginErrorKind::Load, "entry function returned an error"},
        {"noentry", NVJ_PLUGIN_NOENTRY, PluginErrorKind::Load, "no nyrexvj_plugin_entry"},
        {"mismatch", NVJ_PLUGIN_MISMATCH, PluginErrorKind::Incompatible, "reports API 2.0"},
        {"idmismatch", NVJ_PLUGIN_IDMISMATCH, PluginErrorKind::Incompatible, "id differs"},
        {"duplicate", NVJ_PLUGIN_DUPLICATE, PluginErrorKind::Load, "two sources"},
        {"toomany", NVJ_PLUGIN_TOOMANY, PluginErrorKind::Load, "9 sources"},
    };
    for (const BrokenCase& item : cases) {
        const fs::path dir = freshDir(std::string("guard_") + item.name);
        installPlugin(dir, "broken", item.library, "broken");
        enablePlugins(dir, {"broken"});
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        CHECK(stateOf(host, "broken") == PluginState::Failed);
        const PluginInfo* info = host.find("broken");
        CHECK(info != nullptr && info->errorKind == item.kind);
        CHECK(contains(messageOf(host, "broken"), item.message));
        CHECK(host.waitForThreads(3.0) == 0);
    }
}

void testRuntimeErrorKinds() {
    struct RuntimeCase {
        const char* name;
        const char* library;
        PluginErrorKind kind;
        bool frame;
    };
    const RuntimeCase cases[] = {
        {"slow", NVJ_PLUGIN_SLOW, PluginErrorKind::Timeout, false},
        {"fail", NVJ_PLUGIN_FAIL, PluginErrorKind::CallFailed, false},
        {"errortext", NVJ_PLUGIN_ERRORTEXT, PluginErrorKind::CallFailed, false},
        {"overflow", NVJ_PLUGIN_OVERFLOW, PluginErrorKind::Guard, true},
        {"imagechanged", NVJ_PLUGIN_IMAGECHANGED, PluginErrorKind::BadOutput, true},
        {"slowframe", NVJ_PLUGIN_SLOWFRAME, PluginErrorKind::Timeout, true},
    };
    for (const RuntimeCase& item : cases) {
        const fs::path dir = freshDir(std::string("kind_") + item.name);
        installPlugin(dir, "broken", item.library, "broken");
        enablePlugins(dir, {"broken"});
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
        std::unique_ptr<LiveSource> source;
        if (item.frame) {
            std::string error;
            source = host.openFrameSource("broken/img", error);
            CHECK(source != nullptr);
        } else {
            host.assignModulation(0, "broken/mod");
        }
        CHECK(waitFor(host, [&]() {
            if (!item.frame) {
                host.modulation(0);
            }
            return stateOf(host, "broken") == PluginState::Failed;
        }, 4.0));
        const PluginInfo* info = host.find("broken");
        CHECK(info != nullptr && info->errorKind == item.kind);
        if (std::string(item.name) == "errortext") {
            CHECK(contains(messageOf(host, "broken"), "the plugin says: the device was unplugged"));
        }
        source.reset();
        host.stopAll();
        CHECK(host.waitForThreads(4.0) == 0);
    }
    CHECK(std::string(pluginErrorKindName(PluginErrorKind::Timeout)) == "time budget");
    CHECK(std::string(pluginErrorKindName(PluginErrorKind::None)) == "none");
}

void testEntryOncePerProcess() {
    const fs::path dir = freshDir("entry_once");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"probe"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    assignProbeSlots(host);
    CHECK(probeIs(host, 0, 1.0f / 16.0f));
    for (int cycle = 0; cycle < 25; ++cycle) {
        host.modulation(3);
        host.setEnabled("probe", false);
        CHECK(stateOf(host, "probe") == PluginState::NotEnabled);
        host.setEnabled("probe", true);
        CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
        CHECK(host.find("probe")->sources.size() == 8);
    }
    CHECK(probeIs(host, 0, 1.0f / 16.0f));
    CHECK(probeIs(host, 1, 0.0f));
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
    CHECK(host.liveInstancesOf("probe") == 0);
    CHECK(host.abandonedInstances() == 0);
    CHECK(readPluginConfig(dir).isEnabled("probe"));
}

void testDisableWhileLoading() {
    const fs::path dir = freshDir("disable_loading");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    EnvGuard slow("NVJ_PROBE_LOAD_MS", "300");
    PluginHost host(dir);
    host.setEnabled("probe", true);
    CHECK(stateOf(host, "probe") == PluginState::Loading);
    pause(50);
    host.setEnabled("probe", false);
    CHECK(stateOf(host, "probe") == PluginState::NotEnabled);
    CHECK(waitFor(host, [&]() { return !host.loading(); }, 3.0));
    pause(100);
    host.update();
    CHECK(stateOf(host, "probe") == PluginState::NotEnabled);
    CHECK(!readPluginConfig(dir).isEnabled("probe"));
    assignProbeSlots(host);
    CHECK(host.modulation(0) == 0.0f);
    CHECK(host.runningInstances() == 0);

    host.setEnabled("probe", true);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    EnvGuard fast("NVJ_PROBE_LOAD_MS", "0");
    CHECK(probeIs(host, 0, 1.0f / 16.0f));
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testEnableTwiceWhileLoading() {
    const fs::path dir = freshDir("enable_twice");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    EnvGuard slow("NVJ_PROBE_LOAD_MS", "250");
    PluginHost host(dir);
    host.setEnabled("probe", true);
    host.setEnabled("probe", true);
    host.setEnabled("probe", false);
    host.setEnabled("probe", true);
    CHECK(stateOf(host, "probe") == PluginState::Loading);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    EnvGuard fast("NVJ_PROBE_LOAD_MS", "0");
    assignProbeSlots(host);
    CHECK(probeIs(host, 0, 1.0f / 16.0f));
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testReenableWaitsForOldInstances() {
    const fs::path dir = freshDir("reenable_destroy");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"probe"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    assignProbeSlots(host);
    host.modulation(3);
    CHECK(waitFor(host, [&]() { return host.runningInstances() == 1; }, 2.0));
    EnvGuard slow("NVJ_PROBE_DESTROY_MS", "300");
    const double begin = now();
    host.setEnabled("probe", false);
    host.setEnabled("probe", true);
    CHECK(stateOf(host, "probe") == PluginState::Loading);
    CHECK(waitFor(host, [&]() { return stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    CHECK(now() - begin > 0.25);
    EnvGuard fast("NVJ_PROBE_DESTROY_MS", "0");
    CHECK(probeIs(host, 1, 0.0f));
    CHECK(probeIs(host, 0, 1.0f / 16.0f));

    host.modulation(3);
    host.setEnabled("probe", false);
    host.setEnabled("probe", true);
    host.setEnabled("probe", false);
    CHECK(stateOf(host, "probe") == PluginState::NotEnabled);
    pause(400);
    host.update();
    CHECK(stateOf(host, "probe") == PluginState::NotEnabled);
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testFailedStaysFailed() {
    const fs::path dir = freshDir("failed_stays");
    installPlugin(dir, "broken", NVJ_PLUGIN_ENTRYFAIL, "broken");
    enablePlugins(dir, {"broken"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return settled(host, "broken"); }, 3.0));
    CHECK(stateOf(host, "broken") == PluginState::Failed);
    for (int i = 0; i < 3; ++i) {
        host.setEnabled("broken", false);
        CHECK(stateOf(host, "broken") == PluginState::Failed);
        host.setEnabled("broken", true);
        pause(50);
        host.update();
        CHECK(stateOf(host, "broken") == PluginState::Failed);
    }
    CHECK(!host.loading());
    CHECK(host.waitForThreads(3.0) == 0);
}

void testInstanceLimits() {
    const fs::path dir = freshDir("instance_limits");
    installPlugin(dir, "broken", NVJ_PLUGIN_FINE, "broken");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"broken", "probe"});
    PluginLimits limits;
    limits.instancesPerPlugin = 2;
    limits.instancesTotal = 3;
    PluginHost host(dir, true, limits);
    CHECK(host.limits().instancesPerPlugin == 2);
    CHECK(waitFor(host, [&]() { return stateOf(host, "broken") == PluginState::Loaded && stateOf(host, "probe") == PluginState::Loaded; }, 3.0));
    std::string error;
    std::unique_ptr<LiveSource> first = host.openFrameSource("broken/img", error);
    std::unique_ptr<LiveSource> second = host.openFrameSource("broken/img", error);
    CHECK(first != nullptr && second != nullptr);
    error.clear();
    std::unique_ptr<LiveSource> third = host.openFrameSource("broken/img", error);
    CHECK(third == nullptr);
    CHECK(contains(error, "limit") && contains(error, "2 running instances"));
    CHECK(stateOf(host, "broken") == PluginState::Loaded);

    assignProbeSlots(host);
    host.modulation(0);
    CHECK(waitFor(host, [&]() { return host.runningInstances() == 3; }, 2.0));
    host.modulation(1);
    const PluginRunStats refused = host.modulationStats(1);
    CHECK(!refused.running && refused.errorKind == PluginErrorKind::Limit && contains(refused.error, "3 plugin instances"));
    CHECK(host.liveInstancesOf("broken") == 2 && host.liveInstancesOf("probe") == 1);
    CHECK(stateOf(host, "probe") == PluginState::Loaded);

    first.reset();
    CHECK(waitFor(host, [&]() { return host.liveInstancesOf("broken") <= 1; }, 2.0));
    host.modulation(1);
    CHECK(waitFor(host, [&]() { return host.modulationStats(1).running; }, 2.0));
    second.reset();
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);
}

void testAbandonedLimit() {
    const fs::path dir = freshDir("abandoned_limit");
    installPlugin(dir, "broken", NVJ_PLUGIN_HANG, "broken");
    installPlugin(dir, "probe", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(dir, {"broken"});
    PluginLimits limits;
    limits.abandoned = 1;
    PluginHost host(dir, true, limits);
    CHECK(waitFor(host, [&]() { return stateOf(host, "broken") == PluginState::Loaded; }, 3.0));
    host.assignModulation(0, "broken/mod");
    CHECK(waitFor(host, [&]() { host.modulation(0); return stateOf(host, "broken") == PluginState::Failed; }, 3.0));
    CHECK(host.find("broken")->errorKind == PluginErrorKind::Timeout);
    CHECK(waitFor(host, [&]() { return host.abandonedInstances() >= 1; }, 2.0));
    host.setEnabled("probe", true);
    CHECK(stateOf(host, "probe") == PluginState::Failed);
    CHECK(host.find("probe")->errorKind == PluginErrorKind::Limit);
    CHECK(contains(messageOf(host, "probe"), "stuck"));
    CHECK(!host.loading());
    pause(100);
    host.update();
    CHECK(stateOf(host, "probe") == PluginState::Failed);
}

void testDutyLimit() {
    const fs::path dir = freshDir("duty_limit");
    installPlugin(dir, "broken", NVJ_PLUGIN_BUSYFRAME, "broken");
    enablePlugins(dir, {"broken"});
    PluginHost host(dir);
    CHECK(waitFor(host, [&]() { return stateOf(host, "broken") == PluginState::Loaded; }, 3.0));
    std::string error;
    std::unique_ptr<LiveSource> source = host.openFrameSource("broken/img", error);
    CHECK(source != nullptr);
    const double end = now() + 3.0;
    while (now() < end) {
        host.update();
        LiveFrame frame;
        source->take(frame);
        pause(5);
    }
    const PluginRunStats stats = host.runnerStats("broken/img");
    CHECK(stateOf(host, "broken") == PluginState::Loaded);
    CHECK(stats.running && !stats.failed);
    CHECK(stats.throttled);
    CHECK(stats.duty > 0.6 && stats.duty < 0.86);
    CHECK(stats.frames >= 10 && stats.frames <= 40);
    CHECK(stats.maxCallMs < 100.0);
    source.reset();
    host.stopAll();
    CHECK(host.waitForThreads(3.0) == 0);

    const fs::path other = freshDir("duty_fast");
    installPlugin(other, "good", NVJ_PLUGIN_PROBE, "probe");
    enablePlugins(other, {"probe"});
    PluginHost fast(other);
    CHECK(waitFor(fast, [&]() { return stateOf(fast, "probe") == PluginState::Loaded; }, 3.0));
    assignProbeSlots(fast);
    fast.modulation(0);
    pause(600);
    fast.modulation(0);
    const PluginRunStats quick = fast.modulationStats(0);
    CHECK(!quick.throttled && quick.duty < 0.2 && quick.calls > 10);
    fast.stopAll();
    CHECK(fast.waitForThreads(3.0) == 0);
}

struct FuzzRandom {
    uint64_t state;

    explicit FuzzRandom(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}

    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }

    size_t below(size_t limit) { return limit == 0 ? 0 : static_cast<size_t>(next() % limit); }
};

const char* kFuzzTokens[] = {"null", "true", "false", "[]", "{}", "-1", "0", "1", "65536", "1e999", "-0", "1.5", "\"\"", "\"\\u0000\"", "\"\\ud800\"", "\"a/b\"", "\"../x\"", "[[[[[[", "]]]]]]", "{\"a\":", "\"id\"", "\"api\"", "\"library\"", "\"requires\"", "\"enabled\"", "\"format\"", "\"disabled\"", "\"parameters\"", "\"version\"", "\"nyrexvj-plugin\"", ":", ",", "\"", "\\", "\xff\xfe", "\n"};

std::string mutateText(const std::string& base, FuzzRandom& random) {
    std::string text = base;
    const size_t operations = 1 + random.below(4);
    for (size_t i = 0; i < operations; ++i) {
        const size_t kind = random.below(9);
        if (text.empty()) {
            text = "{}";
        }
        const size_t at = random.below(text.size());
        switch (kind) {
        case 0:
            text[at] = static_cast<char>(random.below(256));
            break;
        case 1:
            text.erase(at, 1 + random.below(std::min<size_t>(12, text.size() - at)));
            break;
        case 2:
            text.insert(at, kFuzzTokens[random.below(sizeof(kFuzzTokens) / sizeof(kFuzzTokens[0]))]);
            break;
        case 3:
            text.resize(at);
            break;
        case 4: {
            const size_t length = 1 + random.below(std::min<size_t>(40, text.size() - at));
            text.insert(random.below(text.size()), text.substr(at, length));
            break;
        }
        case 5: {
            const size_t start = text.find('"', at);
            if (start != std::string::npos) {
                const size_t finish = text.find('"', start + 1);
                if (finish != std::string::npos) {
                    text.replace(start, finish - start + 1, kFuzzTokens[random.below(sizeof(kFuzzTokens) / sizeof(kFuzzTokens[0]))]);
                }
            }
            break;
        }
        case 6:
            text.insert(at, std::string(1 + random.below(300), static_cast<char>('0' + random.below(10))));
            break;
        case 7:
            text.insert(at, std::string(1 + random.below(2000), '['));
            break;
        default:
            text.swap(text);
            text.insert(at, 1, static_cast<char>(random.below(32)));
            break;
        }
    }
    return text;
}

void testFuzzManifest() {
    const fs::path dir = dummyPluginFolder("fuzz_manifest");
    const std::string bases[] = {
        manifestWith("fuzzed", 1, 0, ""),
        manifestWith("fuzzed", 1, kPluginApiMinor, "[\"host.audio\",\"source.frame\"]"),
        std::string("{\"format\":\"nyrexvj-plugin\",\"version\":1,\"id\":\"a.b-c_d\",\"name\":\"Name\",\"pluginVersion\":\"2.0\",\"api\":{\"major\":1,\"minor\":0},\"library\":{\"linux-x64\":\"plugin.so\",\"windows-x64\":\"plugin.dll\"}}"),
    };
    FuzzRandom random(7);
    int usable = 0;
    int rejected = 0;
    int threw = 0;
    const int rounds = 20000;
    const double begin = now();
    for (int i = 0; i < rounds; ++i) {
        std::string text = mutateText(bases[random.below(3)], random);
        if (i % 1000 == 999) {
            text = std::string(kMaxManifestBytes + 1 + random.below(100), 'x');
        }
        if (i % 1500 == 1499) {
            text = std::string(kMaxManifestBytes - 10, '[');
        }
        PluginEntry entry;
        try {
            entry = parseManifest(text, dir, 1, kPluginApiMinor, "linux-x64");
        } catch (...) {
            ++threw;
            continue;
        }
        if (!entry.usable()) {
            ++rejected;
            CHECK(entry.problem != PluginProblem::None);
            continue;
        }
        ++usable;
        const PluginManifest& manifest = entry.manifest;
        const bool good = validPluginId(manifest.id) && !manifest.name.empty() && manifest.name.size() <= kMaxPluginNameLength && manifest.version.size() <= kMaxPluginVersionLength && manifest.apiMajor == kPluginApiMajor && manifest.apiMinor <= kPluginApiMinor && manifest.requiredCapabilities.size() <= kMaxPluginRequires && !manifest.library.empty() && manifest.library.find('/') == std::string::npos && manifest.library.find('\\') == std::string::npos && fs::is_regular_file(entry.libraryPath);
        bool capabilities = true;
        for (const std::string& name : manifest.requiredCapabilities) {
            capabilities = capabilities && validCapabilityName(name) && pluginHostHasCapability(name);
        }
        CHECK(good);
        CHECK(capabilities);
    }
    CHECK(threw == 0);
    CHECK(usable > 200);
    CHECK(rejected > 5000);
    CHECK(now() - begin < 30.0);
}

void testFuzzSettingsFiles() {
    PluginConfig sample;
    sample.setEnabled("alpha", true);
    sample.setEnabled("beta.two", true);
    sample.modulation[0] = "alpha/pulse";
    sample.modulation[7] = "beta.two/x";
    sample.frames[1] = "alpha/plasma";
    sample.parameters["alpha/pulse"] = {0.1f, 0.9f, 0.5f, 0.0f};
    const std::string configBase = pluginConfigToJson(sample);
    std::vector<PluginSafeEntry> entries(2);
    entries[0].id = "alpha";
    entries[0].reason = "crashed in the last session";
    entries[0].time = 1700000000;
    entries[1].id = "beta";
    entries[1].time = 5;
    const std::string safeBase = pluginSafeListToJson(entries);
    FuzzRandom random(11);
    int threw = 0;
    int parsedConfig = 0;
    int parsedSafe = 0;
    for (int i = 0; i < 20000; ++i) {
        const std::string configText = mutateText(configBase, random);
        try {
            const PluginConfig config = pluginConfigFromJson(configText);
            bool good = config.enabled.size() <= kMaxPluginConfigEntries && config.parameters.size() <= kMaxPluginConfigEntries;
            for (const std::string& id : config.enabled) {
                good = good && validPluginId(id);
            }
            std::string pluginId;
            std::string sourceId;
            for (const std::string& key : config.modulation) {
                good = good && (key.empty() || splitPluginSourceKey(key, pluginId, sourceId));
            }
            for (const std::string& key : config.frames) {
                good = good && (key.empty() || splitPluginSourceKey(key, pluginId, sourceId));
            }
            for (const auto& item : config.parameters) {
                for (float value : item.second) {
                    good = good && std::isfinite(value) && value >= 0.0f && value <= 1.0f;
                }
            }
            CHECK(good);
            CHECK(pluginConfigToJson(pluginConfigFromJson(pluginConfigToJson(config))) == pluginConfigToJson(config));
            parsedConfig += config.enabled.empty() ? 0 : 1;
        } catch (...) {
            ++threw;
        }
        const std::string safeText = mutateText(safeBase, random);
        try {
            const std::vector<PluginSafeEntry> parsed = pluginSafeListFromJson(safeText);
            bool good = parsed.size() <= kMaxPluginConfigEntries;
            for (const PluginSafeEntry& entry : parsed) {
                good = good && validPluginId(entry.id) && entry.reason.size() <= 120;
            }
            CHECK(good);
            parsedSafe += parsed.empty() ? 0 : 1;
        } catch (...) {
            ++threw;
        }
    }
    CHECK(threw == 0);
    CHECK(parsedConfig > 500);
    CHECK(parsedSafe > 500);
    CHECK(pluginConfigFromJson("{\"format\":5}").enabled.empty());
    CHECK(pluginSafeListFromJson("{\"format\":5}").empty());
    CHECK(pluginSafeListFromJson("{\"format\":\"nyrexvj-plugin-safe-list\",\"disabled\":[{\"id\":5},{\"id\":\"ok\",\"reason\":7}]}").size() == 1);
}

void testFuzzCrashReports() {
    FuzzRandom random(13);
    const std::string base = "NyrexVJ crash report\nversion: 0.33.0\nplugin: alpha\nplugin: beta.two\nreason: signal 11\n";
    int found = 0;
    for (int i = 0; i < 5000; ++i) {
        const std::string text = mutateText(base, random);
        const fs::path dir = freshDir("fuzz_crash");
        const std::vector<std::string> ids = recordPluginCrashes(dir, text, 1700000000);
        for (const std::string& id : ids) {
            ++found;
        }
        for (const PluginSafeEntry& entry : readPluginSafeList(dir)) {
            CHECK(validPluginId(entry.id));
        }
        if (i % 500 != 0) {
            continue;
        }
    }
    CHECK(found > 1000);
}

void testFuzzPluginTables() {
    const fs::path dir = freshDir("fuzz_tables");
    installPlugin(dir, "fuzz", NVJ_PLUGIN_FUZZ, "fuzz");
    enablePlugins(dir, {"fuzz"});
    int loaded = 0;
    int failed = 0;
    int ran = 0;
    std::map<PluginErrorKind, int> kinds;
    const int seeds = 400;
    for (int seed = 1; seed <= seeds; ++seed) {
        EnvGuard guard("NVJ_FUZZ_SEED", std::to_string(seed).c_str());
        PluginHost host(dir);
        CHECK(waitFor(host, [&]() { return settled(host, "fuzz"); }, 3.0));
        const PluginInfo* info = host.find("fuzz");
        CHECK(info != nullptr);
        if (info == nullptr) {
            continue;
        }
        if (info->state == PluginState::Failed) {
            ++failed;
            CHECK(info->errorKind != PluginErrorKind::None);
            CHECK(!info->message.empty());
            CHECK(info->message.size() < 600);
            ++kinds[info->errorKind];
            continue;
        }
        CHECK(info->state == PluginState::Loaded);
        ++loaded;
        CHECK(info->errorKind == PluginErrorKind::None);
        CHECK(!info->sources.empty() && info->sources.size() <= static_cast<size_t>(NYREXVJ_PLUGIN_MAX_SOURCES));
        std::vector<std::string> ids;
        int slot = 0;
        std::vector<std::unique_ptr<LiveSource>> sources;
        for (const PluginSourceDesc& source : info->sources) {
            CHECK(validPluginId(source.id));
            CHECK(!source.name.empty() && source.name.size() <= kMaxPluginNameLength);
            CHECK(source.parameterCount >= 0 && source.parameterCount <= kPluginParameters);
            CHECK(std::find(ids.begin(), ids.end(), source.id) == ids.end());
            ids.push_back(source.id);
            if (source.kind == kPluginSourceKindModulation && slot < kPluginModulationSlots) {
                host.assignModulation(slot++, source.key);
            } else if (source.kind == kPluginSourceKindFrame && sources.size() < 3) {
                std::string error;
                std::unique_ptr<LiveSource> opened = host.openFrameSource(source.key, error);
                if (opened) {
                    sources.push_back(std::move(opened));
                }
            }
        }
        const double end = now() + 0.05;
        while (now() < end) {
            host.update();
            for (int i = 0; i < slot; ++i) {
                const float value = host.modulation(i);
                CHECK(std::isfinite(value) && value >= 0.0f && value <= 1.0f);
            }
            for (std::unique_ptr<LiveSource>& source : sources) {
                LiveFrame frame;
                if (source->take(frame)) {
                    CHECK(frame.width == kPluginFrameWidth && frame.height == kPluginFrameHeight);
                }
            }
            pause(2);
        }
        ++ran;
        sources.clear();
        host.stopAll();
        CHECK(host.waitForThreads(3.0) == 0);
        CHECK(host.abandonedInstances() == 0);
    }
    CHECK(loaded > 60);
    CHECK(failed > 100);
    CHECK(ran == loaded);
    CHECK(kinds[PluginErrorKind::Guard] > 0);
    CHECK(kinds[PluginErrorKind::Incompatible] > 0);
    CHECK(kinds[PluginErrorKind::Load] > 0);
    CHECK(pluginThreadsAlive() == 0);
}


void testCompatibilityBeforeCode() {
    const fs::path dir = freshDir("compat");
    installPlugin(dir, "major", NVJ_PLUGIN_CRASHLOAD, "major-two", 2, 0);
    installPlugin(dir, "minor", NVJ_PLUGIN_CRASHLOAD, "minor-newer", 1, kPluginApiMinor + 1);
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

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER gPreviousFilter = nullptr;

LONG WINAPI endChildFilter(EXCEPTION_POINTERS* info) {
    if (gPreviousFilter != nullptr) {
        gPreviousFilter(info);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int crashChild(const std::string& dataDir) {
    const fs::path dir(dataDir);
    std::error_code ec;
    fs::create_directories(recoveryDirectory(dir), ec);
    installCrashHandler(crashReportPath(dir).string(), "test");
#ifdef _WIN32
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    gPreviousFilter = SetUnhandledExceptionFilter(endChildFilter);
#endif
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

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
int heapCrashChild(const std::string& dataDir) {
    const fs::path dir(dataDir);
    std::error_code ec;
    fs::create_directories(recoveryDirectory(dir), ec);
    installCrashHandler(crashReportPath(dir).string(), "test");
    std::thread idle([]() { std::this_thread::sleep_for(std::chrono::seconds(30)); });
    idle.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    volatile size_t request = 24;
    for (int i = 0; i < 512; ++i) {
        volatile unsigned char* block = static_cast<volatile unsigned char*>(std::malloc(request));
        for (int k = 0; k < 32; ++k) {
            block[k] = 0xff;
        }
    }
    for (int round = 0; round < 100000; ++round) {
        void* more = std::malloc(24 + static_cast<size_t>(round % 200));
        if (round % 3 == 0) {
            std::free(more);
        }
    }
    return 3;
}
#endif

int runChild(const fs::path& dir) {
    std::string command = "\"" + gSelf + "\" --crash-child \"" + dir.string() + "\"";
#ifdef _WIN32
    command = "\"" + command + "\"";
#endif
    return std::system(command.c_str());
}

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
void testCrashInsideTheMemoryManager() {
    const fs::path dir = freshDir("child_heap");
    const std::string command = "timeout 60 \"" + gSelf + "\" --heap-crash-child \"" + dir.string() + "\" > /dev/null 2>&1";
    const int code = std::system(command.c_str());
    CHECK(code != 0);
    CHECK(code != 124 * 256);
    std::string report;
    CHECK(readWholeFile(crashReportPath(dir), report));
    CHECK(report.find("reason: program aborted (SIGABRT)") != std::string::npos);
    CHECK(report.find("NyrexVJ test") == 0);
}
#endif

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
#ifdef _WIN32
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
    gSelf = fs::absolute(argv[0]).string();
    if (argc >= 3 && std::string(argv[1]) == "--crash-child") {
        return crashChild(argv[2]);
    }
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    if (argc >= 3 && std::string(argv[1]) == "--heap-crash-child") {
        return heapCrashChild(argv[2]);
    }
#endif
    testManifest();
    testConfigAndSafeList();
    testCrashMarkers();
    testAbiLayout();
    testCapabilityList();
    testRequires();
    testVersionMatrix();
    testCompatibilityBeforeCode();
    testLibraryAndManifestMustAgree();
    testBrokenLoads();
    testTableGuardsAndErrorKinds();
    testExamplePlugin();
    testLegacyPlugin();
    testCapabilityQueries();
    testPluginErrorText();
    testPluginErrorTextLimits();
    testEntryOncePerProcess();
    testDisableWhileLoading();
    testEnableTwiceWhileLoading();
    testReenableWaitsForOldInstances();
    testFailedStaysFailed();
    testInstanceLimits();
    testSlowAndFailing();
    testRuntimeErrorKinds();
    testFailingCalls();
    testFrameSourceGuards();
    testLogLimit();
    testDutyLimit();
    testFuzzManifest();
    testFuzzSettingsFiles();
    testFuzzCrashReports();
    testFuzzPluginTables();
    testCrashLeadsToSafeMode();
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
    testCrashInsideTheMemoryManager();
#endif
    testSlowLoadStaysResponsive();
    testHangStaysResponsive();
    testAbandonedLimit();
    return testing::finish();
}

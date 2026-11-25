#include "app/api_selftest.hpp"

#include "app/app.hpp"
#include "control/api_security.hpp"
#include "control/control_targets.hpp"
#include "control/http_client.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "platform/platform.hpp"
#include "render/engine.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Report {
    json items = json::array();
    int failed = 0;
    int passed = 0;

    void check(bool ok, const std::string& name, const std::string& detail = std::string()) {
        json item;
        item["name"] = name;
        item["ok"] = ok;
        if (!detail.empty()) {
            item["detail"] = detail;
        }
        items.push_back(item);
        if (ok) {
            ++passed;
        } else {
            ++failed;
            logLine("API self test FAILED: " + name + (detail.empty() ? std::string() : " (" + detail + ")"));
        }
    }
};

struct Rig {
    App& app;
    Engine& engine;
    ApiSystem& api;

    explicit Rig(App& target) : app(target), engine(target.engine()), api(target.api()) {}

    void frame() {
        api.update(1.0 / 60.0, 60.0f);
        engine.update(1.0 / 60.0);
    }

    template <typename Fn>
    auto pump(Fn fn) -> decltype(fn()) {
        auto future = std::async(std::launch::async, fn);
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return future.get();
    }

    std::string raw(const std::string& method, const std::string& path, const std::string& body, bool authorized, const std::string& extra = std::string()) const {
        std::string text = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(api.port()) + "\r\n";
        if (authorized) {
            text += "Authorization: Bearer " + api.token() + "\r\n";
        }
        text += extra;
        if (!body.empty()) {
            text += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
        }
        return text + "\r\n" + body;
    }

    HttpExchange call(const std::string& method, const std::string& path, const std::string& body = std::string(), bool authorized = true, const std::string& extra = std::string()) {
        const int port = api.port();
        const std::string text = raw(method, path, body, authorized, extra);
        return pump([port, text] { return httpExchange(port, text, 4000); });
    }

    json callJson(const std::string& method, const std::string& path, const std::string& body = std::string(), int* status = nullptr) {
        const HttpExchange reply = call(method, path, body);
        if (status != nullptr) {
            *status = reply.status;
        }
        json parsed = json::parse(reply.body, nullptr, false);
        return parsed.is_discarded() ? json::object() : parsed;
    }

    void settle() {
        for (int i = 0; i < 3; ++i) {
            frame();
        }
    }
};

bool near(double a, double b, double tolerance = 0.002) {
    return std::fabs(a - b) <= tolerance;
}

}

int runApiSelfTest(App& app, const std::string& outputPath) {
    Rig rig(app);
    Report report;
    json out;
    out["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    std::error_code ec;
    fs::remove(api::tokenPath(), ec);

    report.check(!rig.api.running(), "the web interface is off by default");
    rig.api.update(1.0 / 60.0, 60.0f);
    report.check(!rig.api.running() && rig.api.queued() == 0, "an update with the interface off does nothing");

    ApiConfig config;
    config.enabled = true;
    config.port = 0;
    config.allowRemote = false;
    rig.api.configure(config);
    report.check(rig.api.running(), "the server starts when turned on", rig.api.error());
    if (!rig.api.running()) {
        out["items"] = report.items;
        out["failed"] = report.failed;
        if (!outputPath.empty()) {
            std::ofstream file(outputPath);
            file << out.dump(2);
        }
        return 1;
    }
    rig.settle();
    report.check(rig.api.port() > 0, "the server reports the bound port");
    report.check(api::validTokenFormat(rig.api.token()), "the token has the expected format");
    report.check(fs::exists(api::tokenPath()), "the token is saved in its own file");
    std::string stored;
    report.check(api::loadToken(stored) && stored == rig.api.token(), "the saved token matches");
#ifndef _WIN32
    const fs::perms permissions = fs::status(api::tokenPath(), ec).permissions();
    report.check((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none, "the token file is private to the user");
#endif
    report.check(rig.api.browserUrl().find("/#token=") != std::string::npos, "the browser address carries the token in the fragment");

    Composition look = factoryPresets().front();
    rig.engine.applyComposition(look);
    rig.settle();

    json status = rig.callJson("GET", "/api/v1/status");
    report.check(status.value("app", "") == "NyrexVJ", "status names the program");
    report.check(status.value("version", "") == NYREXVJ_VERSION, "status reports the program version");
    report.check(status.value("composition", "") == rig.engine.comp.name, "status reports the composition");
    report.check(near(status.value("bpm", 0.0), rig.engine.comp.bpm, 0.01), "status reports the tempo");

    report.check(rig.call("GET", "/api/v1/status", "", false).status == 401, "no token is refused");
    report.check(rig.call("GET", "/api/v1/status", "", true, "Origin: http://evil.example\r\n").status == 403, "a foreign origin is refused");
    report.check(rig.call("GET", "/api/v1/status", "", true, "Sec-Fetch-Site: cross-site\r\n").status == 403, "a cross-site request is refused");
    {
        const int port = rig.api.port();
        const std::string text = "GET /api/v1/status HTTP/1.1\r\nHost: attacker.example:" + std::to_string(port) + "\r\nAuthorization: Bearer " + rig.api.token() + "\r\n\r\n";
        report.check(rig.pump([port, text] { return httpExchange(port, text, 3000); }).status == 403, "a foreign host name is refused");
    }
    report.check(rig.call("OPTIONS", "/api/v1/status", "", true, "Origin: http://evil.example\r\nAccess-Control-Request-Method: PUT\r\n").status == 403, "a cross-origin preflight is refused");
    report.check(rig.call("GET", "/").status == 200, "the web page is served");

    json controls = rig.callJson("GET", "/api/v1/controls");
    report.check(controls.contains("controls") && controls["controls"].size() == controlTargets().size(), "the control list matches the control targets");

    int code = 0;
    json level = rig.callJson("PUT", "/api/v1/controls/master/level", "{\"value\":0.4}", &code);
    rig.settle();
    report.check(code == 200 && near(rig.engine.comp.master.level, 0.4), "setting the master level reaches the engine", std::to_string(rig.engine.comp.master.level));
    report.check(near(level.value("value", -1.0), 0.4), "the reply reports the applied value");
    json read = rig.callJson("GET", "/api/v1/controls/master/level");
    report.check(near(read.value("value", -1.0), 0.4), "the value reads back from the next snapshot");

    rig.callJson("PUT", "/api/v1/controls/layer/1/opacity", "{\"value\":0.25}", &code);
    rig.settle();
    report.check(code == 200 && near(rig.engine.comp.layers[0].opacity, 0.25), "setting a layer opacity reaches the engine");

    rig.callJson("PUT", "/api/v1/controls/master/blackout", "{\"value\":true}", &code);
    report.check(code == 200 && rig.engine.blackout, "blackout turns on");
    rig.callJson("PUT", "/api/v1/controls/master/blackout", "{\"value\":false}", &code);
    report.check(code == 200 && !rig.engine.blackout, "blackout turns off");

    rig.callJson("PUT", "/api/v1/controls/master/level", "{\"value\":2}", &code);
    report.check(code == 400, "a value above one is refused");
    rig.callJson("PUT", "/api/v1/controls/master/level", "{\"value\":\"x\"}", &code);
    report.check(code == 400, "a text value is refused");
    rig.callJson("PUT", "/api/v1/controls/master/nothing", "{\"value\":1}", &code);
    report.check(code == 404, "an unknown control is refused");

    int filled = -1;
    for (int column = 0; column < kColumnCount; ++column) {
        if (rig.engine.comp.layers[0].cells[static_cast<size_t>(column)] >= 0) {
            filled = column;
            break;
        }
    }
    report.check(filled >= 0, "the test look has a clip on layer 1");
    if (filled >= 0) {
        rig.engine.comp.layers[0].activeColumn = -1;
        rig.callJson("POST", "/api/v1/layers/1/clips/" + std::to_string(filled + 1) + "/trigger", "", &code);
        rig.settle();
        report.check(code == 200 && rig.engine.comp.layers[0].activeColumn == filled, "triggering a clip starts it");
        rig.callJson("POST", "/api/v1/layers/1/clear", "", &code);
        rig.settle();
        report.check(code == 200 && rig.engine.comp.layers[0].activeColumn == -1, "clearing a layer stops it");
    }
    int empty = -1;
    for (int column = 0; column < kColumnCount; ++column) {
        if (rig.engine.comp.layers[0].cells[static_cast<size_t>(column)] < 0) {
            empty = column;
            break;
        }
    }
    if (empty >= 0) {
        rig.callJson("POST", "/api/v1/layers/1/clips/" + std::to_string(empty + 1) + "/trigger", "", &code);
        report.check(code == 409, "an empty cell is reported");
    }
    rig.callJson("POST", "/api/v1/columns/1/trigger", "", &code);
    report.check(code == 200, "a column can be triggered");

    const std::vector<Composition>& looks = factoryPresets();
    json presets = rig.callJson("GET", "/api/v1/presets");
    report.check(presets.contains("factory") && presets["factory"].size() == looks.size(), "the factory presets are listed");
    if (looks.size() > 1) {
        const std::string target = looks[1].name;
        rig.callJson("POST", "/api/v1/presets/load", json{{"source", "factory"}, {"name", target}}.dump(), &code);
        rig.settle();
        report.check(code == 200 && rig.engine.comp.name == target, "a factory preset loads on the main thread", rig.engine.comp.name);
    }
    rig.callJson("POST", "/api/v1/presets/load", "{\"source\":\"factory\",\"name\":\"No such preset\"}", &code);
    report.check(code == 404, "an unknown preset is refused");
    rig.callJson("POST", "/api/v1/presets/load", "{\"source\":\"user\",\"name\":\"../../etc/passwd\"}", &code);
    report.check(code == 400, "a path in a preset name is refused");

    Composition mine = looks.front();
    mine.name = "ApiSelfTestPreset";
    std::string saveError;
    report.check(saveUserPreset(mine, "ApiSelfTestPreset", saveError), "a user preset can be saved for the test", saveError);
    presets = rig.callJson("GET", "/api/v1/presets");
    bool listed = false;
    for (const json& name : presets.value("user", json::array())) {
        listed = listed || name == "ApiSelfTestPreset";
    }
    report.check(listed, "the user preset is listed");
    rig.callJson("POST", "/api/v1/presets/load", "{\"source\":\"user\",\"name\":\"ApiSelfTestPreset\"}", &code);
    rig.settle();
    report.check(code == 200 && rig.engine.comp.name == "ApiSelfTestPreset", "a user preset loads on the main thread", rig.engine.comp.name);

    {
        std::ofstream broken(fs::path(presetsDirectory()) / "ApiBrokenPreset.json");
        broken << "{ this is not a preset";
    }
    const std::string before = rig.engine.comp.name;
    rig.callJson("POST", "/api/v1/presets/load", "{\"source\":\"user\",\"name\":\"ApiBrokenPreset\"}", &code);
    rig.settle();
    report.check(code == 422 && rig.engine.comp.name == before, "a broken preset is refused and changes nothing");
    deleteUserPreset("ApiSelfTestPreset");
    deleteUserPreset("ApiBrokenPreset");

    json layers = rig.callJson("GET", "/api/v1/layers");
    report.check(layers.contains("layers") && layers["layers"].size() == kLayerCount, "the layers are listed");
    json decks = rig.callJson("GET", "/api/v1/decks");
    report.check(decks.contains("decks") && decks["decks"].size() == kDeckCount, "the decks are listed");
    json clips = rig.callJson("GET", "/api/v1/clips");
    report.check(clips.contains("clips") && !clips["clips"].empty(), "the clip library is listed");
    json effects = rig.callJson("GET", "/api/v1/effects");
    report.check(effects.contains("effects") && !effects["effects"].empty(), "the effect library is listed");
    json outputs = rig.callJson("GET", "/api/v1/outputs");
    report.check(outputs.contains("outputs") && !outputs["outputs"].empty(), "the outputs are listed");

    {
        const double begin = platform::processCpuSeconds();
        const auto started = Clock::now();
        for (int i = 0; i < 2000; ++i) {
            rig.api.update(1.0 / 60.0, 60.0f);
        }
        const double idleMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count() / 2000.0;
        out["updateIdleMs"] = idleMs;
        out["updateIdleCpuSeconds"] = platform::processCpuSeconds() - begin;
        const auto snapStart = Clock::now();
        for (int i = 0; i < 200; ++i) {
            rig.api.buildSnapshot(60.0f);
        }
        const double snapshotMs = std::chrono::duration<double, std::milli>(Clock::now() - snapStart).count() / 200.0;
        out["snapshotMs"] = snapshotMs;
        report.check(snapshotMs < 2.0, "building a snapshot stays cheap", std::to_string(snapshotMs) + " ms");
    }

    {
        const std::uint64_t appliedBefore = rig.api.applied();
        const auto begin = Clock::now();
        const int port = rig.api.port();
        const std::string text = rig.raw("PUT", "/api/v1/controls/master/level", "{\"value\":0.9}", true);
        auto future = std::async(std::launch::async, [port, text] { return httpExchange(port, text, 4000); });
        std::this_thread::sleep_for(std::chrono::milliseconds(1400));
        const HttpExchange stalled = future.get();
        const double waited = std::chrono::duration<double>(Clock::now() - begin).count();
        report.check(stalled.status == 503, "a stalled main thread gives a clear busy answer", std::to_string(stalled.status));
        report.check(waited < 2.5, "the client is not held up for long when the main thread stalls", std::to_string(waited));
        for (int i = 0; i < 4; ++i) {
            rig.frame();
        }
        report.check(rig.api.applied() == appliedBefore && !near(rig.engine.comp.master.level, 0.9), "a command that timed out is not applied later");
    }

    rig.engine.comp.master.level = 0.4f;
    {
        int limited = 0;
        const int port = rig.api.port();
        const std::string text = rig.raw("GET", "/api/v1/status", "", true);
        auto future = std::async(std::launch::async, [&limited, port, text] {
            for (int i = 0; i < 300; ++i) {
                if (httpExchange(port, text, 2000).status == 429) {
                    ++limited;
                }
            }
            return limited;
        });
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            rig.frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        future.get();
        report.check(limited > 0, "a burst of requests is rate limited", std::to_string(limited));
    }

    rig.api.restart();
    rig.api.configure(config);
    rig.settle();
    report.check(rig.api.running(), "the server restarts on request");
    for (int i = 0; i < 6; ++i) {
        const int port = rig.api.port();
        const std::string text = "GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nAuthorization: Bearer 0000\r\n\r\n";
        rig.pump([port, text] { return httpExchange(port, text, 2000); });
    }
    const HttpExchange lockedOut = rig.call("GET", "/api/v1/status");
    report.check(lockedOut.status == 429, "repeated wrong tokens lock the address out", std::to_string(lockedOut.status));

    const std::string oldToken = rig.api.token();
    rig.api.regenerateToken();
    rig.api.configure(config);
    rig.settle();
    report.check(rig.api.running() && rig.api.token() != oldToken, "a new token replaces the old one");
    const HttpExchange fresh = rig.call("GET", "/api/v1/status");
    report.check(fresh.status == 200, "the new token works and the lockout is gone after the restart", std::to_string(fresh.status));
    {
        const int port = rig.api.port();
        const std::string text = "GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nAuthorization: Bearer " + oldToken + "\r\n\r\n";
        report.check(rig.pump([port, text] { return httpExchange(port, text, 3000); }).status == 401, "the old token no longer works");
    }

    const int closedPort = rig.api.port();
    ApiConfig off;
    off.enabled = false;
    rig.api.configure(off);
    report.check(!rig.api.running(), "turning the interface off stops the server");
    report.check(httpExchange(closedPort, "GET / HTTP/1.1\r\nHost: x\r\n\r\n", 500).status == -1, "the port is closed after turning off");
    report.check(rig.api.queued() == 0, "no command stays queued after turning off");

    out["items"] = report.items;
    out["passed"] = report.passed;
    out["failed"] = report.failed;
    logLine("API self test: " + std::to_string(report.passed) + " passed, " + std::to_string(report.failed) + " failed");
    if (!outputPath.empty()) {
        std::ofstream file(outputPath);
        file << out.dump(2);
    }
    return report.failed == 0 ? 0 : 1;
}

namespace {

struct Spread {
    double avg = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

Spread spreadOf(std::vector<double> values) {
    Spread out;
    if (values.empty()) {
        return out;
    }
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    const auto at = [&values](double fraction) {
        return values[std::min(values.size() - 1, static_cast<size_t>(fraction * static_cast<double>(values.size())))];
    };
    out.avg = sum / static_cast<double>(values.size());
    out.p50 = at(0.5);
    out.p95 = at(0.95);
    out.p99 = at(0.99);
    out.max = values.back();
    return out;
}

json spreadJson(const Spread& spread) {
    return json{{"avg", spread.avg}, {"p50", spread.p50}, {"p95", spread.p95}, {"p99", spread.p99}, {"max", spread.max}};
}

}

int runApiBenchmark(App& app, const std::string& spec, const std::string& outputPath) {
    int frames = 240;
    if (!spec.empty()) {
        frames = std::clamp(std::atoi(spec.c_str()), 60, 3000);
    }
    Rig rig(app);
    Engine& engine = rig.engine;
    engine.setGpuTiming(false);
    const std::vector<Composition>& looks = factoryPresets();
    if (!looks.empty()) {
        Composition look = looks.front();
        look.resolution = 0;
        engine.applyComposition(look);
    }
    engine.setResolutionIndex(0);

    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["frames"] = frames;

    ApiConfig on;
    on.enabled = true;
    on.port = 0;

    const auto runPhase = [&](const std::string& name, bool enabled, int clients, bool flood) {
        ApiConfig config = enabled ? on : ApiConfig{};
        rig.api.configure(config);
        for (int i = 0; i < 20; ++i) {
            rig.frame();
            engine.render();
        }
        glFinish();
        std::atomic<bool> stop{false};
        std::atomic<long long> requests{0};
        std::vector<std::thread> workers;
        if (enabled && clients > 0) {
            const int port = rig.api.port();
            const std::string status = rig.raw("GET", "/api/v1/status", "", true);
            const std::string layers = rig.raw("GET", "/api/v1/layers", "", true);
            const std::string controls = rig.raw("GET", "/api/v1/controls", "", true);
            const std::string write = rig.raw("PUT", "/api/v1/controls/layer/2/opacity", "{\"value\":" + std::to_string(engine.comp.layers[1].opacity) + "}", true);
            for (int i = 0; i < clients; ++i) {
                workers.emplace_back([&, port, status, layers, controls, write, i] {
                    int turn = i;
                    while (!stop.load()) {
                        const std::string& pick = turn % 4 == 0 ? status : turn % 4 == 1 ? layers : turn % 4 == 2 ? controls : write;
                        ++turn;
                        httpExchange(port, pick, 1500);
                        ++requests;
                        if (!flood) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }
                });
            }
        }
        std::vector<double> work;
        std::vector<double> update;
        const double cpuStart = platform::processCpuSeconds();
        const auto wallStart = Clock::now();
        auto next = Clock::now();
        for (int i = 0; i < frames; ++i) {
            const auto begin = Clock::now();
            const auto apiBegin = Clock::now();
            rig.api.update(1.0 / 60.0, 60.0f);
            update.push_back(std::chrono::duration<double, std::milli>(Clock::now() - apiBegin).count());
            engine.update(1.0 / 60.0);
            engine.render();
            glFinish();
            work.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
            next += std::chrono::microseconds(16667);
            if (next > Clock::now()) {
                std::this_thread::sleep_until(next);
            } else {
                next = Clock::now();
            }
        }
        const double wall = std::chrono::duration<double>(Clock::now() - wallStart).count();
        const double cpu = platform::processCpuSeconds() - cpuStart;
        stop = true;
        for (std::thread& worker : workers) {
            worker.join();
        }
        json item;
        item["frameMs"] = spreadJson(spreadOf(work));
        item["apiUpdateMs"] = spreadJson(spreadOf(update));
        item["cpuSeconds"] = cpu;
        item["wallSeconds"] = wall;
        item["cpuPercentOfOneCore"] = wall > 0.0 ? 100.0 * cpu / wall : 0.0;
        item["requests"] = static_cast<long long>(requests.load());
        item["requestsPerSecond"] = wall > 0.0 ? static_cast<double>(requests.load()) / wall : 0.0;
        report["phases"][name] = item;
        logLine("API benchmark " + name + ": " + std::to_string(spreadOf(work).avg) + " ms per frame, " + std::to_string(requests.load()) + " requests");
    };

    runPhase("off", false, 0, false);
    runPhase("idle", true, 0, false);
    runPhase("clients", true, 3, false);
    runPhase("flood", true, 6, true);
    rig.api.configure(ApiConfig{});
    runPhase("off again", false, 0, false);

    if (!outputPath.empty()) {
        std::ofstream file(outputPath);
        file << report.dump(2);
    }
    return 0;
}

}

#include "check.hpp"

#include "control/api_hub.hpp"
#include "control/api_page.hpp"
#include "control/api_router.hpp"
#include "control/api_security.hpp"
#include "control/api_server.hpp"
#include "control/control_targets.hpp"
#include "control/http_client.hpp"
#include "control/tcp_socket.hpp"
#include "core/http_message.hpp"
#include "core/log.hpp"
#include "core/settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <cstring>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using namespace nvj;
using namespace nvj::api;

namespace {

HttpParseState parseAll(HttpRequestParser& parser, const std::string& text) {
    return parser.feed(text.data(), text.size());
}

std::string get(const std::string& target, const std::string& host = "127.0.0.1:9020", const std::string& extra = "") {
    return "GET " + target + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra + "\r\n";
}

std::string post(const std::string& target, const std::string& body, const std::string& extra = "", const std::string& method = "POST", const std::string& type = "application/json") {
    return method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1:9020\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body;
}

bool failsWith(const std::string& text, int status) {
    HttpRequestParser parser;
    const HttpParseState state = parseAll(parser, text);
    return state == HttpParseState::Failed && parser.errorStatus() == status;
}

void testParserBasics() {
    HttpRequestParser parser;
    CHECK(parseAll(parser, get("/api/v1/status?a=b", "localhost:9020", "X-Test:  value  \r\n")) == HttpParseState::Complete);
    CHECK(parser.request().method == "GET");
    CHECK(parser.request().path == "/api/v1/status");
    CHECK(parser.request().query == "a=b");
    CHECK(parser.request().version == "HTTP/1.1");
    CHECK(*parser.request().header("host") == "localhost:9020");
    CHECK(*parser.request().header("x-test") == "value");
    CHECK(parser.request().body.empty());

    HttpRequestParser bytewise;
    const std::string text = post("/api/v1/controls/master/level", "{\"value\":0.5}");
    HttpParseState state = HttpParseState::NeedMore;
    for (size_t i = 0; i < text.size(); ++i) {
        CHECK(state == HttpParseState::NeedMore);
        state = bytewise.feed(text.data() + i, 1);
    }
    CHECK(state == HttpParseState::Complete);
    CHECK(bytewise.request().body == "{\"value\":0.5}");
    CHECK(bytewise.request().path == "/api/v1/controls/master/level");

    HttpRequestParser pipelined;
    const std::string two = get("/a") + get("/b");
    CHECK(parseAll(pipelined, two) == HttpParseState::Complete);
    CHECK(pipelined.request().path == "/a");
    CHECK(pipelined.extraBytes() == get("/b").size());

    HttpRequestParser old;
    CHECK(parseAll(old, "GET / HTTP/1.0\r\n\r\n") == HttpParseState::Complete);

    HttpRequestParser encoded;
    CHECK(parseAll(encoded, get("/api/v1/presets%20x")) == HttpParseState::Complete);
    CHECK(encoded.request().path == "/api/v1/presets x");

    HttpRequestParser partial;
    CHECK(parseAll(partial, "POST /x HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\n12345") == HttpParseState::NeedMore);
    CHECK(partial.headersComplete());
    CHECK(parseAll(partial, "67890") == HttpParseState::Complete);
    CHECK(partial.request().body == "1234567890");
}

void testParserRejects() {
    CHECK(failsWith("GET / HTTP/1.1\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost:\r\n\r\n", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 501));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 501));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding : chunked\r\nContent-Length: 4\r\n\r\n0\r\n\r\n", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\nContent-Length: 1\r\n\r\nx", 417));
    CHECK(failsWith("GET / HTTP/1.1\nHost: a\n\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\nX: b\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\rX: b\r\n\r\n", 400));
    CHECK(failsWith(std::string("GET / HTTP/1.1\r\nHost: a\r\nX: b") + '\0' + "c\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost : a\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\n folded: value\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\nno colon here\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\n: empty\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\nX: a\x01" "b\r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/2.0\r\nHost: a\r\n\r\n", 505));
    CHECK(failsWith("GET / HTTX/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET  / HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("get / HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET http://evil/ HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET //evil/x HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a/../b HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a/./b HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /%2e%2e/b HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a%2fb HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a%5cb HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a%00b HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a%0d%0ab HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a%zz HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a% HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a%4 HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a\\b HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a#frag HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /a\xc3\xa4 HTTP/1.1\r\nHost: a\r\n\r\n", 400));
    CHECK(failsWith("GET /" + std::string(3000, 'a') + " HTTP/1.1\r\nHost: a\r\n\r\n", 414));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\nX: " + std::string(9000, 'a') + "\r\n\r\n", 431));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\nX: " + std::string(9000, 'a'), 431));
    std::string many = "GET / HTTP/1.1\r\nHost: a\r\n";
    for (int i = 0; i < 60; ++i) {
        many += "X" + std::to_string(i) + ": 1\r\n";
    }
    CHECK(failsWith(many + "\r\n", 431));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 70000\r\n\r\n", 413));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999\r\n\r\n", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: +5\r\n\r\n12345", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5 5\r\n\r\n12345", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 0x5\r\n\r\n12345", 400));
    CHECK(failsWith("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: \r\n\r\n", 400));
    CHECK(failsWith("GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\n\r\nabcd", 400));
    CHECK(failsWith("DELETE / HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\n\r\nabcd", 400));

    HttpRequestParser sticky;
    CHECK(parseAll(sticky, "garbage\r\n\r\n") == HttpParseState::Failed);
    CHECK(parseAll(sticky, get("/")) == HttpParseState::Failed);
}

void testParserBounded() {
    HttpRequestParser parser;
    const std::string chunk(1000, 'a');
    HttpParseState state = parseAll(parser, "GET / HTTP/1.1\r\nHost: a\r\nX: ");
    size_t fed = 0;
    while (state == HttpParseState::NeedMore && fed < 1000000) {
        state = parser.feed(chunk.data(), chunk.size());
        fed += chunk.size();
        CHECK(parser.buffered() <= kHttpMaxHeaderBytes + 2000);
    }
    CHECK(state == HttpParseState::Failed);
    CHECK(fed < 20000);
}

void testParserFuzz() {
    std::mt19937 rng(12345);
    const std::string seeds[] = {
        get("/api/v1/status", "127.0.0.1:9020", "Authorization: Bearer abc\r\n"),
        post("/api/v1/controls/master/level", "{\"value\":0.25}"),
        post("/api/v1/presets/load", "{\"source\":\"factory\",\"name\":\"x\"}", "Origin: http://127.0.0.1:9020\r\n"),
    };
    int completed = 0;
    int failed = 0;
    for (int round = 0; round < 6000; ++round) {
        std::string text = seeds[static_cast<size_t>(round) % 3];
        const int mutations = 1 + static_cast<int>(rng() % 6);
        for (int i = 0; i < mutations && !text.empty(); ++i) {
            const size_t position = rng() % text.size();
            switch (rng() % 5) {
            case 0:
                text[position] = static_cast<char>(rng() % 256);
                break;
            case 1:
                text.erase(position, 1 + rng() % 4);
                break;
            case 2:
                text.insert(position, std::string(1 + rng() % 3, static_cast<char>(rng() % 256)));
                break;
            case 3:
                text.insert(position, "\r\n");
                break;
            default:
                text.resize(position);
                break;
            }
        }
        HttpRequestParser parser;
        size_t offset = 0;
        HttpParseState state = HttpParseState::NeedMore;
        while (offset < text.size() && state == HttpParseState::NeedMore) {
            const size_t step = 1 + rng() % 40;
            const size_t take = std::min(step, text.size() - offset);
            state = parser.feed(text.data() + offset, take);
            offset += take;
        }
        if (state == HttpParseState::Complete) {
            ++completed;
            const HttpRequest& request = parser.request();
            CHECK(!request.method.empty());
            CHECK(!request.path.empty() && request.path[0] == '/');
            size_t from = 0;
            while (from <= request.path.size()) {
                size_t to = request.path.find('/', from);
                if (to == std::string::npos) {
                    to = request.path.size();
                }
                const std::string segment = request.path.substr(from, to - from);
                CHECK(segment != ".." && segment != ".");
                from = to + 1;
            }
            CHECK(request.body.size() <= kHttpMaxBodyBytes);
        } else if (state == HttpParseState::Failed) {
            ++failed;
            CHECK(parser.errorStatus() >= 400);
        }
        CHECK(parser.buffered() <= kHttpMaxHeaderBytes + 4096 + kHttpMaxBodyBytes);
    }
    CHECK(completed > 100);
    CHECK(failed > 100);

    for (int round = 0; round < 2000; ++round) {
        std::string noise(1 + rng() % 600, '\0');
        for (char& c : noise) {
            c = static_cast<char>(rng() % 256);
        }
        HttpRequestParser parser;
        parser.feed(noise.data(), noise.size());
        CHECK(parser.state() != HttpParseState::Complete || !parser.request().method.empty());
    }
}

void testHttpHelpers() {
    HttpResponse response;
    response.status = 200;
    response.body = "hi";
    response.setHeader("X-Test", "a\r\nInjected: yes");
    response.setHeader("Content-Length", "999");
    response.setHeader("Connection", "keep-alive");
    const std::string text = serializeHttpResponse(response);
    CHECK(text.compare(0, 15, "HTTP/1.1 200 OK") == 0);
    CHECK(text.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(text.find("Content-Length: 999") == std::string::npos);
    CHECK(text.find("Connection: close\r\n") != std::string::npos);
    CHECK(text.find("keep-alive") == std::string::npos);
    CHECK(text.find("\r\nInjected") == std::string::npos);
    CHECK(text.size() >= 2 && text.substr(text.size() - 2) == "hi");

    std::string out;
    CHECK(decodePercent("a%20b", out) && out == "a b");
    CHECK(!decodePercent("a%2", out));
    CHECK(!decodePercent("a%2F", out));
    CHECK(lowerAscii("AbC-Z") == "abc-z");
    CHECK(trimHttpSpace(" \t x y \t") == "x y");
}

void testSecurity() {
    CHECK(constantTimeEqual("abc", "abc"));
    CHECK(!constantTimeEqual("abc", "abd"));
    CHECK(!constantTimeEqual("abc", "abcd"));
    CHECK(!constantTimeEqual("", "a"));
    CHECK(constantTimeEqual("", ""));

    std::string first;
    std::string second;
    CHECK(generateToken(first));
    CHECK(generateToken(second));
    CHECK(first.size() == kTokenChars);
    CHECK(validTokenFormat(first));
    CHECK(first != second);
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i) {
        std::string token;
        CHECK(generateToken(token));
        seen.insert(token);
    }
    CHECK(seen.size() == 200);
    int ones = 0;
    for (const std::string& token : seen) {
        for (const char c : token) {
            const int digit = c <= '9' ? c - '0' : c - 'a' + 10;
            ones += (digit & 1) + ((digit >> 1) & 1) + ((digit >> 2) & 1) + ((digit >> 3) & 1);
        }
    }
    const double ratio = static_cast<double>(ones) / (200.0 * kTokenChars * 4.0);
    CHECK(ratio > 0.45 && ratio < 0.55);

    CHECK(!validTokenFormat(""));
    CHECK(!validTokenFormat(std::string(63, 'a')));
    CHECK(!validTokenFormat(std::string(65, 'a')));
    CHECK(!validTokenFormat(std::string(63, 'a') + "G"));
    CHECK(validTokenFormat(std::string(64, 'f')));

    std::string token;
    CHECK(parseBearer("Bearer abc", token) && token == "abc");
    CHECK(parseBearer("bearer   abc  ", token) && token == "abc");
    CHECK(!parseBearer("Basic abc", token));
    CHECK(!parseBearer("Bearer", token));
    CHECK(!parseBearer("Bearer ", token));
    CHECK(!parseBearer("", token));

    CHECK(hostAllowed("127.0.0.1:9020", 9020, false));
    CHECK(hostAllowed("localhost:9020", 9020, false));
    CHECK(hostAllowed("LOCALHOST:9020", 9020, false));
    CHECK(hostAllowed("[::1]:9020", 9020, false));
    CHECK(hostAllowed("127.5.5.5:9020", 9020, false));
    CHECK(!hostAllowed("127.0.0.1:9021", 9020, false));
    CHECK(!hostAllowed("127.0.0.1", 9020, false));
    CHECK(hostAllowed("127.0.0.1", 80, false));
    CHECK(!hostAllowed("evil.example:9020", 9020, false));
    CHECK(!hostAllowed("evil.example:9020", 9020, true));
    CHECK(!hostAllowed("localhost.evil.example:9020", 9020, false));
    CHECK(!hostAllowed("127.0.0.1.evil.example:9020", 9020, true));
    CHECK(!hostAllowed("192.168.1.20:9020", 9020, false));
    CHECK(hostAllowed("192.168.1.20:9020", 9020, true));
    CHECK(!hostAllowed("192.168.1.256:9020", 9020, true));
    CHECK(!hostAllowed("192.168.1:9020", 9020, true));
    CHECK(!hostAllowed("192.168.1.20:", 9020, true));
    CHECK(!hostAllowed("192.168.1.20:abc", 9020, true));
    CHECK(!hostAllowed("192.168.1.20:99999", 9020, true));
    CHECK(!hostAllowed("[fe80::1]:9020", 9020, true));
    CHECK(!hostAllowed("", 9020, true));
    CHECK(!hostAllowed(":9020", 9020, true));
    CHECK(!hostAllowed("a b:9020", 9020, true));

    const std::string host = "127.0.0.1:9020";
    const std::string same = "http://127.0.0.1:9020";
    const std::string other = "http://evil.example";
    const std::string https = "https://127.0.0.1:9020";
    const std::string null = "null";
    CHECK(originAllowed(nullptr, nullptr, host));
    CHECK(originAllowed(&same, nullptr, host));
    CHECK(!originAllowed(&other, nullptr, host));
    CHECK(!originAllowed(&https, nullptr, host));
    CHECK(!originAllowed(&null, nullptr, host));
    const std::string sameSite = "same-origin";
    const std::string none = "none";
    const std::string cross = "cross-site";
    const std::string sameSiteOnly = "same-site";
    CHECK(originAllowed(nullptr, &sameSite, host));
    CHECK(originAllowed(nullptr, &none, host));
    CHECK(!originAllowed(nullptr, &cross, host));
    CHECK(!originAllowed(nullptr, &sameSiteOnly, host));
}

void testRateLimiter() {
    RateLimitConfig config;
    config.ratePerSecond = 10.0;
    config.burst = 5.0;
    config.failureLimit = 3;
    config.failureWindow = 10.0;
    config.lockSeconds = 30.0;
    config.maxClients = 8;
    RateLimiter limiter(config);
    double retry = 0.0;
    const std::uint32_t a = 0x0a000001u;
    const std::uint32_t b = 0x0a000002u;
    for (int i = 0; i < 5; ++i) {
        CHECK(limiter.check(a, 100.0, retry) == RateVerdict::Allow);
    }
    CHECK(limiter.check(a, 100.0, retry) == RateVerdict::Limited);
    CHECK(retry > 0.0 && retry <= 0.11);
    CHECK(limiter.check(b, 100.0, retry) == RateVerdict::Allow);
    CHECK(limiter.check(a, 100.2, retry) == RateVerdict::Allow);
    CHECK(limiter.check(a, 100.2, retry) == RateVerdict::Allow);
    CHECK(limiter.check(a, 100.2, retry) == RateVerdict::Limited);
    for (int i = 0; i < 100; ++i) {
        const RateVerdict verdict = limiter.check(a, 200.0 + i * 0.1, retry);
        CHECK(verdict == RateVerdict::Allow);
    }

    double now = 1000.0;
    limiter.recordAuthFailure(a, now);
    limiter.recordAuthFailure(a, now + 1);
    CHECK(limiter.check(a, now + 1.5, retry) == RateVerdict::Allow);
    limiter.recordAuthFailure(a, now + 2);
    CHECK(limiter.check(a, now + 3, retry) == RateVerdict::Locked);
    CHECK(retry > 28.0 && retry <= 30.0);
    CHECK(limiter.check(b, now + 3, retry) == RateVerdict::Allow);
    CHECK(limiter.check(a, now + 31.9, retry) == RateVerdict::Locked);
    CHECK(limiter.check(a, now + 32.1, retry) == RateVerdict::Allow);

    limiter.recordAuthFailure(b, 2000.0);
    limiter.recordAuthFailure(b, 2000.0);
    limiter.recordAuthSuccess(b);
    limiter.recordAuthFailure(b, 2000.0);
    CHECK(limiter.check(b, 2001.0, retry) == RateVerdict::Allow);
    limiter.recordAuthFailure(b, 3000.0);
    limiter.recordAuthFailure(b, 3011.0);
    limiter.recordAuthFailure(b, 3022.0);
    CHECK(limiter.check(b, 3023.0, retry) == RateVerdict::Allow);

    RateLimiter crowded(config);
    for (std::uint32_t i = 0; i < 1000; ++i) {
        crowded.check(0x0b000000u + i, 10.0 + i * 0.001, retry);
        CHECK(crowded.clientCount() <= config.maxClients);
    }
    crowded.recordAuthFailure(0x0c000001u, 50.0);
    crowded.recordAuthFailure(0x0c000001u, 50.0);
    crowded.recordAuthFailure(0x0c000001u, 50.0);
    for (std::uint32_t i = 0; i < 100; ++i) {
        crowded.check(0x0d000000u + i, 51.0, retry);
    }
    CHECK(crowded.check(0x0c000001u, 52.0, retry) == RateVerdict::Locked);
    CHECK(crowded.clientCount() <= config.maxClients + 1);
}

std::shared_ptr<const Snapshot> makeSnapshot() {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->revision = 7;
    snapshot->appVersion = "0.36.0";
    snapshot->composition = "Test";
    snapshot->bpm = 120.0f;
    snapshot->activeDeck = 1;
    snapshot->masterLevel = 0.75f;
    snapshot->fps = 59.5f;
    snapshot->width = 1920;
    snapshot->height = 1080;
    for (int i = 0; i < kDecks; ++i) {
        snapshot->deckNames[static_cast<size_t>(i)] = "Deck " + std::to_string(i + 1);
        snapshot->deckUsed[static_cast<size_t>(i)] = i < 3;
    }
    for (int layer = 0; layer < kLayers; ++layer) {
        LayerInfo& info = snapshot->layers[static_cast<size_t>(layer)];
        info.cells.fill(-1);
        info.cells[0] = 0;
        info.cells[1] = 1;
        info.activeColumn = layer == 0 ? 1 : -1;
        info.opacity = 0.5f;
        info.fx[0].effect = 1;
        info.fx[0].params = {0.1f, 0.2f, 0.3f, 0.4f};
    }
    snapshot->controlValues.resize(controlTargets().size());
    for (size_t i = 0; i < snapshot->controlValues.size(); ++i) {
        snapshot->controlValues[i] = static_cast<float>(i % 11) / 10.0f;
    }
    OutputInfo output;
    output.name = "Main";
    output.enabled = true;
    output.open = true;
    output.ndi = true;
    output.ndiName = "NyrexVJ";
    snapshot->outputs.push_back(output);
    auto library = std::make_shared<Library>();
    library->clips = {{"Plasma", "shader"}, {"Tunnel", "scene"}, {"Clip \"quoted\" <b>", "video"}};
    library->effects = {{"Blur", {"Radius", "", "", ""}, false}, {"Mirror", {"Axis", "Mix", "", ""}, true}};
    library->factoryPresets = {"Warm Up", "Peak"};
    snapshot->library = library;
    return snapshot;
}

struct Fixture {
    Hub hub;
    RouterConfig config;
    std::string token;
    std::unique_ptr<Router> router;
    std::vector<std::string> userPresets{"Mine", "Other"};
    double now = 1000.0;

    Fixture() {
        generateToken(token);
        config.port = 9020;
        config.token = token;
        PresetAccess access;
        access.listUser = [this] { return userPresets; };
        access.readUser = [](const std::string& name, std::string& text, std::string&) {
            text = "{\"name\":\"" + name + "\"}";
            return true;
        };
        router = std::make_unique<Router>(hub, config, access);
        hub.publish(makeSnapshot());
    }

    Reply send(const std::string& raw, std::uint32_t peer = 0x7f000001u) {
        HttpRequestParser parser;
        const HttpParseState state = parser.feed(raw.data(), raw.size());
        CHECK(state == HttpParseState::Complete);
        now += 0.05;
        return router->handle(parser.request(), peer, now);
    }

    std::string auth() const { return "Authorization: Bearer " + token + "\r\n"; }

    HttpResponse finish(Reply reply, const Result& result) {
        if (!reply.ticket) {
            return reply.response;
        }
        HttpResponse response;
        reply.finish(result, response);
        return response;
    }
};

json body(const HttpResponse& response) {
    return json::parse(response.body, nullptr, false);
}

Result okResult(float value) {
    Result result;
    result.ok = true;
    result.status = 200;
    result.value = value;
    return result;
}

void testRouterBasics() {
    Fixture f;
    Reply page = f.send(get("/"));
    CHECK(page.response.status == 200);
    CHECK(page.response.header("Content-Type") != nullptr && page.response.header("Content-Type")->compare(0, 9, "text/html") == 0);
    CHECK(page.response.header("Content-Security-Policy") != nullptr);
    CHECK(page.ticket == nullptr);
    CHECK(f.send(post("/", "{}")).response.status == 405);
    CHECK(f.send(get("/", "evil.example:9020")).response.status == 403);

    Reply missing = f.send(get("/api/v1/status"));
    CHECK(missing.response.status == 401);
    CHECK(missing.response.header("WWW-Authenticate") != nullptr);
    Reply wrong = f.send(get("/api/v1/status", "127.0.0.1:9020", "Authorization: Bearer nope\r\n"));
    CHECK(wrong.response.status == 401);
    Reply basic = f.send(get("/api/v1/status", "127.0.0.1:9020", "Authorization: Basic " + f.token + "\r\n"));
    CHECK(basic.response.status == 401);
    Reply ok = f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth()));
    CHECK(ok.response.status == 200);
    const json status = body(ok.response);
    CHECK(status["app"] == "NyrexVJ");
    CHECK(status["apiVersion"] == 1);
    CHECK(status["version"] == "0.36.0");
    CHECK(status["activeDeck"] == 2);
    CHECK(status["output"]["width"] == 1920);
    CHECK(*ok.response.header("Content-Type") == "application/json; charset=utf-8");
    CHECK(ok.response.header("Cache-Control") != nullptr);
    CHECK(ok.response.header("X-Content-Type-Options") != nullptr);
    CHECK(ok.response.header("Content-Security-Policy") != nullptr);

    Reply noToken = f.send(get("/api/v1/status?token=" + f.token));
    CHECK(noToken.response.status == 401);

    Reply wrongHost = f.send(get("/api/v1/status", "evil.example:9020", f.auth()));
    CHECK(wrongHost.response.status == 403);
    Reply wrongPort = f.send(get("/api/v1/status", "127.0.0.1:1234", f.auth()));
    CHECK(wrongPort.response.status == 403);
    Reply lan = f.send(get("/api/v1/status", "192.168.0.5:9020", f.auth()));
    CHECK(lan.response.status == 403);
    Reply origin = f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth() + "Origin: http://evil.example\r\n"));
    CHECK(origin.response.status == 403);
    Reply ownOrigin = f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth() + "Origin: http://127.0.0.1:9020\r\n"));
    CHECK(ownOrigin.response.status == 200);
    Reply cross = f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth() + "Sec-Fetch-Site: cross-site\r\n"));
    CHECK(cross.response.status == 403);

    for (const std::string method : {"OPTIONS", "DELETE", "PATCH", "HEAD"}) {
        Reply reply = f.send(method + " /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:9020\r\nOrigin: http://evil.example\r\nAccess-Control-Request-Method: GET\r\n\r\n");
        CHECK(reply.response.status == 403);
        Reply plain = f.send(method + " /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:9020\r\n" + f.auth() + "\r\n");
        CHECK(plain.response.status == 405);
        CHECK(plain.response.header("Allow") != nullptr);
    }

    const std::string paths[] = {"/api/v1/status", "/api/v1/controls", "/api/v1/layers", "/api/v1/decks", "/api/v1/clips", "/api/v1/effects", "/api/v1/presets", "/api/v1/outputs", "/api/v1", "/api/v2/status", "/nothing", "/api/v1/", "/api/v1//status", "/api/v1/status/extra"};
    for (const std::string& path : paths) {
        Reply reply = f.send(get(path, "127.0.0.1:9020", f.auth()));
        for (const auto& header : reply.response.headers) {
            CHECK(lowerAscii(header.first).compare(0, 14, "access-control") != 0);
        }
    }
    CHECK(f.send(get("/api/v1/nothing", "127.0.0.1:9020", f.auth())).response.status == 404);
    CHECK(f.send(get("/api/v1/status/extra", "127.0.0.1:9020", f.auth())).response.status == 404);
    CHECK(f.send(get("/api/v2/status", "127.0.0.1:9020", f.auth())).response.status == 404);
    CHECK(f.send(get("/api/v1/", "127.0.0.1:9020", f.auth())).response.status == 404);
    CHECK(f.send(get("/api/v1", "127.0.0.1:9020", f.auth())).response.status == 200);
    CHECK(f.send(post("/api/v1/status", "{}", f.auth())).response.status == 405);
}

void testRouterLockout() {
    Fixture f;
    for (int i = 0; i < 5; ++i) {
        CHECK(f.send(get("/api/v1/status", "127.0.0.1:9020", "Authorization: Bearer bad\r\n")).response.status == 401);
    }
    Reply locked = f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth()));
    CHECK(locked.response.status == 429);
    CHECK(locked.response.header("Retry-After") != nullptr);
    CHECK(body(locked.response)["error"]["code"] == "locked");
    Reply other = f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth()), 0xc0a80105u);
    CHECK(other.response.status == 200);
    f.now += 61.0;
    CHECK(f.send(get("/api/v1/status", "127.0.0.1:9020", f.auth())).response.status == 200);

    Fixture g;
    int limited = 0;
    for (int i = 0; i < 400; ++i) {
        g.now -= 0.0495;
        Reply reply = g.send(get("/api/v1/status", "127.0.0.1:9020", g.auth()));
        if (reply.response.status == 429) {
            ++limited;
            CHECK(body(reply.response)["error"]["code"] == "rate_limited");
        }
    }
    CHECK(limited > 0);
    CHECK(g.router->stats().limited.load() == static_cast<std::uint64_t>(limited));
}

void testRouterReads() {
    Fixture f;
    const std::string auth = f.auth();
    const json controls = body(f.send(get("/api/v1/controls", "127.0.0.1:9020", auth)).response);
    CHECK(controls["controls"].size() == controlTargets().size());
    std::set<std::string> ids;
    for (const json& item : controls["controls"]) {
        ids.insert(item["id"].get<std::string>());
        CHECK(item["value"].is_number());
        const std::string kind = item["kind"];
        CHECK(kind == "continuous" || kind == "toggle" || kind == "trigger");
    }
    CHECK(ids.size() == controlTargets().size());
    for (const ControlTarget& target : controlTargets()) {
        CHECK(ids.count(target.id) == 1);
        Reply single = f.send(get("/api/v1/controls/" + target.id, "127.0.0.1:9020", auth));
        CHECK(single.response.status == 200);
        CHECK(body(single.response)["id"] == target.id);
    }
    CHECK(f.send(get("/api/v1/controls/master/nothing", "127.0.0.1:9020", auth)).response.status == 404);
    CHECK(f.send(get("/api/v1/controls/layer/9/opacity", "127.0.0.1:9020", auth)).response.status == 404);
    const json filtered = body(f.send(get("/api/v1/controls?prefix=layer/2/clip/", "127.0.0.1:9020", auth)).response);
    CHECK(filtered["controls"].size() == 8);
    CHECK(f.send(get("/api/v1/controls?prefix=%00", "127.0.0.1:9020", auth)).response.status == 400);
    CHECK(f.send(get("/api/v1/controls?other=1", "127.0.0.1:9020", auth)).response.status == 400);

    const json layers = body(f.send(get("/api/v1/layers", "127.0.0.1:9020", auth)).response);
    CHECK(layers["layers"].size() == 4);
    CHECK(layers["layers"][0]["clips"][0]["clip"] == "Plasma");
    CHECK(layers["layers"][0]["clips"][2]["clip"].is_null());
    CHECK(layers["layers"][0]["activeColumn"] == 2);
    CHECK(layers["layers"][1]["activeColumn"].is_null());
    CHECK(layers["layers"][0]["effects"][0]["effect"] == "Mirror");
    CHECK(layers["layers"][0]["effects"][1]["effect"].is_null());
    const json layer = body(f.send(get("/api/v1/layers/3", "127.0.0.1:9020", auth)).response);
    CHECK(layer["index"] == 3);
    CHECK(f.send(get("/api/v1/layers/0", "127.0.0.1:9020", auth)).response.status == 404);
    CHECK(f.send(get("/api/v1/layers/5", "127.0.0.1:9020", auth)).response.status == 404);
    CHECK(f.send(get("/api/v1/layers/abc", "127.0.0.1:9020", auth)).response.status == 404);
    CHECK(f.send(get("/api/v1/layers/-1", "127.0.0.1:9020", auth)).response.status == 404);
    CHECK(f.send(get("/api/v1/layers/01x", "127.0.0.1:9020", auth)).response.status == 404);

    const json decks = body(f.send(get("/api/v1/decks", "127.0.0.1:9020", auth)).response);
    CHECK(decks["decks"].size() == 16);
    CHECK(decks["decks"][1]["active"] == true);
    CHECK(decks["decks"][0]["active"] == false);
    CHECK(decks["decks"][2]["used"] == true);
    CHECK(decks["decks"][3]["used"] == false);

    const json clips = body(f.send(get("/api/v1/clips", "127.0.0.1:9020", auth)).response);
    CHECK(clips["clips"].size() == 3);
    CHECK(clips["clips"][2]["name"] == "Clip \"quoted\" <b>");
    const json effects = body(f.send(get("/api/v1/effects", "127.0.0.1:9020", auth)).response);
    CHECK(effects["effects"].size() == 2);
    CHECK(effects["effects"][1]["graph"] == true);
    const json presets = body(f.send(get("/api/v1/presets", "127.0.0.1:9020", auth)).response);
    CHECK(presets["factory"].size() == 2);
    CHECK(presets["user"].size() == 2);
    const json outputs = body(f.send(get("/api/v1/outputs", "127.0.0.1:9020", auth)).response);
    CHECK(outputs["outputs"].size() == 1);
    CHECK(outputs["outputs"][0]["ndiName"] == "NyrexVJ");

    Hub empty;
    RouterConfig config;
    config.token = f.token;
    Router bare(empty, config);
    HttpRequestParser parser;
    const std::string raw = get("/api/v1/status", "127.0.0.1:9020", auth);
    parser.feed(raw.data(), raw.size());
    CHECK(bare.handle(parser.request(), 1, 1.0).response.status == 503);
}

void testRouterWrites() {
    Fixture f;
    const std::string auth = f.auth();
    Reply level = f.send(post("/api/v1/controls/master/level", "{\"value\":0.25}", auth, "PUT"));
    CHECK(level.ticket != nullptr);
    CHECK(f.hub.queued() == 1);
    std::vector<Command> seen;
    f.hub.drain(16, [&](const Command& command) {
        seen.push_back(command);
        return okResult(command.value);
    });
    CHECK(seen.size() == 1);
    CHECK(seen[0].type == CommandType::Control);
    CHECK(controlTargets()[static_cast<size_t>(seen[0].control)].id == "master/level");
    CHECK(seen[0].value == 0.25f);
    Result result;
    CHECK(level.ticket->poll(result));
    const HttpResponse done = f.finish(level, result);
    CHECK(done.status == 200);
    CHECK(body(done)["id"] == "master/level");
    CHECK(body(done)["value"] == 0.25);

    Reply failing = f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth));
    Result failure;
    failure.ok = false;
    failure.status = 409;
    failure.code = "nope";
    failure.message = "refused";
    const HttpResponse failed = f.finish(failing, failure);
    CHECK(failed.status == 409);
    CHECK(body(failed)["error"]["code"] == "nope");
    f.hub.clear();

    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":1.5}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":-0.1}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":\"0.5\"}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":null}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":[1]}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "[0.5]", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "0.5", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":1e999}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth, "POST", "text/plain")).response.status == 415);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth, "POST", "application/x-www-form-urlencoded")).response.status == 415);
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth, "POST", "application/jsonx")).response.status == 415);
    CHECK(f.hub.queued() == 0);

    std::string deep;
    for (int i = 0; i < 200; ++i) {
        deep += "[";
    }
    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":0.5,\"x\":" + deep + "}", auth)).response.status == 400);
    CHECK(f.hub.queued() == 0);

    CHECK(f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth, "POST", "application/json; charset=utf-8")).ticket != nullptr);
    CHECK(f.send(post("/api/v1/controls/master/blackout", "{\"value\":true}", auth)).ticket != nullptr);
    CHECK(f.send(post("/api/v1/controls/master/tap", "{}", auth)).ticket != nullptr);
    CHECK(f.send(post("/api/v1/controls/master/tap", "", auth)).ticket != nullptr);
    CHECK(f.send(post("/api/v1/controls/master/nothing", "{\"value\":1}", auth)).response.status == 404);
    f.hub.clear();

    Reply clip = f.send(post("/api/v1/layers/1/clips/2/trigger", "", auth));
    CHECK(clip.ticket != nullptr);
    Reply empty = f.send(post("/api/v1/layers/1/clips/5/trigger", "", auth));
    CHECK(empty.response.status == 409);
    CHECK(f.send(post("/api/v1/layers/1/clips/9/trigger", "", auth)).response.status == 404);
    CHECK(f.send(post("/api/v1/layers/9/clips/1/trigger", "", auth)).response.status == 404);
    CHECK(f.send(get("/api/v1/layers/1/clips/1/trigger", "127.0.0.1:9020", auth)).response.status == 405);
    Reply clear = f.send(post("/api/v1/layers/2/clear", "", auth));
    CHECK(clear.ticket != nullptr);
    Reply column = f.send(post("/api/v1/columns/3/trigger", "", auth));
    CHECK(column.ticket != nullptr);
    CHECK(f.send(post("/api/v1/columns/9/trigger", "", auth)).response.status == 404);
    Reply deck = f.send(post("/api/v1/decks/2/activate", "", auth));
    CHECK(deck.ticket != nullptr);
    CHECK(f.send(post("/api/v1/decks/9/activate", "", auth)).response.status == 409);
    CHECK(f.send(post("/api/v1/decks/17/activate", "", auth)).response.status == 404);
    CHECK(f.send(post("/api/v1/decks/next", "", auth)).ticket != nullptr);
    CHECK(f.send(post("/api/v1/decks/previous", "", auth)).ticket != nullptr);
    std::vector<std::string> triggered;
    f.hub.drain(64, [&](const Command& command) {
        triggered.push_back(controlTargets()[static_cast<size_t>(command.control)].id);
        return okResult(0.0f);
    });
    const std::vector<std::string> expected = {"layer/1/clip/2", "layer/2/clear", "column/3", "deck/2", "deck/next", "deck/previous"};
    CHECK(triggered == expected);

    Reply factory = f.send(post("/api/v1/presets/load", "{\"source\":\"factory\",\"name\":\"Peak\"}", auth));
    CHECK(factory.ticket != nullptr);
    Reply user = f.send(post("/api/v1/presets/load", "{\"source\":\"user\",\"name\":\"Mine\"}", auth));
    CHECK(user.ticket != nullptr);
    std::vector<Command> loads;
    f.hub.drain(8, [&](const Command& command) {
        loads.push_back(command);
        return okResult(0.0f);
    });
    CHECK(loads.size() == 2);
    CHECK(loads[0].type == CommandType::LoadFactoryPreset && loads[0].name == "Peak");
    CHECK(loads[1].type == CommandType::LoadUserPreset && loads[1].name == "Mine");
    CHECK(loads[1].text == "{\"name\":\"Mine\"}");
    CHECK(f.send(post("/api/v1/presets/load", "{\"source\":\"factory\",\"name\":\"Nope\"}", auth)).response.status == 404);
    CHECK(f.send(post("/api/v1/presets/load", "{\"source\":\"user\",\"name\":\"Nope\"}", auth)).response.status == 404);
    CHECK(f.send(post("/api/v1/presets/load", "{\"source\":\"disk\",\"name\":\"Mine\"}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/presets/load", "{\"source\":\"user\"}", auth)).response.status == 400);
    CHECK(f.send(post("/api/v1/presets/load", "{\"source\":\"user\",\"name\":5}", auth)).response.status == 400);
    const std::vector<std::string> badNames = {"../etc/passwd", "..\\\\..\\\\x", "a/b", "", "C:\\\\x", "x\\u0000y", ".", "..", " Mine", std::string(200, 'a')};
    for (const std::string& name : badNames) {
        const std::string request = "{\"source\":\"user\",\"name\":\"" + name + "\"}";
        Reply reply = f.send(post("/api/v1/presets/load", request, auth));
        CHECK(reply.ticket == nullptr);
        CHECK(reply.response.status == 400 || reply.response.status == 404);
    }
    CHECK(f.hub.queued() == 0);
}

void testHubQueue() {
    Fixture f;
    const std::string auth = f.auth();
    std::vector<std::shared_ptr<Ticket>> tickets;
    for (size_t i = 0; i < kMaxQueuedCommands; ++i) {
        Reply reply = f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth));
        CHECK(reply.ticket != nullptr);
        tickets.push_back(reply.ticket);
        f.now += 1.0;
    }
    Reply overflow = f.send(post("/api/v1/controls/master/level", "{\"value\":0.5}", auth));
    CHECK(overflow.response.status == 503);
    CHECK(overflow.response.header("Retry-After") != nullptr);
    CHECK(f.router->stats().busy.load() >= 1);

    CHECK(tickets[0]->cancel());
    CHECK(tickets[1]->cancel());
    int applied = f.hub.drain(10, [&](const Command&) { return okResult(0.5f); });
    CHECK(applied == 10);
    CHECK(f.hub.queued() == kMaxQueuedCommands - 12);
    CHECK(tickets[0]->state() == Ticket::State::Cancelled);
    CHECK(tickets[2]->state() == Ticket::State::Done);
    CHECK(!tickets[2]->cancel());
    f.hub.clear();
    CHECK(f.hub.queued() == 0);
    CHECK(tickets[20]->state() == Ticket::State::Cancelled);

    Ticket ticket;
    CHECK(ticket.begin());
    CHECK(!ticket.cancel());
    Result result;
    CHECK(!ticket.poll(result));
    ticket.complete(okResult(2.0f));
    CHECK(ticket.poll(result) && result.value == 2.0f);
}

void testHubThreads() {
    Hub hub;
    std::atomic<bool> stop{false};
    std::atomic<int> applied{0};
    std::thread main([&] {
        while (!stop.load()) {
            applied += hub.drain(16, [](const Command& command) { return okResult(command.value); });
            hub.publish(makeSnapshot());
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        applied += hub.drain(1000, [](const Command& command) { return okResult(command.value); });
    });
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<int> problems{0};
    std::vector<std::thread> clients;
    for (int t = 0; t < 4; ++t) {
        clients.emplace_back([&] {
            for (int i = 0; i < 500; ++i) {
                Command command;
                command.control = 0;
                command.value = 0.5f;
                auto ticket = hub.submit(command);
                if (!ticket) {
                    ++rejected;
                    continue;
                }
                ++accepted;
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                Result result;
                while (!ticket->poll(result) && std::chrono::steady_clock::now() < until) {
                    std::this_thread::yield();
                }
                if (!ticket->poll(result)) {
                    ++problems;
                }
                auto snapshot = hub.snapshot();
                if (snapshot != nullptr && snapshot->revision != 7) {
                    ++problems;
                }
            }
        });
    }
    for (std::thread& client : clients) {
        client.join();
    }
    stop = true;
    main.join();
    CHECK(problems.load() == 0);
    CHECK(accepted.load() + rejected.load() == 2000);
    CHECK(applied.load() == accepted.load());
}

struct Exchange {
    std::string text;
    bool closed = false;
    int status = 0;
    std::string bodyText;
};

Exchange talk(int port, const std::string& request, int timeoutMs = 3000, bool halfClose = false) {
    const HttpExchange raw = httpExchange(port, request, timeoutMs, halfClose);
    Exchange out;
    out.text = raw.text;
    out.closed = raw.closed;
    out.status = raw.status;
    out.bodyText = raw.body;
    return out;
}

bool waitForIdle(const Server& server, int timeoutMs = 2000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < until) {
        if (server.stats().connections == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return server.stats().connections == 0;
}

struct LiveServer {
    Hub hub;
    RouterConfig config;
    std::unique_ptr<Router> router;
    Server server;
    std::string token;
    std::atomic<bool> stopMain{false};
    std::thread mainThread;
    std::atomic<int> applied{0};
    ServerConfig serverConfig;

    explicit LiveServer(bool withMainThread, double commandSeconds = 1.0, size_t perAddress = 32) {
        generateToken(token);
        config.token = token;
        config.limits.ratePerSecond = 100000.0;
        config.limits.burst = 100000.0;
        serverConfig.port = 0;
        serverConfig.commandSeconds = commandSeconds;
        serverConfig.headerSeconds = 0.6;
        serverConfig.requestSeconds = 1.2;
        serverConfig.idlePollMs = 10;
        serverConfig.maxPerAddress = perAddress;
        serverConfig.maxConnections = 64;
        hub.publish(makeSnapshot());
        PresetAccess access;
        access.listUser = [] { return std::vector<std::string>{"Mine"}; };
        access.readUser = [](const std::string&, std::string& text, std::string&) {
            text = "{}";
            return true;
        };
        router = std::make_unique<Router>(hub, config, access);
        std::string error;
        CHECK(server.start(serverConfig, *router, error));
        if (withMainThread) {
            mainThread = std::thread([this] {
                while (!stopMain.load()) {
                    applied += hub.drain(16, [](const Command& command) { return okResult(command.value); });
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
        }
    }

    ~LiveServer() {
        server.stop();
        stopMain = true;
        if (mainThread.joinable()) {
            mainThread.join();
        }
    }

    int port() const { return server.port(); }

    std::string hostHeader() const { return "127.0.0.1:" + std::to_string(server.port()); }

    std::string request(const std::string& method, const std::string& target, const std::string& payload = "", bool authorized = true) const {
        std::string text = method + " " + target + " HTTP/1.1\r\nHost: " + hostHeader() + "\r\n";
        if (authorized) {
            text += "Authorization: Bearer " + token + "\r\n";
        }
        if (!payload.empty()) {
            text += "Content-Type: application/json\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n";
        }
        return text + "\r\n" + payload;
    }
};

void testServerBasics() {
    LiveServer live(true);
    CHECK(live.server.running());
    CHECK(live.port() > 0);
    Exchange status = talk(live.port(), live.request("GET", "/api/v1/status"));
    CHECK(status.status == 200);
    CHECK(status.closed);
    CHECK(json::parse(status.bodyText)["app"] == "NyrexVJ");
    CHECK(status.text.find("Connection: close") != std::string::npos);

    CHECK(talk(live.port(), live.request("GET", "/api/v1/status", "", false)).status == 401);
    Exchange page = talk(live.port(), live.request("GET", "/", "", false));
    CHECK(page.status == 200);
    CHECK(page.text.find("Content-Security-Policy") != std::string::npos);

    Exchange set = talk(live.port(), live.request("PUT", "/api/v1/controls/master/level", "{\"value\":0.4}"));
    CHECK(set.status == 200);
    CHECK(json::parse(set.bodyText)["value"] == 0.4);
    CHECK(live.applied.load() >= 1);

    Exchange halfClosed = talk(live.port(), live.request("GET", "/api/v1/status"), 3000, true);
    CHECK(halfClosed.status == 200);

    Exchange wrongHost = talk(live.port(), "GET /api/v1/status HTTP/1.1\r\nHost: evil.example\r\nAuthorization: Bearer " + live.token + "\r\n\r\n");
    CHECK(wrongHost.status == 403);

    Exchange garbage = talk(live.port(), std::string("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03\r\n\r\n", 15));
    CHECK(garbage.status == 400);
    Exchange bigHeader = talk(live.port(), "GET / HTTP/1.1\r\nHost: a\r\nX: " + std::string(200000, 'a') + "\r\n\r\n");
    CHECK(bigHeader.status == 431);
    Exchange bigBody = talk(live.port(), "POST /api/v1/controls/master/level HTTP/1.1\r\nHost: a\r\nContent-Length: 500000\r\n\r\n" + std::string(100000, 'a'));
    CHECK(bigBody.status == 413);
    Exchange smuggle = talk(live.port(), "POST /api/v1/controls/master/level HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\nGET /x HTTP/1.1\r\n\r\n");
    CHECK(smuggle.status == 501);
    CHECK(smuggle.text.find("HTTP/1.1", 12) == std::string::npos);

    Exchange pipelined = talk(live.port(), live.request("GET", "/api/v1/status") + live.request("GET", "/api/v1/outputs"));
    CHECK(pipelined.status == 200);
    CHECK(pipelined.text.find("HTTP/1.1", 12) == std::string::npos);
    CHECK(pipelined.bodyText.find("\"outputs\"") == std::string::npos);

    for (int i = 0; i < 200; ++i) {
        Exchange quick = talk(live.port(), live.request("GET", "/api/v1/status"));
        CHECK(quick.status == 200 || quick.status == 429);
    }
    CHECK(waitForIdle(live.server));
}

void testServerStalledMain() {
    LiveServer live(false, 0.25);
    const auto started = std::chrono::steady_clock::now();
    std::atomic<int> reads{0};
    std::thread reader([&] {
        for (int i = 0; i < 20; ++i) {
            Exchange status = talk(live.port(), live.request("GET", "/api/v1/status"));
            if (status.status == 200) {
                ++reads;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    Exchange set = talk(live.port(), live.request("POST", "/api/v1/controls/master/level", "{\"value\":0.4}"));
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    reader.join();
    CHECK(set.status == 503);
    CHECK(set.text.find("Retry-After") != std::string::npos);
    CHECK(elapsed > 0.2 && elapsed < 1.5);
    CHECK(reads.load() == 20);
    CHECK(live.hub.queued() == 1);
    const int applied = live.hub.drain(8, [](const Command&) { return okResult(0.0f); });
    CHECK(applied == 0);
    CHECK(live.hub.queued() == 0);

    std::vector<std::thread> flood;
    std::atomic<int> busy{0};
    std::atomic<int> other{0};
    for (int t = 0; t < 6; ++t) {
        flood.emplace_back([&] {
            for (int i = 0; i < 30; ++i) {
                Exchange reply = talk(live.port(), live.request("POST", "/api/v1/controls/master/level", "{\"value\":0.4}"), 3000);
                if (reply.status == 503) {
                    ++busy;
                } else {
                    ++other;
                }
            }
        });
    }
    for (std::thread& thread : flood) {
        thread.join();
    }
    CHECK(busy.load() + other.load() == 180);
    CHECK(busy.load() == 180);
    CHECK(live.hub.queued() <= kMaxQueuedCommands);
}

void testServerAbuse() {
    LiveServer live(true, 1.0, 8);
    std::vector<std::unique_ptr<TcpConnection>> idle;
    std::string error;
    for (int i = 0; i < 60; ++i) {
        std::unique_ptr<TcpConnection> connection = tcpConnectLoopback(live.port(), error);
        if (connection) {
            idle.push_back(std::move(connection));
        }
    }
    CHECK(idle.size() == 60);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ServerStats stats = live.server.stats();
    CHECK(stats.connections <= 8);
    CHECK(stats.refused >= 40);
    Exchange blocked = talk(live.port(), live.request("GET", "/api/v1/status"), 400);
    CHECK(blocked.status == 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    stats = live.server.stats();
    CHECK(stats.timeouts >= 1);
    idle.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Exchange recovered = talk(live.port(), live.request("GET", "/api/v1/status"));
    CHECK(recovered.status == 200);

    std::unique_ptr<TcpConnection> slow = tcpConnectLoopback(live.port(), error);
    CHECK(slow != nullptr);
    const std::string head = "GET /api/v1/status HTTP/1.1\r\nHost: " + live.hostHeader() + "\r\n";
    slow->send(head.data(), static_cast<int>(head.size()));
    std::string collected;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    bool closed = false;
    while (std::chrono::steady_clock::now() < until && !closed) {
        char buffer[512];
        const int got = slow->receive(buffer, sizeof(buffer));
        if (got > 0) {
            collected.append(buffer, static_cast<size_t>(got));
        } else if (got < 0) {
            closed = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(collected.compare(0, 12, "HTTP/1.1 408") == 0);
    CHECK(closed);

    for (int i = 0; i < 50; ++i) {
        std::unique_ptr<TcpConnection> broken = tcpConnectLoopback(live.port(), error);
        CHECK(broken != nullptr);
        const std::string part = "POST /api/v1/controls/master/level HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\n{\"va";
        broken->send(part.data(), static_cast<int>(part.size()));
        broken->close();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(talk(live.port(), live.request("GET", "/api/v1/status")).status == 200);
    CHECK(waitForIdle(live.server));

    std::string many;
    for (int i = 0; i < 40; ++i) {
        many += live.request("GET", "/api/v1/status");
    }
    CHECK(talk(live.port(), many).status == 200);

    Server second;
    std::string startError;
    Router& active = *live.router;
    ServerConfig clash;
    clash.port = live.port();
    CHECK(!second.start(clash, active, startError));
    CHECK(!startError.empty());
    CHECK(!second.running());
}

void testServerConcurrency() {
    LiveServer live(true);
    std::atomic<int> good{0};
    std::atomic<int> limited{0};
    std::atomic<int> bad{0};
    std::vector<std::thread> clients;
    for (int t = 0; t < 8; ++t) {
        clients.emplace_back([&, t] {
            for (int i = 0; i < 40; ++i) {
                const bool write = (i + t) % 2 == 0;
                Exchange reply = write ? talk(live.port(), live.request("POST", "/api/v1/controls/master/level", "{\"value\":0.5}"))
                                       : talk(live.port(), live.request("GET", "/api/v1/layers"));
                if (reply.status == 200) {
                    ++good;
                } else if (reply.status == 429 || reply.status == 0) {
                    ++limited;
                } else {
                    ++bad;
                }
            }
        });
    }
    for (std::thread& client : clients) {
        client.join();
    }
    CHECK(bad.load() == 0);
    CHECK(good.load() > 100);
    CHECK(good.load() + limited.load() == 320);
}

void testServerRestartAndStop() {
    Hub hub;
    hub.publish(makeSnapshot());
    RouterConfig config;
    generateToken(config.token);
    std::string error;
    Router router(hub, config);
    Server server;
    ServerConfig serverConfig;
    serverConfig.port = 0;
    serverConfig.idlePollMs = 10;
    for (int round = 0; round < 5; ++round) {
        CHECK(server.start(serverConfig, router, error));
        CHECK(server.running());
        const int port = server.port();
        const std::string request = "GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nAuthorization: Bearer " + config.token + "\r\n\r\n";
        CHECK(talk(port, request).status == 200);
        server.stop();
        CHECK(!server.running());
        std::unique_ptr<TcpConnection> refused = tcpConnectLoopback(port, error);
        CHECK(refused == nullptr);
    }
    CHECK(server.start(serverConfig, router, error));
    const std::string request = "POST /api/v1/controls/master/level HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(server.port()) + "\r\nAuthorization: Bearer " + config.token + "\r\nContent-Type: application/json\r\nContent-Length: 13\r\n\r\n{\"value\":0.5}";
    std::unique_ptr<TcpConnection> waiting = tcpConnectLoopback(server.port(), error);
    CHECK(waiting != nullptr);
    waiting->send(request.data(), static_cast<int>(request.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto begin = std::chrono::steady_clock::now();
    server.stop();
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() < 1.0);
    CHECK(hub.queued() == 1);
    hub.clear();
}

}

std::string assetBody(const std::string& path, std::string* type = nullptr) {
    const PageAsset* asset = findPageAsset(path);
    if (asset == nullptr) {
        return std::string();
    }
    if (type != nullptr) {
        *type = asset->type;
    }
    return asset->body;
}

void testPageAssets() {
    std::string type;
    const std::string html = assetBody("/", &type);
    CHECK(!html.empty() && type.compare(0, 9, "text/html") == 0);
    const std::string script = assetBody("/app.js", &type);
    CHECK(!script.empty() && type.compare(0, 15, "text/javascript") == 0);
    const std::string style = assetBody("/app.css", &type);
    CHECK(!style.empty() && type.compare(0, 8, "text/css") == 0);
    const std::string lang = assetBody("/lang.json", &type);
    CHECK(!lang.empty() && type.compare(0, 16, "application/json") == 0);
    CHECK(findPageAsset("/nothing") == nullptr);
    CHECK(findPageAsset("/api/v1/status") == nullptr);
    CHECK(findPageAsset("//") == nullptr);
    CHECK(findPageAsset("/app.js/") == nullptr);

    CHECK(html.find("<script src=\"/app.js\"") != std::string::npos);
    CHECK(html.find("<link rel=\"stylesheet\" href=\"/app.css\">") != std::string::npos);
    size_t scripts = 0;
    for (size_t at = html.find("<script"); at != std::string::npos; at = html.find("<script", at + 1)) {
        ++scripts;
        const size_t close = html.find('>', at);
        CHECK(close != std::string::npos && html.substr(at, close - at).find("src=") != std::string::npos);
    }
    CHECK(scripts == 1);
    for (const char* forbidden : {" onclick=", " onload=", " onerror=", " style=", "<style", "javascript:", "http://", "https://"}) {
        CHECK(html.find(forbidden) == std::string::npos);
        CHECK(style.find(forbidden) == std::string::npos);
    }
    CHECK(style.find("@import") == std::string::npos);
    CHECK(style.find("url(") == std::string::npos);

    for (const char* forbidden : {"eval(", "new Function", "innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "setAttribute(\"style\"", "XMLHttpRequest", "WebSocket", "importScripts", "localStorage", "document.cookie", "http://", "https://"}) {
        CHECK(script.find(forbidden) == std::string::npos);
    }
    CHECK(script.find("setTimeout(\"") == std::string::npos);
    CHECK(script.find("setInterval(\"") == std::string::npos);
    CHECK(script.find("//") == std::string::npos);
    CHECK(script.find("/*") == std::string::npos);
    CHECK(script.find("sessionStorage") != std::string::npos);
    CHECK(script.find("history.replaceState") != std::string::npos);
    CHECK(script.find("location.hash") != std::string::npos);
    CHECK(script.find("location.search") == std::string::npos);
    CHECK(script.find("console.") == std::string::npos);

    const size_t total = html.size() + script.size() + style.size() + lang.size();
    CHECK(total < 64 * 1024);
}

void testPageLanguages() {
    const json all = json::parse(pageLanguageJson(), nullptr, false);
    CHECK(!all.is_discarded() && all.is_object());
    const std::vector<std::string> expected = {"en", "de", "es", "fr", "it", "pt", "nl", "ja"};
    CHECK(all.size() == expected.size());
    for (const std::string& code : expected) {
        CHECK(all.contains(code) && all[code].is_object());
    }
    const json& english = all["en"];
    std::set<std::string> keys;
    for (auto it = english.begin(); it != english.end(); ++it) {
        keys.insert(it.key());
        CHECK(it.value().is_string() && !it.value().get<std::string>().empty());
    }
    CHECK(keys.size() >= 40);
    for (const std::string& code : expected) {
        std::set<std::string> own;
        for (auto it = all[code].begin(); it != all[code].end(); ++it) {
            own.insert(it.key());
            CHECK(it.value().is_string() && !it.value().get<std::string>().empty());
            CHECK(it.value().get<std::string>().find('<') == std::string::npos);
            CHECK(it.value().get<std::string>().find('>') == std::string::npos);
        }
        CHECK(own == keys);
        CHECK(all[code]["loaded"].get<std::string>().find("{name}") != std::string::npos);
        if (code != "en") {
            size_t same = 0;
            for (const std::string& key : keys) {
                if (all[code][key] == english[key]) {
                    ++same;
                }
            }
            CHECK(same < keys.size() / 2);
        }
    }

    const std::string script = assetBody("/app.js");
    const std::regex use("\\bt\\(\"([A-Za-z]+)\"");
    std::set<std::string> used;
    for (std::sregex_iterator it(script.begin(), script.end(), use), end; it != end; ++it) {
        used.insert((*it)[1].str());
    }
    CHECK(used.size() >= 30);
    for (const std::string& key : used) {
        CHECK(keys.count(key) == 1);
    }
    for (const std::string& key : keys) {
        CHECK(used.count(key) == 1);
    }
}

void useDataDir(const std::string& path) {
    std::filesystem::create_directories(path);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void testTokenFile() {
    const std::string dir = (std::filesystem::temp_directory_path() / ("nvj_api_token_" + std::to_string(std::random_device{}()))).string();
    useDataDir(dir);
    std::string loaded;
    CHECK(!loadToken(loaded));
    std::string token;
    CHECK(generateToken(token));
    CHECK(validTokenFormat(token));
    std::string second;
    CHECK(generateToken(second));
    CHECK(second != token);
    CHECK(!saveToken("short"));
    CHECK(!saveToken(std::string(64, 'G')));
    CHECK(saveToken(token));
    CHECK(loadToken(loaded) && loaded == token);
    CHECK(std::filesystem::path(tokenPath()).parent_path() == std::filesystem::path(dir));
    CHECK(saveToken(second));
    CHECK(loadToken(loaded) && loaded == second);
#ifndef _WIN32
    const std::filesystem::perms permissions = std::filesystem::status(tokenPath()).permissions();
    CHECK((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
#endif
    CHECK(!std::filesystem::exists(tokenPath() + ".tmp"));
    {
        std::ofstream broken(tokenPath(), std::ios::trunc);
        broken << "not a token\n";
    }
    CHECK(!loadToken(loaded));
    {
        std::ofstream padded(tokenPath(), std::ios::trunc);
        padded << token << "\r\n\r\n";
    }
    CHECK(loadToken(loaded) && loaded == token);

    Settings settings;
    settings.apiEnabled = true;
    settings.apiPort = 9123;
    settings.apiAllowRemote = true;
    CHECK(saveSettings(settings));
    std::ifstream file(std::filesystem::path(dir) / "settings.json");
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(text.find(token) == std::string::npos);
    CHECK(text.find(second) == std::string::npos);
    CHECK(text.find("token") == std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void testSettingsRoundTrip() {
    const std::string dir = (std::filesystem::temp_directory_path() / ("nvj_api_settings_" + std::to_string(std::random_device{}()))).string();
    useDataDir(dir);
    Settings defaults;
    CHECK(!defaults.apiEnabled);
    CHECK(!defaults.apiAllowRemote);
    CHECK(defaults.apiPort == 9020);
    Settings loaded;
    CHECK(!loadSettings(loaded) || !loaded.apiEnabled);
    Settings wanted;
    wanted.apiEnabled = true;
    wanted.apiAllowRemote = true;
    wanted.apiPort = 9555;
    CHECK(saveSettings(wanted));
    Settings back;
    CHECK(loadSettings(back));
    CHECK(back.apiEnabled && back.apiAllowRemote && back.apiPort == 9555);
    for (const auto& [written, expected] : std::vector<std::pair<int, int>>{{80, 1024}, {-5, 1024}, {1023, 1024}, {1024, 1024}, {65535, 65535}, {70000, 65535}, {0, 1024}}) {
        std::ofstream file(std::filesystem::path(dir) / "settings.json", std::ios::trunc);
        file << "{\"apiEnabled\":true,\"apiPort\":" << written << "}";
        file.close();
        Settings clamped;
        CHECK(loadSettings(clamped));
        CHECK(clamped.apiPort == expected);
        CHECK(clamped.apiEnabled);
        CHECK(!clamped.apiAllowRemote);
    }
    {
        std::ofstream file(std::filesystem::path(dir) / "settings.json", std::ios::trunc);
        file << "{\"apiEnabled\":\"yes\",\"apiPort\":\"9020\",\"apiAllowRemote\":1}";
    }
    Settings odd;
    CHECK(loadSettings(odd));
    CHECK(!odd.apiEnabled);
    CHECK(!odd.apiAllowRemote);
    CHECK(odd.apiPort == 9020);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

int main() {
    testParserBasics();
    testParserRejects();
    testParserBounded();
    testParserFuzz();
    testHttpHelpers();
    testSecurity();
    testRateLimiter();
    testRouterBasics();
    testRouterLockout();
    testRouterReads();
    testRouterWrites();
    testHubQueue();
    testHubThreads();
    testServerBasics();
    testServerStalledMain();
    testServerAbuse();
    testServerConcurrency();
    testServerRestartAndStop();
    testPageAssets();
    testPageLanguages();
    testTokenFile();
    testSettingsRoundTrip();
    return testing::finish();
}

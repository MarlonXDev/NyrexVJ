#include "control/api_security.hpp"

#include "core/http_message.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nvj::api {

namespace {

bool parseDecimalPort(const std::string& text, int& port) {
    if (text.empty() || text.size() > 5) {
        return false;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    if (value < 1 || value > 65535) {
        return false;
    }
    port = value;
    return true;
}

bool ipv4Literal(const std::string& text) {
    int parts = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('.', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string part = text.substr(start, end - start);
        if (part.empty() || part.size() > 3) {
            return false;
        }
        int value = 0;
        for (const char c : part) {
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10 + (c - '0');
        }
        if (value > 255) {
            return false;
        }
        ++parts;
        if (end == text.size()) {
            break;
        }
        start = end + 1;
    }
    return parts == 4;
}

bool splitHost(const std::string& hostHeader, std::string& host, int& port, bool& hasPort) {
    host.clear();
    hasPort = false;
    port = 0;
    if (hostHeader.empty()) {
        return false;
    }
    std::string rest;
    if (hostHeader[0] == '[') {
        const size_t close = hostHeader.find(']');
        if (close == std::string::npos) {
            return false;
        }
        host = hostHeader.substr(0, close + 1);
        rest = hostHeader.substr(close + 1);
        if (!rest.empty() && rest[0] != ':') {
            return false;
        }
    } else {
        const size_t colon = hostHeader.find(':');
        if (colon == std::string::npos) {
            host = hostHeader;
        } else {
            host = hostHeader.substr(0, colon);
            rest = hostHeader.substr(colon);
        }
    }
    if (!rest.empty()) {
        if (!parseDecimalPort(rest.substr(1), port)) {
            return false;
        }
        hasPort = true;
    }
    return !host.empty();
}

}

bool constantTimeEqual(const std::string& a, const std::string& b) {
    const size_t length = std::max(a.size(), b.size());
    unsigned char difference = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char left = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char right = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        difference = static_cast<unsigned char>(difference | (left ^ right));
    }
    return difference == 0;
}

bool generateToken(std::string& token) {
    std::array<unsigned char, kTokenBytes> raw{};
    if (!platform::secureRandomBytes(raw.data(), raw.size())) {
        return false;
    }
    static const char digits[] = "0123456789abcdef";
    token.clear();
    token.reserve(kTokenChars);
    for (const unsigned char byte : raw) {
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0x0f]);
    }
    return true;
}

bool validTokenFormat(const std::string& token) {
    if (token.size() != kTokenChars) {
        return false;
    }
    return std::all_of(token.begin(), token.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool parseBearer(const std::string& header, std::string& token) {
    const std::string prefix = "bearer ";
    if (header.size() <= prefix.size() || lowerAscii(header.substr(0, prefix.size())) != prefix) {
        return false;
    }
    token = trimHttpSpace(header.substr(prefix.size()));
    return !token.empty();
}

std::string tokenPath() {
    return (std::filesystem::path(userDataDir()) / "api_token").string();
}

bool loadSecretFile(const std::string& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
        content.pop_back();
    }
    text = content;
    return true;
}

bool saveSecretFile(const std::string& path, const std::string& text) {
    namespace fs = std::filesystem;
    const fs::path target = path;
    fs::path temp = target;
    temp += ".tmp";
    std::error_code ec;
#ifdef _WIN32
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << text << "\n";
        if (!file) {
            return false;
        }
    }
#else
    const int descriptor = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        return false;
    }
    const std::string line = text + "\n";
    size_t done = 0;
    while (done < line.size()) {
        const ssize_t wrote = ::write(descriptor, line.data() + done, line.size() - done);
        if (wrote <= 0) {
            ::close(descriptor);
            fs::remove(temp, ec);
            return false;
        }
        done += static_cast<size_t>(wrote);
    }
    ::fsync(descriptor);
    ::close(descriptor);
#endif
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool loadToken(std::string& token) {
    std::string text;
    if (!loadSecretFile(tokenPath(), text) || !validTokenFormat(text)) {
        return false;
    }
    token = text;
    return true;
}

bool saveToken(const std::string& token) {
    if (!validTokenFormat(token)) {
        return false;
    }
    return saveSecretFile(tokenPath(), token);
}

bool hostAllowed(const std::string& hostHeader, int port, bool allowRemote) {
    std::string host;
    int hostPort = 0;
    bool hasPort = false;
    if (!splitHost(hostHeader, host, hostPort, hasPort)) {
        return false;
    }
    if (hasPort ? hostPort != port : port != 80) {
        return false;
    }
    const std::string lower = lowerAscii(host);
    if (lower == "localhost" || lower == "[::1]") {
        return true;
    }
    if (!ipv4Literal(host)) {
        return false;
    }
    if (host.compare(0, 4, "127.") == 0) {
        return true;
    }
    return allowRemote;
}

bool originAllowed(const std::string* origin, const std::string* fetchSite, const std::string& hostHeader) {
    if (fetchSite != nullptr) {
        const std::string site = lowerAscii(*fetchSite);
        if (site != "same-origin" && site != "none") {
            return false;
        }
    }
    if (origin == nullptr) {
        return true;
    }
    return lowerAscii(*origin) == "http://" + lowerAscii(hostHeader);
}

RateLimiter::RateLimiter(const RateLimitConfig& config) : config_(config) {}

RateLimiter::Client& RateLimiter::find(std::uint32_t address, double now) {
    auto it = clients_.find(address);
    if (it == clients_.end()) {
        if (clients_.size() >= config_.maxClients) {
            evict(now);
        }
        Client fresh;
        fresh.tokens = config_.burst;
        fresh.updated = now;
        fresh.lastSeen = now;
        it = clients_.emplace(address, fresh).first;
    }
    return it->second;
}

void RateLimiter::evict(double now) {
    for (auto it = clients_.begin(); it != clients_.end();) {
        const Client& client = it->second;
        if (client.lockedUntil <= now && now - client.lastSeen > 10.0) {
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
    if (clients_.size() < config_.maxClients) {
        return;
    }
    auto oldest = clients_.end();
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it->second.lockedUntil > now) {
            continue;
        }
        if (oldest == clients_.end() || it->second.lastSeen < oldest->second.lastSeen) {
            oldest = it;
        }
    }
    if (oldest != clients_.end()) {
        clients_.erase(oldest);
    }
}

RateVerdict RateLimiter::check(std::uint32_t address, double now, double& retryAfter) {
    retryAfter = 0.0;
    Client& client = find(address, now);
    client.lastSeen = now;
    if (client.lockedUntil > now) {
        retryAfter = client.lockedUntil - now;
        return RateVerdict::Locked;
    }
    const double elapsed = std::max(0.0, now - client.updated);
    client.tokens = std::min(config_.burst, client.tokens + elapsed * config_.ratePerSecond);
    client.updated = now;
    if (client.tokens < 1.0) {
        retryAfter = (1.0 - client.tokens) / std::max(config_.ratePerSecond, 0.001);
        return RateVerdict::Limited;
    }
    client.tokens -= 1.0;
    return RateVerdict::Allow;
}

void RateLimiter::recordAuthFailure(std::uint32_t address, double now) {
    Client& client = find(address, now);
    if (client.failures == 0 || now - client.failureStart > config_.failureWindow) {
        client.failures = 0;
        client.failureStart = now;
    }
    ++client.failures;
    if (client.failures >= config_.failureLimit) {
        client.lockedUntil = now + config_.lockSeconds;
        client.failures = 0;
    }
}

void RateLimiter::recordAuthSuccess(std::uint32_t address) {
    const auto it = clients_.find(address);
    if (it != clients_.end()) {
        it->second.failures = 0;
    }
}

}

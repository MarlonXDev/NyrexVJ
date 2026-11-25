#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nvj::api {

constexpr size_t kTokenBytes = 32;
constexpr size_t kTokenChars = kTokenBytes * 2;

bool constantTimeEqual(const std::string& a, const std::string& b);
bool generateToken(std::string& token);
bool validTokenFormat(const std::string& token);
bool parseBearer(const std::string& header, std::string& token);
std::string tokenPath();
bool loadToken(std::string& token);
bool saveToken(const std::string& token);
bool loadSecretFile(const std::string& path, std::string& text);
bool saveSecretFile(const std::string& path, const std::string& text);

bool hostAllowed(const std::string& hostHeader, int port, bool allowRemote);
bool originAllowed(const std::string* origin, const std::string* fetchSite, const std::string& hostHeader);

struct RateLimitConfig {
    double ratePerSecond = 40.0;
    double burst = 80.0;
    int failureLimit = 5;
    double failureWindow = 60.0;
    double lockSeconds = 60.0;
    size_t maxClients = 256;
};

enum class RateVerdict { Allow, Limited, Locked };

class RateLimiter {
public:
    explicit RateLimiter(const RateLimitConfig& config = RateLimitConfig());

    RateVerdict check(std::uint32_t address, double now, double& retryAfter);
    void recordAuthFailure(std::uint32_t address, double now);
    void recordAuthSuccess(std::uint32_t address);
    size_t clientCount() const { return clients_.size(); }
    const RateLimitConfig& config() const { return config_; }

private:
    struct Client {
        double tokens = 0.0;
        double updated = 0.0;
        double lastSeen = 0.0;
        int failures = 0;
        double failureStart = 0.0;
        double lockedUntil = 0.0;
    };

    Client& find(std::uint32_t address, double now);
    void evict(double now);

    RateLimitConfig config_;
    std::unordered_map<std::uint32_t, Client> clients_;
};

}

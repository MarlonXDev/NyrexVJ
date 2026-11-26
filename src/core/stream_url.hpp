#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace nvj {
namespace stream {

enum class Scheme { Rtsp, Srt, Udp };

enum class Profile { LowLatency = 0, Balanced = 1, Smooth = 2 };

constexpr size_t kMaxUrlBytes = 1024;
constexpr int kMaxStreams = 8;
constexpr int kMinPictureSide = 16;
constexpr int kMaxPictureSide = 8192;
constexpr long long kMaxPicturePixels = 8192LL * 4608LL;

struct Query {
    std::string key;
    std::string value;
};

struct ParsedUrl {
    Scheme scheme = Scheme::Rtsp;
    std::string host;
    int port = 0;
    std::string user;
    std::string password;
    std::string path;
    std::vector<Query> query;
    bool listener = false;
    bool hasCredentials = false;
};

struct ParseResult {
    bool ok = false;
    std::string error;
    ParsedUrl url;
};

struct OpenPlan {
    std::string url;
    std::vector<std::pair<std::string, std::string>> options;
    bool listener = false;
    bool waitsForPeer = false;
};

struct ProfileSpec {
    const char* name;
    double lagLimitSeconds;
    long long srtLatencyMicros;
    long long probeBytes;
    long long analyzeMicros;
    long long maxDelayMicros;
    int udpFifoPackets;
    bool noBuffer;
};

ParseResult parseUrl(const std::string& text);
std::string maskUrl(const std::string& text);
const char* schemeName(Scheme scheme);
const ProfileSpec& profileSpec(Profile profile);
Profile profileFromInt(int value);
OpenPlan makeOpenPlan(const ParsedUrl& url, Profile profile);
bool codecAllowed(const std::string& codecName);
bool pictureSizeAllowed(int width, int height);

}
}

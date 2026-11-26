#include "core/stream_url.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace nvj {
namespace stream {

namespace {

constexpr ProfileSpec kProfiles[] = {
    {"Low latency", 0.4, 120000, 1000000, 700000, 0, 7168, true},
    {"Balanced", 1.0, 250000, 2000000, 1500000, 200000, 14336, true},
    {"Smooth", 2.0, 800000, 5000000, 3000000, 500000, 28672, false},
};

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool isAlnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool inSet(char c, const char* set) {
    for (const char* p = set; *p != '\0'; ++p) {
        if (*p == c) {
            return true;
        }
    }
    return false;
}

bool allIn(const std::string& text, const char* extra) {
    for (char c : text) {
        if (!isAlnum(c) && !inSet(c, extra)) {
            return false;
        }
    }
    return true;
}

bool parseNumber(const std::string& text, long long minimum, long long maximum, long long& out) {
    if (text.empty() || text.size() > 12) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    const long long value = std::atoll(text.c_str());
    if (value < minimum || value > maximum) {
        return false;
    }
    out = value;
    return true;
}

ParseResult fail(const std::string& message) {
    ParseResult result;
    result.error = message;
    return result;
}

struct Rule {
    const char* key;
    long long minimum;
    long long maximum;
};

bool numericOption(const Rule* rules, size_t count, const std::string& key, const std::string& value, std::string& error) {
    for (size_t i = 0; i < count; ++i) {
        if (key == rules[i].key) {
            long long parsed = 0;
            if (!parseNumber(value, rules[i].minimum, rules[i].maximum, parsed)) {
                error = "The value of an option in the address is out of range: " + key.substr(0, 24);
                return false;
            }
            return true;
        }
    }
    error = "An option in the address is not valid or not supported: " + key.substr(0, 24);
    return false;
}

bool splitQuery(const std::string& raw, std::vector<Query>& out, std::string& error) {
    size_t start = 0;
    while (start <= raw.size()) {
        size_t end = raw.find('&', start);
        if (end == std::string::npos) {
            end = raw.size();
        }
        const std::string part = raw.substr(start, end - start);
        if (!part.empty()) {
            const size_t equals = part.find('=');
            Query query;
            query.key = lower(equals == std::string::npos ? part : part.substr(0, equals));
            query.value = equals == std::string::npos ? std::string() : part.substr(equals + 1);
            if (query.key.empty() || !allIn(query.key, "_-")) {
                error = "An option in the address is not valid or not supported: " + query.key.substr(0, 24);
                return false;
            }
            for (const Query& other : out) {
                if (other.key == query.key) {
                    error = "An option in the address appears twice: " + query.key.substr(0, 24);
                    return false;
                }
            }
            out.push_back(std::move(query));
        }
        if (end >= raw.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool validSrtQuery(ParsedUrl& url, std::string& error) {
    static const Rule numbers[] = {
        {"latency", 0, 10000000},
        {"rcvlatency", 0, 10000000},
        {"peerlatency", 0, 10000000},
        {"connect_timeout", 100, 60000},
        {"timeout", 100000, 60000000},
        {"pkt_size", 188, 1456},
    };
    for (const Query& query : url.query) {
        if (query.key == "mode") {
            if (query.value == "listener") {
                url.listener = true;
            } else if (query.value != "caller") {
                error = "The value of an option in the address is out of range: mode";
                return false;
            }
        } else if (query.key == "passphrase") {
            if (query.value.size() < 10 || query.value.size() > 64) {
                error = "The value of an option in the address is out of range: passphrase";
                return false;
            }
        } else if (query.key == "streamid") {
            if (query.value.empty() || query.value.size() > 512) {
                error = "The value of an option in the address is out of range: streamid";
                return false;
            }
        } else if (!numericOption(numbers, sizeof(numbers) / sizeof(numbers[0]), query.key, query.value, error)) {
            return false;
        }
    }
    return true;
}

bool validUdpQuery(const ParsedUrl& url, std::string& error) {
    static const Rule numbers[] = {
        {"fifo_size", 188, 4000000},
        {"buffer_size", 65536, 67108864},
        {"overrun_nonfatal", 0, 1},
        {"reuse", 0, 1},
        {"pkt_size", 188, 65507},
        {"timeout", 100000, 60000000},
    };
    for (const Query& query : url.query) {
        if (query.key == "localaddr") {
            bool literal = !query.value.empty() && query.value.size() <= 45 && query.value.find_first_of(".:") != std::string::npos;
            for (char c : query.value) {
                literal = literal && (std::isxdigit(static_cast<unsigned char>(c)) != 0 || c == '.' || c == ':');
            }
            if (!literal) {
                error = "The value of an option in the address is out of range: localaddr";
                return false;
            }
        } else if (!numericOption(numbers, sizeof(numbers) / sizeof(numbers[0]), query.key, query.value, error)) {
            return false;
        }
    }
    return true;
}

bool hasKey(const ParsedUrl& url, const char* key) {
    for (const Query& query : url.query) {
        if (query.key == key) {
            return true;
        }
    }
    return false;
}

void sensitiveMask(std::string& query) {
    size_t start = 0;
    std::string out;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) {
            end = query.size();
        }
        std::string part = query.substr(start, end - start);
        const size_t equals = part.find('=');
        if (equals != std::string::npos) {
            const std::string key = lower(part.substr(0, equals));
            for (const char* secret : {"pass", "pwd", "token", "secret", "key", "auth", "streamid", "sig"}) {
                if (key.find(secret) != std::string::npos) {
                    part = part.substr(0, equals + 1) + "****";
                    break;
                }
            }
        }
        if (!out.empty() || start > 0) {
            out += '&';
        }
        out += part;
        if (end >= query.size()) {
            break;
        }
        start = end + 1;
    }
    query = out;
}

}

const char* schemeName(Scheme scheme) {
    switch (scheme) {
    case Scheme::Rtsp:
        return "rtsp";
    case Scheme::Srt:
        return "srt";
    case Scheme::Udp:
        return "udp";
    }
    return "rtsp";
}

const ProfileSpec& profileSpec(Profile profile) {
    const int index = std::clamp(static_cast<int>(profile), 0, 2);
    return kProfiles[index];
}

Profile profileFromInt(int value) {
    return static_cast<Profile>(std::clamp(value, 0, 2));
}

ParseResult parseUrl(const std::string& input) {
    size_t first = 0;
    size_t last = input.size();
    while (first < last && (input[first] == ' ' || input[first] == '\t')) {
        ++first;
    }
    while (last > first && (input[last - 1] == ' ' || input[last - 1] == '\t' || input[last - 1] == '\r' || input[last - 1] == '\n')) {
        --last;
    }
    const std::string text = input.substr(first, last - first);
    if (text.empty()) {
        return fail("No address is entered");
    }
    if (text.size() > kMaxUrlBytes) {
        return fail("The address is too long");
    }
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7f || c == '\\' || c == '"' || c == '<' || c == '>' || c == '`' || c == '{' || c == '}' || c == '|' || c == '^') {
            return fail("The address contains a character that is not allowed. Write special characters as %XX");
        }
        if (c == '#') {
            return fail("The address contains a character that is not allowed. Write special characters as %XX");
        }
    }
    const size_t colon = text.find("://");
    if (colon == std::string::npos) {
        return fail("The address must start with rtsp://, srt:// or udp://");
    }
    const std::string schemeText = lower(text.substr(0, colon));
    ParsedUrl url;
    if (schemeText == "rtsp") {
        url.scheme = Scheme::Rtsp;
    } else if (schemeText == "srt") {
        url.scheme = Scheme::Srt;
    } else if (schemeText == "udp") {
        url.scheme = Scheme::Udp;
    } else {
        return fail("The address must start with rtsp://, srt:// or udp://");
    }
    const std::string rest = text.substr(colon + 3);
    size_t authorityEnd = rest.find_first_of("/?");
    if (authorityEnd == std::string::npos) {
        authorityEnd = rest.size();
    }
    const std::string authority = rest.substr(0, authorityEnd);
    std::string tail = rest.substr(authorityEnd);

    std::string hostPort = authority;
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        const std::string userInfo = authority.substr(0, at);
        hostPort = authority.substr(at + 1);
        if (url.scheme == Scheme::Srt) {
            return fail("Only an RTSP address can have a user name and password. SRT uses the passphrase option");
        }
        if (url.scheme == Scheme::Udp) {
            if (!userInfo.empty()) {
                return fail("Only an RTSP address can have a user name and password. SRT uses the passphrase option");
            }
        } else if (!userInfo.empty()) {
            if (userInfo.find('@') != std::string::npos) {
                return fail("The address contains a character that is not allowed. Write special characters as %XX");
            }
            const size_t split = userInfo.find(':');
            url.user = split == std::string::npos ? userInfo : userInfo.substr(0, split);
            url.password = split == std::string::npos ? std::string() : userInfo.substr(split + 1);
            if (!allIn(url.user, "-._~%!$&'()*+,;=") || !allIn(url.password, "-._~%!$&'()*+,;=:")) {
                return fail("The address contains a character that is not allowed. Write special characters as %XX");
            }
            url.hasCredentials = true;
        }
    }

    std::string portText;
    if (!hostPort.empty() && hostPort.front() == '[') {
        const size_t close = hostPort.find(']');
        if (close == std::string::npos) {
            return fail("The name or address of the computer is not valid");
        }
        url.host = hostPort.substr(0, close + 1);
        if (!allIn(url.host.substr(1, url.host.size() - 2), ":.%")) {
            return fail("The name or address of the computer is not valid");
        }
        const std::string after = hostPort.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return fail("The name or address of the computer is not valid");
            }
            portText = after.substr(1);
        }
    } else {
        const size_t split = hostPort.rfind(':');
        url.host = split == std::string::npos ? hostPort : hostPort.substr(0, split);
        if (split != std::string::npos) {
            portText = hostPort.substr(split + 1);
        }
        if (!allIn(url.host, "-._")) {
            return fail("The name or address of the computer is not valid");
        }
    }
    if (url.host.size() > 253) {
        return fail("The name or address of the computer is not valid");
    }
    if (!portText.empty()) {
        long long port = 0;
        if (!parseNumber(portText, 1, 65535, port)) {
            return fail("The port is missing or not a number from 1 to 65535");
        }
        url.port = static_cast<int>(port);
    }

    std::string rawQuery;
    const size_t question = tail.find('?');
    if (question != std::string::npos) {
        rawQuery = tail.substr(question + 1);
        tail = tail.substr(0, question);
    }
    for (char c : rawQuery) {
        if (!isAlnum(c) && !inSet(c, "-._~%:/@,=&+*!$()'")) {
            return fail("The address contains a character that is not allowed. Write special characters as %XX");
        }
    }
    if (!tail.empty() && !allIn(tail, "-._~%:/@,=+*!$()'&")) {
        return fail("The address contains a character that is not allowed. Write special characters as %XX");
    }
    if (tail.find("..") != std::string::npos) {
        return fail("The path of the address must not contain ..");
    }

    std::string error;
    if (url.scheme == Scheme::Rtsp) {
        if (url.host.empty()) {
            return fail("The name or address of the computer is not valid");
        }
        url.path = tail.empty() ? std::string("/") : tail;
        if (!rawQuery.empty()) {
            url.path += "?" + rawQuery;
        }
        url.listener = false;
        return ParseResult{true, std::string(), url};
    }
    if (!tail.empty() && tail != "/") {
        return fail("An SRT or UDP address has no path");
    }
    if (!splitQuery(rawQuery, url.query, error)) {
        return fail(error);
    }
    if (url.port == 0) {
        return fail("The port is missing or not a number from 1 to 65535");
    }
    if (url.scheme == Scheme::Srt) {
        if (!validSrtQuery(url, error)) {
            return fail(error);
        }
        if (url.host.empty() && !url.listener) {
            return fail("An SRT address without a computer name needs mode=listener");
        }
    } else {
        if (!validUdpQuery(url, error)) {
            return fail(error);
        }
        url.listener = true;
    }
    return ParseResult{true, std::string(), url};
}

std::string maskUrl(const std::string& text) {
    std::string clean;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        clean += (u < 0x20 || u >= 0x7f) ? '?' : c;
    }
    if (clean.size() > 300) {
        clean = clean.substr(0, 300) + "...";
    }
    const size_t colon = clean.find("://");
    if (colon == std::string::npos) {
        return clean;
    }
    std::string head = clean.substr(0, colon + 3);
    std::string rest = clean.substr(colon + 3);
    size_t authorityEnd = rest.find_first_of("/?");
    if (authorityEnd == std::string::npos) {
        authorityEnd = rest.size();
    }
    std::string authority = rest.substr(0, authorityEnd);
    std::string tail = rest.substr(authorityEnd);
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        const std::string userInfo = authority.substr(0, at);
        const size_t split = userInfo.find(':');
        authority = (split == std::string::npos ? userInfo : userInfo.substr(0, split) + ":****") + authority.substr(at);
    }
    const size_t question = tail.find('?');
    if (question != std::string::npos) {
        std::string query = tail.substr(question + 1);
        sensitiveMask(query);
        tail = tail.substr(0, question + 1) + query;
    }
    return head + authority + tail;
}

OpenPlan makeOpenPlan(const ParsedUrl& url, Profile profile) {
    const ProfileSpec& spec = profileSpec(profile);
    OpenPlan plan;
    plan.listener = url.listener;
    plan.waitsForPeer = url.scheme == Scheme::Srt && url.listener;
    std::string address = std::string(schemeName(url.scheme)) + "://";
    if (url.hasCredentials) {
        address += url.user;
        if (!url.password.empty()) {
            address += ":" + url.password;
        }
        address += "@";
    }
    if (url.scheme == Scheme::Udp) {
        address += url.host.empty() ? std::string("0.0.0.0") : url.host;
    } else {
        address += url.host;
    }
    if (url.port > 0) {
        address += ":" + std::to_string(url.port);
    }
    if (url.scheme == Scheme::Rtsp) {
        address += url.path;
    } else {
        std::vector<Query> query = url.query;
        auto add = [&](const char* key, const std::string& value) {
            if (!hasKey(url, key)) {
                query.push_back({key, value});
            }
        };
        if (url.scheme == Scheme::Srt) {
            add("latency", std::to_string(spec.srtLatencyMicros));
            add("connect_timeout", "4000");
            add("timeout", "5000000");
        } else {
            add("fifo_size", std::to_string(spec.udpFifoPackets));
            add("overrun_nonfatal", "1");
            add("timeout", "5000000");
        }
        for (size_t i = 0; i < query.size(); ++i) {
            address += (i == 0 ? "?" : "&") + query[i].key + "=" + query[i].value;
        }
    }
    plan.url = address;
    const char* whitelist = url.scheme == Scheme::Rtsp ? "rtsp,rtp,udp,tcp" : (url.scheme == Scheme::Srt ? "srt" : "udp");
    plan.options.emplace_back("protocol_whitelist", whitelist);
    plan.options.emplace_back("max_streams", std::to_string(kMaxStreams));
    plan.options.emplace_back("probesize", std::to_string(spec.probeBytes));
    plan.options.emplace_back("analyzeduration", std::to_string(spec.analyzeMicros));
    plan.options.emplace_back("max_delay", std::to_string(spec.maxDelayMicros));
    if (spec.noBuffer) {
        plan.options.emplace_back("fflags", "nobuffer");
        plan.options.emplace_back("flags", "low_delay");
    }
    if (url.scheme == Scheme::Rtsp) {
        plan.options.emplace_back("rtsp_transport", "tcp");
        plan.options.emplace_back("allowed_media_types", "video");
        plan.options.emplace_back("timeout", "5000000");
    }
    return plan;
}

bool codecAllowed(const std::string& codecName) {
    for (const char* name : {"h264", "hevc", "av1", "mjpeg", "mpeg2video", "vp8", "vp9"}) {
        if (codecName == name) {
            return true;
        }
    }
    return false;
}

bool pictureSizeAllowed(int width, int height) {
    if (width < kMinPictureSide || height < kMinPictureSide || width > kMaxPictureSide || height > kMaxPictureSide) {
        return false;
    }
    return static_cast<long long>(width) * static_cast<long long>(height) <= kMaxPicturePixels;
}

}
}

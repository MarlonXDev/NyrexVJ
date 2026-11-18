#include "update_logic.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifndef NYREXVJ_VERSION
#define NYREXVJ_VERSION "0.0.0-dev"
#endif

#ifndef NYREXVJ_BUILD_NUMBER
#define NYREXVJ_BUILD_NUMBER 0
#endif

namespace nvj {

namespace {

bool readNumber(const std::string& text, size_t& pos, int& value) {
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }
    long long result = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        result = result * 10 + (text[pos] - '0');
        if (result > 1000000) {
            return false;
        }
        ++pos;
    }
    value = static_cast<int>(result);
    return true;
}

std::string lowerCopy(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}

Version parseVersion(const std::string& text) {
    Version version;
    size_t pos = 0;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V')) {
        ++pos;
    }
    if (!readNumber(text, pos, version.major)) {
        return Version{};
    }
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        if (!readNumber(text, pos, version.minor)) {
            return Version{};
        }
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            if (!readNumber(text, pos, version.patch)) {
                return Version{};
            }
        }
    }
    if (pos < text.size() && text[pos] == '-') {
        version.dev = true;
    }
    version.valid = true;
    return version;
}

std::string versionToString(const Version& version) {
    std::string out = std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
    if (version.dev) {
        out += "-dev";
    }
    return out;
}

int compareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return a.patch < b.patch ? -1 : 1;
    }
    if (a.dev != b.dev) {
        return a.dev ? -1 : 1;
    }
    return 0;
}

int parseBuildNumber(const std::string& text) {
    std::string lower = lowerCopy(text);
    size_t at = lower.find("build");
    while (at != std::string::npos) {
        size_t pos = at + 5;
        while (pos < lower.size() && (lower[pos] == ' ' || lower[pos] == '#' || lower[pos] == ':')) {
            ++pos;
        }
        int value = 0;
        if (readNumber(lower, pos, value)) {
            return value;
        }
        at = lower.find("build", at + 5);
    }
    return 0;
}

bool parseRelease(const std::string& json, ReleaseInfo& release, std::string& error) {
    release = ReleaseInfo{};
    nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "The update server sent an unreadable answer";
        return false;
    }
    try {
        release.tag = doc.value("tag_name", std::string());
        release.name = doc.value("name", std::string());
        release.notes = doc.value("body", std::string());
        release.pageUrl = doc.value("html_url", std::string());
        release.prerelease = doc.value("prerelease", false);
        release.draft = doc.value("draft", false);
        if (doc.contains("assets") && doc["assets"].is_array()) {
            for (const nlohmann::json& asset : doc["assets"]) {
                if (!asset.is_object()) {
                    continue;
                }
                std::string name = asset.value("name", std::string());
                std::string url = asset.value("browser_download_url", std::string());
                if (name == kExeAssetName) {
                    release.exeUrl = url;
                } else if (name == kSumsAssetName) {
                    release.sumsUrl = url;
                }
            }
        }
    } catch (const std::exception&) {
        error = "The update information has an unexpected format";
        return false;
    }
    if (release.tag.empty()) {
        error = "The update information has no version";
        return false;
    }
    release.version = parseVersion(release.tag);
    release.buildNumber = parseBuildNumber(release.name);
    if (release.buildNumber == 0) {
        release.buildNumber = parseBuildNumber(release.notes);
    }
    return true;
}

bool findHash(const std::string& sumsText, const std::string& fileName, std::string& hashHex) {
    size_t pos = 0;
    while (pos < sumsText.size()) {
        size_t end = sumsText.find('\n', pos);
        if (end == std::string::npos) {
            end = sumsText.size();
        }
        std::string line = sumsText.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.size() < 66) {
            continue;
        }
        std::string hash = line.substr(0, 64);
        bool hex = std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
        if (!hex) {
            continue;
        }
        size_t nameStart = 64;
        while (nameStart < line.size() && (line[nameStart] == ' ' || line[nameStart] == '*')) {
            ++nameStart;
        }
        if (line.substr(nameStart) == fileName) {
            hashHex = lowerCopy(hash);
            return true;
        }
    }
    return false;
}

bool isUpdateAvailable(const ReleaseInfo& release, UpdateChannel channel, const CurrentBuild& current) {
    if (release.draft || release.exeUrl.empty()) {
        return false;
    }
    if (channel == UpdateChannel::Nightly) {
        return release.buildNumber > current.buildNumber;
    }
    if (!release.version.valid || !current.version.valid) {
        return false;
    }
    return compareVersions(release.version, current.version) > 0;
}

std::string releaseApiUrl(UpdateChannel channel) {
    std::string base = std::string("https://api.github.com/repos/") + kRepoOwner + "/" + kRepoName + "/releases/";
    return channel == UpdateChannel::Stable ? base + "latest" : base + "tags/nightly";
}

const char* channelName(UpdateChannel channel) {
    return channel == UpdateChannel::Stable ? "stable" : "nightly";
}

UpdateChannel channelFromName(const std::string& name) {
    return lowerCopy(name) == "stable" ? UpdateChannel::Stable : UpdateChannel::Nightly;
}

CurrentBuild currentBuild() {
    CurrentBuild build;
    build.version = parseVersion(NYREXVJ_VERSION);
    build.buildNumber = NYREXVJ_BUILD_NUMBER;
    return build;
}

}

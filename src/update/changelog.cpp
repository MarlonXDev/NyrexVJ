#include "update/changelog.hpp"

#include "update/update_logic.hpp"

#include <sstream>

extern const unsigned char nvjChangelogEn[];
extern const unsigned long long nvjChangelogEnSize;
extern const unsigned char nvjChangelogDe[];
extern const unsigned long long nvjChangelogDeSize;

namespace nvj {

namespace {

std::string trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        return std::string();
    }
    size_t end = text.find_last_not_of(" \t\r");
    return text.substr(start, end - start + 1);
}

}

std::vector<ChangelogEntry> parseChangelog(const std::string& markdown) {
    std::vector<ChangelogEntry> entries;
    std::istringstream stream(markdown);
    std::string line;
    while (std::getline(stream, line)) {
        std::string clean = trim(line);
        if (clean.rfind("## ", 0) == 0) {
            ChangelogEntry entry;
            std::string heading = trim(clean.substr(3));
            size_t split = heading.find(" - ");
            if (split == std::string::npos) {
                entry.version = heading;
            } else {
                entry.version = trim(heading.substr(0, split));
                entry.date = trim(heading.substr(split + 3));
            }
            if (!entry.version.empty() && entry.version.front() == '[') {
                entry.version = entry.version.substr(1);
            }
            if (!entry.version.empty() && entry.version.back() == ']') {
                entry.version.pop_back();
            }
            entries.push_back(entry);
        } else if (!entries.empty() && clean.rfind("- ", 0) == 0) {
            entries.back().items.push_back(trim(clean.substr(2)));
        } else if (!entries.empty() && !clean.empty() && clean.rfind("#", 0) != 0 && !entries.back().items.empty()) {
            entries.back().items.back() += " " + clean;
        }
    }
    return entries;
}

const std::vector<ChangelogEntry>& changelog(bool german) {
    static const std::vector<ChangelogEntry> english = parseChangelog(std::string(reinterpret_cast<const char*>(nvjChangelogEn), static_cast<size_t>(nvjChangelogEnSize)));
    static const std::vector<ChangelogEntry> deutsch = parseChangelog(std::string(reinterpret_cast<const char*>(nvjChangelogDe), static_cast<size_t>(nvjChangelogDeSize)));
    return german && !deutsch.empty() ? deutsch : english;
}

std::string baseVersionString(const std::string& version) {
    Version parsed = parseVersion(version);
    if (!parsed.valid) {
        return version;
    }
    return std::to_string(parsed.major) + "." + std::to_string(parsed.minor) + "." + std::to_string(parsed.patch);
}

bool isNewerVersion(const std::string& candidate, const std::string& reference) {
    Version a = parseVersion(candidate);
    Version b = parseVersion(reference);
    if (!a.valid) {
        return false;
    }
    if (!b.valid) {
        return true;
    }
    a.dev = false;
    b.dev = false;
    return compareVersions(a, b) > 0;
}

WelcomeKind welcomeKind(const std::string& lastSeenVersion, const std::string& currentVersion) {
    if (lastSeenVersion.empty()) {
        return WelcomeKind::FirstRun;
    }
    if (isNewerVersion(currentVersion, lastSeenVersion)) {
        return WelcomeKind::Update;
    }
    return WelcomeKind::None;
}

}

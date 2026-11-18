#pragma once

#include <string>
#include <vector>

namespace nvj {

struct ChangelogEntry {
    std::string version;
    std::string date;
    std::vector<std::string> items;
};

std::vector<ChangelogEntry> parseChangelog(const std::string& markdown);
const std::vector<ChangelogEntry>& changelog(bool german);
std::string baseVersionString(const std::string& version);
bool isNewerVersion(const std::string& candidate, const std::string& reference);

enum class WelcomeKind {
    None,
    FirstRun,
    Update,
};

WelcomeKind welcomeKind(const std::string& lastSeenVersion, const std::string& currentVersion);

}

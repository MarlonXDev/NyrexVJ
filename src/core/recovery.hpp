#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nvj {

constexpr size_t kRotatingAutosaves = 10;

struct AutosaveEntry {
    std::filesystem::path path;
    long long timestamp = 0;
    std::uintmax_t bytes = 0;
    bool latest = false;
};

struct PreviousSession {
    bool crashed = false;
    bool otherInstance = false;
    std::string version;
    long long startedAt = 0;
    std::string crashReport;
    std::string logTail;
};

using ProcessAliveFn = std::function<bool(unsigned long)>;

std::filesystem::path recoveryDirectory(const std::filesystem::path& dataDir);
std::filesystem::path crashReportPath(const std::filesystem::path& dataDir);
std::filesystem::path previousLogPath(const std::filesystem::path& dataDir);

bool writeFileAtomic(const std::filesystem::path& path, const std::string& text);
bool readWholeFile(const std::filesystem::path& path, std::string& text);

PreviousSession inspectPreviousSession(const std::filesystem::path& dataDir, unsigned long ownPid, const ProcessAliveFn& alive);
bool beginSession(const std::filesystem::path& dataDir, unsigned long pid, const std::string& version, long long now);
void endSession(const std::filesystem::path& dataDir, unsigned long pid);

bool writeRotatingAutosave(const std::filesystem::path& dataDir, const std::string& text, long long now, size_t keep = kRotatingAutosaves);
std::vector<AutosaveEntry> listAutosaves(const std::filesystem::path& dataDir);

bool writeCreatorRecovery(const std::filesystem::path& dataDir, const std::string& text);
bool readCreatorRecovery(const std::filesystem::path& dataDir, std::string& text);
bool hasCreatorRecovery(const std::filesystem::path& dataDir);
void clearCreatorRecovery(const std::filesystem::path& dataDir);

std::string formatTimestamp(long long seconds);
std::string tailLines(const std::string& text, size_t lines);

}

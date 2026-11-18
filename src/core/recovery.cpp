#include "core/recovery.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace nvj {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLockName = "session.lock";
constexpr const char* kAutosavePrefix = "autosave-";
constexpr const char* kCreatorName = "creator-scene.json";
constexpr size_t kLogTailLines = 40;
constexpr size_t kReportLimit = 16384;

fs::path lockPath(const fs::path& dataDir) {
    return recoveryDirectory(dataDir) / kLockName;
}

std::string rotatingName(long long seconds) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%s%012lld.json", kAutosavePrefix, std::max(seconds, 0LL));
    return buffer;
}

bool parseRotatingName(const std::string& name, long long& seconds) {
    const std::string prefix = kAutosavePrefix;
    if (name.size() != prefix.size() + 12 + 5 || name.compare(0, prefix.size(), prefix) != 0 || name.compare(name.size() - 5, 5, ".json") != 0) {
        return false;
    }
    long long value = 0;
    for (size_t i = prefix.size(); i < prefix.size() + 12; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = value * 10 + (name[i] - '0');
    }
    seconds = value;
    return true;
}

std::vector<std::pair<std::string, fs::path>> rotatingFiles(const fs::path& dataDir) {
    std::vector<std::pair<std::string, fs::path>> files;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(recoveryDirectory(dataDir), ec)) {
        long long ignored = 0;
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && parseRotatingName(name, ignored)) {
            files.emplace_back(name, entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string valueOf(const std::string& text, const std::string& key) {
    std::istringstream stream(text);
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, prefix.size(), prefix) == 0) {
            return line.substr(prefix.size());
        }
    }
    return {};
}

unsigned long parseUnsigned(const std::string& text) {
    if (text.empty() || text.size() > 18) {
        return 0;
    }
    unsigned long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10 + static_cast<unsigned long>(c - '0');
    }
    return value;
}

}

fs::path recoveryDirectory(const fs::path& dataDir) {
    return dataDir / "recovery";
}

fs::path crashReportPath(const fs::path& dataDir) {
    return recoveryDirectory(dataDir) / "crash-report.txt";
}

fs::path previousLogPath(const fs::path& dataDir) {
    return dataDir / "nyrexvj.previous.log";
}

bool writeFileAtomic(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << text;
        file.flush();
        if (!file) {
            file.close();
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        std::error_code removeEc;
        fs::remove(path, removeEc);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    if (ec) {
        std::error_code removeEc;
        fs::remove(temporary, removeEc);
        return false;
    }
    return true;
}

bool readWholeFile(const fs::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

std::string tailLines(const std::string& text, size_t lines) {
    if (lines == 0 || text.empty()) {
        return {};
    }
    size_t end = text.size();
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    size_t start = end;
    size_t found = 0;
    while (start > 0) {
        if (text[start - 1] == '\n') {
            ++found;
            if (found == lines) {
                break;
            }
        }
        --start;
    }
    return text.substr(start, end - start);
}

std::string formatTimestamp(long long seconds) {
    const std::time_t value = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

PreviousSession inspectPreviousSession(const fs::path& dataDir, unsigned long ownPid, const ProcessAliveFn& alive) {
    PreviousSession result;
    std::string lock;
    if (!readWholeFile(lockPath(dataDir), lock)) {
        std::error_code ec;
        fs::remove(crashReportPath(dataDir), ec);
        return result;
    }
    const unsigned long pid = parseUnsigned(valueOf(lock, "pid"));
    result.version = valueOf(lock, "version");
    result.startedAt = static_cast<long long>(parseUnsigned(valueOf(lock, "started")));
    if (pid != 0 && pid != ownPid && alive && alive(pid)) {
        result.otherInstance = true;
        return result;
    }
    result.crashed = true;
    std::string report;
    if (readWholeFile(crashReportPath(dataDir), report)) {
        if (report.size() > kReportLimit) {
            report.resize(kReportLimit);
        }
        result.crashReport = report;
    }
    std::error_code ec;
    fs::remove(crashReportPath(dataDir), ec);
    std::string log;
    if (readWholeFile(previousLogPath(dataDir), log)) {
        result.logTail = tailLines(log, kLogTailLines);
    }
    return result;
}

bool beginSession(const fs::path& dataDir, unsigned long pid, const std::string& version, long long now) {
    std::ostringstream text;
    text << "pid=" << pid << "\nversion=" << version << "\nstarted=" << now << "\n";
    return writeFileAtomic(lockPath(dataDir), text.str());
}

void endSession(const fs::path& dataDir, unsigned long pid) {
    std::string lock;
    if (!readWholeFile(lockPath(dataDir), lock)) {
        return;
    }
    if (parseUnsigned(valueOf(lock, "pid")) != pid) {
        return;
    }
    std::error_code ec;
    fs::remove(lockPath(dataDir), ec);
}

bool writeRotatingAutosave(const fs::path& dataDir, const std::string& text, long long now, size_t keep) {
    if (!writeFileAtomic(recoveryDirectory(dataDir) / rotatingName(now), text)) {
        return false;
    }
    std::vector<std::pair<std::string, fs::path>> files = rotatingFiles(dataDir);
    while (files.size() > std::max<size_t>(keep, 1)) {
        std::error_code ec;
        fs::remove(files.front().second, ec);
        files.erase(files.begin());
    }
    return true;
}

std::vector<AutosaveEntry> listAutosaves(const fs::path& dataDir) {
    std::vector<AutosaveEntry> entries;
    std::error_code ec;
    const fs::path latest = dataDir / "autosave.json";
    if (fs::is_regular_file(latest, ec)) {
        AutosaveEntry entry;
        entry.path = latest;
        entry.latest = true;
        entry.bytes = fs::file_size(latest, ec);
        const auto written = fs::last_write_time(latest, ec);
        if (!ec) {
            const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
            entry.timestamp = static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count());
        }
        entries.push_back(entry);
    }
    std::vector<std::pair<std::string, fs::path>> files = rotatingFiles(dataDir);
    for (auto it = files.rbegin(); it != files.rend(); ++it) {
        AutosaveEntry entry;
        entry.path = it->second;
        parseRotatingName(it->first, entry.timestamp);
        entry.bytes = fs::file_size(it->second, ec);
        entries.push_back(entry);
    }
    return entries;
}

bool writeCreatorRecovery(const fs::path& dataDir, const std::string& text) {
    return writeFileAtomic(recoveryDirectory(dataDir) / kCreatorName, text);
}

bool readCreatorRecovery(const fs::path& dataDir, std::string& text) {
    return readWholeFile(recoveryDirectory(dataDir) / kCreatorName, text);
}

bool hasCreatorRecovery(const fs::path& dataDir) {
    std::error_code ec;
    return fs::is_regular_file(recoveryDirectory(dataDir) / kCreatorName, ec);
}

void clearCreatorRecovery(const fs::path& dataDir) {
    std::error_code ec;
    fs::remove(recoveryDirectory(dataDir) / kCreatorName, ec);
}

}

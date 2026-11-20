#include "platform/platform.hpp"

#include <cerrno>
#include <csignal>
#include <fstream>
#include <filesystem>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

namespace nvj::platform {

bool updatesSupported() {
    return false;
}

bool httpGet(const std::string&, HttpResult& result, const ProgressFn&) {
    result = HttpResult{};
    result.error = "Updates are only available on Windows";
    return false;
}

std::string currentExePath() {
    std::error_code ec;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : path.string();
}

bool replaceExecutable(const std::string&, const std::vector<unsigned char>&, ReplaceMode& mode, std::string& error) {
    mode = ReplaceMode::InPlace;
    error = "Updates are only available on Windows";
    return false;
}

bool startDeferredUpdate(const std::string&, unsigned long, std::string& error) {
    error = "Updates are only available on Windows";
    return false;
}

void removeStaleFiles(const std::string&) {
}

bool handleUpdateHelper(int, char**, int&) {
    return false;
}

bool replaceSelf(const std::vector<unsigned char>&, std::string& error) {
    error = "Updates are only available on Windows";
    return false;
}

bool relaunchSelf() {
    return false;
}

void cleanupOldBinary() {
}

bool openUrl(const std::string&) {
    return false;
}

bool openFolder(const std::string&) {
    return false;
}

size_t processMemoryBytes() {
    std::ifstream in("/proc/self/statm");
    unsigned long long total = 0;
    unsigned long long resident = 0;
    if (!(in >> total >> resident)) {
        return 0;
    }
    return static_cast<size_t>(resident) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

double processCpuSeconds() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
    const auto seconds = [](const timeval& value) { return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) * 1.0e-6; };
    return seconds(usage.ru_utime) + seconds(usage.ru_stime);
}

unsigned long currentProcessId() {
    return static_cast<unsigned long>(getpid());
}

bool processAlive(unsigned long pid) {
    if (pid == 0) {
        return false;
    }
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

}

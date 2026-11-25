#include "platform/platform.hpp"

#include <cerrno>
#include <fcntl.h>
#include <chrono>
#include <csignal>
#include <fstream>
#include <filesystem>
#include <thread>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

namespace nvj::platform {

#ifndef __linux__
bool updatesSupported() {
    return false;
}

bool httpGet(const std::string&, HttpResult& result, const ProgressFn&) {
    result = HttpResult{};
    result.error = "Updates are only available on Windows";
    return false;
}

bool httpDownload(const std::string&, const std::string&, unsigned long long, const DownloadProgressFn&, DownloadResult& result) {
    result = DownloadResult{};
    result.error = "Downloads are only available on Windows and Linux";
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
#endif

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

SystemInfo systemInfo() {
    SystemInfo info;
    struct utsname name{};
    if (uname(&name) == 0) {
        info.os = name.sysname;
        info.osVersion = name.release;
        info.architecture = name.machine;
    }
    info.logicalCores = static_cast<int>(std::thread::hardware_concurrency());
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0) {
        info.memoryBytes = static_cast<unsigned long long>(pages) * static_cast<unsigned long long>(pageSize);
    }
#ifdef __linux__
    std::ifstream cpu("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpu, line)) {
        if (line.rfind("model name", 0) == 0) {
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                size_t begin = colon + 1;
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
                    ++begin;
                }
                info.cpuName = line.substr(begin);
            }
            break;
        }
    }
#endif
    return info;
}

int processThreadCount() {
#ifdef __linux__
    std::error_code error;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", error)) {
        (void)entry;
        ++count;
    }
    return error ? 0 : count;
#else
    return 0;
#endif
}

int processHandleCount() {
#ifdef __linux__
    std::error_code error;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", error)) {
        (void)entry;
        ++count;
    }
    return error ? 0 : count;
#else
    return 0;
#endif
}

void sleepPrecise(double seconds) {
    if (seconds <= 0.0) {
        return;
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    constexpr double kSpin = 0.0001;
    if (seconds > kSpin) {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds - kSpin));
    }
    while (std::chrono::steady_clock::now() < end) {
        std::this_thread::yield();
    }
}

void setCurrentThreadBackground(bool background) {
#ifdef __linux__
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), background ? 5 : 0);
#else
    (void)background;
#endif
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

bool secureRandomBytes(unsigned char* out, size_t size) {
    if (out == nullptr || size == 0) {
        return false;
    }
    const int descriptor = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    size_t done = 0;
    while (done < size) {
        const ssize_t got = ::read(descriptor, out + done, size - done);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            ::close(descriptor);
            return false;
        }
        done += static_cast<size_t>(got);
    }
    ::close(descriptor);
    return true;
}

}

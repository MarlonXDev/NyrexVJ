#include "core/crash_handler.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nvj {

namespace {

char gReportPath[1024] = {};
char gVersion[64] = {};
volatile std::sig_atomic_t gWritten = 0;

constexpr int kMarkerFree = 0;
constexpr int kMarkerClaiming = 1;
constexpr int kMarkerIdle = 2;
constexpr int kMarkerInCall = 3;

struct Marker {
    std::atomic<int> state{kMarkerFree};
    char id[kCrashMarkerIdLength] = {};
};

Marker gMarkers[kCrashMarkerSlots];

bool validSlot(int slot) {
    return slot >= 0 && slot < kCrashMarkerSlots;
}

#ifndef _WIN32
void putText(int fd, const char* text) {
    size_t left = std::strlen(text);
    while (left > 0) {
        const ssize_t written = ::write(fd, text, left);
        if (written <= 0) {
            return;
        }
        text += written;
        left -= static_cast<size_t>(written);
    }
}

void putNumber(int fd, unsigned long long value) {
    char reversed[24];
    size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));
    char text[24];
    for (size_t i = 0; i < count; ++i) {
        text[i] = reversed[count - 1 - i];
    }
    text[count] = '\0';
    putText(fd, text);
}

void writeReport(const char* reason, unsigned long code) {
    if (gWritten != 0 || gReportPath[0] == '\0') {
        return;
    }
    gWritten = 1;
    const int fd = ::open(gReportPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return;
    }
    putText(fd, "NyrexVJ ");
    putText(fd, gVersion);
    putText(fd, "\nreason: ");
    putText(fd, reason);
    putText(fd, "\ncode: ");
    putNumber(fd, code);
    putText(fd, "\ntime: ");
    putNumber(fd, static_cast<unsigned long long>(std::time(nullptr)));
    putText(fd, "\n");
    for (int i = 0; i < kCrashMarkerSlots; ++i) {
        if (gMarkers[i].state.load() == kMarkerInCall) {
            putText(fd, "plugin: ");
            putText(fd, gMarkers[i].id);
            putText(fd, "\n");
        }
    }
    ::close(fd);
}
#else
void writeReport(const char* reason, unsigned long code) {
    if (gWritten != 0 || gReportPath[0] == '\0') {
        return;
    }
    gWritten = 1;
    std::FILE* file = std::fopen(gReportPath, "wb");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "NyrexVJ %s\nreason: %s\ncode: %lu\ntime: %lld\n", gVersion, reason, code, static_cast<long long>(std::time(nullptr)));
    for (int i = 0; i < kCrashMarkerSlots; ++i) {
        if (gMarkers[i].state.load() == kMarkerInCall) {
            std::fprintf(file, "plugin: %s\n", gMarkers[i].id);
        }
    }
    std::fclose(file);
}
#endif

const char* signalName(int signal) {
    switch (signal) {
    case SIGSEGV:
        return "invalid memory access (SIGSEGV)";
    case SIGABRT:
        return "program aborted (SIGABRT)";
    case SIGFPE:
        return "arithmetic error (SIGFPE)";
    case SIGILL:
        return "illegal instruction (SIGILL)";
    default:
        return "fatal signal";
    }
}

void onSignal(int signal) {
    writeReport(signalName(signal), static_cast<unsigned long>(signal));
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void onTerminate() {
    writeReport("uncaught C++ exception", 0);
    std::abort();
}

#ifdef _WIN32
LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    const unsigned long code = info != nullptr && info->ExceptionRecord != nullptr ? info->ExceptionRecord->ExceptionCode : 0;
    writeReport("unhandled exception", code);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}

void installCrashHandler(const std::string& reportPath, const std::string& version) {
    std::snprintf(gReportPath, sizeof(gReportPath), "%s", reportPath.c_str());
    std::snprintf(gVersion, sizeof(gVersion), "%s", version.c_str());
    gWritten = 0;
    std::signal(SIGSEGV, onSignal);
    std::signal(SIGABRT, onSignal);
    std::signal(SIGFPE, onSignal);
    std::signal(SIGILL, onSignal);
    std::set_terminate(onTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onException);
#endif
}

int crashMarkerAcquire(const std::string& id) {
    for (int i = 0; i < kCrashMarkerSlots; ++i) {
        int expected = kMarkerFree;
        if (gMarkers[i].state.compare_exchange_strong(expected, kMarkerClaiming)) {
            std::snprintf(gMarkers[i].id, sizeof(gMarkers[i].id), "%s", id.c_str());
            gMarkers[i].state.store(kMarkerIdle);
            return i;
        }
    }
    return -1;
}

void crashMarkerEnter(int slot) {
    if (validSlot(slot)) {
        gMarkers[slot].state.store(kMarkerInCall);
    }
}

void crashMarkerLeave(int slot) {
    if (validSlot(slot)) {
        int expected = kMarkerInCall;
        gMarkers[slot].state.compare_exchange_strong(expected, kMarkerIdle);
    }
}

void crashMarkerRelease(int slot) {
    if (validSlot(slot)) {
        gMarkers[slot].state.store(kMarkerFree);
    }
}

std::vector<std::string> crashMarkerActive() {
    std::vector<std::string> ids;
    for (int i = 0; i < kCrashMarkerSlots; ++i) {
        if (gMarkers[i].state.load() == kMarkerInCall) {
            ids.emplace_back(gMarkers[i].id);
        }
    }
    return ids;
}

std::vector<std::string> pluginsInCrashReport(const std::string& report) {
    std::vector<std::string> ids;
    size_t position = 0;
    while (position < report.size()) {
        size_t end = report.find('\n', position);
        if (end == std::string::npos) {
            end = report.size();
        }
        std::string line = report.substr(position, end - position);
        position = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const std::string prefix = "plugin: ";
        if (line.compare(0, prefix.size(), prefix) == 0 && line.size() > prefix.size()) {
            const std::string id = line.substr(prefix.size());
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

}

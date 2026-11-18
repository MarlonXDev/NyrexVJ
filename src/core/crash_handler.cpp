#include "core/crash_handler.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nvj {

namespace {

char gReportPath[1024] = {};
char gVersion[64] = {};
volatile std::sig_atomic_t gWritten = 0;

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
    std::fclose(file);
}

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

}

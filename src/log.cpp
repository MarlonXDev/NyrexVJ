#include "log.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace nvj {

namespace {

std::mutex gMutex;
std::ofstream gFile;
std::chrono::steady_clock::time_point gStart = std::chrono::steady_clock::now();

}

std::string userDataDir() {
    namespace fs = std::filesystem;
    fs::path base;
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    base = appData ? fs::path(appData) : fs::current_path();
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && *xdg) {
        base = xdg;
    } else if (home && *home) {
        base = fs::path(home) / ".config";
    } else {
        base = fs::current_path();
    }
#endif
    fs::path dir = base / "NyrexVJ";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

void logInit() {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gFile.is_open()) {
        return;
    }
    gFile.open(std::filesystem::path(userDataDir()) / "nyrexvj.log", std::ios::trunc);
}

void logLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(gMutex);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count();
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "[%8.3f] ", seconds);
    std::string full = std::string(prefix) + line;
    if (gFile.is_open()) {
        gFile << full << '\n';
        gFile.flush();
    }
    std::fprintf(stderr, "%s\n", full.c_str());
}

}

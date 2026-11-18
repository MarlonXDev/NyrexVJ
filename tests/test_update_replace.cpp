#ifdef _WIN32

#include "check.hpp"

#include "platform/platform.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace nvj;

std::string utf8(const fs::path& path) {
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), length, nullptr, nullptr);
    return text;
}

fs::path selfPath() {
    std::wstring buffer(1024, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer);
}

std::vector<unsigned char> readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

HANDLE launch(const fs::path& exe, const std::wstring& arguments) {
    std::wstring command = L"\"" + exe.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const std::wstring directory = exe.parent_path().wstring();
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &info)) {
        return nullptr;
    }
    CloseHandle(info.hThread);
    return info.hProcess;
}

void stop(HANDLE process) {
    if (process != nullptr) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
    }
}

fs::path makeDir(const char* name) {
    fs::path dir = fs::temp_directory_path() / (std::string("nvj_update_") + name + "_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

fs::path withSuffix(const fs::path& path, const wchar_t* suffix) {
    fs::path result = path;
    result += suffix;
    return result;
}

bool waitUntil(bool (*done)(const fs::path&), const fs::path& argument, int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        if (done(argument)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return done(argument);
}

bool present(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::vector<unsigned char> sampleBinary() {
    std::vector<unsigned char> data = readAll(selfPath());
    for (int i = 0; i < 16; ++i) {
        data.push_back('X');
    }
    return data;
}

void testRunningProgramIsReplaced() {
    const fs::path dir = makeDir("running");
    const fs::path victim = dir / "victim.exe";
    fs::copy_file(selfPath(), victim);
    HANDLE running = launch(victim, L"--sleep");
    CHECK(running != nullptr);

    const std::vector<unsigned char> replacement = {'N', 'E', 'W', '1', '2', '3'};
    platform::ReplaceMode mode = platform::ReplaceMode::Deferred;
    std::string error;
    const bool ok = platform::replaceExecutable(utf8(victim), replacement, mode, error);
    std::printf("running replace: ok=%d mode=%d error=%s\n", ok ? 1 : 0, static_cast<int>(mode), error.c_str());
    CHECK(ok);
    CHECK(mode == platform::ReplaceMode::InPlace);
    CHECK(readAll(victim) == replacement);
    CHECK(present(withSuffix(victim, L".old")));
    CHECK(!present(withSuffix(victim, L".new")));

    stop(running);
    platform::removeStaleFiles(utf8(victim));
    CHECK(!present(withSuffix(victim, L".old")));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testBusyOldNameIsSkipped() {
    const fs::path dir = makeDir("busy");
    const fs::path victim = dir / "victim.exe";
    const fs::path busyOld = withSuffix(victim, L".old");
    fs::copy_file(selfPath(), victim);
    fs::copy_file(selfPath(), busyOld);
    HANDLE running = launch(victim, L"--sleep");
    HANDLE busy = launch(busyOld, L"--sleep");
    CHECK(running != nullptr);
    CHECK(busy != nullptr);

    SetLastError(0);
    const BOOL direct = MoveFileExW(victim.c_str(), busyOld.c_str(), MOVEFILE_REPLACE_EXISTING);
    const DWORD directCode = direct ? 0 : GetLastError();
    std::printf("direct rename onto a running .old: success=%d error=%lu\n", direct ? 1 : 0, static_cast<unsigned long>(directCode));
    if (direct) {
        MoveFileExW(busyOld.c_str(), victim.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    const std::vector<unsigned char> replacement = {'N', 'E', 'W'};
    platform::ReplaceMode mode = platform::ReplaceMode::Deferred;
    std::string error;
    const bool ok = platform::replaceExecutable(utf8(victim), replacement, mode, error);
    std::printf("busy .old replace: ok=%d mode=%d error=%s\n", ok ? 1 : 0, static_cast<int>(mode), error.c_str());
    CHECK(ok);
    CHECK(mode == platform::ReplaceMode::InPlace);
    CHECK(readAll(victim) == replacement);

    stop(running);
    stop(busy);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

bool markerAppeared(const fs::path& marker) {
    return present(marker);
}

void testDeferredUpdateHelper() {
    const fs::path dir = makeDir("deferred");
    const fs::path victim = dir / "victim.exe";
    fs::copy_file(selfPath(), victim);
    const std::vector<unsigned char> original = readAll(victim);
    HANDLE running = launch(victim, L"--sleep");
    CHECK(running != nullptr);

    HANDLE hold = CreateFileW(victim.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);

    const std::vector<unsigned char> replacement = sampleBinary();
    platform::ReplaceMode mode = platform::ReplaceMode::InPlace;
    std::string error;
    const bool ok = platform::replaceExecutable(utf8(victim), replacement, mode, error);
    std::printf("locked replace: ok=%d mode=%d error=%s\n", ok ? 1 : 0, static_cast<int>(mode), error.c_str());
    CHECK(ok);
    CHECK(mode == platform::ReplaceMode::Deferred);
    CHECK(readAll(victim) == original);
    CHECK(present(withSuffix(victim, L".new")));

    if (hold != INVALID_HANDLE_VALUE) {
        CloseHandle(hold);
    }

    std::string helperError;
    const bool started = platform::startDeferredUpdate(utf8(victim), GetProcessId(running), helperError);
    std::printf("helper start: %d %s\n", started ? 1 : 0, helperError.c_str());
    CHECK(started);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(readAll(victim) == original);
    stop(running);

    const fs::path marker = dir / "ran.txt";
    CHECK(waitUntil(markerAppeared, marker, 30000));
    CHECK(readAll(victim) == replacement);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--sleep") {
        Sleep(120000);
        return 0;
    }

    int helperExit = 0;
    if (platform::handleUpdateHelper(argc, argv, helperExit)) {
        return helperExit;
    }

    const fs::path self = selfPath();
    if (self.filename() == "victim.exe") {
        std::ofstream marker(self.parent_path() / "ran.txt");
        marker << "ran";
        return 0;
    }

    testRunningProgramIsReplaced();
    testBusyOldNameIsSkipped();
    testDeferredUpdateHelper();
    return testing::finish();
}

#else

int main() {
    return 0;
}

#endif

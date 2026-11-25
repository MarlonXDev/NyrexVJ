#include "platform/platform.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>

#include "core/log.hpp"

#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace nvj::platform {

namespace {

class SleepTimer {
public:
    SleepTimer() {
        handle_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        highResolution_ = handle_ != nullptr;
        if (handle_ == nullptr) {
            handle_ = CreateWaitableTimerW(nullptr, TRUE, nullptr);
            using SetResolution = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            SetResolution setResolution = ntdll != nullptr ? reinterpret_cast<SetResolution>(reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSetTimerResolution"))) : nullptr;
            if (setResolution != nullptr) {
                ULONG actual = 0;
                setResolution(10000, TRUE, &actual);
            }
        }
    }

    ~SleepTimer() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    SleepTimer(const SleepTimer&) = delete;
    SleepTimer& operator=(const SleepTimer&) = delete;

    bool highResolution() const { return highResolution_; }

    void wait(double seconds) {
        if (handle_ == nullptr) {
            Sleep(static_cast<DWORD>(seconds * 1000.0));
            return;
        }
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(seconds * 1.0e7);
        if (SetWaitableTimer(handle_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(handle_, INFINITE);
        }
    }

private:
    HANDLE handle_ = nullptr;
    bool highResolution_ = false;
};

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

struct Handle {
    HINTERNET value = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (value) {
            WinHttpCloseHandle(value);
        }
    }
};

std::string describeError(const char* step, DWORD code) {
    return std::string(step) + " failed (error " + std::to_string(code) + ")";
}

std::string lastError(const char* step) {
    const DWORD code = GetLastError();
    return describeError(step, code);
}

constexpr int kOldNameCount = 10;

std::mutex gDeferredMutex;
std::string gDeferredExe;

std::filesystem::path suffixed(const std::filesystem::path& exe, const std::wstring& suffix) {
    std::filesystem::path result = exe;
    result += suffix;
    return result;
}

std::filesystem::path oldCandidate(const std::filesystem::path& exe, int index) {
    return suffixed(exe, index == 0 ? std::wstring(L".old") : L".old" + std::to_wstring(index));
}

void removeStale(const std::filesystem::path& exe) {
    std::error_code ec;
    for (int i = 0; i < kOldNameCount; ++i) {
        const std::filesystem::path path = oldCandidate(exe, i);
        if (std::filesystem::exists(path, ec)) {
            std::filesystem::remove(path, ec);
        }
    }
    std::filesystem::remove(suffixed(exe, L".new"), ec);
}

std::filesystem::path pickOld(const std::filesystem::path& exe) {
    std::error_code ec;
    for (int i = 0; i < kOldNameCount; ++i) {
        const std::filesystem::path path = oldCandidate(exe, i);
        if (!std::filesystem::exists(path, ec)) {
            return path;
        }
    }
    return std::filesystem::path();
}

bool transientError(DWORD code) {
    return code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
}

bool moveWithRetry(const std::filesystem::path& from, const std::filesystem::path& to, DWORD& code) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            return true;
        }
        code = GetLastError();
        if (!transientError(code)) {
            return false;
        }
        Sleep(250);
    }
    return false;
}

}

bool updatesSupported() {
    return true;
}

bool httpGet(const std::string& url, HttpResult& result, const ProgressFn& progress) {
    result = HttpResult{};
    std::wstring wideUrl = widen(url);

    wchar_t host[256];
    wchar_t path[2048];
    wchar_t extra[2048];
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
        result.error = lastError("Reading the address");
        return false;
    }
    std::wstring hostName(host, parts.dwHostNameLength);
    std::wstring objectName = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);

    Handle session;
    session.value = WinHttpOpen(L"NyrexVJ-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) {
        session.value = WinHttpOpen(L"NyrexVJ-Updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session.value) {
        result.error = lastError("Opening the connection");
        return false;
    }
    WinHttpSetTimeouts(session.value, 10000, 10000, 30000, 30000);

    Handle connection;
    connection.value = WinHttpConnect(session.value, hostName.c_str(), parts.nPort, 0);
    if (!connection.value) {
        result.error = lastError("Connecting");
        return false;
    }

    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", objectName.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request.value) {
        result.error = lastError("Creating the request");
        return false;
    }

    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        result.error = lastError("Sending the request");
        return false;
    }
    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        result.error = lastError("Receiving the answer");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.status = static_cast<int>(status);

    DWORD contentLength = 0;
    DWORD lengthSize = sizeof(contentLength);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
        contentLength = 0;
    }
    if (contentLength > 0 && contentLength < 256u * 1024u * 1024u) {
        result.body.reserve(contentLength);
    }

    const size_t limit = 256u * 1024u * 1024u;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            result.error = lastError("Reading the data");
            return false;
        }
        if (available == 0) {
            break;
        }
        size_t offset = result.body.size();
        if (offset + available > limit) {
            result.error = "The download is too large";
            return false;
        }
        result.body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.value, result.body.data() + offset, available, &read)) {
            result.error = lastError("Reading the data");
            return false;
        }
        result.body.resize(offset + read);
        if (progress) {
            progress(result.body.size(), contentLength);
        }
    }

    if (result.status != 200) {
        result.error = "HTTP " + std::to_string(result.status);
        return false;
    }
    return true;
}

bool httpDownload(const std::string& url, const std::string& destinationUtf8, unsigned long long maxBytes, const DownloadProgressFn& progress, DownloadResult& result) {
    result = DownloadResult{};
    if (url.rfind("https://", 0) != 0) {
        result.error = "Only secure (https) addresses are allowed";
        return false;
    }
    std::wstring wideUrl = widen(url);
    wchar_t host[256];
    wchar_t path[2048];
    wchar_t extra[2048];
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
        result.error = lastError("Reading the address");
        return false;
    }
    std::wstring hostName(host, parts.dwHostNameLength);
    std::wstring objectName = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);

    Handle session;
    session.value = WinHttpOpen(L"NyrexVJ-FFmpegSetup", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) {
        session.value = WinHttpOpen(L"NyrexVJ-FFmpegSetup", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session.value) {
        result.error = lastError("Opening the connection");
        return false;
    }
    WinHttpSetTimeouts(session.value, 15000, 15000, 30000, 30000);

    Handle connection;
    connection.value = WinHttpConnect(session.value, hostName.c_str(), parts.nPort, 0);
    if (!connection.value) {
        result.error = lastError("Connecting");
        return false;
    }
    Handle request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", objectName.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request.value) {
        result.error = lastError("Creating the request");
        return false;
    }
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        result.error = lastError("Sending the request");
        return false;
    }
    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        result.error = lastError("Receiving the answer");
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.status = static_cast<int>(status);
    if (status != 200) {
        result.error = "HTTP " + std::to_string(status);
        return false;
    }
    unsigned long long total = 0;
    {
        wchar_t lengthText[32] = {0};
        DWORD lengthSize = sizeof(lengthText);
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
            total = _wcstoui64(lengthText, nullptr, 10);
        }
    }
    const std::filesystem::path target(widen(destinationUtf8));
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "The target file cannot be created";
        return false;
    }
    std::vector<unsigned char> buffer(256 * 1024);
    auto fail = [&](const std::string& message) {
        out.close();
        std::error_code ec;
        std::filesystem::remove(target, ec);
        result.error = message;
        return false;
    };
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            return fail(lastError("Reading the data"));
        }
        if (available == 0) {
            break;
        }
        if (available > buffer.size()) {
            available = static_cast<DWORD>(buffer.size());
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), available, &read)) {
            return fail(lastError("Reading the data"));
        }
        if (result.bytes + read > maxBytes) {
            return fail("The download is larger than expected");
        }
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(read));
        if (!out) {
            return fail("The file could not be written (disk full?)");
        }
        result.bytes += read;
        if (progress && !progress(result.bytes, total)) {
            result.cancelled = true;
            return fail("Cancelled");
        }
    }
    out.close();
    if (!out) {
        return fail("The file could not be written (disk full?)");
    }
    return true;
}

std::string currentExePath() {
    std::wstring buffer(512, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::string();
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return narrow(buffer);
}

bool replaceExecutable(const std::string& exePath, const std::vector<unsigned char>& newBinary, ReplaceMode& mode, std::string& error) {
    namespace fs = std::filesystem;
    mode = ReplaceMode::InPlace;
    if (exePath.empty()) {
        error = "Could not find the program file";
        return false;
    }
    const fs::path exe = fs::path(widen(exePath));
    const fs::path fresh = suffixed(exe, L".new");
    removeStale(exe);

    {
        std::ofstream file(fresh, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Could not write next to the program (no permission?)";
            return false;
        }
        file.write(reinterpret_cast<const char*>(newBinary.data()), static_cast<std::streamsize>(newBinary.size()));
        file.flush();
        if (!file) {
            error = "Could not save the downloaded update";
            return false;
        }
    }

    std::error_code ec;
    DWORD code = 0;
    const fs::path old = pickOld(exe);
    if (!old.empty() && moveWithRetry(exe, old, code)) {
        if (MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            return true;
        }
        const DWORD placeCode = GetLastError();
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        fs::remove(fresh, ec);
        error = describeError("Putting the new program in place", placeCode);
        return false;
    }

    logLine("Update: the running program could not be renamed (error " + std::to_string(code) + "), the update is finished after the restart instead");
    mode = ReplaceMode::Deferred;
    return true;
}

bool startDeferredUpdate(const std::string& exePath, unsigned long waitPid, std::string& error) {
    const std::wstring exe = widen(exePath);
    const std::wstring fresh = exe + L".new";
    if (!std::filesystem::exists(fresh)) {
        error = "The downloaded update is missing";
        return false;
    }
    std::wstring command = L"\"" + fresh + L"\" --finish-update " + std::to_wstring(waitPid) + L" \"" + exe + L"\"";
    const std::wstring directory = std::filesystem::path(exe).parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(fresh.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &info)) {
        error = lastError("Starting the update helper");
        return false;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
}

bool replaceSelf(const std::vector<unsigned char>& newBinary, std::string& error) {
    const std::string exe = currentExePath();
    ReplaceMode mode = ReplaceMode::InPlace;
    if (!replaceExecutable(exe, newBinary, mode, error)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(gDeferredMutex);
    gDeferredExe = mode == ReplaceMode::Deferred ? exe : std::string();
    return true;
}

bool relaunchSelf() {
    std::string deferred;
    {
        std::lock_guard<std::mutex> lock(gDeferredMutex);
        deferred = gDeferredExe;
    }
    std::string ignored;
    if (!deferred.empty() && startDeferredUpdate(deferred, GetCurrentProcessId(), ignored)) {
        return true;
    }
    std::wstring exe = widen(currentExePath());
    if (exe.empty()) {
        return false;
    }
    std::wstring command = L"\"" + exe + L"\"";
    std::wstring directory = std::filesystem::path(exe).parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &info)) {
        return false;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
}

void removeStaleFiles(const std::string& exePath) {
    if (!exePath.empty()) {
        removeStale(std::filesystem::path(widen(exePath)));
    }
}

void cleanupOldBinary() {
    namespace fs = std::filesystem;
    const std::string exePath = currentExePath();
    if (exePath.empty()) {
        return;
    }
    const fs::path exe = fs::path(widen(exePath));
    std::error_code ec;
    for (int attempt = 0; attempt < 10; ++attempt) {
        bool left = false;
        for (int i = 0; i < kOldNameCount; ++i) {
            if (fs::exists(oldCandidate(exe, i), ec)) {
                left = true;
            }
        }
        if (!left) {
            break;
        }
        removeStale(exe);
        Sleep(200);
    }
    fs::remove(suffixed(exe, L".new"), ec);
}

bool handleUpdateHelper(int, char**, int& exitCode) {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) {
        return false;
    }
    DWORD pid = 0;
    std::wstring target;
    bool found = false;
    for (int i = 1; i + 2 < count; ++i) {
        if (std::wstring(args[i]) == L"--finish-update") {
            pid = static_cast<DWORD>(std::wcstoul(args[i + 1], nullptr, 10));
            target = args[i + 2];
            found = true;
            break;
        }
    }
    LocalFree(args);
    if (!found) {
        return false;
    }

    if (pid != 0) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (process != nullptr) {
            WaitForSingleObject(process, 60000);
            CloseHandle(process);
        }
    }

    const std::wstring self = widen(currentExePath());
    bool copied = false;
    for (int attempt = 0; attempt < 150 && !copied; ++attempt) {
        copied = CopyFileW(self.c_str(), target.c_str(), FALSE) != 0;
        if (!copied) {
            Sleep(200);
        }
    }

    std::wstring command = L"\"" + target + L"\"";
    const std::wstring directory = std::filesystem::path(target).parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &info)) {
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
    }
    exitCode = copied ? 0 : 1;
    return true;
}

bool openUrl(const std::string& url) {
    std::wstring wide = widen(url);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool openFolder(const std::string& path) {
    std::wstring wide = widen(path);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

size_t processMemoryBytes() {
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return 0;
    }
    return static_cast<size_t>(counters.WorkingSetSize);
}

int processThreadCount() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    int count = 0;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == self) {
                ++count;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

int processHandleCount() {
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count)) {
        return 0;
    }
    return static_cast<int>(count);
}

double processCpuSeconds() {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return 0.0;
    }
    const auto seconds = [](const FILETIME& value) {
        ULARGE_INTEGER wide;
        wide.LowPart = value.dwLowDateTime;
        wide.HighPart = value.dwHighDateTime;
        return static_cast<double>(wide.QuadPart) * 1.0e-7;
    };
    return seconds(kernel) + seconds(user);
}

void sleepPrecise(double seconds) {
    if (seconds <= 0.0) {
        return;
    }
    thread_local SleepTimer timer;
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    const double spin = timer.highResolution() ? 0.0006 : 0.0016;
    if (seconds > spin) {
        timer.wait(seconds - spin);
    }
    while (std::chrono::steady_clock::now() < end) {
        YieldProcessor();
    }
}

void setCurrentThreadBackground(bool background) {
    SetThreadPriority(GetCurrentThread(), background ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL);
}

unsigned long currentProcessId() {
    return static_cast<unsigned long>(GetCurrentProcessId());
}

bool processAlive(unsigned long pid) {
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}

bool secureRandomBytes(unsigned char* out, size_t size) {
    if (out == nullptr || size == 0) {
        return false;
    }
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

}

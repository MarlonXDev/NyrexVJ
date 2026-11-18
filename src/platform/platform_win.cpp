#include "platform/platform.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <filesystem>
#include <fstream>

namespace nvj::platform {

namespace {

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

std::string lastError(const char* step) {
    return std::string(step) + " failed (error " + std::to_string(GetLastError()) + ")";
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

bool replaceSelf(const std::vector<unsigned char>& newBinary, std::string& error) {
    namespace fs = std::filesystem;
    fs::path exe = fs::path(widen(currentExePath()));
    if (exe.empty()) {
        error = "Could not find the program file";
        return false;
    }
    fs::path fresh = exe;
    fresh += L".new";
    fs::path old = exe;
    old += L".old";

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
    fs::remove(old, ec);

    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = lastError("Moving the old program");
        fs::remove(fresh, ec);
        return false;
    }
    if (!MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = lastError("Putting the new program in place");
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        fs::remove(fresh, ec);
        return false;
    }
    return true;
}

bool relaunchSelf() {
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

void cleanupOldBinary() {
    namespace fs = std::filesystem;
    fs::path exe = fs::path(widen(currentExePath()));
    if (exe.empty()) {
        return;
    }
    std::error_code ec;
    fs::path old = exe;
    old += L".old";
    fs::path fresh = exe;
    fresh += L".new";
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (!fs::exists(old, ec)) {
            break;
        }
        if (fs::remove(old, ec)) {
            break;
        }
        Sleep(200);
    }
    fs::remove(fresh, ec);
}

bool openUrl(const std::string& url) {
    std::wstring wide = widen(url);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool openFolder(const std::string& path) {
    std::wstring wide = widen(path);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}

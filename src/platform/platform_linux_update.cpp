#include "platform/platform.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nvj::platform {

namespace {

constexpr int kOptUrl = 10002;
constexpr int kOptWriteData = 10001;
constexpr int kOptWriteFunction = 20011;
constexpr int kOptUserAgent = 10018;
constexpr int kOptFollow = 52;
constexpr int kOptMaxRedirects = 68;
constexpr int kOptNoProgress = 43;
constexpr int kOptConnectTimeout = 78;
constexpr int kOptLowSpeedLimit = 19;
constexpr int kOptLowSpeedTime = 20;
constexpr int kOptNoSignal = 99;
constexpr int kOptProtocols = 181;
constexpr int kOptRedirProtocols = 182;
constexpr int kOptXferFunction = 20219;
constexpr int kOptXferData = 10057;
constexpr int kOptErrorBuffer = 10010;
constexpr int kInfoResponseCode = 0x200002;
constexpr long kProtocolsWeb = 3;
constexpr size_t kBodyLimit = 256u * 1024u * 1024u;

using EasyInitFn = void* (*)();
using EasyCleanupFn = void (*)(void*);
using EasySetoptFn = int (*)(void*, int, ...);
using EasyPerformFn = int (*)(void*);
using EasyGetinfoFn = int (*)(void*, int, ...);
using EasyStrErrorFn = const char* (*)(int);
using GlobalInitFn = int (*)(long);

struct CurlApi {
    void* handle = nullptr;
    EasyInitFn init = nullptr;
    EasyCleanupFn cleanup = nullptr;
    EasySetoptFn setopt = nullptr;
    EasyPerformFn perform = nullptr;
    EasyGetinfoFn getinfo = nullptr;
    EasyStrErrorFn strerror = nullptr;
};

struct CurlState {
    std::mutex mutex;
    CurlApi api;
    bool loaded = false;
    std::string error;
    std::string path;
};

CurlState& curlState() {
    static CurlState state;
    return state;
}

template <class T>
T symbol(void* handle, const char* name) {
    return reinterpret_cast<T>(dlsym(handle, name));
}

bool loadCurlLocked(CurlState& state) {
    if (state.loaded) {
        return true;
    }
    std::vector<std::string> names;
    if (!state.path.empty()) {
        names.push_back(state.path);
    } else if (const char* env = std::getenv("NYREXVJ_CURL_LIB")) {
        if (*env) {
            names.push_back(env);
        }
    }
    if (names.empty()) {
        names = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
    }
    for (const std::string& name : names) {
        void* handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            continue;
        }
        CurlApi api;
        api.handle = handle;
        api.init = symbol<EasyInitFn>(handle, "curl_easy_init");
        api.cleanup = symbol<EasyCleanupFn>(handle, "curl_easy_cleanup");
        api.setopt = symbol<EasySetoptFn>(handle, "curl_easy_setopt");
        api.perform = symbol<EasyPerformFn>(handle, "curl_easy_perform");
        api.getinfo = symbol<EasyGetinfoFn>(handle, "curl_easy_getinfo");
        api.strerror = symbol<EasyStrErrorFn>(handle, "curl_easy_strerror");
        GlobalInitFn globalInit = symbol<GlobalInitFn>(handle, "curl_global_init");
        if (!api.init || !api.cleanup || !api.setopt || !api.perform || !api.getinfo) {
            dlclose(handle);
            continue;
        }
        if (globalInit) {
            globalInit(3);
        }
        state.api = api;
        state.loaded = true;
        state.error.clear();
        return true;
    }
    state.error = "The update download needs libcurl (libcurl.so.4), which was not found on this computer";
    return false;
}

bool acquireCurl(CurlApi& api, std::string& error) {
    CurlState& state = curlState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!loadCurlLocked(state)) {
        error = state.error;
        return false;
    }
    api = state.api;
    return true;
}

struct Transfer {
    HttpResult* result = nullptr;
    const ProgressFn* progress = nullptr;
    bool tooLarge = false;
};

size_t writeBody(char* data, size_t size, size_t count, void* user) {
    Transfer* transfer = static_cast<Transfer*>(user);
    const size_t bytes = size * count;
    if (transfer->result->body.size() + bytes > kBodyLimit) {
        transfer->tooLarge = true;
        return 0;
    }
    const unsigned char* begin = reinterpret_cast<const unsigned char*>(data);
    transfer->result->body.insert(transfer->result->body.end(), begin, begin + bytes);
    return bytes;
}

int reportProgress(void* user, int64_t total, int64_t now, int64_t, int64_t) {
    Transfer* transfer = static_cast<Transfer*>(user);
    if (transfer->progress && *transfer->progress && now >= 0) {
        (*transfer->progress)(static_cast<size_t>(now), total > 0 ? static_cast<size_t>(total) : 0);
    }
    return 0;
}

struct DownloadTransfer {
    std::FILE* file = nullptr;
    const DownloadProgressFn* progress = nullptr;
    unsigned long long written = 0;
    unsigned long long limit = 0;
    bool cancelled = false;
    bool tooLarge = false;
    bool writeFailed = false;
};

size_t writeFile(char* data, size_t size, size_t count, void* user) {
    DownloadTransfer* transfer = static_cast<DownloadTransfer*>(user);
    const size_t bytes = size * count;
    if (transfer->written + bytes > transfer->limit) {
        transfer->tooLarge = true;
        return 0;
    }
    if (bytes > 0 && std::fwrite(data, 1, bytes, transfer->file) != bytes) {
        transfer->writeFailed = true;
        return 0;
    }
    transfer->written += bytes;
    return bytes;
}

int reportDownloadProgress(void* user, int64_t total, int64_t now, int64_t, int64_t) {
    DownloadTransfer* transfer = static_cast<DownloadTransfer*>(user);
    if (transfer->progress && *transfer->progress && now >= 0) {
        if (!(*transfer->progress)(static_cast<unsigned long long>(now), total > 0 ? static_cast<unsigned long long>(total) : 0)) {
            transfer->cancelled = true;
            return 1;
        }
    }
    return 0;
}

std::string cachedExePath;
std::mutex exePathMutex;

bool commandExists(const char* name) {
    const char* path = std::getenv("PATH");
    if (!path) {
        return false;
    }
    std::string list = path;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        const std::string directory = list.substr(start, end - start);
        if (!directory.empty() && access((directory + "/" + name).c_str(), X_OK) == 0) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

bool spawnDetached(const char* program, const std::string& argument) {
    if (!commandExists(program)) {
        return false;
    }
    const pid_t first = fork();
    if (first < 0) {
        return false;
    }
    if (first == 0) {
        if (fork() != 0) {
            _exit(0);
        }
        setsid();
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
            if (null > 2) {
                close(null);
            }
        }
        execlp(program, program, argument.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(first, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}

void setCurlLibraryForTesting(const std::string& path) {
    CurlState& state = curlState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.loaded && state.api.handle) {
        dlclose(state.api.handle);
    }
    state.api = CurlApi{};
    state.loaded = false;
    state.error.clear();
    state.path = path;
}

bool updatesSupported() {
    CurlApi api;
    std::string error;
    return acquireCurl(api, error);
}

bool httpGet(const std::string& url, HttpResult& result, const ProgressFn& progress) {
    result = HttpResult{};
    CurlApi api;
    if (!acquireCurl(api, result.error)) {
        return false;
    }
    void* easy = api.init();
    if (!easy) {
        result.error = "Opening the connection failed";
        return false;
    }
    Transfer transfer;
    transfer.result = &result;
    transfer.progress = &progress;
    char errorText[256] = {0};
    api.setopt(easy, kOptUrl, url.c_str());
    api.setopt(easy, kOptUserAgent, "NyrexVJ-Updater");
    api.setopt(easy, kOptFollow, 1L);
    api.setopt(easy, kOptMaxRedirects, 8L);
    api.setopt(easy, kOptProtocols, kProtocolsWeb);
    api.setopt(easy, kOptRedirProtocols, kProtocolsWeb);
    api.setopt(easy, kOptConnectTimeout, 10L);
    api.setopt(easy, kOptLowSpeedLimit, 1L);
    api.setopt(easy, kOptLowSpeedTime, 30L);
    api.setopt(easy, kOptNoSignal, 1L);
    api.setopt(easy, kOptWriteFunction, &writeBody);
    api.setopt(easy, kOptWriteData, &transfer);
    api.setopt(easy, kOptNoProgress, 0L);
    api.setopt(easy, kOptXferFunction, &reportProgress);
    api.setopt(easy, kOptXferData, &transfer);
    api.setopt(easy, kOptErrorBuffer, errorText);
    const int code = api.perform(easy);
    long status = 0;
    if (code == 0) {
        api.getinfo(easy, kInfoResponseCode, &status);
    }
    api.cleanup(easy);
    if (code != 0) {
        if (transfer.tooLarge) {
            result.error = "The download is too large";
        } else {
            const char* text = errorText[0] ? errorText : (api.strerror ? api.strerror(code) : nullptr);
            result.error = std::string("Download failed: ") + (text ? text : "unknown error");
        }
        return false;
    }
    result.status = static_cast<int>(status);
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
    CurlApi api;
    if (!acquireCurl(api, result.error)) {
        return false;
    }
    std::FILE* file = std::fopen(destinationUtf8.c_str(), "wb");
    if (!file) {
        result.error = "The target file cannot be created";
        return false;
    }
    void* easy = api.init();
    if (!easy) {
        std::fclose(file);
        std::remove(destinationUtf8.c_str());
        result.error = "Opening the connection failed";
        return false;
    }
    DownloadTransfer transfer;
    transfer.file = file;
    transfer.progress = &progress;
    transfer.limit = maxBytes;
    char errorText[256] = {0};
    api.setopt(easy, kOptUrl, url.c_str());
    api.setopt(easy, kOptUserAgent, "NyrexVJ-FFmpegSetup");
    api.setopt(easy, kOptFollow, 1L);
    api.setopt(easy, kOptMaxRedirects, 8L);
    api.setopt(easy, kOptProtocols, 2L);
    api.setopt(easy, kOptRedirProtocols, 2L);
    api.setopt(easy, kOptConnectTimeout, 15L);
    api.setopt(easy, kOptLowSpeedLimit, 1L);
    api.setopt(easy, kOptLowSpeedTime, 30L);
    api.setopt(easy, kOptNoSignal, 1L);
    api.setopt(easy, kOptWriteFunction, &writeFile);
    api.setopt(easy, kOptWriteData, &transfer);
    api.setopt(easy, kOptNoProgress, 0L);
    api.setopt(easy, kOptXferFunction, &reportDownloadProgress);
    api.setopt(easy, kOptXferData, &transfer);
    api.setopt(easy, kOptErrorBuffer, errorText);
    const int code = api.perform(easy);
    long status = 0;
    if (code == 0) {
        api.getinfo(easy, kInfoResponseCode, &status);
    }
    api.cleanup(easy);
    const bool flushed = std::fclose(file) == 0;
    result.bytes = transfer.written;
    result.status = static_cast<int>(status);
    auto fail = [&](const std::string& message) {
        std::remove(destinationUtf8.c_str());
        result.error = message;
        return false;
    };
    if (transfer.cancelled) {
        result.cancelled = true;
        return fail("Cancelled");
    }
    if (code != 0) {
        if (transfer.tooLarge) {
            return fail("The download is larger than expected");
        }
        if (transfer.writeFailed) {
            return fail("The file could not be written (disk full?)");
        }
        const char* text = errorText[0] ? errorText : (api.strerror ? api.strerror(code) : nullptr);
        return fail(std::string("Download failed: ") + (text ? text : "unknown error"));
    }
    if (status != 200) {
        return fail("HTTP " + std::to_string(status));
    }
    if (!flushed) {
        return fail("The file could not be written (disk full?)");
    }
    return true;
}

std::string currentExePath() {
    std::lock_guard<std::mutex> lock(exePathMutex);
    if (!cachedExePath.empty()) {
        return cachedExePath;
    }
    std::error_code ec;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return std::string();
    }
    std::string text = path.string();
    const std::string suffix = " (deleted)";
    if (text.size() > suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
        text.erase(text.size() - suffix.size());
    }
    cachedExePath = text;
    return cachedExePath;
}

bool replaceExecutable(const std::string& exePath, const std::vector<unsigned char>& newBinary, ReplaceMode& mode, std::string& error) {
    mode = ReplaceMode::InPlace;
    if (exePath.empty() || newBinary.empty()) {
        error = "There is nothing to install";
        return false;
    }
    const std::string temporary = exePath + ".new";
    struct stat info{};
    mode_t permissions = 0755;
    if (stat(exePath.c_str(), &info) == 0) {
        permissions = (info.st_mode & 07777) | 0111;
    }
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, permissions);
    if (fd < 0) {
        error = "Cannot write next to the program (" + std::string(std::strerror(errno)) + "): " + temporary;
        return false;
    }
    size_t written = 0;
    while (written < newBinary.size()) {
        const ssize_t count = write(fd, newBinary.data() + written, newBinary.size() - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = "Writing the new program failed: " + std::string(std::strerror(errno));
            close(fd);
            unlink(temporary.c_str());
            return false;
        }
        written += static_cast<size_t>(count);
    }
    fchmod(fd, permissions);
    fsync(fd);
    close(fd);
    if (rename(temporary.c_str(), exePath.c_str()) != 0) {
        error = "Replacing the program failed: " + std::string(std::strerror(errno));
        unlink(temporary.c_str());
        return false;
    }
    return true;
}

bool startDeferredUpdate(const std::string&, unsigned long, std::string& error) {
    error = "A deferred update is not needed on Linux";
    return false;
}

void removeStaleFiles(const std::string& exePath) {
    if (exePath.empty()) {
        return;
    }
    unlink((exePath + ".new").c_str());
    unlink((exePath + ".old").c_str());
}

bool handleUpdateHelper(int, char**, int&) {
    return false;
}

bool replaceSelf(const std::vector<unsigned char>& newBinary, std::string& error) {
    ReplaceMode mode = ReplaceMode::InPlace;
    return replaceExecutable(currentExePath(), newBinary, mode, error);
}

bool relaunchSelf() {
    const std::string exe = currentExePath();
    if (exe.empty()) {
        return false;
    }
    const pid_t first = fork();
    if (first < 0) {
        return false;
    }
    if (first == 0) {
        if (fork() != 0) {
            _exit(0);
        }
        setsid();
        const std::string directory = std::filesystem::path(exe).parent_path().string();
        if (!directory.empty() && chdir(directory.c_str()) != 0) {
            _exit(126);
        }
        execl(exe.c_str(), exe.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(first, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void cleanupOldBinary() {
    removeStaleFiles(currentExePath());
}

bool openUrl(const std::string& url) {
    return spawnDetached("xdg-open", url);
}

bool openFolder(const std::string& path) {
    return spawnDetached("xdg-open", path);
}

}

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace nvj::platform {

struct HttpResult {
    int status = 0;
    std::vector<unsigned char> body;
    std::string error;
};

using ProgressFn = std::function<void(size_t received, size_t total)>;

struct DownloadResult {
    int status = 0;
    unsigned long long bytes = 0;
    bool cancelled = false;
    std::string error;
};

using DownloadProgressFn = std::function<bool(unsigned long long received, unsigned long long total)>;

bool updatesSupported();
bool httpGet(const std::string& url, HttpResult& result, const ProgressFn& progress = nullptr);
bool httpDownload(const std::string& url, const std::string& destinationUtf8, unsigned long long maxBytes, const DownloadProgressFn& progress, DownloadResult& result);
std::string currentExePath();
enum class ReplaceMode { InPlace, Deferred };

bool replaceExecutable(const std::string& exePath, const std::vector<unsigned char>& newBinary, ReplaceMode& mode, std::string& error);
bool startDeferredUpdate(const std::string& exePath, unsigned long waitPid, std::string& error);
void removeStaleFiles(const std::string& exePath);
bool handleUpdateHelper(int argc, char** argv, int& exitCode);
bool replaceSelf(const std::vector<unsigned char>& newBinary, std::string& error);
bool relaunchSelf();
void cleanupOldBinary();
bool openUrl(const std::string& url);
bool openFolder(const std::string& path);
size_t processMemoryBytes();
double processCpuSeconds();
void sleepPrecise(double seconds);
void setCurrentThreadBackground(bool background);
unsigned long currentProcessId();
bool processAlive(unsigned long pid);

#ifdef __linux__
void setCurlLibraryForTesting(const std::string& path);
#endif

}

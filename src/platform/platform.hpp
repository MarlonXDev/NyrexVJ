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

bool updatesSupported();
bool httpGet(const std::string& url, HttpResult& result, const ProgressFn& progress = nullptr);
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

}

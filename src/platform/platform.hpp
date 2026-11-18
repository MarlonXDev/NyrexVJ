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
bool replaceSelf(const std::vector<unsigned char>& newBinary, std::string& error);
bool relaunchSelf();
void cleanupOldBinary();
bool openUrl(const std::string& url);
bool openFolder(const std::string& path);

}

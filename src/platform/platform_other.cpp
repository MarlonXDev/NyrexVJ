#include "platform/platform.hpp"

#include <filesystem>

namespace nvj::platform {

bool updatesSupported() {
    return false;
}

bool httpGet(const std::string&, HttpResult& result, const ProgressFn&) {
    result = HttpResult{};
    result.error = "Updates are only available on Windows";
    return false;
}

std::string currentExePath() {
    std::error_code ec;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : path.string();
}

bool replaceSelf(const std::vector<unsigned char>&, std::string& error) {
    error = "Updates are only available on Windows";
    return false;
}

bool relaunchSelf() {
    return false;
}

void cleanupOldBinary() {
}

bool openUrl(const std::string&) {
    return false;
}

bool openFolder(const std::string&) {
    return false;
}

}

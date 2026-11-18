#include "updater.hpp"

#include "log.hpp"
#include "platform.hpp"
#include "sha256.hpp"

namespace nvj {

namespace {

constexpr size_t kMinBinarySize = 512 * 1024;

std::string asText(const std::vector<unsigned char>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

}

UpdaterBackend platformBackend() {
    UpdaterBackend backend;
    backend.get = [](const std::string& url, platform::HttpResult& result, const platform::ProgressFn& progress) { return platform::httpGet(url, result, progress); };
    backend.replace = [](const std::vector<unsigned char>& data, std::string& error) { return platform::replaceSelf(data, error); };
    backend.supported = platform::updatesSupported();
    return backend;
}

Updater::Updater() : backend_(platformBackend()) {
}

void Updater::setBackend(UpdaterBackend backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    backend_ = std::move(backend);
}

Updater::~Updater() {
    joinWorker();
}

void Updater::joinWorker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Updater::setChannel(UpdateChannel channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_ = channel;
}

UpdateChannel Updater::channel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channel_;
}

UpdateStatus Updater::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void Updater::set(UpdateState state, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.message = message;
    if (state != UpdateState::Downloading) {
        status_.progress = 0.0f;
    }
}

void Updater::checkAsync() {
    if (busy_.load()) {
        return;
    }
    UpdaterBackend backend;
    UpdateChannel channel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backend = backend_;
        channel = channel_;
    }
    if (!backend.supported) {
        set(UpdateState::Unsupported, "Automatic updates are available in the Windows version");
        return;
    }
    joinWorker();
    busy_.store(true);
    set(UpdateState::Checking, "Checking for updates");
    worker_ = std::thread([this, channel, backend]() { runCheck(channel, backend); });
}

void Updater::installAsync() {
    if (busy_.load()) {
        return;
    }
    UpdateStatus current = status();
    if (current.state != UpdateState::Available) {
        return;
    }
    UpdaterBackend backend;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backend = backend_;
    }
    joinWorker();
    busy_.store(true);
    set(UpdateState::Downloading, "Downloading update");
    ReleaseInfo release = current.release;
    worker_ = std::thread([this, release, backend]() { runInstall(release, backend); });
}

void Updater::runCheck(UpdateChannel channel, UpdaterBackend backend) {
    platform::HttpResult result;
    if (!backend.get(releaseApiUrl(channel), result, nullptr)) {
        std::string message;
        if (result.status == 404) {
            message = "No update information found. If the GitHub repository is still private, updates cannot be checked yet";
        } else if (result.status == 403) {
            message = "GitHub refused the request (too many checks?). Try again later";
        } else {
            message = "Could not reach GitHub: " + result.error;
        }
        logLine("Update check failed: " + result.error);
        set(UpdateState::Failed, message);
        busy_.store(false);
        return;
    }

    ReleaseInfo release;
    std::string error;
    if (!parseRelease(asText(result.body), release, error)) {
        logLine("Update check failed: " + error);
        set(UpdateState::Failed, error);
        busy_.store(false);
        return;
    }

    CurrentBuild build = currentBuild();
    bool available = isUpdateAvailable(release, channel, build);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.release = release;
    }
    if (available) {
        std::string label = channel == UpdateChannel::Nightly ? release.name : release.tag;
        logLine("Update available: " + label);
        set(UpdateState::Available, "Update available: " + label);
    } else {
        logLine("No update available");
        set(UpdateState::UpToDate, "You are up to date");
    }
    busy_.store(false);
}

void Updater::runInstall(ReleaseInfo release, UpdaterBackend backend) {
    auto fail = [this](const std::string& message) {
        logLine("Update failed: " + message);
        set(UpdateState::Failed, message);
        busy_.store(false);
    };

    if (release.sumsUrl.empty()) {
        fail("This release has no checksum file, so it will not be installed");
        return;
    }

    platform::HttpResult sums;
    if (!backend.get(release.sumsUrl, sums, nullptr)) {
        fail("Could not download the checksum file: " + sums.error);
        return;
    }
    std::string expected;
    if (!findHash(asText(sums.body), kExeAssetName, expected)) {
        fail("The checksum file does not list the program");
        return;
    }

    platform::HttpResult binary;
    bool downloaded = backend.get(release.exeUrl, binary, [this](size_t received, size_t total) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.progress = total > 0 ? static_cast<float>(received) / static_cast<float>(total) : 0.0f;
    });
    if (!downloaded) {
        fail("Download failed: " + binary.error);
        return;
    }

    if (binary.body.size() < kMinBinarySize || binary.body[0] != 'M' || binary.body[1] != 'Z') {
        fail("The downloaded file is not a valid program");
        return;
    }
    if (sha256Hex(binary.body) != expected) {
        fail("The download is damaged (checksum does not match)");
        return;
    }

    std::string error;
    if (!backend.replace(binary.body, error)) {
        fail(error);
        return;
    }

    logLine("Update installed");
    set(UpdateState::ReadyToRestart, "Update installed. Restart to use the new version");
    busy_.store(false);
}

}

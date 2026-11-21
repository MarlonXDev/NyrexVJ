#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nvj::ffmpeg_setup {

struct LibraryFile {
    std::string base;
    int major = 0;
};

struct Package {
    std::string version;
    std::string platform;
    std::string assetName;
    std::string url;
    std::string checksumUrl;
    bool zip = false;
    int avcodecMajor = 0;
    std::vector<LibraryFile> libraries;
};

struct Capabilities {
    bool compiled = false;
    bool loaded = false;
    bool dxvDecoder = false;
    bool hapEncoder = false;
    bool hapEncoderWorks = false;
    bool movMuxer = false;
    bool snappy = false;
    std::string folder;
    std::string loadedFrom;
    std::string problem;
    std::string version;
    std::string license;
    std::vector<std::string> missing;
    bool ready() const { return compiled && loaded && dxvDecoder && hapEncoderWorks && movMuxer; }
};

enum class License { Ok, Gpl, NonFree, Unknown };

struct LicenseReport {
    License verdict = License::Unknown;
    bool configurationFound = false;
    bool version3 = false;
};

enum class Stage { Idle, Checksums, Download, Verify, Extract, Install, Reload, Done, Failed, Cancelled };

struct Status {
    Stage stage = Stage::Idle;
    double progress = 0.0;
    unsigned long long done = 0;
    unsigned long long total = 0;
    bool running = false;
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string version;
    std::string license;
};

struct InstallResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string hash;
    std::string license;
    std::string version;
};

using ProgressFn = std::function<bool(Stage stage, unsigned long long done, unsigned long long total)>;

constexpr unsigned long long kMaxArchiveBytes = 400ull * 1024ull * 1024ull;
constexpr unsigned long long kRequiredFreeBytes = 700ull * 1024ull * 1024ull;

std::string platformKey();
int compiledAvcodecMajor();
bool choosePackage(int avcodecMajor, const std::string& platform, Package& package, std::string& reason);
bool trustedUrl(const std::string& url);
bool parseChecksums(const std::string& text, const std::string& asset, std::string& hash);
std::string libraryFileName(const LibraryFile& library, const std::string& platform);
std::vector<std::string> requiredFiles(const Package& package);
std::string archiveMap(const Package& package, const std::string& entryName);
LicenseReport inspectLibraryLicense(const std::filesystem::path& file);
bool licenseTextIsLgpl(const std::string& text);
std::string metadataFileName();

InstallResult installFromArchive(const Package& package, const std::filesystem::path& archive, const std::string& expectedHash, const std::filesystem::path& target, const ProgressFn& progress);
InstallResult downloadAndInstall(const Package& package, const std::filesystem::path& target, const ProgressFn& progress);

Capabilities checkCapabilities();
std::string describeStage(Stage stage);

class Installer {
public:
    Installer();
    ~Installer();
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    bool start(const Package& package, const std::filesystem::path& target);
    void cancel();
    bool running() const;
    Status status() const;
    void join();

private:
    struct State;
    std::shared_ptr<State> state_;
    std::thread thread_;
};

}

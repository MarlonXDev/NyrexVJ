#include "media/ffmpeg_setup.hpp"

#include "core/archive.hpp"
#include "core/log.hpp"
#include "core/sha256.hpp"
#include "media/ffmpeg_api.hpp"
#include "media/hap.hpp"
#include "media/video_decoder.hpp"
#include "platform/platform.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace nvj::ffmpeg_setup {

namespace {

namespace fs = std::filesystem;

constexpr const char* kReleaseBase = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/";

struct ManifestEntry {
    int avcodec;
    const char* version;
    std::vector<LibraryFile> libraries;
};

const std::vector<ManifestEntry>& manifest() {
    static const std::vector<ManifestEntry> entries = {
        {62, "8.1", {{"avutil", 60}, {"swscale", 9}, {"swresample", 6}, {"avcodec", 62}, {"avformat", 62}}},
        {63, "9.0", {{"avutil", 61}, {"swscale", 10}, {"swresample", 7}, {"avcodec", 63}, {"avformat", 63}}},
    };
    return entries;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isHex64(const std::string& text) {
    if (text.size() != 64) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool isWindowsPlatform(const std::string& platform) {
    return platform.rfind("win", 0) == 0;
}

void removeQuiet(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

InstallResult failure(const std::string& message) {
    InstallResult result;
    result.error = message;
    return result;
}

InstallResult cancelledResult() {
    InstallResult result;
    result.cancelled = true;
    result.error = "Cancelled";
    return result;
}

bool report(const ProgressFn& progress, Stage stage, unsigned long long done, unsigned long long total) {
    return !progress || progress(stage, done, total);
}

std::string readSmallFile(const fs::path& path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::string text(limit, '\0');
    in.read(text.data(), static_cast<std::streamsize>(limit));
    text.resize(static_cast<size_t>(in.gcount()));
    return text;
}

}

std::string platformKey() {
#if defined(_WIN32)
#if defined(_M_ARM64)
    return "winarm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "win64";
#else
    return std::string();
#endif
#elif defined(__linux__)
#if defined(__aarch64__)
    return "linuxarm64";
#elif defined(__x86_64__)
    return "linux64";
#else
    return std::string();
#endif
#else
    return std::string();
#endif
}

int compiledAvcodecMajor() {
#ifdef NYREXVJ_HAVE_FFMPEG
    return LIBAVCODEC_VERSION_MAJOR;
#else
    return 0;
#endif
}

bool choosePackage(int avcodecMajor, const std::string& platform, Package& package, std::string& reason) {
    package = Package{};
    if (avcodecMajor <= 0) {
        reason = "This build of NyrexVJ was made without video support";
        return false;
    }
    if (platform != "win64" && platform != "winarm64" && platform != "linux64" && platform != "linuxarm64") {
        reason = "There is no ready-made FFmpeg build for this system";
        return false;
    }
    for (const ManifestEntry& entry : manifest()) {
        if (entry.avcodec != avcodecMajor) {
            continue;
        }
        package.version = entry.version;
        package.platform = platform;
        package.zip = isWindowsPlatform(platform);
        package.avcodecMajor = entry.avcodec;
        package.libraries = entry.libraries;
        package.assetName = "ffmpeg-n" + package.version + "-latest-" + platform + "-lgpl-shared-" + package.version + (package.zip ? ".zip" : ".tar.xz");
        package.url = std::string(kReleaseBase) + package.assetName;
        package.checksumUrl = std::string(kReleaseBase) + "checksums.sha256";
        return true;
    }
    reason = "No ready-made FFmpeg build matches this version of NyrexVJ";
    return false;
}

bool trustedUrl(const std::string& url) {
    const std::string prefix = kReleaseBase;
    if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string rest = url.substr(prefix.size());
    if (rest.find('/') != std::string::npos || rest.find('?') != std::string::npos || rest.find('#') != std::string::npos || rest.find('%') != std::string::npos || rest.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

bool parseChecksums(const std::string& text, const std::string& asset, std::string& hash) {
    std::istringstream stream(text);
    std::string line;
    bool found = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < 66) {
            continue;
        }
        const std::string candidate = line.substr(0, 64);
        if (!isHex64(candidate)) {
            continue;
        }
        size_t nameStart = 64;
        while (nameStart < line.size() && (line[nameStart] == ' ' || line[nameStart] == '*')) {
            ++nameStart;
        }
        if (nameStart == 64) {
            continue;
        }
        if (line.substr(nameStart) == asset) {
            if (found && lower(candidate) != hash) {
                hash.clear();
                return false;
            }
            hash = lower(candidate);
            found = true;
        }
    }
    return found;
}

std::string libraryFileName(const LibraryFile& library, const std::string& platform) {
    if (isWindowsPlatform(platform)) {
        return library.base + "-" + std::to_string(library.major) + ".dll";
    }
    return "lib" + library.base + ".so." + std::to_string(library.major);
}

std::vector<std::string> requiredFiles(const Package& package) {
    std::vector<std::string> files;
    for (const LibraryFile& library : package.libraries) {
        files.push_back(libraryFileName(library, package.platform));
    }
    files.push_back("LICENSE.txt");
    return files;
}

std::string archiveMap(const Package& package, const std::string& entryName) {
    const size_t slash = entryName.find('/');
    if (slash == std::string::npos) {
        return std::string();
    }
    const std::string rest = entryName.substr(slash + 1);
    if (rest == "LICENSE.txt") {
        return rest;
    }
    if (package.zip) {
        const std::string prefix = "bin/";
        if (rest.size() <= prefix.size() + 4 || rest.compare(0, prefix.size(), prefix) != 0) {
            return std::string();
        }
        const std::string file = rest.substr(prefix.size());
        if (file.find('/') != std::string::npos || file.size() < 5 || lower(file.substr(file.size() - 4)) != ".dll") {
            return std::string();
        }
        return file;
    }
    const std::string prefix = "lib/";
    if (rest.size() <= prefix.size() || rest.compare(0, prefix.size(), prefix) != 0) {
        return std::string();
    }
    const std::string file = rest.substr(prefix.size());
    if (file.find('/') != std::string::npos || file.rfind("lib", 0) != 0 || file.find(".so") == std::string::npos) {
        return std::string();
    }
    return file;
}

LicenseReport inspectLibraryLicense(const fs::path& file) {
    LicenseReport report;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return report;
    }
    static const char* const kGpl = "--enable-gpl";
    static const char* const kNonFree = "--enable-nonfree";
    static const char* const kPrefix = "--prefix=";
    static const char* const kVersion3 = "--enable-version3";
    constexpr size_t kCarry = 32;
    std::string carry;
    std::vector<char> buffer(1024 * 1024);
    bool gpl = false;
    bool nonFree = false;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const size_t got = static_cast<size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        std::string window = carry;
        window.append(buffer.data(), got);
        if (window.find(kGpl) != std::string::npos) {
            gpl = true;
        }
        if (window.find(kNonFree) != std::string::npos) {
            nonFree = true;
        }
        if (window.find(kPrefix) != std::string::npos) {
            report.configurationFound = true;
        }
        if (window.find(kVersion3) != std::string::npos) {
            report.version3 = true;
        }
        carry = window.size() > kCarry ? window.substr(window.size() - kCarry) : window;
    }
    if (nonFree) {
        report.verdict = License::NonFree;
    } else if (gpl) {
        report.verdict = License::Gpl;
    } else if (report.configurationFound) {
        report.verdict = License::Ok;
    } else {
        report.verdict = License::Unknown;
    }
    return report;
}

bool licenseTextIsLgpl(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    int checked = 0;
    while (std::getline(stream, line) && checked < 12) {
        std::string trimmed = line;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char c) { return !std::isspace(c); }));
        if (trimmed.empty()) {
            continue;
        }
        ++checked;
        const std::string upper = [&] {
            std::string value = trimmed;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return value;
        }();
        if (upper.find("GNU LESSER GENERAL PUBLIC LICENSE") != std::string::npos) {
            return true;
        }
        if (upper.find("GNU GENERAL PUBLIC LICENSE") != std::string::npos) {
            return false;
        }
    }
    return false;
}

std::string metadataFileName() {
    return "NYREXVJ_FFMPEG.txt";
}

InstallResult installFromArchive(const Package& package, const fs::path& archive, const std::string& expectedHash, const fs::path& target, const ProgressFn& progress) {
    if (!isHex64(expectedHash)) {
        return failure("The expected checksum is not valid");
    }
    if (target.empty() || target.filename().empty()) {
        return failure("The target folder is not valid");
    }
    std::error_code ec;
    const fs::path parent = target.parent_path();
    fs::create_directories(parent, ec);
    if (ec) {
        return failure("The target folder cannot be created");
    }
    const fs::path staging = parent / (target.filename().string() + ".staging");
    const fs::path previous = parent / (target.filename().string() + ".previous");
    removeQuiet(staging);
    fs::create_directories(staging / "files", ec);
    if (ec) {
        return failure("The temporary folder cannot be created");
    }
    auto abort = [&](const InstallResult& result) {
        removeQuiet(staging);
        return result;
    };

    if (!report(progress, Stage::Verify, 0, 1)) {
        return abort(cancelledResult());
    }
    std::string actual;
    bool cancelled = false;
    std::error_code sizeEc;
    const unsigned long long archiveSize = static_cast<unsigned long long>(fs::file_size(archive, sizeEc));
    if (sizeEc || archiveSize == 0 || archiveSize > kMaxArchiveBytes) {
        return abort(failure("The download has an unexpected size"));
    }
    const bool hashed = sha256FileHex(archive, actual, [&](unsigned long long done) {
        if (!report(progress, Stage::Verify, done, archiveSize)) {
            cancelled = true;
            return false;
        }
        return true;
    });
    if (cancelled) {
        return abort(cancelledResult());
    }
    if (!hashed) {
        return abort(failure("The download cannot be read"));
    }
    if (lower(actual) != lower(expectedHash)) {
        return abort(failure("The checksum of the download does not match the published one"));
    }

    archive::Options options;
    options.map = [&package](const std::string& name, bool) { return archiveMap(package, name); };
    options.maxTotalBytes = 900ull * 1024ull * 1024ull;
    options.maxEntryBytes = 300ull * 1024ull * 1024ull;
    options.maxEntries = 5000;
    options.progress = [&](unsigned long long done, unsigned long long total) { return report(progress, Stage::Extract, done, total); };
    archive::Result extracted = package.zip ? archive::extractZip(archive, staging / "files", options) : archive::extractTarXz(archive, staging / "files", options);
    if (extracted.cancelled) {
        return abort(cancelledResult());
    }
    if (!extracted.ok) {
        return abort(failure(extracted.error.empty() ? "The download cannot be unpacked" : extracted.error));
    }

    if (!report(progress, Stage::Install, 0, 1)) {
        return abort(cancelledResult());
    }
    const fs::path files = staging / "files";
    for (const std::string& name : requiredFiles(package)) {
        std::error_code existsEc;
        const bool present = fs::is_regular_file(files / name, existsEc) && !existsEc;
        if (!present) {
            return abort(failure("The download is missing this file: " + name));
        }
    }
    if (!licenseTextIsLgpl(readSmallFile(files / "LICENSE.txt", 8192))) {
        return abort(failure("The download is not an LGPL build of FFmpeg and was not installed"));
    }
    LibraryFile avutil;
    for (const LibraryFile& library : package.libraries) {
        if (library.base == "avutil") {
            avutil = library;
        }
    }
    const LicenseReport license = inspectLibraryLicense(files / libraryFileName(avutil, package.platform));
    if (license.verdict == License::Gpl) {
        return abort(failure("The download is a GPL build of FFmpeg and was not installed"));
    }
    if (license.verdict == License::NonFree) {
        return abort(failure("The download is a non-free build of FFmpeg and was not installed"));
    }
    if (license.verdict != License::Ok) {
        return abort(failure("The licence of the download could not be confirmed and it was not installed"));
    }

    {
        std::ofstream meta(files / metadataFileName(), std::ios::binary | std::ios::trunc);
        meta << "NyrexVJ FFmpeg setup\n";
        meta << "version=" << package.version << "\n";
        meta << "platform=" << package.platform << "\n";
        meta << "asset=" << package.assetName << "\n";
        meta << "url=" << package.url << "\n";
        meta << "sha256=" << lower(actual) << "\n";
        meta << "license=LGPL" << (license.version3 ? " v3" : "") << " (configuration checked)\n";
        meta << "installed=" << static_cast<long long>(std::time(nullptr)) << "\n";
    }

    if (!report(progress, Stage::Install, 1, 1)) {
        return abort(cancelledResult());
    }
    bool hadTarget = false;
    {
        std::error_code existsEc;
        hadTarget = fs::exists(target, existsEc);
    }
    if (hadTarget) {
        removeQuiet(previous);
        std::error_code renameEc;
        fs::rename(target, previous, renameEc);
        if (renameEc) {
            return abort(failure("The existing FFmpeg folder is in use and cannot be replaced. Restart NyrexVJ and try again"));
        }
    }
    std::error_code moveEc;
    fs::rename(files, target, moveEc);
    if (moveEc) {
        if (hadTarget) {
            std::error_code restoreEc;
            fs::rename(previous, target, restoreEc);
        }
        return abort(failure("The new FFmpeg folder cannot be put in place"));
    }
    removeQuiet(staging);

    InstallResult result;
    result.ok = true;
    result.hash = lower(actual);
    result.license = std::string("LGPL") + (license.version3 ? " v3" : "");
    result.version = package.version;
    return result;
}

InstallResult downloadAndInstall(const Package& package, const fs::path& target, const ProgressFn& progress) {
    if (!trustedUrl(package.url) || !trustedUrl(package.checksumUrl)) {
        return failure("The download address is not trusted");
    }
    if (target.empty() || target.filename().empty()) {
        return failure("The target folder is not valid");
    }
    std::error_code ec;
    const fs::path parent = target.parent_path();
    fs::create_directories(parent, ec);
    if (ec) {
        return failure("The target folder cannot be created");
    }
    const fs::space_info space = fs::space(parent, ec);
    if (!ec && space.available < kRequiredFreeBytes) {
        return failure("There is not enough free disk space (about 700 MB are needed)");
    }
    const fs::path work = parent / (target.filename().string() + ".download");
    removeQuiet(work);
    fs::create_directories(work, ec);
    if (ec) {
        return failure("The temporary folder cannot be created");
    }
    auto finish = [&](const InstallResult& result) {
        removeQuiet(work);
        return result;
    };

    if (!report(progress, Stage::Checksums, 0, 1)) {
        return finish(cancelledResult());
    }
    platform::HttpResult sums;
    if (!platform::httpGet(package.checksumUrl, sums, nullptr)) {
        return finish(failure("The checksum list could not be loaded: " + sums.error));
    }
    if (sums.body.size() > 256 * 1024) {
        return finish(failure("The checksum list is not valid"));
    }
    std::string hash;
    if (!parseChecksums(std::string(sums.body.begin(), sums.body.end()), package.assetName, hash)) {
        return finish(failure("The checksum list does not contain this FFmpeg build"));
    }
    if (!report(progress, Stage::Checksums, 1, 1)) {
        return finish(cancelledResult());
    }

    const fs::path part = work / (package.assetName + ".part");
    const fs::path archive = work / package.assetName;
    platform::DownloadResult download;
    const bool downloaded = platform::httpDownload(package.url, pathToUtf8(part), kMaxArchiveBytes, [&](unsigned long long done, unsigned long long total) { return report(progress, Stage::Download, done, total); }, download);
    if (!downloaded) {
        if (download.cancelled) {
            return finish(cancelledResult());
        }
        return finish(failure("The download failed: " + download.error));
    }
    fs::rename(part, archive, ec);
    if (ec) {
        return finish(failure("The download cannot be saved"));
    }
    InstallResult result = installFromArchive(package, archive, hash, target, progress);
    return finish(result);
}


std::string describeStage(Stage stage) {
    switch (stage) {
    case Stage::Checksums:
        return "Checking the checksum list";
    case Stage::Download:
        return "Downloading FFmpeg";
    case Stage::Verify:
        return "Verifying the download";
    case Stage::Extract:
        return "Unpacking";
    case Stage::Install:
        return "Installing";
    case Stage::Reload:
        return "Loading FFmpeg";
    case Stage::Done:
        return "FFmpeg is ready";
    case Stage::Failed:
        return "The setup failed";
    case Stage::Cancelled:
        return "The setup was cancelled";
    case Stage::Idle:
        break;
    }
    return "Waiting";
}

#ifdef NYREXVJ_HAVE_FFMPEG

namespace {

bool probeHapEncode(const FfmpegApi& api, const char* format, bool snappy, CompressedFormat expected) {
    const AVCodec* codec = api.avcodec_find_encoder_by_name("hap");
    if (codec == nullptr) {
        return false;
    }
    AVCodecContext* context = api.avcodec_alloc_context3(codec);
    if (context == nullptr) {
        return false;
    }
    context->width = 64;
    context->height = 64;
    context->pix_fmt = AV_PIX_FMT_RGBA;
    context->time_base = AVRational{1, 25};
    context->framerate = AVRational{25, 1};
    bool ok = true;
    if (context->priv_data != nullptr) {
        ok = api.av_opt_set(context->priv_data, "format", format, 0) >= 0 && api.av_opt_set(context->priv_data, "compressor", snappy ? "snappy" : "none", 0) >= 0;
    }
    if (ok) {
        ok = api.avcodec_open2(context, codec, nullptr) >= 0;
    }
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    if (ok) {
        frame = api.av_frame_alloc();
        packet = api.av_packet_alloc();
        ok = frame != nullptr && packet != nullptr;
    }
    if (ok) {
        frame->format = AV_PIX_FMT_RGBA;
        frame->width = 64;
        frame->height = 64;
        ok = api.av_frame_get_buffer(frame, 0) >= 0;
    }
    if (ok) {
        for (int y = 0; y < 64; ++y) {
            uint8_t* row = frame->data[0] + static_cast<size_t>(y) * static_cast<size_t>(frame->linesize[0]);
            for (int x = 0; x < 64; ++x) {
                row[x * 4 + 0] = static_cast<uint8_t>(x * 4);
                row[x * 4 + 1] = static_cast<uint8_t>(y * 4);
                row[x * 4 + 2] = static_cast<uint8_t>((x + y) * 2);
                row[x * 4 + 3] = static_cast<uint8_t>(x < 32 ? 255 : 100);
            }
        }
        frame->pts = 0;
        ok = api.avcodec_send_frame(context, frame) >= 0 && api.avcodec_send_frame(context, nullptr) >= 0;
    }
    if (ok) {
        ok = api.avcodec_receive_packet(context, packet) >= 0;
    }
    if (ok) {
        HapFrameInfo info;
        ok = hapInspect(packet->data, static_cast<size_t>(packet->size), info) == HapResult::Ok && info.format == expected;
    }
    if (packet != nullptr) {
        api.av_packet_free(&packet);
    }
    if (frame != nullptr) {
        api.av_frame_free(&frame);
    }
    api.avcodec_free_context(&context);
    return ok;
}

}

Capabilities checkCapabilities() {
    Capabilities caps;
    caps.compiled = true;
    caps.folder = ffmpegDirectory();
    const VideoSupport support = videoSupport();
    caps.loaded = support.loaded;
    caps.loadedFrom = support.loadedFrom;
    caps.problem = support.problem;
    if (!caps.loaded) {
        caps.missing.push_back("FFmpeg libraries");
        return caps;
    }
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        caps.loaded = false;
        caps.problem = problem;
        caps.missing.push_back("FFmpeg libraries");
        return caps;
    }
    const char* version = api->av_version_info();
    caps.version = version != nullptr ? version : "";
    const char* license = api->avcodec_license();
    caps.license = license != nullptr ? license : "";
    caps.dxvDecoder = api->avcodec_find_decoder(AV_CODEC_ID_DXV) != nullptr;
    if (!caps.dxvDecoder) {
        caps.missing.push_back("DXV decoder");
    }
    caps.hapEncoder = api->avcodec_find_encoder_by_name("hap") != nullptr;
    if (!caps.hapEncoder) {
        caps.missing.push_back("HAP encoder");
    }
    AVFormatContext* output = nullptr;
    if (api->avformat_alloc_output_context2(&output, nullptr, "mov", nullptr) >= 0 && output != nullptr) {
        caps.movMuxer = true;
        api->avformat_free_context(output);
    } else {
        caps.missing.push_back("QuickTime (MOV) writer");
    }
    if (caps.hapEncoder) {
        const bool plain = probeHapEncode(*api, "hap", false, CompressedFormat::Dxt1) && probeHapEncode(*api, "hap_alpha", false, CompressedFormat::Dxt5) && probeHapEncode(*api, "hap_q", false, CompressedFormat::YcoCgDxt5);
        caps.hapEncoderWorks = plain;
        caps.snappy = plain && probeHapEncode(*api, "hap", true, CompressedFormat::Dxt1);
        if (!plain) {
            caps.missing.push_back("Working HAP encoder");
        }
    }
    return caps;
}

#else

Capabilities checkCapabilities() {
    Capabilities caps;
    caps.compiled = false;
    caps.folder = ffmpegDirectory();
    caps.problem = "This build of NyrexVJ was made without video support";
    caps.missing.push_back("FFmpeg libraries");
    return caps;
}

#endif

struct Installer::State {
    mutable std::mutex mutex;
    Status status;
    std::atomic<bool> cancel{false};
};

Installer::Installer() : state_(std::make_shared<State>()) {}

Installer::~Installer() {
    cancel();
    join();
}

bool Installer::start(const Package& package, const fs::path& target) {
    if (running()) {
        return false;
    }
    join();
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->status = Status{};
        state_->status.running = true;
        state_->status.stage = Stage::Checksums;
    }
    state_->cancel = false;
    std::shared_ptr<State> state = state_;
    thread_ = std::thread([state, package, target]() {
        platform::setCurrentThreadBackground(true);
        auto weightOf = [](Stage stage, unsigned long long done, unsigned long long total) {
            const double fraction = total > 0 ? std::min(1.0, static_cast<double>(done) / static_cast<double>(total)) : 0.0;
            switch (stage) {
            case Stage::Checksums:
                return 0.02 * fraction;
            case Stage::Download:
                return 0.02 + 0.78 * fraction;
            case Stage::Verify:
                return 0.80 + 0.05 * fraction;
            case Stage::Extract:
                return 0.85 + 0.12 * fraction;
            case Stage::Install:
                return 0.97 + 0.02 * fraction;
            case Stage::Reload:
                return 0.99;
            default:
                return 1.0;
            }
        };
        const ProgressFn progress = [&](Stage stage, unsigned long long done, unsigned long long total) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->status.stage = stage;
                state->status.done = done;
                state->status.total = total;
                state->status.progress = weightOf(stage, done, total);
            }
            return !state->cancel.load();
        };
        InstallResult result = downloadAndInstall(package, target, progress);
        if (result.ok) {
            progress(Stage::Reload, 0, 0);
            retryVideoSupport();
            const VideoSupport support = videoSupport();
            if (!support.loaded) {
                result.ok = false;
                result.error = "FFmpeg was installed but could not be loaded: " + support.problem;
            }
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        state->status.running = false;
        state->status.ok = result.ok;
        state->status.cancelled = result.cancelled;
        state->status.error = result.ok ? std::string() : result.error;
        state->status.version = result.version;
        state->status.license = result.license;
        state->status.stage = result.ok ? Stage::Done : (result.cancelled ? Stage::Cancelled : Stage::Failed);
        state->status.progress = result.ok ? 1.0 : state->status.progress;
        if (result.ok) {
            logLine("FFmpeg " + result.version + " (" + result.license + ") was installed from " + package.url);
        } else {
            logLine("FFmpeg setup ended: " + result.error);
        }
    });
    return true;
}

void Installer::cancel() {
    state_->cancel = true;
}

bool Installer::running() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->status.running;
}

Status Installer::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->status;
}

void Installer::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

}

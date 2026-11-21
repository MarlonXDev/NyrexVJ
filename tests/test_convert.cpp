#include "check.hpp"

#include "core/archive.hpp"
#include "core/sha256.hpp"
#include "media/convert_cache.hpp"
#include "media/convert_manager.hpp"
#include "media/hap_convert.hpp"
#include "media/video_redirect.hpp"
#include "media/ffmpeg_setup.hpp"
#include "media/video_decoder.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

const fs::path kData = NYREXVJ_TEST_DATA;

std::vector<unsigned char> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string stripFirst(const std::string& name) {
    const size_t slash = name.find('/');
    return slash == std::string::npos ? std::string() : name.substr(slash + 1);
}

std::string zipMap(const std::string& name, bool) {
    const std::string rest = stripFirst(name);
    if (rest.rfind("bin/", 0) == 0 && rest.size() > 4) {
        return rest.substr(4);
    }
    if (rest == "LICENSE.txt") {
        return rest;
    }
    return std::string();
}

std::string tarMap(const std::string& name, bool) {
    const std::string rest = stripFirst(name);
    if (rest.rfind("lib/", 0) == 0 && rest.size() > 4) {
        return rest.substr(4);
    }
    if (rest == "LICENSE.txt") {
        return rest;
    }
    return std::string();
}

void testSafePaths() {
    CHECK(nvj::archive::safeRelativePath("a.dll"));
    CHECK(nvj::archive::safeRelativePath("bin/a.dll"));
    CHECK(!nvj::archive::safeRelativePath(""));
    CHECK(!nvj::archive::safeRelativePath("/abs.dll"));
    CHECK(!nvj::archive::safeRelativePath("\\abs.dll"));
    CHECK(!nvj::archive::safeRelativePath("../a.dll"));
    CHECK(!nvj::archive::safeRelativePath("bin/../../a.dll"));
    CHECK(!nvj::archive::safeRelativePath("bin//a.dll"));
    CHECK(!nvj::archive::safeRelativePath("bin/./a.dll"));
    CHECK(!nvj::archive::safeRelativePath("C:/a.dll"));
    CHECK(!nvj::archive::safeRelativePath("bin\\a.dll"));
    CHECK(!nvj::archive::safeRelativePath(std::string("a\0b", 3)));
    CHECK(!nvj::archive::safeRelativePath("bin/"));
    CHECK(nvj::archive::safeLinkTarget("libx.so.1"));
    CHECK(!nvj::archive::safeLinkTarget("../x"));
    CHECK(!nvj::archive::safeLinkTarget("a/b"));
    CHECK(!nvj::archive::safeLinkTarget(".."));
    CHECK(!nvj::archive::safeLinkTarget(""));
}

void testCrcAndInflate() {
    const std::string text = "123456789";
    const unsigned int crc = nvj::archive::crc32Update(0, reinterpret_cast<const unsigned char*>(text.data()), text.size());
    CHECK(crc == 0xCBF43926u);
    const unsigned char stored[] = {0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c'};
    std::vector<unsigned char> out;
    std::string error;
    CHECK(nvj::archive::inflateRaw(stored, sizeof(stored), out, 100, error));
    CHECK(out.size() == 3 && out[0] == 'a' && out[2] == 'c');
    const unsigned char broken[] = {0x01, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c'};
    CHECK(!nvj::archive::inflateRaw(broken, sizeof(broken), out, 100, error));
    CHECK(!error.empty());
    const unsigned char truncated[] = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'a'};
    CHECK(!nvj::archive::inflateRaw(truncated, sizeof(truncated), out, 100, error));
    CHECK(!nvj::archive::inflateRaw(stored, sizeof(stored), out, 2, error));
    const unsigned char fixedBlock[] = {0x4B, 0x4C, 0x4A, 0x06, 0x00};
    CHECK(nvj::archive::inflateRaw(fixedBlock, sizeof(fixedBlock), out, 100, error));
    CHECK(out.size() == 3 && out[0] == 'a' && out[1] == 'b' && out[2] == 'c');
}

void testZip(const fs::path& scratch) {
    const fs::path target = scratch / "zip_ok";
    nvj::archive::Options options;
    options.map = zipMap;
    const nvj::archive::Result result = nvj::archive::extractZip(kData / "archive_sample.zip", target, options);
    CHECK(result.ok);
    CHECK(result.files.size() == 3);
    CHECK(fs::exists(target / "a.dll") && fs::exists(target / "b.dll") && fs::exists(target / "LICENSE.txt"));
    CHECK(!fs::exists(target / "ignored.txt") && !fs::exists(target / "doc"));
    CHECK(nvj::sha256Hex(readBytes(target / "a.dll")) == "602a4f619185acf28e84722c4dfc8dca12518ce3922677dcaab751bacbe8b485");
    CHECK(nvj::sha256Hex(readBytes(target / "b.dll")) == "174262e8b83994ba5e3e9646d9b92d3cdbba5f13aee712a724a77a8fb64e5039");
    CHECK(nvj::sha256Hex(readBytes(target / "LICENSE.txt")) == "a3997468ebcfeb726b7f92fa8f7f7e0b398c62543a0b35281190a5e334dddb34");
    CHECK(result.bytes == 66287 + 1500 + 44);

    const fs::path escape = scratch / "zip_escape";
    nvj::archive::Options everything;
    const nvj::archive::Result bad = nvj::archive::extractZip(kData / "archive_traversal.zip", escape, everything);
    CHECK(!bad.ok && !bad.error.empty());
    CHECK(!fs::exists(scratch / "evil.dll") && !fs::exists(escape.parent_path() / "evil.dll") && !fs::exists("/abs.dll"));

    nvj::archive::Options limited;
    limited.map = zipMap;
    limited.maxTotalBytes = 1000;
    CHECK(!nvj::archive::extractZip(kData / "archive_sample.zip", scratch / "zip_limit", limited).ok);
    nvj::archive::Options entryLimited;
    entryLimited.map = zipMap;
    entryLimited.maxEntryBytes = 2000;
    CHECK(!nvj::archive::extractZip(kData / "archive_sample.zip", scratch / "zip_limit2", entryLimited).ok);

    nvj::archive::Options cancel;
    cancel.map = zipMap;
    cancel.progress = [](unsigned long long, unsigned long long) { return false; };
    const nvj::archive::Result cancelled = nvj::archive::extractZip(kData / "archive_sample.zip", scratch / "zip_cancel", cancel);
    CHECK(!cancelled.ok && cancelled.cancelled);

    std::vector<unsigned char> damaged = readBytes(kData / "archive_sample.zip");
    damaged[60] ^= 0x55;
    const fs::path damagedFile = scratch / "damaged.zip";
    std::ofstream(damagedFile, std::ios::binary).write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
    CHECK(!nvj::archive::extractZip(damagedFile, scratch / "zip_damaged", options).ok);
    const fs::path junk = scratch / "junk.zip";
    std::ofstream(junk, std::ios::binary) << "this is not a zip file at all, only some text";
    CHECK(!nvj::archive::extractZip(junk, scratch / "zip_junk", options).ok);
    CHECK(!nvj::archive::extractZip(scratch / "missing.zip", scratch / "zip_missing", options).ok);
}

void checkTarResult(const fs::path& target, const nvj::archive::Result& result) {
    CHECK(result.ok);
    CHECK(fs::exists(target / "libx.so.1.2.3") && fs::exists(target / "LICENSE.txt"));
    CHECK(fs::is_symlink(target / "libx.so.1") && fs::is_symlink(target / "libx.so"));
    CHECK(fs::read_symlink(target / "libx.so.1").string() == "libx.so.1.2.3");
    CHECK(nvj::sha256Hex(readBytes(target / "libx.so")) == "194f6932a8009f6f9a90781457fffef32e3a419590d59c8b4ff8d935aea9ae42");
    CHECK(nvj::sha256Hex(readBytes(target / "libx.so.1.2.3")) == "194f6932a8009f6f9a90781457fffef32e3a419590d59c8b4ff8d935aea9ae42");
    const std::string longName = std::string(130, 'n') + ".so.9";
    CHECK(fs::exists(target / longName));
    CHECK(nvj::sha256Hex(readBytes(target / longName)) == "8a51c63189b07318dccded24bd88dccd32c3c2cf56b1aaa40e4f459a443de118");
    CHECK(!fs::exists(target / "ignored.1"));
}

void testTar(const fs::path& scratch) {
    nvj::archive::Options options;
    options.map = tarMap;
    const fs::path plain = scratch / "tar_plain";
    checkTarResult(plain, nvj::archive::extractTar(kData / "archive_sample.tar", plain, options));
    if (nvj::archive::xzAvailable()) {
        const fs::path xz = scratch / "tar_xz";
        checkTarResult(xz, nvj::archive::extractTarXz(kData / "archive_sample.tar.xz", xz, options));
        const nvj::archive::Result unsafe = nvj::archive::extractTarXz(kData / "archive_unsafe_link.tar.xz", scratch / "tar_unsafe", options);
        CHECK(!unsafe.ok && !unsafe.error.empty());
        nvj::archive::Options cancel;
        cancel.map = tarMap;
        cancel.progress = [](unsigned long long, unsigned long long) { return false; };
        const nvj::archive::Result cancelled = nvj::archive::extractTarXz(kData / "archive_sample.tar.xz", scratch / "tar_cancel", cancel);
        CHECK(!cancelled.ok && cancelled.cancelled);
        std::vector<unsigned char> damaged = readBytes(kData / "archive_sample.tar.xz");
        damaged[damaged.size() / 2] ^= 0xFF;
        const fs::path damagedFile = scratch / "damaged.tar.xz";
        std::ofstream(damagedFile, std::ios::binary).write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
        CHECK(!nvj::archive::extractTarXz(damagedFile, scratch / "tar_damaged", options).ok);
        std::vector<unsigned char> cut = readBytes(kData / "archive_sample.tar.xz");
        cut.resize(cut.size() / 2);
        const fs::path cutFile = scratch / "cut.tar.xz";
        std::ofstream(cutFile, std::ios::binary).write(reinterpret_cast<const char*>(cut.data()), static_cast<std::streamsize>(cut.size()));
        CHECK(!nvj::archive::extractTarXz(cutFile, scratch / "tar_cut", options).ok);
    }
    std::vector<unsigned char> truncated = readBytes(kData / "archive_sample.tar");
    truncated.resize(1000);
    const fs::path truncatedFile = scratch / "truncated.tar";
    std::ofstream(truncatedFile, std::ios::binary).write(reinterpret_cast<const char*>(truncated.data()), static_cast<std::streamsize>(truncated.size()));
    CHECK(!nvj::archive::extractTar(truncatedFile, scratch / "tar_truncated", options).ok);
}

std::string readText(const fs::path& path) {
    const std::vector<unsigned char> bytes = readBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

nvj::ffmpeg_setup::Package packageFor(const std::string& platform) {
    nvj::ffmpeg_setup::Package package;
    std::string reason;
    CHECK(nvj::ffmpeg_setup::choosePackage(62, platform, package, reason));
    return package;
}

void testPackages() {
    using namespace nvj::ffmpeg_setup;
    Package package;
    std::string reason;
    CHECK(choosePackage(62, "win64", package, reason));
    CHECK(package.version == "8.1" && package.zip && package.avcodecMajor == 62);
    CHECK(package.assetName == "ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip");
    CHECK(package.url == "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip");
    CHECK(package.checksumUrl == "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/checksums.sha256");
    CHECK(trustedUrl(package.url) && trustedUrl(package.checksumUrl));
    const std::vector<std::string> windowsFiles = requiredFiles(package);
    CHECK(windowsFiles.size() == 6);
    CHECK(std::find(windowsFiles.begin(), windowsFiles.end(), "avcodec-62.dll") != windowsFiles.end());
    CHECK(std::find(windowsFiles.begin(), windowsFiles.end(), "avutil-60.dll") != windowsFiles.end());
    CHECK(std::find(windowsFiles.begin(), windowsFiles.end(), "swresample-6.dll") != windowsFiles.end());
    CHECK(choosePackage(62, "linux64", package, reason));
    CHECK(!package.zip && package.assetName == "ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz");
    const std::vector<std::string> linuxFiles = requiredFiles(package);
    CHECK(std::find(linuxFiles.begin(), linuxFiles.end(), "libavcodec.so.62") != linuxFiles.end());
    CHECK(std::find(linuxFiles.begin(), linuxFiles.end(), "libswscale.so.9") != linuxFiles.end());
    CHECK(choosePackage(62, "linuxarm64", package, reason) && package.assetName.find("linuxarm64") != std::string::npos);
    CHECK(choosePackage(62, "winarm64", package, reason) && package.zip);
    CHECK(choosePackage(63, "win64", package, reason) && package.version == "9.0" && package.assetName == "ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip");
    const std::vector<std::string> nineFiles = requiredFiles(package);
    CHECK(std::find(nineFiles.begin(), nineFiles.end(), "avutil-61.dll") != nineFiles.end());
    CHECK(std::find(nineFiles.begin(), nineFiles.end(), "swscale-10.dll") != nineFiles.end());
    CHECK(!choosePackage(60, "linux64", package, reason) && !reason.empty() && package.assetName.empty());
    CHECK(!choosePackage(61, "win64", package, reason));
    CHECK(!choosePackage(0, "win64", package, reason));
    CHECK(!choosePackage(62, "macos", package, reason));
    CHECK(!choosePackage(62, "", package, reason));
    CHECK(!trustedUrl("http://github.com/BtbN/FFmpeg-Builds/releases/download/latest/x.zip"));
    CHECK(!trustedUrl("https://evil.example.com/BtbN/FFmpeg-Builds/releases/download/latest/x.zip"));
    CHECK(!trustedUrl("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/../../evil.zip"));
    CHECK(!trustedUrl("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/a/b.zip"));
    CHECK(!trustedUrl("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"));
    CHECK(!trustedUrl("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/x.zip?x=1"));
    CHECK(!trustedUrl("https://github.com/other/FFmpeg-Builds/releases/download/latest/x.zip"));
    CHECK(!trustedUrl(""));
    const std::string key = platformKey();
    CHECK(key.empty() || key == "win64" || key == "winarm64" || key == "linux64" || key == "linuxarm64");
}

void testChecksumParsing() {
    using namespace nvj::ffmpeg_setup;
    const std::string text = readText(kData / "ffmpeg_checksums_sample.txt");
    std::string hash;
    CHECK(parseChecksums(text, "ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip", hash));
    CHECK(hash == "04dd2932ef23a9ea4a7fe0b2151c044e14c3b4490af64763d417d8c8e5953dd3");
    CHECK(parseChecksums(text, "ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz", hash));
    CHECK(hash == "9b225c677d0829b54a8e3cb10bd4895718e64e7d78042ad470808b525e08e4f9");
    CHECK(parseChecksums(text, "ffmpeg-n8.1-latest-win64-lgpl-8.1.zip", hash));
    CHECK(hash != "04dd2932ef23a9ea4a7fe0b2151c044e14c3b4490af64763d417d8c8e5953dd3");
    CHECK(!parseChecksums(text, "ffmpeg-n8.1-latest-win64-lgpl-shared-8.1", hash));
    CHECK(!parseChecksums(text, "lgpl-shared-8.1.zip", hash));
    CHECK(!parseChecksums(text, "", hash));
    const std::string good(64, 'a');
    CHECK(parseChecksums(good + "  one.zip\r\n" + good + " *two.zip\n", "one.zip", hash) && hash == good);
    CHECK(parseChecksums(good + "  one.zip\r\n" + good + " *two.zip\n", "two.zip", hash));
    CHECK(!parseChecksums(std::string(63, 'a') + "  one.zip\n", "one.zip", hash));
    CHECK(!parseChecksums(std::string(64, 'g') + "  one.zip\n", "one.zip", hash));
    CHECK(!parseChecksums(good + "one.zip\n", "one.zip", hash));
    CHECK(!parseChecksums(good + "  one.zip\n" + std::string(64, 'b') + "  one.zip\n", "one.zip", hash));
    CHECK(parseChecksums(std::string(64, 'A') + "  one.zip\n", "one.zip", hash) && hash == std::string(64, 'a'));
    CHECK(!parseChecksums("", "one.zip", hash));
}

void testArchiveMapping() {
    using namespace nvj::ffmpeg_setup;
    const Package win = packageFor("win64");
    CHECK(archiveMap(win, "ffmpeg-x/bin/avcodec-62.dll") == "avcodec-62.dll");
    CHECK(archiveMap(win, "ffmpeg-x/LICENSE.txt") == "LICENSE.txt");
    CHECK(archiveMap(win, "ffmpeg-x/bin/ffmpeg.exe").empty());
    CHECK(archiveMap(win, "ffmpeg-x/bin/sub/a.dll").empty());
    CHECK(archiveMap(win, "ffmpeg-x/include/a.h").empty());
    CHECK(archiveMap(win, "LICENSE.txt").empty());
    CHECK(archiveMap(win, "ffmpeg-x/bin/.dll").empty());
    const Package lin = packageFor("linux64");
    CHECK(archiveMap(lin, "ffmpeg-x/lib/libavcodec.so.62") == "libavcodec.so.62");
    CHECK(archiveMap(lin, "ffmpeg-x/lib/libavcodec.so.62.28.103") == "libavcodec.so.62.28.103");
    CHECK(archiveMap(lin, "ffmpeg-x/lib/pkgconfig/libavcodec.pc").empty());
    CHECK(archiveMap(lin, "ffmpeg-x/lib/readme.txt").empty());
    CHECK(archiveMap(lin, "ffmpeg-x/bin/ffmpeg").empty());
    CHECK(archiveMap(lin, "ffmpeg-x/LICENSE.txt") == "LICENSE.txt");
}

void testLicenseInspection(const fs::path& scratch) {
    using namespace nvj::ffmpeg_setup;
    const fs::path directory = scratch / "license";
    std::error_code ec;
    fs::create_directories(directory, ec);
    auto write = [&](const std::string& name, const std::string& content) {
        std::ofstream(directory / name, std::ios::binary) << content;
        return directory / name;
    };
    const std::string filler(3 * 1024 * 1024, 'x');
    const LicenseReport lgpl = inspectLibraryLicense(write("lgpl.bin", filler + "--prefix=/a --enable-version3 --enable-shared" + filler));
    CHECK(lgpl.verdict == License::Ok && lgpl.configurationFound && lgpl.version3);
    const LicenseReport gpl = inspectLibraryLicense(write("gpl.bin", filler + "--prefix=/a --enable-gpl --enable-shared" + filler));
    CHECK(gpl.verdict == License::Gpl);
    const LicenseReport nonfree = inspectLibraryLicense(write("nonfree.bin", "--prefix=/a --enable-gpl --enable-nonfree"));
    CHECK(nonfree.verdict == License::NonFree);
    const LicenseReport none = inspectLibraryLicense(write("none.bin", filler));
    CHECK(none.verdict == License::Unknown && !none.configurationFound);
    const LicenseReport missing = inspectLibraryLicense(directory / "missing.bin");
    CHECK(missing.verdict == License::Unknown);
    std::string boundary(1024 * 1024 - 5, 'x');
    const LicenseReport split = inspectLibraryLicense(write("split.bin", boundary + "--enable-gpl" + std::string(100, 'y') + "--prefix=/a"));
    CHECK(split.verdict == License::Gpl);
    CHECK(licenseTextIsLgpl("                   GNU LESSER GENERAL PUBLIC LICENSE\n   Version 3\n"));
    CHECK(licenseTextIsLgpl("\n\n  gnu lesser general public license\n"));
    CHECK(!licenseTextIsLgpl("                    GNU GENERAL PUBLIC LICENSE\n   Version 3\n"));
    CHECK(!licenseTextIsLgpl(""));
    CHECK(!licenseTextIsLgpl("Some other license text\n"));
}

void installCase(const fs::path& scratch, const std::string& name, const std::string& archiveName, const std::string& platform, bool expectOk, const std::string& errorPart) {
    using namespace nvj::ffmpeg_setup;
    const Package package = packageFor(platform);
    const fs::path archive = kData / archiveName;
    std::string hash;
    CHECK(nvj::sha256FileHex(archive, hash));
    const fs::path target = scratch / name / "ffmpeg";
    const InstallResult result = installFromArchive(package, archive, hash, target, nullptr);
    CHECK(result.ok == expectOk);
    if (expectOk) {
        for (const std::string& file : requiredFiles(package)) {
            CHECK(fs::exists(target / file));
        }
        CHECK(fs::exists(target / metadataFileName()));
        CHECK(!fs::exists(target / "ffmpeg.exe") && !fs::exists(target / "ffmpeg"));
        CHECK(readText(target / metadataFileName()).find("sha256=" + hash) != std::string::npos);
        CHECK(result.hash == hash && result.license == "LGPL v3" && result.version == "8.1");
    } else {
        CHECK(!fs::exists(target));
        CHECK(result.error.find(errorPart) != std::string::npos);
    }
    CHECK(!fs::exists(scratch / name / "ffmpeg.staging"));
}

void testInstall(const fs::path& scratch) {
    using namespace nvj::ffmpeg_setup;
    std::error_code ec;
    installCase(scratch, "inst_ok", "ffmpeg_fake_ok.zip", "win64", true, "");
    installCase(scratch, "inst_gpl", "ffmpeg_fake_gpl.zip", "win64", false, "GPL build");
    installCase(scratch, "inst_nonfree", "ffmpeg_fake_nonfree.zip", "win64", false, "non-free");
    installCase(scratch, "inst_noconfig", "ffmpeg_fake_noconfig.zip", "win64", false, "could not be confirmed");
    installCase(scratch, "inst_missing", "ffmpeg_fake_missing.zip", "win64", false, "avformat-62.dll");
    installCase(scratch, "inst_gpllicense", "ffmpeg_fake_gpllicense.zip", "win64", false, "not an LGPL build");
    if (nvj::archive::xzAvailable()) {
        installCase(scratch, "inst_tar_ok", "ffmpeg_fake_ok.tar.xz", "linux64", true, "");
        installCase(scratch, "inst_tar_gpl", "ffmpeg_fake_gpl.tar.xz", "linux64", false, "GPL build");
        const fs::path link = scratch / "inst_tar_ok" / "ffmpeg" / "libavcodec.so.62";
        CHECK(fs::is_symlink(link) && fs::read_symlink(link).string() == "libavcodec.so.62.1.100");
    }

    const Package package = packageFor("win64");
    const fs::path archive = kData / "ffmpeg_fake_ok.zip";
    std::string hash;
    CHECK(nvj::sha256FileHex(archive, hash));

    const fs::path wrongTarget = scratch / "inst_wrong" / "ffmpeg";
    const InstallResult wrong = installFromArchive(package, archive, std::string(64, '0'), wrongTarget, nullptr);
    CHECK(!wrong.ok && wrong.error.find("checksum") != std::string::npos && !fs::exists(wrongTarget));
    CHECK(!installFromArchive(package, archive, "not-a-hash", wrongTarget, nullptr).ok);
    CHECK(!installFromArchive(package, kData / "missing.zip", hash, wrongTarget, nullptr).ok);

    const fs::path replaceTarget = scratch / "inst_replace" / "ffmpeg";
    fs::create_directories(replaceTarget, ec);
    std::ofstream(replaceTarget / "old.txt") << "keep me";
    const InstallResult replaced = installFromArchive(package, archive, hash, replaceTarget, nullptr);
    CHECK(replaced.ok);
    CHECK(fs::exists(scratch / "inst_replace" / "ffmpeg.previous" / "old.txt"));
    CHECK(readText(scratch / "inst_replace" / "ffmpeg.previous" / "old.txt") == "keep me");
    CHECK(!fs::exists(replaceTarget / "old.txt") && fs::exists(replaceTarget / "avcodec-62.dll"));

    const fs::path failTarget = scratch / "inst_keep" / "ffmpeg";
    fs::create_directories(failTarget, ec);
    std::ofstream(failTarget / "old.txt") << "still here";
    std::string gplHash;
    CHECK(nvj::sha256FileHex(kData / "ffmpeg_fake_gpl.zip", gplHash));
    CHECK(!installFromArchive(package, kData / "ffmpeg_fake_gpl.zip", gplHash, failTarget, nullptr).ok);
    CHECK(readText(failTarget / "old.txt") == "still here" && !fs::exists(scratch / "inst_keep" / "ffmpeg.previous"));

    const fs::path cancelTarget = scratch / "inst_cancel" / "ffmpeg";
    const InstallResult cancelled = installFromArchive(package, archive, hash, cancelTarget, [](Stage stage, unsigned long long, unsigned long long) { return stage != Stage::Extract; });
    CHECK(!cancelled.ok && cancelled.cancelled && !fs::exists(cancelTarget) && !fs::exists(scratch / "inst_cancel" / "ffmpeg.staging"));
    const InstallResult cancelledEarly = installFromArchive(package, archive, hash, cancelTarget, [](Stage, unsigned long long, unsigned long long) { return false; });
    CHECK(!cancelledEarly.ok && cancelledEarly.cancelled && !fs::exists(cancelTarget));

    Package untrusted = package;
    untrusted.url = "http://example.com/evil.zip";
    const InstallResult refused = downloadAndInstall(untrusted, scratch / "inst_untrusted" / "ffmpeg", nullptr);
    CHECK(!refused.ok && refused.error.find("not trusted") != std::string::npos);
    Package foreignChecksums = package;
    foreignChecksums.checksumUrl = "https://example.com/checksums.sha256";
    CHECK(!downloadAndInstall(foreignChecksums, scratch / "inst_untrusted" / "ffmpeg", nullptr).ok);
    CHECK(!fs::exists(scratch / "inst_untrusted" / "ffmpeg.download"));

    Installer installer;
    CHECK(!installer.running());
    CHECK(installer.status().stage == Stage::Idle);
    CHECK(installer.start(untrusted, scratch / "inst_installer" / "ffmpeg"));
    installer.join();
    const Status status = installer.status();
    CHECK(!status.running && !status.ok && status.stage == Stage::Failed && !status.error.empty());
    CHECK(!installer.running());
    CHECK(installer.start(untrusted, scratch / "inst_installer" / "ffmpeg"));
    installer.cancel();
    installer.join();
    CHECK(!installer.running());
}

void testRealArchives(const fs::path& scratch) {
    using namespace nvj::ffmpeg_setup;
    const char* directory = std::getenv("NYREXVJ_TEST_ARCHIVES");
    if (directory == nullptr || *directory == '\0') {
        return;
    }
    const std::string sums = readText(fs::path(directory) / "sums" / "checksums.sha256");
    CHECK(!sums.empty());
    struct Case {
        std::string platform;
        std::string folder;
    };
    const std::vector<Case> cases = {{"win64", "win"}, {"linux64", "linux"}};
    for (const Case& item : cases) {
        if (item.platform == "linux64" && !nvj::archive::xzAvailable()) {
            continue;
        }
        const Package package = packageFor(item.platform);
        const fs::path archive = fs::path(directory) / item.folder / package.assetName;
        if (!fs::exists(archive)) {
            continue;
        }
        std::string expected;
        CHECK(parseChecksums(sums, package.assetName, expected));
        const fs::path target = scratch / ("real_" + item.platform) / "ffmpeg";
        const auto start = std::chrono::steady_clock::now();
        const InstallResult result = installFromArchive(package, archive, expected, target, nullptr);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("real archive %s: %s in %.1f s\n", package.assetName.c_str(), result.ok ? "installed" : result.error.c_str(), seconds);
        CHECK(result.ok);
        CHECK(result.license == "LGPL v3");
        for (const std::string& file : requiredFiles(package)) {
            CHECK(fs::exists(target / file));
        }
        const std::string metadata = readText(target / metadataFileName());
        CHECK(metadata.find("sha256=" + expected) != std::string::npos);
        CHECK(!fs::exists(target / "ffplay.exe") && !fs::exists(target / "ffplay"));
        const LicenseReport report = inspectLibraryLicense(target / libraryFileName(LibraryFile{"avutil", 60}, item.platform));
        CHECK(report.verdict == License::Ok && report.version3);
        std::string flipped = expected;
        flipped[0] = flipped[0] == 'a' ? 'b' : 'a';
        CHECK(!installFromArchive(package, archive, flipped, scratch / ("real_bad_" + item.platform) / "ffmpeg", nullptr).ok);
    }
}

void testNetworkInstall() {
    using namespace nvj::ffmpeg_setup;
    const char* enabled = std::getenv("NYREXVJ_TEST_NETWORK");
    if (enabled == nullptr || *enabled == '\0') {
        return;
    }
    Package package;
    std::string reason;
    if (!choosePackage(compiledAvcodecMajor(), platformKey(), package, reason)) {
        std::printf("network install skipped: %s\n", reason.c_str());
        return;
    }
    const fs::path target = nvj::pathFromUtf8(nvj::ffmpegDirectory());
    Stage lastStage = Stage::Idle;
    unsigned long long maxDone = 0;
    bool sawDownload = false;
    bool monotonic = true;
    unsigned long long lastDownloadDone = 0;
    const auto start = std::chrono::steady_clock::now();
    const InstallResult result = downloadAndInstall(package, target, [&](Stage stage, unsigned long long done, unsigned long long total) {
        if (stage == Stage::Download) {
            sawDownload = true;
            monotonic = monotonic && done >= lastDownloadDone;
            lastDownloadDone = done;
            maxDone = std::max(maxDone, done);
            CHECK(total == 0 || done <= total);
        }
        lastStage = stage;
        return true;
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("network install of %s: %s in %.1f s (%llu bytes)\n", package.assetName.c_str(), result.ok ? "ok" : result.error.c_str(), seconds, maxDone);
    CHECK(result.ok);
    CHECK(sawDownload && monotonic && maxDone > 10ull * 1024ull * 1024ull);
    CHECK(lastStage == Stage::Install);
    CHECK(result.license.rfind("LGPL", 0) == 0);
    for (const std::string& file : requiredFiles(package)) {
        CHECK(fs::exists(target / file));
    }
    nvj::retryVideoSupport();
    const Capabilities caps = checkCapabilities();
    std::printf("capabilities: loaded=%d dxv=%d hap=%d works=%d mov=%d snappy=%d version=%s license=%s from=%s\n", caps.loaded, caps.dxvDecoder, caps.hapEncoder, caps.hapEncoderWorks, caps.movMuxer, caps.snappy, caps.version.c_str(), caps.license.c_str(), caps.loadedFrom.c_str());
    CHECK(caps.compiled && caps.loaded);
    CHECK(caps.dxvDecoder && caps.hapEncoder && caps.hapEncoderWorks && caps.movMuxer);
    CHECK(caps.ready());
    CHECK(caps.license.rfind("LGPL", 0) == 0);
    CHECK(caps.missing.empty());
}

void testCapabilitiesWithoutLibraries() {
    using namespace nvj::ffmpeg_setup;
    const Capabilities caps = checkCapabilities();
    if (!caps.loaded) {
        CHECK(!caps.ready());
        CHECK(!caps.missing.empty());
    } else {
        CHECK(!caps.version.empty());
    }
}

}

void writeBytes(const fs::path& path, size_t size, unsigned char seed) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::vector<char> block(size);
    for (size_t i = 0; i < size; ++i) {
        block[i] = static_cast<char>((i * 31u + seed * 7u + (i >> 8)) & 0xff);
    }
    out.write(block.data(), static_cast<std::streamsize>(size));
}

std::string u8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

void poke(const fs::path& path, size_t offset) {
    std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
    io.seekg(static_cast<std::streamoff>(offset));
    char value = 0;
    io.read(&value, 1);
    value = static_cast<char>(value ^ 0x5a);
    io.seekp(static_cast<std::streamoff>(offset));
    io.write(&value, 1);
}

nvj::hapcache::Entry makeEntry(nvj::hapcache::Cache& cache, const std::string& name, unsigned long long bytes, std::int64_t lastUsed) {
    nvj::hapcache::Entry entry;
    entry.key = nvj::hapcache::keyFor("fingerprint-" + name, "tag");
    entry.source = "/videos/" + name + ".mov";
    entry.fingerprint = "fingerprint-" + name;
    entry.tag = "tag";
    entry.bytes = bytes;
    entry.created = lastUsed;
    entry.lastUsed = lastUsed;
    entry.format = "hap";
    writeBytes(cache.dataPath(entry.key), static_cast<size_t>(bytes), 3);
    CHECK(cache.commit(entry));
    return entry;
}

void testCache(const fs::path& scratch) {
    using namespace nvj::hapcache;
    const fs::path root = scratch / "cache_unit";
    std::error_code ec;
    fs::remove_all(root, ec);

    writeBytes(root / "small.bin", 1000, 1);
    fs::copy_file(root / "small.bin", root / "small_copy.bin");
    std::string a, b, error;
    CHECK(fingerprintOf(u8(root / "small.bin"), a, error) && fingerprintOf(u8(root / "small_copy.bin"), b, error));
    CHECK(a == b && a.size() == 64);
    poke(root / "small_copy.bin", 10);
    CHECK(fingerprintOf(u8(root / "small_copy.bin"), b, error) && a != b);
    writeBytes(root / "small_other.bin", 1001, 1);
    CHECK(fingerprintOf(u8(root / "small_other.bin"), b, error) && a != b);

    writeBytes(root / "big.bin", 3 * 1024 * 1024, 2);
    std::string big, changed;
    CHECK(fingerprintOf(u8(root / "big.bin"), big, error));
    poke(root / "big.bin", 1536 * 1024);
    CHECK(fingerprintOf(u8(root / "big.bin"), changed, error) && changed == big);
    poke(root / "big.bin", 0);
    CHECK(fingerprintOf(u8(root / "big.bin"), changed, error) && changed != big);
    poke(root / "big.bin", 0);
    CHECK(fingerprintOf(u8(root / "big.bin"), changed, error));
    poke(root / "big.bin", 3 * 1024 * 1024 - 1);
    std::string lastChanged;
    CHECK(fingerprintOf(u8(root / "big.bin"), lastChanged, error) && lastChanged != changed);
    CHECK(!fingerprintOf(u8(root / "missing.bin"), a, error) && !error.empty());

    const SourceStamp stamp = stampOf(u8(root / "small.bin"));
    CHECK(stamp.ok && stamp.bytes == 1000);
    fs::last_write_time(root / "small.bin", fs::file_time_type::clock::now() - std::chrono::hours(5));
    CHECK(stampOf(u8(root / "small.bin")).mtime != stamp.mtime);
    CHECK(!stampOf(u8(root / "missing.bin")).ok);

    const std::string tag = settingsTag("auto", true);
    CHECK(tag != settingsTag("auto", false) && tag != settingsTag("hap_q", true));
    const std::string key = keyFor("abc", tag);
    CHECK(key.size() == 32 && key.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(key != keyFor("abd", tag) && key != keyFor("abc", settingsTag("hap", true)) && key == keyFor("abc", tag));

    Entry full;
    full.key = key;
    full.source = "C:\\Videos\\Mein Clip\nmit Zeilenumbruch.mov";
    full.fingerprint = "f";
    full.tag = tag;
    full.sourceBytes = 123456789012ull;
    full.sourceMtime = 1700000000;
    full.width = 1920;
    full.height = 1080;
    full.fps = 29.97;
    full.frames = 900;
    full.format = "hap_q";
    full.alphaKept = true;
    full.bytes = 987654321ull;
    full.convertSeconds = 12.5;
    full.created = 5;
    full.lastUsed = 6;
    full.bad = true;
    full.badReason = "reason";
    Entry parsed;
    CHECK(parseEntry(serializeEntry(full), parsed));
    CHECK(parsed.key == key && parsed.source == "C:\\Videos\\Mein Clip mit Zeilenumbruch.mov" && parsed.sourceBytes == 123456789012ull && parsed.sourceMtime == 1700000000);
    CHECK(parsed.width == 1920 && parsed.height == 1080 && std::abs(parsed.fps - 29.97) < 1e-5 && parsed.frames == 900 && parsed.format == "hap_q" && parsed.alphaKept);
    CHECK(parsed.bytes == 987654321ull && parsed.created == 5 && parsed.lastUsed == 6 && parsed.bad && parsed.badReason == "reason" && parsed.converter == kConverterVersion);
    CHECK(!parseEntry("garbage", parsed) && !parseEntry("key=short\nbytes=1\n", parsed) && !parseEntry("", parsed));

    Cache cache(root / "store");
    Entry entry = makeEntry(cache, "one", 1000, 100);
    Entry found;
    CHECK(cache.find(entry.key, found) && found.bytes == 1000 && found.source == "/videos/one.mov" && !found.bad);
    CHECK(!cache.find("nothex", found) && !cache.find(std::string(32, 'a'), found));
    {
        std::ofstream trunc(cache.dataPath(entry.key), std::ios::binary | std::ios::trunc);
        trunc << "short";
    }
    CHECK(!cache.find(entry.key, found));
    writeBytes(cache.dataPath(entry.key), 1000, 3);
    CHECK(cache.find(entry.key, found));
    Entry other = entry;
    other.converter = 99;
    CHECK(cache.commit(other) && !cache.find(entry.key, found));
    CHECK(cache.commit(entry) && cache.find(entry.key, found));
    CHECK(cache.touch(entry.key) && cache.find(entry.key, found) && found.lastUsed >= nowSeconds() - 5);
    CHECK(cache.markBad(entry.key, "no good") && cache.find(entry.key, found) && found.bad && found.badReason == "no good");
    CHECK(!cache.markBad(std::string(32, 'b'), "x"));
    CHECK(cache.remove(entry.key) && !cache.find(entry.key, found));
    CHECK(!fs::exists(cache.dataPath(entry.key)) && !fs::exists(cache.metaPath(entry.key)));

    writeBytes(cache.directory() / "readme.meta", 20, 1);
    {
        Entry lonely;
        lonely.key = keyFor("lonely", "tag");
        lonely.bytes = 10;
        CHECK(cache.commit(lonely));
    }
    CHECK(cache.list().empty());
    fs::remove(cache.directory() / "readme.meta", ec);
    fs::remove(cache.metaPath(keyFor("lonely", "tag")), ec);

    const Entry k1 = makeEntry(cache, "k1", 1000, 100);
    const Entry k2 = makeEntry(cache, "k2", 1000, 200);
    const Entry k3 = makeEntry(cache, "k3", 1000, 300);
    const Entry k4 = makeEntry(cache, "k4", 1000, 400);
    CHECK(cache.list().size() == 4 && cache.totalBytes() == 4000);
    CHECK(cache.markBad(k2.key, "bad"));
    CHECK(cache.evict(5000, {}) == 0);
    CHECK(cache.evict(2500, {k1.key}) == 2);
    CHECK(!cache.find(k2.key, found) && cache.find(k1.key, found) && !cache.find(k3.key, found) && cache.find(k4.key, found));
    CHECK(cache.totalBytes() == 2000);
    CHECK(cache.clear({k4.key}) == 1 && cache.find(k4.key, found) && !cache.find(k1.key, found));

    const Entry keep = makeEntry(cache, "keep", 500, 10);
    const std::string oldPart = u8(cache.partPath(keyFor("part-old", "tag")));
    const std::string freshPart = u8(cache.partPath(keyFor("part-fresh", "tag")));
    const std::string activePart = u8(cache.partPath(keyFor("part-active", "tag")));
    writeBytes(fs::path(std::u8string(oldPart.begin(), oldPart.end())), 100, 1);
    writeBytes(fs::path(std::u8string(freshPart.begin(), freshPart.end())), 100, 1);
    writeBytes(fs::path(std::u8string(activePart.begin(), activePart.end())), 100, 1);
    const auto longAgo = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(fs::path(std::u8string(oldPart.begin(), oldPart.end())), longAgo);
    fs::last_write_time(fs::path(std::u8string(activePart.begin(), activePart.end())), longAgo);
    writeBytes(cache.dataPath(keyFor("orphan-data", "tag")), 100, 1);
    {
        Entry orphan;
        orphan.key = keyFor("orphan-meta", "tag");
        orphan.bytes = 50;
        CHECK(cache.commit(orphan));
    }
    writeBytes(cache.directory() / (keyFor("tmp", "tag") + ".meta.tmp"), 10, 1);
    writeBytes(cache.directory() / "dismissed.txt", 10, 1);
    CHECK(cache.cleanStale(24 * 3600, {keyFor("part-active", "tag")}) == 4);
    CHECK(!fs::exists(fs::path(std::u8string(oldPart.begin(), oldPart.end()))));
    CHECK(fs::exists(fs::path(std::u8string(freshPart.begin(), freshPart.end()))));
    CHECK(fs::exists(fs::path(std::u8string(activePart.begin(), activePart.end()))));
    CHECK(fs::exists(cache.directory() / "dismissed.txt"));
    CHECK(cache.find(keep.key, found));
}

void testRedirectHooks() {
    namespace redirect = nvj::redirect;
    redirect::uninstall();
    CHECK(!redirect::installed() && redirect::lookup("a.mov").empty() && redirect::generation() == 0);
    redirect::markBad("a.mov", "x");
    int bad = 0;
    std::string badSource;
    redirect::Hooks hooks;
    hooks.lookup = [](const std::string& path) { return path == "a.mov" ? std::string("a_hap.mov") : std::string(); };
    hooks.markBad = [&](const std::string& path, const std::string& reason) {
        ++bad;
        badSource = path + ":" + reason;
    };
    hooks.generation = [] { return std::uint64_t(7); };
    redirect::install(hooks);
    CHECK(redirect::installed() && redirect::lookup("a.mov") == "a_hap.mov" && redirect::lookup("b.mov").empty() && redirect::generation() == 7);
    redirect::markBad("a.mov", "broken");
    CHECK(bad == 1 && badSource == "a.mov:broken");
    redirect::uninstall();
    CHECK(!redirect::installed() && redirect::lookup("a.mov").empty());
}

struct Fake {
    std::atomic<int> converts{0};
    std::atomic<int> inspects{0};
    std::atomic<bool> hold{false};
    std::atomic<bool> failNext{false};
    unsigned long long outputBytes = 4096;
    int frames = 5;
};

nvj::hapjobs::Backend fakeBackend(const std::shared_ptr<Fake>& fake) {
    nvj::hapjobs::Backend backend;
    backend.inspect = [fake](const std::string& path) {
        ++fake->inspects;
        nvj::hapconv::SourceInfo info;
        if (path.find("plain") != std::string::npos) {
            info.error = "The video is not DXV";
            return info;
        }
        info.ok = true;
        info.isDxv = true;
        info.variant = nvj::hapconv::Variant::Dxt5;
        info.width = 128;
        info.height = 72;
        info.fps = 25.0;
        info.frames = fake->frames;
        info.duration = fake->frames / 25.0;
        return info;
    };
    backend.convert = [fake](const std::string& source, const std::string& output, const nvj::hapconv::Options&, const nvj::hapconv::ProgressFn& progress) {
        ++fake->converts;
        nvj::hapconv::Result result;
        while (fake->hold.load()) {
            if (!progress(1, fake->frames)) {
                result.cancelled = true;
                result.error = "cancelled";
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        for (int frame = 1; frame <= fake->frames; ++frame) {
            if (!progress(frame, fake->frames)) {
                result.cancelled = true;
                result.error = "cancelled";
                return result;
            }
        }
        if (fake->failNext.exchange(false)) {
            result.error = "The encoder stopped";
            return result;
        }
        writeBytes(fs::path(std::u8string(output.begin(), output.end())), static_cast<size_t>(fake->outputBytes), 9);
        result.ok = true;
        result.variant = nvj::hapconv::Variant::Dxt5;
        result.format = nvj::hapconv::TargetFormat::HapAlpha;
        result.alphaKept = true;
        result.width = 128;
        result.height = 72;
        result.fps = 25.0;
        result.frames = fake->frames;
        result.bytes = fake->outputBytes;
        result.seconds = 0.01;
        (void)source;
        return result;
    };
    return backend;
}

bool waitUntil(const std::function<bool()>& predicate, int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

int countFiles(const fs::path& directory, const std::string& extension) {
    int count = 0;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (u8(it->path().extension()) == extension) {
            ++count;
        }
    }
    return count;
}

void testManagerLogic(const fs::path& scratch) {
    using namespace nvj::hapjobs;
    const fs::path root = scratch / "manager";
    std::error_code ec;
    fs::remove_all(root, ec);
    int seed = 1;
    auto source = [&](const std::string& name, size_t size = 3000) {
        const fs::path path = root / "videos" / name;
        writeBytes(path, size, static_cast<unsigned char>(seed++));
        return u8(path);
    };
    Config automatic;
    automatic.mode = ImportMode::Automatic;

    CHECK(importModeFromName("ask") == ImportMode::Ask && importModeFromName("automatic") == ImportMode::Automatic && importModeFromName("off") == ImportMode::Off && importModeFromName("zzz") == ImportMode::Off);
    CHECK(std::string(importModeName(ImportMode::Ask)) == "ask" && std::string(importModeName(ImportMode::Automatic)) == "automatic" && std::string(importModeName(ImportMode::Off)) == "off");
    CHECK(isCandidatePath("C:/clips/a.mov") && isCandidatePath("a.MOV") && !isCandidatePath("a.mp4") && !isCandidatePath("a"));
    CHECK(jobIsFinished(JobState::Done) && jobIsFinished(JobState::Failed) && !jobIsFinished(JobState::Waiting) && !jobIsFinished(JobState::Converting));

    const std::string a = source("a.mov");
    const std::string originalHash = [&] {
        std::string hex;
        nvj::sha256FileHex(fs::path(std::u8string(a.begin(), a.end())), hex);
        return hex;
    }();
    {
        auto fake = std::make_shared<Fake>();
        Manager manager(root / "cache1");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.offer(a);
        CHECK(manager.jobs().empty() && fake->inspects == 0);
        manager.setReady(true);
        const std::uint64_t before = manager.generation();
        manager.offer(a);
        CHECK(manager.waitIdle(5000));
        std::vector<JobInfo> jobs = manager.jobs();
        CHECK(jobs.size() == 1 && jobs[0].state == JobState::Done && jobs[0].framesDone == 5 && jobs[0].framesTotal == 5 && jobs[0].bytes == 4096 && jobs[0].name == "a.mov");
        CHECK(jobs[0].format == "HAP Alpha" || jobs[0].format == "hap_alpha" || !jobs[0].format.empty());
        CHECK(jobs[0].alphaKept && jobs[0].width == 128 && jobs[0].height == 72 && jobs[0].sourceBytes == 3000);
        CHECK(manager.generation() > before);
        const std::string converted = manager.convertedPathFor(a);
        CHECK(!converted.empty() && fs::exists(fs::path(std::u8string(converted.begin(), converted.end()))) && fs::file_size(fs::path(std::u8string(converted.begin(), converted.end()))) == 4096);
        CHECK(manager.convertedPathFor(u8(root / "videos" / "nothing.mov")).empty());
        CacheStats stats = manager.cacheStats();
        CHECK(stats.entries == 1 && stats.bytes == 4096 && stats.limitBytes == automatic.cacheLimitBytes);
        CHECK(fake->converts == 1 && countFiles(root / "cache1", ".part") == 0);

        manager.offer(a);
        CHECK(manager.waitIdle(2000) && manager.jobs().size() == 1 && fake->converts == 1);

        const std::string copy = u8(root / "videos" / "a_copy.mov");
        fs::copy_file(fs::path(std::u8string(a.begin(), a.end())), fs::path(std::u8string(copy.begin(), copy.end())));
        manager.offer(copy);
        CHECK(manager.waitIdle(5000));
        jobs = manager.jobs();
        CHECK(jobs.size() == 2 && jobs[1].state == JobState::Cached && fake->converts == 1);
        CHECK(manager.convertedPathFor(copy) == converted);

        const auto soon = fs::file_time_type::clock::now() + std::chrono::hours(1);
        fs::last_write_time(fs::path(std::u8string(a.begin(), a.end())), soon);
        CHECK(manager.convertedPathFor(a).empty());
        manager.clearFinished();
        CHECK(manager.jobs().empty());
        manager.offer(a);
        CHECK(manager.waitIdle(5000));
        jobs = manager.jobs();
        CHECK(jobs.size() == 1 && jobs[0].state == JobState::Cached && fake->converts == 1);
        CHECK(manager.convertedPathFor(a) == converted);

        writeBytes(fs::path(std::u8string(a.begin(), a.end())), 3500, 77);
        CHECK(manager.convertedPathFor(a).empty());
        manager.offer(a);
        CHECK(manager.waitIdle(5000) && fake->converts == 2 && !manager.convertedPathFor(a).empty() && manager.convertedPathFor(a) != converted);
    }
    {
        auto fake = std::make_shared<Fake>();
        Manager manager(root / "cache1");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.setReady(true);
        CHECK(!manager.convertedPathFor(a).empty());
        manager.offer(a);
        CHECK(manager.waitIdle(2000) && manager.jobs().empty() && fake->inspects == 0);
        CHECK(manager.cacheStats().entries == 2);
        manager.offer(u8(root / "videos" / "a_copy.mov"));
        CHECK(manager.waitIdle(5000));
        CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Cached && fake->converts == 0);
        CHECK(!manager.convertedPathFor(u8(root / "videos" / "a_copy.mov")).empty());
    }
    {
        std::string hex;
        const std::string pristine = source("pristine.mov");
        nvj::sha256FileHex(fs::path(std::u8string(pristine.begin(), pristine.end())), hex);
        auto fake = std::make_shared<Fake>();
        Manager manager(root / "cache_orig");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.setReady(true);
        manager.offer(pristine);
        CHECK(manager.waitIdle(5000));
        std::string after;
        nvj::sha256FileHex(fs::path(std::u8string(pristine.begin(), pristine.end())), after);
        CHECK(after == hex && !hex.empty());
        (void)originalHash;
    }
    {
        auto fake = std::make_shared<Fake>();
        Config off;
        off.mode = ImportMode::Off;
        Manager manager(root / "cache2");
        manager.setBackend(fakeBackend(fake));
        manager.configure(off);
        manager.setReady(true);
        const std::string c = source("c.mov");
        manager.offer(c);
        CHECK(manager.waitIdle(1000) && manager.jobs().empty() && fake->inspects == 0);
        const int id = manager.request(c);
        CHECK(id > 0 && manager.waitIdle(5000));
        CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Done && !manager.convertedPathFor(c).empty());
        CHECK(manager.request(c, false) > 0);
        CHECK(manager.waitIdle(5000));
        const std::string manual = source("manual.mov");
        manager.offer(manual, true);
        CHECK(manager.waitIdle(5000));
        CHECK(manager.jobs().back().source == manual && manager.jobs().back().state == JobState::Done && !manager.convertedPathFor(manual).empty());
        const std::string manualPlain = source("manual_plain.mov");
        manager.offer(manualPlain, true);
        CHECK(manager.waitIdle(5000));
        bool leftover = false;
        for (const JobInfo& job : manager.jobs()) {
            leftover = leftover || job.source == manualPlain;
        }
        CHECK(!leftover);
    }
    {
        auto fake = std::make_shared<Fake>();
        Config ask;
        ask.mode = ImportMode::Ask;
        const std::string d = source("d.mov");
        const std::string e = source("e.mov");
        const std::string f = source("f.mov");
        const std::string g = source("g.mov");
        {
            Manager manager(root / "cache3");
            manager.setBackend(fakeBackend(fake));
            manager.configure(ask);
            manager.setReady(true);
            manager.offer(d);
            CHECK(manager.waitIdle(5000));
            CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Waiting && manager.waitingCount() == 1 && !manager.busy() && fake->converts == 0);
            CHECK(manager.convertedPathFor(d).empty());
            manager.approve(manager.jobs()[0].id);
            CHECK(manager.waitIdle(5000));
            CHECK(manager.jobs()[0].state == JobState::Done && manager.waitingCount() == 0 && fake->converts == 1 && !manager.convertedPathFor(d).empty());
            manager.clearFinished();

            manager.offer(e);
            CHECK(manager.waitIdle(5000) && manager.waitingCount() == 1);
            manager.dismiss(manager.jobs()[0].id);
            CHECK(manager.jobs().empty() && manager.waitingCount() == 0);
            manager.offer(e);
            CHECK(manager.waitIdle(1000) && manager.jobs().empty());

            manager.offer(f);
            manager.offer(g);
            CHECK(manager.waitIdle(5000) && manager.waitingCount() == 2);
            manager.dismissAll();
            CHECK(manager.jobs().empty());
            manager.offer(f, true);
            CHECK(manager.waitIdle(5000) && manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Done);
            manager.clearFinished();
        }
        {
            Manager manager(root / "cache3");
            manager.setBackend(fakeBackend(fake));
            manager.configure(ask);
            manager.setReady(true);
            manager.offer(e);
            manager.offer(f);
            manager.offer(g);
            CHECK(manager.waitIdle(2000) && manager.jobs().empty());
            const std::string h = source("h.mov");
            manager.offer(h);
            CHECK(manager.waitIdle(5000) && manager.waitingCount() == 1);
            Config automaticAgain = ask;
            automaticAgain.mode = ImportMode::Automatic;
            manager.configure(automaticAgain);
            CHECK(manager.waitIdle(5000));
            CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Done);
            const std::string i = source("i.mov");
            manager.configure(ask);
            manager.offer(i);
            CHECK(manager.waitIdle(5000) && manager.waitingCount() == 1);
            const int id = manager.jobs().back().id;
            manager.cancel(id);
            CHECK(manager.waitingCount() == 0 && manager.jobs().back().state == JobState::Cancelled);
            Config off = ask;
            off.mode = ImportMode::Off;
            const std::string j = source("j.mov");
            manager.configure(ask);
            manager.offer(j);
            CHECK(manager.waitIdle(5000) && manager.waitingCount() == 1);
            manager.configure(off);
            CHECK(manager.waitingCount() == 0 && manager.jobs().back().state == JobState::Cancelled);
        }
    }
    {
        auto fake = std::make_shared<Fake>();
        Manager manager(root / "cache4");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.setReady(true);
        const std::string plain = source("plain.mov");
        manager.offer(plain);
        CHECK(manager.waitIdle(5000) && manager.jobs().empty());
        manager.request(plain);
        CHECK(manager.waitIdle(5000));
        CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Failed && manager.jobs()[0].error.find("not DXV") != std::string::npos && fake->converts == 0);
        manager.request(u8(root / "videos" / "does_not_exist.mov"));
        CHECK(manager.waitIdle(5000));
        CHECK(manager.jobs().back().state == JobState::Failed && !manager.jobs().back().error.empty());
        manager.setReady(false);
        const int id = manager.request(source("k.mov"));
        CHECK(manager.waitIdle(2000));
        bool seen = false;
        for (const JobInfo& job : manager.jobs()) {
            if (job.id == id) {
                seen = job.state == JobState::Failed && job.error.find("FFmpeg") != std::string::npos;
            }
        }
        CHECK(seen);
        const int before = static_cast<int>(manager.jobs().size());
        manager.offer(source("l.mov"));
        CHECK(manager.waitIdle(1000) && static_cast<int>(manager.jobs().size()) == before);
    }
    {
        auto fake = std::make_shared<Fake>();
        fake->hold = true;
        Manager manager(root / "cache5");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.setReady(true);
        const std::string m = source("m.mov");
        const std::string n = source("n.mov");
        manager.offer(m);
        CHECK(waitUntil([&] { const auto jobs = manager.jobs(); return !jobs.empty() && jobs[0].state == JobState::Converting && jobs[0].framesDone >= 1; }, 5000));
        CHECK(manager.busy() && manager.activeCount() == 1);
        manager.offer(n);
        CHECK(manager.activeCount() == 2);
        manager.cancel(manager.jobs()[0].id);
        fake->hold = false;
        CHECK(manager.waitIdle(5000));
        const auto jobs = manager.jobs();
        CHECK(jobs.size() == 2 && jobs[0].state == JobState::Cancelled && jobs[1].state == JobState::Done);
        CHECK(manager.convertedPathFor(m).empty() && !manager.convertedPathFor(n).empty());
        CHECK(manager.cacheStats().entries == 1 && countFiles(root / "cache5", ".part") == 0);
        CHECK(!fs::exists(fs::path(std::u8string(m.begin(), m.end()))) == false);

        const std::string o = source("o.mov");
        const std::string p = source("p.mov");
        fake->hold = true;
        manager.offer(o);
        manager.offer(p);
        CHECK(waitUntil([&] { const auto list = manager.jobs(); return list.size() == 4 && list[2].state == JobState::Converting; }, 5000));
        manager.cancelAll();
        fake->hold = false;
        CHECK(manager.waitIdle(5000));
        const auto after = manager.jobs();
        CHECK(after[2].state == JobState::Cancelled && after[3].state == JobState::Cancelled && manager.cacheStats().entries == 1);
        manager.clearFinished();
        CHECK(manager.jobs().empty());
    }
    {
        auto fake = std::make_shared<Fake>();
        fake->failNext = true;
        Manager manager(root / "cache6");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.setReady(true);
        const std::string q = source("q.mov");
        manager.offer(q);
        CHECK(manager.waitIdle(5000));
        CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Failed && manager.jobs()[0].error == "The encoder stopped");
        CHECK(manager.cacheStats().entries == 0 && countFiles(root / "cache6", ".part") == 0 && countFiles(root / "cache6", ".mov") == 0);
        manager.offer(q);
        CHECK(manager.waitIdle(1000) && manager.jobs().size() == 1 && fake->converts == 1);
        manager.request(q);
        CHECK(manager.waitIdle(5000) && manager.jobs().back().state == JobState::Done && !manager.convertedPathFor(q).empty());
    }
    {
        auto fake = std::make_shared<Fake>();
        const std::string r = source("r.mov");
        std::string converted;
        {
            Manager manager(root / "cache7");
            manager.setBackend(fakeBackend(fake));
            manager.configure(automatic);
            manager.setReady(true);
            manager.offer(r);
            CHECK(manager.waitIdle(5000));
            converted = manager.convertedPathFor(r);
            CHECK(!converted.empty());
            manager.markBad(r, "test failure");
            CHECK(manager.convertedPathFor(r).empty());
            manager.markBad(u8(root / "videos" / "unknown.mov"), "x");
        }
        Manager manager(root / "cache7");
        manager.setBackend(fakeBackend(fake));
        manager.configure(automatic);
        manager.setReady(true);
        CHECK(manager.convertedPathFor(r).empty());
        const int converts = fake->converts;
        manager.offer(r);
        CHECK(manager.waitIdle(5000) && manager.jobs().empty() && fake->converts == converts);
        manager.request(r);
        CHECK(manager.waitIdle(5000));
        CHECK(manager.jobs().size() == 1 && manager.jobs()[0].state == JobState::Failed && manager.jobs()[0].error.find("failed earlier") != std::string::npos);
        manager.request(r, true);
        CHECK(manager.waitIdle(5000) && fake->converts == converts + 1 && !manager.convertedPathFor(r).empty());
        CHECK(manager.jobs().back().state == JobState::Done);
    }
    {
        auto fake = std::make_shared<Fake>();
        fake->outputBytes = 600 * 1024;
        Config limited = automatic;
        limited.cacheLimitBytes = 1024 * 1024;
        Manager manager(root / "cache8");
        manager.setBackend(fakeBackend(fake));
        manager.configure(limited);
        manager.setReady(true);
        const std::string k1 = source("k1.mov");
        const std::string k2 = source("k2.mov");
        const std::string k3 = source("k3.mov");
        manager.offer(k1);
        CHECK(manager.waitIdle(5000));
        manager.offer(k2);
        CHECK(manager.waitIdle(5000));
        manager.offer(k3);
        CHECK(manager.waitIdle(5000));
        CHECK(manager.cacheStats().entries == 1 && manager.cacheStats().bytes == 600 * 1024);
        CHECK(manager.convertedPathFor(k1).empty() && manager.convertedPathFor(k2).empty() && !manager.convertedPathFor(k3).empty());
        const std::uint64_t before = manager.generation();
        CHECK(manager.clearCache() == 1 && manager.cacheStats().entries == 0 && manager.convertedPathFor(k3).empty() && manager.generation() > before);
    }
    {
        auto fake = std::make_shared<Fake>();
        fake->outputBytes = 600 * 1024;
        Config limited = automatic;
        limited.cacheLimitBytes = 1024 * 1024;
        Manager manager(root / "cache9");
        manager.setBackend(fakeBackend(fake));
        manager.configure(limited);
        manager.setReady(true);
        const std::string s1 = source("s1.mov");
        const std::string s2 = source("s2.mov");
        manager.offer(s1);
        CHECK(manager.waitIdle(5000));
        CHECK(!manager.convertedPathFor(s1).empty());
        manager.offer(s2);
        CHECK(manager.waitIdle(5000));
        CHECK(manager.cacheStats().entries == 2 && !manager.convertedPathFor(s1).empty() && !manager.convertedPathFor(s2).empty());
        Config small = limited;
        small.cacheLimitBytes = 100;
        manager.configure(small);
        CHECK(manager.trimCache() == 0 && manager.cacheStats().entries == 2);
    }
    {
        auto fake = std::make_shared<Fake>();
        fake->hold = true;
        const auto start = std::chrono::steady_clock::now();
        {
            Manager manager(root / "cache10");
            manager.setBackend(fakeBackend(fake));
            manager.configure(automatic);
            manager.setReady(true);
            manager.offer(source("t.mov"));
            CHECK(waitUntil([&] { const auto jobs = manager.jobs(); return !jobs.empty() && jobs[0].state == JobState::Converting; }, 5000));
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(seconds < 3.0 && countFiles(root / "cache10", ".part") == 0);
    }
    {
        auto fake = std::make_shared<Fake>();
        Manager manager(root / "cache11");
        manager.setBackend(fakeBackend(fake));
        Config formatted = automatic;
        formatted.format = nvj::hapconv::TargetFormat::HapQ;
        manager.configure(formatted);
        manager.setReady(true);
        const std::string u = source("u.mov");
        manager.offer(u);
        CHECK(manager.waitIdle(5000) && fake->converts == 1);
        const std::string first = manager.convertedPathFor(u);
        formatted.format = nvj::hapconv::TargetFormat::Hap;
        manager.configure(formatted);
        manager.offer(u);
        CHECK(manager.waitIdle(5000) && fake->converts == 2);
        const std::string second = manager.convertedPathFor(u);
        CHECK(!first.empty() && !second.empty() && first != second);
        formatted.format = nvj::hapconv::TargetFormat::HapQ;
        manager.configure(formatted);
        manager.request(u);
        CHECK(manager.waitIdle(5000) && fake->converts == 2 && manager.jobs().back().state == JobState::Cached && manager.convertedPathFor(u) == first);
    }
}

void testManagerReal(const fs::path& scratch) {
    using namespace nvj::hapjobs;
    if (!nvj::ffmpeg_setup::checkCapabilities().ready()) {
        std::printf("real conversion test skipped: FFmpeg with DXV decoder and HAP encoder is not available\n");
        return;
    }
    const fs::path root = scratch / "manager_real";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "videos", ec);
    const fs::path original = root / "videos" / "real.mov";
    fs::copy_file(kData / "test_dxv_dxt1.mov", original);
    const std::string source = u8(original);
    std::string before;
    nvj::sha256FileHex(original, before);
    Config config;
    config.mode = ImportMode::Automatic;
    Manager manager(root / "cache");
    manager.configure(config);
    manager.setReady(true);
    manager.offer(source);
    CHECK(manager.waitIdle(30000));
    const std::vector<JobInfo> jobs = manager.jobs();
    CHECK(jobs.size() == 1 && jobs[0].state == JobState::Done);
    if (jobs.empty() || jobs[0].state != JobState::Done) {
        std::printf("real conversion error: %s\n", jobs.empty() ? "no job" : jobs[0].error.c_str());
        return;
    }
    CHECK(jobs[0].variant == nvj::hapconv::Variant::Dxt1 && jobs[0].framesDone == 12 && jobs[0].width == 128 && jobs[0].height == 72 && !jobs[0].alphaKept);
    const std::string converted = manager.convertedPathFor(source);
    CHECK(!converted.empty());
    nvj::VideoDecoder decoder;
    std::string error;
    CHECK(decoder.open(converted, error, nvj::VideoOptions()));
    CHECK(decoder.status().codec == "hap" && decoder.info().width == 128 && decoder.info().height == 72);
    std::string after;
    nvj::sha256FileHex(original, after);
    CHECK(before == after && !before.empty());
    CHECK(fs::file_size(original) == 19057);
    std::printf("real manager conversion: %.3f s, %llu bytes -> %llu bytes\n", jobs[0].seconds, jobs[0].sourceBytes, jobs[0].bytes);

    CHECK(nvj::hapcache::stampOf(source).ok);
    Manager second(root / "cache");
    second.configure(config);
    second.setReady(true);
    CHECK(second.convertedPathFor(source) == converted);
    second.offer(source);
    CHECK(second.waitIdle(5000) && second.jobs().empty());
}

int main() {
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_convert_test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", (scratch / "data").string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", (scratch / "data").string().c_str(), 1);
#endif
    testSafePaths();
    testCrcAndInflate();
    testZip(scratch);
    testTar(scratch);
    testPackages();
    testChecksumParsing();
    testArchiveMapping();
    testLicenseInspection(scratch);
    testInstall(scratch);
    testRealArchives(scratch);
    testCapabilitiesWithoutLibraries();
    testCache(scratch);
    testRedirectHooks();
    testManagerLogic(scratch);
    testManagerReal(scratch);
    testNetworkInstall();
    return testing::finish();
}

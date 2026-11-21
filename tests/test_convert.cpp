#include "check.hpp"

#include "core/archive.hpp"
#include "core/sha256.hpp"
#include "media/ffmpeg_setup.hpp"
#include "media/video_decoder.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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
    testNetworkInstall();
    return testing::finish();
}

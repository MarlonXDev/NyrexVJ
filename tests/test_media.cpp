#include "check.hpp"

#include "media/media_image.hpp"
#include "media/video_decoder.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

namespace {

namespace fs = std::filesystem;

const fs::path kData = NYREXVJ_TEST_DATA;

void writeTga(const fs::path& path) {
    std::vector<uint8_t> bytes(18, 0);
    bytes[2] = 2;
    bytes[12] = 4;
    bytes[14] = 2;
    bytes[16] = 32;
    bytes[17] = 0x28;
    for (int i = 0; i < 8; ++i) {
        const bool opaque = i % 4 < 2;
        bytes.push_back(30);
        bytes.push_back(60);
        bytes.push_back(240);
        bytes.push_back(opaque ? 255 : 0);
    }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void testNames() {
    CHECK(nvj::isImageFileName("a.PNG") && nvj::isImageFileName("b.jpeg") && nvj::isImageFileName("c.tga") && nvj::isImageFileName("d.gif") && nvj::isImageFileName("e.bmp"));
    CHECK(!nvj::isImageFileName("a.mp4") && !nvj::isImageFileName("noextension") && !nvj::isImageFileName("a.txt"));
    CHECK(nvj::isVideoFileName("a.MP4") && nvj::isVideoFileName("b.mov") && nvj::isVideoFileName("c.mkv") && nvj::isVideoFileName("d.avi") && nvj::isVideoFileName("e.webm") && nvj::isVideoFileName("f.m4v"));
    CHECK(!nvj::isVideoFileName("a.png") && !nvj::isVideoFileName("a.glb"));
    CHECK(nvj::imageClipName("x") == "Image: x" && nvj::videoClipName("x") == "Video: x");
}

void testPremultiply() {
    std::vector<uint8_t> pixels = {200, 100, 50, 255, 200, 100, 50, 128, 200, 100, 50, 0};
    nvj::premultiplyAlpha(pixels);
    CHECK(pixels[0] == 200 && pixels[1] == 100 && pixels[2] == 50 && pixels[3] == 255);
    CHECK(pixels[4] == 100 && pixels[5] == 50 && pixels[6] == 25 && pixels[7] == 128);
    CHECK(pixels[8] == 0 && pixels[9] == 0 && pixels[10] == 0 && pixels[11] == 0);
}

void testImages(const fs::path& scratch) {
    nvj::SceneImage image;
    std::string error;
    CHECK(nvj::loadMediaImage(kData / "test_alpha.png", image, error));
    CHECK(image.width == 8 && image.height == 8 && image.rgba.size() == 8 * 8 * 4);
    CHECK(image.rgba[0] == 200 && image.rgba[1] == 100 && image.rgba[2] == 50 && image.rgba[3] == 255);
    const size_t half = 4 * 4;
    CHECK(image.rgba[half + 3] == 128 && image.rgba[half] == 100 && image.rgba[half + 1] == 50 && image.rgba[half + 2] == 25);
    const size_t clear = 7 * 4;
    CHECK(image.rgba[clear + 3] == 0 && image.rgba[clear] == 0 && image.rgba[clear + 1] == 0 && image.rgba[clear + 2] == 0);

    const fs::path tga = scratch / "alpha.tga";
    writeTga(tga);
    nvj::SceneImage fromTga;
    CHECK(nvj::loadMediaImage(tga, fromTga, error));
    CHECK(fromTga.width == 4 && fromTga.height == 2);
    CHECK(fromTga.rgba[3] == 255 && fromTga.rgba[0] == 240 && fromTga.rgba[2] == 30);
    CHECK(fromTga.rgba[2 * 4 + 3] == 0 && fromTga.rgba[2 * 4] == 0);

    CHECK(!nvj::loadMediaImage(scratch / "missing.png", image, error) && !error.empty());
    const fs::path empty = scratch / "empty.png";
    std::ofstream(empty).close();
    CHECK(!nvj::loadMediaImage(empty, image, error));
    const fs::path junk = scratch / "junk.png";
    std::ofstream(junk) << "this is not a picture at all";
    CHECK(!nvj::loadMediaImage(junk, image, error));
    CHECK(image.width == 8);
}

void testLibrary(const fs::path& scratch) {
    const fs::path media = fs::path(nvj::mediaDirectory());
    std::error_code ec;
    fs::create_directories(media, ec);
    fs::copy_file(kData / "test_alpha.png", media / "Logo Mark.png", fs::copy_options::overwrite_existing, ec);
    fs::copy_file(kData / "test_h264.mp4", media / "Intro Loop.mp4", fs::copy_options::overwrite_existing, ec);
    fs::copy_file(kData / "test_hap.mov", media / "Tunnel.MOV", fs::copy_options::overwrite_existing, ec);
    std::ofstream(media / "notes.txt") << "not media";
    const std::vector<nvj::AssetFile> assets = nvj::listMediaAssets();
    CHECK(assets.size() == 3);
    const int before = static_cast<int>(nvj::clipLibrary().size());
    const int added = nvj::rescanLibrary();
    CHECK(added == 0 || added == 3);
    const int image = nvj::clipIndexByName("Image: Logo Mark");
    const int video = nvj::clipIndexByName("Video: Intro Loop");
    const int hap = nvj::clipIndexByName("Video: Tunnel");
    CHECK(image >= 0 && video >= 0 && hap >= 0);
    CHECK(nvj::clipIndexByName("Image: notes") < 0);
    if (image >= 0 && video >= 0) {
        const nvj::ClipDef& imageDef = nvj::clipLibrary()[static_cast<size_t>(image)];
        const nvj::ClipDef& videoDef = nvj::clipLibrary()[static_cast<size_t>(video)];
        CHECK(imageDef.kind == nvj::ClipKind::ImageFile && imageDef.isMedia() && !imageDef.isScene() && !imageDef.is3d);
        CHECK(videoDef.kind == nvj::ClipKind::VideoFile && videoDef.isMedia());
        CHECK(!imageDef.path.empty() && fs::exists(fs::path(std::u8string(imageDef.path.begin(), imageDef.path.end()))));
    }
    const int again = nvj::rescanLibrary();
    CHECK(again == 0);
    CHECK(static_cast<int>(nvj::clipLibrary().size()) >= before);
    (void)scratch;
}

void testImport(const fs::path& scratch) {
    const fs::path media = fs::path(nvj::mediaDirectory());
    std::error_code ec;
    fs::remove_all(media, ec);

    const nvj::ImportResult first = nvj::importAsset((kData / "test_alpha.png").string());
    CHECK(first.kind == nvj::ImportKind::Media && first.added && first.name == "test_alpha.png");
    CHECK(fs::exists(media / "test_alpha.png"));

    const nvj::ImportResult again = nvj::importAsset((kData / "test_alpha.png").string());
    CHECK(again.kind == nvj::ImportKind::Media && !again.added);

    const fs::path other = scratch / "other";
    fs::create_directories(other, ec);
    std::ofstream(other / "test_alpha.png") << "different content";
    const nvj::ImportResult renamed = nvj::importAsset((other / "test_alpha.png").string());
    CHECK(renamed.kind == nvj::ImportKind::Media && renamed.added && renamed.name == "test_alpha (2).png");
    CHECK(fs::exists(media / "test_alpha (2).png"));

    const nvj::ImportResult video = nvj::importAsset((kData / "test_h264.mp4").string());
    CHECK(video.kind == nvj::ImportKind::Media && video.added);

    std::ofstream(other / "notes.txt") << "text";
    CHECK(nvj::importAsset((other / "notes.txt").string()).kind == nvj::ImportKind::Unsupported);
    CHECK(nvj::importAsset((other / "missing.png").string()).kind == nvj::ImportKind::Missing);
    CHECK(nvj::importAsset(other.string()).kind == nvj::ImportKind::Missing);

    std::ofstream(other / "thing.glb") << "glTF";
    const nvj::ImportResult model = nvj::importAsset((other / "thing.glb").string());
    CHECK(model.kind == nvj::ImportKind::Model && model.added);
    CHECK(fs::exists(fs::path(nvj::modelDirectory()) / "thing.glb"));
    fs::remove(fs::path(nvj::modelDirectory()) / "thing.glb", ec);
}

std::shared_ptr<const nvj::VideoFrame> waitForFrame(nvj::VideoDecoder& decoder, double time, double low, double high) {
    const auto start = std::chrono::steady_clock::now();
    std::shared_ptr<const nvj::VideoFrame> frame;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 5.0) {
        frame = decoder.frameAt(time);
        if (frame && frame->time >= low && frame->time <= high) {
            return frame;
        }
        if (decoder.failed()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return frame;
}

uint64_t checksum(const nvj::VideoFrame& frame) {
    uint64_t sum = 1469598103934665603ull;
    for (const uint8_t byte : frame.rgba) {
        sum = (sum ^ byte) * 1099511628211ull;
    }
    return sum;
}

bool videoRequired() {
    const char* value = std::getenv("NYREXVJ_REQUIRE_VIDEO");
    return value != nullptr && *value != '\0';
}

void testVideo() {
    const nvj::VideoSupport support = nvj::videoSupport();
    CHECK(!support.folder.empty());
    if (!support.compiled) {
        CHECK(!videoRequired());
        std::printf("video decoding skipped: built without the FFmpeg headers\n");
        return;
    }
    CHECK(!support.files.empty());
    if (!support.loaded) {
        CHECK(!videoRequired());
        std::printf("video decoding skipped: FFmpeg libraries not found (%s)\n", support.problem.c_str());
        return;
    }

    nvj::VideoDecoder decoder;
    std::string error;
    CHECK(!decoder.open((kData / "does_not_exist.mp4").string(), error) && !error.empty());
    CHECK(!decoder.isOpen());
    CHECK(!decoder.open((kData / "test_alpha.png").string(), error) || decoder.info().duration >= 0.0);
    decoder.close();

    CHECK(decoder.open((kData / "test_h264.mp4").string(), error));
    CHECK(decoder.isOpen());
    const nvj::VideoInfo info = decoder.info();
    CHECK(info.width == 128 && info.height == 72);
    CHECK(info.codec == "h264");
    CHECK(info.duration > 1.9 && info.duration < 2.1);
    CHECK(info.fps > 24.0 && info.fps < 26.0);
    CHECK(!info.hasAlpha);

    std::shared_ptr<const nvj::VideoFrame> first = waitForFrame(decoder, 0.0, 0.0, 0.1);
    CHECK(first != nullptr);
    uint64_t firstSum = 0;
    if (first) {
        CHECK(first->width == 128 && first->height == 72 && first->rgba.size() == 128u * 72u * 4u);
        uint64_t light = 0;
        bool opaque = true;
        for (size_t i = 0; i + 3 < first->rgba.size(); i += 4) {
            light += first->rgba[i] + first->rgba[i + 1] + first->rgba[i + 2];
            opaque = opaque && first->rgba[i + 3] == 255;
        }
        CHECK(light > 1000);
        CHECK(opaque);
        firstSum = checksum(*first);
    }

    double last = -1.0;
    unsigned long long lastId = 0;
    bool ordered = true;
    const auto begin = std::chrono::steady_clock::now();
    double now = 0.0;
    while ((now = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()) < 0.7) {
        const std::shared_ptr<const nvj::VideoFrame> frame = decoder.frameAt(now);
        if (frame) {
            ordered = ordered && frame->time >= last && frame->id >= lastId && frame->time <= now + 0.05;
            last = frame->time;
            lastId = frame->id;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    CHECK(ordered);
    CHECK(last > 0.4);

    std::shared_ptr<const nvj::VideoFrame> middle = waitForFrame(decoder, 1.5, 1.4, 1.6);
    CHECK(middle != nullptr && middle->time >= 1.4 && middle->time <= 1.6);
    if (middle) {
        CHECK(checksum(*middle) != firstSum);
    }
    std::shared_ptr<const nvj::VideoFrame> looped = waitForFrame(decoder, 2.3, 2.2, 2.5);
    CHECK(looped != nullptr && looped->time >= 2.2 && looped->time <= 2.5);
    std::shared_ptr<const nvj::VideoFrame> back = waitForFrame(decoder, 0.2, 0.1, 0.3);
    CHECK(back != nullptr && back->time >= 0.1 && back->time <= 0.3);
    std::shared_ptr<const nvj::VideoFrame> far = waitForFrame(decoder, 41.0, 40.9, 41.2);
    CHECK(far != nullptr && far->time >= 40.9 && far->time <= 41.2);
    CHECK(!decoder.failed());
    decoder.close();
    CHECK(!decoder.isOpen());
    CHECK(decoder.frameAt(0.0) == nullptr);

    nvj::VideoDecoder hap;
    CHECK(hap.open((kData / "test_hap.mov").string(), error));
    CHECK(hap.info().codec == "hap" && hap.info().width == 64 && hap.info().height == 36);
    std::shared_ptr<const nvj::VideoFrame> hapFrame = waitForFrame(hap, 0.1, 0.0, 0.2);
    CHECK(hapFrame != nullptr);
    if (hapFrame) {
        uint64_t light = 0;
        for (size_t i = 0; i + 3 < hapFrame->rgba.size(); i += 4) {
            light += hapFrame->rgba[i] + hapFrame->rgba[i + 1] + hapFrame->rgba[i + 2];
        }
        CHECK(light > 500);
    }

    nvj::VideoDecoder alpha;
    CHECK(alpha.open((kData / "test_hap_alpha.mov").string(), error));
    CHECK(alpha.info().hasAlpha);
    std::shared_ptr<const nvj::VideoFrame> alphaFrame = waitForFrame(alpha, 0.1, 0.0, 0.2);
    CHECK(alphaFrame != nullptr);
    if (alphaFrame) {
        const size_t row = static_cast<size_t>(alphaFrame->height / 2) * static_cast<size_t>(alphaFrame->width) * 4;
        CHECK(alphaFrame->rgba[row + 4 * 4 + 3] == 255);
        CHECK(alphaFrame->rgba[row + 4 * 58 + 3] == 0);
    }

    for (int i = 0; i < 12; ++i) {
        nvj::VideoDecoder quick;
        CHECK(quick.open((kData / "test_h264.mp4").string(), error));
        if (i % 2 == 0) {
            quick.frameAt(0.0);
        }
    }
}

}

int main() {
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_media_test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", (scratch / "data").string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", (scratch / "data").string().c_str(), 1);
#endif
    testNames();
    testPremultiply();
    testImages(scratch);
    testLibrary(scratch);
    testImport(scratch);
    testVideo();
    return testing::finish();
}

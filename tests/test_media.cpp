#include "check.hpp"

#include "media/ffmpeg_setup.hpp"
#include "media/hap.hpp"
#include "media/hap_convert.hpp"
#include "media/media_image.hpp"
#include "media/playback_driver.hpp"
#include "media/video_decoder.hpp"
#include "media/video_hardware.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
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


struct Picture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

int sampleAt(const nvj::VideoPlane& plane, int x, int y, int channel) {
    const uint8_t* p = plane.data + static_cast<size_t>(y) * static_cast<size_t>(plane.stride) + static_cast<size_t>(x * plane.channels + channel) * static_cast<size_t>(plane.bytes);
    if (plane.bytes == 2) {
        return p[0] | (p[1] << 8);
    }
    return p[0];
}

uint8_t clampByte(double value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0), 0l, 255l));
}

Picture pictureFromRgba(const nvj::VideoFrame& frame) {
    Picture picture;
    picture.width = frame.planes[0].width;
    picture.height = frame.planes[0].height;
    picture.pixels.resize(static_cast<size_t>(picture.width) * static_cast<size_t>(picture.height) * 4u);
    for (int y = 0; y < picture.height; ++y) {
        std::copy_n(frame.planes[0].data + static_cast<size_t>(y) * static_cast<size_t>(frame.planes[0].stride), static_cast<size_t>(picture.width) * 4u, picture.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * static_cast<size_t>(picture.width) * 4u));
    }
    return picture;
}

Picture pictureFromYuv(const nvj::VideoFrame& frame) {
    Picture picture;
    const nvj::VideoPlane& luma = frame.planes[0];
    picture.width = luma.width;
    picture.height = luma.height;
    picture.pixels.resize(static_cast<size_t>(picture.width) * static_cast<size_t>(picture.height) * 4u);
    const int depth = frame.bitDepth;
    const double maxCode = std::pow(2.0, depth) - 1.0;
    const double k = std::pow(2.0, depth - 8);
    double kr = 0.2126;
    double kb = 0.0722;
    if (frame.matrix == 0) {
        kr = 0.299;
        kb = 0.114;
    } else if (frame.matrix == 2) {
        kr = 0.2627;
        kb = 0.0593;
    }
    const double kg = 1.0 - kr - kb;
    const bool semi = frame.layout == nvj::VideoLayout::SemiPlanar;
    const auto code = [&](const nvj::VideoPlane& plane, int x, int y, int channel) {
        int value = sampleAt(plane, x, y, channel);
        if (frame.msbAligned && plane.bytes == 2) {
            value >>= (16 - depth);
        }
        return static_cast<double>(value);
    };
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            const nvj::VideoPlane& chromaU = frame.planes[1];
            const int cx = x * chromaU.width / luma.width;
            const int cy = y * chromaU.height / luma.height;
            const double yy = code(luma, x, y, 0);
            const double cb = code(chromaU, cx, cy, 0);
            const double cr = semi ? code(chromaU, cx, cy, 1) : code(frame.planes[2], cx, cy, 0);
            double ly = 0.0;
            double ub = 0.0;
            double vr = 0.0;
            if (frame.fullRange) {
                ly = yy / maxCode;
                ub = (cb - 128.0 * k) / maxCode;
                vr = (cr - 128.0 * k) / maxCode;
            } else {
                ly = (yy - 16.0 * k) / (219.0 * k);
                ub = (cb - 128.0 * k) / (224.0 * k);
                vr = (cr - 128.0 * k) / (224.0 * k);
            }
            double r = ly + 2.0 * (1.0 - kr) * vr;
            double b = ly + 2.0 * (1.0 - kb) * ub;
            double g = ly - 2.0 * kb * (1.0 - kb) / kg * ub - 2.0 * kr * (1.0 - kr) / kg * vr;
            if (frame.matrix == 3) {
                r = ly + vr - ub;
                g = ly + ub;
                b = ly - ub - vr;
            }
            const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(picture.width) + static_cast<size_t>(x)) * 4u;
            picture.pixels[o] = clampByte(r);
            picture.pixels[o + 1] = clampByte(g);
            picture.pixels[o + 2] = clampByte(b);
            const double alpha = frame.planeCount == 4 ? code(frame.planes[3], x, y, 0) / maxCode : 1.0;
            picture.pixels[o + 3] = clampByte(alpha);
        }
    }
    return picture;
}

void expand565(uint16_t value, int out[3]) {
    out[0] = ((value >> 11) & 31) * 255 / 31;
    out[1] = ((value >> 5) & 63) * 255 / 63;
    out[2] = (value & 31) * 255 / 31;
}

Picture pictureFromCompressed(const nvj::VideoFrame& frame) {
    Picture picture;
    const nvj::VideoPlane& plane = frame.planes[0];
    const bool dxt1 = frame.compressed == nvj::CompressedFormat::Dxt1;
    const size_t blockBytes = dxt1 ? 8u : 16u;
    picture.width = frame.width;
    picture.height = frame.height;
    picture.pixels.assign(static_cast<size_t>(picture.width) * static_cast<size_t>(picture.height) * 4u, 0);
    const int blocksX = plane.width / 4;
    const int blocksY = plane.height / 4;
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const uint8_t* block = plane.data + (static_cast<size_t>(by) * static_cast<size_t>(blocksX) + static_cast<size_t>(bx)) * blockBytes;
            const uint8_t* colorBlock = block + (dxt1 ? 0 : 8);
            int alphaTable[8] = {255, 255, 255, 255, 255, 255, 255, 255};
            if (!dxt1) {
                const int a0 = block[0];
                const int a1 = block[1];
                alphaTable[0] = a0;
                alphaTable[1] = a1;
                if (a0 > a1) {
                    for (int i = 1; i < 7; ++i) {
                        alphaTable[i + 1] = ((7 - i) * a0 + i * a1) / 7;
                    }
                } else {
                    for (int i = 1; i < 5; ++i) {
                        alphaTable[i + 1] = ((5 - i) * a0 + i * a1) / 5;
                    }
                    alphaTable[6] = 0;
                    alphaTable[7] = 255;
                }
            }
            uint64_t alphaBits = 0;
            if (!dxt1) {
                for (int i = 0; i < 6; ++i) {
                    alphaBits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                }
            }
            const uint16_t c0 = static_cast<uint16_t>(colorBlock[0] | (colorBlock[1] << 8));
            const uint16_t c1 = static_cast<uint16_t>(colorBlock[2] | (colorBlock[3] << 8));
            int palette[4][3];
            expand565(c0, palette[0]);
            expand565(c1, palette[1]);
            for (int c = 0; c < 3; ++c) {
                if (c0 > c1) {
                    palette[2][c] = (2 * palette[0][c] + palette[1][c]) / 3;
                    palette[3][c] = (palette[0][c] + 2 * palette[1][c]) / 3;
                } else {
                    palette[2][c] = (palette[0][c] + palette[1][c]) / 2;
                    palette[3][c] = 0;
                }
            }
            const uint32_t indices = static_cast<uint32_t>(colorBlock[4]) | (static_cast<uint32_t>(colorBlock[5]) << 8) | (static_cast<uint32_t>(colorBlock[6]) << 16) | (static_cast<uint32_t>(colorBlock[7]) << 24);
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    const int x = bx * 4 + px;
                    const int y = by * 4 + py;
                    if (x >= picture.width || y >= picture.height) {
                        continue;
                    }
                    const int texel = py * 4 + px;
                    const int* color = palette[(indices >> (2 * texel)) & 3];
                    const int alpha = dxt1 ? 255 : alphaTable[(alphaBits >> (3 * texel)) & 7];
                    const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(picture.width) + static_cast<size_t>(x)) * 4u;
                    if (frame.compressed == nvj::CompressedFormat::YcoCgDxt5) {
                        const double scale = color[2] / 255.0 * (255.0 / 8.0) + 1.0;
                        const double co = (color[0] / 255.0 - 0.50196) / scale;
                        const double cg = (color[1] / 255.0 - 0.50196) / scale;
                        const double luma = alpha / 255.0;
                        picture.pixels[o] = clampByte(luma + co - cg);
                        picture.pixels[o + 1] = clampByte(luma + cg);
                        picture.pixels[o + 2] = clampByte(luma - co - cg);
                        picture.pixels[o + 3] = 255;
                    } else {
                        picture.pixels[o] = static_cast<uint8_t>(color[0]);
                        picture.pixels[o + 1] = static_cast<uint8_t>(color[1]);
                        picture.pixels[o + 2] = static_cast<uint8_t>(color[2]);
                        picture.pixels[o + 3] = static_cast<uint8_t>(alpha);
                    }
                }
            }
        }
    }
    return picture;
}

Picture pictureOf(const nvj::VideoFrame& frame) {
    switch (frame.layout) {
    case nvj::VideoLayout::Rgba:
        return pictureFromRgba(frame);
    case nvj::VideoLayout::Compressed:
        return pictureFromCompressed(frame);
    default:
        return pictureFromYuv(frame);
    }
}

struct Difference {
    double mean = 0.0;
    int worst = 0;
    double alphaMean = 0.0;
    bool sameSize = false;
};

Difference compare(const Picture& a, const Picture& b) {
    Difference diff;
    diff.sameSize = a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size();
    if (!diff.sameSize || a.pixels.empty()) {
        return diff;
    }
    double sum = 0.0;
    double alphaSum = 0.0;
    size_t count = 0;
    size_t alphaCount = 0;
    for (size_t i = 0; i + 3 < a.pixels.size(); i += 4) {
        alphaSum += std::abs(a.pixels[i + 3] - b.pixels[i + 3]);
        ++alphaCount;
        if (a.pixels[i + 3] < 24 && b.pixels[i + 3] < 24) {
            continue;
        }
        for (size_t c = 0; c < 3; ++c) {
            const int delta = std::abs(static_cast<int>(a.pixels[i + c]) - static_cast<int>(b.pixels[i + c]));
            sum += delta;
            diff.worst = std::max(diff.worst, delta);
            ++count;
        }
    }
    diff.mean = count == 0 ? 0.0 : sum / static_cast<double>(count);
    diff.alphaMean = alphaCount == 0 ? 0.0 : alphaSum / static_cast<double>(alphaCount);
    return diff;
}

nvj::VideoOptions legacyOptions() {
    nvj::VideoOptions options;
    options.gpuConversion = false;
    options.hapDirect = false;
    options.compressedTextures = false;
    options.mode = nvj::VideoDecodeMode::ForceSoftware;
    return options;
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
    for (const uint8_t byte : pictureOf(frame).pixels) {
        sum = (sum ^ byte) * 1099511628211ull;
    }
    return sum;
}

bool videoRequired() {
    const char* value = std::getenv("NYREXVJ_REQUIRE_VIDEO");
    return value != nullptr && *value != '\0';
}

bool videoAvailable() {
    const nvj::VideoSupport support = nvj::videoSupport();
    return support.compiled && support.loaded;
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
    CHECK(!decoder.open((kData / "does_not_exist.mp4").string(), error, legacyOptions()) && !error.empty());
    CHECK(!decoder.isOpen());
    CHECK(!decoder.open((kData / "test_alpha.png").string(), error, legacyOptions()) || decoder.info().duration >= 0.0);
    decoder.close();

    CHECK(decoder.open((kData / "test_h264.mp4").string(), error, legacyOptions()));
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
        const Picture picture = pictureOf(*first);
        CHECK(first->width == 128 && first->height == 72 && first->layout == nvj::VideoLayout::Rgba && picture.pixels.size() == 128u * 72u * 4u);
        uint64_t light = 0;
        bool opaque = true;
        for (size_t i = 0; i + 3 < picture.pixels.size(); i += 4) {
            light += picture.pixels[i] + picture.pixels[i + 1] + picture.pixels[i + 2];
            opaque = opaque && picture.pixels[i + 3] == 255;
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
    CHECK(hap.open((kData / "test_hap.mov").string(), error, legacyOptions()));
    CHECK(hap.info().codec == "hap" && hap.info().width == 64 && hap.info().height == 36);
    std::shared_ptr<const nvj::VideoFrame> hapFrame = waitForFrame(hap, 0.1, 0.0, 0.2);
    CHECK(hapFrame != nullptr);
    if (hapFrame) {
        const Picture picture = pictureOf(*hapFrame);
        uint64_t light = 0;
        for (size_t i = 0; i + 3 < picture.pixels.size(); i += 4) {
            light += picture.pixels[i] + picture.pixels[i + 1] + picture.pixels[i + 2];
        }
        CHECK(light > 500);
        CHECK(hapFrame->layout == nvj::VideoLayout::Rgba && hap.status().path == nvj::DecodePath::HapCpu);
    }

    nvj::VideoDecoder alpha;
    CHECK(alpha.open((kData / "test_hap_alpha.mov").string(), error, legacyOptions()));
    CHECK(alpha.info().hasAlpha);
    std::shared_ptr<const nvj::VideoFrame> alphaFrame = waitForFrame(alpha, 0.1, 0.0, 0.2);
    CHECK(alphaFrame != nullptr);
    if (alphaFrame) {
        const Picture picture = pictureOf(*alphaFrame);
        const size_t row = static_cast<size_t>(alphaFrame->height / 2) * static_cast<size_t>(alphaFrame->width) * 4;
        CHECK(picture.pixels[row + 4 * 4 + 3] == 255);
        CHECK(picture.pixels[row + 4 * 58 + 3] == 0);
    }

    for (int i = 0; i < 12; ++i) {
        nvj::VideoDecoder quick;
        CHECK(quick.open((kData / "test_h264.mp4").string(), error, legacyOptions()));
        if (i % 2 == 0) {
            quick.frameAt(0.0);
        }
    }
}


std::vector<uint8_t> hapSection(uint8_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    out.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((payload.size() >> 16) & 0xFF));
    out.push_back(type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void testHapParser() {
    CHECK(nvj::hapCodedSize(36) == 36 && nvj::hapCodedSize(37) == 40 && nvj::hapCodedSize(1080) == 1080 && nvj::hapCodedSize(2) == 4);
    CHECK(nvj::hapTextureBytes(nvj::CompressedFormat::Dxt1, 64, 36) == 64u * 36u / 2u);
    CHECK(nvj::hapTextureBytes(nvj::CompressedFormat::Dxt5, 64, 36) == 64u * 36u);
    CHECK(nvj::hapTextureBytes(nvj::CompressedFormat::YcoCgDxt5, 3840, 2160) == 3840u * 2160u);
    CHECK(std::string(nvj::hapVariantName(nvj::CompressedFormat::Dxt1)) == "HAP");
    CHECK(std::string(nvj::hapVariantName(nvj::CompressedFormat::Dxt5)) == "HAP Alpha");
    CHECK(std::string(nvj::hapVariantName(nvj::CompressedFormat::YcoCgDxt5)) == "HAP Q");
    nvj::CompressedFormat format = nvj::CompressedFormat::None;
    CHECK(nvj::hapFormatFromFourcc('H' | ('a' << 8) | ('p' << 16) | ('1' << 24), format) && format == nvj::CompressedFormat::Dxt1);
    CHECK(nvj::hapFormatFromFourcc('H' | ('a' << 8) | ('p' << 16) | ('5' << 24), format) && format == nvj::CompressedFormat::Dxt5);
    CHECK(nvj::hapFormatFromFourcc('H' | ('a' << 8) | ('p' << 16) | ('Y' << 24), format) && format == nvj::CompressedFormat::YcoCgDxt5);
    CHECK(!nvj::hapFormatFromFourcc('a' | ('v' << 8) | ('c' << 16) | ('1' << 24), format));

    const std::vector<uint8_t> block = {0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<uint8_t> output(8, 0xFF);
    nvj::HapFrameInfo info;

    const std::vector<uint8_t> plain = hapSection(0xAB, block);
    CHECK(nvj::hapInspect(plain.data(), plain.size(), info) == nvj::HapResult::Ok && info.format == nvj::CompressedFormat::Dxt1);
    CHECK(nvj::hapDecode(plain.data(), plain.size(), 4, 4, output.data(), output.size(), info) == nvj::HapResult::Ok && output == block);

    std::vector<uint8_t> snappyPayload = {0x08, 0x1C};
    snappyPayload.insert(snappyPayload.end(), block.begin(), block.end());
    const std::vector<uint8_t> snappy = hapSection(0xBB, snappyPayload);
    std::fill(output.begin(), output.end(), 0xFF);
    CHECK(nvj::hapDecode(snappy.data(), snappy.size(), 4, 4, output.data(), output.size(), info) == nvj::HapResult::Ok && output == block);

    std::vector<uint8_t> copyPayload = {0x10, 0x1C};
    copyPayload.insert(copyPayload.end(), block.begin(), block.end());
    copyPayload.push_back(0x1E);
    copyPayload.push_back(0x08);
    copyPayload.push_back(0x00);
    const std::vector<uint8_t> copying = hapSection(0xBE, copyPayload);
    std::vector<uint8_t> big(16, 0);
    CHECK(nvj::hapDecode(copying.data(), copying.size(), 4, 4, big.data(), big.size(), info) == nvj::HapResult::Ok && info.format == nvj::CompressedFormat::Dxt5);
    CHECK(std::equal(block.begin(), block.end(), big.begin()) && std::equal(block.begin(), block.end(), big.begin() + 8));

    size_t length = 0;
    CHECK(nvj::snappyUncompressedLength(snappyPayload.data(), snappyPayload.size(), length) && length == 8);
    std::vector<uint8_t> broken = snappyPayload;
    broken.pop_back();
    size_t written = 0;
    std::vector<uint8_t> scratch(8, 0);
    CHECK(!nvj::snappyDecompress(broken.data(), broken.size(), scratch.data(), scratch.size(), written));
    CHECK(!nvj::snappyDecompress(snappyPayload.data(), snappyPayload.size(), scratch.data(), 4, written));

    std::vector<uint8_t> truncated = plain;
    truncated.pop_back();
    CHECK(nvj::hapInspect(truncated.data(), truncated.size(), info) == nvj::HapResult::Corrupt);
    CHECK(nvj::hapDecode(truncated.data(), truncated.size(), 4, 4, output.data(), output.size(), info) == nvj::HapResult::Corrupt);
    CHECK(nvj::hapInspect(plain.data(), 2, info) == nvj::HapResult::Corrupt);
    const std::vector<uint8_t> unknown = hapSection(0xA7, block);
    CHECK(nvj::hapInspect(unknown.data(), unknown.size(), info) == nvj::HapResult::Unsupported);
    const std::vector<uint8_t> otherCompressor = hapSection(0x5B, block);
    CHECK(nvj::hapInspect(otherCompressor.data(), otherCompressor.size(), info) == nvj::HapResult::Unsupported);
    const std::vector<uint8_t> wrongSize = hapSection(0xAB, std::vector<uint8_t>(16, 0));
    CHECK(nvj::hapDecode(wrongSize.data(), wrongSize.size(), 4, 4, output.data(), output.size(), info) == nvj::HapResult::Corrupt);
    std::vector<uint8_t> tooSmall(4, 0);
    CHECK(nvj::hapDecode(plain.data(), plain.size(), 4, 4, tooSmall.data(), tooSmall.size(), info) == nvj::HapResult::Corrupt);
}

void testModeNames() {
    CHECK(std::string(nvj::videoDecodeModeName(nvj::VideoDecodeMode::Auto)) == "auto");
    CHECK(std::string(nvj::videoDecodeModeName(nvj::VideoDecodeMode::PreferHardware)) == "hardware");
    CHECK(std::string(nvj::videoDecodeModeName(nvj::VideoDecodeMode::ForceSoftware)) == "software");
    CHECK(nvj::videoDecodeModeFromName("hardware") == nvj::VideoDecodeMode::PreferHardware);
    CHECK(nvj::videoDecodeModeFromName("software") == nvj::VideoDecodeMode::ForceSoftware);
    CHECK(nvj::videoDecodeModeFromName("auto") == nvj::VideoDecodeMode::Auto);
    CHECK(nvj::videoDecodeModeFromName("nonsense") == nvj::VideoDecodeMode::Auto);
    CHECK(nvj::videoDecodeModeFromName("") == nvj::VideoDecodeMode::Auto);
    for (const nvj::DecodePath path : {nvj::DecodePath::None, nvj::DecodePath::Software, nvj::DecodePath::Hardware, nvj::DecodePath::HapGpu, nvj::DecodePath::HapCpu}) {
        CHECK(std::string(nvj::decodePathName(path)).size() > 0);
    }
}

bool waitForSameFrame(nvj::VideoDecoder& a, nvj::VideoDecoder& b, double time, std::shared_ptr<const nvj::VideoFrame>& first, std::shared_ptr<const nvj::VideoFrame>& second) {
    first = waitForFrame(a, time + 0.0005, time - 0.001, time + 0.001);
    second = waitForFrame(b, time + 0.0005, time - 0.001, time + 0.001);
    return first && second && std::abs(first->time - time) < 0.001 && std::abs(second->time - time) < 0.001;
}

struct PathCase {
    const char* file;
    nvj::CompressedFormat format;
    const char* variant;
    bool alpha;
};

void testHapDirect() {
    const PathCase cases[] = {
        {"test_hap.mov", nvj::CompressedFormat::Dxt1, "HAP", false},
        {"test_hap_alpha.mov", nvj::CompressedFormat::Dxt5, "HAP Alpha", true},
        {"test_hap_q.mov", nvj::CompressedFormat::YcoCgDxt5, "HAP Q", false},
        {"test_hap_plain.mov", nvj::CompressedFormat::Dxt1, "HAP", false},
        {"test_hap_chunks.mov", nvj::CompressedFormat::Dxt5, "HAP Alpha", true},
    };
    for (const PathCase& test : cases) {
        std::string error;
        nvj::VideoDecoder direct;
        nvj::VideoDecoder cpu;
        CHECK(direct.open((kData / test.file).string(), error, nvj::VideoOptions()));
        CHECK(cpu.open((kData / test.file).string(), error, legacyOptions()));
        CHECK(direct.info().width == 64 && direct.info().height == 36 && direct.info().hasAlpha == test.alpha);
        for (const double time : {0.0, 0.28}) {
            std::shared_ptr<const nvj::VideoFrame> a;
            std::shared_ptr<const nvj::VideoFrame> b;
            const bool both = waitForSameFrame(direct, cpu, time, a, b);
            CHECK(both);
            if (!both) {
                continue;
            }
            CHECK(a->layout == nvj::VideoLayout::Compressed && a->compressed == test.format);
            CHECK(a->width == 64 && a->height == 36 && a->planes[0].width == 64 && a->planes[0].height == 36);
            CHECK(b->layout == nvj::VideoLayout::Rgba);
            const Difference diff = compare(pictureOf(*a), pictureOf(*b));
            CHECK(diff.sameSize);
            CHECK(diff.mean < 3.0);
            CHECK(diff.worst < 40);
            CHECK(diff.alphaMean < 2.0);
        }
        const nvj::VideoStatus status = direct.status();
        CHECK(status.path == nvj::DecodePath::HapGpu);
        CHECK(status.variant == test.variant);
        CHECK(status.decoder.find("HAP") != std::string::npos);
        CHECK(status.fallbackReason.empty());
        CHECK(cpu.status().path == nvj::DecodePath::HapCpu && !cpu.status().fallbackReason.empty());
    }

    nvj::VideoOptions noTextures;
    noTextures.compressedTextures = false;
    nvj::VideoDecoder fallback;
    std::string error;
    CHECK(fallback.open((kData / "test_hap.mov").string(), error, noTextures));
    const std::shared_ptr<const nvj::VideoFrame> frame = waitForFrame(fallback, 0.0, 0.0, 0.01);
    CHECK(frame != nullptr && frame->layout == nvj::VideoLayout::Rgba);
    CHECK(fallback.status().path == nvj::DecodePath::HapCpu && !fallback.status().fallbackReason.empty());

    nvj::VideoDecoder seeking;
    CHECK(seeking.open((kData / "test_hap_q.mov").string(), error, nvj::VideoOptions()));
    CHECK(waitForFrame(seeking, 0.0, 0.0, 0.01) != nullptr);
    const std::shared_ptr<const nvj::VideoFrame> middle = waitForFrame(seeking, 0.36, 0.34, 0.38);
    CHECK(middle != nullptr && middle->layout == nvj::VideoLayout::Compressed);
    const std::shared_ptr<const nvj::VideoFrame> wrapped = waitForFrame(seeking, 0.9, 0.28, 0.34);
    CHECK(wrapped != nullptr);
    const std::shared_ptr<const nvj::VideoFrame> back = waitForFrame(seeking, 0.08, 0.06, 0.10);
    CHECK(back != nullptr && back->time >= 0.06 && back->time <= 0.10);
    const std::shared_ptr<const nvj::VideoFrame> far = waitForFrame(seeking, 31.2, 31.1, 31.3);
    CHECK(far != nullptr && far->time >= 31.1 && far->time <= 31.3);
    CHECK(!seeking.failed());
    CHECK(seeking.status().dropped + seeking.status().underruns >= 0u);
}

void testGpuLayouts() {
    struct Case {
        const char* file;
        nvj::VideoLayout layout;
        int planes;
        int depth;
        bool alpha;
        double tolerance;
        const char* transfer = "";
        int matrix = -1;
        bool fullRange = false;
    };
    const Case cases[] = {
        {"test_h264.mp4", nvj::VideoLayout::Planar, 3, 8, false, 6.0},
        {"test_hevc10.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0, "", 0, false},
        {"test_prores_alpha.mov", nvj::VideoLayout::Planar, 4, 12, true, 6.0},
        {"test_ycocg.mkv", nvj::VideoLayout::Planar, 3, 8, false, 2.0},
        {"test_cs_601_limited.mp4", nvj::VideoLayout::Planar, 3, 8, false, 6.0, "", 0, false},
        {"test_cs_709_full.mp4", nvj::VideoLayout::Planar, 3, 8, false, 6.0, "", 1, true},
        {"test_cs_2020_limited10.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0, "", 2, false},
        {"test_cs_2020_full10.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0, "", 2, true},
        {"test_hdr_pq.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0, "PQ", 2, false},
        {"test_hdr_hlg.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0, "HLG", 2, false},
        {"test_h264_422_10.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0, "", 1, false},
        {"test_h264_444.mp4", nvj::VideoLayout::Planar, 3, 8, false, 6.0, "", 1, false},
        {"test_cs_2020_444_10.mp4", nvj::VideoLayout::Planar, 3, 10, false, 1.0, "", 2, false},
    };
    for (const Case& test : cases) {
        std::string error;
        nvj::VideoDecoder gpu;
        nvj::VideoDecoder cpu;
        CHECK(gpu.open((kData / test.file).string(), error, nvj::VideoOptions()));
        CHECK(cpu.open((kData / test.file).string(), error, legacyOptions()));
        for (const double time : {0.0, 0.2}) {
            std::shared_ptr<const nvj::VideoFrame> a;
            std::shared_ptr<const nvj::VideoFrame> b;
            const bool both = waitForSameFrame(gpu, cpu, time, a, b);
            if (!both) {
                std::fprintf(stderr, "no matching frame in %s at %.2f s (gpu %.4f, cpu %.4f)\n", test.file, time, a ? a->time : -1.0, b ? b->time : -1.0);
            }
            CHECK(both);
            if (!both) {
                continue;
            }
            CHECK(a->layout == test.layout && a->planeCount == test.planes && a->bitDepth == test.depth);
            if (test.matrix >= 0) {
                CHECK(a->matrix == test.matrix);
                CHECK(a->fullRange == test.fullRange);
            }
            CHECK(a->outWidth > 0 && a->outHeight > 0 && a->planes[0].width == a->width && a->planes[0].height == a->height);
            CHECK(b->layout == nvj::VideoLayout::Rgba);
            const Difference diff = compare(pictureOf(*a), pictureOf(*b));
            CHECK(diff.sameSize);
            CHECK(diff.mean < test.tolerance);
            CHECK(diff.alphaMean < 2.0);
            if (test.matrix >= 0) {
                nvj::VideoFrame wrong = *a;
                wrong.matrix = (a->matrix + 1) % 3;
                const Difference other = compare(pictureOf(wrong), pictureOf(*b));
                std::printf("%s: matrix %d mean %.3f, other matrix %.3f\n", test.file, a->matrix, diff.mean, other.mean);
                CHECK(other.mean > diff.mean * 1.5);
            }
            if (test.alpha) {
                const Picture picture = pictureOf(*a);
                const size_t row = static_cast<size_t>(picture.height / 2) * static_cast<size_t>(picture.width) * 4u;
                CHECK(picture.pixels[row + 4u * 8u + 3u] > 240);
                CHECK(picture.pixels[row + 4u * static_cast<size_t>(picture.width - 8) + 3u] < 16);
            }
        }
        const nvj::VideoStatus status = gpu.status();
        CHECK(status.path == nvj::DecodePath::Software);
        CHECK(!status.decoder.empty() && !status.pixelFormat.empty());
        CHECK(status.threads >= 1 && status.decoded > 0);
        CHECK(status.decodeMs >= 0.0);
        CHECK(status.fallbackReason.empty());
        CHECK(status.transfer == test.transfer);
        CHECK(status.firstPictureMs > 0.0 && status.firstPictureMs < 5000.0);
    }
}

void testTransferNames() {
    CHECK(std::string(nvj::videoTransferName(16)) == "PQ");
    CHECK(std::string(nvj::videoTransferName(18)) == "HLG");
    for (const int code : {0, 1, 2, 5, 6, 8, 13, 17, 15, 19, 99, -1}) {
        CHECK(std::string(nvj::videoTransferName(code)).empty());
    }
}

void testSoftwareOnlyCodecs() {
    CHECK(nvj::softwareOnlyCodecLabel("dxv") == "DXV");
    CHECK(nvj::softwareOnlyCodecLabel("prores") == "ProRes");
    CHECK(nvj::softwareOnlyCodecLabel("dnxhd") == "DNxHD/DNxHR");
    CHECK(nvj::softwareOnlyCodecLabel("cfhd") == "CineForm");
    for (const char* name : {"h264", "hevc", "av1", "vp9", "hap", "mjpeg", "mpeg2video", ""}) {
        CHECK(nvj::softwareOnlyCodecLabel(name).empty());
        CHECK(nvj::softwareOnlyReason(name).empty());
    }
    CHECK(nvj::softwareOnlyReason("dxv").find("DXV") == 0);
    CHECK(nvj::softwareOnlyReason("dxv").find("not supported by") == std::string::npos);
    CHECK(!nvj::softwareOnlyReason("prores").empty() && nvj::softwareOnlyReason("prores") != nvj::softwareOnlyReason("dxv"));
    std::string error;
    {
        nvj::VideoOptions options;
        options.mode = nvj::VideoDecodeMode::PreferHardware;
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_prores_alpha.mov").string(), error, options));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.1) != nullptr);
        const nvj::VideoStatus status = decoder.status();
        CHECK(status.codec == "prores" && status.softwareOnly && !status.hardwareWanted);
        CHECK(status.path == nvj::DecodePath::Software && status.backend == "software");
        CHECK(status.fallbackReason == nvj::softwareOnlyReason("prores") && status.fallbackDetail.empty());
        CHECK(!decoder.failed());
    }
    {
        nvj::VideoOptions options;
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_prores_alpha.mov").string(), error, options));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.1) != nullptr);
        const nvj::VideoStatus status = decoder.status();
        CHECK(status.softwareOnly && !status.hardwareWanted && status.fallbackReason.empty());
    }
    {
        nvj::VideoOptions options;
        options.mode = nvj::VideoDecodeMode::PreferHardware;
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_h264.mp4").string(), error, options));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.01) != nullptr);
        const nvj::VideoStatus status = decoder.status();
        CHECK(!status.softwareOnly && status.hardwareWanted);
        CHECK(status.fallbackReason != nvj::softwareOnlyReason("prores"));
    }
}

void testYcocg() {
    std::string error;
    nvj::VideoDecoder gpu;
    CHECK(gpu.open((kData / "test_ycocg.mkv").string(), error, nvj::VideoOptions()));
    const std::shared_ptr<const nvj::VideoFrame> planar = waitForFrame(gpu, 0.0, 0.0, 0.3);
    CHECK(planar != nullptr);
    if (planar) {
        CHECK(planar->layout == nvj::VideoLayout::Planar && planar->planeCount == 3 && planar->bitDepth == 8);
        CHECK(planar->matrix == 3 && planar->fullRange);
        const Picture reference = pictureOf(*planar);
        const size_t middle = (static_cast<size_t>(reference.height / 2) * static_cast<size_t>(reference.width) + static_cast<size_t>(reference.width / 2)) * 4u;
        CHECK(std::abs(reference.pixels[middle] - 188) <= 1);
        CHECK(std::abs(reference.pixels[middle + 1] - 100) <= 1);
        CHECK(std::abs(reference.pixels[middle + 2] - 124) <= 1);
    }
    nvj::VideoDecoder cpu;
    CHECK(cpu.open((kData / "test_ycocg.mkv").string(), error, legacyOptions()));
    const std::shared_ptr<const nvj::VideoFrame> rgba = waitForFrame(cpu, 0.0, 0.0, 0.3);
    CHECK(rgba != nullptr);
    if (rgba) {
        CHECK(rgba->layout == nvj::VideoLayout::Rgba);
        const Picture picture = pictureOf(*rgba);
        const size_t middle = (static_cast<size_t>(picture.height / 2) * static_cast<size_t>(picture.width) + static_cast<size_t>(picture.width / 2)) * 4u;
        CHECK(std::abs(picture.pixels[middle] - 188) <= 1);
        CHECK(std::abs(picture.pixels[middle + 1] - 100) <= 1);
        CHECK(std::abs(picture.pixels[middle + 2] - 124) <= 1);
        CHECK(picture.pixels[middle + 3] == 255);
    }
}

void testDecodeModes() {
    std::string error;
    {
        nvj::VideoOptions options;
        options.mode = nvj::VideoDecodeMode::ForceSoftware;
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_h264.mp4").string(), error, options));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.01) != nullptr);
        const nvj::VideoStatus status = decoder.status();
        CHECK(status.path == nvj::DecodePath::Software && status.backend == "software" && !status.hardwareWanted);
        CHECK(status.codec == "h264");
    }
    {
        nvj::VideoOptions options;
        options.mode = nvj::VideoDecodeMode::Auto;
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_h264.mp4").string(), error, options));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.01) != nullptr);
        const nvj::VideoStatus status = decoder.status();
        CHECK(status.path == nvj::DecodePath::Software && !status.hardwareWanted && status.fallbackReason.empty());
    }
    {
        nvj::VideoOptions options;
        options.mode = nvj::VideoDecodeMode::PreferHardware;
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_h264.mp4").string(), error, options));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.01) != nullptr);
        const nvj::VideoStatus status = decoder.status();
        CHECK(status.hardwareWanted);
        if (status.path == nvj::DecodePath::Hardware) {
            CHECK(!status.backend.empty());
        } else {
            CHECK(status.path == nvj::DecodePath::Software && !status.fallbackReason.empty() && !status.fallbackDetail.empty());
        }
        CHECK(!decoder.failed());
    }
}

bool onlyLibvaMissing(const char* file) {
    return std::string(file) != "libva-drm.so.2";
}

bool nothingLoads(const char*) {
    return false;
}

bool everythingLoads(const char*) {
    return true;
}

void testHardwareLibraryGuard() {
    CHECK(nvj::hardwareBackendLibraries("cuda").empty());
    CHECK(nvj::hardwareBackendLibraries("nonsense").empty());
#ifndef _WIN32
    CHECK(!nvj::hardwareBackendLibraries("vaapi").empty());
    CHECK(!nvj::hardwareBackendLibraries("vdpau").empty());
    CHECK(!nvj::hardwareBackendLibraries("qsv").empty());
    CHECK(nvj::missingHardwareLibrary("vaapi", onlyLibvaMissing) == "libva-drm.so.2");
    CHECK(nvj::missingHardwareLibrary("qsv", onlyLibvaMissing) == "libva-drm.so.2");
    CHECK(nvj::missingHardwareLibrary("vdpau", onlyLibvaMissing).empty());
    CHECK(nvj::missingHardwareLibrary("vaapi", nothingLoads) == "libva.so.2");
    CHECK(nvj::missingHardwareLibrary("vdpau", nothingLoads) == "libvdpau.so.1");
#endif
    CHECK(nvj::missingHardwareLibrary("cuda", nothingLoads).empty());
    CHECK(nvj::missingHardwareLibrary("vaapi", everythingLoads).empty());
    CHECK(nvj::missingHardwareLibrary("software").empty());
}

void testHardwareProbe() {
    const nvj::HardwareProbeReport report = nvj::probeHardwareDecoding();
    CHECK(report.ffmpegAvailable);
    CHECK(report.rows.size() >= 2);
    CHECK(std::string(nvj::probeCodecLabel(0)) == "H.264" && std::string(nvj::probeCodecLabel(1)) == "HEVC" && std::string(nvj::probeCodecLabel(2)) == "AV1");
    CHECK(!nvj::hardwareBackendOrder().empty());
    for (const std::string& backend : nvj::hardwareBackendOrder()) {
        CHECK(!nvj::hardwareBackendLabel(backend).empty());
    }
    if (!report.rows.empty()) {
        CHECK(report.rows.front().backend == "software");
        CHECK(report.rows.front().codecs[0].state == nvj::ProbeState::Supported);
        CHECK(report.rows.front().codecs[1].state == nvj::ProbeState::Supported);
    }
    for (size_t i = 1; i < report.rows.size(); ++i) {
        for (const nvj::HardwareProbeCell& cell : report.rows[i].codecs) {
            CHECK(cell.state != nvj::ProbeState::Untested);
            if (!report.rows[i].deviceFound) {
                CHECK(cell.state != nvj::ProbeState::Supported);
            }
        }
    }
}

void testLoopContinuity() {
    std::string error;
    for (const bool legacy : {false, true}) {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / "test_prores_alpha.mov").string(), error, legacy ? legacyOptions() : nvj::VideoOptions()));
        const double period = 0.04;
        int found = 0;
        for (int k = 0; k < 28; ++k) {
            const double expected = k * period;
            const std::shared_ptr<const nvj::VideoFrame> frame = waitForFrame(decoder, expected + 0.0005, expected - 0.001, expected + 0.001);
            if (frame && std::abs(frame->time - expected) < 0.001) {
                ++found;
            } else {
                std::fprintf(stderr, "missing frame %d (%s): got %.4f\n", k, legacy ? "cpu" : "gpu", frame ? frame->time : -1.0);
                break;
            }
        }
        CHECK(found == 28);
        CHECK(decoder.status().dropped == 0);
    }
}

int indexOfFrame(const nvj::VideoFrame& frame) {
    if (frame.planeCount < 1 || frame.planes[0].data == nullptr) {
        return -1;
    }
    const nvj::VideoPlane& plane = frame.planes[0];
    const int centerX = plane.width / 2;
    const int centerY = plane.height / 2;
    const int step = frame.layout == nvj::VideoLayout::Rgba ? 4 : 1;
    double sum = 0.0;
    int count = 0;
    for (int y = centerY - 4; y < centerY + 4; ++y) {
        for (int x = centerX - 4; x < centerX + 4; ++x) {
            sum += plane.data[static_cast<size_t>(y) * static_cast<size_t>(plane.stride) + static_cast<size_t>(x * step)];
            ++count;
        }
    }
    const double level = sum / count;
    const bool limited = frame.layout != nvj::VideoLayout::Rgba && !frame.fullRange;
    const double luma = limited ? level : level * 219.0 / 255.0 + 16.0;
    return static_cast<int>(std::lround((luma - 40.0) / 3.0));
}

bool reachIndex(nvj::VideoDecoder& decoder, double clock, int expectedIndex, double expectedTime, double slack = 0.0015) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 5.0) {
        const std::shared_ptr<const nvj::VideoFrame> frame = decoder.frameAt(clock);
        if (frame && std::abs(frame->time - expectedTime) < slack) {
            if (indexOfFrame(*frame) == expectedIndex) {
                return true;
            }
            std::fprintf(stderr, "index mismatch at %.4f: expected %d got %d\n", clock, expectedIndex, indexOfFrame(*frame));
            return false;
        }
        if (decoder.failed()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    std::fprintf(stderr, "no frame for clock %.4f (index %d, time %.4f)\n", clock, expectedIndex, expectedTime);
    return false;
}

void testExactSeek() {
    std::string error;
    for (const char* file : {"test_index_mjpeg.avi", "test_index_h264.mp4"}) {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / file).string(), error, nvj::VideoOptions()));
        CHECK(decoder.info().duration > 2.3 && decoder.info().duration < 2.5);
        const double targets[] = {1.0, 0.0, 2.3, 1.2, 1.18, 0.5, 2.36, 0.04, 1.95, 1.2, 1.16, 1.2, 0.08};
        for (const double target : targets) {
            const int expected = static_cast<int>(std::floor(target * 25.0 + 1.0e-6));
            CHECK(reachIndex(decoder, target + 0.0005, expected, expected * 0.04));
        }
        CHECK(decoder.status().seeks > 5);
        CHECK(!decoder.failed());
        for (int k = 0; k < 24; ++k) {
            const double clock = (12 + k) * 0.04;
            CHECK(reachIndex(decoder, clock + 0.0005, 12 + k, clock));
        }
    }
}

void testRangeLoop() {
    std::string error;
    struct Case {
        const char* file;
        double in;
        double out;
        int first;
        int count;
    };
    const Case cases[] = {{"test_index_mjpeg.avi", 0.4, 1.2, 10, 20}, {"test_index_h264.mp4", 0.48, 1.48, 12, 25}, {"test_index_h264.mp4", 1.0, 2.0, 25, 25}};
    for (const Case& c : cases) {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / c.file).string(), error, nvj::VideoOptions()));
        decoder.setRange(c.in, c.out);
        CHECK(std::abs(decoder.status().rangeIn - c.in) < 1.0e-6);
        CHECK(std::abs(decoder.status().rangeOut - c.out) < 1.0e-6);
        int found = 0;
        const int total = c.count * 3;
        for (int k = 0; k < total; ++k) {
            const double clock = k * 0.04;
            if (!reachIndex(decoder, clock + 0.0005, c.first + k % c.count, clock)) {
                std::fprintf(stderr, "range loop %s failed at step %d\n", c.file, k);
                break;
            }
            ++found;
        }
        CHECK(found == total);
        decoder.setRange(0.0, 0.0);
        CHECK(decoder.status().rangeOut > 2.3);
        CHECK(reachIndex(decoder, 0.0005, 0, 0.0));
        CHECK(reachIndex(decoder, 2.3, 57, 57 * 0.04));
        CHECK(!decoder.failed());
    }
}

void testReversePlayback() {
    std::string error;
    for (const char* file : {"test_index_mjpeg.avi", "test_index_h264.mp4"}) {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open((kData / file).string(), error, nvj::VideoOptions()));
        CHECK(!decoder.reversing());
        CHECK(reachIndex(decoder, 0.0005, 0, 0.0));
        decoder.setDirection(true);
        CHECK(decoder.reversing());
        int found = 0;
        double lastTime = 1.0e9;
        bool descending = true;
        for (int k = 59; k >= 0; --k) {
            const double clock = k * 0.04;
            if (!reachIndex(decoder, clock + 0.0005, k, clock)) {
                std::fprintf(stderr, "reverse %s failed at index %d\n", file, k);
                break;
            }
            const std::shared_ptr<const nvj::VideoFrame> frame = decoder.frameAt(clock + 0.0005);
            descending = descending && frame && frame->time < lastTime;
            lastTime = frame ? frame->time : lastTime;
            ++found;
        }
        CHECK(found == 60);
        CHECK(descending);
        const nvj::VideoStatus reverse = decoder.status();
        CHECK(reverse.reverse);
        CHECK(reverse.reverseFrames >= 6 && reverse.reverseFrames <= 16);
        decoder.setDirection(false);
        CHECK(!decoder.reversing());
        for (int k = 12; k < 40; ++k) {
            const double clock = k * 0.04;
            CHECK(reachIndex(decoder, clock + 0.0005, k, clock));
        }
        decoder.setDirection(true);
        for (int k = 39; k >= 20; --k) {
            const double clock = k * 0.04;
            CHECK(reachIndex(decoder, clock + 0.0005, k, clock));
        }
        CHECK(!decoder.failed());
        decoder.setRange(0.48, 1.48);
        for (int k = 24; k >= 0; --k) {
            const double clock = k * 0.04;
            CHECK(reachIndex(decoder, clock + 0.0005, 12 + k, clock));
        }
        decoder.setDirection(false);
        for (int k = 0; k < 25; ++k) {
            const double clock = k * 0.04;
            CHECK(reachIndex(decoder, clock + 0.0005, 12 + k, clock));
        }
        CHECK(!decoder.failed());
    }
}

void testReverseHap() {
    std::string error;
    nvj::VideoDecoder decoder;
    CHECK(decoder.open((kData / "test_hap.mov").string(), error, nvj::VideoOptions()));
    const double fps = decoder.info().fps;
    CHECK(fps > 1.0);
    const int last = std::max(2, std::min(20, static_cast<int>(decoder.info().duration * fps) - 2));
    CHECK(waitForFrame(decoder, 0.0005, 0.0, 0.002) != nullptr);
    decoder.setDirection(true);
    int found = 0;
    for (int k = last; k >= 0; --k) {
        const double clock = k / fps;
        const std::shared_ptr<const nvj::VideoFrame> frame = waitForFrame(decoder, clock + 0.0005, clock - 0.002, clock + 0.002);
        if (!frame || std::abs(frame->time - clock) > 0.002) {
            break;
        }
        ++found;
    }
    CHECK(found == last + 1);
    CHECK(!decoder.failed());
}

int expectedIndex(const nvj::Playhead& head) {
    return static_cast<int>(std::floor(head.position() * 25.0 + 1.0e-6));
}

struct Transport {
    nvj::VideoDecoder decoder;
    nvj::PlaybackDriver driver;
    double seconds = 0.0;
    bool visible = true;

    bool open(const char* file, const nvj::ClipPlayback& playback) {
        std::string error;
        driver.setPlayback(playback);
        return decoder.open((kData / file).string(), error, nvj::VideoOptions());
    }

    bool step(double clock, double beat, bool wait = true) {
        visible = driver.drive(decoder, decoder.info(), clock, beat, seconds);
        if (!visible || !wait) {
            return true;
        }
        const int expected = expectedIndex(driver.head());
        const double offset = seconds - (driver.head().position() - driver.head().in());
        return reachIndex(decoder, seconds, expected, expected * 0.04 - driver.head().in() + offset);
    }
};

void testTransportModes() {
    for (const char* file : {"test_index_mjpeg.avi", "test_index_h264.mp4"}) {
        {
            Transport t;
            CHECK(t.open(file, nvj::ClipPlayback{}));
            int good = 0;
            for (int k = 0; k < 130; ++k) {
                if (!t.step(0.01 + k * 0.04, 0.0)) {
                    std::fprintf(stderr, "default loop %s failed at step %d\n", file, k);
                    break;
                }
                ++good;
            }
            CHECK(good == 130);
        }
        {
            nvj::ClipPlayback playback;
            playback.in = 0.4;
            playback.out = 1.2;
            playback.speed = 2.0f;
            Transport t;
            CHECK(t.open(file, playback));
            int good = 0;
            for (int k = 0; k < 60; ++k) {
                if (!t.step(0.01 + k * 0.04, 0.0)) {
                    std::fprintf(stderr, "range speed 2 %s failed at step %d\n", file, k);
                    break;
                }
                ++good;
            }
            CHECK(good == 60);
        }
        {
            nvj::ClipPlayback playback;
            playback.in = 0.4;
            playback.out = 1.2;
            playback.loop = nvj::LoopMode::PingPong;
            Transport t;
            CHECK(t.open(file, playback));
            int good = 0;
            bool sawReverse = false;
            for (int k = 0; k < 90; ++k) {
                if (!t.step(0.01 + k * 0.04, 0.0)) {
                    std::fprintf(stderr, "pingpong %s failed at step %d\n", file, k);
                    break;
                }
                sawReverse = sawReverse || t.driver.head().reverse();
                ++good;
            }
            CHECK(good == 90);
            CHECK(sawReverse);
        }
        {
            nvj::ClipPlayback playback;
            playback.in = 0.4;
            playback.out = 1.2;
            playback.loop = nvj::LoopMode::OnceHold;
            Transport t;
            CHECK(t.open(file, playback));
            int good = 0;
            for (int k = 0; k < 40; ++k) {
                if (!t.step(0.01 + k * 0.04, 0.0)) {
                    break;
                }
                ++good;
            }
            CHECK(good == 40);
            CHECK(t.driver.head().finished() && t.visible);
            CHECK(expectedIndex(t.driver.head()) == 29);
        }
        {
            nvj::ClipPlayback playback;
            playback.in = 0.4;
            playback.out = 1.2;
            playback.loop = nvj::LoopMode::OnceStop;
            Transport t;
            CHECK(t.open(file, playback));
            bool vanished = false;
            for (int k = 0; k < 40; ++k) {
                t.step(0.01 + k * 0.04, 0.0, false);
                vanished = vanished || !t.visible;
                if (k < 15) {
                    CHECK(t.visible);
                }
            }
            CHECK(vanished && !t.visible);
            t.step(0.0, 0.0, false);
            CHECK(t.visible && !t.driver.head().finished());
        }
        {
            nvj::ClipPlayback playback;
            playback.speed = -1.0f;
            Transport t;
            CHECK(t.open(file, playback));
            int good = 0;
            for (int k = 0; k < 90; ++k) {
                if (!t.step(0.01 + k * 0.04, 0.0)) {
                    std::fprintf(stderr, "reverse loop %s failed at step %d\n", file, k);
                    break;
                }
                ++good;
            }
            CHECK(good == 90);
            CHECK(t.decoder.status().reverse || t.driver.head().position() > 0.0);
        }
        {
            nvj::ClipPlayback playback;
            playback.in = 0.4;
            playback.out = 1.2;
            playback.syncBeats = 4.0f;
            Transport t;
            CHECK(t.open(file, playback));
            int good = 0;
            for (int k = 0; k < 50; ++k) {
                if (!t.step(0.01 + k * 0.04, k * 0.51)) {
                    std::fprintf(stderr, "beat sync %s failed at step %d\n", file, k);
                    break;
                }
                ++good;
            }
            CHECK(good == 50);
        }
        {
            nvj::ClipPlayback playback;
            playback.speed = 0.0f;
            Transport t;
            CHECK(t.open(file, playback));
            CHECK(t.step(0.01, 0.0));
            t.driver.scrubTo(1.01);
            CHECK(t.step(0.02, 0.0));
            CHECK(expectedIndex(t.driver.head()) == 25);
            t.driver.stepFrames(1);
            CHECK(t.step(0.03, 0.0));
            CHECK(expectedIndex(t.driver.head()) == 26);
            t.driver.stepFrames(-2);
            CHECK(t.step(0.04, 0.0));
            CHECK(expectedIndex(t.driver.head()) == 24);
            t.driver.scrubTo(0.01);
            CHECK(t.step(0.05, 0.0));
            CHECK(expectedIndex(t.driver.head()) == 0);
            t.driver.scrubTo(2.35);
            CHECK(t.step(0.06, 0.0));
            CHECK(expectedIndex(t.driver.head()) == 58);
        }
    }
}

void testPrerollReady() {
    for (const char* file : {"test_index_mjpeg.avi", "test_index_h264.mp4"}) {
        nvj::ClipPlayback playback;
        playback.in = 1.2;
        playback.out = 2.2;
        Transport t;
        CHECK(t.open(file, playback));
        double seconds = 0.0;
        const auto begin = std::chrono::steady_clock::now();
        std::shared_ptr<const nvj::VideoFrame> frame;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() < 5.0) {
            CHECK(t.driver.prepare(t.decoder, t.decoder.info(), seconds));
            frame = t.decoder.frameAt(seconds);
            if (frame && indexOfFrame(*frame) == 30) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        CHECK(frame != nullptr && indexOfFrame(*frame) == 30);
        const unsigned long long seeksBefore = t.decoder.status().seeks;
        t.visible = t.driver.drive(t.decoder, t.decoder.info(), 0.001, 0.0, seconds);
        const std::shared_ptr<const nvj::VideoFrame> first = t.decoder.frameAt(seconds);
        CHECK(first != nullptr && indexOfFrame(*first) == 30);
        CHECK(t.decoder.status().seeks == seeksBefore);
        CHECK(t.decoder.status().dropped == 0);
    }
}

int framesDuring(nvj::VideoDecoder& decoder, double seconds, double videoSpan) {
    int received = 0;
    unsigned long long lastId = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (;;) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (elapsed >= seconds) {
            return received;
        }
        const std::shared_ptr<const nvj::VideoFrame> frame = decoder.frameAt(std::fmod(elapsed, videoSpan));
        if (frame && frame->id != lastId) {
            lastId = frame->id;
            ++received;
        }
        if (decoder.failed()) {
            return received;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

bool damageFile(const fs::path& path, std::uintmax_t from, unsigned char value) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || from >= size) {
        return false;
    }
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekp(static_cast<std::streamoff>(from));
    const std::vector<char> block(4096, static_cast<char>(value));
    std::uintmax_t left = size - from;
    while (left > 0 && file) {
        const std::uintmax_t part = std::min<std::uintmax_t>(left, block.size());
        file.write(block.data(), static_cast<std::streamsize>(part));
        left -= part;
    }
    file.flush();
    return static_cast<bool>(file);
}

void testMediaOutage(const fs::path& scratch) {
    std::error_code ec;
    const fs::path folder = scratch / "outage";
    fs::create_directories(folder, ec);
    const fs::path original = kData / "test_h264.mp4";
    const std::uintmax_t size = fs::file_size(original, ec);
    CHECK(!ec && size > 4096);
    std::string error;

    for (const double keep : {0.5, 0.1, 0.0}) {
        const fs::path cut = folder / "cut_at_open.mp4";
        fs::copy_file(original, cut, fs::copy_options::overwrite_existing, ec);
        fs::resize_file(cut, static_cast<std::uintmax_t>(static_cast<double>(size) * keep), ec);
        CHECK(!ec);
        nvj::VideoDecoder decoder;
        const bool opened = decoder.open(cut.string(), error, legacyOptions());
        CHECK(opened || !error.empty());
        if (opened) {
            framesDuring(decoder, 1.0, 2.0);
        }
        decoder.close();
        CHECK(!decoder.isOpen());
        CHECK(decoder.frameAt(0.0) == nullptr);
    }

    const fs::path live = folder / "live.mp4";
    fs::copy_file(original, live, fs::copy_options::overwrite_existing, ec);
    {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open(live.string(), error, legacyOptions()));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.1) != nullptr);
        const int before = framesDuring(decoder, 0.5, 2.0);
        CHECK(before > 3);
        fs::resize_file(live, size / 3, ec);
        if (!ec) {
            framesDuring(decoder, 2.0, 2.0);
            CHECK(decoder.isOpen() || decoder.failed());
            CHECK(!decoder.failed() || !decoder.failure().empty());
        }
        decoder.close();
        CHECK(!decoder.isOpen());
    }

    fs::copy_file(original, live, fs::copy_options::overwrite_existing, ec);
    {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open(live.string(), error, legacyOptions()));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.1) != nullptr);
        if (damageFile(live, size / 2, 0xFF)) {
            framesDuring(decoder, 2.0, 2.0);
            CHECK(!decoder.failed() || !decoder.failure().empty());
        }
        decoder.close();
        CHECK(!decoder.isOpen());
    }

    fs::copy_file(original, live, fs::copy_options::overwrite_existing, ec);
    {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open(live.string(), error, legacyOptions()));
        CHECK(waitForFrame(decoder, 0.0, 0.0, 0.1) != nullptr);
        fs::remove(live, ec);
        if (!ec) {
            CHECK(framesDuring(decoder, 1.5, 2.0) > 3);
            CHECK(!decoder.failed());
        }
        decoder.close();
        CHECK(!decoder.isOpen());
    }

    fs::copy_file(original, live, fs::copy_options::overwrite_existing, ec);
    {
        nvj::VideoDecoder decoder;
        CHECK(decoder.open(live.string(), error, legacyOptions()));
        decoder.close();
        fs::remove(live, ec);
        CHECK(!decoder.open(live.string(), error, legacyOptions()) && !error.empty());
        CHECK(!decoder.isOpen());
        fs::copy_file(original, live, fs::copy_options::overwrite_existing, ec);
        CHECK(decoder.open(live.string(), error, legacyOptions()));
        CHECK(waitForFrame(decoder, 0.5, 0.4, 0.6) != nullptr);
        CHECK(!decoder.failed());
        decoder.close();
    }
}

void testManyDecoders() {
    constexpr int kCount = 6;
    std::vector<std::unique_ptr<nvj::VideoDecoder>> decoders;
    const char* files[kCount] = {"test_h264.mp4", "test_hap.mov", "test_hap_alpha.mov", "test_hevc10.mp4", "test_hap_q.mov", "test_prores_alpha.mov"};
    std::string error;
    for (const char* file : files) {
        decoders.push_back(std::make_unique<nvj::VideoDecoder>());
        CHECK(decoders.back()->open((kData / file).string(), error, nvj::VideoOptions()));
    }
    const auto begin = std::chrono::steady_clock::now();
    std::array<unsigned long long, kCount> lastIds{};
    bool ordered = true;
    double now = 0.0;
    while ((now = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()) < 1.0) {
        for (int i = 0; i < kCount; ++i) {
            const std::shared_ptr<const nvj::VideoFrame> frame = decoders[static_cast<size_t>(i)]->frameAt(now);
            if (frame) {
                ordered = ordered && frame->id >= lastIds[static_cast<size_t>(i)];
                lastIds[static_cast<size_t>(i)] = frame->id;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }
    CHECK(ordered);
    for (int i = 0; i < kCount; ++i) {
        CHECK(lastIds[static_cast<size_t>(i)] > 0);
        CHECK(!decoders[static_cast<size_t>(i)]->failed());
        CHECK(decoders[static_cast<size_t>(i)]->status().decoded > 3);
    }
    decoders.clear();
}

bool converterAvailable() {
    return nvj::ffmpeg_setup::checkCapabilities().ready();
}

void testTargetChoice() {
    using namespace nvj::hapconv;
    bool alpha = false;
    CHECK(chooseTarget(Variant::Dxt1, TargetFormat::Auto, alpha) == TargetFormat::Hap && !alpha);
    CHECK(chooseTarget(Variant::Dxt5, TargetFormat::Auto, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(chooseTarget(Variant::Ycg6, TargetFormat::Auto, alpha) == TargetFormat::HapQ && !alpha);
    CHECK(chooseTarget(Variant::Yg10, TargetFormat::Auto, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(chooseTarget(Variant::Unknown, TargetFormat::Auto, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(chooseTarget(Variant::Dxt1, TargetFormat::HapQ, alpha) == TargetFormat::HapQ && !alpha);
    CHECK(chooseTarget(Variant::Dxt1, TargetFormat::HapAlpha, alpha) == TargetFormat::HapAlpha && !alpha);
    CHECK(chooseTarget(Variant::Ycg6, TargetFormat::Hap, alpha) == TargetFormat::Hap && !alpha);
    CHECK(chooseTarget(Variant::Dxt5, TargetFormat::Hap, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(chooseTarget(Variant::Dxt5, TargetFormat::HapQ, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(chooseTarget(Variant::Yg10, TargetFormat::HapQ, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(chooseTarget(Variant::Yg10, TargetFormat::Hap, alpha) == TargetFormat::HapAlpha && alpha);
    CHECK(!variantHasAlpha(Variant::Dxt1) && variantHasAlpha(Variant::Dxt5) && !variantHasAlpha(Variant::Ycg6) && variantHasAlpha(Variant::Yg10));
    CHECK(!variantIsHighQuality(Variant::Dxt1) && !variantIsHighQuality(Variant::Dxt5) && variantIsHighQuality(Variant::Ycg6) && variantIsHighQuality(Variant::Yg10));
    const uint8_t dxt1[] = {'1', 'T', 'X', 'D', 4, 0, 0, 0};
    const uint8_t dxt5[] = {'5', 'T', 'X', 'D', 4, 0, 0, 0};
    const uint8_t ycg6[] = {'6', 'G', 'C', 'Y', 4, 0, 0, 0};
    const uint8_t yg10[] = {'0', '1', 'G', 'Y', 4, 0, 0, 0};
    const uint8_t other[] = {'a', 'b', 'c', 'd'};
    CHECK(variantFromPacket(dxt1, sizeof(dxt1)) == Variant::Dxt1);
    CHECK(variantFromPacket(dxt5, sizeof(dxt5)) == Variant::Dxt5);
    CHECK(variantFromPacket(ycg6, sizeof(ycg6)) == Variant::Ycg6);
    CHECK(variantFromPacket(yg10, sizeof(yg10)) == Variant::Yg10);
    CHECK(variantFromPacket(other, sizeof(other)) == Variant::Unknown);
    CHECK(variantFromPacket(dxt1, 3) == Variant::Unknown && variantFromPacket(nullptr, 8) == Variant::Unknown);
    CHECK(targetFromName("hap") == TargetFormat::Hap && targetFromName("hap_alpha") == TargetFormat::HapAlpha && targetFromName("hap_q") == TargetFormat::HapQ && targetFromName("auto") == TargetFormat::Auto && targetFromName("junk") == TargetFormat::Auto);
    CHECK(targetKey(TargetFormat::HapQ) == "hap_q" && targetKey(TargetFormat::Auto) == "auto");
    CHECK(std::string(encoderFormat(TargetFormat::HapQ)) == "hap_q" && std::string(encoderFormat(TargetFormat::HapAlpha)) == "hap_alpha" && std::string(encoderFormat(TargetFormat::Hap)) == "hap");
}

nvj::VideoOptions hapGpuOptions() {
    nvj::VideoOptions options;
    options.hapDirect = true;
    options.compressedTextures = true;
    options.gpuConversion = false;
    options.mode = nvj::VideoDecodeMode::ForceSoftware;
    return options;
}

std::shared_ptr<const nvj::VideoFrame> frameOf(const std::string& path, const nvj::VideoOptions& options, double time, nvj::VideoStatus* status = nullptr) {
    static std::vector<std::unique_ptr<nvj::VideoDecoder>> keep;
    keep.push_back(std::make_unique<nvj::VideoDecoder>());
    std::string error;
    if (!keep.back()->open(path, error, options)) {
        return nullptr;
    }
    const std::shared_ptr<const nvj::VideoFrame> frame = waitForFrame(*keep.back(), time, time - 0.011, time + 0.011);
    if (status != nullptr) {
        *status = keep.back()->status();
    }
    return frame;
}

void testConverter(const fs::path& scratch) {
    using namespace nvj::hapconv;
    const std::string dxv = (kData / "test_dxv_dxt1.mov").string();
    const SourceInfo info = inspectSource(dxv);
    CHECK(info.ok && info.isDxv && info.variant == Variant::Dxt1);
    CHECK(info.width == 128 && info.height == 72 && std::abs(info.fps - 25.0) < 0.01 && info.frames == 12);
    CHECK(info.duration > 0.45 && info.duration < 0.52);
    const SourceInfo h264 = inspectSource((kData / "test_h264.mp4").string());
    CHECK(!h264.ok && !h264.isDxv && !h264.error.empty());
    CHECK(inspectSource((kData / "test_h264.mp4").string(), true).ok);
    CHECK(!inspectSource((kData / "missing.mov").string()).ok);
    CHECK(!inspectSource((kData / "test_alpha.png").string()).ok);

    const fs::path directory = scratch / "convert";
    std::error_code ec;
    fs::create_directories(directory, ec);
    const std::string output = (directory / "dxv_to_hap.mov").string();
    std::vector<int> steps;
    Options options;
    const Result result = convertFile(dxv, output, options, [&](int done, int total) {
        steps.push_back(done);
        CHECK(total == 0 || total >= done);
        return true;
    });
    CHECK(result.ok);
    CHECK(result.frames == 12 && result.width == 128 && result.height == 72 && std::abs(result.fps - 25.0) < 0.01);
    CHECK(result.variant == Variant::Dxt1 && result.format == TargetFormat::Hap && !result.alphaKept && result.snappy);
    CHECK(steps.size() == 12 && steps.front() == 1 && steps.back() == 12);
    CHECK(result.bytes > 0 && fs::exists(output) && result.sourceBytes == fs::file_size(dxv));
    std::string verifyError;
    CHECK(verifyOutput(output, result, verifyError));

    nvj::VideoStatus hapStatus;
    const std::shared_ptr<const nvj::VideoFrame> hapFrame = frameOf(output, hapGpuOptions(), 0.2, &hapStatus);
    const std::shared_ptr<const nvj::VideoFrame> dxvFrame = frameOf(dxv, legacyOptions(), 0.2);
    CHECK(hapFrame != nullptr && dxvFrame != nullptr);
    if (hapFrame && dxvFrame) {
        CHECK(hapFrame->layout == nvj::VideoLayout::Compressed && hapFrame->compressed == nvj::CompressedFormat::Dxt1);
        CHECK(hapStatus.path == nvj::DecodePath::HapGpu && hapStatus.codec == "hap");
        CHECK(hapFrame->width == 128 && hapFrame->height == 72 && std::abs(hapFrame->time - dxvFrame->time) < 0.001);
        const Difference diff = compare(pictureOf(*dxvFrame), pictureOf(*hapFrame));
        std::printf("DXV DXT1 to HAP: mean error %.2f worst %d alpha %.2f\n", diff.mean, diff.worst, diff.alphaMean);
        CHECK(diff.sameSize && diff.mean < 1.5 && diff.worst <= 12 && diff.alphaMean < 0.5);
    }
    nvj::VideoStatus dxvStatus;
    frameOf(dxv, nvj::VideoOptions(), 0.0, &dxvStatus);
    CHECK(dxvStatus.codec == "dxv" && dxvStatus.path == nvj::DecodePath::Software && dxvStatus.softwareOnly);

    nvj::VideoDecoder timing;
    std::string error;
    CHECK(timing.open(output, error, nvj::VideoOptions()));
    CHECK(timing.info().width == 128 && timing.info().height == 72 && std::abs(timing.info().fps - 25.0) < 0.5);
    CHECK(timing.info().duration > 0.45 && timing.info().duration < 0.52);
    nvj::VideoDecoder original;
    CHECK(original.open(dxv, error, nvj::VideoOptions()));
    CHECK(std::abs(timing.info().duration - original.info().duration) < 0.045);

    const double ratio = static_cast<double>(result.bytes) / static_cast<double>(result.sourceBytes);
    std::printf("DXV %llu bytes, HAP %llu bytes (%.2fx), convert %.3f s (decode %.3f s, encode %.3f s)\n", result.sourceBytes, result.bytes, ratio, result.seconds, result.decodeSeconds, result.encodeSeconds);

    {
        Options q;
        q.preference = TargetFormat::HapQ;
        const std::string path = (directory / "dxv_to_hapq.mov").string();
        const Result r = convertFile(dxv, path, q, nullptr);
        CHECK(r.ok && r.format == TargetFormat::HapQ && !r.alphaKept);
        const std::shared_ptr<const nvj::VideoFrame> frame = frameOf(path, hapGpuOptions(), 0.2);
        const std::shared_ptr<const nvj::VideoFrame> reference = frameOf(dxv, legacyOptions(), 0.2);
        CHECK(frame != nullptr && reference != nullptr);
        if (frame && reference) {
            CHECK(frame->layout == nvj::VideoLayout::Compressed && frame->compressed == nvj::CompressedFormat::YcoCgDxt5);
            const Difference diff = compare(pictureOf(*reference), pictureOf(*frame));
            std::printf("DXV DXT1 to HAP Q: mean error %.2f worst %d\n", diff.mean, diff.worst);
            CHECK(diff.sameSize && diff.mean < 3.0 && diff.worst <= 24);
        }
    }
    {
        Options a;
        a.preference = TargetFormat::HapAlpha;
        const std::string path = (directory / "dxv_to_hapalpha.mov").string();
        const Result r = convertFile(dxv, path, a, nullptr);
        CHECK(r.ok && r.format == TargetFormat::HapAlpha && !r.alphaKept);
        const std::shared_ptr<const nvj::VideoFrame> frame = frameOf(path, hapGpuOptions(), 0.2);
        CHECK(frame != nullptr && frame->compressed == nvj::CompressedFormat::Dxt5);
    }
    {
        Options plain;
        plain.snappy = false;
        const std::string path = (directory / "dxv_to_hap_raw.mov").string();
        const Result r = convertFile(dxv, path, plain, nullptr);
        CHECK(r.ok && !r.snappy && r.bytes > result.bytes);
    }
    {
        int calls = 0;
        const std::string path = (directory / "cancelled.mov").string();
        const Result r = convertFile(dxv, path, Options(), [&](int, int) { return ++calls < 4; });
        CHECK(!r.ok && r.cancelled && !fs::exists(path) && calls == 4);
    }
    {
        const Result r = convertFile(dxv, (directory / "missing_folder" / "x.mov").string(), Options(), nullptr);
        CHECK(!r.ok && !r.cancelled && !r.error.empty());
        const Result notDxv = convertFile((kData / "test_h264.mp4").string(), (directory / "notdxv.mov").string(), Options(), nullptr);
        CHECK(!notDxv.ok && notDxv.error.find("not DXV") != std::string::npos && !fs::exists(directory / "notdxv.mov"));
        const Result missing = convertFile((kData / "missing.mov").string(), (directory / "missing.mov").string(), Options(), nullptr);
        CHECK(!missing.ok && !fs::exists(directory / "missing.mov"));
        Options any;
        any.allowAnyCodec = true;
        const Result odd = convertFile((kData / "test_odd_size.avi").string(), (directory / "odd.mov").string(), any, nullptr);
        CHECK(!odd.ok && odd.error.find("multiple of 4") != std::string::npos && !fs::exists(directory / "odd.mov"));
    }
    {
        Options any;
        any.allowAnyCodec = true;
        const std::string path = (directory / "ycocg_to_hapq.mov").string();
        const Result r = convertFile((kData / "test_ycocg.mkv").string(), path, any, nullptr);
        CHECK(r.ok && r.variant == Variant::Ycg6 && r.format == TargetFormat::HapQ && !r.alphaKept);
        if (r.ok) {
            const std::shared_ptr<const nvj::VideoFrame> frame = frameOf(path, hapGpuOptions(), 0.0);
            CHECK(frame != nullptr);
            if (frame) {
                CHECK(frame->compressed == nvj::CompressedFormat::YcoCgDxt5 && frame->width == 64 && frame->height == 64);
                const Picture picture = pictureOf(*frame);
                const size_t middle = (static_cast<size_t>(picture.height / 2) * static_cast<size_t>(picture.width) + static_cast<size_t>(picture.width / 2)) * 4u;
                std::printf("YCoCg to HAP Q centre pixel: %d %d %d (expected 188 100 124)\n", picture.pixels[middle], picture.pixels[middle + 1], picture.pixels[middle + 2]);
                CHECK(std::abs(picture.pixels[middle] - 188) <= 6);
                CHECK(std::abs(picture.pixels[middle + 1] - 100) <= 6);
                CHECK(std::abs(picture.pixels[middle + 2] - 124) <= 6);
            }
        }
    }
    {
        Options any;
        any.allowAnyCodec = true;
        const std::string path = (directory / "alpha_to_hapalpha.mov").string();
        const Result r = convertFile((kData / "test_prores_alpha.mov").string(), path, any, nullptr);
        CHECK(r.ok && r.format == TargetFormat::HapAlpha && r.alphaKept);
        if (r.ok) {
            const std::shared_ptr<const nvj::VideoFrame> frame = frameOf(path, hapGpuOptions(), 0.0);
            const std::shared_ptr<const nvj::VideoFrame> reference = frameOf((kData / "test_prores_alpha.mov").string(), legacyOptions(), 0.0);
            CHECK(frame != nullptr && reference != nullptr);
            if (frame && reference) {
                CHECK(frame->compressed == nvj::CompressedFormat::Dxt5);
                const Difference diff = compare(pictureOf(*reference), pictureOf(*frame));
                std::printf("ProRes alpha to HAP Alpha: mean error %.2f alpha error %.2f\n", diff.mean, diff.alphaMean);
                CHECK(diff.sameSize && diff.alphaMean < 1.5 && diff.mean < 6.0);
            }
        }
        Options noAlpha = any;
        noAlpha.preference = TargetFormat::HapQ;
        const Result forced = convertFile((kData / "test_prores_alpha.mov").string(), (directory / "alpha_forced.mov").string(), noAlpha, nullptr);
        CHECK(forced.ok && forced.format == TargetFormat::HapAlpha && forced.alphaKept);
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
    testHapParser();
    testModeNames();
    testTransferNames();
    testHardwareLibraryGuard();
    if (videoAvailable()) {
        testHapDirect();
        testGpuLayouts();
        testDecodeModes();
        testSoftwareOnlyCodecs();
        testYcocg();
        testHardwareProbe();
        testLoopContinuity();
        testExactSeek();
        testRangeLoop();
        testReversePlayback();
        testReverseHap();
        testTransportModes();
        testPrerollReady();
        testManyDecoders();
        testMediaOutage(scratch);
    }
    testTargetChoice();
    if (videoAvailable() && converterAvailable()) {
        testConverter(scratch);
    } else {
        std::printf("converter tests skipped: FFmpeg with DXV decoder and HAP encoder is not available\n");
    }
    return testing::finish();
}

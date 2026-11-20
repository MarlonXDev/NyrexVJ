#include "check.hpp"

#include "media/hap.hpp"
#include "media/media_image.hpp"
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
            const double r = ly + 2.0 * (1.0 - kr) * vr;
            const double b = ly + 2.0 * (1.0 - kb) * ub;
            const double g = ly - 2.0 * kb * (1.0 - kb) / kg * ub - 2.0 * kr * (1.0 - kr) / kg * vr;
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
    };
    const Case cases[] = {
        {"test_h264.mp4", nvj::VideoLayout::Planar, 3, 8, false, 6.0},
        {"test_hevc10.mp4", nvj::VideoLayout::Planar, 3, 10, false, 6.0},
        {"test_prores_alpha.mov", nvj::VideoLayout::Planar, 4, 12, true, 6.0},
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
            CHECK(a->outWidth > 0 && a->outHeight > 0 && a->planes[0].width == a->width && a->planes[0].height == a->height);
            CHECK(b->layout == nvj::VideoLayout::Rgba);
            const Difference diff = compare(pictureOf(*a), pictureOf(*b));
            CHECK(diff.sameSize);
            CHECK(diff.mean < test.tolerance);
            CHECK(diff.alphaMean < 2.0);
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
    if (videoAvailable()) {
        testHapDirect();
        testGpuLayouts();
        testDecodeModes();
        testHardwareProbe();
        testLoopContinuity();
        testManyDecoders();
    }
    return testing::finish();
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace nvj {

enum class VideoLayout { Rgba, Planar, SemiPlanar, Compressed, PackedUyvy };

enum class CompressedFormat { None, Dxt1, Dxt5, YcoCgDxt5 };

enum class VideoDecodeMode { Auto, PreferHardware, ForceSoftware };

enum class DecodePath { None, Software, Hardware, HapGpu, HapCpu };

struct VideoPlane {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    int channels = 1;
    int bytes = 1;

    size_t size() const { return static_cast<size_t>(stride) * static_cast<size_t>(height); }
};

struct VideoFrame {
    int width = 0;
    int height = 0;
    int outWidth = 0;
    int outHeight = 0;
    double time = 0.0;
    unsigned long long id = 0;
    VideoLayout layout = VideoLayout::Rgba;
    CompressedFormat compressed = CompressedFormat::None;
    std::array<VideoPlane, 4> planes{};
    int planeCount = 0;
    int bitDepth = 8;
    bool msbAligned = false;
    bool fullRange = false;
    int matrix = 1;
    bool hasAlpha = false;
    std::shared_ptr<void> owner;
};

struct VideoOptions {
    int maxDimension = 1920;
    VideoDecodeMode mode = VideoDecodeMode::Auto;
    bool gpuConversion = true;
    bool hapDirect = true;
    bool compressedTextures = true;
    std::string hardwareFailure;
};

struct VideoStatus {
    DecodePath path = DecodePath::None;
    std::string codec;
    std::string decoder;
    std::string backend;
    std::string pixelFormat;
    std::string variant;
    std::string fallbackReason;
    std::string fallbackDetail;
    bool hardwareWanted = false;
    bool softwareOnly = false;
    bool convertedCopy = false;
    int threads = 1;
    double decodeMs = 0.0;
    unsigned long long decoded = 0;
    unsigned long long dropped = 0;
    unsigned long long underruns = 0;
    unsigned long long seeks = 0;
    bool reverse = false;
    int reverseFrames = 0;
    double rangeIn = 0.0;
    double rangeOut = 0.0;
};

const char* decodePathName(DecodePath path);
const char* videoDecodeModeName(VideoDecodeMode mode);
VideoDecodeMode videoDecodeModeFromName(const std::string& name);

}

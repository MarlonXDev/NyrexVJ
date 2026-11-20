#pragma once

#include "media/video_types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct VideoSupport {
    bool compiled = false;
    bool loaded = false;
    std::string folder;
    std::vector<std::string> files;
    std::string problem;
};

struct VideoInfo {
    int width = 0;
    int height = 0;
    int decodedWidth = 0;
    int decodedHeight = 0;
    double fps = 0.0;
    double duration = 0.0;
    bool hasAlpha = false;
    std::string codec;
};

constexpr int kMaxVideoDimension = 1920;

std::string ffmpegDirectory();
VideoSupport videoSupport();
void retryVideoSupport();

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const std::string& path, std::string& error, const VideoOptions& options = VideoOptions());
    void close();
    bool isOpen() const;
    bool failed() const;
    std::string failure() const;
    const VideoInfo& info() const;
    VideoStatus status() const;
    std::shared_ptr<const VideoFrame> frameAt(double seconds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

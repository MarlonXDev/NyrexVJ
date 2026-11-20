#pragma once

#include "media/ffmpeg_api.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include <string>

namespace nvj {

struct HardwareSetup {
    const AVCodec* decoder = nullptr;
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    AVPixelFormat format = AV_PIX_FMT_NONE;
    AVBufferRef* device = nullptr;
    std::string backend;
};

const AVCodec* hardwareDecoder(const FfmpegApi& api, AVCodecID codec, const std::string& backend);
bool prepareHardware(const FfmpegApi& api, AVCodecID codec, const std::string& backend, HardwareSetup& setup, std::string& reason);
void releaseHardware(const FfmpegApi& api, HardwareSetup& setup);
bool openHardwareContext(const FfmpegApi& api, const AVCodecParameters* parameters, HardwareSetup& setup, AVCodecContext*& context, std::string& reason);
std::string ffmpegError(const FfmpegApi& api, int code);

}

#endif

#pragma once

#include "media/ffmpeg_api.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include <cstdint>
#include <vector>

namespace nvj {

bool convertYcocgToRgba(const FfmpegApi& api, const AVFrame& frame, std::vector<uint8_t>& rgba);

class RgbaConverter {
public:
    RgbaConverter() = default;
    ~RgbaConverter();
    RgbaConverter(const RgbaConverter&) = delete;
    RgbaConverter& operator=(const RgbaConverter&) = delete;

    bool convert(const FfmpegApi& api, const AVFrame& frame, uint8_t* destination, int destinationStride);

private:
    const FfmpegApi* api_ = nullptr;
    SwsContext* scaler_ = nullptr;
    std::vector<uint8_t> scratch_;
};

}

#endif

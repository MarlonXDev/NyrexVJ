#include "media/live_convert.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include <cstring>

namespace nvj {

std::string ffmpegErrorText(const FfmpegApi& api, int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    api.av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

bool convertToUyvy(const FfmpegApi& api, SwsContext*& sws, const AVFrame& picture, double arrived, int maxSide, const char* what, LiveFrame& out, std::string& problem) {
    const int width = picture.width & ~1;
    const int height = picture.height;
    if (width <= 0 || height <= 0 || width > maxSide || height > maxSide) {
        problem = std::string("Unsupported ") + what + " picture size";
        return false;
    }
    const size_t rowBytes = static_cast<size_t>(width) * 2u;
    out.format = LiveFormat::Uyvy;
    out.width = width;
    out.height = height;
    out.stride = static_cast<int>(rowBytes);
    out.capturedAt = arrived;
    out.data.resize(rowBytes * static_cast<size_t>(height));
    const AVPixelFormat source = static_cast<AVPixelFormat>(picture.format);
    if (source == AV_PIX_FMT_UYVY422 && picture.data[0] != nullptr) {
        for (int y = 0; y < height; ++y) {
            std::memcpy(out.data.data() + static_cast<size_t>(y) * rowBytes, picture.data[0] + static_cast<ptrdiff_t>(y) * picture.linesize[0], rowBytes);
        }
        return true;
    }
    sws = api.sws_getCachedContext(sws, picture.width, picture.height, source, width, height, AV_PIX_FMT_UYVY422, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        const char* name = api.av_get_pix_fmt_name(source);
        problem = std::string("Unsupported ") + what + " pixel format " + (name != nullptr ? name : "unknown");
        return false;
    }
    const char* sourceName = api.av_get_pix_fmt_name(source);
    const bool jpegRange = picture.color_range == AVCOL_RANGE_JPEG || (sourceName != nullptr && std::strncmp(sourceName, "yuvj", 4) == 0);
    int sourceMatrix = height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    if (picture.colorspace == AVCOL_SPC_BT709) {
        sourceMatrix = SWS_CS_ITU709;
    } else if (picture.colorspace == AVCOL_SPC_BT470BG || picture.colorspace == AVCOL_SPC_SMPTE170M) {
        sourceMatrix = SWS_CS_ITU601;
    }
    const int targetMatrix = height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    api.sws_setColorspaceDetails(sws, api.sws_getCoefficients(sourceMatrix), jpegRange ? 1 : 0, api.sws_getCoefficients(targetMatrix), 0, 0, 1 << 16, 1 << 16);
    uint8_t* destination[4] = {out.data.data(), nullptr, nullptr, nullptr};
    int destinationStride[4] = {static_cast<int>(rowBytes), 0, 0, 0};
    const int scaled = api.sws_scale(sws, picture.data, picture.linesize, 0, picture.height, destination, destinationStride);
    if (scaled <= 0) {
        problem = std::string("The ") + what + " picture could not be converted";
        return false;
    }
    return true;
}

}

#endif

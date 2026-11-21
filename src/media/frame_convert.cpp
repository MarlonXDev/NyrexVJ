#include "media/frame_convert.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include <algorithm>
#include <cstring>

namespace nvj {

bool convertYcocgToRgba(const FfmpegApi& api, const AVFrame& frame, std::vector<uint8_t>& rgba) {
    const AVPixFmtDescriptor* d = api.av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
    if (d == nullptr || (d->flags & (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_BE)) != AV_PIX_FMT_FLAG_PLANAR || d->nb_components < 3 || d->nb_components > 4) {
        return false;
    }
    for (int i = 0; i < d->nb_components; ++i) {
        if (d->comp[i].plane != i || d->comp[i].step != 1 || d->comp[i].offset != 0 || d->comp[i].depth != 8 || frame.data[i] == nullptr || frame.linesize[i] <= 0) {
            return false;
        }
    }
    const bool alpha = d->nb_components == 4;
    rgba.resize(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u);
    uint8_t* out = rgba.data();
    for (int y = 0; y < frame.height; ++y) {
        const int cy = y >> d->log2_chroma_h;
        const uint8_t* luma = frame.data[0] + static_cast<ptrdiff_t>(y) * frame.linesize[0];
        const uint8_t* cg = frame.data[1] + static_cast<ptrdiff_t>(cy) * frame.linesize[1];
        const uint8_t* co = frame.data[2] + static_cast<ptrdiff_t>(cy) * frame.linesize[2];
        const uint8_t* opacity = alpha ? frame.data[3] + static_cast<ptrdiff_t>(y) * frame.linesize[3] : nullptr;
        for (int x = 0; x < frame.width; ++x) {
            const int cx = x >> d->log2_chroma_w;
            const int lumaValue = luma[x];
            const int g = cg[cx] - 128;
            const int o = co[cx] - 128;
            out[0] = static_cast<uint8_t>(std::clamp(lumaValue + o - g, 0, 255));
            out[1] = static_cast<uint8_t>(std::clamp(lumaValue + g, 0, 255));
            out[2] = static_cast<uint8_t>(std::clamp(lumaValue - g - o, 0, 255));
            out[3] = opacity != nullptr ? opacity[x] : 255;
            out += 4;
        }
    }
    return true;
}

RgbaConverter::~RgbaConverter() {
    if (scaler_ != nullptr && api_ != nullptr) {
        api_->sws_freeContext(scaler_);
    }
}

bool RgbaConverter::convert(const FfmpegApi& api, const AVFrame& frame, uint8_t* destination, int destinationStride) {
    api_ = &api;
    if (frame.width <= 0 || frame.height <= 0 || destination == nullptr) {
        return false;
    }
    if (frame.format == AV_PIX_FMT_RGBA && frame.data[0] != nullptr) {
        const size_t rowBytes = static_cast<size_t>(frame.width) * 4u;
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(destination + static_cast<ptrdiff_t>(y) * destinationStride, frame.data[0] + static_cast<ptrdiff_t>(y) * frame.linesize[0], rowBytes);
        }
        return true;
    }
    const bool ycocg = frame.colorspace == AVCOL_SPC_YCGCO && convertYcocgToRgba(api, frame, scratch_);
    const AVPixelFormat inputFormat = ycocg ? AV_PIX_FMT_RGBA : static_cast<AVPixelFormat>(frame.format);
    scaler_ = api.sws_getCachedContext(scaler_, frame.width, frame.height, inputFormat, frame.width, frame.height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (scaler_ == nullptr) {
        return false;
    }
    uint8_t* planesOut[4] = {destination, nullptr, nullptr, nullptr};
    int strideOut[4] = {destinationStride, 0, 0, 0};
    if (ycocg) {
        const uint8_t* planes[4] = {scratch_.data(), nullptr, nullptr, nullptr};
        const int planeStride[4] = {frame.width * 4, 0, 0, 0};
        api.sws_scale(scaler_, planes, planeStride, 0, frame.height, planesOut, strideOut);
        return true;
    }
    int space = SWS_CS_DEFAULT;
    if (frame.colorspace == AVCOL_SPC_BT709 || (frame.colorspace == AVCOL_SPC_UNSPECIFIED && frame.height >= 720)) {
        space = SWS_CS_ITU709;
    } else if (frame.colorspace == AVCOL_SPC_BT2020_NCL || frame.colorspace == AVCOL_SPC_BT2020_CL) {
        space = SWS_CS_BT2020;
    }
    const int sourceRange = frame.color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    api.sws_setColorspaceDetails(scaler_, api.sws_getCoefficients(space), sourceRange, api.sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    api.sws_scale(scaler_, frame.data, frame.linesize, 0, frame.height, planesOut, strideOut);
    return true;
}

}

#endif

#include "media/frame_luma.hpp"

namespace nvj {

bool flowCanAnalyse(const VideoFrame& frame, std::string* reason) {
    const auto refuse = [&](const char* text) {
        if (reason != nullptr) {
            *reason = text;
        }
        return false;
    };
    if (frame.planeCount < 1 || frame.planes[0].data == nullptr || frame.planes[0].width <= 0 || frame.planes[0].height <= 0) {
        return refuse("the frame has no picture data");
    }
    switch (frame.layout) {
    case VideoLayout::Rgba:
        if (frame.planes[0].bytes != 1 || frame.planes[0].channels != 4) {
            return refuse("the RGBA frame is not 8 bit");
        }
        return true;
    case VideoLayout::Planar:
    case VideoLayout::SemiPlanar:
        if (frame.planes[0].bytes < 1 || frame.planes[0].bytes > 2) {
            return refuse("the luma plane has an unsupported sample size");
        }
        return true;
    case VideoLayout::PackedUyvy:
        if (frame.planes[0].bytes != 1 || frame.bitDepth > 8 || frame.width <= 0 || frame.height <= 0 || frame.planes[0].stride < frame.width * 2) {
            return refuse("the packed frame is not 8 bit UYVY");
        }
        return true;
    default:
        return refuse("compressed frames are not analysed");
    }
}

flow::Luma lumaFromVideoFrame(const VideoFrame& frame, int targetWidth, int targetHeight) {
    if (!flowCanAnalyse(frame)) {
        return flow::Luma();
    }
    const VideoPlane& plane = frame.planes[0];
    switch (frame.layout) {
    case VideoLayout::Rgba:
        return flow::lumaFromRgba(plane.data, plane.width, plane.height, plane.stride, targetWidth, targetHeight);
    case VideoLayout::PackedUyvy:
        return flow::lumaFromUyvy(plane.data, frame.width, frame.height, plane.stride, targetWidth, targetHeight);
    default: {
        const bool alpha = frame.layout == VideoLayout::Planar && frame.planeCount == 4 && frame.planes[3].data != nullptr && frame.planes[3].width == plane.width && frame.planes[3].height == plane.height;
        const std::uint8_t* alphaData = alpha ? frame.planes[3].data : nullptr;
        const int alphaStride = alpha ? frame.planes[3].stride : 0;
        const int alphaBytes = alpha ? frame.planes[3].bytes : 0;
        return flow::lumaFromPlane(plane.data, plane.width, plane.height, plane.stride, plane.bytes, frame.bitDepth, frame.msbAligned, alphaData, alphaStride, alphaBytes, targetWidth, targetHeight);
    }
    }
}

}

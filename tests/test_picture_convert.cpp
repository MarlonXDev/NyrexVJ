#include "check.hpp"

#include "media/live_convert.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef NYREXVJ_HAVE_FFMPEG

namespace {

bool videoRequired() {
    const char* value = std::getenv("NYREXVJ_REQUIRE_VIDEO");
    return value != nullptr && *value != '\0';
}

constexpr size_t kCanaryBytes = 512;
constexpr unsigned char kCanary = 0xab;

struct Counters {
    int converted = 0;
    int refused = 0;
    size_t overshoot = 0;
    int widthOfOvershoot = 0;
};

int planeRows(const nvj::FfmpegApi& api, const AVFrame& picture, int plane) {
    const AVPixFmtDescriptor* description = api.av_pix_fmt_desc_get(static_cast<AVPixelFormat>(picture.format));
    if (description == nullptr) {
        return 0;
    }
    if (plane == 0 || plane == 3 || description->nb_components < 3) {
        return picture.height;
    }
    return (picture.height + (1 << description->log2_chroma_h) - 1) >> description->log2_chroma_h;
}

void fill(const nvj::FfmpegApi& api, AVFrame& picture) {
    for (int plane = 0; plane < 4; ++plane) {
        if (picture.data[plane] == nullptr) {
            continue;
        }
        const int rows = planeRows(api, picture, plane);
        const unsigned char value = plane == 0 ? 100 : 128;
        for (int row = 0; row < rows; ++row) {
            std::memset(picture.data[plane] + static_cast<ptrdiff_t>(row) * picture.linesize[plane], value, static_cast<size_t>(picture.linesize[plane]));
        }
    }
}

void runCase(const nvj::FfmpegApi& api, SwsContext*& sws, AVPixelFormat format, int width, int height, Counters& counters) {
    AVFrame* picture = api.av_frame_alloc();
    picture->format = format;
    picture->width = width;
    picture->height = height;
    if (api.av_frame_get_buffer(picture, 32) < 0) {
        api.av_frame_free(&picture);
        ++counters.refused;
        return;
    }
    fill(api, *picture);

    const int evenWidth = width & ~1;
    const size_t frameBytes = static_cast<size_t>(evenWidth) * 2u * static_cast<size_t>(height);
    nvj::LiveFrame out;
    out.data.assign(frameBytes + nvj::kConvertPadding + kCanaryBytes, kCanary);
    std::string problem;
    const bool ok = nvj::convertToUyvy(api, sws, *picture, 1.0, 16384, "test", out, problem);
    if (!ok) {
        ++counters.refused;
        api.av_frame_free(&picture);
        return;
    }
    ++counters.converted;
    CHECK(out.width == evenWidth);
    CHECK(out.height == height);
    CHECK(out.stride == evenWidth * 2);
    CHECK(out.data.size() == frameBytes);
    const unsigned char* behind = out.data.data() + frameBytes;
    size_t reached = 0;
    for (size_t i = 0; i < nvj::kConvertPadding + kCanaryBytes; ++i) {
        if (behind[i] != kCanary) {
            reached = i + 1;
        }
    }
    if (reached > counters.overshoot) {
        counters.overshoot = reached;
        counters.widthOfOvershoot = width;
    }
    CHECK(reached <= nvj::kConvertPadding / 2);
    if (reached > nvj::kConvertPadding / 2) {
        std::fprintf(stderr, "format %d, %dx%d wrote %zu bytes past the picture\n", static_cast<int>(format), width, height, reached);
    }
    if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUV422P || format == AV_PIX_FMT_YUV444P || format == AV_PIX_FMT_NV12) {
        const size_t middle = static_cast<size_t>(height / 2) * static_cast<size_t>(out.stride) + (static_cast<size_t>(evenWidth / 2) / 2u) * 4u;
        CHECK(out.data[middle] >= 120 && out.data[middle] <= 136);
        CHECK(out.data[middle + 1] >= 92 && out.data[middle + 1] <= 108);
    }
    api.av_frame_free(&picture);
}

void testConversionStaysInsideItsBuffer() {
    std::string problem;
    const nvj::FfmpegApi* api = nvj::ffmpegApi(problem);
    if (api == nullptr) {
        CHECK(!videoRequired());
        std::printf("conversion test skipped: FFmpeg libraries not found (%s)\n", problem.c_str());
        return;
    }
    const AVPixelFormat formats[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUYV422, AV_PIX_FMT_UYVY422, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_RGB24, AV_PIX_FMT_BGRA};
    const int widths[] = {2, 3, 6, 14, 15, 18, 30, 34, 46, 62, 66, 98, 126, 130, 254, 258, 318, 321, 322, 330, 424, 426, 640, 854, 856, 1366, 1368, 1920};
    const int heights[] = {2, 3, 7, 178, 180, 480, 768};
    Counters counters;
    SwsContext* sws = nullptr;
    for (const AVPixelFormat format : formats) {
        for (const int width : widths) {
            for (const int height : heights) {
                runCase(*api, sws, format, width, height, counters);
            }
        }
    }
    if (sws != nullptr) {
        api->sws_freeContext(sws);
    }
    CHECK(counters.converted > 1500);
    std::printf("converted %d pictures, %d not possible, the most written past the end of a picture was %zu bytes (width %d), the padding is %zu bytes\n", counters.converted, counters.refused, counters.overshoot, counters.widthOfOvershoot, nvj::kConvertPadding);
}

}

int main() {
    testConversionStaysInsideItsBuffer();
    return testing::finish();
}

#else

int main() {
    std::printf("conversion test skipped: built without the FFmpeg headers\n");
    CHECK(true);
    return testing::finish();
}

#endif

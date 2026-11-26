#pragma once

#include "core/live_source.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include "media/ffmpeg_api.hpp"

#include <cstddef>
#include <string>

namespace nvj {

constexpr size_t kConvertPadding = 128;

std::string ffmpegErrorText(const FfmpegApi& api, int code);
bool convertToUyvy(const FfmpegApi& api, SwsContext*& sws, const AVFrame& picture, double arrived, int maxSide, const char* what, LiveFrame& out, std::string& problem);

}

#endif

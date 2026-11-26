#pragma once

#include "core/optical_flow.hpp"
#include "media/video_types.hpp"

#include <string>

namespace nvj {

bool flowCanAnalyse(const VideoFrame& frame, std::string* reason = nullptr);
flow::Luma lumaFromVideoFrame(const VideoFrame& frame, int targetWidth, int targetHeight);

}

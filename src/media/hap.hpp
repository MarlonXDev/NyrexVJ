#pragma once

#include "media/video_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nvj {

enum class HapResult { Ok, Unsupported, Corrupt };

struct HapFrameInfo {
    CompressedFormat format = CompressedFormat::None;
    uint8_t type = 0;
    size_t textureBytes = 0;
};

int hapCodedSize(int pixels);
size_t hapTextureBytes(CompressedFormat format, int width, int height);
const char* hapVariantName(CompressedFormat format);
bool hapFormatFromFourcc(uint32_t fourcc, CompressedFormat& format);

bool snappyUncompressedLength(const uint8_t* input, size_t size, size_t& length);
bool snappyDecompress(const uint8_t* input, size_t size, uint8_t* output, size_t capacity, size_t& written);

HapResult hapInspect(const uint8_t* input, size_t size, HapFrameInfo& info);
HapResult hapDecode(const uint8_t* input, size_t size, int width, int height, uint8_t* output, size_t capacity, HapFrameInfo& info, std::string* error = nullptr);

}

#include "media/hap.hpp"

#include <cstring>

namespace nvj {

namespace {

constexpr uint8_t kCompressorNone = 0xA;
constexpr uint8_t kCompressorSnappy = 0xB;
constexpr uint8_t kCompressorComplex = 0xC;
constexpr uint8_t kSectionDecodeInstructions = 0x01;
constexpr uint8_t kSectionCompressors = 0x02;
constexpr uint8_t kSectionSizes = 0x03;
constexpr uint8_t kSectionOffsets = 0x04;
constexpr size_t kMaxChunks = 1024;

struct Section {
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    size_t totalSize = 0;
    uint8_t type = 0;
};

bool readSection(const uint8_t* data, size_t size, Section& section) {
    if (size < 4) {
        return false;
    }
    size_t length = static_cast<size_t>(data[0]) | (static_cast<size_t>(data[1]) << 8) | (static_cast<size_t>(data[2]) << 16);
    size_t header = 4;
    if (length == 0) {
        if (size < 8) {
            return false;
        }
        length = static_cast<size_t>(data[4]) | (static_cast<size_t>(data[5]) << 8) | (static_cast<size_t>(data[6]) << 16) | (static_cast<size_t>(data[7]) << 24);
        header = 8;
    }
    if (length > size - header) {
        return false;
    }
    section.type = data[3];
    section.payload = data + header;
    section.payloadSize = length;
    section.totalSize = header + length;
    return true;
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool textureFormat(uint8_t nibble, CompressedFormat& format) {
    switch (nibble) {
    case 0xB:
        format = CompressedFormat::Dxt1;
        return true;
    case 0xE:
        format = CompressedFormat::Dxt5;
        return true;
    case 0xF:
        format = CompressedFormat::YcoCgDxt5;
        return true;
    default:
        return false;
    }
}

bool readVarint(const uint8_t*& cursor, const uint8_t* end, size_t& value) {
    value = 0;
    int shift = 0;
    while (cursor < end && shift <= 28) {
        const uint8_t byte = *cursor++;
        value |= static_cast<size_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

}

int hapCodedSize(int pixels) {
    return (pixels + 3) & ~3;
}

size_t hapTextureBytes(CompressedFormat format, int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const size_t blocks = static_cast<size_t>(hapCodedSize(width) / 4) * static_cast<size_t>(hapCodedSize(height) / 4);
    switch (format) {
    case CompressedFormat::Dxt1:
        return blocks * 8;
    case CompressedFormat::Dxt5:
    case CompressedFormat::YcoCgDxt5:
        return blocks * 16;
    default:
        return 0;
    }
}

const char* hapVariantName(CompressedFormat format) {
    switch (format) {
    case CompressedFormat::Dxt1:
        return "HAP";
    case CompressedFormat::Dxt5:
        return "HAP Alpha";
    case CompressedFormat::YcoCgDxt5:
        return "HAP Q";
    default:
        return "HAP";
    }
}

bool hapFormatFromFourcc(uint32_t fourcc, CompressedFormat& format) {
    const auto tag = [](char a, char b, char c, char d) {
        return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
    };
    if (fourcc == tag('H', 'a', 'p', '1')) {
        format = CompressedFormat::Dxt1;
        return true;
    }
    if (fourcc == tag('H', 'a', 'p', '5')) {
        format = CompressedFormat::Dxt5;
        return true;
    }
    if (fourcc == tag('H', 'a', 'p', 'Y')) {
        format = CompressedFormat::YcoCgDxt5;
        return true;
    }
    return false;
}

bool snappyUncompressedLength(const uint8_t* input, size_t size, size_t& length) {
    const uint8_t* cursor = input;
    return readVarint(cursor, input + size, length);
}

bool snappyDecompress(const uint8_t* input, size_t size, uint8_t* output, size_t capacity, size_t& written) {
    const uint8_t* cursor = input;
    const uint8_t* const end = input + size;
    size_t expected = 0;
    if (!readVarint(cursor, end, expected) || expected > capacity) {
        return false;
    }
    size_t out = 0;
    while (cursor < end) {
        const uint8_t tag = *cursor++;
        const uint8_t kind = tag & 3;
        if (kind == 0) {
            size_t length = static_cast<size_t>(tag >> 2) + 1;
            if (length > 60) {
                const size_t extra = length - 60;
                if (static_cast<size_t>(end - cursor) < extra) {
                    return false;
                }
                length = 0;
                for (size_t i = 0; i < extra; ++i) {
                    length |= static_cast<size_t>(cursor[i]) << (8 * i);
                }
                length += 1;
                cursor += extra;
            }
            if (static_cast<size_t>(end - cursor) < length || expected - out < length) {
                return false;
            }
            std::memcpy(output + out, cursor, length);
            cursor += length;
            out += length;
            continue;
        }
        size_t length = 0;
        size_t offset = 0;
        if (kind == 1) {
            if (cursor >= end) {
                return false;
            }
            length = static_cast<size_t>((tag >> 2) & 7) + 4;
            offset = (static_cast<size_t>(tag >> 5) << 8) | *cursor++;
        } else if (kind == 2) {
            if (end - cursor < 2) {
                return false;
            }
            length = static_cast<size_t>(tag >> 2) + 1;
            offset = static_cast<size_t>(cursor[0]) | (static_cast<size_t>(cursor[1]) << 8);
            cursor += 2;
        } else {
            if (end - cursor < 4) {
                return false;
            }
            length = static_cast<size_t>(tag >> 2) + 1;
            offset = static_cast<size_t>(readLe32(cursor));
            cursor += 4;
        }
        if (offset == 0 || offset > out || expected - out < length) {
            return false;
        }
        const uint8_t* source = output + out - offset;
        uint8_t* target = output + out;
        if (offset >= length) {
            std::memcpy(target, source, length);
        } else {
            for (size_t i = 0; i < length; ++i) {
                target[i] = source[i];
            }
        }
        out += length;
    }
    if (out != expected) {
        return false;
    }
    written = out;
    return true;
}

HapResult hapInspect(const uint8_t* input, size_t size, HapFrameInfo& info) {
    Section frame;
    if (!readSection(input, size, frame)) {
        return HapResult::Corrupt;
    }
    CompressedFormat format = CompressedFormat::None;
    if (!textureFormat(frame.type & 0x0F, format)) {
        return HapResult::Unsupported;
    }
    const uint8_t compressor = static_cast<uint8_t>(frame.type >> 4);
    if (compressor != kCompressorNone && compressor != kCompressorSnappy && compressor != kCompressorComplex) {
        return HapResult::Unsupported;
    }
    info.format = format;
    info.type = frame.type;
    return HapResult::Ok;
}

HapResult hapDecode(const uint8_t* input, size_t size, int width, int height, uint8_t* output, size_t capacity, HapFrameInfo& info, std::string* error) {
    const auto fail = [&](HapResult result, const char* text) {
        if (error != nullptr) {
            *error = text;
        }
        return result;
    };
    const HapResult inspected = hapInspect(input, size, info);
    if (inspected != HapResult::Ok) {
        return fail(inspected, inspected == HapResult::Unsupported ? "this HAP variant is not supported" : "the HAP frame header is damaged");
    }
    info.textureBytes = hapTextureBytes(info.format, width, height);
    if (info.textureBytes == 0 || info.textureBytes > capacity) {
        return fail(HapResult::Corrupt, "the HAP frame size does not fit");
    }
    Section frame;
    readSection(input, size, frame);
    const uint8_t compressor = static_cast<uint8_t>(frame.type >> 4);

    if (compressor == kCompressorNone) {
        if (frame.payloadSize != info.textureBytes) {
            return fail(HapResult::Corrupt, "the HAP frame has the wrong size");
        }
        std::memcpy(output, frame.payload, info.textureBytes);
        return HapResult::Ok;
    }
    if (compressor == kCompressorSnappy) {
        size_t written = 0;
        if (!snappyDecompress(frame.payload, frame.payloadSize, output, info.textureBytes, written) || written != info.textureBytes) {
            return fail(HapResult::Corrupt, "the HAP frame could not be decompressed");
        }
        return HapResult::Ok;
    }

    Section container;
    if (!readSection(frame.payload, frame.payloadSize, container) || container.type != kSectionDecodeInstructions) {
        return fail(HapResult::Corrupt, "the HAP chunk table is missing");
    }
    const uint8_t* compressors = nullptr;
    size_t compressorCount = 0;
    const uint8_t* sizes = nullptr;
    size_t sizeBytes = 0;
    const uint8_t* offsets = nullptr;
    size_t offsetBytes = 0;
    size_t position = 0;
    while (position < container.payloadSize) {
        Section part;
        if (!readSection(container.payload + position, container.payloadSize - position, part)) {
            return fail(HapResult::Corrupt, "the HAP chunk table is damaged");
        }
        if (part.type == kSectionCompressors) {
            compressors = part.payload;
            compressorCount = part.payloadSize;
        } else if (part.type == kSectionSizes) {
            sizes = part.payload;
            sizeBytes = part.payloadSize;
        } else if (part.type == kSectionOffsets) {
            offsets = part.payload;
            offsetBytes = part.payloadSize;
        }
        position += part.totalSize;
    }
    if (compressors == nullptr || sizes == nullptr || compressorCount == 0 || compressorCount > kMaxChunks || sizeBytes != compressorCount * 4 || (offsets != nullptr && offsetBytes != compressorCount * 4)) {
        return fail(HapResult::Corrupt, "the HAP chunk table is inconsistent");
    }
    const uint8_t* chunkData = frame.payload + container.totalSize;
    const size_t chunkArea = frame.payloadSize - container.totalSize;
    size_t read = 0;
    size_t written = 0;
    for (size_t i = 0; i < compressorCount; ++i) {
        const size_t chunkSize = readLe32(sizes + i * 4);
        const size_t start = offsets != nullptr ? readLe32(offsets + i * 4) : read;
        if (start > chunkArea || chunkSize > chunkArea - start) {
            return fail(HapResult::Corrupt, "a HAP chunk lies outside the frame");
        }
        const uint8_t* chunk = chunkData + start;
        if (compressors[i] == kCompressorNone) {
            if (chunkSize > info.textureBytes - written) {
                return fail(HapResult::Corrupt, "the HAP chunks are larger than the picture");
            }
            std::memcpy(output + written, chunk, chunkSize);
            written += chunkSize;
        } else if (compressors[i] == kCompressorSnappy) {
            size_t produced = 0;
            if (!snappyDecompress(chunk, chunkSize, output + written, info.textureBytes - written, produced)) {
                return fail(HapResult::Corrupt, "a HAP chunk could not be decompressed");
            }
            written += produced;
        } else {
            return fail(HapResult::Unsupported, "this HAP chunk compressor is not supported");
        }
        read = start + chunkSize;
    }
    if (written != info.textureBytes) {
        return fail(HapResult::Corrupt, "the HAP chunks do not fill the picture");
    }
    return HapResult::Ok;
}

}

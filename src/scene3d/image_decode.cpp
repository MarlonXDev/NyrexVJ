#include "scene3d/image_decode.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

namespace nvj {

namespace {

bool isPngOrJpeg(const uint8_t* data, size_t size) {
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (size >= 8) {
        bool match = true;
        for (size_t i = 0; i < 8; ++i) {
            match = match && data[i] == png[i];
        }
        if (match) {
            return true;
        }
    }
    return size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
}

bool decode(const uint8_t* data, size_t size, int maxDimension, size_t maxBytes, bool anyFormat, SceneImage& out, std::string& error) {
    if (data == nullptr || size == 0 || size > 0x7fffffffu) {
        error = "empty image data";
        return false;
    }
    if (!anyFormat && !isPngOrJpeg(data, size)) {
        error = "unsupported image format (only PNG and JPEG are read)";
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &channels)) {
        error = anyFormat ? "unsupported image format (PNG, JPEG, BMP, TGA and GIF are read)" : "unsupported image format (only PNG and JPEG are read)";
        return false;
    }
    if (width <= 0 || height <= 0 || width > maxDimension || height > maxDimension) {
        error = "image dimensions exceed the limit";
        return false;
    }
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (bytes > maxBytes) {
        error = "image memory budget exceeded";
        return false;
    }
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        error = "image could not be decoded";
        return false;
    }
    out.width = width;
    out.height = height;
    out.rgba.assign(pixels, pixels + bytes);
    stbi_image_free(pixels);
    return true;
}

}

bool decodeSceneImage(const uint8_t* data, size_t size, int maxDimension, size_t maxBytes, SceneImage& out, std::string& error) {
    return decode(data, size, maxDimension, maxBytes, false, out, error);
}

bool decodeMediaImage(const uint8_t* data, size_t size, int maxDimension, size_t maxBytes, SceneImage& out, std::string& error) {
    return decode(data, size, maxDimension, maxBytes, true, out, error);
}

}

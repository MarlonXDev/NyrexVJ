#include "scene3d/image_decode.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

namespace nvj {

bool decodeSceneImage(const uint8_t* data, size_t size, int maxDimension, size_t maxBytes, SceneImage& out, std::string& error) {
    if (data == nullptr || size == 0 || size > 0x7fffffffu) {
        error = "empty image data";
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &channels)) {
        error = "unsupported image format (only PNG and JPEG are read)";
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

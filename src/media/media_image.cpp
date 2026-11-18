#include "media/media_image.hpp"

#include "scene3d/image_decode.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace nvj {

namespace {

std::string extensionOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return std::string();
    }
    std::string extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

}

bool isImageFileName(const std::string& name) {
    const std::string extension = extensionOf(name);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga" || extension == ".gif";
}

bool isVideoFileName(const std::string& name) {
    const std::string extension = extensionOf(name);
    return extension == ".mp4" || extension == ".mov" || extension == ".m4v" || extension == ".mkv" || extension == ".avi" || extension == ".webm";
}

void premultiplyAlpha(std::vector<uint8_t>& rgba) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const unsigned alpha = rgba[i + 3];
        if (alpha == 255) {
            continue;
        }
        rgba[i] = static_cast<uint8_t>((rgba[i] * alpha + 127) / 255);
        rgba[i + 1] = static_cast<uint8_t>((rgba[i + 1] * alpha + 127) / 255);
        rgba[i + 2] = static_cast<uint8_t>((rgba[i + 2] * alpha + 127) / 255);
    }
}

bool loadMediaImage(const std::filesystem::path& file, SceneImage& out, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) {
        error = "the file cannot be read";
        return false;
    }
    if (size == 0 || size > kMaxMediaFileBytes) {
        error = size == 0 ? "the file is empty" : "the file is larger than 256 MiB";
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        error = "the file cannot be opened";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "the file could not be read completely";
        return false;
    }
    SceneImage image;
    if (!decodeMediaImage(bytes.data(), bytes.size(), kMaxMediaImageDimension, kMaxMediaImageBytes, image, error)) {
        return false;
    }
    premultiplyAlpha(image.rgba);
    out = std::move(image);
    return true;
}

}

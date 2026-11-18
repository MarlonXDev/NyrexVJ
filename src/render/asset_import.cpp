#include "render/asset_import.hpp"

#include "core/log.hpp"
#include "media/media_image.hpp"
#include "render/clip_library.hpp"
#include "scene3d/scene_pack.hpp"

#include <filesystem>
#include <system_error>

namespace nvj {

namespace {

namespace fs = std::filesystem;

bool sameFile(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    const auto sizeA = fs::file_size(a, ec);
    if (ec) {
        return false;
    }
    const auto sizeB = fs::file_size(b, ec);
    if (ec || sizeA != sizeB) {
        return false;
    }
    std::error_code timeEc;
    return fs::last_write_time(a, timeEc) == fs::last_write_time(b, timeEc) || sizeA == 0;
}

fs::path freeName(const fs::path& directory, const fs::path& file) {
    std::error_code ec;
    const std::string stem = pathToUtf8(file.stem());
    const std::string extension = pathToUtf8(file.extension());
    for (int i = 2; i < 1000; ++i) {
        const fs::path candidate = directory / pathFromUtf8(stem + " (" + std::to_string(i) + ")" + extension);
        if (!fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return fs::path();
}

}

ImportResult importAsset(const std::string& sourceUtf8) {
    ImportResult result;
    const fs::path source = pathFromUtf8(sourceUtf8);
    result.name = pathToUtf8(source.filename());
    std::error_code ec;
    if (!fs::is_regular_file(source, ec) || ec) {
        result.kind = ImportKind::Missing;
        result.reason = "not a file";
        return result;
    }
    const std::string name = result.name;
    std::string directoryText;
    if (isImageFileName(name) || isVideoFileName(name)) {
        result.kind = ImportKind::Media;
        directoryText = mediaDirectory();
    } else if (isModelAssetName(name)) {
        result.kind = ImportKind::Model;
        directoryText = modelDirectory();
    } else {
        result.kind = ImportKind::Unsupported;
        result.reason = "unsupported file type";
        return result;
    }
    const auto size = fs::file_size(source, ec);
    if (ec) {
        result.kind = ImportKind::Failed;
        result.reason = ec.message();
        return result;
    }
    if (size > kMaxMediaFileBytes) {
        result.kind = ImportKind::TooLarge;
        result.reason = "file is larger than 256 MiB";
        return result;
    }
    const fs::path directory = pathFromUtf8(directoryText);
    fs::create_directories(directory, ec);
    fs::path target = directory / source.filename();
    if (fs::exists(target, ec)) {
        if (sameFile(source, target)) {
            result.added = false;
            return result;
        }
        target = freeName(directory, source.filename());
        if (target.empty()) {
            result.kind = ImportKind::Failed;
            result.reason = "no free file name";
            return result;
        }
    }
    fs::copy_file(source, target, fs::copy_options::none, ec);
    if (ec) {
        result.kind = ImportKind::Failed;
        result.reason = ec.message();
        logLine("Import of '" + name + "' failed: " + result.reason);
        return result;
    }
    std::error_code timeEc;
    const fs::file_time_type stamp = fs::last_write_time(source, timeEc);
    if (!timeEc) {
        fs::last_write_time(target, stamp, timeEc);
    }
    result.name = pathToUtf8(target.filename());
    result.added = true;
    logLine("Imported '" + result.name + "' into " + directoryText);
    return result;
}

}

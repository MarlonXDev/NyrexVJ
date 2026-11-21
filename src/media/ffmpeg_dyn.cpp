#include "media/ffmpeg_api.hpp"

#include "media/video_decoder.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include "core/log.hpp"
#include "core/settings.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <vector>

#if __has_include(<libswresample/version.h>)
#include <libswresample/version.h>
#define NYREXVJ_SWRESAMPLE_MAJOR LIBSWRESAMPLE_VERSION_MAJOR
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace nvj {

namespace {

namespace fs = std::filesystem;

struct Library {
    const char* base;
    int major;
};

std::vector<Library> libraries() {
    return {
        {"avutil", LIBAVUTIL_VERSION_MAJOR},
        {"swscale", LIBSWSCALE_VERSION_MAJOR},
        {"avcodec", LIBAVCODEC_VERSION_MAJOR},
        {"avformat", LIBAVFORMAT_VERSION_MAJOR},
    };
}

#ifdef NYREXVJ_SWRESAMPLE_MAJOR
const Library kSwresample{"swresample", NYREXVJ_SWRESAMPLE_MAJOR};
#endif

std::string libraryFile(const Library& library) {
#ifdef _WIN32
    return std::string(library.base) + "-" + std::to_string(library.major) + ".dll";
#else
    return "lib" + std::string(library.base) + ".so." + std::to_string(library.major);
#endif
}

#ifdef _WIN32

using Handle = HMODULE;

Handle openLibrary(const fs::path& directory, const std::string& file) {
    const fs::path full = directory / fs::path(std::u8string(file.begin(), file.end()));
    std::error_code ec;
    if (!fs::exists(full, ec)) {
        return nullptr;
    }
    return LoadLibraryExW(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void* symbol(Handle handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
}

std::vector<fs::path> searchFolders() {
    std::vector<fs::path> folders;
    const std::string own = ffmpegDirectory();
    folders.push_back(fs::path(std::u8string(own.begin(), own.end())));
    const std::string exe = platform::currentExePath();
    if (!exe.empty()) {
        const fs::path dir = fs::path(std::u8string(exe.begin(), exe.end())).parent_path();
        folders.push_back(dir / "ffmpeg");
        folders.push_back(dir);
    }
    return folders;
}

#else

using Handle = void*;

Handle openLibrary(const fs::path& directory, const std::string& file) {
    if (directory.empty()) {
        return dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    }
    const fs::path full = directory / file;
    std::error_code ec;
    if (!fs::exists(full, ec)) {
        return nullptr;
    }
    return dlopen(full.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* symbol(Handle handle, const char* name) {
    return dlsym(handle, name);
}

std::vector<fs::path> searchFolders() {
    std::vector<fs::path> folders;
    const char* custom = std::getenv("NYREXVJ_FFMPEG_DIR");
    if (custom != nullptr && *custom != '\0') {
        folders.push_back(fs::path(custom));
    }
    folders.push_back(fs::path(ffmpegDirectory()));
    const std::string exe = platform::currentExePath();
    if (!exe.empty()) {
        const fs::path dir = fs::path(exe).parent_path();
        folders.push_back(dir / "ffmpeg");
        folders.push_back(dir);
    }
    folders.push_back(fs::path());
    return folders;
}

#endif

fs::path& usedFolder() {
    static fs::path folder;
    return folder;
}

bool load(FfmpegApi& api, std::string& problem) {
    const char* disabled = std::getenv("NYREXVJ_NO_FFMPEG");
    if (disabled != nullptr && *disabled != '\0') {
        problem = "disabled by NYREXVJ_NO_FFMPEG";
        return false;
    }
    const std::vector<Library> list = libraries();
    std::vector<Handle> handles(list.size(), nullptr);
    bool found = false;
    for (const fs::path& folder : searchFolders()) {
        std::fill(handles.begin(), handles.end(), nullptr);
        bool all = true;
        for (size_t i = 0; i < list.size(); ++i) {
            handles[i] = openLibrary(folder, libraryFile(list[i]));
            if (handles[i] == nullptr) {
                all = false;
                break;
            }
#ifdef NYREXVJ_SWRESAMPLE_MAJOR
            if (i == 0) {
                openLibrary(folder, libraryFile(kSwresample));
            }
#endif
        }
        if (all) {
            found = true;
            usedFolder() = folder;
            break;
        }
    }
    if (!found) {
        problem = "libraries not found";
        return false;
    }
    const auto find = [&](const char* name) -> void* {
        for (Handle handle : handles) {
            if (void* address = symbol(handle, name)) {
                return address;
            }
        }
        return nullptr;
    };
    bool ok = true;
#define NYX_FN(name)                                                         \
    do {                                                                     \
        api.name = reinterpret_cast<decltype(api.name)>(find(#name));        \
        if (api.name == nullptr) {                                           \
            problem = std::string("function ") + #name + " is missing";      \
            ok = false;                                                      \
        }                                                                    \
    } while (false);
    NYX_FFMPEG_FUNCTIONS(NYX_FN)
#undef NYX_FN
    return ok;
}

}

namespace {

std::mutex& loadMutex() {
    static std::mutex mutex;
    return mutex;
}

bool& retryRequested() {
    static bool requested = false;
    return requested;
}

}

const FfmpegApi* ffmpegApi(std::string& problem) {
    static FfmpegApi api;
    static std::string error;
    static bool ok = false;
    static bool tried = false;
    std::lock_guard<std::mutex> lock(loadMutex());
    if (!ok && (!tried || retryRequested())) {
        tried = true;
        retryRequested() = false;
        FfmpegApi fresh;
        std::string failure;
        if (load(fresh, failure)) {
            api = fresh;
            api.av_log_set_level(AV_LOG_ERROR);
            ok = true;
            error.clear();
            logLine("FFmpeg libraries loaded, video clips are available");
        } else {
            error = failure;
            logLine("FFmpeg libraries were not loaded (" + error + "), video clips are not available");
        }
    }
    problem = error;
    return ok ? &api : nullptr;
}

const FfmpegDeviceApi* ffmpegDeviceApi(std::string& problem) {
    static FfmpegDeviceApi api;
    static std::string error;
    static bool ok = false;
    static bool tried = false;
    std::string base;
    if (ffmpegApi(base) == nullptr) {
        problem = "FFmpeg is not loaded (" + base + ")";
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(loadMutex());
    if (!tried) {
        tried = true;
        const Library library{"avdevice", LIBAVFORMAT_VERSION_MAJOR};
        Handle handle = openLibrary(usedFolder(), libraryFile(library));
#ifndef _WIN32
        if (handle == nullptr) {
            handle = openLibrary(fs::path(), libraryFile(library));
        }
#endif
        if (handle == nullptr) {
            error = libraryFile(library) + " was not found";
        } else {
            api.registerAll = reinterpret_cast<decltype(api.registerAll)>(symbol(handle, "avdevice_register_all"));
            api.listInputSources = reinterpret_cast<decltype(api.listInputSources)>(symbol(handle, "avdevice_list_input_sources"));
            api.freeList = reinterpret_cast<decltype(api.freeList)>(symbol(handle, "avdevice_free_list_devices"));
            if (api.registerAll == nullptr || api.listInputSources == nullptr || api.freeList == nullptr) {
                error = "the functions of " + libraryFile(library) + " are missing";
            } else {
                api.registerAll();
                ok = true;
                error.clear();
                logLine("FFmpeg device library loaded, cameras and capture cards are available");
            }
        }
        if (!ok) {
            logLine("FFmpeg device library was not loaded (" + error + "), cameras are not available");
        }
    }
    problem = error;
    return ok ? &api : nullptr;
}

void retryVideoSupport() {
    std::lock_guard<std::mutex> lock(loadMutex());
    retryRequested() = true;
}

VideoSupport videoSupport() {
    VideoSupport support;
    support.compiled = true;
    support.folder = ffmpegDirectory();
    for (const Library& library : libraries()) {
        support.files.push_back(libraryFile(library));
    }
#ifdef NYREXVJ_SWRESAMPLE_MAJOR
    support.files.push_back(libraryFile(kSwresample));
#endif
    std::string problem;
    support.loaded = ffmpegApi(problem) != nullptr;
    support.problem = problem;
    if (support.loaded) {
        support.loadedFrom = usedFolder().empty() ? std::string() : fs::path(usedFolder()).string();
    }
    return support;
}

}

#endif

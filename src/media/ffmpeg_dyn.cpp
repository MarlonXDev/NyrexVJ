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

Handle openLibrary(const fs::path&, const std::string& file) {
    return dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* symbol(Handle handle, const char* name) {
    return dlsym(handle, name);
}

std::vector<fs::path> searchFolders() {
    return {fs::path()};
}

#endif

bool load(FfmpegApi& api, std::string& problem) {
    const char* disabled = std::getenv("NYREXVJ_NO_FFMPEG");
    if (disabled != nullptr && *disabled != '\0') {
        problem = "disabled by NYREXVJ_NO_FFMPEG";
        return false;
    }
    std::vector<Handle> handles(4, nullptr);
    bool found = false;
    for (const fs::path& folder : searchFolders()) {
        std::fill(handles.begin(), handles.end(), nullptr);
        const std::vector<Library> list = libraries();
        bool all = true;
        for (size_t i = 0; i < list.size(); ++i) {
            handles[i] = openLibrary(folder, libraryFile(list[i]));
            if (handles[i] == nullptr) {
                all = false;
                break;
            }
        }
        if (all) {
            found = true;
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
#ifdef _WIN32
    support.files.push_back("swresample-" + std::to_string(NYREXVJ_SWRESAMPLE_MAJOR) + ".dll");
#else
    support.files.push_back("libswresample.so." + std::to_string(NYREXVJ_SWRESAMPLE_MAJOR));
#endif
#endif
    std::string problem;
    support.loaded = ffmpegApi(problem) != nullptr;
    support.problem = problem;
    return support;
}

}

#endif

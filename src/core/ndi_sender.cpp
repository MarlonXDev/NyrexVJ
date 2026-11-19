#include "core/ndi_sender.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace nvj {

namespace {

struct NdiSendCreate {
    const char* name;
    const char* groups;
    bool clockVideo;
    bool clockAudio;
};

struct NdiVideoFrame {
    int xres;
    int yres;
    uint32_t fourCC;
    int rateN;
    int rateD;
    float aspect;
    int formatType;
    int64_t timecode;
    const uint8_t* data;
    int stride;
    const char* metadata;
    int64_t timestamp;
};

static_assert(sizeof(void*) != 8 || sizeof(NdiVideoFrame) == 72, "NDI video frame layout");
static_assert(sizeof(void*) != 8 || sizeof(NdiSendCreate) == 24, "NDI send create layout");

constexpr uint32_t fourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<unsigned char>(a)) | (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr uint32_t kFourCCRgbx = fourCC('R', 'G', 'B', 'X');
constexpr int kFormatProgressive = 1;
constexpr int64_t kTimecodeSynthesize = INT64_MAX;
constexpr int64_t kTimestampUndefined = INT64_MIN;

using InitializeFn = bool (*)();
using DestroyFn = void (*)();
using SendCreateFn = void* (*)(const NdiSendCreate*);
using SendDestroyFn = void (*)(void*);
using SendVideoFn = void (*)(void*, const NdiVideoFrame*);
using ConnectionsFn = int (*)(void*, uint32_t);

#ifdef _WIN32
using Handle = HMODULE;
Handle openLibrary(const std::string& path) {
    return LoadLibraryA(path.c_str());
}
void* findSymbol(Handle handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(handle, name));
}
#else
using Handle = void*;
Handle openLibrary(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* findSymbol(Handle handle, const char* name) {
    return dlsym(handle, name);
}
#endif

struct Api {
    Handle handle = nullptr;
    std::string path;
    InitializeFn initialize = nullptr;
    DestroyFn destroy = nullptr;
    SendCreateFn sendCreate = nullptr;
    SendDestroyFn sendDestroy = nullptr;
    SendVideoFn sendVideo = nullptr;
    ConnectionsFn connections = nullptr;
    bool initialized = false;

    ~Api() {
        if (initialized && destroy) {
            destroy();
        }
    }
};

std::mutex apiMutex;
Api api;
bool loaded = false;
std::string loadErrorText;
std::string libraryOverride;

std::vector<std::string> candidates() {
    std::vector<std::string> list;
    if (!libraryOverride.empty()) {
        list.push_back(libraryOverride);
        return list;
    }
    if (const char* env = std::getenv("NYREXVJ_NDI_LIB")) {
        if (*env) {
            list.push_back(env);
            return list;
        }
    }
#ifdef _WIN32
    const char* file = "Processing.NDI.Lib.x64.dll";
    if (const char* redist = std::getenv("NDILIB_REDIST_FOLDER")) {
        if (*redist) {
            std::string dir = redist;
            if (dir.back() != '\\' && dir.back() != '/') {
                dir += '\\';
            }
            list.push_back(dir + file);
        }
    }
    list.push_back(file);
#elif defined(__APPLE__)
    list.push_back("libndi.dylib");
    list.push_back("/usr/local/lib/libndi.dylib");
#else
    for (const char* variable : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"}) {
        if (const char* dir = std::getenv(variable)) {
            if (*dir) {
                list.push_back(std::string(dir) + "/libndi.so.6");
                list.push_back(std::string(dir) + "/libndi.so.5");
            }
        }
    }
    list.push_back("libndi.so.6");
    list.push_back("libndi.so.5");
    list.push_back("libndi.so");
#endif
    return list;
}

bool loadLocked() {
    if (loaded) {
        return true;
    }
    loadErrorText.clear();
    for (const std::string& candidate : candidates()) {
        Handle handle = openLibrary(candidate);
        if (!handle) {
            continue;
        }
        Api next;
        next.handle = handle;
        next.path = candidate;
        next.initialize = reinterpret_cast<InitializeFn>(findSymbol(handle, "NDIlib_initialize"));
        next.destroy = reinterpret_cast<DestroyFn>(findSymbol(handle, "NDIlib_destroy"));
        next.sendCreate = reinterpret_cast<SendCreateFn>(findSymbol(handle, "NDIlib_send_create"));
        next.sendDestroy = reinterpret_cast<SendDestroyFn>(findSymbol(handle, "NDIlib_send_destroy"));
        next.sendVideo = reinterpret_cast<SendVideoFn>(findSymbol(handle, "NDIlib_send_send_video_v2"));
        next.connections = reinterpret_cast<ConnectionsFn>(findSymbol(handle, "NDIlib_send_get_no_connections"));
        if (!next.initialize || !next.sendCreate || !next.sendDestroy || !next.sendVideo) {
            loadErrorText = candidate + ": functions missing";
            continue;
        }
        if (!next.initialize()) {
            loadErrorText = candidate + ": NDI is not supported on this computer";
            continue;
        }
        next.initialized = true;
        api = next;
        next.initialized = false;
        loaded = true;
        logLine("NDI runtime loaded: " + candidate);
        return true;
    }
    if (loadErrorText.empty()) {
        loadErrorText = "NDI runtime not found";
    }
    return false;
}

}

struct NdiSender::Impl {
    void* sender = nullptr;
    std::string name;
    std::string error;
    std::vector<uint8_t> frame;
};

NdiSender::NdiSender() : impl_(std::make_unique<Impl>()) {}

NdiSender::~NdiSender() {
    stop();
}

bool NdiSender::available() {
    std::lock_guard<std::mutex> lock(apiMutex);
    return loadLocked();
}

void NdiSender::recheck() {
    std::lock_guard<std::mutex> lock(apiMutex);
    if (!loaded) {
        loadLocked();
    }
}

std::string NdiSender::loadedLibrary() {
    std::lock_guard<std::mutex> lock(apiMutex);
    return loaded ? api.path : std::string();
}

std::string NdiSender::loadError() {
    std::lock_guard<std::mutex> lock(apiMutex);
    return loaded ? std::string() : loadErrorText;
}

void NdiSender::setLibraryOverride(const std::string& path) {
    std::lock_guard<std::mutex> lock(apiMutex);
    libraryOverride = path;
}

bool NdiSender::start(const std::string& name) {
    stop();
    impl_->error.clear();
    std::lock_guard<std::mutex> lock(apiMutex);
    if (!loadLocked()) {
        impl_->error = loadErrorText;
        return false;
    }
    impl_->name = name.empty() ? "NyrexVJ" : name.substr(0, 60);
    NdiSendCreate create{};
    create.name = impl_->name.c_str();
    create.groups = nullptr;
    create.clockVideo = false;
    create.clockAudio = false;
    impl_->sender = api.sendCreate(&create);
    if (!impl_->sender) {
        impl_->error = "NDI sender could not be created";
        return false;
    }
    return true;
}

void NdiSender::stop() {
    if (!impl_->sender) {
        return;
    }
    std::lock_guard<std::mutex> lock(apiMutex);
    api.sendDestroy(impl_->sender);
    impl_->sender = nullptr;
}

bool NdiSender::active() const {
    return impl_->sender != nullptr;
}

int NdiSender::connections() const {
    if (!impl_->sender) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(apiMutex);
    if (!api.connections) {
        return 1;
    }
    return std::max(0, api.connections(impl_->sender, 0));
}

bool NdiSender::sendRgbaBottomUp(const uint8_t* rgba, int width, int height) {
    if (!impl_->sender || !rgba || width <= 0 || height <= 0) {
        return false;
    }
    const size_t stride = static_cast<size_t>(width) * 4u;
    impl_->frame.resize(stride * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        std::memcpy(impl_->frame.data() + static_cast<size_t>(y) * stride, rgba + static_cast<size_t>(height - 1 - y) * stride, stride);
    }
    NdiVideoFrame frame{};
    frame.xres = width;
    frame.yres = height;
    frame.fourCC = kFourCCRgbx;
    frame.rateN = 60;
    frame.rateD = 1;
    frame.aspect = static_cast<float>(width) / static_cast<float>(height);
    frame.formatType = kFormatProgressive;
    frame.timecode = kTimecodeSynthesize;
    frame.data = impl_->frame.data();
    frame.stride = static_cast<int>(stride);
    frame.metadata = nullptr;
    frame.timestamp = kTimestampUndefined;
    std::lock_guard<std::mutex> lock(apiMutex);
    api.sendVideo(impl_->sender, &frame);
    return true;
}

const std::string& NdiSender::name() const {
    return impl_->name;
}

const std::string& NdiSender::error() const {
    return impl_->error;
}

}

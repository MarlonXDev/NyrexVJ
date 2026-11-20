#include "core/ndi_runtime.hpp"

#include "core/log.hpp"

#include <cstdlib>
#include <mutex>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace nvj {
namespace ndi {

namespace {

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

struct State {
    Handle handle = nullptr;
    std::string path;
    Api api;
    bool initialized = false;

    ~State() {
        if (initialized && api.destroy) {
            api.destroy();
        }
    }
};

std::mutex stateMutex;
State state;
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

template <typename Fn>
Fn symbol(Handle handle, const char* name) {
    return reinterpret_cast<Fn>(findSymbol(handle, name));
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
        State next;
        next.handle = handle;
        next.path = candidate;
        Api& api = next.api;
        api.initialize = symbol<InitializeFn>(handle, "NDIlib_initialize");
        api.destroy = symbol<DestroyFn>(handle, "NDIlib_destroy");
        api.sendCreate = symbol<SendCreateFn>(handle, "NDIlib_send_create");
        api.sendDestroy = symbol<SendDestroyFn>(handle, "NDIlib_send_destroy");
        api.sendVideo = symbol<SendVideoFn>(handle, "NDIlib_send_send_video_v2");
        api.sendConnections = symbol<ConnectionsFn>(handle, "NDIlib_send_get_no_connections");
        api.findCreate = symbol<FindCreateFn>(handle, "NDIlib_find_create_v2");
        api.findDestroy = symbol<FindDestroyFn>(handle, "NDIlib_find_destroy");
        api.findWait = symbol<FindWaitFn>(handle, "NDIlib_find_wait_for_sources");
        api.findSources = symbol<FindSourcesFn>(handle, "NDIlib_find_get_current_sources");
        api.recvCreate = symbol<RecvCreateFn>(handle, "NDIlib_recv_create_v3");
        api.recvDestroy = symbol<RecvDestroyFn>(handle, "NDIlib_recv_destroy");
        api.recvCapture = symbol<RecvCaptureFn>(handle, "NDIlib_recv_capture_v2");
        api.recvFreeVideo = symbol<RecvFreeVideoFn>(handle, "NDIlib_recv_free_video_v2");
        api.recvConnections = symbol<RecvConnectionsFn>(handle, "NDIlib_recv_get_no_connections");
        if (!api.initialize || (!api.canSend() && !api.canReceive())) {
            loadErrorText = candidate + ": functions missing";
            continue;
        }
        if (!api.initialize()) {
            loadErrorText = candidate + ": NDI is not supported on this computer";
            continue;
        }
        next.initialized = true;
        state.handle = next.handle;
        state.path = next.path;
        state.api = next.api;
        state.initialized = true;
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

bool available() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return loadLocked();
}

void recheck() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!loaded) {
        loadLocked();
    }
}

std::string loadedLibrary() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return loaded ? state.path : std::string();
}

std::string loadError() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return loaded ? std::string() : loadErrorText;
}

void setLibraryOverride(const std::string& path) {
    std::lock_guard<std::mutex> lock(stateMutex);
    libraryOverride = path;
}

bool load(Api& out) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!loadLocked()) {
        return false;
    }
    out = state.api;
    return true;
}

bool loadForSending(Api& out, std::string& error) {
    if (!load(out)) {
        error = loadError();
        return false;
    }
    if (!out.canSend()) {
        error = "This NDI runtime cannot send";
        return false;
    }
    return true;
}

bool loadForReceiving(Api& out, std::string& error) {
    if (!load(out)) {
        error = loadError();
        return false;
    }
    if (!out.canReceive()) {
        error = "This NDI runtime cannot receive";
        return false;
    }
    return true;
}

}
}

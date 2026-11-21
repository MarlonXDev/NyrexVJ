#include "core/dynlib.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace nvj {

bool DynamicLibrary::open(const std::filesystem::path& path, std::string& error) {
    if (handle_ != nullptr) {
        return true;
    }
#ifdef _WIN32
    const std::filesystem::path full = std::filesystem::absolute(path);
    HMODULE module = LoadLibraryExW(full.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        error = "LoadLibrary failed with error " + std::to_string(GetLastError());
        return false;
    }
    handle_ = reinterpret_cast<void*>(module);
#else
    const std::filesystem::path full = std::filesystem::absolute(path);
    void* handle = dlopen(full.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* message = dlerror();
        error = message != nullptr ? message : "dlopen failed";
        return false;
    }
    handle_ = handle;
#endif
    return true;
}

void* DynamicLibrary::symbol(const char* name) const {
    if (handle_ == nullptr) {
        return nullptr;
    }
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

}

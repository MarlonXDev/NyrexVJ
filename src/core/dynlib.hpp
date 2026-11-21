#pragma once

#include <filesystem>
#include <string>

namespace nvj {

class DynamicLibrary {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    void* symbol(const char* name) const;
    bool isOpen() const { return handle_ != nullptr; }

private:
    void* handle_ = nullptr;
};

}

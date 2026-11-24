#pragma once

#include "core/output_map.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace nvj {

constexpr size_t kMaxMaskFileBytes = 16u * 1024u * 1024u;

class MaskImages {
public:
    static MaskImages& instance();

    std::filesystem::path folder() const;
    std::shared_ptr<const MaskBitmap> get(const std::string& name);
    std::string error(const std::string& name) const;
    bool loading(const std::string& name) const;
    bool importFile(const std::string& sourcePath, std::string& nameOut, std::string& error);
    bool waitFor(const std::string& name, int milliseconds);
    void reload(const std::string& name);
    void clear();

private:
    struct Entry {
        std::shared_ptr<const MaskBitmap> bitmap;
        std::string error;
        std::shared_future<void> job;
        bool running = false;
        std::chrono::steady_clock::time_point failedAt;
    };

    void start(const std::string& name, Entry& entry);

    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;
};

bool loadMaskFile(const std::filesystem::path& path, MaskBitmap& out, std::string& error);

}

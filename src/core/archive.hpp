#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nvj::archive {

struct Options {
    std::function<std::string(const std::string& name, bool directory)> map;
    std::function<bool(unsigned long long done, unsigned long long total)> progress;
    unsigned long long maxTotalBytes = 1024ull * 1024ull * 1024ull;
    unsigned long long maxEntryBytes = 512ull * 1024ull * 1024ull;
    size_t maxEntries = 20000;
};

struct Result {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<std::string> files;
    unsigned long long bytes = 0;
};

bool safeRelativePath(const std::string& path);
bool safeLinkTarget(const std::string& target);
bool xzAvailable();

Result extractZip(const std::filesystem::path& archive, const std::filesystem::path& destination, const Options& options);
Result extractTarXz(const std::filesystem::path& archive, const std::filesystem::path& destination, const Options& options);
Result extractTar(const std::filesystem::path& archive, const std::filesystem::path& destination, const Options& options);

unsigned int crc32Update(unsigned int crc, const unsigned char* data, size_t size);
bool inflateRaw(const unsigned char* input, size_t inputSize, std::vector<unsigned char>& output, unsigned long long maxOutput, std::string& error);

}

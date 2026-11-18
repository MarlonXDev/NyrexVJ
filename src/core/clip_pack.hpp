#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

constexpr uint32_t kPackVersion = 1;

struct PackEntry {
    std::string name;
    std::vector<uint8_t> data;
};

struct PackLimits {
    size_t maxFileBytes = 256u * 1024u * 1024u;
    size_t maxEntries = 256;
    size_t maxEntryBytes = 128u * 1024u * 1024u;
    size_t maxSceneBytes = 4u * 1024u * 1024u;
};

bool isSafePackName(const std::string& name);
bool writePack(const std::vector<PackEntry>& entries, std::vector<uint8_t>& out, std::string& error, const PackLimits& limits = {});
bool readPack(const uint8_t* data, size_t size, std::vector<PackEntry>& entries, std::string& error, const PackLimits& limits = {});

}

#include "core/clip_pack.hpp"

#include "core/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <set>

namespace nvj {

namespace {

constexpr char kMagic[8] = {'N', 'Y', 'X', 'C', 'L', 'I', 'P', '1'};
constexpr size_t kHeaderBytes = 16;
constexpr size_t kMaxNameBytes = 128;
constexpr size_t kHashBytes = 32;

void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

void putU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t getU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    return v;
}

std::array<uint8_t, kHashBytes> hashOf(const uint8_t* data, size_t size) {
    const std::string hex = sha256Hex(data, size);
    std::array<uint8_t, kHashBytes> out{};
    for (size_t i = 0; i < kHashBytes && i * 2 + 1 < hex.size(); ++i) {
        const auto nibble = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
        out[i] = static_cast<uint8_t>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    }
    return out;
}

std::string lowerText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

size_t entryLimit(const std::string& name, const PackLimits& limits) {
    return name == "scene.json" ? limits.maxSceneBytes : limits.maxEntryBytes;
}

}

bool isSafePackName(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameBytes || name.front() == '/' || name.back() == '/') {
        return false;
    }
    size_t segment = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/') {
            const std::string part = name.substr(segment, i - segment);
            if (part.empty() || part == "." || part == ".." || part.front() == ' ' || part.back() == ' ' || part.back() == '.') {
                return false;
            }
            segment = i + 1;
            continue;
        }
        const unsigned char u = static_cast<unsigned char>(name[i]);
        const char c = name[i];
        if (u < 0x20 || u == 0x7f || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

bool writePack(const std::vector<PackEntry>& entries, std::vector<uint8_t>& out, std::string& error, const PackLimits& limits) {
    out.clear();
    if (entries.size() > limits.maxEntries) {
        error = "too many entries for a clip pack";
        return false;
    }
    std::set<std::string> seen;
    size_t directory = 0;
    size_t total = 0;
    for (const PackEntry& entry : entries) {
        if (!isSafePackName(entry.name)) {
            error = "unsafe entry name '" + entry.name + "'";
            return false;
        }
        if (!seen.insert(lowerText(entry.name)).second) {
            error = "duplicate entry name '" + entry.name + "'";
            return false;
        }
        if (entry.data.size() > entryLimit(entry.name, limits)) {
            error = "entry '" + entry.name + "' is too large";
            return false;
        }
        directory += 2 + entry.name.size() + 8 + 8 + kHashBytes;
        total += entry.data.size();
    }
    if (kHeaderBytes + directory + total > limits.maxFileBytes) {
        error = "the clip pack would be too large";
        return false;
    }
    out.reserve(kHeaderBytes + directory + total);
    out.insert(out.end(), kMagic, kMagic + 8);
    putU32(out, kPackVersion);
    putU32(out, static_cast<uint32_t>(entries.size()));
    uint64_t offset = kHeaderBytes + directory;
    for (const PackEntry& entry : entries) {
        putU16(out, static_cast<uint16_t>(entry.name.size()));
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        putU64(out, entry.data.size());
        putU64(out, offset);
        const auto hash = hashOf(entry.data.data(), entry.data.size());
        out.insert(out.end(), hash.begin(), hash.end());
        offset += entry.data.size();
    }
    for (const PackEntry& entry : entries) {
        out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    return true;
}

bool readPack(const uint8_t* data, size_t size, std::vector<PackEntry>& entries, std::string& error, const PackLimits& limits) {
    entries.clear();
    if (data == nullptr || size < kHeaderBytes) {
        error = "the file is too small to be a clip pack";
        return false;
    }
    if (size > limits.maxFileBytes) {
        error = "the clip pack is too large";
        return false;
    }
    if (std::memcmp(data, kMagic, 8) != 0) {
        error = "this is not a NyrexVJ clip pack";
        return false;
    }
    const uint32_t version = getU32(data + 8);
    if (version == 0) {
        error = "the clip pack has an invalid version";
        return false;
    }
    if (version > kPackVersion) {
        error = "the clip pack was made with a newer NyrexVJ (pack version " + std::to_string(version) + ")";
        return false;
    }
    const uint32_t count = getU32(data + 12);
    if (count > limits.maxEntries) {
        error = "the clip pack has too many entries";
        return false;
    }
    struct Raw {
        std::string name;
        uint64_t size = 0;
        uint64_t offset = 0;
        std::array<uint8_t, kHashBytes> hash{};
    };
    std::vector<Raw> raw;
    size_t cursor = kHeaderBytes;
    std::set<std::string> seen;
    for (uint32_t i = 0; i < count; ++i) {
        if (size - cursor < 2) {
            error = "the clip pack directory is truncated";
            return false;
        }
        const size_t nameLength = getU16(data + cursor);
        cursor += 2;
        if (nameLength == 0 || nameLength > kMaxNameBytes || size - cursor < nameLength + 8 + 8 + kHashBytes) {
            error = "the clip pack directory is truncated or has an invalid name length";
            return false;
        }
        Raw r;
        r.name.assign(reinterpret_cast<const char*>(data + cursor), nameLength);
        cursor += nameLength;
        r.size = getU64(data + cursor);
        cursor += 8;
        r.offset = getU64(data + cursor);
        cursor += 8;
        std::memcpy(r.hash.data(), data + cursor, kHashBytes);
        cursor += kHashBytes;
        if (!isSafePackName(r.name)) {
            error = "the clip pack contains an unsafe entry name";
            return false;
        }
        if (!seen.insert(lowerText(r.name)).second) {
            error = "the clip pack contains a duplicate entry name";
            return false;
        }
        raw.push_back(std::move(r));
    }
    const size_t dataStart = cursor;
    std::vector<size_t> order(raw.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    for (const Raw& r : raw) {
        if (r.size > entryLimit(r.name, limits)) {
            error = "entry '" + r.name + "' is larger than allowed";
            return false;
        }
        if (r.offset < dataStart || r.offset > size || r.size > size - r.offset) {
            error = "entry '" + r.name + "' lies outside the file";
            return false;
        }
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return raw[a].offset < raw[b].offset; });
    uint64_t previousEnd = dataStart;
    for (const size_t index : order) {
        const Raw& r = raw[index];
        if (r.offset < previousEnd) {
            error = "entries of the clip pack overlap";
            return false;
        }
        previousEnd = r.offset + r.size;
    }
    entries.reserve(raw.size());
    for (const Raw& r : raw) {
        const uint8_t* begin = data + r.offset;
        if (hashOf(begin, static_cast<size_t>(r.size)) != r.hash) {
            entries.clear();
            error = "entry '" + r.name + "' is damaged (checksum does not match)";
            return false;
        }
        PackEntry entry;
        entry.name = r.name;
        entry.data.assign(begin, begin + r.size);
        entries.push_back(std::move(entry));
    }
    return true;
}

}

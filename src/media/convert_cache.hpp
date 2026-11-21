#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace nvj::hapcache {

constexpr int kConverterVersion = 1;

struct Entry {
    std::string key;
    std::string source;
    std::string fingerprint;
    std::string tag;
    unsigned long long sourceBytes = 0;
    std::int64_t sourceMtime = 0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int frames = 0;
    std::string format;
    bool alphaKept = false;
    unsigned long long bytes = 0;
    double convertSeconds = 0.0;
    std::int64_t created = 0;
    std::int64_t lastUsed = 0;
    int converter = kConverterVersion;
    bool bad = false;
    std::string badReason;
};

struct SourceStamp {
    unsigned long long bytes = 0;
    std::int64_t mtime = 0;
    bool ok = false;
};

SourceStamp stampOf(const std::string& sourceUtf8);
bool fingerprintOf(const std::string& sourceUtf8, std::string& fingerprint, std::string& error);
std::string settingsTag(const std::string& formatKey, bool snappy);
std::string keyFor(const std::string& fingerprint, const std::string& tag);
std::int64_t nowSeconds();

class Cache {
public:
    explicit Cache(std::filesystem::path directory);

    const std::filesystem::path& directory() const { return directory_; }
    std::filesystem::path dataPath(const std::string& key) const;
    std::filesystem::path partPath(const std::string& key) const;
    std::filesystem::path metaPath(const std::string& key) const;

    bool find(const std::string& key, Entry& entry) const;
    bool commit(Entry entry);
    bool markBad(const std::string& key, const std::string& reason);
    bool touch(const std::string& key);
    bool remove(const std::string& key);

    std::vector<Entry> list() const;
    unsigned long long totalBytes() const;
    int evict(unsigned long long limitBytes, const std::set<std::string>& keep);
    int clear(const std::set<std::string>& keep);
    int cleanStale(std::int64_t maxPartAgeSeconds, const std::set<std::string>& activeKeys);

private:
    std::filesystem::path directory_;
};

std::string serializeEntry(const Entry& entry);
bool parseEntry(const std::string& text, Entry& entry);

}

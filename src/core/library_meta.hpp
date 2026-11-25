#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nvj {

constexpr size_t kMaxTagsPerClip = 8;
constexpr size_t kMaxTagLength = 24;
constexpr size_t kMaxMetaClips = 4096;
constexpr size_t kMaxMetaFileBytes = 512u * 1024u;

std::string normalizeTag(const std::string& text);

struct ClipMeta {
    bool favorite = false;
    std::vector<std::string> tags;
};

struct LibraryQuery {
    std::vector<std::string> words;
    std::vector<std::string> tags;
    bool favoritesOnly = false;
};

LibraryQuery parseLibraryQuery(const std::string& text);

class LibraryMeta {
public:
    bool favorite(const std::string& clip) const;
    void setFavorite(const std::string& clip, bool on);
    const std::vector<std::string>& tags(const std::string& clip) const;
    bool addTag(const std::string& clip, const std::string& tag);
    bool removeTag(const std::string& clip, const std::string& tag);
    std::vector<std::string> allTags() const;
    bool matches(const std::string& clip, const LibraryQuery& query) const;
    size_t size() const { return entries_.size(); }
    bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }
    void rename(const std::string& from, const std::string& to);

    std::string toJson() const;
    bool fromJson(const std::string& text, std::string& error);

private:
    void prune(const std::string& clip);

    std::map<std::string, ClipMeta> entries_;
    bool dirty_ = false;
};

bool loadLibraryMetaFile(LibraryMeta& meta, const std::filesystem::path& path, std::string& error);
bool saveLibraryMetaFile(const LibraryMeta& meta, const std::filesystem::path& path, std::string& error);

}

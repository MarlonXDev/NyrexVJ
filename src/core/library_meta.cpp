#include "core/library_meta.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace nvj {

namespace {

const std::vector<std::string> kNoTags;

char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowerText(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = lowerAscii(c);
    }
    return out;
}

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}

std::string normalizeTag(const std::string& text) {
    std::string out;
    bool pendingDash = false;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (isBlank(c)) {
            pendingDash = !out.empty();
            continue;
        }
        if (u < 0x20 || u == 0x7F || c == '"' || c == '\\') {
            continue;
        }
        if (out.empty() && c == '#') {
            continue;
        }
        if (pendingDash) {
            out.push_back('-');
            pendingDash = false;
        }
        out.push_back(lowerAscii(c));
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    if (out.size() > kMaxTagLength) {
        size_t cut = kMaxTagLength;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
    }
    return out;
}

LibraryQuery parseLibraryQuery(const std::string& text) {
    LibraryQuery query;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isBlank(text[i])) {
            ++i;
        }
        size_t end = i;
        while (end < text.size() && !isBlank(text[end])) {
            ++end;
        }
        if (end > i) {
            const std::string word = lowerText(text.substr(i, end - i));
            if (word.rfind("tag:", 0) == 0) {
                const std::string tag = normalizeTag(word.substr(4));
                if (!tag.empty()) {
                    query.tags.push_back(tag);
                }
            } else if (word == "is:fav" || word == "is:favorite" || word == "is:favourite") {
                query.favoritesOnly = true;
            } else {
                query.words.push_back(word);
            }
        }
        i = end;
    }
    return query;
}

bool LibraryMeta::favorite(const std::string& clip) const {
    const auto it = entries_.find(clip);
    return it != entries_.end() && it->second.favorite;
}

void LibraryMeta::setFavorite(const std::string& clip, bool on) {
    if (clip.empty() || clip.size() > 256) {
        return;
    }
    if (on && entries_.find(clip) == entries_.end() && entries_.size() >= kMaxMetaClips) {
        return;
    }
    const bool before = favorite(clip);
    if (before == on) {
        return;
    }
    entries_[clip].favorite = on;
    dirty_ = true;
    prune(clip);
}

const std::vector<std::string>& LibraryMeta::tags(const std::string& clip) const {
    const auto it = entries_.find(clip);
    return it == entries_.end() ? kNoTags : it->second.tags;
}

bool LibraryMeta::addTag(const std::string& clip, const std::string& tag) {
    const std::string clean = normalizeTag(tag);
    if (clean.empty() || clip.empty() || clip.size() > 256) {
        return false;
    }
    const auto found = entries_.find(clip);
    if (found == entries_.end() && entries_.size() >= kMaxMetaClips) {
        return false;
    }
    ClipMeta& meta = entries_[clip];
    if (std::find(meta.tags.begin(), meta.tags.end(), clean) != meta.tags.end()) {
        prune(clip);
        return false;
    }
    if (meta.tags.size() >= kMaxTagsPerClip) {
        prune(clip);
        return false;
    }
    meta.tags.push_back(clean);
    dirty_ = true;
    return true;
}

bool LibraryMeta::removeTag(const std::string& clip, const std::string& tag) {
    const auto it = entries_.find(clip);
    if (it == entries_.end()) {
        return false;
    }
    const std::string clean = normalizeTag(tag);
    auto& list = it->second.tags;
    const auto pos = std::find(list.begin(), list.end(), clean);
    if (pos == list.end()) {
        return false;
    }
    list.erase(pos);
    dirty_ = true;
    prune(clip);
    return true;
}

std::vector<std::string> LibraryMeta::allTags() const {
    std::set<std::string> unique;
    for (const auto& entry : entries_) {
        unique.insert(entry.second.tags.begin(), entry.second.tags.end());
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

bool LibraryMeta::matches(const std::string& clip, const LibraryQuery& query) const {
    const auto it = entries_.find(clip);
    const bool fav = it != entries_.end() && it->second.favorite;
    if (query.favoritesOnly && !fav) {
        return false;
    }
    const std::vector<std::string>& clipTags = it == entries_.end() ? kNoTags : it->second.tags;
    for (const std::string& wanted : query.tags) {
        const bool any = std::any_of(clipTags.begin(), clipTags.end(), [&](const std::string& tag) { return tag.rfind(wanted, 0) == 0; });
        if (!any) {
            return false;
        }
    }
    if (query.words.empty()) {
        return true;
    }
    const std::string name = lowerText(clip);
    for (const std::string& word : query.words) {
        if (name.find(word) != std::string::npos) {
            continue;
        }
        const bool inTag = std::any_of(clipTags.begin(), clipTags.end(), [&](const std::string& tag) { return tag.find(word) != std::string::npos; });
        if (!inTag) {
            return false;
        }
    }
    return true;
}

void LibraryMeta::rename(const std::string& from, const std::string& to) {
    if (from == to || to.empty() || to.size() > 256) {
        return;
    }
    const auto it = entries_.find(from);
    if (it == entries_.end() || entries_.find(to) != entries_.end()) {
        return;
    }
    entries_[to] = it->second;
    entries_.erase(from);
    dirty_ = true;
}

void LibraryMeta::prune(const std::string& clip) {
    const auto it = entries_.find(clip);
    if (it != entries_.end() && !it->second.favorite && it->second.tags.empty()) {
        entries_.erase(it);
    }
}

std::string LibraryMeta::toJson() const {
    nlohmann::json doc;
    doc["format"] = "nyrexvj-library";
    doc["version"] = 1;
    nlohmann::json clips = nlohmann::json::object();
    for (const auto& entry : entries_) {
        nlohmann::json item;
        if (entry.second.favorite) {
            item["favorite"] = true;
        }
        if (!entry.second.tags.empty()) {
            item["tags"] = entry.second.tags;
        }
        clips[entry.first] = item;
    }
    doc["clips"] = clips;
    return doc.dump(2);
}

bool LibraryMeta::fromJson(const std::string& text, std::string& error) {
    if (text.size() > kMaxMetaFileBytes) {
        error = "The library file is too large";
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (!doc.is_object() || doc.value("format", std::string()) != "nyrexvj-library") {
        error = "This is not a library file";
        return false;
    }
    const int version = doc.value("version", 0);
    if (version < 1) {
        error = "The library file has no valid version";
        return false;
    }
    if (version > 1) {
        error = "The library file was written by a newer version of NyrexVJ (format version " + std::to_string(version) + ")";
        return false;
    }
    const auto clips = doc.find("clips");
    if (clips == doc.end() || !clips->is_object()) {
        error = "The library file has no clip list";
        return false;
    }
    std::map<std::string, ClipMeta> parsed;
    for (auto it = clips->begin(); it != clips->end(); ++it) {
        if (parsed.size() >= kMaxMetaClips) {
            break;
        }
        if (it.key().empty() || it.key().size() > 256 || !it.value().is_object()) {
            continue;
        }
        ClipMeta meta;
        const auto fav = it.value().find("favorite");
        meta.favorite = fav != it.value().end() && fav->is_boolean() && fav->get<bool>();
        const auto tags = it.value().find("tags");
        if (tags != it.value().end() && tags->is_array()) {
            for (const nlohmann::json& tag : *tags) {
                if (!tag.is_string() || meta.tags.size() >= kMaxTagsPerClip) {
                    continue;
                }
                const std::string clean = normalizeTag(tag.get<std::string>());
                if (!clean.empty() && std::find(meta.tags.begin(), meta.tags.end(), clean) == meta.tags.end()) {
                    meta.tags.push_back(clean);
                }
            }
        }
        if (meta.favorite || !meta.tags.empty()) {
            parsed[it.key()] = meta;
        }
    }
    entries_ = std::move(parsed);
    dirty_ = false;
    return true;
}

bool loadLibraryMetaFile(LibraryMeta& meta, const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxMetaFileBytes) {
        error = "The library file is too large";
    } else {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        if (meta.fromJson(buffer.str(), error)) {
            return true;
        }
    }
    std::filesystem::path broken = path;
    broken.replace_extension(".broken.json");
    std::filesystem::rename(path, broken, ec);
    return false;
}

bool saveLibraryMetaFile(const LibraryMeta& meta, const std::filesystem::path& path, std::string& error) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "The library file could not be written";
            return false;
        }
        file << meta.toJson();
        if (!file.good()) {
            error = "The library file could not be written";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "The library file could not be replaced";
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

}

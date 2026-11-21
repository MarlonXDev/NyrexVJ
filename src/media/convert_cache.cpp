#include "media/convert_cache.hpp"

#include "core/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace nvj::hapcache {

namespace fs = std::filesystem;

namespace {

fs::path pathOf(const std::string& utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8Of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string clean(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return out;
}

bool readAll(const fs::path& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

bool isKey(const std::string& name) {
    if (name.size() != 32) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string formatDouble(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

}

std::int64_t nowSeconds() {
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

SourceStamp stampOf(const std::string& sourceUtf8) {
    SourceStamp stamp;
    std::error_code ec;
    const fs::path path = pathOf(sourceUtf8);
    const auto size = fs::file_size(path, ec);
    if (ec) {
        return stamp;
    }
    const auto time = fs::last_write_time(path, ec);
    if (ec) {
        return stamp;
    }
    stamp.bytes = static_cast<unsigned long long>(size);
    stamp.mtime = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
    stamp.ok = true;
    return stamp;
}

bool fingerprintOf(const std::string& sourceUtf8, std::string& fingerprint, std::string& error) {
    constexpr std::size_t kSlice = 1024 * 1024;
    const fs::path path = pathOf(sourceUtf8);
    std::error_code ec;
    const auto size = static_cast<unsigned long long>(fs::file_size(path, ec));
    if (ec) {
        error = "The file cannot be read";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "The file cannot be opened";
        return false;
    }
    Sha256 hash;
    const std::string head = "size:" + std::to_string(size) + "|";
    hash.update(reinterpret_cast<const unsigned char*>(head.data()), head.size());
    std::vector<unsigned char> buffer(kSlice);
    const unsigned long long first = std::min<unsigned long long>(size, kSlice);
    in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(first));
    if (static_cast<unsigned long long>(in.gcount()) != first) {
        error = "The file cannot be read completely";
        return false;
    }
    hash.update(buffer.data(), static_cast<std::size_t>(first));
    if (size > kSlice) {
        const unsigned long long rest = size - kSlice;
        const unsigned long long last = std::min<unsigned long long>(rest, kSlice);
        in.clear();
        in.seekg(static_cast<std::streamoff>(size - last), std::ios::beg);
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(last));
        if (static_cast<unsigned long long>(in.gcount()) != last) {
            error = "The file cannot be read completely";
            return false;
        }
        hash.update(buffer.data(), static_cast<std::size_t>(last));
    }
    fingerprint = hash.finalHex();
    return true;
}

std::string settingsTag(const std::string& formatKey, bool snappy) {
    return "v" + std::to_string(kConverterVersion) + "|" + formatKey + "|" + (snappy ? "snappy" : "raw");
}

std::string keyFor(const std::string& fingerprint, const std::string& tag) {
    return sha256Hex(fingerprint + "|" + tag).substr(0, 32);
}

std::string serializeEntry(const Entry& entry) {
    std::ostringstream out;
    out << "key=" << entry.key << "\n";
    out << "source=" << clean(entry.source) << "\n";
    out << "fingerprint=" << entry.fingerprint << "\n";
    out << "tag=" << clean(entry.tag) << "\n";
    out << "sourceBytes=" << entry.sourceBytes << "\n";
    out << "sourceMtime=" << entry.sourceMtime << "\n";
    out << "width=" << entry.width << "\n";
    out << "height=" << entry.height << "\n";
    out << "fps=" << formatDouble(entry.fps) << "\n";
    out << "frames=" << entry.frames << "\n";
    out << "format=" << clean(entry.format) << "\n";
    out << "alpha=" << (entry.alphaKept ? 1 : 0) << "\n";
    out << "bytes=" << entry.bytes << "\n";
    out << "convertSeconds=" << formatDouble(entry.convertSeconds) << "\n";
    out << "created=" << entry.created << "\n";
    out << "lastUsed=" << entry.lastUsed << "\n";
    out << "converter=" << entry.converter << "\n";
    out << "bad=" << (entry.bad ? 1 : 0) << "\n";
    out << "badReason=" << clean(entry.badReason) << "\n";
    return out.str();
}

bool parseEntry(const std::string& text, Entry& entry) {
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    auto text_of = [&](const char* name) {
        const auto it = values.find(name);
        return it == values.end() ? std::string() : it->second;
    };
    auto number = [&](const char* name) {
        return std::strtoll(text_of(name).c_str(), nullptr, 10);
    };
    Entry parsed;
    parsed.key = text_of("key");
    if (!isKey(parsed.key)) {
        return false;
    }
    parsed.source = text_of("source");
    parsed.fingerprint = text_of("fingerprint");
    parsed.tag = text_of("tag");
    parsed.sourceBytes = static_cast<unsigned long long>(std::strtoull(text_of("sourceBytes").c_str(), nullptr, 10));
    parsed.sourceMtime = number("sourceMtime");
    parsed.width = static_cast<int>(number("width"));
    parsed.height = static_cast<int>(number("height"));
    parsed.fps = std::strtod(text_of("fps").c_str(), nullptr);
    parsed.frames = static_cast<int>(number("frames"));
    parsed.format = text_of("format");
    parsed.alphaKept = number("alpha") != 0;
    parsed.bytes = static_cast<unsigned long long>(std::strtoull(text_of("bytes").c_str(), nullptr, 10));
    parsed.convertSeconds = std::strtod(text_of("convertSeconds").c_str(), nullptr);
    parsed.created = number("created");
    parsed.lastUsed = number("lastUsed");
    parsed.converter = static_cast<int>(number("converter"));
    parsed.bad = number("bad") != 0;
    parsed.badReason = text_of("badReason");
    entry = parsed;
    return true;
}

Cache::Cache(fs::path directory) : directory_(std::move(directory)) {
    std::error_code ec;
    fs::create_directories(directory_, ec);
}

fs::path Cache::dataPath(const std::string& key) const {
    return directory_ / (key + ".mov");
}

fs::path Cache::partPath(const std::string& key) const {
    return directory_ / (key + ".part");
}

fs::path Cache::metaPath(const std::string& key) const {
    return directory_ / (key + ".meta");
}

bool Cache::find(const std::string& key, Entry& entry) const {
    if (!isKey(key)) {
        return false;
    }
    std::string text;
    if (!readAll(metaPath(key), text)) {
        return false;
    }
    Entry parsed;
    if (!parseEntry(text, parsed) || parsed.key != key) {
        return false;
    }
    if (parsed.converter != kConverterVersion) {
        return false;
    }
    std::error_code ec;
    const auto size = fs::file_size(dataPath(key), ec);
    if (ec || size == 0 || static_cast<unsigned long long>(size) != parsed.bytes) {
        return false;
    }
    entry = parsed;
    return true;
}

bool Cache::commit(Entry entry) {
    if (!isKey(entry.key)) {
        return false;
    }
    const fs::path meta = metaPath(entry.key);
    const fs::path temporary = directory_ / (entry.key + ".meta.tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        const std::string text = serializeEntry(entry);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ec;
            fs::remove(temporary, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temporary, meta, ec);
    if (ec) {
        fs::remove(meta, ec);
        ec.clear();
        fs::rename(temporary, meta, ec);
    }
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

bool Cache::markBad(const std::string& key, const std::string& reason) {
    std::string text;
    if (!isKey(key) || !readAll(metaPath(key), text)) {
        return false;
    }
    Entry entry;
    if (!parseEntry(text, entry)) {
        return false;
    }
    entry.bad = true;
    entry.badReason = reason;
    return commit(entry);
}

bool Cache::touch(const std::string& key) {
    std::string text;
    if (!isKey(key) || !readAll(metaPath(key), text)) {
        return false;
    }
    Entry entry;
    if (!parseEntry(text, entry)) {
        return false;
    }
    entry.lastUsed = nowSeconds();
    return commit(entry);
}

bool Cache::remove(const std::string& key) {
    if (!isKey(key)) {
        return false;
    }
    std::error_code ec;
    bool removed = fs::remove(dataPath(key), ec);
    ec.clear();
    removed = fs::remove(metaPath(key), ec) || removed;
    ec.clear();
    fs::remove(partPath(key), ec);
    return removed;
}

std::vector<Entry> Cache::list() const {
    std::vector<Entry> entries;
    std::error_code ec;
    for (fs::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path file = it->path();
        if (file.extension() != ".meta") {
            continue;
        }
        const std::string stem = utf8Of(file.stem());
        std::string text;
        Entry entry;
        if (!isKey(stem) || !readAll(file, text) || !parseEntry(text, entry) || entry.key != stem) {
            continue;
        }
        std::error_code sizeError;
        const auto size = fs::file_size(dataPath(stem), sizeError);
        if (sizeError || static_cast<unsigned long long>(size) != entry.bytes) {
            continue;
        }
        entries.push_back(entry);
    }
    return entries;
}

unsigned long long Cache::totalBytes() const {
    unsigned long long total = 0;
    for (const Entry& entry : list()) {
        total += entry.bytes;
    }
    return total;
}

int Cache::evict(unsigned long long limitBytes, const std::set<std::string>& keep) {
    std::vector<Entry> entries = list();
    unsigned long long total = 0;
    for (const Entry& entry : entries) {
        total += entry.bytes;
    }
    if (total <= limitBytes) {
        return 0;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.bad != b.bad) {
            return a.bad;
        }
        if (a.lastUsed != b.lastUsed) {
            return a.lastUsed < b.lastUsed;
        }
        return a.key < b.key;
    });
    int removed = 0;
    for (const Entry& entry : entries) {
        if (total <= limitBytes) {
            break;
        }
        if (keep.count(entry.key) != 0) {
            continue;
        }
        if (remove(entry.key)) {
            total -= std::min(total, entry.bytes);
            ++removed;
        }
    }
    return removed;
}

int Cache::clear(const std::set<std::string>& keep) {
    int removed = 0;
    for (const Entry& entry : list()) {
        if (keep.count(entry.key) != 0) {
            continue;
        }
        if (remove(entry.key)) {
            ++removed;
        }
    }
    return removed;
}

int Cache::cleanStale(std::int64_t maxPartAgeSeconds, const std::set<std::string>& activeKeys) {
    int removed = 0;
    std::error_code ec;
    std::vector<fs::path> doomed;
    for (fs::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path file = it->path();
        const std::string name = utf8Of(file.filename());
        const std::string extension = utf8Of(file.extension());
        const std::string stem = utf8Of(file.stem());
        if (extension == ".part") {
            if (!isKey(stem) || activeKeys.count(stem) != 0) {
                continue;
            }
            std::error_code timeError;
            const auto time = fs::last_write_time(file, timeError);
            if (timeError) {
                continue;
            }
            const std::int64_t age = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - time).count());
            if (age >= maxPartAgeSeconds) {
                doomed.push_back(file);
            }
        } else if (name.size() > 9 && name.compare(name.size() - 9, 9, ".meta.tmp") == 0) {
            doomed.push_back(file);
        } else if (extension == ".mov") {
            if (isKey(stem) && !fs::exists(metaPath(stem), ec) && activeKeys.count(stem) == 0) {
                doomed.push_back(file);
            }
        } else if (extension == ".meta") {
            Entry entry;
            if (isKey(stem) && !find(stem, entry) && activeKeys.count(stem) == 0) {
                doomed.push_back(file);
            }
        }
    }
    for (const fs::path& file : doomed) {
        std::error_code removeError;
        if (fs::remove(file, removeError)) {
            ++removed;
        }
    }
    return removed;
}

}

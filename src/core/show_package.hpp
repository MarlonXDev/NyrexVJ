#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nvj::show {

constexpr int kFormatVersion = 1;
constexpr const char* kFormatName = "nyrexvj-show";
constexpr const char* kExtension = ".nyxshow";
constexpr const char* kPackFormatName = "nyrexvj-pack";
constexpr const char* kPackExtension = ".nyxpack";
constexpr size_t kHeaderBytes = 16;
constexpr size_t kFooterBytes = 32;
constexpr size_t kMaxNameBytes = 120;
constexpr size_t kMaxLicenseBytes = 240;
constexpr size_t kMaxNotesBytes = 2000;
constexpr size_t kMaxShowNameBytes = 80;
constexpr size_t kMaxRequires = 128;
constexpr size_t kMaxPackDependencies = 32;
constexpr size_t kMaxPackTextBytes = 2000;
constexpr size_t kMaxPackShortBytes = 80;

enum class Format {
    Show,
    Pack
};

const char* formatName(Format format);

enum class Kind {
    Composition,
    Preset,
    Media,
    Model,
    ScenePack,
    Shader,
    Node,
    Venue,
    Mask,
    Cues,
    Timeline,
    Plugin,
    Preview,
    License
};

constexpr int kKindCount = 14;

bool kindAllowedIn(Format format, Kind kind);

const char* kindName(Kind kind);
bool kindFromName(const std::string& name, Kind& kind);

struct Limits {
    unsigned long long maxTotalBytes = 16ull * 1024ull * 1024ull * 1024ull;
    size_t maxEntries = 4096;
    size_t maxManifestBytes = 4u * 1024u * 1024u;
    int maxJsonDepth = 24;

    unsigned long long maxBytesFor(Kind kind) const;
    static Limits forPack();
};

bool isSafeItemName(Kind kind, const std::string& name, std::string* reason = nullptr);
bool validUtf8(const std::string& text);
bool jsonNestingOk(const std::string& text, int limit);

struct Item {
    Kind kind = Kind::Media;
    std::string name;
    unsigned long long size = 0;
    unsigned long long offset = 0;
    std::string sha256;
    std::string license;
    bool included = true;
    bool missing = false;
};

struct PackDependency {
    std::string id;
    std::string min;
};

struct PackMeta {
    std::string id;
    std::string version;
    std::string author;
    std::string description;
    std::string license;
    std::string homepage;
    std::string minApp;
    std::string maxApp;
    std::string preview;
    std::vector<PackDependency> dependencies;
};

struct Manifest {
    Format format = Format::Show;
    PackMeta meta;
    int version = kFormatVersion;
    std::string appVersion;
    std::string name;
    std::string created;
    std::string notes;
    std::vector<Item> items;
    std::vector<std::string> plugins;
    std::vector<std::string> live;
};

struct SourceItem {
    Kind kind = Kind::Media;
    std::string name;
    std::filesystem::path file;
    std::string data;
    bool fromData = false;
    bool include = true;
    std::string license;
};

struct WriteRequest {
    Format format = Format::Show;
    PackMeta meta;
    std::string name;
    std::string notes;
    std::string appVersion;
    std::string created;
    std::vector<SourceItem> items;
    std::vector<std::string> plugins;
    std::vector<std::string> live;
};

struct Progress {
    unsigned long long done = 0;
    unsigned long long total = 0;
    std::string current;
};

using ProgressFn = std::function<bool(const Progress&)>;

struct WriteResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    unsigned long long bytes = 0;
    Manifest manifest;
};

WriteResult writePackage(const WriteRequest& request, const std::filesystem::path& destination, bool overwrite, const Limits& limits = {}, const ProgressFn& progress = nullptr);

struct Inspection {
    bool ok = false;
    std::string error;
    Manifest manifest;
    unsigned long long fileBytes = 0;
    std::vector<std::string> warnings;
};

Inspection inspectPackage(const std::filesystem::path& file, const Limits& limits = {}, Format expected = Format::Show);

bool extractItem(const std::filesystem::path& package, const Item& item, const std::filesystem::path& destination, bool allowReplace, const ProgressFn& progress, std::string& error);
bool verifyItem(const std::filesystem::path& package, const Item& item, const ProgressFn& progress, std::string& error);
bool readItemData(const std::filesystem::path& package, const Item& item, size_t maxBytes, std::string& out, std::string& error);

std::string manifestToJson(const Manifest& manifest);
bool manifestFromJson(const std::string& text, const Limits& limits, Manifest& manifest, std::vector<std::string>& warnings, std::string& error, Format expected = Format::Show);

}

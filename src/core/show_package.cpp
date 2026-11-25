#include "core/show_package.hpp"

#include "core/archive.hpp"
#include "core/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>

namespace nvj::show {

namespace fs = std::filesystem;

namespace {

constexpr char kHeaderMagic[8] = {'N', 'Y', 'X', 'S', 'H', 'O', 'W', '\0'};
constexpr char kFooterMagic[8] = {'N', 'Y', 'X', 'S', 'H', 'E', 'N', 'D'};
constexpr char kClipMagic[8] = {'N', 'Y', 'X', 'C', 'L', 'I', 'P', '1'};
constexpr size_t kChunkBytes = 1024u * 1024u;

struct KindInfo {
    Kind kind;
    const char* name;
    const char* fixedName;
    std::vector<std::string> extensions;
    unsigned long long maxBytes;
};

const std::vector<KindInfo>& kindTable() {
    static const std::vector<KindInfo> table = {
        {Kind::Composition, "composition", "composition.json", {}, 8ull * 1024 * 1024},
        {Kind::Preset, "preset", "", {".json"}, 8ull * 1024 * 1024},
        {Kind::Media, "media", "", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".mp4", ".mov", ".m4v", ".mkv", ".avi", ".webm"}, 4096ull * 1024 * 1024},
        {Kind::Model, "model", "", {".glb", ".gltf", ".bin", ".png", ".jpg", ".jpeg"}, 512ull * 1024 * 1024},
        {Kind::ScenePack, "scenepack", "", {".nyxclip"}, 256ull * 1024 * 1024},
        {Kind::Shader, "shader", "", {".glsl", ".frag", ".fs"}, 256ull * 1024},
        {Kind::Node, "node", "", {".nyxfx"}, 256ull * 1024},
        {Kind::Venue, "venue", "", {".json"}, 2ull * 1024 * 1024},
        {Kind::Mask, "mask", "", {".png", ".jpg", ".jpeg", ".bmp", ".tga"}, 16ull * 1024 * 1024},
        {Kind::Cues, "cues", "cues.json", {}, 8ull * 1024 * 1024},
        {Kind::Timeline, "timeline", "timeline.json", {}, 4ull * 1024 * 1024},
    };
    return table;
}

const KindInfo& infoFor(Kind kind) {
    return kindTable()[static_cast<size_t>(kind)];
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool reservedDeviceName(const std::string& name) {
    std::string stem = lowerAscii(name);
    const size_t dot = stem.find('.');
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    while (!stem.empty() && (stem.back() == ' ')) {
        stem.pop_back();
    }
    static const char* const kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* reserved : kReserved) {
        if (stem == reserved) {
            return true;
        }
    }
    return false;
}

bool decodeUtf8(const std::string& text, std::vector<unsigned int>& points) {
    points.clear();
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned int value = 0;
        size_t extra = 0;
        if (c < 0x80) {
            value = c;
        } else if (c >= 0xC2 && c <= 0xDF) {
            value = c & 0x1F;
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            value = c & 0x0F;
            extra = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            value = c & 0x07;
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;
        }
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            value = (value << 6) | (next & 0x3F);
        }
        if ((extra == 2 && value < 0x800) || (extra == 3 && (value < 0x10000 || value > 0x10FFFF)) || (value >= 0xD800 && value <= 0xDFFF)) {
            return false;
        }
        points.push_back(value);
        i += extra + 1;
    }
    return true;
}

bool disguisingCodePoint(unsigned int value) {
    return (value >= 0x200B && value <= 0x200F) || (value >= 0x202A && value <= 0x202E) || (value >= 0x2066 && value <= 0x2069) || value == 0xFEFF || (value >= 0x80 && value <= 0x9F);
}

std::string clampUtf8(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return text;
    }
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut);
}

bool nestingWithin(const std::string& text, int limit) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > limit) {
                return false;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

bool isHex64(const std::string& text) {
    if (text.size() != 64) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

void put32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void put64(std::string& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

uint32_t get32(const unsigned char* data) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(data[i]) << (8 * i);
    }
    return value;
}

uint64_t get64(const unsigned char* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

std::string duplicateKey(Kind kind, const std::string& name) {
    return std::string(kindName(kind)) + ":" + lowerAscii(name);
}

fs::path temporaryFor(const fs::path& destination) {
    fs::path temporary = destination;
    temporary += ".part";
    return temporary;
}

}

const char* kindName(Kind kind) {
    const size_t index = static_cast<size_t>(kind);
    return index < kindTable().size() ? kindTable()[index].name : "media";
}

bool kindFromName(const std::string& name, Kind& kind) {
    for (const KindInfo& info : kindTable()) {
        if (name == info.name) {
            kind = info.kind;
            return true;
        }
    }
    return false;
}

unsigned long long Limits::maxBytesFor(Kind kind) const {
    return std::min(infoFor(kind).maxBytes, maxTotalBytes);
}

bool validUtf8(const std::string& text) {
    std::vector<unsigned int> points;
    return decodeUtf8(text, points);
}

bool jsonNestingOk(const std::string& text, int limit) {
    return nestingWithin(text, limit);
}

bool isSafeItemName(Kind kind, const std::string& name, std::string* reason) {
    auto fail = [&](const char* text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };
    if (name.empty()) {
        return fail("The name is empty");
    }
    if (name.size() > kMaxNameBytes) {
        return fail("The name is too long");
    }
    std::vector<unsigned int> points;
    if (!decodeUtf8(name, points)) {
        return fail("The name is not valid text");
    }
    for (unsigned int point : points) {
        if (point < 0x20 || point == 0x7F) {
            return fail("The name has a control character");
        }
        if (point < 0x80 && std::strchr("\\/:*?\"<>|", static_cast<char>(point)) != nullptr) {
            return fail("The name has a character that is not allowed in file names");
        }
        if (disguisingCodePoint(point)) {
            return fail("The name has an invisible or direction changing character");
        }
    }
    if (name == "." || name == "..") {
        return fail("The name is a folder reference");
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return fail("The name starts or ends with a space or ends with a dot");
    }
    if (reservedDeviceName(name)) {
        return fail("The name is reserved by the operating system");
    }
    const KindInfo& info = infoFor(kind);
    if (info.fixedName[0] != '\0') {
        if (name != info.fixedName) {
            return fail("The name is not the one this kind of file must have");
        }
        return true;
    }
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return fail("The file has no allowed extension");
    }
    const std::string extension = lowerAscii(name.substr(dot));
    if (std::find(info.extensions.begin(), info.extensions.end(), extension) == info.extensions.end()) {
        return fail("The file extension is not allowed for this kind of file");
    }
    return true;
}

std::string manifestToJson(const Manifest& manifest) {
    nlohmann::json doc;
    doc["format"] = kFormatName;
    doc["version"] = manifest.version;
    doc["app"] = "NyrexVJ";
    doc["appVersion"] = manifest.appVersion;
    doc["name"] = manifest.name;
    doc["created"] = manifest.created;
    doc["notes"] = manifest.notes;
    nlohmann::json files = nlohmann::json::array();
    for (const Item& item : manifest.items) {
        nlohmann::json entry;
        entry["kind"] = kindName(item.kind);
        entry["name"] = item.name;
        entry["size"] = item.size;
        entry["sha256"] = item.sha256;
        entry["included"] = item.included;
        if (item.included) {
            entry["offset"] = item.offset;
        }
        if (item.missing) {
            entry["missing"] = true;
        }
        if (!item.license.empty()) {
            entry["license"] = item.license;
        }
        files.push_back(entry);
    }
    doc["files"] = files;
    nlohmann::json needs;
    needs["plugins"] = manifest.plugins;
    needs["live"] = manifest.live;
    doc["requires"] = needs;
    return doc.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

namespace {

std::string stringField(const nlohmann::json& object, const char* key, size_t maxBytes) {
    auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return std::string();
    }
    return clampUtf8(found->get<std::string>(), maxBytes);
}

void listField(const nlohmann::json& object, const char* key, std::vector<std::string>& out) {
    auto found = object.find(key);
    if (found == object.end() || !found->is_array()) {
        return;
    }
    for (const nlohmann::json& entry : *found) {
        if (out.size() >= kMaxRequires) {
            break;
        }
        if (entry.is_string()) {
            const std::string text = clampUtf8(entry.get<std::string>(), kMaxNameBytes);
            if (validUtf8(text) && !text.empty()) {
                out.push_back(text);
            }
        }
    }
}

}

bool manifestFromJson(const std::string& text, const Limits& limits, Manifest& manifest, std::vector<std::string>& warnings, std::string& error) {
    if (text.size() > limits.maxManifestBytes) {
        error = "The list of files in the package is too large";
        return false;
    }
    if (!nestingWithin(text, limits.maxJsonDepth)) {
        error = "The list of files in the package is nested too deeply";
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "The list of files in the package cannot be read";
        return false;
    }
    if (doc.value("format", std::string()) != kFormatName) {
        error = "This is not a show package";
        return false;
    }
    auto versionField = doc.find("version");
    if (versionField == doc.end() || !versionField->is_number_integer()) {
        error = "The show package has no valid version";
        return false;
    }
    const long long version = versionField->get<long long>();
    if (version < 1) {
        error = "The show package has no valid version";
        return false;
    }
    if (version > kFormatVersion) {
        error = "The show package was made by a newer version of NyrexVJ";
        return false;
    }
    Manifest result;
    result.version = static_cast<int>(version);
    result.appVersion = stringField(doc, "appVersion", 40);
    result.name = stringField(doc, "name", kMaxShowNameBytes);
    result.created = stringField(doc, "created", 40);
    result.notes = stringField(doc, "notes", kMaxNotesBytes);
    if (!validUtf8(result.name) || !validUtf8(result.notes)) {
        error = "The show package has text that is not valid";
        return false;
    }
    auto files = doc.find("files");
    if (files == doc.end() || !files->is_array()) {
        error = "The show package has no list of files";
        return false;
    }
    if (files->size() > limits.maxEntries) {
        error = "The show package lists too many files";
        return false;
    }
    std::set<std::string> seen;
    for (const nlohmann::json& entry : *files) {
        if (!entry.is_object()) {
            error = "The list of files in the package has an entry that cannot be read";
            return false;
        }
        const std::string kindText = stringField(entry, "kind", 40);
        Kind kind = Kind::Media;
        if (!kindFromName(kindText, kind)) {
            warnings.push_back("A file of the kind '" + clampUtf8(kindText, 40) + "' is not known to this version and was ignored");
            continue;
        }
        Item item;
        item.kind = kind;
        item.name = stringField(entry, "name", kMaxNameBytes + 1);
        std::string reason;
        if (!isSafeItemName(kind, item.name, &reason)) {
            error = "The package lists a file name that is not safe: " + reason;
            return false;
        }
        auto size = entry.find("size");
        if (size == entry.end() || !size->is_number_unsigned()) {
            error = "The package lists a file without a valid size";
            return false;
        }
        item.size = size->get<unsigned long long>();
        if (item.size > limits.maxBytesFor(kind)) {
            error = "The package lists a file that is larger than allowed: " + item.name;
            return false;
        }
        item.sha256 = stringField(entry, "sha256", 64);
        item.included = entry.value("included", true);
        item.missing = entry.value("missing", false);
        item.license = stringField(entry, "license", kMaxLicenseBytes);
        if (!validUtf8(item.license)) {
            item.license.clear();
        }
        if (item.included) {
            if (!isHex64(item.sha256)) {
                error = "The package lists a file without a valid checksum: " + item.name;
                return false;
            }
            auto offset = entry.find("offset");
            if (offset == entry.end() || !offset->is_number_unsigned()) {
                error = "The package lists a file without a valid position: " + item.name;
                return false;
            }
            item.offset = offset->get<unsigned long long>();
        } else if (!item.sha256.empty() && !isHex64(item.sha256)) {
            item.sha256.clear();
        }
        if (!seen.insert(duplicateKey(kind, item.name)).second) {
            error = "The package lists the same file twice: " + item.name;
            return false;
        }
        result.items.push_back(std::move(item));
    }
    auto needs = doc.find("requires");
    if (needs != doc.end() && needs->is_object()) {
        listField(*needs, "plugins", result.plugins);
        listField(*needs, "live", result.live);
    }
    manifest = std::move(result);
    return true;
}

WriteResult writePackage(const WriteRequest& request, const fs::path& destination, bool overwrite, const Limits& limits, const ProgressFn& progress) {
    WriteResult result;
    std::error_code ec;
    if (request.items.size() > limits.maxEntries) {
        result.error = "The show has too many files";
        return result;
    }
    if (!validUtf8(request.name) || request.name.size() > kMaxShowNameBytes) {
        result.error = "The name of the show is too long or not valid text";
        return result;
    }
    Manifest manifest;
    manifest.appVersion = clampUtf8(request.appVersion, 40);
    manifest.name = request.name;
    manifest.created = clampUtf8(request.created, 40);
    manifest.notes = clampUtf8(request.notes, kMaxNotesBytes);
    manifest.plugins.assign(request.plugins.begin(), request.plugins.begin() + static_cast<std::ptrdiff_t>(std::min(request.plugins.size(), kMaxRequires)));
    manifest.live.assign(request.live.begin(), request.live.begin() + static_cast<std::ptrdiff_t>(std::min(request.live.size(), kMaxRequires)));

    std::set<std::string> seen;
    unsigned long long total = 0;
    struct Prepared {
        const SourceItem* source;
        Item item;
    };
    std::vector<Prepared> prepared;
    for (const SourceItem& source : request.items) {
        std::string reason;
        if (!isSafeItemName(source.kind, source.name, &reason)) {
            result.error = "This file name cannot be used in a package: " + reason;
            return result;
        }
        if (!seen.insert(duplicateKey(source.kind, source.name)).second) {
            result.error = "The show lists the same file twice: " + source.name;
            return result;
        }
        Item item;
        item.kind = source.kind;
        item.name = source.name;
        item.license = clampUtf8(source.license, kMaxLicenseBytes);
        item.included = source.include;
        if (source.fromData) {
            item.size = source.data.size();
        } else {
            std::error_code statEc;
            if (!fs::is_regular_file(source.file, statEc) || statEc) {
                item.missing = true;
                item.included = false;
            } else {
                const auto size = fs::file_size(source.file, statEc);
                if (statEc) {
                    item.missing = true;
                    item.included = false;
                } else {
                    item.size = static_cast<unsigned long long>(size);
                }
            }
        }
        if (!item.missing && item.size > limits.maxBytesFor(source.kind)) {
            result.error = "The file is larger than a show package may hold: " + source.name;
            return result;
        }
        if (item.included) {
            total += item.size;
            if (total > limits.maxTotalBytes) {
                result.error = "The files of the show are larger than a show package may hold";
                return result;
            }
        }
        prepared.push_back({&source, std::move(item)});
    }

    if (fs::is_directory(destination, ec)) {
        result.error = "The destination is a folder";
        return result;
    }
    if (!overwrite && fs::exists(destination, ec)) {
        result.error = "The destination file already exists";
        return result;
    }
    if (destination.has_parent_path()) {
        fs::create_directories(destination.parent_path(), ec);
    }
    const fs::path temporary = temporaryFor(destination);
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "The package file could not be created";
        return result;
    }
    auto fail = [&](const std::string& message) {
        out.close();
        std::error_code removeEc;
        fs::remove(temporary, removeEc);
        result.error = message;
        result.ok = false;
        return result;
    };

    unsigned long long work = 0;
    for (const Prepared& entry : prepared) {
        if (!entry.item.missing) {
            work += entry.item.size;
        }
    }
    Progress state;
    state.total = work;
    auto report = [&](const std::string& current) {
        state.current = current;
        return !progress || progress(state);
    };

    std::string header;
    header.append(kHeaderMagic, 8);
    put32(header, static_cast<uint32_t>(kFormatVersion));
    put32(header, 0);
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    unsigned long long position = kHeaderBytes;

    std::vector<char> buffer(kChunkBytes);
    for (Prepared& entry : prepared) {
        Item& item = entry.item;
        const SourceItem& source = *entry.source;
        if (item.missing) {
            continue;
        }
        Sha256 hash;
        if (source.fromData) {
            hash.update(reinterpret_cast<const unsigned char*>(source.data.data()), source.data.size());
            if (item.included) {
                item.offset = position;
                out.write(source.data.data(), static_cast<std::streamsize>(source.data.size()));
                position += source.data.size();
            }
            state.done += item.size;
        } else {
            std::ifstream in(source.file, std::ios::binary);
            if (!in) {
                item.missing = true;
                item.included = false;
                work -= std::min(work, item.size);
                state.total = work;
                item.size = 0;
                continue;
            }
            if (item.included) {
                item.offset = position;
            }
            unsigned long long copied = 0;
            while (copied < item.size) {
                const size_t want = static_cast<size_t>(std::min<unsigned long long>(buffer.size(), item.size - copied));
                in.read(buffer.data(), static_cast<std::streamsize>(want));
                const std::streamsize got = in.gcount();
                if (got <= 0) {
                    break;
                }
                hash.update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(got));
                if (item.included) {
                    out.write(buffer.data(), got);
                }
                copied += static_cast<unsigned long long>(got);
                state.done += static_cast<unsigned long long>(got);
                if (!report(item.name)) {
                    out.close();
                    std::error_code removeEc;
                    fs::remove(temporary, removeEc);
                    result.cancelled = true;
                    result.error = "Cancelled";
                    return result;
                }
            }
            if (copied != item.size) {
                return fail("A file changed while it was being packed: " + item.name);
            }
            char extra = 0;
            in.read(&extra, 1);
            if (in.gcount() != 0) {
                return fail("A file changed while it was being packed: " + item.name);
            }
            if (item.included) {
                position += item.size;
            }
        }
        item.sha256 = hash.finalHex();
        if (!out) {
            return fail("The package file could not be written, the disk may be full");
        }
        if (!report(item.name)) {
            out.close();
            std::error_code removeEc;
            fs::remove(temporary, removeEc);
            result.cancelled = true;
            result.error = "Cancelled";
            return result;
        }
    }

    for (const Prepared& entry : prepared) {
        manifest.items.push_back(entry.item);
    }
    const std::string manifestText = manifestToJson(manifest);
    if (manifestText.size() > limits.maxManifestBytes) {
        return fail("The list of files of the show is too large");
    }
    const unsigned long long manifestOffset = position;
    out.write(manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
    std::string footer;
    put64(footer, manifestOffset);
    put32(footer, static_cast<uint32_t>(manifestText.size()));
    put32(footer, archive::crc32Update(0, reinterpret_cast<const unsigned char*>(manifestText.data()), manifestText.size()));
    put32(footer, static_cast<uint32_t>(kFormatVersion));
    put32(footer, 0);
    footer.append(kFooterMagic, 8);
    out.write(footer.data(), static_cast<std::streamsize>(footer.size()));
    out.flush();
    if (!out) {
        return fail("The package file could not be written, the disk may be full");
    }
    out.close();

    std::error_code renameEc;
    fs::rename(temporary, destination, renameEc);
    if (renameEc) {
        std::error_code removeEc;
        fs::remove(temporary, removeEc);
        result.error = "The package file could not be moved to its place";
        return result;
    }
    result.ok = true;
    result.bytes = position + manifestText.size() + kFooterBytes;
    result.manifest = std::move(manifest);
    return result;
}

Inspection inspectPackage(const fs::path& file, const Limits& limits) {
    Inspection result;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.error = "The file was not found";
        return result;
    }
    const auto size = fs::file_size(file, ec);
    if (ec) {
        result.error = "The file could not be read";
        return result;
    }
    result.fileBytes = static_cast<unsigned long long>(size);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        result.error = "The file could not be opened";
        return result;
    }
    unsigned char header[kHeaderBytes] = {};
    in.read(reinterpret_cast<char*>(header), static_cast<std::streamsize>(kHeaderBytes));
    if (static_cast<size_t>(in.gcount()) < 8) {
        result.error = "This is not a show package";
        return result;
    }
    if (std::memcmp(header, kClipMagic, 8) == 0) {
        result.error = "This is a scene pack (.nyxclip), not a show package";
        return result;
    }
    if (std::memcmp(header, kHeaderMagic, 8) != 0) {
        result.error = "This is not a show package";
        return result;
    }
    if (result.fileBytes < kHeaderBytes + kFooterBytes) {
        result.error = "The show package is cut off";
        return result;
    }
    const uint32_t headerVersion = get32(header + 8);
    if (headerVersion < 1) {
        result.error = "The show package has no valid version";
        return result;
    }
    if (headerVersion > static_cast<uint32_t>(kFormatVersion)) {
        result.error = "The show package was made by a newer version of NyrexVJ";
        return result;
    }

    unsigned char footer[kFooterBytes] = {};
    in.seekg(static_cast<std::streamoff>(result.fileBytes - kFooterBytes));
    in.read(reinterpret_cast<char*>(footer), static_cast<std::streamsize>(kFooterBytes));
    if (static_cast<size_t>(in.gcount()) != kFooterBytes || std::memcmp(footer + 24, kFooterMagic, 8) != 0) {
        result.error = "The show package is cut off or damaged";
        return result;
    }
    const uint64_t manifestOffset = get64(footer);
    const uint32_t manifestSize = get32(footer + 8);
    const uint32_t manifestCrc = get32(footer + 12);
    const uint32_t footerVersion = get32(footer + 16);
    if (footerVersion != headerVersion) {
        result.error = "The show package is damaged";
        return result;
    }
    if (manifestSize == 0 || manifestSize > limits.maxManifestBytes || manifestOffset < kHeaderBytes || manifestOffset + manifestSize + kFooterBytes != result.fileBytes) {
        result.error = "The show package is damaged";
        return result;
    }
    std::string manifestText(manifestSize, '\0');
    in.seekg(static_cast<std::streamoff>(manifestOffset));
    in.read(manifestText.data(), static_cast<std::streamsize>(manifestSize));
    if (static_cast<size_t>(in.gcount()) != manifestSize) {
        result.error = "The show package is cut off";
        return result;
    }
    if (archive::crc32Update(0, reinterpret_cast<const unsigned char*>(manifestText.data()), manifestText.size()) != manifestCrc) {
        result.error = "The list of files in the package is damaged";
        return result;
    }
    Manifest manifest;
    if (!manifestFromJson(manifestText, limits, manifest, result.warnings, result.error)) {
        return result;
    }
    if (manifest.version != static_cast<int>(headerVersion)) {
        result.error = "The show package is damaged";
        return result;
    }

    std::vector<std::pair<unsigned long long, unsigned long long>> ranges;
    unsigned long long total = 0;
    for (const Item& item : manifest.items) {
        if (!item.included) {
            continue;
        }
        if (item.offset < kHeaderBytes || item.offset > manifestOffset || item.size > manifestOffset - item.offset) {
            result.error = "The package lists a file outside of its data: " + item.name;
            return result;
        }
        total += item.size;
        if (total > limits.maxTotalBytes || total > result.fileBytes) {
            result.error = "The package lists more data than it holds";
            return result;
        }
        if (item.size > 0) {
            ranges.emplace_back(item.offset, item.offset + item.size);
        }
    }
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].first < ranges[i - 1].second) {
            result.error = "The package lists files that overlap";
            return result;
        }
    }
    result.manifest = std::move(manifest);
    result.ok = true;
    return result;
}

namespace {

bool streamItem(const fs::path& package, const Item& item, std::ofstream* out, const ProgressFn& progress, std::string& error) {
    if (!item.included) {
        error = "The file is not part of the package";
        return false;
    }
    std::ifstream in(package, std::ios::binary);
    if (!in) {
        error = "The package could not be opened";
        return false;
    }
    in.seekg(static_cast<std::streamoff>(item.offset));
    Sha256 hash;
    std::vector<char> buffer(kChunkBytes);
    unsigned long long copied = 0;
    Progress state;
    state.total = item.size;
    state.current = item.name;
    while (copied < item.size) {
        const size_t want = static_cast<size_t>(std::min<unsigned long long>(buffer.size(), item.size - copied));
        in.read(buffer.data(), static_cast<std::streamsize>(want));
        const std::streamsize got = in.gcount();
        if (got <= 0) {
            error = "The package is cut off";
            return false;
        }
        hash.update(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(got));
        if (out) {
            out->write(buffer.data(), got);
            if (!*out) {
                error = "The file could not be written, the disk may be full";
                return false;
            }
        }
        copied += static_cast<unsigned long long>(got);
        state.done = copied;
        if (progress && !progress(state)) {
            error = "Cancelled";
            return false;
        }
    }
    if (hash.finalHex() != item.sha256) {
        error = "The file does not match its checksum, the package is damaged: " + item.name;
        return false;
    }
    return true;
}

}

bool verifyItem(const fs::path& package, const Item& item, const ProgressFn& progress, std::string& error) {
    return streamItem(package, item, nullptr, progress, error);
}

bool extractItem(const fs::path& package, const Item& item, const fs::path& destination, bool allowReplace, const ProgressFn& progress, std::string& error) {
    std::error_code ec;
    if (!allowReplace && fs::exists(destination, ec)) {
        error = "A file with this name already exists";
        return false;
    }
    if (destination.has_parent_path()) {
        fs::create_directories(destination.parent_path(), ec);
    }
    fs::path temporary = destination;
    temporary += ".nyxshow-part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "The file could not be created";
            return false;
        }
        if (!streamItem(package, item, &out, progress, error)) {
            out.close();
            fs::remove(temporary, ec);
            return false;
        }
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temporary, ec);
            error = "The file could not be written, the disk may be full";
            return false;
        }
    }
    if (!allowReplace && fs::exists(destination, ec)) {
        fs::remove(temporary, ec);
        error = "A file with this name already exists";
        return false;
    }
    fs::rename(temporary, destination, ec);
    if (ec) {
        std::error_code removeEc;
        fs::remove(temporary, removeEc);
        error = "The file could not be moved to its place";
        return false;
    }
    return true;
}

bool readItemData(const fs::path& package, const Item& item, size_t maxBytes, std::string& out, std::string& error) {
    out.clear();
    if (item.size > maxBytes) {
        error = "The file is larger than allowed: " + item.name;
        return false;
    }
    std::ifstream in(package, std::ios::binary);
    if (!in) {
        error = "The package could not be opened";
        return false;
    }
    if (!item.included) {
        error = "The file is not part of the package";
        return false;
    }
    in.seekg(static_cast<std::streamoff>(item.offset));
    out.resize(static_cast<size_t>(item.size));
    in.read(out.data(), static_cast<std::streamsize>(item.size));
    if (static_cast<unsigned long long>(in.gcount()) != item.size) {
        out.clear();
        error = "The package is cut off";
        return false;
    }
    if (sha256Hex(reinterpret_cast<const unsigned char*>(out.data()), out.size()) != item.sha256) {
        out.clear();
        error = "The file does not match its checksum, the package is damaged: " + item.name;
        return false;
    }
    return true;
}

}

#include "check.hpp"

#include "core/archive.hpp"
#include "core/sha256.hpp"
#include "core/show_package.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

namespace fs = std::filesystem;
using namespace nvj::show;

fs::path scratchDirectory(const char* name) {
    const fs::path path = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
    return path;
}

void writeBytes(const fs::path& file, const std::string& data) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string readBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string pattern(size_t size, unsigned seed) {
    std::string data(size, '\0');
    unsigned state = seed * 2654435761u + 12345u;
    for (char& c : data) {
        state = state * 1664525u + 1013904223u;
        c = static_cast<char>(state >> 24);
    }
    return data;
}

void put32(std::string& out, unsigned value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void put64(std::string& out, unsigned long long value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

std::string rawPackage(const std::string& dataRegion, const std::string& manifestText, unsigned headerVersion = 1, unsigned footerVersion = 1) {
    std::string out;
    out.append("NYXSHOW\0", 8);
    put32(out, headerVersion);
    put32(out, 0);
    out += dataRegion;
    const unsigned long long manifestOffset = 16 + dataRegion.size();
    out += manifestText;
    put64(out, manifestOffset);
    put32(out, static_cast<unsigned>(manifestText.size()));
    put32(out, nvj::archive::crc32Update(0, reinterpret_cast<const unsigned char*>(manifestText.data()), manifestText.size()));
    put32(out, footerVersion);
    put32(out, 0);
    out.append("NYXSHEND", 8);
    return out;
}

nlohmann::json fileEntry(const char* kind, const std::string& name, const std::string& data, unsigned long long offset) {
    nlohmann::json entry;
    entry["kind"] = kind;
    entry["name"] = name;
    entry["size"] = data.size();
    entry["offset"] = offset;
    entry["sha256"] = nvj::sha256Hex(data);
    entry["included"] = true;
    return entry;
}

std::string manifestWith(const nlohmann::json& files, int version = 1) {
    nlohmann::json doc;
    doc["format"] = "nyrexvj-show";
    doc["version"] = version;
    doc["name"] = "hostile";
    doc["files"] = files;
    return doc.dump();
}

Inspection inspectRaw(const fs::path& dir, const std::string& bytes, const Limits& limits = {}) {
    const fs::path file = dir / "raw.nyxshow";
    writeBytes(file, bytes);
    return inspectPackage(file, limits);
}

SourceItem fromText(Kind kind, const std::string& name, const std::string& data) {
    SourceItem item;
    item.kind = kind;
    item.name = name;
    item.data = data;
    item.fromData = true;
    return item;
}

SourceItem fromDisk(Kind kind, const std::string& name, const fs::path& file) {
    SourceItem item;
    item.kind = kind;
    item.name = name;
    item.file = file;
    return item;
}

const Item* findItem(const Manifest& manifest, Kind kind, const std::string& name) {
    for (const Item& item : manifest.items) {
        if (item.kind == kind && item.name == name) {
            return &item;
        }
    }
    return nullptr;
}

void testNames() {
    std::string reason;
    CHECK(isSafeItemName(Kind::Media, "Intro loop.mp4", &reason));
    CHECK(isSafeItemName(Kind::Media, "cover.PNG"));
    CHECK(isSafeItemName(Kind::Media, "B\xC3\xA4r.mp4"));
    CHECK(isSafeItemName(Kind::Composition, "composition.json"));
    CHECK(!isSafeItemName(Kind::Composition, "other.json"));
    CHECK(isSafeItemName(Kind::Cues, "cues.json"));
    CHECK(isSafeItemName(Kind::Timeline, "timeline.json"));
    CHECK(isSafeItemName(Kind::Preset, "Warm Night.json"));
    CHECK(isSafeItemName(Kind::Model, "robot.glb"));
    CHECK(isSafeItemName(Kind::Model, "robot.bin"));
    CHECK(isSafeItemName(Kind::Shader, "tunnel.frag"));
    CHECK(isSafeItemName(Kind::Node, "glow.nyxfx"));
    CHECK(isSafeItemName(Kind::ScenePack, "city.nyxclip"));
    CHECK(isSafeItemName(Kind::Venue, "Main stage.json"));
    CHECK(isSafeItemName(Kind::Mask, "left.png"));

    const char* hostile[] = {"", ".", "..", "../x.mp4", "..\\x.mp4", "a/b.mp4", "a\\b.mp4", "/etc/passwd.mp4", "C:\\x.mp4", "C:x.mp4", "x.mp4:stream", "con.mp4", "NUL.mp4", "Com1.mp4", "lpt9.png", "aux", "x.mp4.", "x.mp4 ", " x.mp4", "x*.mp4", "x?.mp4", "x<.mp4", "x|.mp4", "x\".mp4", "x\n.mp4", "x\t.mp4", "x\x7F.mp4", ".mp4", "noextension", "tool.exe", "tool.dll", "tool.so", "tool.bat", "tool.ps1", "tool.mp4.exe", "x\xE2\x80\xAE" "4pm.mp4", "x\xE2\x80\x8B.mp4", "x\xFF.mp4", "x\xC0\x80.mp4", "x\xED\xA0\x80.mp4"};
    for (const char* name : hostile) {
        CHECK(!isSafeItemName(Kind::Media, name));
    }
    CHECK(!isSafeItemName(Kind::Media, std::string(121, 'a') + ".mp4"));
    CHECK(isSafeItemName(Kind::Media, std::string(116, 'a') + ".mp4"));
    CHECK(!isSafeItemName(Kind::Preset, "x.exe"));
    CHECK(!isSafeItemName(Kind::Shader, "x.dll"));
    CHECK(!isSafeItemName(Kind::Node, "x.json"));
    CHECK(!isSafeItemName(Kind::Mask, "x.mp4"));
    CHECK(!isSafeItemName(Kind::Media, "plugin.dll"));
    CHECK(validUtf8("Gr\xC3\xBC\xC3\x9F" "e"));
    CHECK(!validUtf8("\xC3"));
    CHECK(!validUtf8("\xF5\x80\x80\x80"));
}

void testRoundtrip(const fs::path& base) {
    const fs::path source = base / "source";
    const std::string video = pattern(3 * 1024 * 1024 + 17, 1);
    const std::string image = pattern(5000, 2);
    writeBytes(source / "loop.mp4", video);
    writeBytes(source / "cover.png", image);

    WriteRequest request;
    request.name = "Friday show";
    request.notes = "Notes for the next person";
    request.appVersion = "0.41.0";
    request.created = "2026-11-25T10:00:00Z";
    request.items.push_back(fromText(Kind::Composition, "composition.json", "{\"format\":2}"));
    request.items.push_back(fromDisk(Kind::Media, "loop.mp4", source / "loop.mp4"));
    request.items.push_back(fromDisk(Kind::Media, "cover.png", source / "cover.png"));
    request.items.push_back(fromDisk(Kind::Media, "gone.mp4", source / "gone.mp4"));
    SourceItem licensed = fromText(Kind::Shader, "tunnel.frag", "void main(){}");
    licensed.license = "CC0 by Marlon";
    request.items.push_back(licensed);
    SourceItem excluded = fromDisk(Kind::Media, "big.mkv", source / "cover.png");
    excluded.include = false;
    request.items.push_back(excluded);
    request.plugins = {"Spout Output"};
    request.live = {"NDI: Camera 1"};

    const fs::path destination = base / "out" / "show.nyxshow";
    std::vector<std::string> seenFiles;
    unsigned long long lastDone = 0;
    bool monotone = true;
    const WriteResult written = writePackage(request, destination, false, {}, [&](const Progress& progress) {
        if (progress.done < lastDone) {
            monotone = false;
        }
        lastDone = progress.done;
        return true;
    });
    CHECK(written.ok);
    CHECK(monotone);
    CHECK(fs::exists(destination));
    CHECK(!fs::exists(fs::path(destination.string() + ".part")));
    CHECK(written.bytes == fs::file_size(destination));

    const Inspection inspected = inspectPackage(destination);
    CHECK(inspected.ok);
    CHECK(inspected.manifest.name == "Friday show");
    CHECK(inspected.manifest.notes == "Notes for the next person");
    CHECK(inspected.manifest.appVersion == "0.41.0");
    CHECK(inspected.manifest.items.size() == 6);
    CHECK(inspected.manifest.plugins.size() == 1 && inspected.manifest.plugins[0] == "Spout Output");
    CHECK(inspected.manifest.live.size() == 1);
    const Item* loop = findItem(inspected.manifest, Kind::Media, "loop.mp4");
    const Item* gone = findItem(inspected.manifest, Kind::Media, "gone.mp4");
    const Item* big = findItem(inspected.manifest, Kind::Media, "big.mkv");
    const Item* shader = findItem(inspected.manifest, Kind::Shader, "tunnel.frag");
    CHECK(loop && loop->included && loop->size == video.size() && loop->sha256 == nvj::sha256Hex(video));
    CHECK(gone && gone->missing && !gone->included);
    CHECK(big && !big->included && !big->missing && big->sha256 == nvj::sha256Hex(image));
    CHECK(shader && shader->license == "CC0 by Marlon");

    const fs::path target = base / "target";
    std::string error;
    CHECK(loop && extractItem(destination, *loop, target / "media" / "loop.mp4", false, nullptr, error));
    CHECK(readBytes(target / "media" / "loop.mp4") == video);
    CHECK(loop && !extractItem(destination, *loop, target / "media" / "loop.mp4", false, nullptr, error));
    CHECK(loop && extractItem(destination, *loop, target / "media" / "loop.mp4", true, nullptr, error));
    CHECK(big && !extractItem(destination, *big, target / "media" / "big.mkv", false, nullptr, error));
    CHECK(!fs::exists(target / "media" / "big.mkv"));
    CHECK(loop && verifyItem(destination, *loop, nullptr, error));

    std::string composition;
    const Item* compositionItem = findItem(inspected.manifest, Kind::Composition, "composition.json");
    CHECK(compositionItem && readItemData(destination, *compositionItem, 1024, composition, error));
    CHECK(composition == "{\"format\":2}");
    CHECK(compositionItem && !readItemData(destination, *compositionItem, 4, composition, error));

    CHECK(!writePackage(request, destination, false).ok);
    CHECK(writePackage(request, destination, true).ok);
    WriteRequest folderCase = request;
    CHECK(!writePackage(folderCase, base / "out", true).ok);

    WriteRequest duplicate = request;
    duplicate.items.push_back(fromText(Kind::Media, "LOOP.MP4", "x"));
    CHECK(!writePackage(duplicate, base / "dup.nyxshow", false).ok);
    CHECK(!fs::exists(base / "dup.nyxshow"));
    WriteRequest unsafe = request;
    unsafe.items.push_back(fromText(Kind::Media, "..\\evil.mp4", "x"));
    CHECK(!writePackage(unsafe, base / "unsafe.nyxshow", false).ok);
    CHECK(!fs::exists(base / "unsafe.nyxshow"));
    WriteRequest tooMany = request;
    Limits small;
    small.maxEntries = 3;
    CHECK(!writePackage(tooMany, base / "many.nyxshow", false, small).ok);
    Limits tiny;
    tiny.maxTotalBytes = 1000;
    CHECK(!writePackage(request, base / "tiny.nyxshow", false, tiny).ok);
    CHECK(!fs::exists(base / "tiny.nyxshow"));
}

void testCancelAndChange(const fs::path& base) {
    const fs::path source = base / "cancel";
    writeBytes(source / "a.mp4", pattern(4 * 1024 * 1024, 3));
    WriteRequest request;
    request.name = "cancel";
    request.items.push_back(fromDisk(Kind::Media, "a.mp4", source / "a.mp4"));
    int calls = 0;
    const fs::path destination = base / "cancelled.nyxshow";
    const WriteResult result = writePackage(request, destination, false, {}, [&](const Progress&) { return ++calls < 2; });
    CHECK(!result.ok && result.cancelled);
    CHECK(!fs::exists(destination));
    CHECK(!fs::exists(fs::path(destination.string() + ".part")));

    const fs::path again = base / "again.nyxshow";
    CHECK(writePackage(request, again, false).ok);
    const Inspection inspected = inspectPackage(again);
    CHECK(inspected.ok);
    std::string error;
    int extractCalls = 0;
    const fs::path out = base / "cancel-out" / "a.mp4";
    CHECK(!extractItem(again, inspected.manifest.items[0], out, false, [&](const Progress&) { return ++extractCalls < 2; }, error));
    CHECK(!fs::exists(out));
    CHECK(!fs::exists(fs::path(out.string() + ".nyxshow-part")));
}

void testHostileContainers(const fs::path& base) {
    const std::string data = pattern(300, 4);
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        const Inspection good = inspectRaw(base, rawPackage(data, manifestWith(files)));
        CHECK(good.ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "../escape.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "C:\\Windows\\x.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "/abs.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "tool.exe", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "aux.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        files.push_back(fileEntry("media", "A.MP4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        files.push_back(fileEntry("model", "a.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        files.push_back(fileEntry("media", "b.mp4", data, 16 + 100));
        CHECK(!inspectRaw(base, rawPackage(data + data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        files.push_back(fileEntry("media", "b.mp4", data, 16 + 300));
        CHECK(inspectRaw(base, rawPackage(data + data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 100000));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 0));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16 + 200));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 16);
        entry["size"] = 0xFFFFFFFFFFFFFFFFull;
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 0xFFFFFFFFFFFFFF00ull);
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 16);
        entry["size"] = 5ull * 1024 * 1024 * 1024;
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("shader", "a.frag", data, 16);
        entry["size"] = 300 * 1024;
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 16);
        entry["size"] = -5;
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 16);
        entry["sha256"] = "not a hash";
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 16);
        entry["sha256"] = std::string(64, 'G');
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json entry = fileEntry("media", "a.mp4", data, 16);
        entry["sha256"] = std::string(64, 'A');
        nlohmann::json files = nlohmann::json::array();
        files.push_back(entry);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        files.push_back(fileEntry("plugin", "evil.dll", data, 16));
        const Inspection result = inspectRaw(base, rawPackage(data, manifestWith(files)));
        CHECK(result.ok);
        CHECK(result.manifest.items.size() == 1);
        CHECK(!result.warnings.empty());
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files, 2), 2, 2)).ok);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files, 0), 0, 0)).ok);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files, 1), 1, 2)).ok);
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files, 1), 2, 2)).ok);
    }
    {
        nlohmann::json doc;
        doc["format"] = "something-else";
        doc["version"] = 1;
        doc["files"] = nlohmann::json::array();
        CHECK(!inspectRaw(base, rawPackage("", doc.dump())).ok);
        doc["format"] = "nyrexvj-show";
        doc.erase("files");
        CHECK(!inspectRaw(base, rawPackage("", doc.dump())).ok);
    }
    {
        const std::string deep = std::string(5000, '[') + std::string(5000, ']');
        CHECK(!inspectRaw(base, rawPackage("", deep)).ok);
        CHECK(!inspectRaw(base, rawPackage("", "{\"format\":\"nyrexvj-show\",\"version\":1,\"files\":" + deep + "}")).ok);
        CHECK(!inspectRaw(base, rawPackage("", "not json at all")).ok);
        CHECK(!inspectRaw(base, rawPackage("", "[]")).ok);
        CHECK(!inspectRaw(base, rawPackage("", "")).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        for (int i = 0; i < 40; ++i) {
            files.push_back(fileEntry("media", "f" + std::to_string(i) + ".mp4", std::string(), 16));
        }
        Limits limits;
        limits.maxEntries = 30;
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files)), limits).ok);
        CHECK(inspectRaw(base, rawPackage(data, manifestWith(files))).ok);
    }
    {
        Limits limits;
        limits.maxManifestBytes = 50;
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        CHECK(!inspectRaw(base, rawPackage(data, manifestWith(files)), limits).ok);
    }
    {
        nlohmann::json files = nlohmann::json::array();
        files.push_back(fileEntry("media", "a.mp4", data, 16));
        std::string bytes = rawPackage(data, manifestWith(files));
        std::string wrongCrc = bytes;
        wrongCrc[wrongCrc.size() - 32 + 12] ^= 0x55;
        CHECK(!inspectRaw(base, wrongCrc).ok);
        std::string wrongSize = bytes;
        wrongSize[wrongSize.size() - 32 + 8] ^= 0x01;
        CHECK(!inspectRaw(base, wrongSize).ok);
        std::string wrongOffset = bytes;
        wrongOffset[wrongOffset.size() - 32] ^= 0x01;
        CHECK(!inspectRaw(base, wrongOffset).ok);
        std::string wrongFooterMagic = bytes;
        wrongFooterMagic[wrongFooterMagic.size() - 1] = 'X';
        CHECK(!inspectRaw(base, wrongFooterMagic).ok);
        std::string wrongHeaderMagic = bytes;
        wrongHeaderMagic[0] = 'Z';
        CHECK(!inspectRaw(base, wrongHeaderMagic).ok);
    }
    {
        std::string clipLike;
        clipLike.append("NYXCLIP1", 8);
        clipLike += pattern(200, 5);
        const Inspection result = inspectRaw(base, clipLike);
        CHECK(!result.ok);
        CHECK(result.error.find("scene pack") != std::string::npos);
    }
    CHECK(!inspectPackage(base / "does-not-exist.nyxshow").ok);
    CHECK(!inspectPackage(base).ok);
    CHECK(!inspectRaw(base, "").ok);
    CHECK(!inspectRaw(base, "NYXSHOW").ok);
}

void testDamage(const fs::path& base) {
    const std::string first = pattern(900, 6);
    const std::string second = pattern(700, 7);
    WriteRequest request;
    request.name = "damage";
    request.items.push_back(fromText(Kind::Composition, "composition.json", first));
    request.items.push_back(fromText(Kind::Media, "b.png", second));
    const fs::path file = base / "damage.nyxshow";
    CHECK(writePackage(request, file, true).ok);
    const std::string good = readBytes(file);

    const fs::path probe = base / "probe.nyxshow";
    int shortCount = 0;
    for (size_t length = 0; length < good.size(); ++length) {
        writeBytes(probe, good.substr(0, length));
        if (inspectPackage(probe).ok) {
            ++shortCount;
        }
    }
    CHECK(shortCount == 0);

    int unsafeSuccesses = 0;
    int detected = 0;
    for (size_t position = 0; position < good.size(); ++position) {
        std::string changed = good;
        changed[position] = static_cast<char>(changed[position] ^ 0x5A);
        writeBytes(probe, changed);
        const Inspection inspected = inspectPackage(probe);
        if (!inspected.ok) {
            ++detected;
            continue;
        }
        for (const Item& item : inspected.manifest.items) {
            if (!item.included) {
                continue;
            }
            std::string data;
            std::string error;
            if (readItemData(probe, item, 1 << 20, data, error)) {
                const std::string& expected = item.kind == Kind::Composition ? first : second;
                if (data != expected) {
                    ++unsafeSuccesses;
                }
            }
        }
    }
    CHECK(unsafeSuccesses == 0);
    CHECK(detected > 0);

    std::string flipped = good;
    flipped[16 + 10] = static_cast<char>(flipped[16 + 10] ^ 0x01);
    writeBytes(probe, flipped);
    const Inspection inspected = inspectPackage(probe);
    CHECK(inspected.ok);
    std::string error;
    CHECK(!verifyItem(probe, inspected.manifest.items[0], nullptr, error));
    CHECK(!error.empty());
    CHECK(!extractItem(probe, inspected.manifest.items[0], base / "damage-out" / "composition.json", false, nullptr, error));
    CHECK(!fs::exists(base / "damage-out" / "composition.json"));
    CHECK(!fs::exists(base / "damage-out" / "composition.json.nyxshow-part"));
    CHECK(verifyItem(probe, inspected.manifest.items[1], nullptr, error));
}

void testManifestText() {
    Manifest manifest;
    manifest.name = "x";
    Item item;
    item.kind = Kind::Preset;
    item.name = "p.json";
    item.size = 3;
    item.offset = 16;
    item.sha256 = std::string(64, 'a');
    manifest.items.push_back(item);
    const std::string text = manifestToJson(manifest);
    Manifest parsed;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(manifestFromJson(text, {}, parsed, warnings, error));
    CHECK(parsed.items.size() == 1 && parsed.items[0].name == "p.json");

    nlohmann::json doc = nlohmann::json::parse(text);
    doc["notes"] = std::string(5000, 'n');
    doc["name"] = std::string(500, 'n');
    doc["extra"] = "ignored";
    CHECK(manifestFromJson(doc.dump(), {}, parsed, warnings, error));
    CHECK(parsed.notes.size() <= 2000);
    CHECK(parsed.name.size() <= 80);

    doc["name"] = "bad ZZ text";
    std::string broken = doc.dump();
    broken.replace(broken.find("ZZ"), 2, "\xFF");
    CHECK(!manifestFromJson(broken, {}, parsed, warnings, error));

    Manifest unclean = manifest;
    unclean.notes = "bad \xFF notes";
    const std::string cleaned = manifestToJson(unclean);
    CHECK(manifestFromJson(cleaned, {}, parsed, warnings, error));
}

void testKinds() {
    for (int i = 0; i < kKindCount; ++i) {
        Kind back = Kind::Media;
        CHECK(kindFromName(kindName(static_cast<Kind>(i)), back));
        CHECK(back == static_cast<Kind>(i));
    }
    Kind kind = Kind::Media;
    CHECK(!kindFromName("plugin", kind));
    CHECK(!kindFromName("", kind));
    CHECK(!kindFromName("Media", kind));
    Limits limits;
    CHECK(limits.maxBytesFor(Kind::Shader) == 256ull * 1024);
    limits.maxTotalBytes = 100;
    CHECK(limits.maxBytesFor(Kind::Media) == 100);
}

}

int main() {
    const fs::path base = scratchDirectory("nyrexvj-show-tests");
    testNames();
    testKinds();
    testManifestText();
    testRoundtrip(base / "roundtrip");
    testCancelAndChange(base / "cancel");
    testHostileContainers(base / "hostile");
    testDamage(base / "damage");
    std::error_code ec;
    fs::remove_all(base, ec);
    return testing::finish();
}

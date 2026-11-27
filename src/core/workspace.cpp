#include "core/workspace.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace nvj::workspace {

namespace {

namespace fs = std::filesystem;

constexpr const char* kFolder = "workspaces";
constexpr const char* kExtension = ".ini";
constexpr const char* kSignature = "; NyrexVJ workspace";

fs::path pathFromName(const std::string& name) {
    const std::string file = name + kExtension;
    return fs::path(std::u8string(file.begin(), file.end()));
}

std::string nameFromPath(const fs::path& path) {
    const std::u8string text = path.stem().u8string();
    return std::string(text.begin(), text.end());
}

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool readNumber(const std::string& text, int& value) {
    if (text.empty() || text.size() > 8) {
        return false;
    }
    size_t index = 0;
    if (text[0] == '-') {
        index = 1;
    }
    if (index >= text.size()) {
        return false;
    }
    for (size_t i = index; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    value = static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
    return true;
}

bool readPair(const std::string& text, int& first, int& second) {
    const size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    return readNumber(text.substr(0, comma), first) && readNumber(text.substr(comma + 1), second);
}

bool sanePosition(const std::string& value) {
    int x = 0;
    int y = 0;
    return readPair(value, x, y) && std::abs(x) <= kMaxCoordinate && std::abs(y) <= kMaxCoordinate;
}

bool saneSize(const std::string& value) {
    int w = 0;
    int h = 0;
    return readPair(value, w, h) && w >= 0 && h >= 0 && w <= kMaxCoordinate && h <= kMaxCoordinate;
}

bool readText(const fs::path& path, size_t limit, std::string& text) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > limit) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool writeTextAtomic(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << text;
        file.flush();
        if (!file) {
            file.close();
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        std::error_code removeEc;
        fs::remove(path, removeEc);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    if (ec) {
        std::error_code removeEc;
        fs::remove(temporary, removeEc);
        return false;
    }
    return true;
}

}

std::string cleanName(const std::string& raw) {
    std::string out;
    bool pendingSpace = false;
    for (const char character : raw) {
        const unsigned char c = static_cast<unsigned char>(character);
        if (c < 0x20 || c == 0x7f) {
            continue;
        }
        const bool forbidden = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
        if (c == ' ' || forbidden) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += character;
    }
    if (out.size() > kMaxNameBytes) {
        size_t cut = kMaxNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    return out;
}

bool reservedName(const std::string& name) {
    std::string stem = name.substr(0, name.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") {
        return true;
    }
    if (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' && stem[3] <= '9') {
        return true;
    }
    return false;
}

std::string nameKey(const std::string& name) {
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c); });
    return key;
}

Check inspectLayout(const std::string& text) {
    Check check;
    if (text.empty()) {
        check.reason = "The layout is empty";
        return check;
    }
    if (text.size() > kMaxFileBytes) {
        check.reason = "The layout is too large";
        return check;
    }
    for (const char character : text) {
        const unsigned char c = static_cast<unsigned char>(character);
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            check.reason = "The layout contains control characters";
            return check;
        }
    }
    std::istringstream stream(text);
    std::string raw;
    bool haveSection = false;
    bool inDocking = false;
    int number = 0;
    while (std::getline(stream, raw)) {
        ++number;
        if (number > kMaxLines) {
            check.reason = "The layout has too many lines";
            return check;
        }
        const std::string line = trim(raw);
        if (line.empty() || line[0] == ';') {
            continue;
        }
        const std::string where = std::to_string(number);
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close == std::string::npos || close == 1 || close + 1 >= line.size() || line[close + 1] != '[' || line.back() != ']' || line.size() < close + 4) {
                check.reason = "Not a section header in line " + where;
                return check;
            }
            const std::string type = line.substr(1, close - 1);
            inDocking = type == "Docking";
            if (type == "Window") {
                ++check.windows;
                if (check.windows > 400) {
                    check.reason = "The layout has too many windows";
                    return check;
                }
            }
            haveSection = true;
            continue;
        }
        if (!haveSection) {
            check.reason = "Content before the first section in line " + where;
            return check;
        }
        if (inDocking) {
            if (line.rfind("DockNode", 0) != 0 && line.rfind("DockSpace", 0) != 0) {
                check.reason = "Not a dock node in line " + where;
                return check;
            }
            ++check.dockNodes;
            std::istringstream tokens(line);
            std::string token;
            while (tokens >> token) {
                if (token.rfind("Pos=", 0) == 0 && !sanePosition(token.substr(4))) {
                    check.reason = "Position out of range in line " + where;
                    return check;
                }
                if (token.rfind("Size=", 0) == 0 && !saneSize(token.substr(5))) {
                    check.reason = "Size out of range in line " + where;
                    return check;
                }
            }
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            check.reason = "Not a key and a value in line " + where;
            return check;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if ((key == "Pos" || key == "ViewportPos") && !sanePosition(value)) {
            check.reason = "Position out of range in line " + where;
            return check;
        }
        if (key == "Size" && !saneSize(value)) {
            check.reason = "Size out of range in line " + where;
            return check;
        }
    }
    check.lines = number;
    if (check.windows == 0) {
        check.reason = "The layout has no windows";
        return check;
    }
    check.ok = true;
    return check;
}

std::string compose(const std::string& layout, int width, int height, const std::string& app) {
    std::string text;
    text += std::string(kSignature) + "\n";
    text += "; format=" + std::to_string(kFormat) + "\n";
    text += "; app=" + app + "\n";
    text += "; display=" + std::to_string(width) + "x" + std::to_string(height) + "\n";
    text += layout;
    if (text.empty() || text.back() != '\n') {
        text += '\n';
    }
    return text;
}

Check inspectFile(const std::string& text, Header* header) {
    Check check;
    Header parsed;
    std::istringstream stream(text);
    std::string raw;
    if (!std::getline(stream, raw) || trim(raw) != kSignature) {
        check.reason = "This is not a NyrexVJ workspace file";
        return check;
    }
    while (std::getline(stream, raw)) {
        const std::string line = trim(raw);
        if (line.empty() || line[0] != ';') {
            break;
        }
        if (line.rfind("; format=", 0) == 0) {
            int value = 0;
            if (readNumber(line.substr(9), value)) {
                parsed.format = value;
            }
        } else if (line.rfind("; app=", 0) == 0) {
            parsed.app = line.substr(6, 32);
        } else if (line.rfind("; display=", 0) == 0) {
            const std::string value = line.substr(10);
            const size_t cross = value.find('x');
            if (cross != std::string::npos) {
                readNumber(value.substr(0, cross), parsed.width);
                readNumber(value.substr(cross + 1), parsed.height);
            }
        }
    }
    if (header != nullptr) {
        *header = parsed;
    }
    if (parsed.format <= 0) {
        check.reason = "The workspace file has no format number";
        return check;
    }
    if (parsed.format > kFormat) {
        check.reason = "This workspace was saved by a newer version of NyrexVJ";
        return check;
    }
    return inspectLayout(text);
}

fs::path folder(const fs::path& dataDir) {
    return dataDir / kFolder;
}

std::vector<Entry> list(const fs::path& dataDir) {
    std::vector<Entry> entries;
    std::error_code ec;
    if (!fs::is_directory(folder(dataDir), ec)) {
        return entries;
    }
    for (fs::directory_iterator it(folder(dataDir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || it->path().extension() != kExtension) {
            continue;
        }
        Entry entry;
        entry.name = nameFromPath(it->path());
        entry.path = it->path();
        std::string text;
        if (!readText(it->path(), kMaxFileBytes + 1024, text)) {
            entry.valid = false;
            entry.problem = "The file could not be read";
        } else {
            Header header;
            const Check check = inspectFile(text, &header);
            entry.valid = check.ok;
            entry.problem = check.reason;
            entry.width = header.width;
            entry.height = header.height;
            entry.app = header.app;
        }
        entries.push_back(entry);
        if (entries.size() >= kMaxWorkspaces * 2) {
            break;
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return nameKey(a.name) < nameKey(b.name); });
    return entries;
}

namespace {

const Entry* findEntry(const std::vector<Entry>& entries, const std::string& name) {
    const std::string key = nameKey(cleanName(name));
    for (const Entry& entry : entries) {
        if (nameKey(entry.name) == key) {
            return &entry;
        }
    }
    return nullptr;
}

}

Result save(const fs::path& dataDir, const std::string& name, const std::string& layout, int width, int height, const std::string& app) {
    Result result;
    const std::string clean = cleanName(name);
    if (clean.empty() || reservedName(clean)) {
        result.error = "That name cannot be used for a workspace";
        return result;
    }
    const Check check = inspectLayout(layout);
    if (!check.ok) {
        result.error = "The current layout cannot be saved: " + check.reason;
        return result;
    }
    const std::vector<Entry> entries = list(dataDir);
    const Entry* existing = findEntry(entries, clean);
    if (existing == nullptr && entries.size() >= kMaxWorkspaces) {
        result.error = "The workspace limit is reached. Delete one first";
        return result;
    }
    const fs::path target = existing != nullptr ? existing->path : folder(dataDir) / pathFromName(clean);
    if (!writeTextAtomic(target, compose(layout, width, height, app))) {
        result.error = "The workspace could not be written";
        return result;
    }
    result.ok = true;
    return result;
}

Result load(const fs::path& dataDir, const std::string& name, std::string& layout, Header* header) {
    Result result;
    const std::vector<Entry> entries = list(dataDir);
    const Entry* entry = findEntry(entries, name);
    if (entry == nullptr) {
        result.error = "There is no workspace with that name";
        return result;
    }
    std::string text;
    if (!readText(entry->path, kMaxFileBytes + 1024, text)) {
        result.error = "The workspace file could not be read";
        return result;
    }
    const Check check = inspectFile(text, header);
    if (!check.ok) {
        result.error = check.reason;
        return result;
    }
    layout = text;
    result.ok = true;
    return result;
}

Result remove(const fs::path& dataDir, const std::string& name) {
    Result result;
    const std::vector<Entry> entries = list(dataDir);
    const Entry* entry = findEntry(entries, name);
    if (entry == nullptr) {
        result.error = "There is no workspace with that name";
        return result;
    }
    std::error_code ec;
    if (!fs::remove(entry->path, ec) || ec) {
        result.error = "The workspace file could not be deleted";
        return result;
    }
    result.ok = true;
    return result;
}

namespace {

fs::path shareFileName(const std::string& name) {
    const std::string file = name + kShareExtension;
    return fs::path(std::u8string(file.begin(), file.end()));
}

std::string cutBytes(const std::string& text, size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut);
}

}

fs::path exportFolder(const fs::path& dataDir) {
    return dataDir / "layouts_out";
}

Result exportTo(const fs::path& dataDir, const std::string& name, const fs::path& destination, fs::path* written) {
    Result result;
    const std::vector<Entry> entries = list(dataDir);
    const Entry* entry = findEntry(entries, name);
    if (entry == nullptr) {
        result.error = "There is no workspace with that name";
        return result;
    }
    std::string text;
    if (!readText(entry->path, kMaxFileBytes + 1024, text)) {
        result.error = "The workspace file could not be read";
        return result;
    }
    const Check check = inspectFile(text);
    if (!check.ok) {
        result.error = check.reason;
        return result;
    }
    const fs::path target = destination.empty() ? exportFolder(dataDir) : destination;
    std::error_code ec;
    fs::create_directories(target, ec);
    if (!fs::is_directory(target, ec)) {
        result.error = "The export folder could not be created";
        return result;
    }
    const fs::path file = target / shareFileName(entry->name);
    if (!writeTextAtomic(file, text)) {
        result.error = "The layout file could not be written";
        return result;
    }
    if (written != nullptr) {
        *written = file;
    }
    result.ok = true;
    return result;
}

Result importFile(const fs::path& dataDir, const fs::path& file, std::string* importedName) {
    Result result;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        result.error = "The layout file could not be read or is too large";
        return result;
    }
    std::string text;
    if (!readText(file, kMaxFileBytes + 1024, text)) {
        result.error = "The layout file could not be read or is too large";
        return result;
    }
    const Check check = inspectFile(text);
    if (!check.ok) {
        result.error = check.reason;
        return result;
    }
    const std::vector<Entry> entries = list(dataDir);
    if (entries.size() >= kMaxWorkspaces) {
        result.error = "The workspace limit is reached. Delete one first";
        return result;
    }
    std::string base = cleanName(nameFromPath(file));
    if (base.empty() || reservedName(base)) {
        base = "Imported layout";
    }
    std::string name = base;
    for (int number = 2; findEntry(entries, name) != nullptr; ++number) {
        if (number > 99) {
            result.error = "That name cannot be used for a workspace";
            return result;
        }
        const std::string suffix = " " + std::to_string(number);
        name = cleanName(cutBytes(base, kMaxNameBytes - suffix.size()) + suffix);
    }
    if (!writeTextAtomic(folder(dataDir) / pathFromName(name), text)) {
        result.error = "The workspace could not be written";
        return result;
    }
    if (importedName != nullptr) {
        *importedName = name;
    }
    result.ok = true;
    return result;
}

bool quarantine(const fs::path& file) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return true;
    }
    fs::path target = file;
    target += ".bad";
    fs::remove(target, ec);
    ec.clear();
    fs::rename(file, target, ec);
    if (!ec) {
        return true;
    }
    ec.clear();
    fs::remove(file, ec);
    return !fs::exists(file, ec);
}

}

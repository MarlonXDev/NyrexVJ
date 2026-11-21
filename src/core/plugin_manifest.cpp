#include "core/plugin_manifest.hpp"

#include "core/recovery.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include <nlohmann/json.hpp>

namespace nvj {

namespace fs = std::filesystem;

namespace {

fs::path utf8Path(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string lower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool printableText(const std::string& text, size_t maxLength) {
    if (text.empty() || text.size() > maxLength) {
        return false;
    }
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool validLibraryName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name.front() == '.') {
        return false;
    }
    for (unsigned char c : name) {
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    const std::string text = lower(name);
    const auto ends = [&](const char* suffix) {
        const std::string tail = suffix;
        return text.size() > tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
    };
    return ends(".dll") || ends(".so");
}

bool readUnsigned(const nlohmann::json& value, unsigned& out) {
    if (!value.is_number_integer()) {
        return false;
    }
    const long long number = value.get<long long>();
    if (number < 0 || number > 65535) {
        return false;
    }
    out = static_cast<unsigned>(number);
    return true;
}

PluginEntry fail(PluginEntry entry, PluginProblem problem, const std::string& detail) {
    entry.problem = problem;
    entry.detail = detail;
    return entry;
}

}

const char* pluginProblemName(PluginProblem problem) {
    switch (problem) {
    case PluginProblem::None:
        return "none";
    case PluginProblem::Unreadable:
        return "plugin.json cannot be read";
    case PluginProblem::TooLarge:
        return "plugin.json is too large";
    case PluginProblem::NotJson:
        return "plugin.json is not valid JSON";
    case PluginProblem::WrongFormat:
        return "plugin.json has another format";
    case PluginProblem::NewerFormat:
        return "plugin.json has a newer format version";
    case PluginProblem::BadId:
        return "the plugin id is not valid";
    case PluginProblem::BadName:
        return "the plugin name is not valid";
    case PluginProblem::BadApi:
        return "the API version is not valid";
    case PluginProblem::WrongMajor:
        return "the plugin was built for another major API version";
    case PluginProblem::NewerMinor:
        return "the plugin needs a newer NyrexVJ (higher minor API version)";
    case PluginProblem::NoLibrary:
        return "the plugin has no library for this system";
    case PluginProblem::BadLibraryName:
        return "the library file name is not valid";
    case PluginProblem::MissingLibrary:
        return "the library file is missing";
    case PluginProblem::DuplicateId:
        return "another plugin already uses this id";
    }
    return "unknown";
}

bool validPluginId(const std::string& id) {
    if (id.empty() || id.size() > kMaxPluginIdLength) {
        return false;
    }
    for (size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        const bool joiner = c == '-' || c == '_' || c == '.';
        if (!alnum && !(joiner && i > 0)) {
            return false;
        }
    }
    return true;
}

const char* pluginPlatformKey() {
#ifdef _WIN32
    return "windows-x64";
#else
    return "linux-x64";
#endif
}

fs::path pluginsFolder(const fs::path& dataDir) {
    return dataDir / "plugins";
}

PluginEntry parseManifest(const std::string& text, const fs::path& directory, unsigned hostMajor, unsigned hostMinor, const std::string& platformKey) {
    PluginEntry entry;
    entry.directory = directory;
    if (text.size() > kMaxManifestBytes) {
        return fail(entry, PluginProblem::TooLarge, std::to_string(text.size()) + " bytes");
    }
    nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail(entry, PluginProblem::NotJson, "");
    }
    if (!doc.contains("format") || !doc["format"].is_string() || doc["format"].get<std::string>() != "nyrexvj-plugin") {
        return fail(entry, PluginProblem::WrongFormat, "");
    }
    unsigned formatVersion = 0;
    if (!doc.contains("version") || !readUnsigned(doc["version"], formatVersion) || formatVersion == 0) {
        return fail(entry, PluginProblem::WrongFormat, "version");
    }
    if (formatVersion > 1) {
        return fail(entry, PluginProblem::NewerFormat, std::to_string(formatVersion));
    }

    PluginManifest manifest;
    if (doc.contains("id") && doc["id"].is_string()) {
        manifest.id = doc["id"].get<std::string>();
    }
    if (!validPluginId(manifest.id)) {
        return fail(entry, PluginProblem::BadId, manifest.id.substr(0, 40));
    }
    if (doc.contains("name") && doc["name"].is_string()) {
        manifest.name = doc["name"].get<std::string>();
    }
    if (!printableText(manifest.name, kMaxPluginNameLength)) {
        entry.manifest.id = manifest.id;
        return fail(entry, PluginProblem::BadName, "");
    }
    if (doc.contains("pluginVersion") && doc["pluginVersion"].is_string()) {
        manifest.version = doc["pluginVersion"].get<std::string>();
        if (!printableText(manifest.version, kMaxPluginVersionLength)) {
            manifest.version.clear();
        }
    }
    entry.manifest = manifest;

    if (!doc.contains("api") || !doc["api"].is_object() || !readUnsigned(doc["api"].value("major", nlohmann::json()), manifest.apiMajor) || !readUnsigned(doc["api"].value("minor", nlohmann::json()), manifest.apiMinor) || manifest.apiMajor == 0) {
        return fail(entry, PluginProblem::BadApi, "");
    }
    entry.manifest.apiMajor = manifest.apiMajor;
    entry.manifest.apiMinor = manifest.apiMinor;
    const std::string apiText = std::to_string(manifest.apiMajor) + "." + std::to_string(manifest.apiMinor);
    if (manifest.apiMajor != hostMajor) {
        return fail(entry, PluginProblem::WrongMajor, "plugin " + apiText + ", NyrexVJ " + std::to_string(hostMajor) + "." + std::to_string(hostMinor));
    }
    if (manifest.apiMinor > hostMinor) {
        return fail(entry, PluginProblem::NewerMinor, "plugin " + apiText + ", NyrexVJ " + std::to_string(hostMajor) + "." + std::to_string(hostMinor));
    }

    if (!doc.contains("library") || !doc["library"].is_object() || !doc["library"].contains(platformKey) || !doc["library"][platformKey].is_string()) {
        return fail(entry, PluginProblem::NoLibrary, platformKey);
    }
    manifest.library = doc["library"][platformKey].get<std::string>();
    if (!validLibraryName(manifest.library)) {
        return fail(entry, PluginProblem::BadLibraryName, manifest.library.substr(0, 40));
    }
    entry.manifest.library = manifest.library;
    entry.libraryPath = directory / utf8Path(manifest.library);
    std::error_code ec;
    if (!fs::is_regular_file(entry.libraryPath, ec) || ec) {
        return fail(entry, PluginProblem::MissingLibrary, manifest.library);
    }
    return entry;
}

std::vector<PluginEntry> scanPlugins(const fs::path& folder, unsigned hostMajor, unsigned hostMinor) {
    std::vector<PluginEntry> found;
    std::error_code ec;
    if (!fs::is_directory(folder, ec) || ec) {
        return found;
    }
    std::vector<fs::path> directories;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        if (it->is_directory(typeError) && !typeError) {
            directories.push_back(it->path());
        }
    }
    std::sort(directories.begin(), directories.end());
    if (directories.size() > kMaxPluginFolders) {
        directories.resize(kMaxPluginFolders);
    }
    for (const fs::path& directory : directories) {
        const fs::path manifestPath = directory / "plugin.json";
        PluginEntry entry;
        std::error_code fileError;
        if (!fs::is_regular_file(manifestPath, fileError) || fileError) {
            continue;
        }
        const std::uintmax_t bytes = fs::file_size(manifestPath, fileError);
        std::string text;
        if (fileError) {
            entry.directory = directory;
            entry.problem = PluginProblem::Unreadable;
        } else if (bytes > kMaxManifestBytes) {
            entry.directory = directory;
            entry.problem = PluginProblem::TooLarge;
            entry.detail = std::to_string(bytes) + " bytes";
        } else if (!readWholeFile(manifestPath, text)) {
            entry.directory = directory;
            entry.problem = PluginProblem::Unreadable;
        } else {
            entry = parseManifest(text, directory, hostMajor, hostMinor, pluginPlatformKey());
        }
        if (entry.usable() || !entry.manifest.id.empty()) {
            for (const PluginEntry& other : found) {
                if (other.manifest.id == entry.manifest.id && other.usable()) {
                    entry.problem = PluginProblem::DuplicateId;
                    entry.detail = entry.manifest.id;
                    break;
                }
            }
        }
        found.push_back(std::move(entry));
    }
    return found;
}

}

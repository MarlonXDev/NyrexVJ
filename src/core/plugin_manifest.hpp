#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

constexpr unsigned kPluginApiMajor = 1;
constexpr unsigned kPluginApiMinor = 0;
constexpr size_t kMaxPluginFolders = 16;
constexpr size_t kMaxManifestBytes = 16 * 1024;
constexpr size_t kMaxPluginIdLength = 32;
constexpr size_t kMaxPluginNameLength = 40;
constexpr size_t kMaxPluginVersionLength = 24;
constexpr int kPluginModulationSlots = 8;
constexpr int kPluginFrameSlots = 4;
constexpr int kPluginParameters = 4;

enum class PluginProblem {
    None,
    Unreadable,
    TooLarge,
    NotJson,
    WrongFormat,
    NewerFormat,
    BadId,
    BadName,
    BadApi,
    WrongMajor,
    NewerMinor,
    NoLibrary,
    BadLibraryName,
    MissingLibrary,
    DuplicateId
};

const char* pluginProblemName(PluginProblem problem);

struct PluginManifest {
    std::string id;
    std::string name;
    std::string version;
    unsigned apiMajor = 0;
    unsigned apiMinor = 0;
    std::string library;
};

struct PluginEntry {
    std::filesystem::path directory;
    std::filesystem::path libraryPath;
    PluginManifest manifest;
    PluginProblem problem = PluginProblem::None;
    std::string detail;

    bool usable() const { return problem == PluginProblem::None; }
};

bool validPluginId(const std::string& id);
const char* pluginPlatformKey();
std::filesystem::path pluginsFolder(const std::filesystem::path& dataDir);

PluginEntry parseManifest(const std::string& text, const std::filesystem::path& directory, unsigned hostMajor, unsigned hostMinor, const std::string& platformKey);
std::vector<PluginEntry> scanPlugins(const std::filesystem::path& folder, unsigned hostMajor = kPluginApiMajor, unsigned hostMinor = kPluginApiMinor);

}

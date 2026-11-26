#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj::workspace {

constexpr int kFormat = 1;
constexpr size_t kMaxNameBytes = 40;
constexpr size_t kMaxFileBytes = 256 * 1024;
constexpr size_t kMaxWorkspaces = 32;
constexpr int kMaxLines = 6000;
constexpr int kMaxCoordinate = 32000;

struct Check {
    bool ok = false;
    std::string reason;
    int windows = 0;
    int dockNodes = 0;
    int lines = 0;
};

struct Header {
    int format = 0;
    int width = 0;
    int height = 0;
    std::string app;
};

struct Entry {
    std::string name;
    std::filesystem::path path;
    int width = 0;
    int height = 0;
    std::string app;
    bool valid = false;
    std::string problem;
};

struct Result {
    bool ok = false;
    std::string error;
};

std::string cleanName(const std::string& raw);
bool reservedName(const std::string& name);
std::string nameKey(const std::string& name);

Check inspectLayout(const std::string& text);
std::string compose(const std::string& layout, int width, int height, const std::string& app);
Check inspectFile(const std::string& text, Header* header = nullptr);

std::filesystem::path folder(const std::filesystem::path& dataDir);
std::vector<Entry> list(const std::filesystem::path& dataDir);
Result save(const std::filesystem::path& dataDir, const std::string& name, const std::string& layout, int width, int height, const std::string& app);
Result load(const std::filesystem::path& dataDir, const std::string& name, std::string& layout, Header* header = nullptr);
Result remove(const std::filesystem::path& dataDir, const std::string& name);

bool quarantine(const std::filesystem::path& file);

}

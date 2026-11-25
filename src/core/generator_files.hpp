#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nvj::gen {

constexpr size_t kMaxDocBytes = 32 * 1024;
constexpr size_t kMaxFiles = 64;
constexpr size_t kMaxNameBytes = 64;

std::string cleanName(const std::string& name);
bool nameIsUsable(const std::string& name);
std::string uniqueName(const std::string& wanted, const std::vector<std::string>& taken);
std::vector<std::string> listStems(const std::filesystem::path& folder, const std::string& extension);
bool readLimited(const std::filesystem::path& path, std::string& text, std::string& error);
std::filesystem::path folderPath(const char* folder);

}

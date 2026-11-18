#pragma once

#include "scene3d/scene_model.hpp"

#include <filesystem>
#include <string>

namespace nvj {

constexpr int kMaxMediaImageDimension = 8192;
constexpr size_t kMaxMediaImageBytes = 320u * 1024u * 1024u;
constexpr unsigned long long kMaxMediaFileBytes = 256ull * 1024ull * 1024ull;

bool isImageFileName(const std::string& name);
bool isVideoFileName(const std::string& name);
void premultiplyAlpha(std::vector<uint8_t>& rgba);
bool loadMediaImage(const std::filesystem::path& file, SceneImage& out, std::string& error);

}

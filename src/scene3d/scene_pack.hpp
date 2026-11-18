#pragma once

#include "core/clip_pack.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/scene_doc.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nvj {

constexpr size_t kPackMaxTotalAssetBytes = 200u * 1024u * 1024u;

struct ScenePack {
    SceneDoc doc;
    std::map<std::string, std::vector<uint8_t>> models;
    std::map<std::string, std::vector<uint8_t>> textures;
};

bool writeScenePack(const ScenePack& pack, std::vector<uint8_t>& out, std::string& error);
bool readScenePack(const uint8_t* data, size_t size, ScenePack& pack, std::string& error);
bool writeScenePackFile(const std::filesystem::path& path, const ScenePack& pack, std::string& error);
bool readScenePackFile(const std::filesystem::path& path, ScenePack& pack, std::string& error);

struct AssetFolders {
    std::filesystem::path models;
    std::filesystem::path textures;
};

class AssetSource {
public:
    AssetSource(AssetFolders folders, std::shared_ptr<const ScenePack> pack = nullptr);
    DocAssets assets() const;
    void clearCache();
    bool collect(const SceneDoc& doc, ScenePack& out, std::vector<std::string>& warnings) const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

std::filesystem::path pathFromUtf8(const std::string& text);
std::string pathToUtf8(const std::filesystem::path& path);
bool readFileBytes(const std::filesystem::path& path, size_t limit, std::vector<uint8_t>& out, std::string& error);

}

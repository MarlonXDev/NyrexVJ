#include "scene3d/scene_pack.hpp"

#include "scene3d/image_decode.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace nvj {

namespace {

constexpr const char* kSceneEntry = "scene.json";
constexpr const char* kAssetPrefix = "assets/";
constexpr const char* kModelPrefix = "assets/models/";
constexpr const char* kTexturePrefix = "assets/textures/";

bool startsWith(const std::string& text, const char* prefix) {
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

bool looksLikeGlb(const std::vector<uint8_t>& data) {
    return data.size() >= 12 && std::memcmp(data.data(), "glTF", 4) == 0;
}

bool looksLikeImage(const std::vector<uint8_t>& data) {
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return true;
    }
    return data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

}

std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

bool readFileBytes(const std::filesystem::path& path, size_t limit, std::vector<uint8_t>& out, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "the file could not be read";
        return false;
    }
    if (size > limit) {
        error = "the file is larger than allowed";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "the file could not be opened";
        return false;
    }
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (static_cast<uint64_t>(stream.gcount()) != size) {
            error = "the file could not be read completely";
            return false;
        }
    }
    return true;
}

bool writeScenePack(const ScenePack& pack, std::vector<uint8_t>& out, std::string& error) {
    SceneDoc doc = pack.doc;
    sanitizeDoc(doc);
    const std::string json = docToJson(doc);
    std::vector<PackEntry> entries;
    PackEntry scene;
    scene.name = kSceneEntry;
    scene.data.assign(json.begin(), json.end());
    entries.push_back(std::move(scene));
    size_t total = 0;
    for (const auto& [name, data] : pack.models) {
        if (!isModelAssetName(name) || !looksLikeGlb(data)) {
            error = "model '" + name + "' cannot be stored in a pack (only .glb files can)";
            return false;
        }
        total += data.size();
        entries.push_back({kModelPrefix + name, data});
    }
    for (const auto& [name, data] : pack.textures) {
        if (!isTextureAssetName(name) || !looksLikeImage(data)) {
            error = "texture '" + name + "' cannot be stored in a pack (only PNG and JPEG can)";
            return false;
        }
        total += data.size();
        entries.push_back({kTexturePrefix + name, data});
    }
    if (total > kPackMaxTotalAssetBytes) {
        error = "the assets of this scene are too large for one pack";
        return false;
    }
    return writePack(entries, out, error);
}

bool readScenePack(const uint8_t* data, size_t size, ScenePack& pack, std::string& error) {
    std::vector<PackEntry> entries;
    if (!readPack(data, size, entries, error)) {
        return false;
    }
    ScenePack result;
    bool haveScene = false;
    for (PackEntry& entry : entries) {
        if (entry.name == kSceneEntry) {
            const std::string text(entry.data.begin(), entry.data.end());
            if (!docFromJson(text, result.doc, error)) {
                return false;
            }
            haveScene = true;
        } else if (startsWith(entry.name, kModelPrefix)) {
            const std::string name = entry.name.substr(std::strlen(kModelPrefix));
            if (isModelAssetName(name) && looksLikeGlb(entry.data)) {
                result.models[name] = std::move(entry.data);
            }
        } else if (startsWith(entry.name, kTexturePrefix)) {
            const std::string name = entry.name.substr(std::strlen(kTexturePrefix));
            if (isTextureAssetName(name) && looksLikeImage(entry.data)) {
                result.textures[name] = std::move(entry.data);
            }
        } else if (!startsWith(entry.name, kAssetPrefix)) {
            continue;
        }
    }
    if (!haveScene) {
        error = "the clip pack has no scene.json";
        return false;
    }
    pack = std::move(result);
    return true;
}

bool writeScenePackFile(const std::filesystem::path& path, const ScenePack& pack, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!writeScenePack(pack, bytes, error)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "the file could not be written";
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            error = "the file could not be written completely";
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        error = "the file could not be replaced";
        return false;
    }
    return true;
}

bool readScenePackFile(const std::filesystem::path& path, ScenePack& pack, std::string& error) {
    std::vector<uint8_t> bytes;
    PackLimits limits;
    if (!readFileBytes(path, limits.maxFileBytes, bytes, error)) {
        return false;
    }
    return readScenePack(bytes.data(), bytes.size(), pack, error);
}

struct AssetSource::State {
    AssetFolders folders;
    std::shared_ptr<const ScenePack> pack;
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<const GltfResult>> models;
    std::map<std::string, std::shared_ptr<const SceneImage>> images;
};

AssetSource::AssetSource(AssetFolders folders, std::shared_ptr<const ScenePack> pack) : state_(std::make_shared<State>()) {
    state_->folders = std::move(folders);
    state_->pack = std::move(pack);
}

void AssetSource::clearCache() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->models.clear();
    state_->images.clear();
}

DocAssets AssetSource::assets() const {
    std::shared_ptr<State> state = state_;
    DocAssets assets;
    assets.loadModel = [state](const std::string& name, const GltfLimits& limits) -> std::shared_ptr<const GltfResult> {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            const auto known = state->models.find(name);
            if (known != state->models.end()) {
                return known->second;
            }
        }
        auto result = std::make_shared<GltfResult>();
        if (!isModelAssetName(name)) {
            result->error = "unsafe model name";
            return result;
        }
        if (state->pack) {
            const auto packed = state->pack->models.find(name);
            if (packed != state->pack->models.end()) {
                *result = loadGltfMemory(packed->second.data(), packed->second.size(), std::filesystem::path(), limits);
            }
        }
        if (!result->ok && result->error.empty()) {
            *result = loadGltfFile(state->folders.models / pathFromUtf8(name), limits);
        }
        if (result->ok) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->models[name] = result;
        }
        return result;
    };
    assets.loadImage = [state](const std::string& name, std::string& error) -> std::shared_ptr<const SceneImage> {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            const auto known = state->images.find(name);
            if (known != state->images.end()) {
                return known->second;
            }
        }
        if (!isTextureAssetName(name)) {
            error = "unsafe texture name";
            return nullptr;
        }
        std::vector<uint8_t> bytes;
        bool found = false;
        if (state->pack) {
            const auto packed = state->pack->textures.find(name);
            if (packed != state->pack->textures.end()) {
                bytes = packed->second;
                found = true;
            }
        }
        if (!found && !readFileBytes(state->folders.textures / pathFromUtf8(name), 64u * 1024u * 1024u, bytes, error)) {
            return nullptr;
        }
        auto image = std::make_shared<SceneImage>();
        if (!decodeSceneImage(bytes.data(), bytes.size(), 4096, 192u * 1024u * 1024u, *image, error)) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        state->images[name] = image;
        return image;
    };
    return assets;
}

bool AssetSource::collect(const SceneDoc& doc, ScenePack& out, std::vector<std::string>& warnings) const {
    out.doc = doc;
    out.models.clear();
    out.textures.clear();
    size_t total = 0;
    const auto fetch = [&](const std::string& name, bool model) -> bool {
        const std::map<std::string, std::vector<uint8_t>>* packed = nullptr;
        if (state_->pack) {
            packed = model ? &state_->pack->models : &state_->pack->textures;
        }
        std::vector<uint8_t> bytes;
        const auto known = packed != nullptr ? packed->find(name) : std::map<std::string, std::vector<uint8_t>>::const_iterator();
        if (packed != nullptr && known != packed->end()) {
            bytes = known->second;
        } else {
            std::string error;
            const std::filesystem::path folder = model ? state_->folders.models : state_->folders.textures;
            if (!readFileBytes(folder / pathFromUtf8(name), 128u * 1024u * 1024u, bytes, error)) {
                warnings.push_back("'" + name + "' was not found and is not stored in the pack");
                return false;
            }
        }
        total += bytes.size();
        if (total > kPackMaxTotalAssetBytes) {
            warnings.push_back("'" + name + "' was left out because the pack would be too large");
            return false;
        }
        (model ? out.models : out.textures)[name] = std::move(bytes);
        return true;
    };
    for (const std::string& name : docModelNames(doc)) {
        fetch(name, true);
    }
    for (const std::string& name : docTextureNames(doc)) {
        fetch(name, false);
    }
    return true;
}

}

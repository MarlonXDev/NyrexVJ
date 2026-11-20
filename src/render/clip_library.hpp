#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nvj {

enum class ClipKind {
    Shader,
    BuiltinScene,
    ModelFile,
    ScenePack,
    SpoutSource,
    ImageFile,
    VideoFile,
    ShaderFile,
    NdiSource,
    CameraSource
};

struct ClipDef {
    std::string name;
    std::string body;
    bool is3d = false;
    ClipKind kind = ClipKind::Shader;
    int sceneIndex = -1;
    std::string path = {};
    bool shadertoy = false;
    std::string error = {};
    long long stamp = 0;
    unsigned long long bytes = 0;

    bool isShaderFile() const { return kind == ClipKind::ShaderFile; }
    bool isScene() const { return kind == ClipKind::BuiltinScene || kind == ClipKind::ModelFile || kind == ClipKind::ScenePack; }
    bool isMedia() const { return kind == ClipKind::ImageFile || kind == ClipKind::VideoFile; }
    bool isLive() const { return kind == ClipKind::NdiSource || kind == ClipKind::CameraSource; }
};

constexpr size_t kMaxModelClips = 64;
constexpr size_t kMaxPackClips = 128;
constexpr size_t kMaxMediaClips = 128;

struct AssetFile {
    std::string name;
    std::string path;
    unsigned long long bytes = 0;
};

std::string modelDirectory();
std::string texturesDirectory();
std::string clipsDirectory();
std::string mediaDirectory();
std::string shaderDirectory();
std::vector<AssetFile> listModelAssets();
std::vector<AssetFile> listTextureAssets();
std::vector<AssetFile> listPackAssets();
std::vector<AssetFile> listMediaAssets();
std::string imageClipName(const std::string& stem);
std::string videoClipName(const std::string& stem);
std::string packClipName(const std::string& stem);

const std::vector<ClipDef>& clipLibrary();
int rescanLibrary();

struct ShaderScan {
    std::vector<size_t> changed;
    int added = 0;
};

ShaderScan refreshShaderClips();
std::string createExampleShader();
int clipIndexByName(const std::string& name);
const std::string& clipVertexSource();
std::string clipFragmentSource(const ClipDef& clip);

}

#pragma once

#include "render/shader_source.hpp"

#include <cstddef>
#include <memory>
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
    CameraSource,
    PluginSource,
    TextFile,
    ParticleClip,
    RouteSource
};

struct ClipDef {
    std::string name;
    std::string body;
    bool is3d = false;
    ClipKind kind = ClipKind::Shader;
    int sceneIndex = -1;
    std::string path = {};
    bool shadertoy = false;
    std::shared_ptr<const ShaderPlan> plan = {};
    std::string error = {};
    long long stamp = 0;
    unsigned long long bytes = 0;

    bool isShaderFile() const { return kind == ClipKind::ShaderFile; }
    bool isAdvancedShader() const { return plan != nullptr; }
    int passCount() const { return plan ? static_cast<int>(plan->passes.size()) : 1; }
    bool isScene() const { return kind == ClipKind::BuiltinScene || kind == ClipKind::ModelFile || kind == ClipKind::ScenePack; }
    bool isMedia() const { return kind == ClipKind::ImageFile || kind == ClipKind::VideoFile; }
    bool isLive() const { return kind == ClipKind::NdiSource || kind == ClipKind::CameraSource || kind == ClipKind::PluginSource; }
    bool isGenerator() const { return kind == ClipKind::TextFile || kind == ClipKind::ParticleClip; }
    bool isRoute() const { return kind == ClipKind::RouteSource; }
};

constexpr size_t kMaxModelClips = 64;
constexpr size_t kMaxPackClips = 128;
constexpr size_t kMaxMediaClips = 128;
constexpr size_t kMaxTextClips = 64;
constexpr size_t kMaxParticleClips = 64;

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
std::string textClipName(const std::string& stem);
std::string particleClipName(const std::string& stem);
std::string generatorStem(const ClipDef& clip);
void setGeneratorClipsEnabled(bool enabled);
bool generatorClipsEnabled();

const std::vector<ClipDef>& clipLibrary();
int rescanLibrary();

struct ShaderScan {
    std::vector<size_t> changed;
    int added = 0;
};

ShaderScan refreshShaderClips();
ShaderScan refreshGeneratorClips();
std::string createExampleShader();
int clipIndexByName(const std::string& name);
const std::string& clipVertexSource();
std::string clipFragmentSource(const ClipDef& clip);
std::string clipPassFragmentSource(const ClipDef& clip, int pass);
std::string planPassFragmentSource(const ShaderPlan& plan, bool shadertoy, int pass);

}

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nvj {

enum class ClipKind {
    Shader,
    BuiltinScene,
    ModelFile
};

struct ClipDef {
    std::string name;
    std::string body;
    bool is3d = false;
    ClipKind kind = ClipKind::Shader;
    int sceneIndex = -1;
    std::string path = {};

    bool isScene() const { return kind != ClipKind::Shader; }
};

constexpr size_t kMaxModelClips = 64;

std::string modelDirectory();

const std::vector<ClipDef>& clipLibrary();
int clipIndexByName(const std::string& name);
const std::string& clipVertexSource();
std::string clipFragmentSource(const ClipDef& clip);

}

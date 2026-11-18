#pragma once

#include "render/clip_library.hpp"
#include "render/gl_util.hpp"
#include "render/scene_player.hpp"
#include "scene3d/scene_pack.hpp"

#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

enum class CreatorSelect { Scene, Object, Light };

enum class CreatorAction { None, New, Template, Pack };

struct CreatorState {
    CreatorState();

    SceneDoc doc;
    std::deque<SceneDoc> undo;
    std::deque<SceneDoc> redo;
    std::shared_ptr<ScenePack> pack;
    AssetSource source;
    std::filesystem::path file;
    bool dirty = false;
    bool editing = false;
    bool rebuild = true;
    CreatorSelect selectKind = CreatorSelect::Object;
    int selectIndex = 0;
    std::unique_ptr<ScenePlayer> shown;
    std::unique_ptr<ScenePlayer> pending;
    RenderTarget target;
    std::string buildError;
    std::vector<std::string> warnings;
    double time = 0.0;
    bool playing = true;
    bool freeCamera = true;
    bool simulateAudio = true;
    float yaw = 0.6f;
    float pitch = 0.32f;
    float distance = 1.0f;
    std::vector<AssetFile> models;
    std::vector<AssetFile> textures;
    std::vector<AssetFile> packs;
    CreatorAction action = CreatorAction::None;
    int actionIndex = -1;
    bool confirmOpen = false;
    std::string status;
    double statusUntil = 0.0;
};

}

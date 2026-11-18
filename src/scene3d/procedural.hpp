#pragma once

#include "scene3d/scene_model.hpp"

#include <vector>

namespace nvj {

struct BuiltinSceneInfo {
    const char* name;
    Scene (*build)();
};

Scene makeTorusKnotScene();
Scene makeCrystalClusterScene();
Scene makeOrbitRingsScene();
SceneMesh plainSphereMesh();
SceneMesh plainTorusMesh();
SceneMesh plainTorusKnotMesh();
const std::vector<BuiltinSceneInfo>& builtinScenes();

}

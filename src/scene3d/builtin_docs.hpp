#pragma once

#include "scene3d/scene_doc.hpp"

#include <vector>

namespace nvj {

struct BuiltinDocInfo {
    const char* name;
    SceneDoc (*make)();
};

const std::vector<BuiltinDocInfo>& builtinDocs();
Scene makeChromeOrbsScene();
Scene makeNeonCubesScene();
Scene makeGoldKnotScene();
Scene makePulseColumnsScene();

}

#pragma once

#include "scene3d/gltf.hpp"
#include "scene3d/scene_doc.hpp"

#include <functional>
#include <memory>
#include <string>

namespace nvj {

struct DocAssets {
    std::function<std::shared_ptr<const GltfResult>(const std::string& name, const GltfLimits& limits)> loadModel;
    std::function<std::shared_ptr<const SceneImage>(const std::string& name, std::string& error)> loadImage;
};

GltfResult buildSceneFromDoc(const SceneDoc& doc, const DocAssets& assets, const GltfLimits& limits = {});

}

#pragma once

#include "scene3d/scene_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nvj {

bool decodeSceneImage(const uint8_t* data, size_t size, int maxDimension, size_t maxBytes, SceneImage& out, std::string& error);
bool decodeMediaImage(const uint8_t* data, size_t size, int maxDimension, size_t maxBytes, SceneImage& out, std::string& error);

}

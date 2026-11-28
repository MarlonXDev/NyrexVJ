#pragma once

#include "scene3d/scene_model.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

struct GltfLimits {
    size_t maxFileBytes = 192u * 1024u * 1024u;
    size_t maxJsonBytes = 16u * 1024u * 1024u;
    size_t maxVertices = 2000000;
    size_t maxIndices = 6000000;
    size_t maxNodes = 10000;
    bool allowGraphs = true;
    size_t maxMeshes = 4096;
    size_t maxAnimationKeys = 200000;
    int maxImageDimension = 4096;
    size_t maxImageBytes = 192u * 1024u * 1024u;
    int maxJsonDepth = 64;
    size_t maxSkins = kSceneMaxSkins;
    size_t maxBones = kSceneMaxBones;
    size_t maxMorphTargets = kSceneMaxMorphTargets;
    size_t maxMorphBytes = 128u * 1024u * 1024u;
    size_t maxGpuBytes = kSceneMaxGpuBytes;
    double maxSeconds = 60.0;
    const std::atomic<bool>* cancel = nullptr;
};

struct GltfResult {
    bool ok = false;
    bool interrupted = false;
    std::string error;
    std::vector<std::string> warnings;
    double milliseconds = 0.0;
    Scene scene;
};

GltfResult loadGltfMemory(const uint8_t* data, size_t size, const std::filesystem::path& baseDirectory, const GltfLimits& limits = {});
GltfResult loadGltfFile(const std::filesystem::path& path, const GltfLimits& limits = {});
std::vector<uint8_t> writeGlb(const Scene& scene);

}

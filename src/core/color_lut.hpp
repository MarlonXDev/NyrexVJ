#pragma once

#include "core/color_model.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

constexpr int kMinCubeSize = 2;
constexpr int kMaxCubeSize = 65;
constexpr std::uintmax_t kMaxCubeFileBytes = 64ull * 1024ull * 1024ull;
constexpr size_t kMaxCubeNameBytes = 120;
constexpr double kMaxCubeValue = 16.0;

struct CubeLut {
    std::string title;
    int size = 0;
    std::array<float, 3> domainMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainMax{1.0f, 1.0f, 1.0f};
    std::vector<float> rgb;

    bool valid() const;
    size_t entries() const { return static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size); }
};

struct CubeParse {
    bool ok = false;
    std::string error;
    int line = 0;
    CubeLut lut;
};

CubeParse parseCube(const std::string& text);
CubeParse loadCubeFile(const std::filesystem::path& path);

CubeLut makeIdentityLut(int size);
Color3 sampleCube(const CubeLut& lut, const Color3& rgb);
double maxIdentityError(const CubeLut& lut);
bool isIdentityLut(const CubeLut& lut, double tolerance);

std::uint64_t cubeGpuBytes(int size);
std::uint64_t colorStageBytes(int width, int height, int lutSize);

std::filesystem::path lutDirectory();
std::vector<std::string> listCubeFiles(const std::filesystem::path& directory);
bool safeCubeName(const std::string& name);

}

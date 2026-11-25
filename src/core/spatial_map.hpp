#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

constexpr int kMaxSpatialSurfaces = 16;
constexpr int kMaxSpatialPoints = 32;
constexpr int kMaxSurfaceDivisions = 32;
constexpr float kMaxSpatialCoordinate = 1000.0f;
constexpr size_t kMaxSpatialNameLength = 40;
constexpr const char* kSpatialConvention = "right-handed, +X right, +Y up, the device looks along -Z, metres, degrees, output x to the right and y down from 0 to 1";

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool operator==(const Vec3f&) const = default;
};

struct SpatialPose {
    Vec3f position{0.0f, 1.5f, 6.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    bool operator==(const SpatialPose&) const = default;
};

struct SpatialLens {
    float fovY = 30.0f;
    float aspect = 16.0f / 9.0f;
    float shiftX = 0.0f;
    float shiftY = 0.0f;
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    bool operator==(const SpatialLens&) const = default;
};

struct SpatialDevice {
    SpatialPose pose;
    SpatialLens lens;
    bool operator==(const SpatialDevice&) const = default;
};

struct SpatialSurface {
    std::string name = "Wall";
    bool enabled = true;
    std::array<Vec3f, 4> corners{{{-2.5f, 3.0f, 0.0f}, {2.5f, 3.0f, 0.0f}, {2.5f, 0.1875f, 0.0f}, {-2.5f, 0.1875f, 0.0f}}};
    std::array<float, 4> uv{0.0f, 0.0f, 1.0f, 1.0f};
    int divisions = 4;
    bool doubleSided = false;
    bool operator==(const SpatialSurface&) const = default;
};

struct CalibrationPoint {
    std::string name;
    Vec3f world;
    float outputX = 0.5f;
    float outputY = 0.5f;
    bool enabled = true;
    bool operator==(const CalibrationPoint&) const = default;
};

enum class SpatialSource {
    SurfaceUv,
    ViewerCamera,
};

struct SpatialMapping {
    bool enabled = false;
    SpatialSource source = SpatialSource::SurfaceUv;
    SpatialDevice projector;
    SpatialDevice camera{SpatialPose{Vec3f{0.0f, 1.6f, 8.0f}, 0.0f, 0.0f, 0.0f}, SpatialLens{40.0f, 16.0f / 9.0f, 0.0f, 0.0f, 0.1f, 200.0f}};
    std::vector<SpatialSurface> surfaces;
    std::vector<CalibrationPoint> points;
    bool operator==(const SpatialMapping&) const = default;
};

using SpatialMatrix = std::array<float, 16>;

struct ProjectedPoint {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
    float w = 0.0f;
    bool inFront = false;
    bool inside = false;
};

Vec3f forwardOf(const SpatialPose& pose);
Vec3f rightOf(const SpatialPose& pose);
Vec3f upOf(const SpatialPose& pose);
SpatialPose poseLookAt(const Vec3f& position, const Vec3f& target, float rollDegrees = 0.0f);
float fovYFromThrowRatio(float throwRatio, float aspect);
float throwRatioFromFovY(float fovYDegrees, float aspect);

SpatialMatrix viewMatrix(const SpatialDevice& device);
SpatialMatrix projectionMatrix(const SpatialLens& lens);
SpatialMatrix viewProjectionMatrix(const SpatialDevice& device);
ProjectedPoint projectPoint(const SpatialDevice& device, const Vec3f& world);
bool deviceRay(const SpatialDevice& device, float x, float y, Vec3f& origin, Vec3f& direction);

Vec3f surfacePoint(const SpatialSurface& surface, float a, float b);
Vec3f surfaceNormal(const SpatialSurface& surface);
float surfaceArea(const SpatialSurface& surface);
bool surfaceUsable(const SpatialSurface& surface);
SpatialSurface wallSurface(const std::string& name, const Vec3f& center, float width, float height, float yawDegrees, float pitchDegrees);
std::vector<SpatialSurface> boxSurfaces(const std::string& name, const Vec3f& center, float width, float height, float depth);
bool addSpatialSurface(SpatialMapping& spatial, const SpatialSurface& surface);
bool removeSpatialSurface(SpatialMapping& spatial, int index);
bool addCalibrationPoint(SpatialMapping& spatial, const CalibrationPoint& point);
bool removeCalibrationPoint(SpatialMapping& spatial, int index);

void normalizeDevice(SpatialDevice& device);
void normalizeSurface(SpatialSurface& surface);
void normalizeSpatial(SpatialMapping& spatial);
bool isDefaultSpatial(const SpatialMapping& spatial);
SpatialMapping defaultSpatialSetup();
int usableSurfaceCount(const SpatialMapping& spatial);

nlohmann::json deviceToJson(const SpatialDevice& device);
bool deviceFromJson(const nlohmann::json& doc, SpatialDevice& device);
nlohmann::json deviceMatricesToJson(const SpatialDevice& device);
nlohmann::json spatialToJson(const SpatialMapping& spatial);
bool spatialFromJson(const nlohmann::json& doc, SpatialMapping& spatial);

}

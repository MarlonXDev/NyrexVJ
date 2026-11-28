#pragma once

#include "scene3d/scene_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace nvj {

constexpr int kWorldCount = 4;
constexpr float kWorldOffsetLimit = 4.0f;
constexpr float kWorldYawLimit = 180.0f;
constexpr float kWorldScaleMin = 0.1f;
constexpr float kWorldScaleMax = 4.0f;

struct WorldPlacement {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yawDegrees = 0.0f;
    float scale = 1.0f;
};

inline bool worldPlacementIsIdentity(const WorldPlacement& placement) {
    return placement.x == 0.0f && placement.y == 0.0f && placement.z == 0.0f && placement.yawDegrees == 0.0f && placement.scale == 1.0f;
}

inline WorldPlacement clampWorldPlacement(WorldPlacement placement) {
    placement.x = std::clamp(placement.x, -kWorldOffsetLimit, kWorldOffsetLimit);
    placement.y = std::clamp(placement.y, -kWorldOffsetLimit, kWorldOffsetLimit);
    placement.z = std::clamp(placement.z, -kWorldOffsetLimit, kWorldOffsetLimit);
    placement.yawDegrees = std::clamp(placement.yawDegrees, -kWorldYawLimit, kWorldYawLimit);
    placement.scale = std::clamp(placement.scale, kWorldScaleMin, kWorldScaleMax);
    return placement;
}

inline Mat4 worldPlacementMatrix(Vec3 ownerCenter, float ownerRadius, Vec3 memberCenter, float memberRadius, const WorldPlacement& placement) {
    const WorldPlacement p = clampWorldPlacement(placement);
    const float owner = std::max(ownerRadius, 1e-4f);
    const float member = std::max(memberRadius, 1e-4f);
    const float fit = p.scale * owner / member;
    const Vec3 target = ownerCenter + Vec3{p.x, p.y, p.z} * owner;
    const float yaw = p.yawDegrees * 0.0174532925f;
    return translation(target) * rotation(axisAngle({0.0f, 1.0f, 0.0f}, yaw)) * scaling({fit, fit, fit}) * translation(memberCenter * -1.0f);
}

struct FrustumPlanes {
    std::array<std::array<float, 4>, 6> planes{};
};

inline FrustumPlanes frustumFromViewProjection(const Mat4& viewProj) {
    const auto row = [&](size_t i) { return std::array<float, 4>{viewProj.m[i], viewProj.m[4 + i], viewProj.m[8 + i], viewProj.m[12 + i]}; };
    const std::array<float, 4> r0 = row(0);
    const std::array<float, 4> r1 = row(1);
    const std::array<float, 4> r2 = row(2);
    const std::array<float, 4> r3 = row(3);
    FrustumPlanes frustum;
    for (size_t k = 0; k < 4; ++k) {
        frustum.planes[0][k] = r3[k] + r0[k];
        frustum.planes[1][k] = r3[k] - r0[k];
        frustum.planes[2][k] = r3[k] + r1[k];
        frustum.planes[3][k] = r3[k] - r1[k];
        frustum.planes[4][k] = r3[k] + r2[k];
        frustum.planes[5][k] = r3[k] - r2[k];
    }
    for (std::array<float, 4>& plane : frustum.planes) {
        const float length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
        if (length > 1e-12f) {
            for (float& value : plane) {
                value /= length;
            }
        }
    }
    return frustum;
}

inline bool sphereMayBeVisible(const FrustumPlanes& frustum, Vec3 centre, float radius) {
    for (const std::array<float, 4>& plane : frustum.planes) {
        if (plane[0] * centre.x + plane[1] * centre.y + plane[2] * centre.z + plane[3] < -radius) {
            return false;
        }
    }
    return true;
}

inline float largestAxisScale(const Mat4& model) {
    float best = 0.0f;
    for (size_t column = 0; column < 3; ++column) {
        const float x = model.m[column * 4 + 0];
        const float y = model.m[column * 4 + 1];
        const float z = model.m[column * 4 + 2];
        best = std::max(best, std::sqrt(x * x + y * y + z * z));
    }
    return best;
}

}

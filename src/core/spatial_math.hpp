#pragma once

#include "core/spatial_map.hpp"

#include <array>
#include <cmath>

namespace nvj::spatial {

constexpr double kPi = 3.14159265358979323846;

struct V3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }

inline V3 normalized(V3 a) {
    const double len = length(a);
    return len > 1e-300 ? a * (1.0 / len) : V3{0.0, 0.0, -1.0};
}

inline V3 toV3(const Vec3f& v) { return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)}; }
inline Vec3f toVec3f(V3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

struct Mat3 {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

inline V3 mul(const Mat3& r, V3 v) {
    return {r.m[0] * v.x + r.m[1] * v.y + r.m[2] * v.z, r.m[3] * v.x + r.m[4] * v.y + r.m[5] * v.z, r.m[6] * v.x + r.m[7] * v.y + r.m[8] * v.z};
}

inline V3 mulTransposed(const Mat3& r, V3 v) {
    return {r.m[0] * v.x + r.m[3] * v.y + r.m[6] * v.z, r.m[1] * v.x + r.m[4] * v.y + r.m[7] * v.z, r.m[2] * v.x + r.m[5] * v.y + r.m[8] * v.z};
}

inline Mat3 rotationFromAngles(double yawDegrees, double pitchDegrees, double rollDegrees) {
    const double yaw = yawDegrees * kPi / 180.0;
    const double pitch = pitchDegrees * kPi / 180.0;
    const double roll = rollDegrees * kPi / 180.0;
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cx = std::cos(pitch), sx = std::sin(pitch);
    const double cz = std::cos(roll), sz = std::sin(roll);
    const double yx[9] = {cy, sy * sx, sy * cx, 0.0, cx, -sx, -sy, cy * sx, cy * cx};
    Mat3 r;
    r.m[0] = yx[0] * cz + yx[1] * sz;
    r.m[1] = -yx[0] * sz + yx[1] * cz;
    r.m[2] = yx[2];
    r.m[3] = yx[3] * cz + yx[4] * sz;
    r.m[4] = -yx[3] * sz + yx[4] * cz;
    r.m[5] = yx[5];
    r.m[6] = yx[6] * cz + yx[7] * sz;
    r.m[7] = -yx[6] * sz + yx[7] * cz;
    r.m[8] = yx[8];
    return r;
}

inline Mat3 rotationOf(const SpatialPose& pose) {
    return rotationFromAngles(static_cast<double>(pose.yaw), static_cast<double>(pose.pitch), static_cast<double>(pose.roll));
}

struct Clip {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

struct LensTerms {
    double fx = 1.0;
    double fy = 1.0;
    double shiftX = 0.0;
    double shiftY = 0.0;
    double a = 0.0;
    double b = 0.0;
};

inline LensTerms lensTermsWithFov(const SpatialLens& lens, double fovY) {
    LensTerms t;
    const double f = 1.0 / std::tan(fovY * 0.5 * kPi / 180.0);
    t.fx = f / static_cast<double>(lens.aspect);
    t.fy = f;
    t.shiftX = 2.0 * static_cast<double>(lens.shiftX);
    t.shiftY = 2.0 * static_cast<double>(lens.shiftY);
    const double n = static_cast<double>(lens.nearPlane);
    const double fr = static_cast<double>(lens.farPlane);
    t.a = (fr + n) / (n - fr);
    t.b = 2.0 * fr * n / (n - fr);
    return t;
}

inline LensTerms lensTerms(const SpatialLens& lens) {
    return lensTermsWithFov(lens, static_cast<double>(lens.fovY));
}

inline Clip toClip(const LensTerms& t, const Mat3& r, V3 position, V3 world) {
    const V3 e = mulTransposed(r, world - position);
    return {t.fx * e.x + t.shiftX * e.z, t.fy * e.y + t.shiftY * e.z, t.a * e.z + t.b, -e.z};
}

}

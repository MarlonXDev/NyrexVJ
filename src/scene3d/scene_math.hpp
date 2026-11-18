#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace nvj {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }

inline Vec3 normalize(Vec3 a) {
    const float len = length(a);
    return len > 1e-12f ? a * (1.0f / len) : Vec3{0.0f, 1.0f, 0.0f};
}

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

inline Quat normalize(Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-12f) {
        return Quat{};
    }
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    const float theta = std::acos(std::min(d, 1.0f));
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

inline Quat axisAngle(Vec3 axis, float angle) {
    const Vec3 n = normalize(axis);
    const float s = std::sin(angle * 0.5f);
    return {n.x * s, n.y * s, n.z * s, std::cos(angle * 0.5f)};
}

struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[static_cast<size_t>(k * 4 + row)] * b.m[static_cast<size_t>(c * 4 + k)];
            }
            r.m[static_cast<size_t>(c * 4 + row)] = sum;
        }
    }
    return r;
}

inline Mat4 translation(Vec3 t) {
    Mat4 r;
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

inline Mat4 scaling(Vec3 s) {
    Mat4 r;
    r.m[0] = s.x;
    r.m[5] = s.y;
    r.m[10] = s.z;
    return r;
}

inline Mat4 rotation(Quat q) {
    q = normalize(q);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    Mat4 r;
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

inline Mat4 composeTrs(Vec3 t, Quat r, Vec3 s) {
    return translation(t) * rotation(r) * scaling(s);
}

inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    r.m.fill(0.0f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

inline Vec3 transformPoint(const Mat4& a, Vec3 p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12], a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13], a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]};
}

inline std::array<float, 9> normalMatrix(const Mat4& a) {
    const float m00 = a.m[0], m01 = a.m[4], m02 = a.m[8];
    const float m10 = a.m[1], m11 = a.m[5], m12 = a.m[9];
    const float m20 = a.m[2], m21 = a.m[6], m22 = a.m[10];
    const float c00 = m11 * m22 - m12 * m21;
    const float c01 = m12 * m20 - m10 * m22;
    const float c02 = m10 * m21 - m11 * m20;
    const float c10 = m02 * m21 - m01 * m22;
    const float c11 = m00 * m22 - m02 * m20;
    const float c12 = m01 * m20 - m00 * m21;
    const float c20 = m01 * m12 - m02 * m11;
    const float c21 = m02 * m10 - m00 * m12;
    const float c22 = m00 * m11 - m01 * m10;
    float det = m00 * c00 + m01 * c01 + m02 * c02;
    if (std::fabs(det) < 1e-12f) {
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    const float inv = 1.0f / det;
    return {c00 * inv, c10 * inv, c20 * inv, c01 * inv, c11 * inv, c21 * inv, c02 * inv, c12 * inv, c22 * inv};
}

}

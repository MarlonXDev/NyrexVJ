#include "core/spatial_mesh.hpp"

#include "core/spatial_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace nvj {

namespace {

using spatial::Clip;
using spatial::V3;

constexpr double kRayEdgeTolerance = 1e-6;

struct ClipVertex {
    Clip clip;
    double s = 0.0;
    double t = 0.0;
    double q = 1.0;
};

struct Terms {
    spatial::LensTerms lens;
    spatial::Mat3 rotation;
    V3 position;
};

Terms termsOf(const SpatialDevice& device) {
    Terms t;
    t.lens = spatial::lensTerms(device.lens);
    t.rotation = spatial::rotationOf(device.pose);
    t.position = spatial::toV3(device.pose.position);
    return t;
}

V3 bilinear(const SpatialSurface& surface, double a, double b) {
    const V3 c0 = spatial::toV3(surface.corners[0]);
    const V3 c1 = spatial::toV3(surface.corners[1]);
    const V3 c2 = spatial::toV3(surface.corners[2]);
    const V3 c3 = spatial::toV3(surface.corners[3]);
    const V3 top = c0 * (1.0 - a) + c1 * a;
    const V3 bottom = c3 * (1.0 - a) + c2 * a;
    return top * (1.0 - b) + bottom * b;
}

void localCoords(const SpatialSurface& surface, double a, double b, double& lx, double& ly) {
    lx = static_cast<double>(surface.uv[0]) + a * static_cast<double>(surface.uv[2] - surface.uv[0]);
    ly = static_cast<double>(surface.uv[1]) + b * static_cast<double>(surface.uv[3] - surface.uv[1]);
}

ClipVertex makeVertex(const OutputMapping& map, const Terms& projector, const Terms& camera, const V3& world, double lx, double ly) {
    ClipVertex v;
    v.clip = spatial::toClip(projector.lens, projector.rotation, projector.position, world);
    if (map.spatial.source == SpatialSource::ViewerCamera) {
        const Clip c = spatial::toClip(camera.lens, camera.rotation, camera.position, world);
        const double q = c.w;
        double lxq = 0.5 * q + 0.5 * c.x;
        double lyq = 0.5 * q - 0.5 * c.y;
        if (map.flipX) {
            lxq = q - lxq;
        }
        if (map.flipY) {
            lyq = q - lyq;
        }
        v.s = static_cast<double>(map.slice[0]) * q + static_cast<double>(map.slice[2]) * lxq;
        v.t = q - (static_cast<double>(map.slice[1]) * q + static_cast<double>(map.slice[3]) * lyq);
        v.q = q;
    } else {
        const TextureCoord tc = contentCoords(map, lx, ly);
        v.s = static_cast<double>(tc.u);
        v.t = static_cast<double>(tc.v);
        v.q = 1.0;
    }
    return v;
}

ClipVertex lerpVertex(const ClipVertex& a, const ClipVertex& b, double f) {
    ClipVertex r;
    r.clip.x = a.clip.x + (b.clip.x - a.clip.x) * f;
    r.clip.y = a.clip.y + (b.clip.y - a.clip.y) * f;
    r.clip.z = a.clip.z + (b.clip.z - a.clip.z) * f;
    r.clip.w = a.clip.w + (b.clip.w - a.clip.w) * f;
    r.s = a.s + (b.s - a.s) * f;
    r.t = a.t + (b.t - a.t) * f;
    r.q = a.q + (b.q - a.q) * f;
    return r;
}

double nearDistance(const ClipVertex& v) {
    return v.clip.z + v.clip.w;
}

SpatialVertex emit(const ClipVertex& v) {
    SpatialVertex out;
    const double w = std::max(v.clip.w, 1e-9);
    out.x = static_cast<float>(0.5 + 0.5 * v.clip.x / w);
    out.y = static_cast<float>(0.5 - 0.5 * v.clip.y / w);
    out.depth = static_cast<float>(v.clip.z / w);
    out.w = static_cast<float>(w);
    out.s = static_cast<float>(v.s);
    out.t = static_cast<float>(v.t);
    out.q = static_cast<float>(v.q);
    return out;
}

bool outsideSamePlane(const std::vector<ClipVertex>& poly) {
    bool left = true;
    bool right = true;
    bool top = true;
    bool bottom = true;
    for (const ClipVertex& v : poly) {
        left = left && v.clip.x < -v.clip.w;
        right = right && v.clip.x > v.clip.w;
        bottom = bottom && v.clip.y < -v.clip.w;
        top = top && v.clip.y > v.clip.w;
    }
    return left || right || top || bottom;
}

}

TextureCoord contentCoords(const OutputMapping& map, double localX, double localY) {
    const double sourceS = map.flipX ? 1.0 - localX : localX;
    const double sourceT = map.flipY ? 1.0 - localY : localY;
    TextureCoord tc;
    tc.u = static_cast<float>(static_cast<double>(map.slice[0]) + static_cast<double>(map.slice[2]) * sourceS);
    tc.v = static_cast<float>(1.0 - (static_cast<double>(map.slice[1]) + static_cast<double>(map.slice[3]) * sourceT));
    return tc;
}

SpatialMesh buildSpatialMesh(const OutputMapping& map) {
    SpatialMesh mesh;
    if (!map.spatial.enabled) {
        return mesh;
    }
    const Terms projector = termsOf(map.spatial.projector);
    const Terms camera = termsOf(map.spatial.camera);
    for (const SpatialSurface& surface : map.spatial.surfaces) {
        if (!surfaceUsable(surface)) {
            continue;
        }
        ++mesh.stats.surfaces;
        const int n = std::clamp(surface.divisions, 1, kMaxSurfaceDivisions);
        const size_t stride = static_cast<size_t>(n + 1);
        std::vector<V3> world(stride * stride);
        std::vector<ClipVertex> clip(stride * stride);
        for (int j = 0; j <= n; ++j) {
            for (int i = 0; i <= n; ++i) {
                const double a = static_cast<double>(i) / static_cast<double>(n);
                const double b = static_cast<double>(j) / static_cast<double>(n);
                const size_t index = static_cast<size_t>(j) * stride + static_cast<size_t>(i);
                world[index] = bilinear(surface, a, b);
                double lx = 0.0;
                double ly = 0.0;
                localCoords(surface, a, b, lx, ly);
                clip[index] = makeVertex(map, projector, camera, world[index], lx, ly);
            }
        }
        std::vector<int64_t> emitted(stride * stride, -1);
        const auto emitShared = [&](size_t index) -> uint32_t {
            if (emitted[index] < 0) {
                emitted[index] = static_cast<int64_t>(mesh.vertices.size());
                mesh.vertices.push_back(emit(clip[index]));
            }
            return static_cast<uint32_t>(emitted[index]);
        };
        const auto triangle = [&](size_t i0, size_t i1, size_t i2) {
            if (mesh.bytes() > kMaxSpatialMeshBytes) {
                return;
            }
            ++mesh.stats.trianglesIn;
            const V3 normal = spatial::cross(world[i1] - world[i0], world[i2] - world[i0]) * -1.0;
            if (spatial::length(normal) < 1e-12) {
                ++mesh.stats.degenerate;
                return;
            }
            if (!surface.doubleSided) {
                const V3 centre = (world[i0] + world[i1] + world[i2]) * (1.0 / 3.0);
                if (spatial::dot(normal, projector.position - centre) <= 0.0) {
                    ++mesh.stats.culled;
                    return;
                }
            }
            const double d0 = nearDistance(clip[i0]);
            const double d1 = nearDistance(clip[i1]);
            const double d2 = nearDistance(clip[i2]);
            if (d0 < 0.0 && d1 < 0.0 && d2 < 0.0) {
                ++mesh.stats.behind;
                return;
            }
            if (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) {
                const std::vector<ClipVertex> poly{clip[i0], clip[i1], clip[i2]};
                if (outsideSamePlane(poly)) {
                    ++mesh.stats.offscreen;
                    return;
                }
                const uint32_t a = emitShared(i0);
                const uint32_t b = emitShared(i1);
                const uint32_t c = emitShared(i2);
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
                ++mesh.stats.trianglesOut;
                return;
            }
            ++mesh.stats.clipped;
            std::vector<ClipVertex> poly;
            const std::array<const ClipVertex*, 3> v{&clip[i0], &clip[i1], &clip[i2]};
            const std::array<double, 3> d{d0, d1, d2};
            for (size_t k = 0; k < 3; ++k) {
                const size_t next = (k + 1) % 3;
                if (d[k] >= 0.0) {
                    poly.push_back(*v[k]);
                }
                if ((d[k] >= 0.0) != (d[next] >= 0.0)) {
                    const double f = d[k] / (d[k] - d[next]);
                    poly.push_back(lerpVertex(*v[k], *v[next], f));
                }
            }
            if (poly.size() < 3) {
                return;
            }
            if (outsideSamePlane(poly)) {
                ++mesh.stats.offscreen;
                return;
            }
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            for (const ClipVertex& pv : poly) {
                mesh.vertices.push_back(emit(pv));
            }
            for (uint32_t k = 1; k + 1 < static_cast<uint32_t>(poly.size()); ++k) {
                mesh.indices.insert(mesh.indices.end(), {base, base + k, base + k + 1});
                ++mesh.stats.trianglesOut;
            }
        };
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const size_t a = static_cast<size_t>(j) * stride + static_cast<size_t>(i);
                const size_t b = a + 1;
                const size_t c = a + stride;
                const size_t d = c + 1;
                triangle(a, b, d);
                triangle(a, d, c);
            }
        }
    }
    return mesh;
}

size_t spatialMeshBytesEstimate(const SpatialMapping& spatial) {
    if (!spatial.enabled) {
        return 0;
    }
    size_t total = 0;
    for (const SpatialSurface& surface : spatial.surfaces) {
        if (!surfaceUsable(surface)) {
            continue;
        }
        const size_t n = static_cast<size_t>(std::clamp(surface.divisions, 1, kMaxSurfaceDivisions));
        total += (n + 1u) * (n + 1u) * sizeof(SpatialVertex) + n * n * 6u * sizeof(uint32_t);
    }
    return std::min(total, kMaxSpatialMeshBytes);
}

SpatialHit castProjectorRay(const OutputMapping& map, float outputX, float outputY) {
    SpatialHit best;
    if (!map.spatial.enabled) {
        return best;
    }
    Vec3f originF;
    Vec3f directionF;
    if (!deviceRay(map.spatial.projector, outputX, outputY, originF, directionF)) {
        return best;
    }
    const V3 origin = spatial::toV3(originF);
    const V3 direction = spatial::toV3(directionF);
    const V3 forward = spatial::toV3(forwardOf(map.spatial.projector.pose));
    const double nearPlane = static_cast<double>(map.spatial.projector.lens.nearPlane);
    double bestT = 1e300;
    double bestLx = 0.0;
    double bestLy = 0.0;
    for (size_t si = 0; si < map.spatial.surfaces.size(); ++si) {
        const SpatialSurface& surface = map.spatial.surfaces[si];
        if (!surfaceUsable(surface)) {
            continue;
        }
        const int n = std::clamp(surface.divisions, 1, kMaxSurfaceDivisions);
        const size_t stride = static_cast<size_t>(n + 1);
        std::vector<V3> world(stride * stride);
        for (int j = 0; j <= n; ++j) {
            for (int i = 0; i <= n; ++i) {
                world[static_cast<size_t>(j) * stride + static_cast<size_t>(i)] = bilinear(surface, static_cast<double>(i) / n, static_cast<double>(j) / n);
            }
        }
        const auto test = [&](size_t i0, size_t i1, size_t i2) {
            const V3 p0 = world[i0];
            const V3 e1 = world[i1] - p0;
            const V3 e2 = world[i2] - p0;
            const V3 front = spatial::cross(e1, e2) * -1.0;
            if (spatial::length(front) < 1e-12) {
                return;
            }
            if (!surface.doubleSided && spatial::dot(front, direction) >= 0.0) {
                return;
            }
            const V3 p = spatial::cross(direction, e2);
            const double det = spatial::dot(e1, p);
            if (std::fabs(det) < 1e-14) {
                return;
            }
            const double inv = 1.0 / det;
            const V3 tvec = origin - p0;
            const double u = spatial::dot(tvec, p) * inv;
            if (u < -kRayEdgeTolerance || u > 1.0 + kRayEdgeTolerance) {
                return;
            }
            const V3 qvec = spatial::cross(tvec, e1);
            const double v = spatial::dot(direction, qvec) * inv;
            if (v < -kRayEdgeTolerance || u + v > 1.0 + kRayEdgeTolerance) {
                return;
            }
            const double t = spatial::dot(e2, qvec) * inv;
            if (t <= 1e-9 || t >= bestT) {
                return;
            }
            const V3 hit = origin + direction * t;
            if (spatial::dot(hit - origin, forward) < nearPlane) {
                return;
            }
            const auto param = [&](size_t index, double& a, double& b) {
                a = static_cast<double>(index % stride) / static_cast<double>(n);
                b = static_cast<double>(index / stride) / static_cast<double>(n);
            };
            double a0, b0, a1, b1, a2, b2;
            param(i0, a0, b0);
            param(i1, a1, b1);
            param(i2, a2, b2);
            const double w0 = 1.0 - u - v;
            const double a = std::clamp(w0 * a0 + u * a1 + v * a2, 0.0, 1.0);
            const double b = std::clamp(w0 * b0 + u * b1 + v * b2, 0.0, 1.0);
            bestT = t;
            best.hit = true;
            best.surface = static_cast<int>(si);
            best.world = spatial::toVec3f(hit);
            best.distance = static_cast<float>(t);
            localCoords(surface, a, b, bestLx, bestLy);
        };
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const size_t a = static_cast<size_t>(j) * stride + static_cast<size_t>(i);
                const size_t b = a + 1;
                const size_t c = a + stride;
                const size_t d = c + 1;
                test(a, b, d);
                test(a, d, c);
            }
        }
    }
    if (!best.hit) {
        return best;
    }
    if (map.spatial.source == SpatialSource::ViewerCamera) {
        const Terms camera = termsOf(map.spatial.camera);
        const Clip c = spatial::toClip(camera.lens, camera.rotation, camera.position, spatial::toV3(best.world));
        if (c.w > 1e-9) {
            const double lx = 0.5 + 0.5 * c.x / c.w;
            const double ly = 0.5 - 0.5 * c.y / c.w;
            const TextureCoord tc = contentCoords(map, lx, ly);
            best.u = tc.u;
            best.v = tc.v;
            best.contentValid = lx >= 0.0 && lx <= 1.0 && ly >= 0.0 && ly <= 1.0;
        }
    } else {
        const TextureCoord tc = contentCoords(map, bestLx, bestLy);
        best.u = tc.u;
        best.v = tc.v;
        best.contentValid = true;
    }
    return best;
}

bool pointLitByProjector(const OutputMapping& map, const Vec3f& world, float tolerance) {
    if (!map.spatial.enabled) {
        return false;
    }
    const ProjectedPoint p = projectPoint(map.spatial.projector, world);
    if (!p.inFront || !p.inside) {
        return false;
    }
    const SpatialHit hit = castProjectorRay(map, p.x, p.y);
    if (!hit.hit) {
        return false;
    }
    const V3 origin = spatial::toV3(map.spatial.projector.pose.position);
    const double expected = spatial::length(spatial::toV3(world) - origin);
    return std::fabs(static_cast<double>(hit.distance) - expected) <= static_cast<double>(tolerance) * std::max(1.0, expected);
}

float projectorBlendWeight(const OutputMapping& map, const Vec3f& world) {
    if (!pointLitByProjector(map, world)) {
        return 0.0f;
    }
    const ProjectedPoint p = projectPoint(map.spatial.projector, world);
    return edgeFactor(map.blend, p.x, p.y);
}

}

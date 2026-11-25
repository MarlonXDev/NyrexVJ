#include "core/spatial_map.hpp"

#include "core/spatial_math.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

using spatial::V3;

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float clampValue(float value, float low, float high, float fallback) {
    value = finiteOr(value, fallback);
    return std::min(std::max(value, low), high);
}

float wrapDegrees(float value) {
    value = finiteOr(value, 0.0f);
    value = std::fmod(value, 360.0f);
    if (value > 180.0f) {
        value -= 360.0f;
    } else if (value < -180.0f) {
        value += 360.0f;
    }
    return value;
}

void normalizeVec(Vec3f& v) {
    v.x = clampValue(v.x, -kMaxSpatialCoordinate, kMaxSpatialCoordinate, 0.0f);
    v.y = clampValue(v.y, -kMaxSpatialCoordinate, kMaxSpatialCoordinate, 0.0f);
    v.z = clampValue(v.z, -kMaxSpatialCoordinate, kMaxSpatialCoordinate, 0.0f);
}

std::string cleanName(const std::string& name) {
    std::string result;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || u == 127) {
            continue;
        }
        result.push_back(c);
    }
    if (result.size() > kMaxSpatialNameLength) {
        size_t cut = kMaxSpatialNameLength;
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0u) == 0x80u) {
            --cut;
        }
        result.resize(cut);
    }
    return result;
}

nlohmann::json vecToJson(const Vec3f& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

bool readVec(const nlohmann::json& doc, const char* key, Vec3f& out) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_array() || it->size() != 3) {
        return false;
    }
    for (const nlohmann::json& item : *it) {
        if (!item.is_number()) {
            return false;
        }
    }
    out = Vec3f{(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    return true;
}

float readNumber(const nlohmann::json& doc, const char* key, float fallback) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<float>();
}

bool readFlag(const nlohmann::json& doc, const char* key, bool fallback) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::string readText(const nlohmann::json& doc, const char* key, const std::string& fallback) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

nlohmann::json surfaceToJson(const SpatialSurface& surface) {
    nlohmann::json doc;
    doc["name"] = surface.name;
    doc["enabled"] = surface.enabled;
    doc["corners"] = nlohmann::json::array({vecToJson(surface.corners[0]), vecToJson(surface.corners[1]), vecToJson(surface.corners[2]), vecToJson(surface.corners[3])});
    doc["uv"] = nlohmann::json::array({surface.uv[0], surface.uv[1], surface.uv[2], surface.uv[3]});
    doc["divisions"] = surface.divisions;
    if (surface.doubleSided) {
        doc["doubleSided"] = true;
    }
    return doc;
}

bool surfaceFromJson(const nlohmann::json& doc, SpatialSurface& surface) {
    if (!doc.is_object()) {
        return false;
    }
    SpatialSurface result;
    result.name = cleanName(readText(doc, "name", result.name));
    result.enabled = readFlag(doc, "enabled", true);
    auto corners = doc.find("corners");
    if (corners == doc.end() || !corners->is_array() || corners->size() != 4) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        const nlohmann::json& corner = (*corners)[i];
        if (!corner.is_array() || corner.size() != 3 || !corner[0].is_number() || !corner[1].is_number() || !corner[2].is_number()) {
            return false;
        }
        result.corners[i] = Vec3f{corner[0].get<float>(), corner[1].get<float>(), corner[2].get<float>()};
    }
    auto uv = doc.find("uv");
    if (uv != doc.end() && uv->is_array() && uv->size() == 4) {
        for (size_t i = 0; i < 4; ++i) {
            if ((*uv)[i].is_number()) {
                result.uv[i] = (*uv)[i].get<float>();
            }
        }
    }
    auto divisions = doc.find("divisions");
    if (divisions != doc.end() && divisions->is_number_integer()) {
        result.divisions = divisions->get<int>();
    }
    result.doubleSided = readFlag(doc, "doubleSided", false);
    normalizeSurface(result);
    surface = result;
    return true;
}

}

Vec3f forwardOf(const SpatialPose& pose) {
    return spatial::toVec3f(spatial::mul(spatial::rotationOf(pose), V3{0.0, 0.0, -1.0}));
}

Vec3f rightOf(const SpatialPose& pose) {
    return spatial::toVec3f(spatial::mul(spatial::rotationOf(pose), V3{1.0, 0.0, 0.0}));
}

Vec3f upOf(const SpatialPose& pose) {
    return spatial::toVec3f(spatial::mul(spatial::rotationOf(pose), V3{0.0, 1.0, 0.0}));
}

SpatialPose poseLookAt(const Vec3f& position, const Vec3f& target, float rollDegrees) {
    SpatialPose pose;
    pose.position = position;
    const V3 f = spatial::normalized(spatial::toV3(target) - spatial::toV3(position));
    pose.yaw = static_cast<float>(std::atan2(-f.x, -f.z) * 180.0 / spatial::kPi);
    pose.pitch = static_cast<float>(std::asin(std::min(1.0, std::max(-1.0, f.y))) * 180.0 / spatial::kPi);
    pose.roll = rollDegrees;
    return pose;
}

float fovYFromThrowRatio(float throwRatio, float aspect) {
    if (!(throwRatio > 0.05f) || !(aspect > 0.0f)) {
        return 30.0f;
    }
    const double horizontal = 2.0 * std::atan(0.5 / static_cast<double>(throwRatio));
    const double vertical = 2.0 * std::atan(std::tan(horizontal * 0.5) / static_cast<double>(aspect));
    return static_cast<float>(vertical * 180.0 / spatial::kPi);
}

float throwRatioFromFovY(float fovYDegrees, float aspect) {
    if (!(fovYDegrees > 0.0f) || !(aspect > 0.0f)) {
        return 1.5f;
    }
    const double vertical = static_cast<double>(fovYDegrees) * spatial::kPi / 180.0;
    const double halfWidth = std::tan(vertical * 0.5) * static_cast<double>(aspect);
    return static_cast<float>(0.5 / halfWidth);
}

SpatialMatrix viewMatrix(const SpatialDevice& device) {
    const spatial::Mat3 r = spatial::rotationOf(device.pose);
    const V3 p = spatial::toV3(device.pose.position);
    const V3 t = spatial::mulTransposed(r, p) * -1.0;
    SpatialMatrix m{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[static_cast<size_t>(col * 4 + row)] = static_cast<float>(r.m[static_cast<size_t>(col * 3 + row)]);
        }
    }
    m[12] = static_cast<float>(t.x);
    m[13] = static_cast<float>(t.y);
    m[14] = static_cast<float>(t.z);
    m[15] = 1.0f;
    return m;
}

SpatialMatrix projectionMatrix(const SpatialLens& lens) {
    const spatial::LensTerms t = spatial::lensTerms(lens);
    SpatialMatrix m{};
    m[0] = static_cast<float>(t.fx);
    m[5] = static_cast<float>(t.fy);
    m[8] = static_cast<float>(t.shiftX);
    m[9] = static_cast<float>(t.shiftY);
    m[10] = static_cast<float>(t.a);
    m[11] = -1.0f;
    m[14] = static_cast<float>(t.b);
    return m;
}

SpatialMatrix viewProjectionMatrix(const SpatialDevice& device) {
    const SpatialMatrix v = viewMatrix(device);
    const SpatialMatrix p = projectionMatrix(device.lens);
    SpatialMatrix r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += static_cast<double>(p[static_cast<size_t>(k * 4 + row)]) * static_cast<double>(v[static_cast<size_t>(c * 4 + k)]);
            }
            r[static_cast<size_t>(c * 4 + row)] = static_cast<float>(sum);
        }
    }
    return r;
}

ProjectedPoint projectPoint(const SpatialDevice& device, const Vec3f& world) {
    const spatial::Mat3 r = spatial::rotationOf(device.pose);
    const spatial::Clip c = spatial::toClip(spatial::lensTerms(device.lens), r, spatial::toV3(device.pose.position), spatial::toV3(world));
    ProjectedPoint result;
    result.w = static_cast<float>(c.w);
    result.inFront = c.w > 1e-9;
    if (!result.inFront) {
        return result;
    }
    const double nx = c.x / c.w;
    const double ny = c.y / c.w;
    const double nz = c.z / c.w;
    result.x = static_cast<float>(0.5 + 0.5 * nx);
    result.y = static_cast<float>(0.5 - 0.5 * ny);
    result.depth = static_cast<float>(nz);
    result.inside = std::fabs(nx) <= 1.0 && std::fabs(ny) <= 1.0 && std::fabs(nz) <= 1.0;
    return result;
}

bool deviceRay(const SpatialDevice& device, float x, float y, Vec3f& origin, Vec3f& direction) {
    const spatial::LensTerms t = spatial::lensTerms(device.lens);
    const double ndcX = 2.0 * static_cast<double>(x) - 1.0;
    const double ndcY = 1.0 - 2.0 * static_cast<double>(y);
    const V3 eye{(ndcX + t.shiftX) / t.fx, (ndcY + t.shiftY) / t.fy, -1.0};
    const V3 world = spatial::normalized(spatial::mul(spatial::rotationOf(device.pose), eye));
    origin = device.pose.position;
    direction = spatial::toVec3f(world);
    return std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z);
}

Vec3f surfacePoint(const SpatialSurface& surface, float a, float b) {
    const V3 c0 = spatial::toV3(surface.corners[0]);
    const V3 c1 = spatial::toV3(surface.corners[1]);
    const V3 c2 = spatial::toV3(surface.corners[2]);
    const V3 c3 = spatial::toV3(surface.corners[3]);
    const double da = static_cast<double>(a);
    const double db = static_cast<double>(b);
    const V3 top = c0 * (1.0 - da) + c1 * da;
    const V3 bottom = c3 * (1.0 - da) + c2 * da;
    return spatial::toVec3f(top * (1.0 - db) + bottom * db);
}

Vec3f surfaceNormal(const SpatialSurface& surface) {
    const V3 c0 = spatial::toV3(surface.corners[0]);
    const V3 c1 = spatial::toV3(surface.corners[1]);
    const V3 c2 = spatial::toV3(surface.corners[2]);
    const V3 c3 = spatial::toV3(surface.corners[3]);
    const V3 n = spatial::cross(c2 - c0, c1 - c3);
    return spatial::toVec3f(spatial::normalized(n));
}

float surfaceArea(const SpatialSurface& surface) {
    const V3 c0 = spatial::toV3(surface.corners[0]);
    const V3 c1 = spatial::toV3(surface.corners[1]);
    const V3 c2 = spatial::toV3(surface.corners[2]);
    const V3 c3 = spatial::toV3(surface.corners[3]);
    const double first = 0.5 * spatial::length(spatial::cross(c1 - c0, c2 - c0));
    const double second = 0.5 * spatial::length(spatial::cross(c2 - c0, c3 - c0));
    return static_cast<float>(first + second);
}

bool surfaceUsable(const SpatialSurface& surface) {
    return surface.enabled && surfaceArea(surface) > 1e-6f;
}

SpatialSurface wallSurface(const std::string& name, const Vec3f& center, float width, float height, float yawDegrees, float pitchDegrees) {
    SpatialSurface surface;
    surface.name = cleanName(name);
    const double hw = static_cast<double>(std::max(width, 0.01f)) * 0.5;
    const double hh = static_cast<double>(std::max(height, 0.01f)) * 0.5;
    const spatial::Mat3 r = spatial::rotationFromAngles(static_cast<double>(yawDegrees), static_cast<double>(pitchDegrees), 0.0);
    const V3 c = spatial::toV3(center);
    const V3 local[4] = {{-hw, hh, 0.0}, {hw, hh, 0.0}, {hw, -hh, 0.0}, {-hw, -hh, 0.0}};
    for (size_t i = 0; i < 4; ++i) {
        surface.corners[i] = spatial::toVec3f(c + spatial::mul(r, local[i]));
    }
    normalizeSurface(surface);
    return surface;
}

std::vector<SpatialSurface> boxSurfaces(const std::string& name, const Vec3f& center, float width, float height, float depth) {
    const double w = static_cast<double>(std::max(width, 0.01f)) * 0.5;
    const double h = static_cast<double>(std::max(height, 0.01f)) * 0.5;
    const double d = static_cast<double>(std::max(depth, 0.01f)) * 0.5;
    const V3 c = spatial::toV3(center);
    struct Face {
        const char* label;
        V3 offset;
        V3 right;
        V3 up;
        double halfWidth;
        double halfHeight;
    };
    const Face faces[5] = {
        {" front", {0.0, 0.0, d}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, w, h},
        {" back", {0.0, 0.0, -d}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, w, h},
        {" right", {w, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, d, h},
        {" left", {-w, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, d, h},
        {" top", {0.0, h, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, w, d},
    };
    std::vector<SpatialSurface> result;
    for (const Face& face : faces) {
        SpatialSurface surface;
        surface.name = cleanName(name + face.label);
        const V3 middle = c + face.offset;
        surface.corners[0] = spatial::toVec3f(middle - face.right * face.halfWidth + face.up * face.halfHeight);
        surface.corners[1] = spatial::toVec3f(middle + face.right * face.halfWidth + face.up * face.halfHeight);
        surface.corners[2] = spatial::toVec3f(middle + face.right * face.halfWidth - face.up * face.halfHeight);
        surface.corners[3] = spatial::toVec3f(middle - face.right * face.halfWidth - face.up * face.halfHeight);
        normalizeSurface(surface);
        result.push_back(surface);
    }
    return result;
}

bool addSpatialSurface(SpatialMapping& spatial, const SpatialSurface& surface) {
    if (spatial.surfaces.size() >= static_cast<size_t>(kMaxSpatialSurfaces)) {
        return false;
    }
    SpatialSurface copy = surface;
    normalizeSurface(copy);
    spatial.surfaces.push_back(copy);
    return true;
}

bool removeSpatialSurface(SpatialMapping& spatial, int index) {
    if (index < 0 || index >= static_cast<int>(spatial.surfaces.size())) {
        return false;
    }
    spatial.surfaces.erase(spatial.surfaces.begin() + index);
    return true;
}

bool addCalibrationPoint(SpatialMapping& spatial, const CalibrationPoint& point) {
    if (spatial.points.size() >= static_cast<size_t>(kMaxSpatialPoints)) {
        return false;
    }
    CalibrationPoint copy = point;
    copy.name = cleanName(copy.name);
    normalizeVec(copy.world);
    copy.outputX = clampValue(copy.outputX, -0.5f, 1.5f, 0.5f);
    copy.outputY = clampValue(copy.outputY, -0.5f, 1.5f, 0.5f);
    spatial.points.push_back(copy);
    return true;
}

bool removeCalibrationPoint(SpatialMapping& spatial, int index) {
    if (index < 0 || index >= static_cast<int>(spatial.points.size())) {
        return false;
    }
    spatial.points.erase(spatial.points.begin() + index);
    return true;
}

void normalizeDevice(SpatialDevice& device) {
    normalizeVec(device.pose.position);
    device.pose.yaw = wrapDegrees(device.pose.yaw);
    device.pose.pitch = clampValue(device.pose.pitch, -89.9f, 89.9f, 0.0f);
    device.pose.roll = wrapDegrees(device.pose.roll);
    SpatialLens& lens = device.lens;
    lens.fovY = clampValue(lens.fovY, 1.0f, 120.0f, 30.0f);
    lens.aspect = clampValue(lens.aspect, 0.25f, 4.0f, 16.0f / 9.0f);
    lens.shiftX = clampValue(lens.shiftX, -1.5f, 1.5f, 0.0f);
    lens.shiftY = clampValue(lens.shiftY, -1.5f, 1.5f, 0.0f);
    lens.nearPlane = clampValue(lens.nearPlane, 0.01f, 10.0f, 0.1f);
    lens.farPlane = clampValue(lens.farPlane, lens.nearPlane * 2.0f, 5000.0f, 200.0f);
}

void normalizeSurface(SpatialSurface& surface) {
    surface.name = cleanName(surface.name);
    for (Vec3f& corner : surface.corners) {
        normalizeVec(corner);
    }
    surface.uv[0] = clampValue(surface.uv[0], -8.0f, 8.0f, 0.0f);
    surface.uv[1] = clampValue(surface.uv[1], -8.0f, 8.0f, 0.0f);
    surface.uv[2] = clampValue(surface.uv[2], -8.0f, 8.0f, 1.0f);
    surface.uv[3] = clampValue(surface.uv[3], -8.0f, 8.0f, 1.0f);
    surface.divisions = std::clamp(surface.divisions, 1, kMaxSurfaceDivisions);
}

void normalizeSpatial(SpatialMapping& spatial) {
    if (spatial.source != SpatialSource::SurfaceUv && spatial.source != SpatialSource::ViewerCamera) {
        spatial.source = SpatialSource::SurfaceUv;
    }
    normalizeDevice(spatial.projector);
    normalizeDevice(spatial.camera);
    if (spatial.surfaces.size() > static_cast<size_t>(kMaxSpatialSurfaces)) {
        spatial.surfaces.resize(static_cast<size_t>(kMaxSpatialSurfaces));
    }
    for (SpatialSurface& surface : spatial.surfaces) {
        normalizeSurface(surface);
    }
    if (spatial.points.size() > static_cast<size_t>(kMaxSpatialPoints)) {
        spatial.points.resize(static_cast<size_t>(kMaxSpatialPoints));
    }
    for (CalibrationPoint& point : spatial.points) {
        point.name = cleanName(point.name);
        normalizeVec(point.world);
        point.outputX = clampValue(point.outputX, -0.5f, 1.5f, 0.5f);
        point.outputY = clampValue(point.outputY, -0.5f, 1.5f, 0.5f);
    }
}

bool isDefaultSpatial(const SpatialMapping& spatial) {
    SpatialMapping copy = spatial;
    normalizeSpatial(copy);
    return copy == SpatialMapping{};
}

SpatialMapping defaultSpatialSetup() {
    SpatialMapping spatial;
    spatial.enabled = true;
    spatial.surfaces.push_back(SpatialSurface{});
    return spatial;
}

int usableSurfaceCount(const SpatialMapping& spatial) {
    int count = 0;
    for (const SpatialSurface& surface : spatial.surfaces) {
        count += surfaceUsable(surface) ? 1 : 0;
    }
    return count;
}

nlohmann::json deviceToJson(const SpatialDevice& device) {
    nlohmann::json doc;
    doc["position"] = vecToJson(device.pose.position);
    doc["yaw"] = device.pose.yaw;
    doc["pitch"] = device.pose.pitch;
    doc["roll"] = device.pose.roll;
    doc["fovY"] = device.lens.fovY;
    doc["aspect"] = device.lens.aspect;
    doc["shiftX"] = device.lens.shiftX;
    doc["shiftY"] = device.lens.shiftY;
    doc["near"] = device.lens.nearPlane;
    doc["far"] = device.lens.farPlane;
    return doc;
}

bool deviceFromJson(const nlohmann::json& doc, SpatialDevice& device) {
    if (!doc.is_object()) {
        return false;
    }
    SpatialDevice result = device;
    readVec(doc, "position", result.pose.position);
    result.pose.yaw = readNumber(doc, "yaw", result.pose.yaw);
    result.pose.pitch = readNumber(doc, "pitch", result.pose.pitch);
    result.pose.roll = readNumber(doc, "roll", result.pose.roll);
    result.lens.fovY = readNumber(doc, "fovY", result.lens.fovY);
    result.lens.aspect = readNumber(doc, "aspect", result.lens.aspect);
    result.lens.shiftX = readNumber(doc, "shiftX", result.lens.shiftX);
    result.lens.shiftY = readNumber(doc, "shiftY", result.lens.shiftY);
    result.lens.nearPlane = readNumber(doc, "near", result.lens.nearPlane);
    result.lens.farPlane = readNumber(doc, "far", result.lens.farPlane);
    normalizeDevice(result);
    device = result;
    return true;
}

nlohmann::json deviceMatricesToJson(const SpatialDevice& device) {
    SpatialDevice copy = device;
    normalizeDevice(copy);
    nlohmann::json doc;
    doc["convention"] = kSpatialConvention;
    doc["layout"] = "column-major, as OpenGL reads it";
    doc["device"] = deviceToJson(copy);
    const SpatialMatrix view = viewMatrix(copy);
    const SpatialMatrix projection = projectionMatrix(copy.lens);
    const SpatialMatrix both = viewProjectionMatrix(copy);
    doc["view"] = std::vector<float>(view.begin(), view.end());
    doc["projection"] = std::vector<float>(projection.begin(), projection.end());
    doc["viewProjection"] = std::vector<float>(both.begin(), both.end());
    return doc;
}

nlohmann::json spatialToJson(const SpatialMapping& spatial) {
    nlohmann::json doc;
    doc["enabled"] = spatial.enabled;
    doc["source"] = spatial.source == SpatialSource::ViewerCamera ? "camera" : "surface";
    doc["projector"] = deviceToJson(spatial.projector);
    doc["camera"] = deviceToJson(spatial.camera);
    nlohmann::json surfaces = nlohmann::json::array();
    for (const SpatialSurface& surface : spatial.surfaces) {
        surfaces.push_back(surfaceToJson(surface));
    }
    doc["surfaces"] = surfaces;
    nlohmann::json points = nlohmann::json::array();
    for (const CalibrationPoint& point : spatial.points) {
        nlohmann::json entry;
        entry["name"] = point.name;
        entry["world"] = vecToJson(point.world);
        entry["output"] = nlohmann::json::array({point.outputX, point.outputY});
        entry["enabled"] = point.enabled;
        points.push_back(entry);
    }
    doc["points"] = points;
    return doc;
}

bool spatialFromJson(const nlohmann::json& doc, SpatialMapping& spatial) {
    if (!doc.is_object()) {
        return false;
    }
    SpatialMapping result;
    result.enabled = readFlag(doc, "enabled", false);
    const std::string source = readText(doc, "source", "surface");
    result.source = source == "camera" ? SpatialSource::ViewerCamera : SpatialSource::SurfaceUv;
    auto projector = doc.find("projector");
    if (projector != doc.end()) {
        deviceFromJson(*projector, result.projector);
    }
    auto camera = doc.find("camera");
    if (camera != doc.end()) {
        deviceFromJson(*camera, result.camera);
    }
    auto surfaces = doc.find("surfaces");
    if (surfaces != doc.end() && surfaces->is_array()) {
        for (const nlohmann::json& entry : *surfaces) {
            if (result.surfaces.size() >= static_cast<size_t>(kMaxSpatialSurfaces)) {
                break;
            }
            SpatialSurface surface;
            if (surfaceFromJson(entry, surface)) {
                result.surfaces.push_back(surface);
            }
        }
    }
    auto points = doc.find("points");
    if (points != doc.end() && points->is_array()) {
        for (const nlohmann::json& entry : *points) {
            if (result.points.size() >= static_cast<size_t>(kMaxSpatialPoints) || !entry.is_object()) {
                continue;
            }
            CalibrationPoint point;
            point.name = cleanName(readText(entry, "name", ""));
            if (!readVec(entry, "world", point.world)) {
                continue;
            }
            auto output = entry.find("output");
            if (output != entry.end() && output->is_array() && output->size() == 2 && (*output)[0].is_number() && (*output)[1].is_number()) {
                point.outputX = (*output)[0].get<float>();
                point.outputY = (*output)[1].get<float>();
            }
            point.enabled = readFlag(entry, "enabled", true);
            result.points.push_back(point);
        }
    }
    normalizeSpatial(result);
    spatial = result;
    return true;
}

}

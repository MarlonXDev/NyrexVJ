#include "check.hpp"

#include "core/output_map.hpp"
#include "core/output_map_json.hpp"
#include "core/spatial_calibration.hpp"
#include "core/spatial_map.hpp"
#include "core/spatial_mesh.hpp"
#include "core/venue_setup.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

using namespace nvj;

bool near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

bool nearVec(const Vec3f& a, const Vec3f& b, double eps) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

SpatialDevice plainDevice(float fovY, float aspect) {
    SpatialDevice device;
    device.pose.position = Vec3f{0.0f, 0.0f, 0.0f};
    device.lens.fovY = fovY;
    device.lens.aspect = aspect;
    device.lens.nearPlane = 1.0f;
    device.lens.farPlane = 3.0f;
    return device;
}

OutputMapping wallMapping() {
    OutputMapping map;
    map.spatial = defaultSpatialSetup();
    return map;
}

void testConventions() {
    SpatialPose identity;
    identity.position = Vec3f{0.0f, 0.0f, 0.0f};
    CHECK(nearVec(forwardOf(identity), Vec3f{0.0f, 0.0f, -1.0f}, 1e-6));
    CHECK(nearVec(rightOf(identity), Vec3f{1.0f, 0.0f, 0.0f}, 1e-6));
    CHECK(nearVec(upOf(identity), Vec3f{0.0f, 1.0f, 0.0f}, 1e-6));

    SpatialPose yawed = identity;
    yawed.yaw = 90.0f;
    CHECK(nearVec(forwardOf(yawed), Vec3f{-1.0f, 0.0f, 0.0f}, 1e-6));
    CHECK(nearVec(rightOf(yawed), Vec3f{0.0f, 0.0f, -1.0f}, 1e-6));

    SpatialPose pitched = identity;
    pitched.pitch = 30.0f;
    CHECK(forwardOf(pitched).y > 0.49f && forwardOf(pitched).y < 0.51f);
    CHECK(near(forwardOf(pitched).z, -std::cos(30.0 * 3.14159265358979 / 180.0), 1e-6));

    SpatialPose rolled = identity;
    rolled.roll = 90.0f;
    CHECK(nearVec(rightOf(rolled), Vec3f{0.0f, 1.0f, 0.0f}, 1e-6));
    CHECK(nearVec(forwardOf(rolled), Vec3f{0.0f, 0.0f, -1.0f}, 1e-6));

    const Vec3f targets[5] = {{0.0f, 0.0f, -5.0f}, {3.0f, 1.0f, -2.0f}, {-4.0f, 2.0f, 7.0f}, {0.5f, -3.0f, 1.0f}, {5.0f, 5.0f, 5.0f}};
    for (const Vec3f& target : targets) {
        const Vec3f position{1.0f, 2.0f, 3.0f};
        const SpatialPose pose = poseLookAt(position, target, 0.0f);
        const double dx = target.x - position.x;
        const double dy = target.y - position.y;
        const double dz = target.z - position.z;
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        const Vec3f f = forwardOf(pose);
        CHECK(near(f.x, dx / len, 1e-5) && near(f.y, dy / len, 1e-5) && near(f.z, dz / len, 1e-5));
    }

    CHECK(near(fovYFromThrowRatio(1.5f, 16.0f / 9.0f), 21.2, 0.05));
    for (float ratio : {0.5f, 0.8f, 1.2f, 1.5f, 2.5f}) {
        CHECK(near(throwRatioFromFovY(fovYFromThrowRatio(ratio, 16.0f / 9.0f), 16.0f / 9.0f), ratio, 1e-4));
    }
    CHECK(near(fovYFromThrowRatio(-1.0f, 1.0f), 30.0, 1e-6));
}

void testProjection() {
    const SpatialDevice square = plainDevice(90.0f, 1.0f);
    ProjectedPoint centre = projectPoint(square, Vec3f{0.0f, 0.0f, -2.0f});
    CHECK(centre.inFront && centre.inside);
    CHECK(near(centre.x, 0.5, 1e-6) && near(centre.y, 0.5, 1e-6));
    const ProjectedPoint top = projectPoint(square, Vec3f{0.0f, 2.0f, -2.0f});
    CHECK(near(top.x, 0.5, 1e-6) && near(top.y, 0.0, 1e-6));
    const ProjectedPoint right = projectPoint(square, Vec3f{2.0f, 0.0f, -2.0f});
    CHECK(near(right.x, 1.0, 1e-6) && near(right.y, 0.5, 1e-6));
    const ProjectedPoint behind = projectPoint(square, Vec3f{0.0f, 0.0f, 5.0f});
    CHECK(!behind.inFront && !behind.inside);
    const ProjectedPoint outside = projectPoint(square, Vec3f{20.0f, 0.0f, -2.0f});
    const ProjectedPoint beyondFar = projectPoint(square, Vec3f{0.0f, 0.0f, -5.0f});
    CHECK(beyondFar.inFront && !beyondFar.inside && beyondFar.depth > 1.0f);
    CHECK(outside.inFront && !outside.inside && outside.x > 1.0f);
    const ProjectedPoint nearPlanePoint = projectPoint(square, Vec3f{0.0f, 0.0f, -1.0f});
    CHECK(near(nearPlanePoint.depth, -1.0, 1e-6));
    const ProjectedPoint farPlanePoint = projectPoint(square, Vec3f{0.0f, 0.0f, -3.0f});
    CHECK(near(farPlanePoint.depth, 1.0, 1e-6));

    SpatialDevice shifted = square;
    shifted.lens.shiftY = 0.5f;
    const ProjectedPoint axisUp = projectPoint(shifted, Vec3f{0.0f, 0.0f, -5.0f});
    CHECK(near(axisUp.x, 0.5, 1e-6) && near(axisUp.y, 1.0, 1e-6));
    shifted.lens.shiftY = 0.0f;
    shifted.lens.shiftX = 0.25f;
    const ProjectedPoint axisSide = projectPoint(shifted, Vec3f{0.0f, 0.0f, -5.0f});
    CHECK(near(axisSide.x, 0.25, 1e-6) && near(axisSide.y, 0.5, 1e-6));

    const SpatialMatrix projection = projectionMatrix(square.lens);
    CHECK(near(projection[0], 1.0, 1e-6) && near(projection[5], 1.0, 1e-6));
    CHECK(near(projection[10], -2.0, 1e-6) && near(projection[14], -3.0, 1e-6) && near(projection[11], -1.0, 1e-6));

    SpatialDevice placed;
    placed.pose.position = Vec3f{1.0f, 2.0f, 3.0f};
    const SpatialMatrix view = viewMatrix(placed);
    CHECK(near(view[12], -1.0, 1e-6) && near(view[13], -2.0, 1e-6) && near(view[14], -3.0, 1e-6));
    CHECK(near(view[0], 1.0, 1e-6) && near(view[5], 1.0, 1e-6) && near(view[10], 1.0, 1e-6) && near(view[15], 1.0, 1e-6));

    SpatialDevice general;
    general.pose = SpatialPose{Vec3f{0.7f, 1.9f, 5.3f}, 12.0f, -6.0f, 2.0f};
    general.lens = SpatialLens{27.0f, 16.0f / 9.0f, 0.1f, 0.3f, 0.2f, 100.0f};
    const SpatialMatrix both = viewProjectionMatrix(general);
    const Vec3f samples[4] = {{0.2f, 1.0f, 0.0f}, {-1.5f, 2.5f, -0.5f}, {1.0f, 0.5f, 1.0f}, {0.0f, 1.5f, -3.0f}};
    for (const Vec3f& world : samples) {
        const double x = both[0] * world.x + both[4] * world.y + both[8] * world.z + both[12];
        const double y = both[1] * world.x + both[5] * world.y + both[9] * world.z + both[13];
        const double w = both[3] * world.x + both[7] * world.y + both[11] * world.z + both[15];
        const ProjectedPoint p = projectPoint(general, world);
        CHECK(w > 0.0);
        CHECK(near(0.5 + 0.5 * x / w, p.x, 1e-4) && near(0.5 - 0.5 * y / w, p.y, 1e-4));
    }

    for (int i = 0; i < 25; ++i) {
        const float x = 0.1f + 0.2f * static_cast<float>(i % 5);
        const float y = 0.1f + 0.2f * static_cast<float>(i / 5);
        Vec3f origin;
        Vec3f direction;
        CHECK(deviceRay(general, x, y, origin, direction));
        const Vec3f world{origin.x + direction.x * 7.0f, origin.y + direction.y * 7.0f, origin.z + direction.z * 7.0f};
        const ProjectedPoint p = projectPoint(general, world);
        CHECK(near(p.x, x, 1e-4) && near(p.y, y, 1e-4));
    }

    const nlohmann::json matrices = deviceMatricesToJson(general);
    CHECK(matrices["view"].size() == 16 && matrices["projection"].size() == 16 && matrices["viewProjection"].size() == 16);
    CHECK(matrices["convention"].get<std::string>() == std::string(kSpatialConvention));
    CHECK(matrices.dump() == deviceMatricesToJson(general).dump());
}

void testSurfaces() {
    const SpatialSurface wall;
    CHECK(nearVec(surfaceNormal(wall), Vec3f{0.0f, 0.0f, 1.0f}, 1e-6));
    CHECK(near(surfaceArea(wall), 5.0 * 2.8125, 1e-4));
    CHECK(nearVec(surfacePoint(wall, 0.0f, 0.0f), Vec3f{-2.5f, 3.0f, 0.0f}, 1e-6));
    CHECK(nearVec(surfacePoint(wall, 1.0f, 1.0f), Vec3f{2.5f, 0.1875f, 0.0f}, 1e-6));
    CHECK(nearVec(surfacePoint(wall, 0.5f, 0.5f), Vec3f{0.0f, 1.59375f, 0.0f}, 1e-6));
    CHECK(surfaceUsable(wall));

    const SpatialSurface turned = wallSurface("Side", Vec3f{4.0f, 1.0f, 0.0f}, 3.0f, 2.0f, 90.0f, 0.0f);
    CHECK(nearVec(surfaceNormal(turned), Vec3f{1.0f, 0.0f, 0.0f}, 1e-5));
    CHECK(near(surfaceArea(turned), 6.0, 1e-4));
    const SpatialSurface floorPlane = wallSurface("Floor", Vec3f{0.0f, 0.0f, 0.0f}, 4.0f, 4.0f, 0.0f, -90.0f);
    CHECK(nearVec(surfaceNormal(floorPlane), Vec3f{0.0f, 1.0f, 0.0f}, 1e-5));

    const std::vector<SpatialSurface> box = boxSurfaces("Box", Vec3f{0.0f, 1.0f, 0.0f}, 2.0f, 1.0f, 0.5f);
    CHECK(box.size() == 5);
    for (const SpatialSurface& face : box) {
        const Vec3f middle = surfacePoint(face, 0.5f, 0.5f);
        const Vec3f normal = surfaceNormal(face);
        const double outward = normal.x * (middle.x - 0.0) + normal.y * (middle.y - 1.0) + normal.z * (middle.z - 0.0);
        CHECK(outward > 0.2);
        CHECK(surfaceUsable(face));
    }
    CHECK(near(surfaceArea(box[0]), 2.0, 1e-4) && near(surfaceArea(box[2]), 0.5, 1e-4) && near(surfaceArea(box[4]), 1.0, 1e-4));

    SpatialSurface flat = wall;
    flat.corners[2] = flat.corners[0];
    flat.corners[3] = flat.corners[0];
    flat.corners[1] = flat.corners[0];
    CHECK(!surfaceUsable(flat));
    SpatialMapping spatial = defaultSpatialSetup();
    spatial.surfaces.push_back(flat);
    CHECK(usableSurfaceCount(spatial) == 1);

    SpatialSurface broken;
    broken.corners[0] = Vec3f{NAN, INFINITY, 1e9f};
    broken.divisions = 9999;
    broken.uv = {-100.0f, NAN, 100.0f, 2.0f};
    broken.name = std::string(200, 'x') + "\n";
    normalizeSurface(broken);
    CHECK(broken.corners[0].x == 0.0f && broken.corners[0].y == 0.0f && broken.corners[0].z == kMaxSpatialCoordinate);
    CHECK(broken.divisions == kMaxSurfaceDivisions);
    CHECK(broken.uv[0] == -8.0f && broken.uv[1] == 0.0f && broken.uv[2] == 8.0f && broken.uv[3] == 2.0f);
    CHECK(broken.name.size() == kMaxSpatialNameLength);

    SpatialMapping full;
    for (int i = 0; i < kMaxSpatialSurfaces; ++i) {
        CHECK(addSpatialSurface(full, SpatialSurface{}));
    }
    CHECK(!addSpatialSurface(full, SpatialSurface{}));
    CHECK(removeSpatialSurface(full, 3) && full.surfaces.size() == static_cast<size_t>(kMaxSpatialSurfaces - 1));
    CHECK(!removeSpatialSurface(full, 99) && !removeSpatialSurface(full, -1));
    for (int i = 0; i < kMaxSpatialPoints; ++i) {
        CHECK(addCalibrationPoint(full, CalibrationPoint{"P", Vec3f{}, 0.5f, 0.5f, true}));
    }
    CHECK(!addCalibrationPoint(full, CalibrationPoint{}));
    CHECK(removeCalibrationPoint(full, 0) && !removeCalibrationPoint(full, 100));

    SpatialDevice weird;
    weird.pose.yaw = 725.0f;
    weird.pose.pitch = 200.0f;
    weird.pose.roll = -540.0f;
    weird.lens.fovY = 500.0f;
    weird.lens.aspect = 0.0f;
    weird.lens.nearPlane = -1.0f;
    weird.lens.farPlane = 0.0f;
    weird.lens.shiftY = 9.0f;
    normalizeDevice(weird);
    CHECK(near(weird.pose.yaw, 5.0, 1e-4) && near(weird.pose.pitch, 89.9, 1e-4) && near(std::fabs(weird.pose.roll), 180.0, 1e-4));
    CHECK(weird.lens.fovY == 120.0f && weird.lens.aspect == 0.25f && weird.lens.nearPlane == 0.01f && weird.lens.farPlane >= 0.02f && weird.lens.shiftY == 1.5f);
}

void testJson() {
    SpatialMapping spatial = defaultSpatialSetup();
    spatial.source = SpatialSource::ViewerCamera;
    spatial.projector.pose = SpatialPose{Vec3f{1.5f, 2.0f, 7.0f}, -8.0f, 4.0f, 1.0f};
    spatial.projector.lens.shiftY = 0.25f;
    spatial.camera.pose.position = Vec3f{0.0f, 1.7f, 9.0f};
    spatial.surfaces[0].doubleSided = true;
    spatial.surfaces[0].uv = {0.0f, 0.0f, 0.5f, 1.0f};
    spatial.surfaces[0].divisions = 12;
    addSpatialSurface(spatial, wallSurface("Pillar", Vec3f{1.0f, 1.0f, 2.0f}, 0.5f, 2.0f, 0.0f, 0.0f));
    addCalibrationPoint(spatial, CalibrationPoint{"Corner", Vec3f{-2.5f, 3.0f, 0.0f}, 0.1f, 0.2f, true});
    addCalibrationPoint(spatial, CalibrationPoint{"Off", Vec3f{2.5f, 3.0f, 0.0f}, 0.9f, 0.2f, false});
    const nlohmann::json doc = spatialToJson(spatial);
    SpatialMapping back;
    CHECK(spatialFromJson(doc, back));
    CHECK(back == spatial);
    CHECK(spatialToJson(back).dump() == doc.dump());

    OutputMapping plain;
    CHECK(!mappingToJson(plain).contains("spatial"));
    CHECK(isDefaultMapping(plain));
    OutputMapping withSpatial;
    withSpatial.spatial = spatial;
    const nlohmann::json mappingDoc = mappingToJson(withSpatial);
    CHECK(mappingDoc.contains("spatial"));
    OutputMapping mappingBack;
    CHECK(mappingFromJson(mappingDoc, mappingBack));
    CHECK(mappingBack == withSpatial);
    CHECK(!isDefaultMapping(withSpatial));

    OutputMapping disabledButFilled = withSpatial;
    disabledButFilled.spatial.enabled = false;
    CHECK(mappingToJson(disabledButFilled).contains("spatial"));

    OutputSet set = defaultOutputs();
    const std::string plainVenue = venueToJson("Plain", set).dump();
    CHECK(plainVenue.find("spatial") == std::string::npos);
    set[1].enabled = true;
    set[1].slices.push_back(OutputSlice{"Second", true, withSpatial});
    const nlohmann::json venueDoc = venueToJson("Wall", set);
    CHECK(venueDoc.dump().find("spatial") != std::string::npos);
    std::string name;
    OutputSet loaded;
    std::string error;
    CHECK(venueFromJson(venueDoc, name, loaded, error));
    CHECK(loaded[1].slices.size() == 1 && loaded[1].slices[0].map.spatial == spatial);
    CHECK(loaded[0].map.spatial == SpatialMapping{});

    SpatialMapping damaged;
    CHECK(!spatialFromJson(nlohmann::json::array(), damaged));
    nlohmann::json bad = {{"enabled", "yes"}, {"source", 7}, {"projector", "x"}, {"surfaces", nlohmann::json::array({1, "a", {{"corners", {1, 2}}}, {{"corners", {{1, 2, 3}, {1, 2}, {1, 2, 3}, {1, 2, 3}}}}})}, {"points", nlohmann::json::array({{{"world", {1, 2}}}, {{"world", {1, 2, 3}}, {"output", {"a", 1}}}})}};
    CHECK(spatialFromJson(bad, damaged));
    CHECK(!damaged.enabled && damaged.source == SpatialSource::SurfaceUv && damaged.surfaces.empty());
    CHECK(damaged.points.size() == 1 && damaged.points[0].outputX == 0.5f);

    nlohmann::json huge = {{"enabled", true}, {"projector", {{"position", {1e30, -1e30, 5}}, {"fovY", 1e9}, {"yaw", 1e9}}}, {"surfaces", nlohmann::json::array()}};
    for (int i = 0; i < 40; ++i) {
        huge["surfaces"].push_back({{"name", "S"}, {"corners", {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, {"divisions", 5000}});
    }
    SpatialMapping clamped;
    CHECK(spatialFromJson(huge, clamped));
    CHECK(clamped.surfaces.size() == static_cast<size_t>(kMaxSpatialSurfaces));
    CHECK(clamped.surfaces[0].divisions == kMaxSurfaceDivisions);
    CHECK(clamped.projector.pose.position.x == kMaxSpatialCoordinate && clamped.projector.lens.fovY == 120.0f);
}

void testMeshBasics() {
    OutputMapping map = wallMapping();
    const SpatialMesh mesh = buildSpatialMesh(map);
    CHECK(mesh.stats.surfaces == 1 && mesh.stats.trianglesIn == 32 && mesh.stats.trianglesOut == 32);
    CHECK(mesh.stats.culled == 0 && mesh.stats.behind == 0 && mesh.stats.clipped == 0 && mesh.stats.degenerate == 0);
    CHECK(mesh.vertices.size() == 25 && mesh.indices.size() == 96);
    CHECK(mesh.bytes() == 25 * sizeof(SpatialVertex) + 96 * sizeof(uint32_t));
    const ProjectedPoint topLeft = projectPoint(map.spatial.projector, map.spatial.surfaces[0].corners[0]);
    CHECK(near(mesh.vertices[0].x, topLeft.x, 1e-5) && near(mesh.vertices[0].y, topLeft.y, 1e-5));
    CHECK(near(mesh.vertices[0].s, 0.0, 1e-6) && near(mesh.vertices[0].t, 1.0, 1e-6) && mesh.vertices[0].q == 1.0f);
    CHECK(near(mesh.vertices[24].s, 1.0, 1e-6) && near(mesh.vertices[24].t, 0.0, 1e-6));
    for (const SpatialVertex& vertex : mesh.vertices) {
        CHECK(vertex.w >= map.spatial.projector.lens.nearPlane * 0.999f);
        CHECK(vertex.depth > -1.0f && vertex.depth < 1.0f);
        CHECK(vertex.x > 0.0f && vertex.x < 1.0f && vertex.y > 0.0f && vertex.y < 1.0f);
    }
    for (uint32_t index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }

    OutputMapping disabled = map;
    disabled.spatial.enabled = false;
    CHECK(buildSpatialMesh(disabled).vertices.empty());
    OutputMapping empty = map;
    empty.spatial.surfaces.clear();
    CHECK(buildSpatialMesh(empty).indices.empty());
    OutputMapping hidden = map;
    hidden.spatial.surfaces[0].enabled = false;
    CHECK(buildSpatialMesh(hidden).stats.surfaces == 0);

    OutputMapping area = map;
    area.slice = {0.25f, 0.5f, 0.5f, 0.5f};
    const SpatialMesh areaMesh = buildSpatialMesh(area);
    CHECK(near(areaMesh.vertices[0].s, 0.25, 1e-6) && near(areaMesh.vertices[0].t, 1.0 - 0.5, 1e-6));
    CHECK(near(areaMesh.vertices[24].s, 0.75, 1e-6) && near(areaMesh.vertices[24].t, 1.0 - 1.0, 1e-6));
    OutputMapping flipped = map;
    flipped.flipX = true;
    flipped.flipY = true;
    const SpatialMesh flippedMesh = buildSpatialMesh(flipped);
    CHECK(near(flippedMesh.vertices[0].s, 1.0, 1e-6) && near(flippedMesh.vertices[0].t, 0.0, 1e-6));
    CHECK(near(flippedMesh.vertices[24].s, 0.0, 1e-6) && near(flippedMesh.vertices[24].t, 1.0, 1e-6));

    OutputMapping partial = map;
    partial.spatial.surfaces[0].uv = {0.0f, 0.0f, 0.5f, 1.0f};
    const SpatialMesh partialMesh = buildSpatialMesh(partial);
    float largestS = 0.0f;
    for (const SpatialVertex& vertex : partialMesh.vertices) {
        largestS = std::max(largestS, vertex.s);
    }
    CHECK(near(largestS, 0.5, 1e-6));
}

void testCulling() {
    OutputMapping map = wallMapping();
    map.spatial.surfaces[0] = wallSurface("Back", Vec3f{0.0f, 1.6f, 0.0f}, 5.0f, 2.8125f, 180.0f, 0.0f);
    const SpatialMesh away = buildSpatialMesh(map);
    CHECK(away.stats.culled == 32 && away.stats.trianglesOut == 0 && away.indices.empty());
    map.spatial.surfaces[0].doubleSided = true;
    const SpatialMesh both = buildSpatialMesh(map);
    CHECK(both.stats.culled == 0 && both.stats.trianglesOut == 32);
    OutputMapping ray = wallMapping();
    ray.spatial.surfaces[0] = wallSurface("Back", Vec3f{0.0f, 1.6f, 0.0f}, 5.0f, 2.8125f, 180.0f, 0.0f);
    CHECK(!castProjectorRay(ray, 0.5f, 0.5f).hit);
    ray.spatial.surfaces[0].doubleSided = true;
    CHECK(castProjectorRay(ray, 0.5f, 0.5f).hit);

    OutputMapping off = wallMapping();
    off.spatial.surfaces[0] = wallSurface("Far left", Vec3f{-60.0f, 1.6f, 0.0f}, 5.0f, 2.8f, 0.0f, 0.0f);
    const SpatialMesh offMesh = buildSpatialMesh(off);
    CHECK(offMesh.stats.trianglesIn == 32 && offMesh.stats.trianglesOut == 0 && offMesh.stats.culled == 0);
}

void testNearClipping() {
    OutputMapping map;
    map.spatial = defaultSpatialSetup();
    map.spatial.surfaces.clear();
    SpatialSurface floorPlane = wallSurface("Floor", Vec3f{0.0f, 0.0f, 0.0f}, 12.0f, 24.0f, 0.0f, -90.0f);
    floorPlane.divisions = 8;
    map.spatial.surfaces.push_back(floorPlane);
    map.spatial.projector.pose = SpatialPose{Vec3f{0.0f, 2.0f, 2.0f}, 0.0f, -30.0f, 0.0f};
    map.spatial.projector.lens.fovY = 50.0f;
    const SpatialMesh mesh = buildSpatialMesh(map);
    CHECK(mesh.stats.clipped > 0);
    CHECK(mesh.stats.behind > 0);
    CHECK(mesh.stats.trianglesOut > 0);
    for (const SpatialVertex& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.s) && std::isfinite(vertex.t));
        CHECK(vertex.w >= map.spatial.projector.lens.nearPlane * 0.999f);
        CHECK(vertex.depth >= -1.0001f);
    }
    for (uint32_t index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }
    const SpatialHit under = castProjectorRay(map, 0.5f, 0.99f);
    CHECK(under.hit && under.world.y < 0.001f && under.world.y > -0.001f);
}

void testRayCast() {
    OutputMapping map = wallMapping();
    map.spatial.surfaces[0] = wallSurface("Oblique", Vec3f{0.5f, 1.4f, 0.0f}, 4.0f, 2.5f, 25.0f, -8.0f);
    map.spatial.surfaces[0].divisions = 6;
    map.spatial.projector.pose = poseLookAt(Vec3f{1.5f, 1.8f, 6.0f}, Vec3f{0.5f, 1.4f, 0.0f}, 0.0f);
    map.slice = {0.0f, 0.0f, 0.5f, 1.0f};
    const SpatialSurface& surface = map.spatial.surfaces[0];
    int checked = 0;
    for (int j = 0; j <= 4; ++j) {
        for (int i = 0; i <= 4; ++i) {
            const float a = 0.05f + 0.225f * static_cast<float>(i);
            const float b = 0.05f + 0.225f * static_cast<float>(j);
            const Vec3f world = surfacePoint(surface, a, b);
            const ProjectedPoint p = projectPoint(map.spatial.projector, world);
            const SpatialHit hit = castProjectorRay(map, p.x, p.y);
            CHECK(hit.hit && hit.surface == 0 && hit.contentValid);
            CHECK(nearVec(hit.world, world, 2e-3));
            const double expectU = 0.0 + 0.5 * a;
            const double expectV = 1.0 - (0.0 + 1.0 * b);
            CHECK(near(hit.u, expectU, 2e-3) && near(hit.v, expectV, 2e-3));
            ++checked;
        }
    }
    CHECK(checked == 25);
    CHECK(!castProjectorRay(map, 0.001f, 0.001f).hit || castProjectorRay(map, 0.001f, 0.001f).surface == 0);
    OutputMapping off = map;
    off.spatial.enabled = false;
    CHECK(!castProjectorRay(off, 0.5f, 0.5f).hit);

    OutputMapping mirrored = map;
    mirrored.flipX = true;
    const Vec3f probe = surfacePoint(surface, 0.2f, 0.4f);
    const ProjectedPoint pp = projectPoint(mirrored.spatial.projector, probe);
    const SpatialHit mirror = castProjectorRay(mirrored, pp.x, pp.y);
    CHECK(near(mirror.u, 0.0 + 0.5 * (1.0 - 0.2), 2e-3));
}

void testMeshMatchesRayCast() {
    OutputMapping map = wallMapping();
    map.spatial.surfaces[0] = wallSurface("Oblique", Vec3f{0.0f, 1.4f, 0.0f}, 4.0f, 2.5f, -30.0f, 5.0f);
    map.spatial.surfaces[0].divisions = 3;
    map.spatial.projector.pose = poseLookAt(Vec3f{-1.0f, 1.6f, 5.0f}, Vec3f{0.0f, 1.4f, 0.0f}, 0.0f);
    const SpatialMesh mesh = buildSpatialMesh(map);
    CHECK(mesh.stats.trianglesOut == 18);
    for (const SpatialVertex& vertex : mesh.vertices) {
        const SpatialHit hit = castProjectorRay(map, vertex.x, vertex.y);
        CHECK(hit.hit);
        CHECK(near(hit.u, vertex.s / vertex.q, 3e-3) && near(hit.v, vertex.t / vertex.q, 3e-3));
    }
}

void testCameraSource() {
    OutputMapping map = wallMapping();
    map.spatial.source = SpatialSource::ViewerCamera;
    map.spatial.camera.pose = poseLookAt(Vec3f{0.0f, 1.6f, 8.0f}, Vec3f{0.0f, 1.6f, 0.0f}, 0.0f);
    map.spatial.camera.lens.fovY = 40.0f;
    map.slice = {0.1f, 0.2f, 0.6f, 0.7f};
    const SpatialMesh mesh = buildSpatialMesh(map);
    CHECK(mesh.stats.trianglesOut == 32);
    for (const SpatialVertex& vertex : mesh.vertices) {
        CHECK(vertex.q > 0.0f);
        const SpatialHit hit = castProjectorRay(map, vertex.x, vertex.y);
        CHECK(hit.hit);
        CHECK(near(hit.u, vertex.s / vertex.q, 3e-3) && near(hit.v, vertex.t / vertex.q, 3e-3));
    }
    const Vec3f centre = surfacePoint(map.spatial.surfaces[0], 0.5f, 0.5f);
    const ProjectedPoint viewer = projectPoint(map.spatial.camera, centre);
    const ProjectedPoint beam = projectPoint(map.spatial.projector, centre);
    const SpatialHit hit = castProjectorRay(map, beam.x, beam.y);
    CHECK(hit.hit && hit.contentValid);
    const TextureCoord expected = contentCoords(map, viewer.x, viewer.y);
    CHECK(near(hit.u, expected.u, 2e-3) && near(hit.v, expected.v, 2e-3));

    OutputMapping narrow = map;
    narrow.spatial.camera.lens.fovY = 8.0f;
    const Vec3f edge = surfacePoint(narrow.spatial.surfaces[0], 0.02f, 0.5f);
    const ProjectedPoint edgeBeam = projectPoint(narrow.spatial.projector, edge);
    const SpatialHit edgeHit = castProjectorRay(narrow, edgeBeam.x, edgeBeam.y);
    CHECK(edgeHit.hit && !edgeHit.contentValid);

    OutputMapping behindViewer = map;
    behindViewer.spatial.camera.pose = poseLookAt(Vec3f{0.0f, 1.6f, -8.0f}, Vec3f{0.0f, 1.6f, -20.0f}, 0.0f);
    const SpatialMesh behindMesh = buildSpatialMesh(behindViewer);
    bool anyNegative = false;
    for (const SpatialVertex& vertex : behindMesh.vertices) {
        anyNegative = anyNegative || vertex.q <= 0.0f;
    }
    CHECK(anyNegative);
    const SpatialHit behindHit = castProjectorRay(behindViewer, 0.5f, 0.5f);
    CHECK(behindHit.hit && !behindHit.contentValid);
}

void testOcclusion() {
    OutputMapping map = wallMapping();
    map.spatial.surfaces.push_back(wallSurface("Pillar", Vec3f{0.0f, 1.5f, 2.0f}, 0.8f, 3.0f, 0.0f, 0.0f));
    const Vec3f behind = Vec3f{0.0f, 1.5f, 0.0f};
    const ProjectedPoint p = projectPoint(map.spatial.projector, behind);
    const SpatialHit hit = castProjectorRay(map, p.x, p.y);
    CHECK(hit.hit && hit.surface == 1);
    CHECK(near(hit.world.z, 2.0, 1e-3));
    CHECK(!pointLitByProjector(map, behind));
    CHECK(pointLitByProjector(map, Vec3f{-2.0f, 1.5f, 0.0f}));
    CHECK(pointLitByProjector(map, Vec3f{2.0f, 2.5f, 0.0f}));
    CHECK(pointLitByProjector(map, Vec3f{0.0f, 1.5f, 2.0f}));
    CHECK(!pointLitByProjector(map, Vec3f{0.0f, 1.5f, -3.0f}));
    CHECK(!pointLitByProjector(map, Vec3f{0.0f, 1.5f, 20.0f}));

    OutputMapping turned = map;
    turned.spatial.surfaces[1] = wallSurface("Pillar", Vec3f{0.0f, 1.5f, 2.0f}, 0.8f, 3.0f, 180.0f, 0.0f);
    CHECK(pointLitByProjector(turned, behind));
    turned.spatial.surfaces[1].doubleSided = true;
    CHECK(!pointLitByProjector(turned, behind));

    const SpatialMesh mesh = buildSpatialMesh(map);
    CHECK(mesh.stats.surfaces == 2);
    float nearest = 1.0f;
    float farthest = -1.0f;
    for (const SpatialVertex& vertex : mesh.vertices) {
        nearest = std::min(nearest, vertex.depth);
        farthest = std::max(farthest, vertex.depth);
    }
    CHECK(farthest > nearest);
}

OutputMapping sideProjector(float x, float blendLeft, float blendRight) {
    OutputMapping map;
    map.spatial = defaultSpatialSetup();
    map.spatial.surfaces[0].corners = {{{-5.0f, 3.0f, 0.0f}, {5.0f, 3.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, {-5.0f, 0.0f, 0.0f}}};
    map.spatial.projector.pose = SpatialPose{Vec3f{x, 1.5f, 6.0f}, 0.0f, 0.0f, 0.0f};
    map.spatial.projector.lens.fovY = 30.0f;
    map.blend.left = blendLeft;
    map.blend.right = blendRight;
    return map;
}

void testSeveralProjectors() {
    const double halfHeight = 6.0 * std::tan(15.0 * 3.14159265358979 / 180.0);
    const double width = 2.0 * halfHeight * (16.0 / 9.0);
    const double offset = 2.0;
    const float blend = static_cast<float>((width - offset) / width);
    OutputMapping a = sideProjector(-1.0f, 0.0f, blend);
    OutputMapping b = sideProjector(1.0f, blend, 0.0f);
    int both = 0;
    int onlyA = 0;
    int onlyB = 0;
    int none = 0;
    for (int i = -30; i <= 30; ++i) {
        const Vec3f world{static_cast<float>(i) * 0.2f, 1.5f, 0.0f};
        const float wa = projectorBlendWeight(a, world);
        const float wb = projectorBlendWeight(b, world);
        const bool litA = pointLitByProjector(a, world);
        const bool litB = pointLitByProjector(b, world);
        if (litA && litB) {
            ++both;
            const double sum = std::pow(wa, a.blend.gamma) + std::pow(wb, b.blend.gamma);
            CHECK(near(sum, 1.0, 2e-3));
        } else if (litA) {
            ++onlyA;
            CHECK(wa > 0.0f);
        } else if (litB) {
            ++onlyB;
            CHECK(wb > 0.0f);
        } else {
            ++none;
        }
    }
    CHECK(both >= 5 && onlyA >= 5 && onlyB >= 5 && none >= 5);

    a.spatial.surfaces.push_back(wallSurface("Pillar", Vec3f{-0.5f, 1.5f, 3.0f}, 0.6f, 3.0f, 0.0f, 0.0f));
    const Vec3f shadowPoint{0.0f, 1.5f, 0.0f};
    CHECK(!pointLitByProjector(a, shadowPoint));
    CHECK(pointLitByProjector(b, shadowPoint));
    const double lit = std::pow(projectorBlendWeight(a, shadowPoint), a.blend.gamma) + std::pow(projectorBlendWeight(b, shadowPoint), b.blend.gamma);
    CHECK(lit < 0.98);
    CHECK(lit > 0.2);
}

std::vector<CalibrationPoint> pointsFor(const SpatialDevice& truth, const std::vector<Vec3f>& world, double noise) {
    std::vector<CalibrationPoint> points;
    int index = 0;
    for (const Vec3f& w : world) {
        const ProjectedPoint p = projectPoint(truth, w);
        CalibrationPoint point;
        point.name = "P" + std::to_string(index + 1);
        point.world = w;
        point.outputX = p.x + static_cast<float>(noise * std::sin(1.7 * index + 0.3));
        point.outputY = p.y + static_cast<float>(noise * std::cos(2.3 * index + 0.9));
        points.push_back(point);
        ++index;
    }
    return points;
}

SpatialDevice truthDevice() {
    SpatialDevice truth;
    truth.pose = SpatialPose{Vec3f{1.2f, 1.8f, 6.5f}, -10.0f, 5.0f, 1.5f};
    truth.lens = SpatialLens{28.0f, 16.0f / 9.0f, 0.0f, 0.1f, 0.1f, 200.0f};
    return truth;
}

std::vector<Vec3f> wallAndPillar() {
    return {{-2.0f, 2.8f, 0.0f}, {2.0f, 2.8f, 0.0f}, {2.0f, 0.4f, 0.0f}, {-2.0f, 0.4f, 0.0f}, {0.0f, 1.6f, 0.0f}, {0.6f, 2.0f, 2.0f}, {0.6f, 0.2f, 2.0f}, {-0.6f, 1.2f, 2.0f}};
}

void testCalibration() {
    const SpatialDevice truth = truthDevice();
    const std::vector<CalibrationPoint> points = pointsFor(truth, wallAndPillar(), 0.0);

    SpatialDevice start = truth;
    start.pose.position = Vec3f{1.6f, 2.1f, 6.9f};
    start.pose.yaw += 4.0f;
    start.pose.pitch -= 3.0f;
    start.pose.roll -= 2.0f;
    const CalibrationResult solved = solveDevice(points, start);
    CHECK(solved.status == CalibrationStatus::Ok);
    CHECK(solved.usedPoints == 8 && solved.errors.size() == 8);
    CHECK(solved.rms < 1e-5);
    CHECK(nearVec(solved.device.pose.position, truth.pose.position, 2e-3));
    CHECK(near(solved.device.pose.yaw, truth.pose.yaw, 0.02) && near(solved.device.pose.pitch, truth.pose.pitch, 0.02) && near(solved.device.pose.roll, truth.pose.roll, 0.02));
    CHECK(solved.device.lens == truth.lens);
    CHECK(solved.starts == 1);

    const CalibrationResult again = solveDevice(points, start);
    CHECK(again.device == solved.device && again.rms == solved.rms && again.iterations == solved.iterations);

    SpatialDevice wrongFov = start;
    wrongFov.lens.fovY = 34.0f;
    CalibrationOptions withFov;
    withFov.solveFov = true;
    const CalibrationResult fovSolved = solveDevice(points, wrongFov, withFov);
    CHECK(fovSolved.status == CalibrationStatus::Ok);
    CHECK(near(fovSolved.device.lens.fovY, truth.lens.fovY, 0.05));
    CHECK(nearVec(fovSolved.device.pose.position, truth.pose.position, 0.03));

    SpatialDevice wild = truth;
    wild.pose = SpatialPose{Vec3f{-4.0f, 0.5f, 3.0f}, 80.0f, -20.0f, 0.0f};
    const CalibrationResult far = solveDevice(points, wild);
    CHECK(far.status == CalibrationStatus::Ok);
    CHECK(nearVec(far.device.pose.position, truth.pose.position, 0.02));
    CHECK(near(far.device.pose.yaw, truth.pose.yaw, 0.1));

    SpatialDevice lookingUp = truth;
    lookingUp.pose = SpatialPose{Vec3f{0.0f, 2.0f, 6.0f}, 0.0f, 80.0f, 0.0f};
    const CalibrationResult restarted = solveDevice(points, lookingUp);
    CHECK(restarted.status == CalibrationStatus::Ok);
    CHECK(restarted.starts > 1);
    CHECK(nearVec(restarted.device.pose.position, truth.pose.position, 0.02));
    CalibrationOptions single;
    single.multiStart = false;
    const CalibrationResult stuck = solveDevice(points, lookingUp, single);
    CHECK(stuck.starts == 1);

    SpatialDevice behindWall = truth;
    behindWall.pose = SpatialPose{Vec3f{0.0f, 2.0f, -12.0f}, 0.0f, 0.0f, 0.0f};
    const CalibrationResult mirrored = solveDevice(points, behindWall);
    CHECK(mirrored.status == CalibrationStatus::Ok);
    CHECK(nearVec(mirrored.device.pose.position, truth.pose.position, 0.02));

    std::vector<Vec3f> flat = {{-2.0f, 2.8f, 0.0f}, {2.0f, 2.8f, 0.0f}, {2.0f, 0.4f, 0.0f}, {-2.0f, 0.4f, 0.0f}};
    const std::vector<CalibrationPoint> flatPoints = pointsFor(truth, flat, 0.0);
    const CalibrationResult planar = solveDevice(flatPoints, start);
    CHECK(planar.status == CalibrationStatus::Ok);
    CHECK(nearVec(planar.device.pose.position, truth.pose.position, 5e-3));

    const std::vector<CalibrationPoint> noisy = pointsFor(truth, wallAndPillar(), 0.0005);
    const CalibrationResult withNoise = solveDevice(noisy, start);
    CHECK(withNoise.status == CalibrationStatus::Ok);
    CHECK(withNoise.rms < 0.0007 && withNoise.rmsPixels < 1.4);
    CHECK(nearVec(withNoise.device.pose.position, truth.pose.position, 0.05));
    CHECK(calibrationError(noisy, withNoise.device) == withNoise.rms);

    CHECK(solveDevice(std::vector<CalibrationPoint>(points.begin(), points.begin() + 3), start).status == CalibrationStatus::TooFewPoints);
    CHECK(solveDevice(std::vector<CalibrationPoint>(points.begin(), points.begin() + 5), start, withFov).status == CalibrationStatus::TooFewPoints);
    std::vector<Vec3f> line = {{-2.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 0.0f}};
    CHECK(solveDevice(pointsFor(truth, line, 0.0), start).status == CalibrationStatus::Collinear);

    std::vector<CalibrationPoint> broken = points;
    broken[0].outputX = 0.9f;
    broken[1].outputY = 0.9f;
    broken[2].outputX = 0.05f;
    broken[5].outputY = 0.7f;
    const CalibrationResult mismatch = solveDevice(broken, start);
    CHECK(mismatch.status == CalibrationStatus::NotConverged);
    CHECK(mismatch.rms > 0.002 && !mismatch.message.empty());

    std::vector<CalibrationPoint> switchedOff = broken;
    switchedOff[0].enabled = false;
    switchedOff[1].enabled = false;
    switchedOff[2].enabled = false;
    switchedOff[5].enabled = false;
    const CalibrationResult recovered = solveDevice(switchedOff, start);
    CHECK(recovered.usedPoints == 4);
    CHECK(recovered.status == CalibrationStatus::Ok || recovered.status == CalibrationStatus::NotConverged);

    CHECK(std::string(calibrationStatusName(CalibrationStatus::Ok)) == "ok");
    CHECK(std::string(calibrationStatusName(CalibrationStatus::Collinear)) == "points on one line");
}

}

int main() {
    testConventions();
    testProjection();
    testSurfaces();
    testJson();
    testMeshBasics();
    testCulling();
    testNearClipping();
    testRayCast();
    testMeshMatchesRayCast();
    testCameraSource();
    testOcclusion();
    testSeveralProjectors();
    testCalibration();
    return testing::finish();
}

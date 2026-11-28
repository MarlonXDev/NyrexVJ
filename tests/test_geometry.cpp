#include "check.hpp"

#include "core/i18n.hpp"
#include "core/settings.hpp"
#include "scene3d/doc_build.hpp"
#include "scene3d/geometry_graph.hpp"
#include "scene3d/geometry_graph_json.hpp"
#include "scene3d/scene_doc.hpp"
#include "scene3d/scene_model.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using namespace nvj;

bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

bool nearVec(Vec3 a, Vec3 b, float eps = 0.001f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

bool identical(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

GeoNode node(GeoNodeType type, const std::string& id, const std::string& in0 = "", const std::string& in1 = "") {
    GeoNode n = makeGeoNode(type, id);
    n.inputs = {in0, in1};
    return n;
}

GeometryGraph chain(PrimitiveKind kind, std::vector<GeoNode> middle) {
    GeometryGraph graph;
    graph.name = "Test";
    GeoNode first = makeGeoNode(GeoNodeType::Primitive, "p");
    first.primitive = kind;
    graph.nodes.push_back(first);
    std::string last = "p";
    for (GeoNode& n : middle) {
        n.inputs[0] = last;
        last = n.id;
        graph.nodes.push_back(n);
    }
    graph.nodes.push_back(node(GeoNodeType::Output, "out", last));
    return graph;
}

struct Bounds {
    Vec3 low;
    Vec3 high;
};

Bounds boundsOf(const SceneMesh& mesh) {
    Bounds b{mesh.positions[0], mesh.positions[0]};
    for (const Vec3& p : mesh.positions) {
        b.low = {std::min(b.low.x, p.x), std::min(b.low.y, p.y), std::min(b.low.z, p.z)};
        b.high = {std::max(b.high.x, p.x), std::max(b.high.y, p.y), std::max(b.high.z, p.z)};
    }
    return b;
}

float signedVolume(const SceneMesh& mesh) {
    float volume = 0.0f;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.positions[mesh.indices[i]];
        const Vec3& b = mesh.positions[mesh.indices[i + 1]];
        const Vec3& c = mesh.positions[mesh.indices[i + 2]];
        volume += dot(a, cross(b, c)) / 6.0f;
    }
    return volume;
}

bool meshSound(const SceneMesh& mesh) {
    if (mesh.positions.empty() || mesh.normals.size() != mesh.positions.size() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        return false;
    }
    if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.positions.size() * 2) {
        return false;
    }
    for (const uint32_t i : mesh.indices) {
        if (i >= mesh.positions.size()) {
            return false;
        }
    }
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        const Vec3& n = mesh.normals[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) {
            return false;
        }
        if (std::fabs(length(n) - 1.0f) > 0.01f) {
            return false;
        }
    }
    return true;
}

bool hasError(const GeoResult& result, const std::string& prefix) {
    for (const std::string& e : result.errors) {
        if (e.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

bool hasNote(const GeoResult& result, const std::string& prefix) {
    for (const std::string& e : result.notes) {
        if (e.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

void testNames() {
    for (int i = 0; i < kGeoNodeTypeCount; ++i) {
        const GeoNodeType type = static_cast<GeoNodeType>(i);
        GeoNodeType back = GeoNodeType::Output;
        CHECK(geoNodeFromKey(geoNodeKey(type), back));
        CHECK(back == type);
        CHECK(std::string(geoNodeLabel(type)).size() > 2);
    }
    GeoNodeType unused;
    CHECK(!geoNodeFromKey("teapot", unused));
    CHECK(!geoNodeFromKey("", unused));
    for (int i = 0; i < kGeoInstanceKindCount; ++i) {
        const GeoInstanceKind kind = static_cast<GeoInstanceKind>(i);
        GeoInstanceKind back = GeoInstanceKind::Ring;
        CHECK(geoInstanceFromKey(geoInstanceKey(kind), back));
        CHECK(back == kind);
    }
    GeoInstanceKind unusedKind;
    CHECK(!geoInstanceFromKey("spiral", unusedKind));
    CHECK(geoInputCount(GeoNodeType::Primitive) == 0);
    CHECK(geoInputCount(GeoNodeType::Merge) == 2);
    CHECK(geoInputCount(GeoNodeType::Noise) == 1);
    CHECK(geoInputCount(GeoNodeType::Output) == 1);
    PrimitiveKind kind = PrimitiveKind::Sphere;
    CHECK(primitiveFromKey("graph", kind) && kind == PrimitiveKind::Graph);
    CHECK(std::string(primitiveKey(PrimitiveKind::Graph)) == "graph" && kPrimitiveKindCount == 9);
    CHECK(primitiveMesh(PrimitiveKind::Graph).positions.empty());
}

void testDefaultGraph() {
    const GeometryGraph graph = makeDefaultGeometryGraph();
    const GeoResult a = evaluateGeometryGraph(graph);
    CHECK(a.ok);
    CHECK(a.errors.empty());
    CHECK(meshSound(a.mesh));
    CHECK(a.hasMaterial);
    CHECK(a.instances.empty());
    CHECK(a.indices == primitiveMesh(PrimitiveKind::Sphere).indices.size() * 16);
    CHECK(a.vertices > primitiveMesh(PrimitiveKind::Sphere).positions.size());
    CHECK(a.nodesEvaluated == 5);
    CHECK(a.milliseconds >= 0.0);
    const GeoResult b = evaluateGeometryGraph(graph);
    CHECK(a.hash == b.hash);
    CHECK(a.mesh.positions.size() == b.mesh.positions.size());
    bool same = true;
    for (size_t i = 0; i < a.mesh.positions.size(); ++i) {
        same = same && identical(a.mesh.positions[i], b.mesh.positions[i]);
    }
    CHECK(same);
}

void testSubdivide() {
    const SceneMesh base = primitiveMesh(PrimitiveKind::Cube);
    size_t lastVertices = base.positions.size();
    for (int levels = 1; levels <= kGeoMaxLevels; ++levels) {
        GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "s");
        divide.levels = levels;
        const GeoResult result = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide}));
        CHECK(result.ok);
        CHECK(meshSound(result.mesh));
        size_t factor = 1;
        for (int i = 0; i < levels; ++i) {
            factor *= 4;
        }
        CHECK(result.indices == base.indices.size() * factor);
        CHECK(result.vertices > lastVertices);
        lastVertices = result.vertices;
        const Bounds b = boundsOf(result.mesh);
        CHECK(nearVec(b.low, {-0.5f, -0.5f, -0.5f}) && nearVec(b.high, {0.5f, 0.5f, 0.5f}));
    }
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "s");
    divide.levels = 2;
    const GeoResult plane = evaluateGeometryGraph(chain(PrimitiveKind::Plane, {divide}));
    CHECK(plane.ok);
    bool flat = true;
    for (size_t i = 0; i < plane.mesh.positions.size(); ++i) {
        flat = flat && near(plane.mesh.positions[i].y, 0.0f, 1e-5f) && near(plane.mesh.normals[i].y, 1.0f, 1e-4f);
    }
    CHECK(flat);
}

void testTransform() {
    GeoNode t = makeGeoNode(GeoNodeType::Transform, "t");
    t.move = {1.0f, 2.0f, 3.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    GeoResult moved = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {t}));
    CHECK(moved.ok);
    Bounds b = boundsOf(moved.mesh);
    CHECK(nearVec(b.low, {0.0f, 1.0f, 2.0f}) && nearVec(b.high, {2.0f, 3.0f, 4.0f}));
    CHECK(meshSound(moved.mesh));

    GeoNode r = makeGeoNode(GeoNodeType::Transform, "t");
    r.scale = {2.0f, 1.0f, 1.0f};
    r.rotate = {0.0f, 90.0f, 0.0f};
    GeoResult turned = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {r}));
    CHECK(turned.ok);
    b = boundsOf(turned.mesh);
    CHECK(near(b.high.x - b.low.x, 1.0f) && near(b.high.z - b.low.z, 2.0f));
    CHECK(meshSound(turned.mesh));

    GeoNode mirror = makeGeoNode(GeoNodeType::Transform, "t");
    mirror.scale = {-1.0f, 1.0f, 1.0f};
    const GeoResult mirrored = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {mirror}));
    CHECK(mirrored.ok);
    CHECK(signedVolume(mirrored.mesh) > 0.9f);
    CHECK(near(signedVolume(evaluateGeometryGraph(chain(PrimitiveKind::Cube, {})).mesh), 1.0f, 0.01f));

    GeoNode squash = makeGeoNode(GeoNodeType::Transform, "t");
    squash.scale = {4.0f, 1.0f, 1.0f};
    const GeoResult squashed = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {squash}));
    CHECK(squashed.ok && meshSound(squashed.mesh));
    const float radius = boundsOf(primitiveMesh(PrimitiveKind::Sphere)).high.y;
    const float semi = radius * 4.0f;
    bool perpendicular = true;
    for (size_t i = 0; i < squashed.mesh.positions.size(); ++i) {
        const Vec3& p = squashed.mesh.positions[i];
        const Vec3 expected = normalize(Vec3{p.x / (semi * semi), p.y / (radius * radius), p.z / (radius * radius)});
        const Vec3& n = squashed.mesh.normals[i];
        perpendicular = perpendicular && n.x * expected.x + n.y * expected.y + n.z * expected.z > 0.999f;
    }
    CHECK(perpendicular);
}

void testNoise() {
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
    divide.levels = 1;
    GeoNode noise = makeGeoNode(GeoNodeType::Noise, "n");
    noise.amount = 0.3f;
    noise.seed = 4;
    const GeoResult before = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {divide}));
    const GeoResult after = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {divide, noise}));
    CHECK(before.ok && after.ok);
    CHECK(meshSound(after.mesh));
    CHECK(before.vertices == after.vertices && before.indices == after.indices);
    const Bounds in = boundsOf(before.mesh);
    const float extent = std::max({in.high.x - in.low.x, in.high.y - in.low.y, in.high.z - in.low.z});
    float biggest = 0.0f;
    float moved = 0.0f;
    for (size_t i = 0; i < before.mesh.positions.size(); ++i) {
        const float d = length(after.mesh.positions[i] - before.mesh.positions[i]);
        biggest = std::max(biggest, d);
        moved += d;
    }
    CHECK(biggest <= noise.amount * extent + 1e-4f);
    CHECK(moved > 0.1f);

    std::map<std::tuple<long, long, long>, Vec3> seen;
    bool closed = true;
    for (size_t i = 0; i < before.mesh.positions.size(); ++i) {
        const Vec3 p = before.mesh.positions[i];
        const auto key = std::make_tuple(std::lround(p.x * 10000.0), std::lround(p.y * 10000.0), std::lround(p.z * 10000.0));
        const auto found = seen.find(key);
        if (found == seen.end()) {
            seen[key] = after.mesh.positions[i];
        } else {
            closed = closed && nearVec(found->second, after.mesh.positions[i], 1e-5f);
        }
    }
    CHECK(closed);

    GeoNode other = noise;
    other.seed = 5;
    const GeoResult different = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {divide, other}));
    CHECK(different.hash != after.hash);
    bool differs = false;
    for (size_t i = 0; i < different.mesh.positions.size(); ++i) {
        differs = differs || !nearVec(different.mesh.positions[i], after.mesh.positions[i], 1e-4f);
    }
    CHECK(differs);
    const GeoResult again = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {divide, noise}));
    bool equal = true;
    for (size_t i = 0; i < again.mesh.positions.size(); ++i) {
        equal = equal && identical(again.mesh.positions[i], after.mesh.positions[i]);
    }
    CHECK(equal);

    GeoNode silent = noise;
    silent.amount = 0.0f;
    const GeoResult flat = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {divide, silent}));
    bool untouched = true;
    for (size_t i = 0; i < flat.mesh.positions.size(); ++i) {
        untouched = untouched && nearVec(flat.mesh.positions[i], before.mesh.positions[i], 1e-5f);
    }
    CHECK(untouched);
}

void testTwist() {
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
    divide.levels = 1;
    GeoNode twist = makeGeoNode(GeoNodeType::Twist, "w");
    twist.amount = 90.0f;
    const GeoResult before = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide}));
    const GeoResult after = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide, twist}));
    CHECK(before.ok && after.ok && meshSound(after.mesh));
    bool radius = true;
    bool centre = true;
    bool turned = false;
    for (size_t i = 0; i < before.mesh.positions.size(); ++i) {
        const Vec3 a = before.mesh.positions[i];
        const Vec3 b = after.mesh.positions[i];
        radius = radius && near(std::sqrt(a.x * a.x + a.z * a.z), std::sqrt(b.x * b.x + b.z * b.z), 1e-4f) && near(a.y, b.y, 1e-6f);
        if (std::fabs(a.y) < 1e-6f) {
            centre = centre && near(a.x, b.x, 1e-5f) && near(a.z, b.z, 1e-5f);
        } else {
            turned = turned || !nearVec(a, b, 0.01f);
        }
    }
    CHECK(radius && centre && turned);
    GeoNode none = twist;
    none.amount = 0.0f;
    const GeoResult flat = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide, none}));
    bool same = true;
    for (size_t i = 0; i < flat.mesh.positions.size(); ++i) {
        same = same && nearVec(flat.mesh.positions[i], before.mesh.positions[i], 1e-5f);
    }
    CHECK(same);
}

void testMerge() {
    GeometryGraph graph;
    GeoNode a = makeGeoNode(GeoNodeType::Primitive, "a");
    GeoNode b = makeGeoNode(GeoNodeType::Primitive, "b");
    b.primitive = PrimitiveKind::Cube;
    GeoNode move = node(GeoNodeType::Transform, "m", "b");
    move.move = {3.0f, 0.0f, 0.0f};
    graph.nodes = {a, b, move, node(GeoNodeType::Merge, "j", "a", "m"), node(GeoNodeType::Output, "out", "j")};
    const GeoResult both = evaluateGeometryGraph(graph);
    CHECK(both.ok && meshSound(both.mesh));
    const SceneMesh sphere = primitiveMesh(PrimitiveKind::Sphere);
    const SceneMesh cube = primitiveMesh(PrimitiveKind::Cube);
    CHECK(both.vertices == sphere.positions.size() + cube.positions.size());
    CHECK(both.indices == sphere.indices.size() + cube.indices.size());
    const Bounds bounds = boundsOf(both.mesh);
    CHECK(bounds.high.x > 3.4f);

    GeoLimits mergeLimit;
    mergeLimit.maxVertices = sphere.positions.size() + 5;
    const GeoResult tooBig = evaluateGeometryGraph(graph, mergeLimit);
    CHECK(!tooBig.ok && hasError(tooBig, "Too many vertices at node: j, " + std::to_string(sphere.positions.size() + cube.positions.size()) + " > " + std::to_string(mergeLimit.maxVertices)));

    graph.nodes[3].inputs[1].clear();
    const GeoResult single = evaluateGeometryGraph(graph);
    CHECK(single.ok && single.vertices == sphere.positions.size());

    GeometryGraph late;
    GeoNode ring = node(GeoNodeType::Instance, "i", "a");
    late.nodes = {a, b, ring, node(GeoNodeType::Merge, "j", "i", "b"), node(GeoNodeType::Output, "out", "j")};
    const GeoResult refused = evaluateGeometryGraph(late);
    CHECK(!refused.ok && hasError(refused, "Merge must come before Instance: "));
}

void testInstances() {
    const auto make = [](GeoInstanceKind kind, int count) {
        GeoNode i = makeGeoNode(GeoNodeType::Instance, "i");
        i.instance = kind;
        i.count = count;
        return chain(PrimitiveKind::Sphere, {i});
    };
    GeometryGraph ring = make(GeoInstanceKind::Ring, 8);
    ring.nodes[1].radius = 2.0f;
    const GeoResult r = evaluateGeometryGraph(ring);
    CHECK(r.ok && r.instances.size() == 8);
    std::set<long> angles;
    bool onRing = true;
    for (const GeoInstance& item : r.instances) {
        onRing = onRing && near(std::sqrt(item.position.x * item.position.x + item.position.z * item.position.z), 2.0f) && near(item.position.y, 0.0f) && near(item.scale, 1.0f);
        angles.insert(std::lround(std::atan2(item.position.z, item.position.x) * 1000.0));
    }
    CHECK(onRing && angles.size() == 8);

    GeometryGraph line = make(GeoInstanceKind::Line, 5);
    line.nodes[1].spacing = 0.5f;
    const GeoResult l = evaluateGeometryGraph(line);
    CHECK(l.ok && l.instances.size() == 5);
    CHECK(near(l.instances[0].position.x, -1.0f) && near(l.instances[4].position.x, 1.0f) && near(l.instances[2].position.x, 0.0f));

    GeometryGraph grid = make(GeoInstanceKind::Grid, 12);
    grid.nodes[1].spacing = 1.0f;
    const GeoResult g = evaluateGeometryGraph(grid);
    CHECK(g.ok && g.instances.size() == 12);
    std::set<std::pair<long, long>> cells;
    for (const GeoInstance& item : g.instances) {
        cells.insert({std::lround(item.position.x * 100.0), std::lround(item.position.z * 100.0)});
    }
    CHECK(cells.size() == 12);

    GeometryGraph scatter = make(GeoInstanceKind::Scatter, 60);
    scatter.nodes[1].radius = 3.0f;
    scatter.nodes[1].seed = 11;
    const GeoResult s = evaluateGeometryGraph(scatter);
    CHECK(s.ok && s.instances.size() == 60);
    bool inside = true;
    for (const GeoInstance& item : s.instances) {
        inside = inside && length(item.position) <= 3.0f + 1e-4f;
    }
    CHECK(inside);
    const GeoResult s2 = evaluateGeometryGraph(scatter);
    bool repeat = true;
    for (size_t i = 0; i < s.instances.size(); ++i) {
        repeat = repeat && identical(s.instances[i].position, s2.instances[i].position);
    }
    CHECK(repeat);
    scatter.nodes[1].seed = 12;
    const GeoResult s3 = evaluateGeometryGraph(scatter);
    bool changed = false;
    for (size_t i = 0; i < s.instances.size(); ++i) {
        changed = changed || !nearVec(s.instances[i].position, s3.instances[i].position, 1e-4f);
    }
    CHECK(changed);

    GeometryGraph jitter = make(GeoInstanceKind::Line, 40);
    jitter.nodes[1].jitter = 1.0f;
    const GeoResult j = evaluateGeometryGraph(jitter);
    float low = 10.0f;
    float high = 0.0f;
    for (const GeoInstance& item : j.instances) {
        low = std::min(low, item.scale);
        high = std::max(high, item.scale);
    }
    CHECK(low >= 0.4f - 1e-4f && high <= 1.6f + 1e-4f && high - low > 0.3f);

    GeometryGraph many = make(GeoInstanceKind::Ring, 5000);
    const GeoResult capped = evaluateGeometryGraph(many);
    CHECK(capped.ok && capped.instances.size() == kGeoMaxInstances);
    CHECK(hasNote(capped, "Instance count was limited: "));
    GeoLimits tight;
    tight.maxInstances = 10;
    const GeoResult ten = evaluateGeometryGraph(many, tight);
    CHECK(ten.ok && ten.instances.size() == 10);

    GeoNode again = makeGeoNode(GeoNodeType::Instance, "k");
    GeometryGraph twice = make(GeoInstanceKind::Ring, 4);
    twice.nodes.insert(twice.nodes.end() - 1, again);
    twice.nodes[twice.nodes.size() - 2].inputs[0] = "i";
    twice.nodes.back().inputs[0] = "k";
    const GeoResult nested = evaluateGeometryGraph(twice);
    CHECK(!nested.ok && hasError(nested, "Geometry is already instanced: "));
}

void testMaterialAndAudio() {
    GeoNode material = makeGeoNode(GeoNodeType::Material, "m");
    material.color = {0.1f, 0.2f, 0.3f};
    material.metallic = 0.7f;
    material.roughness = 0.2f;
    material.emissive = {1.0f, 0.5f, 0.0f};
    material.emissiveStrength = 2.0f;
    material.followPalette = true;
    GeoNode kick = makeGeoNode(GeoNodeType::Audio, "a");
    kick.source = BindSource::Kick;
    kick.target = BindTarget::Inflate;
    kick.bindAmount = 0.5f;
    const GeoResult one = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {material, kick}));
    CHECK(one.ok && one.hasMaterial);
    CHECK(near(one.material.color[2], 0.3f) && near(one.material.metallic, 0.7f) && near(one.material.roughness, 0.2f) && near(one.material.emissiveStrength, 2.0f) && one.material.followPalette);
    CHECK(one.bindings.size() == 1 && one.bindings[0].source == BindSource::Kick && one.bindings[0].target == BindTarget::Inflate && near(one.bindings[0].amount, 0.5f));
    CHECK(!evaluateGeometryGraph(chain(PrimitiveKind::Cube, {})).hasMaterial);

    std::vector<GeoNode> rows;
    for (int i = 0; i < 6; ++i) {
        GeoNode row = makeGeoNode(GeoNodeType::Audio, "a" + std::to_string(i));
        row.bindAmount = 0.1f * static_cast<float>(i + 1);
        rows.push_back(row);
    }
    const GeoResult full = evaluateGeometryGraph(chain(PrimitiveKind::Cube, rows));
    CHECK(full.ok && full.bindings.size() == kGeoMaxBindings);
    CHECK(hasNote(full, "Audio row left out, four rows fit: "));
    GeoNode silent = makeGeoNode(GeoNodeType::Audio, "z");
    silent.bindAmount = 0.0f;
    CHECK(evaluateGeometryGraph(chain(PrimitiveKind::Cube, {silent})).bindings.empty());
}

void testErrors() {
    GeometryGraph empty;
    const GeoResult none = evaluateGeometryGraph(empty);
    CHECK(!none.ok && none.errors.size() == 1 && none.errors[0] == "The graph has no nodes");

    GeometryGraph noOutput = chain(PrimitiveKind::Cube, {});
    noOutput.nodes.pop_back();
    const GeoResult a = evaluateGeometryGraph(noOutput);
    CHECK(!a.ok && a.errors[0] == "The graph needs an Output node");

    GeometryGraph twoOutputs = chain(PrimitiveKind::Cube, {});
    twoOutputs.nodes.push_back(node(GeoNodeType::Output, "out2", "p"));
    const GeoResult b = evaluateGeometryGraph(twoOutputs);
    CHECK(!b.ok && b.errors[0] == "The graph has more than one Output node");

    GeometryGraph missing = chain(PrimitiveKind::Cube, {makeGeoNode(GeoNodeType::Noise, "n")});
    missing.nodes[1].inputs[0].clear();
    const GeoResult c = evaluateGeometryGraph(missing);
    CHECK(!c.ok && hasError(c, "Missing input: n"));

    GeometryGraph ghost = chain(PrimitiveKind::Cube, {makeGeoNode(GeoNodeType::Noise, "n")});
    ghost.nodes[1].inputs[0] = "nowhere";
    const GeoResult d = evaluateGeometryGraph(ghost);
    CHECK(!d.ok && hasError(d, "Input does not exist: nowhere"));

    GeometryGraph loop;
    loop.nodes = {node(GeoNodeType::Noise, "a", "b"), node(GeoNodeType::Noise, "b", "a"), node(GeoNodeType::Output, "out", "a")};
    const GeoResult e = evaluateGeometryGraph(loop);
    CHECK(!e.ok && hasError(e, "Loop through node: "));

    GeometryGraph self;
    self.nodes = {node(GeoNodeType::Noise, "a", "a"), node(GeoNodeType::Output, "out", "a")};
    const GeoResult f = evaluateGeometryGraph(self);
    CHECK(!f.ok && hasError(f, "Loop through node: a"));

    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
    divide.levels = 4;
    GeoLimits small;
    small.maxVertices = 1000;
    const GeoResult g = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide}), small);
    CHECK(!g.ok && hasError(g, "Too many vertices at node: d, over 1000"));
    GeoLimits fewIndices;
    fewIndices.maxIndices = 500;
    const GeoResult h = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide}), fewIndices);
    CHECK(!h.ok && hasError(h, "Too many "));
    CHECK(g.mesh.positions.empty());
    GeoLimits tiny;
    tiny.maxVertices = 10;
    const GeoResult overCube = evaluateGeometryGraph(chain(PrimitiveKind::Cube, {}), tiny);
    CHECK(!overCube.ok && hasError(overCube, "Too many vertices at node: p, 24 > 10"));

    GeometryGraph stray = chain(PrimitiveKind::Cube, {});
    stray.nodes.insert(stray.nodes.begin() + 1, node(GeoNodeType::Noise, "extra", "p"));
    const GeoResult i = evaluateGeometryGraph(stray);
    CHECK(i.ok && hasNote(i, "Not connected to the output, skipped: extra"));

    CHECK(geoInputWouldLoop(chain(PrimitiveKind::Cube, {makeGeoNode(GeoNodeType::Noise, "n"), makeGeoNode(GeoNodeType::Twist, "w")}), 0, "w"));
    GeometryGraph path = chain(PrimitiveKind::Cube, {makeGeoNode(GeoNodeType::Noise, "n"), makeGeoNode(GeoNodeType::Twist, "w")});
    CHECK(geoInputWouldLoop(path, 0, "w"));
    CHECK(geoInputWouldLoop(path, 1, "n"));
    CHECK(geoInputWouldLoop(path, 2, "w"));
    CHECK(!geoInputWouldLoop(path, 2, "p"));
    CHECK(!geoInputWouldLoop(path, 3, "p"));
    CHECK(!geoInputWouldLoop(path, 3, ""));
    CHECK(geoInputWouldLoop(path, 99, "p"));

    GeometryGraph removal = chain(PrimitiveKind::Cube, {makeGeoNode(GeoNodeType::Noise, "n"), makeGeoNode(GeoNodeType::Twist, "w")});
    CHECK(geoRemoveNode(removal, 1));
    CHECK(removal.nodes.size() == 3 && removal.nodes[1].inputs[0].empty());
    CHECK(!geoRemoveNode(removal, 7) && !geoRemoveNode(removal, -1));
    CHECK(geoNewNodeId(removal) == "n1");
    CHECK(geoNodeIndex(removal, "out") == 2 && geoNodeIndex(removal, "zzz") == -1);
}

nlohmann::json graphJson(const GeometryGraph& graph) {
    nlohmann::json out;
    geometryGraphToJson(graph, out);
    return out;
}

void testJson() {
    GeometryGraph graph = makeDefaultGeometryGraph();
    GeoNode ring = node(GeoNodeType::Instance, "n6", "n4");
    ring.instance = GeoInstanceKind::Scatter;
    ring.count = 40;
    ring.jitter = 0.5f;
    GeoNode audio = node(GeoNodeType::Audio, "n7", "n6");
    audio.source = BindSource::Band3;
    audio.target = BindTarget::Ripple;
    audio.bindAmount = 0.4f;
    graph.nodes[4].inputs[0] = "n7";
    graph.nodes.push_back(ring);
    graph.nodes.push_back(audio);
    const nlohmann::json written = graphJson(graph);
    GeometryGraph back;
    std::vector<std::string> notes;
    CHECK(geometryGraphFromJson(written, back, &notes));
    CHECK(notes.empty());
    CHECK(geometryGraphHash(back) == geometryGraphHash(graph));
    CHECK(graphJson(back) == written);
    CHECK(back.nodes.size() == graph.nodes.size());
    CHECK(back.nodes[5].count == 40 && back.nodes[5].instance == GeoInstanceKind::Scatter);
    CHECK(back.nodes[6].source == BindSource::Band3 && back.nodes[6].target == BindTarget::Ripple);
    const GeoResult result = evaluateGeometryGraph(back);
    CHECK(result.ok && result.instances.size() == 40 && result.bindings.size() == 1);

    GeometryGraph changed = graph;
    changed.nodes[2].seed = 77;
    CHECK(geometryGraphHash(changed) != geometryGraphHash(graph));

    nlohmann::json bad = written;
    bad["nodes"].push_back({{"id", "x1"}, {"type", "teapot"}});
    bad["nodes"].push_back(42);
    bad["nodes"].push_back("text");
    GeometryGraph readBad;
    notes.clear();
    CHECK(geometryGraphFromJson(bad, readBad, &notes));
    CHECK(readBad.nodes.size() == graph.nodes.size());
    CHECK(notes.size() == 1 && notes[0].compare(0, 28, "Unknown node type, node left") == 0);

    nlohmann::json many = written;
    for (int i = 0; i < 60; ++i) {
        many["nodes"].push_back({{"id", "x" + std::to_string(i)}, {"type", "noise"}});
    }
    GeometryGraph readMany;
    notes.clear();
    CHECK(geometryGraphFromJson(many, readMany, &notes));
    CHECK(readMany.nodes.size() == kGeoMaxNodes);
    CHECK(!notes.empty() && notes[0].compare(0, 25, "Too many nodes in a geome") == 0);

    nlohmann::json wild = {{"name", "  Wild\u0001Name  "},
                           {"nodes", nlohmann::json::array({{{"id", "a"}, {"type", "noise"}, {"amount", 1e30}, {"frequency", -5}, {"octaves", 99}, {"seed", -4}},
                                                            {{"id", "a"}, {"type", "instance"}, {"count", 1e9}, {"radius", "far"}, {"jitter", 40}, {"inputs", nlohmann::json::array({"a", 7})}},
                                                            {{"id", "bad id!"}, {"type", "transform"}, {"scale", {0, 1e9, 5}}, {"move", {1, 2, 3}}},
                                                            {{"type", "material"}, {"color", {5, -1, 0.5}}, {"roughness", 0}, {"emissiveStrength", 1e9}},
                                                            {{"id", "z"}, {"type", "audio"}, {"source", "nope"}, {"target", "nope"}, {"amount", 99}},
                                                            {{"id", "out"}, {"type", "output"}}})}};
    GeometryGraph readWild;
    notes.clear();
    CHECK(geometryGraphFromJson(wild, readWild, &notes));
    CHECK(readWild.name == "WildName  " || readWild.name.find("Wild") != std::string::npos);
    CHECK(readWild.nodes.size() == 6);
    const GeoNode& n0 = readWild.nodes[0];
    CHECK(n0.amount <= 2.0f && n0.frequency >= 0.05f && n0.octaves == kGeoMaxOctaves && n0.seed == 0);
    const GeoNode& n1 = readWild.nodes[1];
    CHECK(n1.count == static_cast<int>(kGeoMaxInstances) && n1.radius == 1.5f && n1.jitter == 1.0f && n1.id != "a");
    const GeoNode& n2 = readWild.nodes[2];
    CHECK(n2.id != "bad id!" && std::fabs(n2.scale.x) >= 0.001f && n2.scale.y == 100.0f && n2.scale.z == 5.0f && n2.move.x == 1.0f && n2.move.z == 3.0f);
    const GeoNode& n3 = readWild.nodes[3];
    CHECK(!n3.id.empty() && n3.color[0] == 1.0f && n3.color[1] == 0.0f && n3.roughness >= 0.04f && n3.emissiveStrength <= 20.0f);
    const GeoNode& n4 = readWild.nodes[4];
    CHECK(n4.source == BindSource::Bass && n4.target == BindTarget::Scale && n4.bindAmount == 4.0f);
    std::set<std::string> ids;
    for (const GeoNode& n : readWild.nodes) {
        ids.insert(n.id);
    }
    CHECK(ids.size() == readWild.nodes.size());
    CHECK(!notes.empty());
    const GeoResult wildResult = evaluateGeometryGraph(readWild);
    CHECK(wildResult.ok || !wildResult.errors.empty());

    GeometryGraph none;
    CHECK(!geometryGraphFromJson(nlohmann::json::array(), none, nullptr));
    CHECK(!geometryGraphFromJson(nlohmann::json::object(), none, nullptr));
    CHECK(!geometryGraphFromJson(nlohmann::json{{"nodes", 5}}, none, nullptr));
    CHECK(geometryGraphFromJson(nlohmann::json{{"nodes", nlohmann::json::array()}}, none, nullptr) && none.nodes.empty() && none.name == "Graph");
}

void testFuzz() {
    const std::string text = graphJson(makeDefaultGeometryGraph()).dump();
    std::mt19937 rng(20261128);
    int parsed = 0;
    int evaluated = 0;
    int failedClean = 0;
    for (int round = 0; round < 400; ++round) {
        std::string mutated = text;
        const int edits = 1 + static_cast<int>(rng() % 4);
        for (int e = 0; e < edits; ++e) {
            const size_t at = rng() % mutated.size();
            switch (rng() % 4) {
            case 0:
                mutated[at] = static_cast<char>(rng() % 128);
                break;
            case 1:
                mutated.erase(at, 1 + rng() % 6);
                break;
            case 2:
                mutated.insert(at, std::to_string(static_cast<int>(rng() % 100000) - 500));
                break;
            default:
                mutated.insert(at, "\"x\"");
                break;
            }
            if (mutated.empty()) {
                mutated = "{}";
            }
        }
        const nlohmann::json json = nlohmann::json::parse(mutated, nullptr, false);
        if (json.is_discarded()) {
            continue;
        }
        GeometryGraph graph;
        if (!geometryGraphFromJson(json, graph, nullptr)) {
            continue;
        }
        ++parsed;
        GeoLimits limits;
        limits.maxVertices = 50000;
        const GeoResult result = evaluateGeometryGraph(graph, limits);
        if (result.ok) {
            ++evaluated;
            CHECK(meshSound(result.mesh));
            CHECK(result.vertices <= limits.maxVertices && result.instances.size() <= kGeoMaxInstances);
        } else {
            ++failedClean;
            CHECK(!result.errors.empty());
            CHECK(result.mesh.positions.empty());
        }
    }
    CHECK(parsed > 40);
    CHECK(evaluated + failedClean == parsed);
}

void testCache() {
    geometryGraphCacheClear();
    CHECK(geometryGraphCacheStats().entries == 0);
    GeometryGraph graph = makeDefaultGeometryGraph();
    bool hit = true;
    const std::shared_ptr<const GeoResult> first = evaluateGeometryGraphCached(graph, &hit);
    CHECK(!hit && first && first->ok);
    const std::shared_ptr<const GeoResult> second = evaluateGeometryGraphCached(graph, &hit);
    CHECK(hit && second.get() == first.get());
    graph.nodes[2].seed = 3;
    const std::shared_ptr<const GeoResult> third = evaluateGeometryGraphCached(graph, &hit);
    CHECK(!hit && third.get() != first.get() && third->hash != first->hash);
    GeoCacheStats stats = geometryGraphCacheStats();
    CHECK(stats.hits == 1 && stats.misses == 2 && stats.entries == 2);
    CHECK(stats.vertices == first->vertices + third->vertices);

    geometryGraphCacheClear();
    GeometryGraph varied = makeDefaultGeometryGraph();
    for (int i = 0; i < 12; ++i) {
        varied.nodes[2].seed = 100 + i;
        evaluateGeometryGraphCached(varied, &hit);
        CHECK(!hit);
    }
    stats = geometryGraphCacheStats();
    CHECK(stats.entries <= kGeoCacheEntries);
    CHECK(stats.vertices <= kGeoCacheVertices);
    varied.nodes[2].seed = 100;
    evaluateGeometryGraphCached(varied, &hit);
    CHECK(!hit);
    varied.nodes[2].seed = 111;
    evaluateGeometryGraphCached(varied, &hit);
    CHECK(hit);

    geometryGraphCacheClear();
    size_t bigVertices = 0;
    for (int i = 0; i < 8; ++i) {
        GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
        divide.levels = 3;
        GeoNode shift = makeGeoNode(GeoNodeType::Transform, "t");
        shift.move = {static_cast<float>(i), 0.0f, 0.0f};
        GeometryGraph big = chain(PrimitiveKind::Sphere, {divide, shift});
        big.name = "Big" + std::to_string(i);
        const std::shared_ptr<const GeoResult> result = evaluateGeometryGraphCached(big, &hit);
        CHECK(result->ok);
        bigVertices = result->vertices;
    }
    stats = geometryGraphCacheStats();
    CHECK(bigVertices > 20000);
    CHECK(stats.entries >= 1 && stats.entries < 8);
    CHECK(stats.vertices <= kGeoCacheVertices || stats.entries == 1);
    geometryGraphCacheClear();

    std::vector<std::thread> threads;
    std::atomic<int> failures{0};
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t, &failures]() {
            GeometryGraph local = makeDefaultGeometryGraph();
            for (int i = 0; i < 60; ++i) {
                local.nodes[2].seed = (t * 7 + i) % 14;
                const std::shared_ptr<const GeoResult> r = evaluateGeometryGraphCached(local);
                if (!r || !r->ok || r->vertices == 0) {
                    ++failures;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CHECK(failures.load() == 0);
    stats = geometryGraphCacheStats();
    CHECK(stats.entries <= kGeoCacheEntries && stats.hits + stats.misses == 240);
    geometryGraphCacheClear();
}

SceneDoc docWith(const GeometryGraph& graph, const std::string& objectName = "Rock") {
    SceneDoc doc;
    doc.name = "Graph scene";
    GeometryGraph copy = graph;
    docAddGraph(doc, copy);
    DocObject object = makeDefaultObject(PrimitiveKind::Graph);
    object.id = "o1";
    object.name = objectName;
    object.graph = doc.graphs[0].name;
    doc.objects.push_back(object);
    return doc;
}

void testDocument() {
    SceneDoc plain = makeDefaultDoc();
    const std::string plainText = docToJson(plain);
    CHECK(!docUsesGraphs(plain));
    CHECK(docRequiredVersion(plain) == kDocBaseVersion);
    CHECK(!nlohmann::json::parse(plainText).contains("graphs"));
    CHECK(kDocVersion == 5 && kDocGraphVersion == 5);

    SceneDoc doc = docWith(makeDefaultGeometryGraph());
    CHECK(docUsesGraphs(doc));
    CHECK(docRequiredVersion(doc) == kDocGraphVersion);
    const std::string text = docToJson(doc);
    const nlohmann::json json = nlohmann::json::parse(text);
    CHECK(json["version"] == 5);
    CHECK(json["graphs"].size() == 1 && json["graphs"][0]["name"] == "Rock");
    CHECK(json["objects"][0]["kind"] == "graph" && json["objects"][0]["graph"] == "Rock");
    SceneDoc back;
    std::string error;
    CHECK(docFromJson(text, back, error));
    CHECK(back.graphs.size() == 1 && back.objects[0].kind == PrimitiveKind::Graph && back.objects[0].graph == "Rock");
    CHECK(docToJson(back) == text);
    CHECK(geometryGraphHash(back.graphs[0]) == geometryGraphHash(doc.graphs[0]));

    nlohmann::json future = json;
    future["version"] = 6;
    CHECK(!docFromJson(future.dump(), back, error));
    CHECK(error.find("newer NyrexVJ") != std::string::npos);

    nlohmann::json older = json;
    older["version"] = 4;
    SceneDoc read;
    CHECK(docFromJson(older.dump(), read, error));
    CHECK(read.graphs.size() == 1);

    nlohmann::json broken = json;
    broken["graphs"] = nlohmann::json::array({{{"name", "A"}, {"nodes", nlohmann::json::array()}}, 5, {{"name", "B"}}, json["graphs"][0], json["graphs"][0]});
    SceneDoc readBroken;
    CHECK(docFromJson(broken.dump(), readBroken, error));
    std::set<std::string> names;
    for (const GeometryGraph& g : readBroken.graphs) {
        names.insert(g.name);
    }
    CHECK(names.size() == readBroken.graphs.size() && !readBroken.graphs.empty());
    CHECK(!readBroken.notes.empty());

    nlohmann::json crowd = json;
    crowd["graphs"] = nlohmann::json::array();
    for (int i = 0; i < 12; ++i) {
        nlohmann::json g = json["graphs"][0];
        g["name"] = "G" + std::to_string(i);
        crowd["graphs"].push_back(g);
    }
    SceneDoc readCrowd;
    CHECK(docFromJson(crowd.dump(), readCrowd, error));
    CHECK(readCrowd.graphs.size() == kGeoMaxGraphs);

    SceneDoc ops = docWith(makeDefaultGeometryGraph());
    CHECK(docGraphIndex(ops, "Rock") == 0 && docGraphIndex(ops, "Nope") == -1);
    CHECK(docNewGraphName(ops) == "Graph 1");
    CHECK(docAddGraph(ops, makeDefaultGeometryGraph()) == 1);
    CHECK(ops.graphs[1].name != "Rock");
    const int copy = docDuplicateGraph(ops, 0);
    CHECK(copy == 2 && ops.graphs[2].name == "Rock copy");
    CHECK(docDuplicateGraph(ops, 9) == -1);
    CHECK(docRenameGraph(ops, 0, "Stone"));
    CHECK(ops.objects[0].graph == "Stone");
    CHECK(!docRenameGraph(ops, 0, ops.graphs[1].name));
    CHECK(!docRenameGraph(ops, 0, "   "));
    CHECK(!docRenameGraph(ops, 9, "X"));
    CHECK(docRemoveGraph(ops, 0));
    CHECK(ops.objects[0].graph.empty() && ops.graphs.size() == 2);
    CHECK(!docRemoveGraph(ops, 5));
    while (ops.graphs.size() < kGeoMaxGraphs) {
        GeometryGraph g = makeDefaultGeometryGraph();
        g.name = "Extra" + std::to_string(ops.graphs.size());
        CHECK(docAddGraph(ops, g) >= 0);
    }
    CHECK(docAddGraph(ops, makeDefaultGeometryGraph()) == -1);

    SceneDoc tooMany = docWith(makeDefaultGeometryGraph());
    for (int i = 0; i < 20; ++i) {
        GeometryGraph g = makeDefaultGeometryGraph();
        g.name = "Same";
        tooMany.graphs.push_back(g);
    }
    SceneDoc clean = tooMany;
    sanitizeDoc(clean);
    CHECK(clean.graphs.size() == kGeoMaxGraphs);
    std::set<std::string> unique;
    for (const GeometryGraph& g : clean.graphs) {
        unique.insert(g.name);
    }
    CHECK(unique.size() == clean.graphs.size());

    DocObject notGraph = makeDefaultObject(PrimitiveKind::Cube);
    notGraph.graph = "Rock";
    SceneDoc stray;
    stray.objects.push_back(notGraph);
    sanitizeDoc(stray);
    CHECK(stray.objects[0].graph.empty());
}

DocAssets noAssets() {
    return DocAssets{};
}

GeometryGraph rockWithInstances(int count, bool audio) {
    GeoNode shrink = makeGeoNode(GeoNodeType::Transform, "t");
    shrink.scale = {0.2f, 0.2f, 0.2f};
    GeoNode ring = makeGeoNode(GeoNodeType::Instance, "i");
    ring.instance = GeoInstanceKind::Ring;
    ring.count = count;
    ring.radius = 2.0f;
    GeoNode material = makeGeoNode(GeoNodeType::Material, "m");
    material.color = {0.9f, 0.3f, 0.2f};
    std::vector<GeoNode> middle{shrink, ring, material};
    if (audio) {
        GeoNode kick = makeGeoNode(GeoNodeType::Audio, "a");
        kick.source = BindSource::Kick;
        kick.target = BindTarget::Scale;
        kick.bindAmount = 0.5f;
        GeoNode ripple = makeGeoNode(GeoNodeType::Audio, "b");
        ripple.source = BindSource::Bass;
        ripple.target = BindTarget::Ripple;
        ripple.bindAmount = 0.3f;
        middle.push_back(kick);
        middle.push_back(ripple);
    }
    GeometryGraph graph = chain(PrimitiveKind::Sphere, middle);
    graph.name = "Crowd";
    return graph;
}

void testBuild() {
    const GltfResult single = buildSceneFromDoc(docWith(makeDefaultGeometryGraph()), noAssets());
    CHECK(single.ok && single.warnings.empty());
    CHECK(single.scene.nodes.size() == 1 && single.scene.meshes.size() == 1 && single.scene.roots.size() == 1);
    CHECK(single.scene.nodes[0].meshes.size() == 1 && single.scene.nodes[0].children.empty());
    std::string error;
    CHECK(validateScene(single.scene, error));
    const GeoResult direct = evaluateGeometryGraph(makeDefaultGeometryGraph());
    CHECK(single.scene.meshes[0].positions.size() == direct.vertices);
    CHECK(near(single.scene.materials[single.scene.meshes[0].material].roughness, 0.85f));

    for (const int count : {1, 10, 1000}) {
        const GltfResult crowd = buildSceneFromDoc(docWith(rockWithInstances(count, true), "Crowd"), noAssets());
        CHECK(crowd.ok);
        CHECK(crowd.scene.nodes.size() == static_cast<size_t>(count) + 1);
        CHECK(crowd.scene.meshes.size() == 1 && crowd.scene.materials.size() == 1);
        CHECK(crowd.scene.nodes[0].children.size() == static_cast<size_t>(count));
        CHECK(crowd.scene.nodes[0].bindings.size() == 2);
        CHECK(crowd.scene.nodes[1].meshes.size() == 1 && crowd.scene.nodes[1].meshes[0] == 0);
        CHECK(crowd.scene.nodes[static_cast<size_t>(count)].meshes[0] == 0);
        CHECK(validateScene(crowd.scene, error));
        const SceneStats stats = sceneStats(crowd.scene);
        CHECK(stats.meshes == 1);
        const SceneMesh& mesh = crowd.scene.meshes[0];
        CHECK(mesh.positions.size() == primitiveMesh(PrimitiveKind::Sphere).positions.size());
    }

    const GltfResult limited = buildSceneFromDoc(docWith(rockWithInstances(1000, false), "Crowd"), noAssets(), [] {
        GltfLimits limits;
        limits.maxNodes = 500;
        return limits;
    }());
    CHECK(!limited.ok && limited.error.find("too large") != std::string::npos);

    GltfLimits off;
    off.allowGraphs = false;
    SceneDoc withCube = docWith(makeDefaultGeometryGraph());
    withCube.objects.push_back(makeDefaultObject(PrimitiveKind::Cube));
    const GltfResult skipped = buildSceneFromDoc(withCube, noAssets(), off);
    CHECK(skipped.ok && skipped.scene.meshes.size() == 1);
    CHECK(skipped.warnings.size() == 1 && skipped.warnings[0].compare(0, 40, "Geometry graphs are off in the settings,") == 0);
    const GltfResult onlyGraph = buildSceneFromDoc(docWith(makeDefaultGeometryGraph()), noAssets(), off);
    CHECK(!onlyGraph.ok);
    const GltfResult enabled = buildSceneFromDoc(withCube, noAssets());
    CHECK(enabled.ok && enabled.scene.meshes.size() == 2 && enabled.warnings.empty());

    SceneDoc ghost = withCube;
    ghost.objects[0].graph = "Missing";
    const GltfResult missing = buildSceneFromDoc(ghost, noAssets());
    CHECK(missing.ok && missing.scene.meshes.size() == 1);
    CHECK(missing.warnings.size() == 1 && missing.warnings[0].compare(0, 40, "The geometry graph does not exist, objec") == 0);

    SceneDoc loopDoc = withCube;
    loopDoc.graphs[0].nodes[1].inputs[0] = "n3";
    loopDoc.graphs[0].nodes[2].inputs[0] = "n2";
    const GltfResult looped = buildSceneFromDoc(loopDoc, noAssets());
    CHECK(looped.ok && looped.scene.meshes.size() == 1);
    CHECK(looped.warnings.size() >= 2);
    CHECK(looped.warnings[0].compare(0, 36, "The geometry graph has errors, objec") == 0);
    bool loopNamed = false;
    for (const std::string& w : looped.warnings) {
        loopNamed = loopNamed || w.compare(0, 19, "Loop through node: ") == 0;
    }
    CHECK(loopNamed);

    SceneDoc twin = docWith(makeDefaultGeometryGraph(), "First");
    DocObject second = makeDefaultObject(PrimitiveKind::Graph);
    second.id = "o2";
    second.name = "Second";
    second.graph = "Rock";
    second.position = {2.0f, 0.0f, 0.0f};
    twin.objects.push_back(second);
    const GltfResult both = buildSceneFromDoc(twin, noAssets());
    CHECK(both.ok && both.scene.nodes.size() == 2 && both.scene.meshes.size() == 1);
    CHECK(both.scene.nodes[0].meshes[0] == both.scene.nodes[1].meshes[0]);

    GeometryGraph noMaterial = chain(PrimitiveKind::Sphere, {makeGeoNode(GeoNodeType::Noise, "n")});
    noMaterial.name = "Plain";
    SceneDoc own = docWith(noMaterial, "A");
    own.objects[0].material.color = {1.0f, 0.0f, 0.0f};
    DocObject other = own.objects[0];
    other.id = "o2";
    other.name = "B";
    other.material.color = {0.0f, 0.0f, 1.0f};
    own.objects.push_back(other);
    const GltfResult colours = buildSceneFromDoc(own, noAssets());
    CHECK(colours.ok && colours.scene.meshes.size() == 2 && colours.scene.materials.size() == 2);
    CHECK(colours.scene.materials[0].baseColor[0] > 0.9f && colours.scene.materials[1].baseColor[2] > 0.9f);

    GeometryGraph tooBig;
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
    divide.levels = 4;
    tooBig = chain(PrimitiveKind::Sphere, {divide});
    tooBig.name = "Huge";
    GltfLimits tight;
    tight.maxVertices = 5000;
    const GltfResult huge = buildSceneFromDoc(docWith(tooBig, "Huge"), noAssets(), tight);
    CHECK(!huge.ok);

    SceneDoc mixed = makeDefaultDoc();
    const GltfResult before = buildSceneFromDoc(mixed, noAssets());
    mixed.graphs.clear();
    const GltfResult after = buildSceneFromDoc(mixed, noAssets());
    CHECK(before.ok && after.ok && before.scene.meshes.size() == after.scene.meshes.size() && before.scene.nodes.size() == after.scene.nodes.size());
}

void testSettingFlag() {
    namespace fs = std::filesystem;
    const fs::path scratch = fs::temp_directory_path() / "nyrexvj_geometry_settings";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", scratch.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", scratch.string().c_str(), 1);
#endif
    const auto readText = [&]() {
        std::ifstream in(scratch / "settings.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto loadWith = [&](const std::string& text) {
        std::ofstream out(scratch / "settings.json", std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        Settings result;
        loadSettings(result);
        return result;
    };
    const Settings defaults;
    CHECK(defaults.sceneGeometryGraphs);
    CHECK(saveSettings(defaults));
    CHECK(!nlohmann::json::parse(readText()).contains("sceneGeometryGraphs"));
    Settings off;
    off.sceneGeometryGraphs = false;
    CHECK(saveSettings(off));
    const nlohmann::json written = nlohmann::json::parse(readText());
    CHECK(written.contains("sceneGeometryGraphs") && written["sceneGeometryGraphs"] == false);
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(!loaded.sceneGeometryGraphs && loaded.sceneWorlds);
    CHECK(loadWith("{}").sceneGeometryGraphs);
    CHECK(!loadWith("{\"sceneGeometryGraphs\": false}").sceneGeometryGraphs);
    CHECK(loadWith("{\"sceneGeometryGraphs\": \"no\"}").sceneGeometryGraphs);
    CHECK(loadWith("{\"sceneWorlds\": false}").sceneGeometryGraphs);
    fs::remove_all(scratch, ec);

    CHECK(geometryGraphsEnabled());
    setGeometryGraphsEnabled(false);
    CHECK(!geometryGraphsEnabled());
    setGeometryGraphsEnabled(true);
    CHECK(geometryGraphsEnabled());
}

void testDrawBudget() {
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
    divide.levels = 3;
    GeoNode ring = makeGeoNode(GeoNodeType::Instance, "i");
    ring.instance = GeoInstanceKind::Ring;
    ring.count = 100;
    const GeoResult heavy = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {divide, ring}));
    CHECK(!heavy.ok && hasError(heavy, "Too many triangles to draw: out, "));
    CHECK(heavy.mesh.positions.empty() && heavy.instances.empty());

    GeoNode crowd = makeGeoNode(GeoNodeType::Instance, "i");
    crowd.instance = GeoInstanceKind::Scatter;
    crowd.count = 1000;
    const size_t perSphere = primitiveMesh(PrimitiveKind::Sphere).indices.size() / 3;
    const GeoResult thousand = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {crowd}));
    CHECK(thousand.ok && thousand.instances.size() == 1000 && perSphere * 1000 <= kGeoMaxDrawnTriangles);

    GeoLimits exact;
    exact.maxDrawnTriangles = perSphere * 1000;
    CHECK(evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {crowd}), exact).ok);
    GeoLimits under;
    under.maxDrawnTriangles = perSphere * 1000 - 1;
    const GeoResult refused = evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {crowd}), under);
    CHECK(!refused.ok && hasError(refused, "Too many triangles to draw: out, " + std::to_string(perSphere * 1000) + " > " + std::to_string(under.maxDrawnTriangles)));

    GeoLimits plain;
    plain.maxDrawnTriangles = perSphere - 1;
    CHECK(!evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {}), plain).ok);
    plain.maxDrawnTriangles = perSphere;
    CHECK(evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {}), plain).ok);
}

void testMessagesTranslated() {
    std::vector<std::string> messages;
    const auto take = [&](const GeoResult& r) {
        for (const std::string& e : r.errors) {
            messages.push_back(e);
        }
        for (const std::string& n : r.notes) {
            messages.push_back(n);
        }
    };
    GeometryGraph loop;
    loop.nodes = {node(GeoNodeType::Noise, "a", "a"), node(GeoNodeType::Output, "out", "a")};
    take(evaluateGeometryGraph(loop));
    GeometryGraph missing;
    missing.nodes = {node(GeoNodeType::Noise, "a"), node(GeoNodeType::Output, "out", "a")};
    take(evaluateGeometryGraph(missing));
    GeometryGraph absent;
    absent.nodes = {node(GeoNodeType::Output, "out", "nowhere")};
    take(evaluateGeometryGraph(absent));
    GeoLimits tiny;
    tiny.maxVertices = 10;
    take(evaluateGeometryGraph(chain(PrimitiveKind::Cube, {}), tiny));
    GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
    divide.levels = 4;
    GeoLimits fewIndices;
    fewIndices.maxIndices = 500;
    take(evaluateGeometryGraph(chain(PrimitiveKind::Cube, {divide}), fewIndices));
    GeoNode ring = makeGeoNode(GeoNodeType::Instance, "i");
    ring.count = 100;
    GeoNode again = makeGeoNode(GeoNodeType::Instance, "k");
    take(evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {ring, again})));
    GeometryGraph late;
    late.nodes = {makeGeoNode(GeoNodeType::Primitive, "a"), makeGeoNode(GeoNodeType::Primitive, "b"), node(GeoNodeType::Instance, "i", "a"), node(GeoNodeType::Merge, "j", "i", "b"), node(GeoNodeType::Output, "out", "j")};
    take(evaluateGeometryGraph(late));
    GeoNode limited = makeGeoNode(GeoNodeType::Instance, "i");
    limited.count = 100;
    GeoLimits fewCopies;
    fewCopies.maxInstances = 10;
    take(evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {limited}), fewCopies));
    GeoNode heavyDivide = makeGeoNode(GeoNodeType::Subdivide, "d");
    heavyDivide.levels = 3;
    GeoNode copies = makeGeoNode(GeoNodeType::Instance, "i");
    copies.count = 100;
    take(evaluateGeometryGraph(chain(PrimitiveKind::Sphere, {heavyDivide, copies})));
    take(evaluateGeometryGraph(GeometryGraph{}));
    GeometryGraph noOutput;
    noOutput.nodes = {makeGeoNode(GeoNodeType::Primitive, "a")};
    take(evaluateGeometryGraph(noOutput));
    CHECK(messages.size() >= 10);

    for (const Language language : {Language::German, Language::Spanish, Language::French, Language::Italian, Language::Portuguese, Language::Dutch, Language::Japanese}) {
        setLanguage(language);
        bool allTranslated = true;
        for (const std::string& m : messages) {
            allTranslated = allTranslated && trMessage(m) != m;
        }
        CHECK(allTranslated);
    }
    setLanguage(Language::English);
    bool unchanged = true;
    for (const std::string& m : messages) {
        unchanged = unchanged && trMessage(m) == m;
    }
    CHECK(unchanged);
}

ScenePack demoPack() {
    ScenePack pack;
    SceneDoc doc = makeDefaultDoc();
    doc.objects.clear();
    doc.name = "Rock Garden";

    GeometryGraph rocks;
    rocks.name = "Rocks";
    GeoNode sphere = makeGeoNode(GeoNodeType::Primitive, "n1");
    GeoNode divide = node(GeoNodeType::Subdivide, "n2", "n1");
    divide.levels = 2;
    GeoNode noise = node(GeoNodeType::Noise, "n3", "n2");
    noise.amount = 0.22f;
    noise.frequency = 2.2f;
    noise.octaves = 3;
    noise.seed = 7;
    GeoNode flatten = node(GeoNodeType::Transform, "n4", "n3");
    flatten.scale = {0.28f, 0.2f, 0.28f};
    GeoNode ring = node(GeoNodeType::Instance, "n5", "n4");
    ring.instance = GeoInstanceKind::Ring;
    ring.count = 14;
    ring.radius = 2.1f;
    ring.jitter = 0.6f;
    ring.seed = 3;
    GeoNode stone = node(GeoNodeType::Material, "n6", "n5");
    stone.color = {0.55f, 0.5f, 0.45f};
    stone.roughness = 0.9f;
    GeoNode kick = node(GeoNodeType::Audio, "n7", "n6");
    kick.source = BindSource::Kick;
    kick.target = BindTarget::Scale;
    kick.bindAmount = 0.35f;
    rocks.nodes = {sphere, divide, noise, flatten, ring, stone, kick, node(GeoNodeType::Output, "n8", "n7")};

    GeometryGraph crystal;
    crystal.name = "Crystal";
    GeoNode knot = makeGeoNode(GeoNodeType::Primitive, "n1");
    knot.primitive = PrimitiveKind::TorusKnot;
    GeoNode twist = node(GeoNodeType::Twist, "n2", "n1");
    twist.amount = 140.0f;
    GeoNode glass = node(GeoNodeType::Material, "n3", "n2");
    glass.color = {0.2f, 0.8f, 1.0f};
    glass.metallic = 0.3f;
    glass.roughness = 0.25f;
    glass.emissive = {0.0f, 0.6f, 1.0f};
    glass.emissiveStrength = 1.4f;
    GeoNode bass = node(GeoNodeType::Audio, "n4", "n3");
    bass.source = BindSource::Bass;
    bass.target = BindTarget::Inflate;
    bass.bindAmount = 0.3f;
    crystal.nodes = {knot, twist, glass, bass, node(GeoNodeType::Output, "n5", "n4")};

    docAddGraph(doc, rocks);
    docAddGraph(doc, crystal);
    DocObject stones = makeDefaultObject(PrimitiveKind::Graph);
    stones.name = "Rocks";
    stones.graph = "Rocks";
    stones.framing = true;
    DocObject gem = makeDefaultObject(PrimitiveKind::Graph);
    gem.name = "Crystal";
    gem.graph = "Crystal";
    gem.scale = {0.8f, 0.8f, 0.8f};
    gem.animations[0] = {true, MotionKind::Spin, 0.12f, 1.0f, 1, 0.0f};
    doc.objects = {stones, gem};
    sanitizeDoc(doc);
    pack.doc = doc;
    return pack;
}

void testPack() {
    const ScenePack pack = demoPack();
    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(writeScenePack(pack, bytes, error));
    ScenePack back;
    CHECK(readScenePack(bytes.data(), bytes.size(), back, error));
    CHECK(back.doc.graphs.size() == 2 && back.doc.notes.empty());
    CHECK(docToJson(back.doc) == docToJson(pack.doc));
    CHECK(geometryGraphHash(back.doc.graphs[0]) == geometryGraphHash(pack.doc.graphs[0]));
    const GltfResult built = buildSceneFromDoc(back.doc, DocAssets{});
    CHECK(built.ok && built.warnings.empty());
    CHECK(built.scene.nodes.size() == 16 && built.scene.meshes.size() == 2);
    CHECK(validateScene(built.scene, error));
}

void testDemoFiles() {
    namespace fs = std::filesystem;
    const fs::path file = fs::path(NYREXVJ_SOURCE_DIR) / "docs" / "examples" / "geometry-graph" / "Rock Garden.nyxclip";
    CHECK(fs::exists(file));
    if (!fs::exists(file)) {
        return;
    }
    ScenePack pack;
    std::string error;
    CHECK(readScenePackFile(file, pack, error));
    CHECK(pack.doc.notes.empty() && pack.doc.graphs.size() == 2);
    CHECK(docRequiredVersion(pack.doc) == kDocGraphVersion);
    const GltfResult built = buildSceneFromDoc(pack.doc, DocAssets{});
    CHECK(built.ok && built.warnings.empty());
    CHECK(validateScene(built.scene, error));
    CHECK(built.scene.nodes.size() == 16 && built.scene.meshes.size() == 2);
    const ScenePack fresh = demoPack();
    CHECK(docToJson(pack.doc) == docToJson(fresh.doc));
}

int writeDemo(const std::filesystem::path& folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string error;
    if (!writeScenePackFile(folder / "Rock Garden.nyxclip", demoPack(), error)) {
        std::fprintf(stderr, "could not write the demo: %s\n", error.c_str());
        return 1;
    }
    std::printf("demo written to %s\n", folder.string().c_str());
    return 0;
}

double medianMs(int rounds, const std::function<void()>& work) {
    std::vector<double> times;
    for (int i = 0; i < rounds; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        work();
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

int writeBench(const char* path) {
    nlohmann::json report;
    report["version"] = 1;
    const GeometryGraph rock = makeDefaultGeometryGraph();
    GeoResult last;
    report["rock"]["evaluateMs"] = medianMs(15, [&] { last = evaluateGeometryGraph(rock); });
    report["rock"]["vertices"] = last.vertices;
    report["rock"]["indices"] = last.indices;
    report["rock"]["hash"] = std::to_string(last.hash);
    geometryGraphCacheClear();
    evaluateGeometryGraphCached(rock);
    report["rock"]["cachedMs"] = medianMs(200, [&] { evaluateGeometryGraphCached(rock); });
    report["rock"]["hashMs"] = medianMs(200, [&] { geometryGraphHash(rock); });
    for (const int levels : {1, 2, 3, 4}) {
        GeoNode divide = makeGeoNode(GeoNodeType::Subdivide, "d");
        divide.levels = levels;
        GeoNode noise = makeGeoNode(GeoNodeType::Noise, "n");
        GeometryGraph graph = chain(PrimitiveKind::Sphere, {divide, noise});
        GeoResult r;
        const double ms = medianMs(5, [&] { r = evaluateGeometryGraph(graph); });
        nlohmann::json row;
        row["levels"] = levels;
        row["evaluateMs"] = ms;
        row["vertices"] = r.vertices;
        row["indices"] = r.indices;
        row["ok"] = r.ok;
        report["noise"].push_back(row);
    }
    for (const int count : {1, 10, 1000}) {
        for (const bool audio : {false, true}) {
            const SceneDoc doc = docWith(rockWithInstances(count, audio), "Crowd");
            GltfResult built;
            nlohmann::json row;
            row["instances"] = count;
            row["audio"] = audio;
            geometryGraphCacheClear();
            row["firstBuildMs"] = medianMs(1, [&] { built = buildSceneFromDoc(doc, DocAssets{}, {}); });
            row["buildMs"] = medianMs(15, [&] { built = buildSceneFromDoc(doc, DocAssets{}, {}); });
            row["ok"] = built.ok;
            row["nodes"] = built.scene.nodes.size();
            row["meshes"] = built.scene.meshes.size();
            const SceneStats stats = sceneStats(built.scene);
            row["vertices"] = stats.vertices;
            row["triangles"] = stats.triangles;
            row["gpuBytes"] = stats.gpuBytes;
            report["instances"].push_back(row);
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << report.dump(2);
    return out.good() ? 0 : 1;
}

}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--bench") {
        return writeBench(argv[2]);
    }
    if (argc >= 3 && std::string(argv[1]) == "--write-demo") {
        return writeDemo(argv[2]);
    }
    testNames();
    testDefaultGraph();
    testSubdivide();
    testTransform();
    testNoise();
    testTwist();
    testMerge();
    testInstances();
    testMaterialAndAudio();
    testErrors();
    testJson();
    testFuzz();
    testCache();
    testDocument();
    testBuild();
    testSettingFlag();
    testDrawBudget();
    testMessagesTranslated();
    testPack();
    testDemoFiles();
    return testing::finish();
}

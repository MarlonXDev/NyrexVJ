#include "scene3d/gltf.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <map>

namespace nvj {

namespace {

using json = nlohmann::json;

class BinaryBuilder {
public:
    int addView(const void* data, size_t bytes, int target) {
        while (data_.size() % 4 != 0) {
            data_.push_back(0);
        }
        json view;
        view["buffer"] = 0;
        view["byteOffset"] = data_.size();
        view["byteLength"] = bytes;
        if (target != 0) {
            view["target"] = target;
        }
        const auto* p = static_cast<const uint8_t*>(data);
        data_.insert(data_.end(), p, p + bytes);
        views_.push_back(std::move(view));
        return static_cast<int>(views_.size()) - 1;
    }

    int addAccessor(const void* data, size_t bytes, int target, int componentType, size_t count, const char* type, const std::vector<float>* minimum, const std::vector<float>* maximum) {
        json accessor;
        accessor["bufferView"] = addView(data, bytes, target);
        accessor["componentType"] = componentType;
        accessor["count"] = count;
        accessor["type"] = type;
        if (minimum != nullptr) {
            accessor["min"] = *minimum;
        }
        if (maximum != nullptr) {
            accessor["max"] = *maximum;
        }
        accessors_.push_back(std::move(accessor));
        return static_cast<int>(accessors_.size()) - 1;
    }

    std::vector<uint8_t> data_;
    json views_ = json::array();
    json accessors_ = json::array();
};

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

}

std::vector<uint8_t> writeGlb(const Scene& scene) {
    BinaryBuilder bin;
    struct MeshAccessors {
        int position = -1;
        int normal = -1;
        int uv = -1;
        int color = -1;
        int indices = -1;
    };
    std::vector<MeshAccessors> accessors(scene.meshes.size());
    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        const SceneMesh& mesh = scene.meshes[i];
        std::vector<float> lo(3, 3.4e38f);
        std::vector<float> hi(3, -3.4e38f);
        std::vector<float> flat;
        flat.reserve(mesh.positions.size() * 3);
        for (const Vec3& p : mesh.positions) {
            flat.insert(flat.end(), {p.x, p.y, p.z});
            lo = {std::min(lo[0], p.x), std::min(lo[1], p.y), std::min(lo[2], p.z)};
            hi = {std::max(hi[0], p.x), std::max(hi[1], p.y), std::max(hi[2], p.z)};
        }
        accessors[i].position = bin.addAccessor(flat.data(), flat.size() * 4, 34962, 5126, mesh.positions.size(), "VEC3", &lo, &hi);
        flat.clear();
        for (const Vec3& n : mesh.normals) {
            flat.insert(flat.end(), {n.x, n.y, n.z});
        }
        accessors[i].normal = bin.addAccessor(flat.data(), flat.size() * 4, 34962, 5126, mesh.normals.size(), "VEC3", nullptr, nullptr);
        if (!mesh.uvs.empty()) {
            accessors[i].uv = bin.addAccessor(mesh.uvs.data(), mesh.uvs.size() * 4, 34962, 5126, mesh.uvs.size() / 2, "VEC2", nullptr, nullptr);
        }
        if (!mesh.colors.empty()) {
            accessors[i].color = bin.addAccessor(mesh.colors.data(), mesh.colors.size() * 4, 34962, 5126, mesh.colors.size() / 4, "VEC4", nullptr, nullptr);
        }
        accessors[i].indices = bin.addAccessor(mesh.indices.data(), mesh.indices.size() * 4, 34963, 5125, mesh.indices.size(), "SCALAR", nullptr, nullptr);
    }

    json doc;
    doc["asset"] = {{"version", "2.0"}, {"generator", "NyrexVJ"}};
    json materials = json::array();
    for (const SceneMaterial& m : scene.materials) {
        json material;
        material["pbrMetallicRoughness"] = {{"baseColorFactor", {m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]}}, {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}};
        if (m.doubleSided) {
            material["doubleSided"] = true;
        }
        materials.push_back(std::move(material));
    }
    if (!materials.empty()) {
        doc["materials"] = std::move(materials);
    }

    json meshes = json::array();
    json nodes = json::array();
    std::map<std::vector<int>, size_t> meshByContent;
    for (const SceneNode& n : scene.nodes) {
        json node;
        if (!n.name.empty()) {
            node["name"] = n.name;
        }
        if (n.translation.x != 0.0f || n.translation.y != 0.0f || n.translation.z != 0.0f) {
            node["translation"] = {n.translation.x, n.translation.y, n.translation.z};
        }
        if (n.rotation.x != 0.0f || n.rotation.y != 0.0f || n.rotation.z != 0.0f || n.rotation.w != 1.0f) {
            node["rotation"] = {n.rotation.x, n.rotation.y, n.rotation.z, n.rotation.w};
        }
        if (n.scale.x != 1.0f || n.scale.y != 1.0f || n.scale.z != 1.0f) {
            node["scale"] = {n.scale.x, n.scale.y, n.scale.z};
        }
        if (!n.children.empty()) {
            node["children"] = n.children;
        }
        const auto known = meshByContent.find(n.meshes);
        if (known != meshByContent.end()) {
            node["mesh"] = known->second;
        } else if (!n.meshes.empty()) {
            json primitives = json::array();
            for (const int meshIndex : n.meshes) {
                const SceneMesh& mesh = scene.meshes[static_cast<size_t>(meshIndex)];
                const MeshAccessors& a = accessors[static_cast<size_t>(meshIndex)];
                json attributes = {{"POSITION", a.position}, {"NORMAL", a.normal}};
                if (a.uv >= 0) {
                    attributes["TEXCOORD_0"] = a.uv;
                }
                if (a.color >= 0) {
                    attributes["COLOR_0"] = a.color;
                }
                json primitive = {{"attributes", attributes}, {"indices", a.indices}, {"mode", 4}};
                if (mesh.material >= 0) {
                    primitive["material"] = mesh.material;
                }
                primitives.push_back(std::move(primitive));
            }
            node["mesh"] = meshes.size();
            meshByContent[n.meshes] = meshes.size();
            meshes.push_back({{"primitives", std::move(primitives)}});
        }
        nodes.push_back(std::move(node));
    }
    doc["nodes"] = std::move(nodes);
    if (!meshes.empty()) {
        doc["meshes"] = std::move(meshes);
    }
    json sceneEntry;
    sceneEntry["nodes"] = scene.roots;
    doc["scenes"] = json::array();
    doc["scenes"].push_back(std::move(sceneEntry));
    doc["scene"] = 0;

    if (!scene.animations.empty()) {
        json animations = json::array();
        for (const SceneAnimation& animation : scene.animations) {
            json samplers = json::array();
            json channels = json::array();
            for (const SceneChannel& channel : animation.channels) {
                const std::vector<float> lo{channel.times.front()};
                const std::vector<float> hi{channel.times.back()};
                const int input = bin.addAccessor(channel.times.data(), channel.times.size() * 4, 0, 5126, channel.times.size(), "SCALAR", &lo, &hi);
                const bool rotation = channel.path == AnimPath::Rotation;
                const int output = bin.addAccessor(channel.values.data(), channel.values.size() * 4, 0, 5126, channel.times.size(), rotation ? "VEC4" : "VEC3", nullptr, nullptr);
                samplers.push_back({{"input", input}, {"output", output}, {"interpolation", channel.interp == AnimInterp::Step ? "STEP" : "LINEAR"}});
                const char* path = channel.path == AnimPath::Translation ? "translation" : (rotation ? "rotation" : "scale");
                channels.push_back({{"sampler", samplers.size() - 1}, {"target", {{"node", channel.node}, {"path", path}}}});
            }
            json entry = {{"samplers", std::move(samplers)}, {"channels", std::move(channels)}};
            if (!animation.name.empty()) {
                entry["name"] = animation.name;
            }
            animations.push_back(std::move(entry));
        }
        doc["animations"] = std::move(animations);
    }

    while (bin.data_.size() % 4 != 0) {
        bin.data_.push_back(0);
    }
    json bufferEntry;
    bufferEntry["byteLength"] = bin.data_.size();
    doc["buffers"] = json::array();
    doc["buffers"].push_back(std::move(bufferEntry));
    doc["bufferViews"] = std::move(bin.views_);
    doc["accessors"] = std::move(bin.accessors_);

    std::string text = doc.dump();
    while (text.size() % 4 != 0) {
        text.push_back(' ');
    }
    std::vector<uint8_t> out;
    const uint32_t total = static_cast<uint32_t>(12 + 8 + text.size() + 8 + bin.data_.size());
    out.reserve(total);
    appendU32(out, 0x46546C67u);
    appendU32(out, 2);
    appendU32(out, total);
    appendU32(out, static_cast<uint32_t>(text.size()));
    appendU32(out, 0x4E4F534Au);
    out.insert(out.end(), text.begin(), text.end());
    appendU32(out, static_cast<uint32_t>(bin.data_.size()));
    appendU32(out, 0x004E4942u);
    out.insert(out.end(), bin.data_.begin(), bin.data_.end());
    return out;
}

}

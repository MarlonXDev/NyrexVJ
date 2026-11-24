#include "scene3d/gltf.hpp"

#include <draco/compression/encode.h>
#include <draco/mesh/mesh.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

int addAttribute(draco::Mesh& mesh, draco::GeometryAttribute::Type type, draco::DataType dataType, int components, const void* data, size_t points) {
    draco::GeometryAttribute attribute;
    const int byteStride = components * static_cast<int>(draco::DataTypeLength(dataType));
    attribute.Init(type, nullptr, static_cast<uint8_t>(components), dataType, false, byteStride, 0);
    const int id = mesh.AddAttribute(attribute, true, static_cast<uint32_t>(points));
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < points; ++i) {
        mesh.attribute(id)->SetAttributeValue(draco::AttributeValueIndex(static_cast<uint32_t>(i)), bytes + i * static_cast<size_t>(byteStride));
    }
    return id;
}

}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: draco_pack input.glb output-prefix sequential|edgebreaker\n");
        return 2;
    }
    const nvj::GltfResult loaded = nvj::loadGltfFile(argv[1]);
    if (!loaded.ok || loaded.scene.meshes.size() != 1) {
        std::fprintf(stderr, "cannot load %s: %s\n", argv[1], loaded.error.c_str());
        return 1;
    }
    const nvj::SceneMesh& source = loaded.scene.meshes[0];
    const size_t points = source.positions.size();
    draco::Mesh mesh;
    mesh.set_num_points(static_cast<uint32_t>(points));
    nlohmann::json ids;
    ids["POSITION"] = addAttribute(mesh, draco::GeometryAttribute::POSITION, draco::DT_FLOAT32, 3, source.positions.data(), points);
    ids["NORMAL"] = addAttribute(mesh, draco::GeometryAttribute::NORMAL, draco::DT_FLOAT32, 3, source.normals.data(), points);
    if (!source.joints.empty()) {
        ids["JOINTS_0"] = addAttribute(mesh, draco::GeometryAttribute::GENERIC, draco::DT_UINT16, 4, source.joints.data(), points);
        ids["WEIGHTS_0"] = addAttribute(mesh, draco::GeometryAttribute::GENERIC, draco::DT_FLOAT32, 4, source.weights.data(), points);
    }
    const size_t faces = source.indices.size() / 3;
    mesh.SetNumFaces(faces);
    for (size_t f = 0; f < faces; ++f) {
        draco::Mesh::Face face;
        for (size_t k = 0; k < 3; ++k) {
            face[static_cast<int>(k)] = draco::PointIndex(source.indices[f * 3 + k]);
        }
        mesh.SetFace(draco::FaceIndex(static_cast<uint32_t>(f)), face);
    }
    draco::Encoder encoder;
    const bool sequential = std::string(argv[3]) == "sequential";
    encoder.SetSpeedOptions(sequential ? 10 : 5, sequential ? 10 : 5);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, 14);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, 10);
    if (sequential) {
        encoder.SetEncodingMethod(draco::MESH_SEQUENTIAL_ENCODING);
    }
    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(mesh, &buffer);
    if (!status.ok()) {
        std::fprintf(stderr, "encode failed: %s\n", status.error_msg());
        return 1;
    }
    const std::string prefix = argv[2];
    std::ofstream(prefix + ".drc", std::ios::binary).write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    nlohmann::json info;
    info["attributes"] = ids;
    info["points"] = points;
    info["faces"] = faces;
    std::ofstream(prefix + ".json") << info.dump(2) << "\n";
    return 0;
}

#include "scene3d/gltf_compression.hpp"

#if NYREXVJ_MESHOPT
#include <meshoptimizer.h>
#endif

#if NYREXVJ_DRACO
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#include <draco/mesh/mesh.h>
#endif

namespace nvj {

bool meshoptAvailable() {
#if NYREXVJ_MESHOPT
    return true;
#else
    return false;
#endif
}

bool dracoAvailable() {
#if NYREXVJ_DRACO
    return true;
#else
    return false;
#endif
}

std::string meshoptDecode(uint8_t* destination, size_t count, size_t stride, MeshoptMode mode, MeshoptFilter filter, const uint8_t* source, size_t sourceSize) {
#if NYREXVJ_MESHOPT
    int result = 0;
    switch (mode) {
    case MeshoptMode::Attributes:
        result = meshopt_decodeVertexBuffer(destination, count, stride, source, sourceSize);
        break;
    case MeshoptMode::Triangles:
        result = meshopt_decodeIndexBuffer(destination, count, stride, source, sourceSize);
        break;
    case MeshoptMode::Indices:
        result = meshopt_decodeIndexSequence(destination, count, stride, source, sourceSize);
        break;
    }
    if (result != 0) {
        return "the Meshopt data is damaged";
    }
    if (filter != MeshoptFilter::None) {
        if (mode != MeshoptMode::Attributes) {
            return "a Meshopt filter is only valid for attributes";
        }
        if (filter == MeshoptFilter::Octahedral) {
            if (stride != 4 && stride != 8) {
                return "the Meshopt octahedral filter needs a stride of 4 or 8";
            }
            meshopt_decodeFilterOct(destination, count, stride);
        } else if (filter == MeshoptFilter::Quaternion) {
            if (stride != 8) {
                return "the Meshopt quaternion filter needs a stride of 8";
            }
            meshopt_decodeFilterQuat(destination, count, stride);
        } else {
            if (stride % 4 != 0) {
                return "the Meshopt exponential filter needs a stride that is a multiple of 4";
            }
            meshopt_decodeFilterExp(destination, count, stride);
        }
    }
    return std::string();
#else
    (void)destination;
    (void)count;
    (void)stride;
    (void)mode;
    (void)filter;
    (void)source;
    (void)sourceSize;
    return "this build of NyrexVJ has no Meshopt support";
#endif
}

std::string dracoDecode(const uint8_t* data, size_t size, const std::vector<int>& attributeIds, size_t maxPoints, size_t maxIndices, DracoMeshData& out) {
#if NYREXVJ_DRACO
    draco::DecoderBuffer buffer;
    buffer.Init(reinterpret_cast<const char*>(data), size);
    draco::Decoder decoder;
    const auto type = draco::Decoder::GetEncodedGeometryType(&buffer);
    if (!type.ok() || type.value() != draco::TRIANGULAR_MESH) {
        return "the Draco data is not a triangle mesh";
    }
    auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
    if (!decoded.ok()) {
        return "the Draco data is damaged";
    }
    const std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
    const size_t points = static_cast<size_t>(mesh->num_points());
    const size_t faces = static_cast<size_t>(mesh->num_faces());
    if (points == 0 || faces == 0 || points > maxPoints || faces * 3 > maxIndices) {
        return "the Draco mesh is empty or exceeds the size limits";
    }
    out.points = points;
    out.indices.resize(faces * 3);
    for (size_t f = 0; f < faces; ++f) {
        const draco::Mesh::Face& face = mesh->face(draco::FaceIndex(static_cast<uint32_t>(f)));
        for (size_t k = 0; k < 3; ++k) {
            out.indices[f * 3 + k] = face[static_cast<int>(k)].value();
        }
    }
    out.attributes.clear();
    for (const int id : attributeIds) {
        const draco::PointAttribute* attribute = mesh->GetAttributeByUniqueId(static_cast<uint32_t>(id));
        if (attribute == nullptr) {
            return "a Draco attribute id does not exist";
        }
        DracoAttributeData data;
        data.components = attribute->num_components();
        if (data.components < 1 || data.components > 4) {
            return "a Draco attribute has an unsupported size";
        }
        data.values.resize(points * static_cast<size_t>(data.components));
        for (size_t p = 0; p < points; ++p) {
            const draco::AttributeValueIndex index = attribute->mapped_index(draco::PointIndex(static_cast<uint32_t>(p)));
            if (!attribute->ConvertValue<float>(index, static_cast<int8_t>(data.components), &data.values[p * static_cast<size_t>(data.components)])) {
                return "a Draco attribute could not be converted";
            }
        }
        out.attributes.push_back(std::move(data));
    }
    return std::string();
#else
    (void)data;
    (void)size;
    (void)attributeIds;
    (void)maxPoints;
    (void)maxIndices;
    (void)out;
    return "this build of NyrexVJ has no Draco support";
#endif
}

}

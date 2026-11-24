#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

enum class MeshoptMode { Attributes, Triangles, Indices };
enum class MeshoptFilter { None, Octahedral, Quaternion, Exponential };

bool meshoptAvailable();
bool dracoAvailable();

std::string meshoptDecode(uint8_t* destination, size_t count, size_t stride, MeshoptMode mode, MeshoptFilter filter, const uint8_t* source, size_t sourceSize);

struct DracoAttributeData {
    int components = 0;
    std::vector<float> values;
};

struct DracoMeshData {
    size_t points = 0;
    std::vector<uint32_t> indices;
    std::vector<DracoAttributeData> attributes;
};

std::string dracoDecode(const uint8_t* data, size_t size, const std::vector<int>& attributeIds, size_t maxPoints, size_t maxIndices, DracoMeshData& out);

}

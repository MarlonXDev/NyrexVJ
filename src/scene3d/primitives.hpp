#pragma once

#include "scene3d/scene_model.hpp"

namespace nvj {

enum class PrimitiveKind { Sphere, Cube, Cylinder, Cone, Plane, Torus, TorusKnot, Model, Graph };

constexpr int kPrimitiveKindCount = 9;

const char* primitiveKey(PrimitiveKind kind);
const char* primitiveLabel(PrimitiveKind kind);
bool primitiveFromKey(const std::string& key, PrimitiveKind& kind);
SceneMesh primitiveMesh(PrimitiveKind kind);

}

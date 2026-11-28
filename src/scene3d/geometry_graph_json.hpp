#pragma once

#include "scene3d/geometry_graph.hpp"

#include <nlohmann/json.hpp>

namespace nvj {

void geometryGraphToJson(const GeometryGraph& graph, nlohmann::json& out);
bool geometryGraphFromJson(const nlohmann::json& in, GeometryGraph& graph, std::vector<std::string>* notes);

}

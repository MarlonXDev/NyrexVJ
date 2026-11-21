#pragma once

#include "core/node_graph.hpp"

#include <string>
#include <vector>

namespace nvj {

struct GraphError {
    int node = -1;
    std::string message;
};

struct CompiledGraph {
    bool ok = false;
    std::string glsl;
    std::vector<GraphError> errors;
    std::vector<int> usedNodes;
    bool usesPrevious = false;
    bool usesMacros = false;
    int textureReads = 0;
    unsigned long long hash = 0;

    std::string errorFor(int node) const;
    std::string firstError() const;
};

CompiledGraph compileGraph(const GraphDocument& graph);
unsigned long long hashText(const std::string& text);
std::string formatShaderFloat(float value);

}

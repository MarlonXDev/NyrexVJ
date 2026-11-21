#include "core/node_compile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <map>
#include <sstream>

namespace nvj {

std::string formatShaderFloat(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.precision(9);
    stream << value;
    std::string text = stream.str();
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    if (value < 0.0f) {
        text = "(" + text + ")";
    }
    return text;
}

unsigned long long hashText(const std::string& text) {
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string CompiledGraph::errorFor(int node) const {
    for (const GraphError& error : errors) {
        if (error.node == node) {
            return error.message;
        }
    }
    return std::string();
}

std::string CompiledGraph::firstError() const {
    return errors.empty() ? std::string() : errors.front().message;
}

namespace {

struct Source {
    int node = 0;
    int port = 0;
};

struct NodeInfo {
    const GraphNode* node = nullptr;
    const NodeSpec* spec = nullptr;
    PortType generic = PortType::Float;
    std::vector<PortType> outputTypes;
};

std::string varName(int node, int port) {
    return "n" + std::to_string(node) + "_" + std::to_string(port);
}

bool convertible(PortType from, PortType to) {
    if (from == to || from == PortType::Float) {
        return true;
    }
    return (from == PortType::Vec3 && to == PortType::Vec4) || (from == PortType::Vec4 && to == PortType::Vec3);
}

std::string convertExpression(const std::string& expression, PortType from, PortType to) {
    if (from == to) {
        return expression;
    }
    if (from == PortType::Float) {
        return std::string(portTypeGlsl(to)) + "(" + expression + ")";
    }
    if (from == PortType::Vec3 && to == PortType::Vec4) {
        return "vec4(" + expression + ", 1.0)";
    }
    return "(" + expression + ").rgb";
}

std::string substitute(const std::string& templ, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs, const std::vector<float>& values, size_t inputCount, PortType generic, int id) {
    std::string result;
    result.reserve(templ.size() * 2);
    for (size_t i = 0; i < templ.size(); ++i) {
        if (templ[i] != '{') {
            result.push_back(templ[i]);
            continue;
        }
        const size_t close = templ.find('}', i);
        if (close == std::string::npos || close - i > 6) {
            result.push_back(templ[i]);
            continue;
        }
        const std::string token = templ.substr(i + 1, close - i - 1);
        std::string replacement;
        bool known = false;
        if (token == "T") {
            replacement = portTypeGlsl(generic);
            known = true;
        } else if (token == "id") {
            replacement = std::to_string(id);
            known = true;
        } else if (token.size() >= 2 && std::isdigit(static_cast<unsigned char>(token[1]))) {
            const size_t index = static_cast<size_t>(token[1] - '0');
            if (token[0] == 'i' && token.size() == 2 && index < inputs.size()) {
                replacement = inputs[index];
                known = true;
            } else if (token[0] == 'o' && token.size() == 2 && index < outputs.size()) {
                replacement = outputs[index];
                known = true;
            } else if (token[0] == 'c' && (token.size() == 2 || (token.size() == 3 && token[2] == 'i')) && inputCount + index < values.size()) {
                const float v = values[inputCount + index];
                replacement = token.size() == 3 ? std::to_string(static_cast<int>(std::lround(v))) : formatShaderFloat(v);
                known = true;
            }
        }
        if (!known) {
            result.push_back(templ[i]);
            continue;
        }
        result += replacement;
        i = close;
    }
    return result;
}

}

CompiledGraph compileGraph(const GraphDocument& graph) {
    CompiledGraph result;
    const auto fail = [&result](int node, const std::string& message) {
        result.errors.push_back({node, message});
    };

    if (graph.nodes.empty()) {
        fail(-1, "The effect is empty");
        return result;
    }
    if (graph.nodes.size() > kMaxGraphNodes) {
        fail(-1, "The effect has more than " + std::to_string(kMaxGraphNodes) + " nodes");
        return result;
    }

    std::map<int, NodeInfo> info;
    int outputNode = -1;
    int outputs = 0;
    for (const GraphNode& node : graph.nodes) {
        const NodeSpec* spec = findNodeSpec(node.type);
        if (!spec) {
            fail(node.id, "Unknown node type '" + node.type.substr(0, 40) + "'");
            continue;
        }
        if (info.count(node.id)) {
            fail(node.id, "The node number is used twice");
            continue;
        }
        NodeInfo item;
        item.node = &node;
        item.spec = spec;
        info[node.id] = item;
        if (spec->isOutput) {
            outputNode = node.id;
            ++outputs;
        }
    }
    if (!result.errors.empty()) {
        return result;
    }
    if (outputs == 0) {
        fail(-1, "The effect needs an Output node");
        return result;
    }
    if (outputs > 1) {
        fail(outputNode, "The effect has more than one Output node");
        return result;
    }

    std::map<std::pair<int, int>, Source> inputSource;
    for (const GraphLink& link : graph.links) {
        const auto from = info.find(link.fromNode);
        const auto to = info.find(link.toNode);
        if (from == info.end() || to == info.end()) {
            fail(-1, "A connection points to a node that does not exist");
            return result;
        }
        if (link.fromPort < 0 || static_cast<size_t>(link.fromPort) >= from->second.spec->outputs.size() || link.toPort < 0 || static_cast<size_t>(link.toPort) >= to->second.spec->inputs.size()) {
            fail(link.toNode, "A connection points to a port that does not exist");
            return result;
        }
        if (inputSource.count({link.toNode, link.toPort})) {
            fail(link.toNode, "An input has two connections");
            return result;
        }
        inputSource[{link.toNode, link.toPort}] = {link.fromNode, link.fromPort};
    }

    std::vector<int> order;
    std::map<int, int> state;
    bool loop = false;
    std::vector<std::pair<int, size_t>> stack;
    stack.push_back({outputNode, 0});
    state[outputNode] = 1;
    while (!stack.empty() && !loop) {
        auto& frame = stack.back();
        const NodeInfo& current = info[frame.first];
        if (frame.second < current.spec->inputs.size()) {
            const size_t port = frame.second++;
            const auto source = inputSource.find({frame.first, static_cast<int>(port)});
            if (source == inputSource.end()) {
                continue;
            }
            const int next = source->second.node;
            const int mark = state[next];
            if (mark == 1) {
                fail(next, "This node is part of a loop");
                loop = true;
            } else if (mark == 0) {
                state[next] = 1;
                stack.push_back({next, 0});
            }
        } else {
            state[frame.first] = 2;
            order.push_back(frame.first);
            stack.pop_back();
        }
    }
    if (loop) {
        return result;
    }

    std::map<int, std::vector<std::string>> expressions;
    std::string body;
    std::vector<std::string> helpers;
    int reads = 0;

    for (int id : order) {
        NodeInfo& item = info[id];
        const NodeSpec& spec = *item.spec;
        std::vector<float> values = item.node->values;
        fillNodeValues(spec, values);

        PortType generic = PortType::Float;
        bool conflict = false;
        for (size_t k = 0; k < spec.inputs.size(); ++k) {
            if (!spec.inputs[k].generic) {
                continue;
            }
            const auto source = inputSource.find({id, static_cast<int>(k)});
            if (source == inputSource.end()) {
                continue;
            }
            const PortType from = info[source->second.node].outputTypes[static_cast<size_t>(source->second.port)];
            if (from == PortType::Float) {
                continue;
            }
            if (generic == PortType::Float) {
                generic = from;
            } else if (generic != from && !(convertible(from, generic) && from != PortType::Vec2 && generic != PortType::Vec2)) {
                fail(id, std::string(portTypeName(generic)) + " and " + portTypeName(from) + " cannot be combined");
                conflict = true;
            }
        }
        if (conflict) {
            return result;
        }
        item.generic = generic;
        item.outputTypes.clear();
        for (const PortSpec& port : spec.outputs) {
            item.outputTypes.push_back(port.generic ? generic : port.type);
        }

        std::vector<std::string> inputs;
        for (size_t k = 0; k < spec.inputs.size(); ++k) {
            const PortSpec& port = spec.inputs[k];
            const PortType wanted = port.generic ? generic : port.type;
            const auto source = inputSource.find({id, static_cast<int>(k)});
            if (source != inputSource.end()) {
                const PortType from = info[source->second.node].outputTypes[static_cast<size_t>(source->second.port)];
                if (!convertible(from, wanted)) {
                    fail(id, std::string(portTypeName(from)) + " cannot be connected to " + portTypeName(wanted) + " (" + port.name + ")");
                    return result;
                }
                inputs.push_back(convertExpression(varName(source->second.node, source->second.port), from, wanted));
            } else if (port.expression) {
                inputs.push_back(port.expression);
            } else {
                inputs.push_back(convertExpression(formatShaderFloat(values[k]), PortType::Float, wanted));
            }
        }

        std::vector<std::string> names;
        std::string declarations;
        for (size_t k = 0; k < spec.outputs.size(); ++k) {
            names.push_back(varName(id, static_cast<int>(k)));
            declarations += "    " + std::string(portTypeGlsl(item.outputTypes[k])) + " " + names.back() + ";\n";
        }
        std::string code = substitute(spec.code, inputs, names, values, spec.inputs.size(), generic, id);
        std::string indented = "    ";
        for (char c : code) {
            indented.push_back(c);
            if (c == '\n') {
                indented += "    ";
            }
        }
        body += declarations + indented + "\n";

        reads += spec.reads;
        if (spec.readsConst >= 0) {
            reads += static_cast<int>(std::lround(values[spec.inputs.size() + static_cast<size_t>(spec.readsConst)]));
        }
        result.usesPrevious = result.usesPrevious || spec.usesPrevious;
        result.usesMacros = result.usesMacros || spec.usesMacros;
        std::string list = spec.helpers;
        size_t start = 0;
        while (start < list.size()) {
            size_t comma = list.find(',', start);
            if (comma == std::string::npos) {
                comma = list.size();
            }
            const std::string name = list.substr(start, comma - start);
            if (!name.empty() && std::find(helpers.begin(), helpers.end(), name) == helpers.end()) {
                helpers.push_back(name);
            }
            start = comma + 1;
        }
        result.usedNodes.push_back(id);
    }

    result.textureReads = reads;
    if (reads > kMaxTextureReads) {
        fail(-1, "The effect reads the picture " + std::to_string(reads) + " times, the limit is " + std::to_string(kMaxTextureReads));
        return result;
    }

    std::string glsl;
    if (result.usesMacros) {
        glsl += "uniform float uMacros[8];\n\n";
    }
    for (const std::string& name : nodeHelperOrder()) {
        if (std::find(helpers.begin(), helpers.end(), name) != helpers.end()) {
            glsl += nodeHelperSource(name);
            glsl += "\n";
        }
    }
    glsl += "vec4 effect(vec2 uv, vec4 p) {\n" + body + "}\n";
    result.glsl = glsl;
    result.hash = hashText(glsl);
    result.ok = true;
    return result;
}

}

#include "core/node_graph.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace nvj {

using json = nlohmann::json;

const char* portTypeName(PortType type) {
    switch (type) {
    case PortType::Float:
        return "Number";
    case PortType::Vec2:
        return "Vector 2";
    case PortType::Vec3:
        return "Colour";
    case PortType::Vec4:
        return "Colour with alpha";
    }
    return "Number";
}

const char* portTypeGlsl(PortType type) {
    switch (type) {
    case PortType::Float:
        return "float";
    case PortType::Vec2:
        return "vec2";
    case PortType::Vec3:
        return "vec3";
    case PortType::Vec4:
        return "vec4";
    }
    return "float";
}

int portComponents(PortType type) {
    return static_cast<int>(type);
}

float clampNodeValue(const NodeSpec& spec, size_t index, float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    if (index < spec.inputs.size()) {
        const PortSpec& port = spec.inputs[index];
        return std::clamp(value, port.minimum, port.maximum);
    }
    const size_t c = index - spec.inputs.size();
    if (c >= spec.consts.size()) {
        return value;
    }
    const ConstSpec& cs = spec.consts[c];
    value = std::clamp(value, cs.minimum, cs.maximum);
    if (cs.integer) {
        value = std::round(value);
    }
    return value;
}

void fillNodeValues(const NodeSpec& spec, std::vector<float>& values) {
    const size_t before = values.size();
    values.resize(spec.valueCount());
    for (size_t i = before; i < values.size(); ++i) {
        values[i] = i < spec.inputs.size() ? spec.inputs[i].value : spec.consts[i - spec.inputs.size()].value;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = clampNodeValue(spec, i, values[i]);
    }
}

const GraphNode* GraphDocument::find(int id) const {
    for (const GraphNode& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

GraphNode* GraphDocument::find(int id) {
    for (GraphNode& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

int GraphDocument::addNode(const std::string& type, float x, float y) {
    const NodeSpec* spec = findNodeSpec(type);
    if (!spec || nodes.size() >= kMaxGraphNodes) {
        return -1;
    }
    if (spec->isOutput) {
        for (const GraphNode& node : nodes) {
            if (node.type == type) {
                return -1;
            }
        }
    }
    GraphNode node;
    node.id = nextId++;
    node.type = type;
    node.x = std::isfinite(x) ? x : 0.0f;
    node.y = std::isfinite(y) ? y : 0.0f;
    fillNodeValues(*spec, node.values);
    nodes.push_back(std::move(node));
    return nodes.back().id;
}

bool GraphDocument::removeNode(int id) {
    const auto it = std::find_if(nodes.begin(), nodes.end(), [id](const GraphNode& n) { return n.id == id; });
    if (it == nodes.end()) {
        return false;
    }
    nodes.erase(it);
    links.erase(std::remove_if(links.begin(), links.end(), [id](const GraphLink& l) { return l.fromNode == id || l.toNode == id; }), links.end());
    return true;
}

const GraphLink* GraphDocument::linkInto(int toNode, int toPort) const {
    for (const GraphLink& link : links) {
        if (link.toNode == toNode && link.toPort == toPort) {
            return &link;
        }
    }
    return nullptr;
}

bool GraphDocument::reaches(int fromNode, int toNode) const {
    if (fromNode == toNode) {
        return true;
    }
    std::vector<int> stack{fromNode};
    std::vector<int> seen;
    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), current) != seen.end()) {
            continue;
        }
        seen.push_back(current);
        for (const GraphLink& link : links) {
            if (link.fromNode != current) {
                continue;
            }
            if (link.toNode == toNode) {
                return true;
            }
            stack.push_back(link.toNode);
        }
    }
    return false;
}

namespace {

bool convertible(PortType from, PortType to) {
    if (from == to) {
        return true;
    }
    if (from == PortType::Float) {
        return true;
    }
    return (from == PortType::Vec3 && to == PortType::Vec4) || (from == PortType::Vec4 && to == PortType::Vec3);
}

}

std::string GraphDocument::connect(int fromNode, int fromPort, int toNode, int toPort) {
    const GraphNode* a = find(fromNode);
    const GraphNode* b = find(toNode);
    if (!a || !b) {
        return "The node does not exist";
    }
    if (fromNode == toNode) {
        return "A node cannot be connected to itself";
    }
    const NodeSpec* sa = findNodeSpec(a->type);
    const NodeSpec* sb = findNodeSpec(b->type);
    if (!sa || !sb) {
        return "The node type is unknown";
    }
    if (fromPort < 0 || static_cast<size_t>(fromPort) >= sa->outputs.size()) {
        return "The output does not exist";
    }
    if (toPort < 0 || static_cast<size_t>(toPort) >= sb->inputs.size()) {
        return "The input does not exist";
    }
    const PortSpec& source = sa->outputs[static_cast<size_t>(fromPort)];
    const PortSpec& target = sb->inputs[static_cast<size_t>(toPort)];
    if (!source.generic && !target.generic && !convertible(source.type, target.type)) {
        return std::string(portTypeName(source.type)) + " cannot be connected to " + portTypeName(target.type);
    }
    if (reaches(toNode, fromNode)) {
        return "This connection would make a loop";
    }
    const bool replaces = linkInto(toNode, toPort) != nullptr;
    if (!replaces && links.size() >= kMaxGraphLinks) {
        return "The graph has too many connections";
    }
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.toNode == toNode && l.toPort == toPort; }), links.end());
    links.push_back({fromNode, fromPort, toNode, toPort});
    return std::string();
}

bool GraphDocument::disconnect(int toNode, int toPort) {
    const size_t before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(), [&](const GraphLink& l) { return l.toNode == toNode && l.toPort == toPort; }), links.end());
    return links.size() != before;
}

float GraphDocument::value(int nodeId, size_t index) const {
    const GraphNode* node = find(nodeId);
    if (!node || index >= node->values.size()) {
        return 0.0f;
    }
    return node->values[index];
}

void GraphDocument::setValue(int nodeId, size_t index, float value) {
    GraphNode* node = find(nodeId);
    if (!node) {
        return;
    }
    const NodeSpec* spec = findNodeSpec(node->type);
    if (!spec || index >= spec->valueCount()) {
        return;
    }
    fillNodeValues(*spec, node->values);
    node->values[index] = clampNodeValue(*spec, index, value);
}

GraphDocument newGraph(const std::string& name) {
    GraphDocument graph;
    graph.name = name;
    const int picture = graph.addNode("picture", 0.0f, 0.0f);
    const int output = graph.addNode("output", 320.0f, 0.0f);
    graph.connect(picture, 0, output, 0);
    return graph;
}

int nodeRowCount(const NodeSpec& spec) {
    return static_cast<int>(spec.inputs.size() + spec.consts.size() + spec.outputs.size());
}

float nodeHeight(const NodeSpec& spec) {
    return kNodeTitleHeight + static_cast<float>(nodeRowCount(spec)) * kNodeRowHeight + kNodePadding;
}

void arrangeGraph(GraphDocument& graph) {
    const size_t count = graph.nodes.size();
    std::vector<int> depth(count, 0);
    std::vector<int> index(count);
    for (size_t i = 0; i < count; ++i) {
        index[i] = graph.nodes[i].id;
    }
    const auto slot = [&](int id) -> int {
        for (size_t i = 0; i < count; ++i) {
            if (index[i] == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    for (size_t pass = 0; pass < count + 1; ++pass) {
        bool changed = false;
        for (const GraphLink& link : graph.links) {
            const int from = slot(link.fromNode);
            const int to = slot(link.toNode);
            if (from < 0 || to < 0) {
                continue;
            }
            if (depth[static_cast<size_t>(to)] < depth[static_cast<size_t>(from)] + 1) {
                depth[static_cast<size_t>(to)] = depth[static_cast<size_t>(from)] + 1;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    int columns = 0;
    for (int d : depth) {
        columns = std::max(columns, d + 1);
    }
    for (int column = 0; column < columns; ++column) {
        std::vector<size_t> members;
        for (size_t i = 0; i < count; ++i) {
            if (depth[i] == column) {
                members.push_back(i);
            }
        }
        std::stable_sort(members.begin(), members.end(), [&](size_t a, size_t b) { return graph.nodes[a].y < graph.nodes[b].y; });
        float y = 0.0f;
        for (size_t i : members) {
            GraphNode& node = graph.nodes[i];
            const NodeSpec* spec = findNodeSpec(node.type);
            node.x = static_cast<float>(column) * (kNodeWidth + kNodeGap);
            node.y = y;
            y += (spec ? nodeHeight(*spec) : kNodeTitleHeight) + 16.0f;
        }
    }
}

namespace {

std::string cleanLabel(const std::string& text) {
    std::string result;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            continue;
        }
        result.push_back(c);
    }
    if (result.size() > kMaxGraphLabel) {
        size_t cut = kMaxGraphLabel;
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        result.resize(cut);
    }
    return result;
}

}

std::string graphToJson(const GraphDocument& graph) {
    json doc;
    doc["format"] = "nyrexvj-effect";
    doc["version"] = kGraphVersion;
    doc["name"] = graph.name;
    json controls = json::array();
    for (const GraphControl& control : graph.controls) {
        controls.push_back(json{{"label", control.label}, {"value", control.value}});
    }
    doc["controls"] = controls;
    json nodes = json::array();
    for (const GraphNode& node : graph.nodes) {
        json item;
        item["id"] = node.id;
        item["type"] = node.type;
        item["x"] = node.x;
        item["y"] = node.y;
        json values = json::array();
        for (float v : node.values) {
            values.push_back(v);
        }
        item["values"] = values;
        nodes.push_back(item);
    }
    doc["nodes"] = nodes;
    json links = json::array();
    for (const GraphLink& link : graph.links) {
        links.push_back(json{{"from", link.fromNode}, {"fromPort", link.fromPort}, {"to", link.toNode}, {"toPort", link.toPort}});
    }
    doc["links"] = links;
    return doc.dump(2);
}

namespace {

bool readInt(const json& item, const char* key, int& out) {
    const auto it = item.find(key);
    if (it == item.end() || !it->is_number_integer()) {
        return false;
    }
    const long long v = it->get<long long>();
    if (v < -1000000 || v > 1000000) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

}

bool graphFromJson(const std::string& text, GraphDocument& graph, std::string& error) {
    error.clear();
    if (text.size() > kMaxGraphFileBytes) {
        error = "The effect file is larger than 256 KiB";
        return false;
    }
    const json doc = json::parse(text, nullptr, false);
    if (!doc.is_object()) {
        error = "The file is not an effect file";
        return false;
    }
    const auto format = doc.find("format");
    if (format == doc.end() || !format->is_string() || format->get<std::string>() != "nyrexvj-effect") {
        error = "The file is not an effect file";
        return false;
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer()) {
        error = "The effect file has no version";
        return false;
    }
    if (version->get<int>() > kGraphVersion) {
        error = "The effect file was made by a newer NyrexVJ";
        return false;
    }
    if (version->get<int>() < 1) {
        error = "The effect file has an unknown version";
        return false;
    }

    GraphDocument result;
    if (doc.contains("name") && doc["name"].is_string()) {
        result.name = cleanLabel(doc["name"].get<std::string>());
    }
    if (doc.contains("controls") && doc["controls"].is_array()) {
        const json& controls = doc["controls"];
        for (size_t i = 0; i < controls.size() && i < static_cast<size_t>(kGraphControls); ++i) {
            const json& item = controls[i];
            if (!item.is_object()) {
                continue;
            }
            GraphControl& control = result.controls[i];
            if (item.contains("label") && item["label"].is_string()) {
                control.label = cleanLabel(item["label"].get<std::string>());
            }
            if (item.contains("value") && item["value"].is_number()) {
                const double v = item["value"].get<double>();
                control.value = std::isfinite(v) ? std::clamp(static_cast<float>(v), 0.0f, 1.0f) : 0.5f;
            }
        }
    }

    const auto nodes = doc.find("nodes");
    if (nodes == doc.end() || !nodes->is_array()) {
        error = "The effect file has no nodes";
        return false;
    }
    if (nodes->size() > kMaxGraphNodes) {
        error = "The effect has more than " + std::to_string(kMaxGraphNodes) + " nodes";
        return false;
    }
    int outputs = 0;
    for (const json& item : *nodes) {
        if (!item.is_object()) {
            error = "A node is damaged";
            return false;
        }
        GraphNode node;
        if (!readInt(item, "id", node.id) || node.id <= 0) {
            error = "A node has no valid number";
            return false;
        }
        if (result.find(node.id)) {
            error = "Node number " + std::to_string(node.id) + " is used twice";
            return false;
        }
        const auto type = item.find("type");
        if (type == item.end() || !type->is_string()) {
            error = "A node has no type";
            return false;
        }
        node.type = type->get<std::string>();
        const NodeSpec* spec = findNodeSpec(node.type);
        if (!spec) {
            error = "Unknown node type '" + node.type.substr(0, 40) + "'";
            return false;
        }
        if (spec->isOutput && ++outputs > 1) {
            error = "The effect has more than one Output node";
            return false;
        }
        const auto x = item.find("x");
        const auto y = item.find("y");
        node.x = x != item.end() && x->is_number() && std::isfinite(x->get<double>()) ? std::clamp(x->get<float>(), -100000.0f, 100000.0f) : 0.0f;
        node.y = y != item.end() && y->is_number() && std::isfinite(y->get<double>()) ? std::clamp(y->get<float>(), -100000.0f, 100000.0f) : 0.0f;
        const auto values = item.find("values");
        if (values != item.end() && values->is_array()) {
            for (size_t i = 0; i < values->size() && i < spec->valueCount(); ++i) {
                const json& v = (*values)[i];
                node.values.push_back(v.is_number() ? static_cast<float>(v.get<double>()) : 0.0f);
            }
        }
        const size_t given = node.values.size();
        fillNodeValues(*spec, node.values);
        for (size_t i = 0; i < given; ++i) {
            node.values[i] = clampNodeValue(*spec, i, node.values[i]);
        }
        result.nextId = std::max(result.nextId, node.id + 1);
        result.nodes.push_back(std::move(node));
    }

    const auto links = doc.find("links");
    if (links != doc.end()) {
        if (!links->is_array()) {
            error = "The connections are damaged";
            return false;
        }
        if (links->size() > kMaxGraphLinks) {
            error = "The effect has more than " + std::to_string(kMaxGraphLinks) + " connections";
            return false;
        }
        for (const json& item : *links) {
            GraphLink link;
            if (!item.is_object() || !readInt(item, "from", link.fromNode) || !readInt(item, "fromPort", link.fromPort) || !readInt(item, "to", link.toNode) || !readInt(item, "toPort", link.toPort)) {
                error = "A connection is damaged";
                return false;
            }
            if (result.linkInto(link.toNode, link.toPort)) {
                error = "An input has two connections";
                return false;
            }
            const std::string problem = result.connect(link.fromNode, link.fromPort, link.toNode, link.toPort);
            if (!problem.empty()) {
                error = problem;
                return false;
            }
        }
    }
    graph = std::move(result);
    return true;
}

bool isGraphFileName(const std::string& name) {
    if (name.size() < 7) {
        return false;
    }
    std::string tail = name.substr(name.size() - 6);
    std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".nyxfx";
}

std::string graphFileName(const std::string& stem) {
    return stem + ".nyxfx";
}

std::string graphEffectName(const std::string& stem) {
    return "Node: " + stem;
}

bool graphStemFromEffectName(const std::string& name, std::string& stem) {
    const std::string prefix = "Node: ";
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    stem = name.substr(prefix.size());
    return true;
}

std::string graphDirectory() {
    return (std::filesystem::path(userDataDir()) / "nodes").string();
}

namespace {

int portIndex(const std::vector<PortSpec>& ports, const char* name) {
    for (size_t i = 0; i < ports.size(); ++i) {
        if (std::string(ports[i].name) == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void wire(GraphDocument& graph, int from, const char* fromPort, int to, const char* toPort) {
    const GraphNode* a = graph.find(from);
    const GraphNode* b = graph.find(to);
    if (!a || !b) {
        return;
    }
    const NodeSpec* sa = findNodeSpec(a->type);
    const NodeSpec* sb = findNodeSpec(b->type);
    const int p = portIndex(sa->outputs, fromPort);
    const int q = portIndex(sb->inputs, toPort);
    if (p >= 0 && q >= 0) {
        graph.connect(from, p, to, q);
    }
}

void setPort(GraphDocument& graph, int node, const char* port, float value) {
    const GraphNode* n = graph.find(node);
    if (!n) {
        return;
    }
    const NodeSpec* spec = findNodeSpec(n->type);
    const int index = portIndex(spec->inputs, port);
    if (index >= 0) {
        graph.setValue(node, static_cast<size_t>(index), value);
    }
}

void setConst(GraphDocument& graph, int node, size_t index, float value) {
    const GraphNode* n = graph.find(node);
    if (!n) {
        return;
    }
    const NodeSpec* spec = findNodeSpec(n->type);
    graph.setValue(node, spec->inputs.size() + index, value);
}

GraphDocument buildWavyRainbow() {
    GraphDocument g;
    g.name = "Wavy Rainbow";
    g.controls[0] = {"Wobble", 0.4f};
    g.controls[1] = {"Density", 0.3f};
    g.controls[2] = {"Colour", 0.5f};
    const int uv = g.addNode("uv", 0, 0);
    const int c1 = g.addNode("control", 0, 120);
    const int c2 = g.addNode("control", 0, 240);
    const int c3 = g.addNode("control", 0, 360);
    setConst(g, c1, 0, 1);
    setConst(g, c2, 0, 2);
    setConst(g, c3, 0, 3);
    const int amount = g.addNode("remap", 200, 120);
    setPort(g, amount, "To high", 0.08f);
    const int density = g.addNode("remap", 200, 240);
    setPort(g, density, "To high", 32.0f);
    const int wave = g.addNode("wave", 420, 60);
    const int sample = g.addNode("sample", 640, 60);
    const int lfo = g.addNode("lfo", 200, 360);
    const int scale = g.addNode("multiply", 420, 360);
    const int hue = g.addNode("hue", 840, 60);
    const int alpha = g.addNode("makecoloura", 1040, 60);
    const int out = g.addNode("output", 1240, 60);
    wire(g, c1, "Value", amount, "Value");
    wire(g, c2, "Value", density, "Value");
    wire(g, uv, "UV", wave, "UV");
    wire(g, amount, "Result", wave, "Amount");
    wire(g, density, "Result", wave, "Frequency");
    wire(g, wave, "UV", sample, "UV");
    wire(g, sample, "Colour", hue, "Colour");
    wire(g, lfo, "Value", scale, "A");
    wire(g, c3, "Value", scale, "B");
    wire(g, scale, "Result", hue, "Amount");
    wire(g, hue, "Colour", alpha, "Colour");
    wire(g, alpha, "Colour", out, "Colour");
    return g;
}

GraphDocument buildMirrorSwirl() {
    GraphDocument g;
    g.name = "Mirror Swirl";
    g.controls[0] = {"Twist", 0.7f};
    g.controls[1] = {"Radius", 0.5f};
    const int uv = g.addNode("uv", 0, 0);
    const int mirror = g.addNode("mirror", 180, 0);
    const int c1 = g.addNode("control", 0, 160);
    const int c2 = g.addNode("control", 0, 280);
    setConst(g, c1, 0, 1);
    setConst(g, c2, 0, 2);
    const int twist = g.addNode("remap", 200, 160);
    setPort(g, twist, "To low", -1.0f);
    setPort(g, twist, "To high", 1.0f);
    const int radius = g.addNode("remap", 200, 280);
    setPort(g, radius, "To low", 0.05f);
    setPort(g, radius, "To high", 1.0f);
    const int swirl = g.addNode("swirl", 420, 0);
    const int sample = g.addNode("sample", 640, 0);
    const int out = g.addNode("output", 840, 0);
    wire(g, uv, "UV", mirror, "UV");
    wire(g, mirror, "UV", swirl, "UV");
    wire(g, c1, "Value", twist, "Value");
    wire(g, c2, "Value", radius, "Value");
    wire(g, twist, "Result", swirl, "Amount");
    wire(g, radius, "Result", swirl, "Radius");
    wire(g, swirl, "UV", sample, "UV");
    wire(g, sample, "Colour", out, "Colour");
    return g;
}

GraphDocument buildKickPulse() {
    GraphDocument g;
    g.name = "Kick Pulse";
    g.controls[0] = {"Punch", 0.5f};
    const int audio = g.addNode("audio", 0, 0);
    const int c1 = g.addNode("control", 0, 200);
    setConst(g, c1, 0, 1);
    const int product = g.addNode("multiply", 220, 80);
    const int sum = g.addNode("add", 420, 80);
    setPort(g, sum, "B", 1.0f);
    const int uv = g.addNode("uv", 420, 240);
    const int zoom = g.addNode("zoom", 620, 120);
    const int sample = g.addNode("sample", 820, 120);
    const int out = g.addNode("output", 1020, 120);
    wire(g, audio, "Kick", product, "A");
    wire(g, c1, "Value", product, "B");
    wire(g, product, "Result", sum, "A");
    wire(g, uv, "UV", zoom, "UV");
    wire(g, sum, "Result", zoom, "Amount");
    wire(g, zoom, "UV", sample, "UV");
    wire(g, sample, "Colour", out, "Colour");
    return g;
}

GraphDocument buildTrails() {
    GraphDocument g;
    g.name = "Trails";
    g.controls[0] = {"Length", 0.7f};
    const int previous = g.addNode("previous", 0, 0);
    const int picture = g.addNode("picture", 0, 160);
    const int c1 = g.addNode("control", 0, 300);
    setConst(g, c1, 0, 1);
    const int decay = g.addNode("remap", 200, 300);
    setPort(g, decay, "To low", 0.7f);
    setPort(g, decay, "To high", 0.98f);
    const int fade = g.addNode("multiply", 420, 40);
    const int keep = g.addNode("max", 620, 100);
    const int out = g.addNode("output", 820, 100);
    wire(g, c1, "Value", decay, "Value");
    wire(g, previous, "Colour", fade, "A");
    wire(g, decay, "Result", fade, "B");
    wire(g, fade, "Result", keep, "A");
    wire(g, picture, "Colour", keep, "B");
    wire(g, keep, "Result", out, "Colour");
    return g;
}

}

std::vector<GraphDocument> catalogueGraphs(const NodeSpec& spec) {
    std::vector<GraphDocument> graphs;
    if (spec.isOutput) {
        graphs.push_back(newGraph(spec.id));
        return graphs;
    }
    bool hasGeneric = false;
    for (const PortSpec& port : spec.inputs) {
        hasGeneric = hasGeneric || port.generic;
    }
    const std::vector<PortType> variants = hasGeneric ? std::vector<PortType>{PortType::Float, PortType::Vec2, PortType::Vec3, PortType::Vec4} : std::vector<PortType>{PortType::Float};
    for (PortType type : variants) {
        GraphDocument g;
        g.name = std::string(spec.id) + " " + portTypeGlsl(type);
        const int node = g.addNode(spec.id, 240.0f, 0.0f);
        const char* sourceType = type == PortType::Float ? "number" : type == PortType::Vec2 ? "vector2" : type == PortType::Vec3 ? "colour" : "picture";
        for (size_t k = 0; k < spec.inputs.size(); ++k) {
            if (!spec.inputs[k].generic) {
                continue;
            }
            const int source = g.addNode(sourceType, 0.0f, static_cast<float>(k) * 120.0f);
            g.connect(source, 0, node, static_cast<int>(k));
        }
        const PortType produced = spec.outputs[0].generic ? type : spec.outputs[0].type;
        const int output = g.addNode("output", 640.0f, 0.0f);
        if (produced == PortType::Vec2) {
            const int sample = g.addNode("sample", 440.0f, 0.0f);
            g.connect(node, 0, sample, 0);
            g.connect(sample, 0, output, 0);
        } else {
            g.connect(node, 0, output, 0);
        }
        graphs.push_back(std::move(g));
    }
    return graphs;
}

const std::vector<ExampleGraph>& exampleGraphs() {
    static const std::vector<ExampleGraph> examples = {
        {"Wavy Rainbow", buildWavyRainbow},
        {"Mirror Swirl", buildMirrorSwirl},
        {"Kick Pulse", buildKickPulse},
        {"Trails", buildTrails},
    };
    return examples;
}

}

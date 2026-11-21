#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace nvj {

enum class PortType : int {
    Float = 1,
    Vec2 = 2,
    Vec3 = 3,
    Vec4 = 4
};

constexpr size_t kMaxGraphNodes = 96;
constexpr size_t kMaxGraphLinks = 256;
constexpr int kGraphControls = 4;
constexpr int kMaxTextureReads = 64;
constexpr unsigned long long kMaxGraphFileBytes = 256ull * 1024ull;
constexpr size_t kMaxNodeEffects = 64;
constexpr size_t kMaxGraphLabel = 24;
constexpr int kGraphVersion = 1;
constexpr float kNodeWidth = 176.0f;
constexpr float kNodeTitleHeight = 24.0f;
constexpr float kNodeRowHeight = 20.0f;
constexpr float kNodePadding = 6.0f;
constexpr float kNodeGap = 56.0f;

const char* portTypeName(PortType type);
const char* portTypeGlsl(PortType type);
int portComponents(PortType type);

struct PortSpec {
    const char* name = "";
    PortType type = PortType::Float;
    bool generic = false;
    const char* expression = nullptr;
    float value = 0.0f;
    float minimum = -16.0f;
    float maximum = 16.0f;
};

struct ConstSpec {
    const char* name = "";
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool integer = false;
    const char* choices = nullptr;
};

struct NodeSpec {
    const char* id = "";
    const char* title = "";
    const char* category = "";
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<ConstSpec> consts;
    const char* code = "";
    const char* helpers = "";
    int reads = 0;
    int readsConst = -1;
    bool usesPrevious = false;
    bool usesMacros = false;
    bool isOutput = false;

    size_t valueCount() const { return inputs.size() + consts.size(); }
};

const std::vector<NodeSpec>& nodeCatalogue();
const NodeSpec* findNodeSpec(const std::string& id);
const char* nodeHelperSource(const std::string& name);
const std::vector<std::string>& nodeHelperOrder();
std::vector<std::string> nodeCategories();

struct GraphNode {
    int id = 0;
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
    std::vector<float> values;
    bool operator==(const GraphNode&) const = default;
};

struct GraphLink {
    int fromNode = 0;
    int fromPort = 0;
    int toNode = 0;
    int toPort = 0;
    bool operator==(const GraphLink&) const = default;
};

struct GraphControl {
    std::string label;
    float value = 0.5f;
    bool operator==(const GraphControl&) const = default;
};

struct GraphDocument {
    std::string name;
    std::array<GraphControl, kGraphControls> controls;
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    int nextId = 1;

    bool operator==(const GraphDocument&) const = default;

    const GraphNode* find(int id) const;
    GraphNode* find(int id);
    int addNode(const std::string& type, float x, float y);
    bool removeNode(int id);
    std::string connect(int fromNode, int fromPort, int toNode, int toPort);
    bool disconnect(int toNode, int toPort);
    const GraphLink* linkInto(int toNode, int toPort) const;
    bool reaches(int fromNode, int toNode) const;
    float value(int nodeId, size_t index) const;
    void setValue(int nodeId, size_t index, float value);
};

float clampNodeValue(const NodeSpec& spec, size_t index, float value);
void fillNodeValues(const NodeSpec& spec, std::vector<float>& values);

GraphDocument newGraph(const std::string& name);
int nodeRowCount(const NodeSpec& spec);
float nodeHeight(const NodeSpec& spec);
void arrangeGraph(GraphDocument& graph);

std::string graphToJson(const GraphDocument& graph);
bool graphFromJson(const std::string& text, GraphDocument& graph, std::string& error);

std::string graphFileName(const std::string& stem);
bool isGraphFileName(const std::string& name);
std::string graphEffectName(const std::string& stem);
bool graphStemFromEffectName(const std::string& name, std::string& stem);
std::string graphDirectory();

struct ExampleGraph {
    const char* name;
    GraphDocument (*build)();
};

const std::vector<ExampleGraph>& exampleGraphs();
std::vector<GraphDocument> catalogueGraphs(const NodeSpec& spec);

}

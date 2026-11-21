#include "check.hpp"

#include "core/node_compile.hpp"
#include "core/node_files.hpp"
#include "core/node_graph.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>

namespace {

using namespace nvj;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

int portNamed(const std::vector<PortSpec>& ports, const char* name) {
    for (size_t i = 0; i < ports.size(); ++i) {
        if (std::string(ports[i].name) == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void testCatalogue() {
    const std::vector<NodeSpec>& catalogue = nodeCatalogue();
    CHECK(catalogue.size() >= 60);
    std::set<std::string> ids;
    int outputs = 0;
    for (const NodeSpec& spec : catalogue) {
        CHECK(ids.insert(spec.id).second);
        CHECK(std::string(spec.title).size() > 0);
        CHECK(std::string(spec.category).size() > 0);
        CHECK(std::string(spec.code).size() > 0);
        outputs += spec.isOutput ? 1 : 0;
        if (spec.isOutput) {
            CHECK(spec.outputs.empty());
        } else {
            CHECK(!spec.outputs.empty());
        }
        for (const PortSpec& port : spec.inputs) {
            CHECK(std::string(port.name).size() > 0);
            if (port.type != PortType::Float) {
                CHECK(port.expression != nullptr);
                CHECK(!port.generic);
            } else if (!port.expression) {
                CHECK(port.value >= port.minimum && port.value <= port.maximum);
            }
        }
        for (const ConstSpec& c : spec.consts) {
            CHECK(c.value >= c.minimum && c.value <= c.maximum);
            CHECK(c.minimum < c.maximum);
        }
        std::string helpers = spec.helpers;
        if (!helpers.empty()) {
            CHECK(nodeHelperSource(helpers) != nullptr);
        }
        CHECK(findNodeSpec(spec.id) == &spec);
    }
    CHECK(outputs == 1);
    CHECK(findNodeSpec("nothing") == nullptr);
    const std::vector<std::string> categories = nodeCategories();
    CHECK(categories.size() >= 8);
    CHECK(std::find(categories.begin(), categories.end(), "Output") == categories.end());
}

void testBasicCompile() {
    GraphDocument graph = newGraph("Plain");
    CHECK(graph.nodes.size() == 2);
    CHECK(graph.links.size() == 1);
    const CompiledGraph compiled = compileGraph(graph);
    CHECK(compiled.ok);
    CHECK(compiled.errors.empty());
    CHECK(compiled.textureReads == 1);
    CHECK(!compiled.usesPrevious);
    CHECK(!compiled.usesMacros);
    CHECK(contains(compiled.glsl, "vec4 effect(vec2 uv, vec4 p)"));
    CHECK(contains(compiled.glsl, "texture(uTex, uv)"));
    CHECK(contains(compiled.glsl, "return n1_0;"));
    CHECK(compiled.usedNodes.size() == 2);
    CHECK(compiled.hash != 0);
}

void testEveryCatalogueNodeCompiles() {
    int graphs = 0;
    for (const NodeSpec& spec : nodeCatalogue()) {
        for (const GraphDocument& graph : catalogueGraphs(spec)) {
            ++graphs;
            const CompiledGraph compiled = compileGraph(graph);
            if (!compiled.ok) {
                std::fprintf(stderr, "catalogue graph %s: %s\n", graph.name.c_str(), compiled.firstError().c_str());
            }
            CHECK(compiled.ok);
            CHECK(contains(compiled.glsl, "vec4 effect(vec2 uv, vec4 p)"));
            CHECK(!compiled.usedNodes.empty());
            std::string error;
            GraphDocument again;
            CHECK(graphFromJson(graphToJson(graph), again, error));
            CHECK(again == graph);
            CHECK(compileGraph(again).hash == compiled.hash);
        }
    }
    CHECK(graphs >= 100);
}

void testExamples() {
    CHECK(exampleGraphs().size() >= 4);
    std::set<std::string> names;
    for (const ExampleGraph& example : exampleGraphs()) {
        const GraphDocument graph = example.build();
        CHECK(names.insert(graph.name).second);
        CHECK(graph.name == example.name);
        const CompiledGraph compiled = compileGraph(graph);
        if (!compiled.ok) {
            std::fprintf(stderr, "example %s: %s\n", example.name, compiled.firstError().c_str());
        }
        CHECK(compiled.ok);
        CHECK(compiled.usedNodes.size() == graph.nodes.size());
        std::string error;
        GraphDocument loaded;
        CHECK(graphFromJson(graphToJson(graph), loaded, error));
        CHECK(loaded == graph);
        CHECK(compileGraph(loaded).hash == compiled.hash);
    }
    CHECK(compileGraph(exampleGraphs()[3].build()).usesPrevious);
}

void testDeterminismAndCache() {
    GraphDocument a = exampleGraphs()[0].build();
    const CompiledGraph first = compileGraph(a);
    const CompiledGraph second = compileGraph(a);
    CHECK(first.glsl == second.glsl);
    CHECK(first.hash == second.hash);

    GraphDocument moved = a;
    for (GraphNode& node : moved.nodes) {
        node.x += 123.0f;
        node.y -= 45.0f;
    }
    moved.name = "Other name";
    moved.controls[0].label = "Changed label";
    moved.controls[0].value = 0.9f;
    CHECK(compileGraph(moved).hash == first.hash);

    GraphDocument changed = a;
    changed.setValue(1, 0, 0.0f);
    GraphNode* wave = nullptr;
    for (GraphNode& node : changed.nodes) {
        if (node.type == "lfo") {
            wave = &node;
        }
    }
    CHECK(wave != nullptr);
    if (wave) {
        changed.setValue(wave->id, 1, 8.0f);
    }
    CHECK(compileGraph(changed).hash != first.hash);

    GraphDocument reordered = a;
    std::reverse(reordered.links.begin(), reordered.links.end());
    CHECK(compileGraph(reordered).hash == first.hash);
}

void testUnusedNodesAndFlags() {
    GraphDocument graph = newGraph("Flags");
    const int stray = graph.addNode("noise", 0, 300);
    CHECK(stray > 0);
    CompiledGraph compiled = compileGraph(graph);
    CHECK(compiled.ok);
    CHECK(std::find(compiled.usedNodes.begin(), compiled.usedNodes.end(), stray) == compiled.usedNodes.end());
    CHECK(!contains(compiled.glsl, "vn("));

    const int macro = graph.addNode("macro", 0, 400);
    const int mix = graph.addNode("multiply", 100, 400);
    CHECK(graph.connect(macro, 0, mix, 0).empty());
    const GraphNode* out = nullptr;
    for (const GraphNode& node : graph.nodes) {
        if (node.type == "output") {
            out = &node;
        }
    }
    CHECK(out != nullptr);
    if (out) {
        CHECK(graph.connect(mix, 0, out->id, 0).empty());
    }
    compiled = compileGraph(graph);
    CHECK(compiled.ok);
    CHECK(compiled.usesMacros);
    CHECK(contains(compiled.glsl, "uniform float uMacros[8];"));
    CHECK(contains(compiled.glsl, "uMacros[1 - 1]"));
}

void testLoops() {
    GraphDocument graph;
    const int a = graph.addNode("add", 0, 0);
    const int b = graph.addNode("multiply", 100, 0);
    const int c = graph.addNode("min", 200, 0);
    const int out = graph.addNode("output", 300, 0);
    CHECK(graph.connect(a, 0, b, 0).empty());
    CHECK(graph.connect(b, 0, c, 0).empty());
    CHECK(graph.connect(c, 0, out, 0).empty());
    CHECK(!graph.connect(c, 0, a, 0).empty());
    CHECK(contains(graph.connect(c, 0, a, 1), "loop"));
    CHECK(!graph.connect(a, 0, a, 0).empty());
    CHECK(graph.links.size() == 3);

    GraphDocument forced = graph;
    forced.links.push_back({c, 0, a, 0});
    const CompiledGraph compiled = compileGraph(forced);
    CHECK(!compiled.ok);
    CHECK(compiled.glsl.empty());
    CHECK(contains(compiled.firstError(), "loop"));
    CHECK(!compiled.errorFor(a).empty() || !compiled.errorFor(b).empty() || !compiled.errorFor(c).empty());

    std::string json = graphToJson(forced);
    GraphDocument loaded;
    std::string error;
    CHECK(!graphFromJson(json, loaded, error));
    CHECK(contains(error, "loop"));

    GraphDocument previous = newGraph("Feedback");
    const int prev = previous.addNode("previous", 0, 200);
    const int mix = previous.addNode("max", 100, 100);
    CHECK(previous.connect(prev, 0, mix, 0).empty());
    CHECK(previous.reaches(prev, mix));
    CHECK(!previous.reaches(mix, prev));
}

void testTypeErrors() {
    GraphDocument graph;
    const int uv = graph.addNode("uv", 0, 0);
    const int out = graph.addNode("output", 400, 0);
    const int hue = graph.addNode("hue", 200, 0);
    CHECK(contains(graph.connect(uv, 0, out, 0), "cannot be connected"));
    CHECK(contains(graph.connect(uv, 0, hue, 0), "cannot be connected"));
    CHECK(graph.links.empty());
    CHECK(!graph.connect(uv, 3, hue, 0).empty());
    CHECK(!graph.connect(uv, 0, hue, 9).empty());
    CHECK(!graph.connect(99, 0, hue, 0).empty());

    GraphDocument mixed;
    const int vec2 = mixed.addNode("vector2", 0, 0);
    const int colour = mixed.addNode("colour", 0, 100);
    const int add = mixed.addNode("add", 200, 0);
    const int result = mixed.addNode("output", 400, 0);
    CHECK(mixed.connect(vec2, 0, add, 0).empty());
    CHECK(mixed.connect(colour, 0, add, 1).empty());
    CHECK(mixed.connect(add, 0, result, 0).empty());
    const CompiledGraph compiled = compileGraph(mixed);
    CHECK(!compiled.ok);
    CHECK(contains(compiled.errorFor(add), "cannot be combined"));

    GraphDocument widened;
    const int c3 = widened.addNode("colour", 0, 0);
    const int pic = widened.addNode("picture", 0, 100);
    const int sum = widened.addNode("add", 200, 0);
    const int sink = widened.addNode("output", 400, 0);
    CHECK(widened.connect(c3, 0, sum, 0).empty());
    CHECK(widened.connect(pic, 0, sum, 1).empty());
    CHECK(widened.connect(sum, 0, sink, 0).empty());
    CHECK(compileGraph(widened).ok);

    GraphDocument floatToVec;
    const int number = floatToVec.addNode("number", 0, 0);
    const int hue2 = floatToVec.addNode("hue", 100, 0);
    const int alpha = floatToVec.addNode("makecoloura", 200, 0);
    const int sink2 = floatToVec.addNode("output", 300, 0);
    CHECK(floatToVec.connect(number, 0, hue2, 0).empty());
    CHECK(floatToVec.connect(hue2, 0, alpha, 0).empty());
    CHECK(floatToVec.connect(alpha, 0, sink2, 0).empty());
    const CompiledGraph conv = compileGraph(floatToVec);
    CHECK(conv.ok);
    CHECK(contains(conv.glsl, "vec3(n1_0)"));
}

void testOutputRules() {
    GraphDocument empty;
    CHECK(!compileGraph(empty).ok);
    CHECK(contains(compileGraph(empty).firstError(), "empty"));

    GraphDocument noOutput;
    noOutput.addNode("picture", 0, 0);
    const CompiledGraph missing = compileGraph(noOutput);
    CHECK(!missing.ok);
    CHECK(contains(missing.firstError(), "Output"));

    GraphDocument two = newGraph("Two");
    CHECK(two.addNode("output", 0, 0) < 0);
    two.nodes.push_back(GraphNode{50, "output", 0.0f, 0.0f, {}});
    fillNodeValues(*findNodeSpec("output"), two.nodes.back().values);
    const CompiledGraph several = compileGraph(two);
    CHECK(!several.ok);
    CHECK(contains(several.firstError(), "more than one"));

    GraphDocument unknown = newGraph("Unknown");
    unknown.nodes.push_back(GraphNode{60, "bogus", 0.0f, 0.0f, {}});
    const CompiledGraph bad = compileGraph(unknown);
    CHECK(!bad.ok);
    CHECK(contains(bad.errorFor(60), "Unknown node type"));
}

void testLimits() {
    GraphDocument graph;
    for (size_t i = 0; i < kMaxGraphNodes; ++i) {
        CHECK(graph.addNode("number", static_cast<float>(i), 0.0f) > 0);
    }
    CHECK(graph.addNode("number", 0, 0) < 0);
    CHECK(graph.nodes.size() == kMaxGraphNodes);

    GraphDocument chain;
    const int uv = chain.addNode("uv", 0, 100);
    const int blur = chain.addNode("blur", 100.0f, 200.0f);
    chain.setValue(blur, 2, 8.0f);
    CHECK(chain.connect(uv, 0, blur, 0).empty());
    const int sink = chain.addNode("output", 900, 0);
    CHECK(chain.connect(blur, 0, sink, 0).empty());
    const CompiledGraph limited = compileGraph(chain);
    CHECK(limited.ok);
    CHECK(limited.textureReads == 8);

    GraphDocument heavy;
    const int src = heavy.addNode("uv", 0, 0);
    const int out = heavy.addNode("output", 0, 200);
    int previousSum = -1;
    for (int i = 0; i < 9; ++i) {
        const int blur = heavy.addNode("blur", 0, 0);
        heavy.setValue(blur, 2, 8.0f);
        CHECK(heavy.connect(src, 0, blur, 0).empty());
        const int summer = heavy.addNode("add", 0, 0);
        CHECK(heavy.connect(blur, 0, summer, 0).empty());
        if (previousSum >= 0) {
            CHECK(heavy.connect(previousSum, 0, summer, 1).empty());
        }
        previousSum = summer;
    }
    CHECK(heavy.connect(previousSum, 0, out, 0).empty());
    const CompiledGraph over = compileGraph(heavy);
    CHECK(!over.ok);
    CHECK(over.textureReads == 72);
    CHECK(contains(over.firstError(), "limit"));

    GraphDocument links;
    const int number = links.addNode("number", 0, 0);
    std::vector<int> adds;
    for (int i = 0; i < 90; ++i) {
        adds.push_back(links.addNode("add", 0, 0));
    }
    size_t made = 0;
    for (size_t i = 0; i < adds.size(); ++i) {
        if (links.connect(number, 0, adds[i], 0).empty()) {
            ++made;
        }
        if (links.connect(number, 0, adds[i], 1).empty()) {
            ++made;
        }
    }
    CHECK(made == links.links.size());
    CHECK(links.links.size() <= kMaxGraphLinks);
    CHECK(links.links.size() == 180);
}

void testValues() {
    GraphDocument graph;
    const int c = graph.addNode("control", 0, 0);
    graph.setValue(c, 0, 9.0f);
    CHECK(graph.value(c, 0) == 4.0f);
    graph.setValue(c, 0, 2.4f);
    CHECK(graph.value(c, 0) == 2.0f);
    graph.setValue(c, 0, -3.0f);
    CHECK(graph.value(c, 0) == 1.0f);
    const int wave = graph.addNode("wave", 0, 0);
    graph.setValue(wave, 1, 5.0f);
    CHECK(graph.value(wave, 1) == 1.0f);
    graph.setValue(wave, 2, std::numeric_limits<float>::quiet_NaN());
    CHECK(graph.value(wave, 2) == 0.0f);
    graph.setValue(wave, 99, 1.0f);
    CHECK(graph.value(wave, 99) == 0.0f);
    graph.setValue(1234, 0, 1.0f);
    CHECK(graph.value(1234, 0) == 0.0f);

    CHECK(formatShaderFloat(1.0f) == "1.0");
    CHECK(formatShaderFloat(0.5f) == "0.5");
    CHECK(formatShaderFloat(-0.25f) == "(-0.25)");
    CHECK(contains(formatShaderFloat(0.00001f), "e"));
    CHECK(formatShaderFloat(std::numeric_limits<float>::infinity()) == "0.0");
    CHECK(formatShaderFloat(32.0f) == "32.0");
}

void testRemoveAndReplace() {
    GraphDocument graph = newGraph("Edit");
    const int out = graph.nodes[1].id;
    const int pic = graph.nodes[0].id;
    CHECK(graph.linkInto(out, 0) != nullptr);
    const int number = graph.addNode("number", 0, 0);
    CHECK(graph.connect(number, 0, out, 0).empty());
    CHECK(graph.links.size() == 1);
    CHECK(graph.linkInto(out, 0)->fromNode == number);
    CHECK(graph.disconnect(out, 0));
    CHECK(!graph.disconnect(out, 0));
    CHECK(graph.links.empty());
    CHECK(graph.connect(pic, 0, out, 0).empty());
    CHECK(graph.removeNode(pic));
    CHECK(graph.links.empty());
    CHECK(!graph.removeNode(pic));
    CHECK(graph.find(pic) == nullptr);
    const int again = graph.addNode("picture", 0, 0);
    CHECK(again != pic);
}

void testJsonErrors() {
    GraphDocument graph;
    std::string error;
    CHECK(!graphFromJson("not json", graph, error));
    CHECK(!error.empty());
    CHECK(!graphFromJson("[]", graph, error));
    CHECK(!graphFromJson("{\"format\":\"other\",\"version\":1,\"nodes\":[]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"nodes\":[]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":99,\"nodes\":[]}", graph, error));
    CHECK(contains(error, "newer"));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":0,\"nodes\":[]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"nothing\"}]}", graph, error));
    CHECK(contains(error, "Unknown node type"));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"picture\"},{\"id\":1,\"type\":\"uv\"}]}", graph, error));
    CHECK(contains(error, "twice"));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":0,\"type\":\"picture\"}]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"output\"},{\"id\":2,\"type\":\"output\"}]}", graph, error));
    CHECK(contains(error, "more than one"));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"picture\"},{\"id\":2,\"type\":\"output\"}],\"links\":[{\"from\":1,\"fromPort\":0,\"to\":2,\"toPort\":0},{\"from\":1,\"fromPort\":0,\"to\":2,\"toPort\":0}]}", graph, error));
    CHECK(contains(error, "two connections"));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"picture\"},{\"id\":2,\"type\":\"output\"}],\"links\":[{\"from\":1,\"fromPort\":5,\"to\":2,\"toPort\":0}]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"picture\"},{\"id\":2,\"type\":\"output\"}],\"links\":[{\"from\":9,\"fromPort\":0,\"to\":2,\"toPort\":0}]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"picture\"}],\"links\":[{\"from\":\"a\"}]}", graph, error));
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":{}}", graph, error));
    CHECK(graph.nodes.empty());

    std::string nodes;
    for (size_t i = 0; i <= kMaxGraphNodes; ++i) {
        nodes += std::string(i ? "," : "") + "{\"id\":" + std::to_string(i + 1) + ",\"type\":\"number\"}";
    }
    CHECK(!graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[" + nodes + "]}", graph, error));
    CHECK(contains(error, "more than 96"));

    std::string big = "{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[],\"pad\":\"" + std::string(kMaxGraphFileBytes, 'x') + "\"}";
    CHECK(!graphFromJson(big, graph, error));
    CHECK(contains(error, "256 KiB"));
}

void testJsonValues() {
    GraphDocument graph;
    std::string error;
    const std::string text =
        "{\"format\":\"nyrexvj-effect\",\"version\":1,\"name\":\"Test\","
        "\"controls\":[{\"label\":\"A very long label that goes past the limit\",\"value\":3},{\"label\":\"B\\u0001C\",\"value\":-1},{\"value\":\"x\"},5,{\"label\":\"ignored\"}],"
        "\"nodes\":[{\"id\":7,\"type\":\"wave\",\"x\":1e30,\"y\":\"a\",\"values\":[0,99,-99,\"x\",5,1,2,3]},{\"id\":9,\"type\":\"control\",\"values\":[7]}]}";
    CHECK(graphFromJson(text, graph, error));
    CHECK(graph.name == "Test");
    CHECK(graph.controls[0].label.size() <= kMaxGraphLabel);
    CHECK(graph.controls[0].value == 1.0f);
    CHECK(graph.controls[1].label == "BC");
    CHECK(graph.controls[1].value == 0.0f);
    CHECK(graph.controls[2].value == 0.5f);
    CHECK(graph.nodes.size() == 2);
    const GraphNode& wave = graph.nodes[0];
    CHECK(wave.id == 7);
    CHECK(wave.x == 100000.0f);
    CHECK(wave.y == 0.0f);
    CHECK(wave.values.size() == 4);
    CHECK(wave.values[1] == 1.0f);
    CHECK(wave.values[2] == 0.0f);
    CHECK(wave.values[3] == 0.0f);
    CHECK(graph.nodes[1].values.size() == 1);
    CHECK(graph.nodes[1].values[0] == 4.0f);
    CHECK(graph.nextId == 10);
    const int added = graph.addNode("number", 0, 0);
    CHECK(added == 10);

    GraphDocument shortValues;
    CHECK(graphFromJson("{\"format\":\"nyrexvj-effect\",\"version\":1,\"nodes\":[{\"id\":1,\"type\":\"blur\"}]}", shortValues, error));
    CHECK(shortValues.nodes[0].values.size() == 3);
    CHECK(shortValues.nodes[0].values[2] == 8.0f);
}

void testNames() {
    CHECK(isGraphFileName("a.nyxfx"));
    CHECK(isGraphFileName("My Effect.NYXFX"));
    CHECK(!isGraphFileName(".nyxfx"));
    CHECK(!isGraphFileName("a.glsl"));
    CHECK(!isGraphFileName("nyxfx"));
    CHECK(graphFileName("Abc") == "Abc.nyxfx");
    CHECK(graphEffectName("Abc") == "Node: Abc");
    std::string stem;
    CHECK(graphStemFromEffectName("Node: Abc", stem));
    CHECK(stem == "Abc");
    CHECK(!graphStemFromEffectName("Node: ", stem));
    CHECK(!graphStemFromEffectName("Tile", stem));
    CHECK(!graphStemFromEffectName("", stem));
    CHECK(!graphDirectory().empty());
}

void testPortNames() {
    for (const NodeSpec& spec : nodeCatalogue()) {
        std::set<std::string> inputs;
        for (const PortSpec& port : spec.inputs) {
            CHECK(inputs.insert(port.name).second);
        }
        std::set<std::string> outputs;
        for (const PortSpec& port : spec.outputs) {
            CHECK(outputs.insert(port.name).second);
        }
    }
    const NodeSpec* wave = findNodeSpec("wave");
    CHECK(wave != nullptr);
    if (wave) {
        CHECK(portNamed(wave->inputs, "Frequency") == 2);
        CHECK(portNamed(wave->outputs, "UV") == 0);
        CHECK(portNamed(wave->inputs, "Nothing") == -1);
    }
}


void testArrange() {
    for (const ExampleGraph& example : exampleGraphs()) {
        GraphDocument graph = example.build();
        const unsigned long long before = compileGraph(graph).hash;
        arrangeGraph(graph);
        CHECK(compileGraph(graph).hash == before);
        GraphDocument again = graph;
        arrangeGraph(again);
        CHECK(graph == again);
        for (size_t a = 0; a < graph.nodes.size(); ++a) {
            const NodeSpec* sa = findNodeSpec(graph.nodes[a].type);
            CHECK(sa != nullptr);
            if (!sa) {
                continue;
            }
            CHECK(graph.nodes[a].x >= 0.0f);
            CHECK(graph.nodes[a].y >= 0.0f);
            for (size_t b = a + 1; b < graph.nodes.size(); ++b) {
                const NodeSpec* sb = findNodeSpec(graph.nodes[b].type);
                if (!sb) {
                    continue;
                }
                const GraphNode& na = graph.nodes[a];
                const GraphNode& nb = graph.nodes[b];
                const bool apartX = na.x + kNodeWidth <= nb.x || nb.x + kNodeWidth <= na.x;
                const bool apartY = na.y + nodeHeight(*sa) <= nb.y || nb.y + nodeHeight(*sb) <= na.y;
                CHECK(apartX || apartY);
            }
        }
        for (const GraphLink& link : graph.links) {
            const GraphNode* from = graph.find(link.fromNode);
            const GraphNode* to = graph.find(link.toNode);
            CHECK(from != nullptr && to != nullptr);
            if (from && to) {
                CHECK(from->x < to->x);
            }
        }
    }
    GraphDocument empty;
    arrangeGraph(empty);
    CHECK(empty.nodes.empty());
    CHECK(nodeRowCount(*findNodeSpec("output")) >= 1);
    CHECK(nodeHeight(*findNodeSpec("wave")) > nodeHeight(*findNodeSpec("uv")));
}

void testFiles() {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "nvj_node_files_test";
    std::error_code ec;
    fs::remove_all(folder, ec);
    fs::create_directories(folder, ec);

    CHECK(scanGraphFiles(folder).empty());
    CHECK(sanitizeGraphName("A/B:C").find('/') == std::string::npos);
    CHECK(uniqueGraphName(folder, "Glow") == "Glow");
    CHECK(uniqueGraphName(folder, "") == "Effect");

    GraphDocument graph = exampleGraphs()[0].build();
    std::string error;
    CHECK(saveGraph(folder, "Glow", graph, error));
    CHECK(graphExists(folder, "Glow"));
    CHECK(!graphExists(folder, "Other"));
    CHECK(!graphExists(folder, ""));
    CHECK(uniqueGraphName(folder, "Glow") == "Glow 2");
    CHECK(saveGraph(folder, "Glow 2", graph, error));
    CHECK(uniqueGraphName(folder, "Glow") == "Glow 3");

    std::vector<GraphFileInfo> files = scanGraphFiles(folder);
    CHECK(files.size() == 2);
    if (files.size() == 2) {
        CHECK(files[0].stem == "Glow");
        CHECK(files[1].stem == "Glow 2");
        CHECK(files[0].bytes > 0);
    }

    GraphDocument loaded;
    CHECK(readGraphFile(graphPath(folder, "Glow"), loaded, error));
    CHECK(loaded.name == "Glow");
    CHECK(loaded.nodes.size() == graph.nodes.size());
    CHECK(loaded.links.size() == graph.links.size());
    CHECK(compileGraph(loaded).hash == compileGraph(graph).hash);

    CHECK(!saveGraph(folder, "", graph, error));
    CHECK(!error.empty());

    const fs::path exported = folder / "export" / "Shared.nyxfx";
    CHECK(exportGraph(folder, "Glow", exported, error));
    CHECK(fs::is_regular_file(exported));
    CHECK(!exportGraph(folder, "Missing", exported, error));
    CHECK(!exportGraph(folder, "Glow", fs::path(), error));

    std::string imported;
    CHECK(importGraph(folder, exported, imported, error));
    CHECK(imported == "Glow 3");
    CHECK(graphExists(folder, "Glow 3"));
    CHECK(scanGraphFiles(folder).size() == 3);

    {
        std::ofstream broken(folder / "Broken.nyxfx", std::ios::binary);
        broken << "{ not json";
    }
    GraphDocument rejected;
    CHECK(!readGraphFile(folder / "Broken.nyxfx", rejected, error));
    CHECK(!error.empty());
    CHECK(!importGraph(folder, folder / "Broken.nyxfx", imported, error));
    CHECK(!readGraphFile(folder / "None.nyxfx", rejected, error));

    {
        std::ofstream big(folder / "Big.nyxfx", std::ios::binary);
        big << std::string(kMaxGraphFileBytes + 10, ' ');
    }
    CHECK(!readGraphFile(folder / "Big.nyxfx", rejected, error));
    CHECK(error.find("256 KiB") != std::string::npos);

    {
        std::ofstream other(folder / "notes.txt", std::ios::binary);
        other << "x";
    }
    CHECK(scanGraphFiles(folder).size() == 5);

    CHECK(deleteGraph(folder, "Glow 3", error));
    CHECK(!graphExists(folder, "Glow 3"));
    CHECK(!deleteGraph(folder, "Glow 3", error));

    const fs::path examples = folder / "examples";
    const int written = writeExampleGraphs(examples);
    CHECK(written == static_cast<int>(exampleGraphs().size()));
    CHECK(writeExampleGraphs(examples) == 0);
    for (const GraphFileInfo& file : scanGraphFiles(examples)) {
        GraphDocument example;
        CHECK(readGraphFile(file.path, example, error));
        CHECK(compileGraph(example).ok);
    }

    fs::remove_all(folder, ec);
}

}

int main() {
    testCatalogue();
    testPortNames();
    testBasicCompile();
    testEveryCatalogueNodeCompiles();
    testExamples();
    testDeterminismAndCache();
    testUnusedNodesAndFlags();
    testLoops();
    testTypeErrors();
    testOutputRules();
    testLimits();
    testValues();
    testRemoveAndReplace();
    testJsonErrors();
    testJsonValues();
    testNames();
    testArrange();
    testFiles();
    return testing::finish();
}

#include "app/app.hpp"
#include "app/ui_common.hpp"

#include "core/log.hpp"
#include "core/node_files.hpp"
#include "platform/platform.hpp"
#include "render/effect_library.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nvj {

namespace {

constexpr size_t kUndoDepth = 64;
constexpr double kSaveDelay = 0.35;
constexpr double kStatusSeconds = 5.0;

std::string lowerCopy(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

ImU32 withAlpha(ImU32 colour, float alpha) {
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(colour);
    value.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(value);
}

ImU32 typeColour(PortType type) {
    switch (type) {
    case PortType::Float:
        return IM_COL32(190, 190, 190, 255);
    case PortType::Vec2:
        return IM_COL32(110, 200, 120, 255);
    case PortType::Vec3:
        return IM_COL32(255, 160, 60, 255);
    case PortType::Vec4:
        return IM_COL32(230, 100, 200, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

ImU32 categoryColour(const char* category) {
    unsigned int hash = 2166136261u;
    for (const char* p = category; *p; ++p) {
        hash = (hash ^ static_cast<unsigned char>(*p)) * 16777619u;
    }
    const ImColor colour = ImColor::HSV(static_cast<float>(hash % 360u) / 360.0f, 0.42f, 0.52f);
    return static_cast<ImU32>(colour);
}

std::vector<std::string> splitChoices(const char* choices) {
    std::vector<std::string> items;
    if (!choices) {
        return items;
    }
    std::string current;
    for (const char* p = choices; *p; ++p) {
        if (*p == ',') {
            items.push_back(current);
            current.clear();
        } else {
            current.push_back(*p);
        }
    }
    items.push_back(current);
    return items;
}

struct ValueMeta {
    const char* name = "";
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool integer = false;
    const char* choices = nullptr;
};

ValueMeta valueMeta(const NodeSpec& spec, size_t index) {
    ValueMeta meta;
    if (index < spec.inputs.size()) {
        const PortSpec& port = spec.inputs[index];
        meta.name = port.name;
        meta.minimum = port.minimum;
        meta.maximum = port.maximum;
        return meta;
    }
    const ConstSpec& constant = spec.consts[index - spec.inputs.size()];
    meta.name = constant.name;
    meta.minimum = constant.minimum;
    meta.maximum = constant.maximum;
    meta.integer = constant.integer;
    meta.choices = constant.choices;
    return meta;
}

bool editableInput(const NodeSpec& spec, size_t port) {
    return spec.inputs[port].expression == nullptr;
}

struct View {
    ImVec2 origin{0.0f, 0.0f};
    ImVec2 pan{0.0f, 0.0f};
    float k = 1.0f;

    ImVec2 position(const GraphNode& node) const {
        return ImVec2(origin.x + pan.x + node.x * k, origin.y + pan.y + node.y * k);
    }
    ImVec2 extent(const NodeSpec& spec) const {
        return ImVec2(kNodeWidth * k, nodeHeight(spec) * k);
    }
    float rowTop(const GraphNode& node, int row) const {
        return position(node).y + (kNodeTitleHeight + static_cast<float>(row) * kNodeRowHeight) * k;
    }
    float rowMid(const GraphNode& node, int row) const {
        return rowTop(node, row) + kNodeRowHeight * 0.5f * k;
    }
    ImVec2 inputPos(const GraphNode& node, int port) const {
        return ImVec2(position(node).x, rowMid(node, port));
    }
    ImVec2 outputPos(const GraphNode& node, const NodeSpec& spec, int port) const {
        const int row = static_cast<int>(spec.inputs.size() + spec.consts.size()) + port;
        return ImVec2(position(node).x + kNodeWidth * k, rowMid(node, row));
    }
    ImVec2 boxMin(const GraphNode& node, int row) const {
        return ImVec2(position(node).x + 12.0f * k, rowTop(node, row) + 2.0f * k);
    }
    ImVec2 boxMax(const GraphNode& node, int row) const {
        return ImVec2(position(node).x + (kNodeWidth - 8.0f) * k, rowTop(node, row) + (kNodeRowHeight - 2.0f) * k);
    }
};

struct PortHit {
    int node = -1;
    int index = -1;
    bool output = false;
};

struct ValueHit {
    int node = -1;
    int index = -1;
};

bool inside(const ImVec2& point, const ImVec2& a, const ImVec2& b) {
    return point.x >= a.x && point.x <= b.x && point.y >= a.y && point.y <= b.y;
}

float distance(const ImVec2& a, const ImVec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

void syncOrder(NodeEditorState& s) {
    s.order.erase(std::remove_if(s.order.begin(), s.order.end(), [&](int id) { return s.graph.find(id) == nullptr; }), s.order.end());
    for (const GraphNode& node : s.graph.nodes) {
        if (std::find(s.order.begin(), s.order.end(), node.id) == s.order.end()) {
            s.order.push_back(node.id);
        }
    }
}

void bringToFront(NodeEditorState& s, int id) {
    s.order.erase(std::remove(s.order.begin(), s.order.end(), id), s.order.end());
    s.order.push_back(id);
}

PortHit findPort(const NodeEditorState& s, const View& view, const ImVec2& mouse) {
    const float radius = 9.0f * view.k;
    for (auto it = s.order.rbegin(); it != s.order.rend(); ++it) {
        const GraphNode* node = s.graph.find(*it);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (!spec) {
            continue;
        }
        for (size_t i = 0; i < spec->inputs.size(); ++i) {
            if (distance(mouse, view.inputPos(*node, static_cast<int>(i))) <= radius) {
                return {node->id, static_cast<int>(i), false};
            }
        }
        for (size_t o = 0; o < spec->outputs.size(); ++o) {
            if (distance(mouse, view.outputPos(*node, *spec, static_cast<int>(o))) <= radius) {
                return {node->id, static_cast<int>(o), true};
            }
        }
    }
    return PortHit{};
}

int findNodeAt(const NodeEditorState& s, const View& view, const ImVec2& mouse) {
    for (auto it = s.order.rbegin(); it != s.order.rend(); ++it) {
        const GraphNode* node = s.graph.find(*it);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (!spec) {
            continue;
        }
        const ImVec2 a = view.position(*node);
        const ImVec2 size = view.extent(*spec);
        if (inside(mouse, a, ImVec2(a.x + size.x, a.y + size.y))) {
            return node->id;
        }
    }
    return -1;
}

ValueHit findValue(const NodeEditorState& s, const View& view, const ImVec2& mouse, int nodeId) {
    const GraphNode* node = s.graph.find(nodeId);
    const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
    if (!spec) {
        return ValueHit{};
    }
    for (size_t i = 0; i < spec->valueCount(); ++i) {
        if (i < spec->inputs.size() && (!editableInput(*spec, i) || s.graph.linkInto(node->id, static_cast<int>(i)))) {
            continue;
        }
        const int row = static_cast<int>(i);
        if (inside(mouse, view.boxMin(*node, row), view.boxMax(*node, row))) {
            return {node->id, static_cast<int>(i)};
        }
    }
    return ValueHit{};
}

void setStatus(NodeEditorState& s, const std::string& text) {
    s.status = text;
    s.statusUntil = ImGui::GetTime() + kStatusSeconds;
}

void drawLink(ImDrawList* list, const ImVec2& from, const ImVec2& to, ImU32 colour, float thickness, float k) {
    const float reach = std::max(40.0f * k, std::fabs(to.x - from.x) * 0.5f);
    list->AddBezierCubic(from, ImVec2(from.x + reach, from.y), ImVec2(to.x - reach, to.y), to, colour, thickness);
}

std::string valueText(const ValueMeta& meta, float value) {
    char buffer[48];
    if (meta.choices) {
        const std::vector<std::string> items = splitChoices(meta.choices);
        const int index = std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(items.size()) - 1);
        return items[static_cast<size_t>(index)];
    }
    if (meta.integer) {
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    }
    return buffer;
}

void drawValueBox(ImDrawList* list, const View& view, const GraphNode& node, const NodeSpec& spec, size_t index, float alpha, bool hot) {
    const ValueMeta meta = valueMeta(spec, index);
    const int row = static_cast<int>(index);
    const ImVec2 a = view.boxMin(node, row);
    const ImVec2 b = view.boxMax(node, row);
    const float value = index < node.values.size() ? node.values[index] : 0.0f;
    const float rounding = 3.0f * view.k;
    list->AddRectFilled(a, b, withAlpha(hot ? IM_COL32(52, 52, 60, 255) : IM_COL32(24, 24, 28, 255), alpha), rounding);
    if (!meta.choices && meta.maximum > meta.minimum) {
        const float t = std::clamp((value - meta.minimum) / (meta.maximum - meta.minimum), 0.0f, 1.0f);
        if (t > 0.0f) {
            list->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * t, b.y), withAlpha(ImGui::GetColorU32(ui::kAccentDim), alpha), rounding);
        }
    }
    const float fontSize = ImGui::GetFontSize();
    const float textY = a.y + ((b.y - a.y) - fontSize) * 0.5f;
    const ImU32 textColour = withAlpha(IM_COL32(225, 225, 225, 255), alpha);
    const std::string text = valueText(meta, value);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    list->PushClipRect(a, ImVec2(std::max(a.x, b.x - size.x - 8.0f * view.k), b.y), true);
    list->AddText(ImVec2(a.x + 4.0f * view.k, textY), textColour, meta.name);
    list->PopClipRect();
    list->AddText(ImVec2(b.x - size.x - 4.0f * view.k, textY), textColour, text.c_str());
}

void drawNode(ImDrawList* list, const View& view, const NodeEditorState& s, const GraphNode& node, const NodeSpec& spec, bool used, const std::string& error, const ImVec2& mouse, bool interactive) {
    const float k = view.k;
    const float alpha = used ? 1.0f : 0.5f;
    const ImVec2 a = view.position(node);
    const ImVec2 size = view.extent(spec);
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const float rounding = 6.0f * k;
    const float fontSize = ImGui::GetFontSize();
    list->AddRectFilled(a, b, withAlpha(IM_COL32(32, 32, 36, 245), alpha), rounding);
    list->AddRectFilled(a, ImVec2(b.x, a.y + kNodeTitleHeight * k), withAlpha(categoryColour(spec.category), alpha), rounding, ImDrawFlags_RoundCornersTop);
    list->AddText(ImVec2(a.x + 8.0f * k, a.y + (kNodeTitleHeight * k - fontSize) * 0.5f), withAlpha(IM_COL32(245, 245, 245, 255), alpha), spec.title);
    const bool selected = s.selected == node.id;
    if (!error.empty()) {
        list->AddRect(a, b, IM_COL32(235, 70, 70, 255), rounding, 0, 2.0f);
        list->AddText(ImVec2(b.x - fontSize - 6.0f * k, a.y + (kNodeTitleHeight * k - fontSize) * 0.5f), IM_COL32(255, 230, 230, 255), "!");
    } else if (selected) {
        list->AddRect(a, b, ImGui::GetColorU32(ui::kAccent), rounding, 0, 2.0f);
    } else {
        list->AddRect(a, b, withAlpha(IM_COL32(90, 90, 100, 255), alpha), rounding, 0, 1.0f);
    }
    const float radius = 5.0f * k;
    for (size_t i = 0; i < spec.inputs.size(); ++i) {
        const PortSpec& port = spec.inputs[i];
        const int row = static_cast<int>(i);
        const ImVec2 pin = view.inputPos(node, row);
        const bool linked = s.graph.linkInto(node.id, row) != nullptr;
        const bool closeBy = interactive && distance(mouse, pin) <= 9.0f * k;
        const ImU32 colour = port.generic ? typeColour(PortType::Float) : typeColour(port.type);
        if (linked) {
            list->AddCircleFilled(pin, closeBy ? radius + 1.5f * k : radius, colour);
        } else {
            list->AddCircleFilled(pin, radius, IM_COL32(20, 20, 24, 255));
            list->AddCircle(pin, closeBy ? radius + 1.5f * k : radius, colour, 0, 1.5f);
        }
        if (!linked && editableInput(spec, i)) {
            const bool hot = interactive && inside(mouse, view.boxMin(node, row), view.boxMax(node, row));
            drawValueBox(list, view, node, spec, i, alpha, hot);
        } else {
            list->AddText(ImVec2(pin.x + 10.0f * k, pin.y - fontSize * 0.5f), withAlpha(IM_COL32(205, 205, 205, 255), alpha), port.name);
        }
    }
    for (size_t c = 0; c < spec.consts.size(); ++c) {
        const size_t index = spec.inputs.size() + c;
        const ImVec2 box0 = view.boxMin(node, static_cast<int>(index));
        const ImVec2 box1 = view.boxMax(node, static_cast<int>(index));
        const bool hot = interactive && inside(mouse, box0, box1);
        drawValueBox(list, view, node, spec, index, alpha, hot);
    }
    for (size_t o = 0; o < spec.outputs.size(); ++o) {
        const PortSpec& port = spec.outputs[o];
        const ImVec2 pin = view.outputPos(node, spec, static_cast<int>(o));
        const bool closeBy = interactive && distance(mouse, pin) <= 9.0f * k;
        const ImU32 colour = port.generic ? typeColour(PortType::Float) : typeColour(port.type);
        list->AddCircleFilled(pin, closeBy ? radius + 1.5f * k : radius, colour);
        const ImVec2 text = ImGui::CalcTextSize(port.name);
        list->AddText(ImVec2(pin.x - 10.0f * k - text.x, pin.y - fontSize * 0.5f), withAlpha(IM_COL32(205, 205, 205, 255), alpha), port.name);
    }
    if (!error.empty()) {
        const float wrap = kNodeWidth * k * 1.3f;
        list->AddText(ImGui::GetFont(), fontSize * 0.9f, ImVec2(a.x, b.y + 3.0f * k), IM_COL32(255, 130, 130, 255), error.c_str(), nullptr, wrap);
    }
}

bool structureEqual(const GraphDocument& a, const GraphDocument& b) {
    if (a.controls != b.controls || a.nodes.size() != b.nodes.size() || a.links != b.links) {
        return false;
    }
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        if (a.nodes[i].id != b.nodes[i].id || a.nodes[i].type != b.nodes[i].type || a.nodes[i].values != b.nodes[i].values) {
            return false;
        }
    }
    return true;
}

void recompile(NodeEditorState& s) {
    if (s.compiledValid && structureEqual(s.graph, s.compiledFor)) {
        return;
    }
    s.compiled = compileGraph(s.graph);
    s.compiledFor = s.graph;
    s.compiledValid = true;
}

void loadDocument(NodeEditorState& s, const GraphDocument& graph, const std::string& stem) {
    s.hasGraph = true;
    s.stem = stem;
    s.graph = graph;
    s.graph.name = stem;
    s.committed = s.graph;
    s.saved = s.graph;
    s.undo.clear();
    s.redo.clear();
    s.compiledValid = false;
    s.order.clear();
    s.selected = -1;
    s.dragNode = -1;
    s.panning = false;
    s.linking = false;
    s.valueNode = -1;
    s.saveTimer = 0.0;
    s.pan = ImVec2(24.0f, 24.0f);
    std::snprintf(s.nameBuffer, sizeof(s.nameBuffer), "%s", stem.c_str());
    syncOrder(s);
    recompile(s);
}

bool writeCurrent(NodeEditorState& s) {
    if (!s.hasGraph) {
        return true;
    }
    GraphDocument copy = s.graph;
    copy.name = s.stem;
    std::string error;
    if (!saveGraph(graphsFolder(), s.stem, copy, error)) {
        setStatus(s, error);
        return false;
    }
    s.saved = s.graph;
    s.syncName = graphEffectName(s.stem);
    s.saveTimer = 0.0;
    s.filesAt = -1.0e9;
    return true;
}

void commitChanges(NodeEditorState& s) {
    if (s.graph == s.committed) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive()) {
        return;
    }
    s.undo.push_back(s.committed);
    if (s.undo.size() > kUndoDepth) {
        s.undo.erase(s.undo.begin());
    }
    s.redo.clear();
    s.committed = s.graph;
}

void afterHistoryJump(NodeEditorState& s) {
    s.graph.name = s.stem;
    s.committed = s.graph;
    s.linking = false;
    s.dragNode = -1;
    s.valueNode = -1;
    if (s.selected >= 0 && !s.graph.find(s.selected)) {
        s.selected = -1;
    }
    syncOrder(s);
}

void undoStep(NodeEditorState& s) {
    commitChanges(s);
    if (s.undo.empty()) {
        return;
    }
    s.redo.push_back(s.graph);
    s.graph = s.undo.back();
    s.undo.pop_back();
    afterHistoryJump(s);
}

void redoStep(NodeEditorState& s) {
    if (s.redo.empty()) {
        return;
    }
    s.undo.push_back(s.graph);
    s.graph = s.redo.back();
    s.redo.pop_back();
    afterHistoryJump(s);
}

void refreshFiles(NodeEditorState& s) {
    const double now = ImGui::GetTime();
    if (now - s.filesAt < 1.0) {
        return;
    }
    s.filesAt = now;
    s.files.clear();
    for (const GraphFileInfo& file : scanGraphFiles(graphsFolder())) {
        s.files.push_back(file.stem);
    }
}

bool openStem(NodeEditorState& s, const std::string& stem) {
    GraphDocument graph;
    std::string error;
    if (!readGraphFile(graphPath(graphsFolder(), stem), graph, error)) {
        setStatus(s, error);
        return false;
    }
    if (!writeCurrent(s)) {
        return false;
    }
    loadDocument(s, graph, stem);
    return true;
}

bool createGraph(NodeEditorState& s, const std::string& wanted, const GraphDocument* source) {
    if (!writeCurrent(s)) {
        return false;
    }
    const std::string stem = uniqueGraphName(graphsFolder(), wanted);
    GraphDocument graph = source ? *source : newGraph(stem);
    if (!source) {
        arrangeGraph(graph);
    }
    graph.name = stem;
    std::string error;
    if (!saveGraph(graphsFolder(), stem, graph, error)) {
        setStatus(s, error);
        return false;
    }
    loadDocument(s, graph, stem);
    s.syncName = graphEffectName(stem);
    s.filesAt = -1.0e9;
    return true;
}

bool renameCurrent(NodeEditorState& s, const std::string& wanted) {
    const std::string clean = sanitizeGraphName(wanted);
    if (clean.empty() || clean == s.stem) {
        std::snprintf(s.nameBuffer, sizeof(s.nameBuffer), "%s", s.stem.c_str());
        return false;
    }
    const std::string target = uniqueGraphName(graphsFolder(), clean);
    const std::string previous = s.stem;
    GraphDocument copy = s.graph;
    copy.name = target;
    std::string error;
    if (!saveGraph(graphsFolder(), target, copy, error)) {
        setStatus(s, error);
        return false;
    }
    deleteGraph(graphsFolder(), previous, error);
    s.stem = target;
    s.graph.name = target;
    s.committed.name = target;
    s.saved = s.graph;
    for (GraphDocument& entry : s.undo) {
        entry.name = target;
    }
    for (GraphDocument& entry : s.redo) {
        entry.name = target;
    }
    std::snprintf(s.nameBuffer, sizeof(s.nameBuffer), "%s", target.c_str());
    s.syncName = graphEffectName(target);
    s.filesAt = -1.0e9;
    return true;
}

void addNodeAt(NodeEditorState& s, const std::string& type) {
    const int id = s.graph.addNode(type, s.menuCanvas.x, s.menuCanvas.y);
    if (id < 0) {
        const NodeSpec* spec = findNodeSpec(type);
        if (spec && spec->isOutput) {
            setStatus(s, tr("There is only one Output node"));
        } else {
            setStatus(s, tr("The graph has too many nodes"));
        }
        return;
    }
    s.selected = id;
    syncOrder(s);
    bringToFront(s, id);
    const NodeSpec* spec = findNodeSpec(type);
    if (!spec || s.menuLinkNode < 0) {
        return;
    }
    if (s.menuLinkFromOutput) {
        for (size_t p = 0; p < spec->inputs.size(); ++p) {
            if (s.graph.connect(s.menuLinkNode, s.menuLinkPort, id, static_cast<int>(p)).empty()) {
                break;
            }
        }
    } else if (!spec->outputs.empty()) {
        s.graph.connect(id, 0, s.menuLinkNode, s.menuLinkPort);
    }
}

void removeSelected(NodeEditorState& s) {
    const GraphNode* node = s.graph.find(s.selected);
    const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
    if (!spec) {
        return;
    }
    if (spec->isOutput) {
        setStatus(s, tr("The Output node cannot be deleted"));
        return;
    }
    s.graph.removeNode(s.selected);
    s.selected = -1;
    syncOrder(s);
}

void duplicateNode(NodeEditorState& s, int id) {
    const GraphNode* node = s.graph.find(id);
    const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
    if (!spec || spec->isOutput) {
        return;
    }
    const std::vector<float> values = node->values;
    const int created = s.graph.addNode(node->type, node->x + 24.0f, node->y + 24.0f);
    if (created < 0) {
        setStatus(s, tr("The graph has too many nodes"));
        return;
    }
    GraphNode* copy = s.graph.find(created);
    if (copy) {
        copy->values = values;
    }
    s.selected = created;
    syncOrder(s);
    bringToFront(s, created);
}

void drawAddMenu(NodeEditorState& s) {
    if (!ImGui::BeginPopup("##addnode")) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        s.search[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(220.0f);
    const bool entered = ImGui::InputTextWithHint("##nodesearch", tr("Search nodes"), s.search, sizeof(s.search), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Separator();
    bool hasOutput = false;
    for (const GraphNode& node : s.graph.nodes) {
        const NodeSpec* spec = findNodeSpec(node.type);
        hasOutput = hasOutput || (spec && spec->isOutput);
    }
    const std::string needle = lowerCopy(s.search);
    std::string picked;
    if (!needle.empty()) {
        int shown = 0;
        for (const NodeSpec& spec : nodeCatalogue()) {
            const std::string hay = lowerCopy(std::string(spec.title) + " " + spec.category + " " + spec.id);
            if (hay.find(needle) == std::string::npos) {
                continue;
            }
            const bool blocked = spec.isOutput && hasOutput;
            ImGui::BeginDisabled(blocked);
            if (ImGui::Selectable(spec.title) || (entered && shown == 0 && !blocked)) {
                picked = spec.id;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", spec.category);
            ImGui::EndDisabled();
            ++shown;
        }
        if (shown == 0) {
            ImGui::TextDisabled("%s", tr("Nothing found"));
        }
    } else {
        for (const std::string& category : nodeCategories()) {
            if (!ImGui::BeginMenu(category.c_str())) {
                continue;
            }
            for (const NodeSpec& spec : nodeCatalogue()) {
                if (category != spec.category) {
                    continue;
                }
                const bool blocked = spec.isOutput && hasOutput;
                if (ImGui::MenuItem(spec.title, nullptr, false, !blocked)) {
                    picked = spec.id;
                }
            }
            ImGui::EndMenu();
        }
    }
    if (!picked.empty()) {
        addNodeAt(s, picked);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawNodeMenu(NodeEditorState& s) {
    if (!ImGui::BeginPopup("##nodemenu")) {
        return;
    }
    const GraphNode* node = s.graph.find(s.menuNode);
    const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
    if (!spec) {
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("%s", spec->title);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Duplicate"), "Ctrl+D", false, !spec->isOutput)) {
        duplicateNode(s, s.menuNode);
    }
    if (ImGui::MenuItem(tr("Delete"), "Del", false, !spec->isOutput)) {
        s.selected = s.menuNode;
        removeSelected(s);
    }
    if (ImGui::MenuItem(tr("Disconnect inputs"))) {
        for (size_t i = 0; i < spec->inputs.size(); ++i) {
            s.graph.disconnect(s.menuNode, static_cast<int>(i));
        }
    }
    if (ImGui::MenuItem(tr("Disconnect outputs"))) {
        const int id = s.menuNode;
        s.graph.links.erase(std::remove_if(s.graph.links.begin(), s.graph.links.end(), [id](const GraphLink& l) { return l.fromNode == id; }), s.graph.links.end());
    }
    ImGui::EndPopup();
}

void drawValuePopups(NodeEditorState& s) {
    if (ImGui::BeginPopup("##nodevalue")) {
        const GraphNode* node = s.graph.find(s.editNode);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (!spec || s.editIndex < 0 || static_cast<size_t>(s.editIndex) >= spec->valueCount()) {
            ImGui::EndPopup();
            return;
        }
        const ValueMeta meta = valueMeta(*spec, static_cast<size_t>(s.editIndex));
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputFloat(meta.name, &s.editValue, 0.0f, 0.0f, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            s.graph.setValue(s.editNode, static_cast<size_t>(s.editIndex), s.editValue);
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextDisabled("%.2f ... %.2f", meta.minimum, meta.maximum);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##nodechoice")) {
        const GraphNode* node = s.graph.find(s.choiceNode);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (!spec || s.choiceIndex < 0 || static_cast<size_t>(s.choiceIndex) >= spec->valueCount()) {
            ImGui::EndPopup();
            return;
        }
        const ValueMeta meta = valueMeta(*spec, static_cast<size_t>(s.choiceIndex));
        const std::vector<std::string> items = splitChoices(meta.choices);
        const int current = static_cast<int>(std::lround(s.graph.value(s.choiceNode, static_cast<size_t>(s.choiceIndex))));
        for (size_t i = 0; i < items.size(); ++i) {
            if (ImGui::Selectable(items[i].c_str(), static_cast<int>(i) == current)) {
                s.graph.setValue(s.choiceNode, static_cast<size_t>(s.choiceIndex), static_cast<float>(i));
            }
        }
        ImGui::EndPopup();
    }
}

void finishLink(NodeEditorState& s, const View& view, const ImVec2& mouse) {
    const PortHit target = findPort(s, view, mouse);
    const bool toPort = target.node >= 0 && target.output != s.linkFromOutput;
    bool connected = false;
    if (toPort) {
        const int fromNode = s.linkFromOutput ? s.linkNode : target.node;
        const int fromPort = s.linkFromOutput ? s.linkPort : target.index;
        const int toNode = s.linkFromOutput ? target.node : s.linkNode;
        const int toIndex = s.linkFromOutput ? target.index : s.linkPort;
        const std::string error = s.graph.connect(fromNode, fromPort, toNode, toIndex);
        if (error.empty()) {
            connected = true;
        } else {
            setStatus(s, error);
        }
    }
    if (!connected && s.linkRestore && (!s.linkMoved || toPort)) {
        s.graph.connect(s.linkOld.fromNode, s.linkOld.fromPort, s.linkOld.toNode, s.linkOld.toPort);
    }
    if (!connected && !toPort && s.linkMoved && target.node < 0 && findNodeAt(s, view, mouse) < 0 && !s.linkRestore) {
        s.menuLinkNode = s.linkNode;
        s.menuLinkPort = s.linkPort;
        s.menuLinkFromOutput = s.linkFromOutput;
        s.menuCanvas = ImVec2((mouse.x - view.origin.x - view.pan.x) / view.k, (mouse.y - view.origin.y - view.pan.y) / view.k);
        s.openAddMenu = true;
    }
    s.linking = false;
    s.linkRestore = false;
}

void handleCanvasInput(NodeEditorState& s, const View& view, bool hovered, bool windowFocused) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const PortHit port = findPort(s, view, mouse);
        if (port.node >= 0) {
            s.linking = true;
            s.linkMoved = false;
            s.linkStart = mouse;
            s.linkRestore = false;
            if (!port.output) {
                const GraphLink* existing = s.graph.linkInto(port.node, port.index);
                if (existing) {
                    s.linkOld = *existing;
                    s.linkRestore = true;
                    s.linkFromOutput = true;
                    s.linkNode = existing->fromNode;
                    s.linkPort = existing->fromPort;
                    s.graph.disconnect(port.node, port.index);
                } else {
                    s.linkFromOutput = false;
                    s.linkNode = port.node;
                    s.linkPort = port.index;
                }
            } else {
                s.linkFromOutput = true;
                s.linkNode = port.node;
                s.linkPort = port.index;
            }
        } else {
            const int nodeId = findNodeAt(s, view, mouse);
            if (nodeId >= 0) {
                s.selected = nodeId;
                bringToFront(s, nodeId);
                const ValueHit value = findValue(s, view, mouse, nodeId);
                const GraphNode* node = s.graph.find(nodeId);
                const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
                if (value.node >= 0 && spec) {
                    const ValueMeta meta = valueMeta(*spec, static_cast<size_t>(value.index));
                    if (meta.choices) {
                        s.choiceNode = value.node;
                        s.choiceIndex = value.index;
                        s.openChoiceMenu = true;
                    } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || io.KeyCtrl) {
                        s.editNode = value.node;
                        s.editIndex = value.index;
                        s.editValue = s.graph.value(value.node, static_cast<size_t>(value.index));
                        s.openTypedValue = true;
                    } else {
                        const int row = value.index;
                        s.valueNode = value.node;
                        s.valueIndex = value.index;
                        s.valueAccum = s.graph.value(value.node, static_cast<size_t>(value.index));
                        s.valueMouse = mouse.x;
                        s.valueWidth = std::max(20.0f, view.boxMax(*node, row).x - view.boxMin(*node, row).x);
                    }
                } else if (node) {
                    s.dragNode = nodeId;
                    const ImVec2 origin = view.position(*node);
                    s.dragGrab = ImVec2(mouse.x - origin.x, mouse.y - origin.y);
                }
            } else {
                s.selected = -1;
                s.panning = true;
                s.panGrab = ImVec2(mouse.x - s.pan.x, mouse.y - s.pan.y);
            }
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        s.panning = true;
        s.panGrab = ImVec2(mouse.x - s.pan.x, mouse.y - s.pan.y);
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int nodeId = findNodeAt(s, view, mouse);
        if (nodeId >= 0) {
            s.selected = nodeId;
            s.menuNode = nodeId;
            s.openNodeMenu = true;
        } else {
            s.menuLinkNode = -1;
            s.menuLinkPort = -1;
            s.menuCanvas = ImVec2((mouse.x - view.origin.x - view.pan.x) / view.k, (mouse.y - view.origin.y - view.pan.y) / view.k);
            s.openAddMenu = true;
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyShift) {
            s.pan.x += io.MouseWheel * 48.0f;
        } else {
            s.pan.y += io.MouseWheel * 48.0f;
        }
    }

    if (s.panning) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            s.panning = false;
        } else {
            s.pan = ImVec2(mouse.x - s.panGrab.x, mouse.y - s.panGrab.y);
        }
    }
    if (s.dragNode >= 0) {
        GraphNode* node = s.graph.find(s.dragNode);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !node) {
            s.dragNode = -1;
        } else {
            node->x = (mouse.x - s.dragGrab.x - view.origin.x - view.pan.x) / view.k;
            node->y = (mouse.y - s.dragGrab.y - view.origin.y - view.pan.y) / view.k;
        }
    }
    if (s.valueNode >= 0) {
        const GraphNode* node = s.graph.find(s.valueNode);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !spec) {
            s.valueNode = -1;
        } else {
            const ValueMeta meta = valueMeta(*spec, static_cast<size_t>(s.valueIndex));
            const float range = meta.maximum - meta.minimum;
            const float step = meta.integer ? range / s.valueWidth : range / (s.valueWidth * 2.0f);
            const float scale = io.KeyShift ? step * 0.1f : step;
            s.valueAccum = std::clamp(s.valueAccum + (mouse.x - s.valueMouse) * scale, meta.minimum, meta.maximum);
            s.valueMouse = mouse.x;
            s.graph.setValue(s.valueNode, static_cast<size_t>(s.valueIndex), meta.integer ? std::round(s.valueAccum) : s.valueAccum);
        }
    }
    if (s.linking) {
        if (distance(mouse, s.linkStart) > 4.0f) {
            s.linkMoved = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            finishLink(s, view, mouse);
        }
    }

    if (windowFocused && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            removeSelected(s);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            duplicateNode(s, s.selected);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                redoStep(s);
            } else {
                undoStep(s);
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            redoStep(s);
        }
    }
}

void drawCanvas(NodeEditorState& s, const ImVec2& size, bool windowFocused) {
    ImGui::InvisibleButton("##nodecanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 cmin = ImGui::GetItemRectMin();
    const ImVec2 cmax = ImGui::GetItemRectMax();

    View view;
    view.origin = cmin;
    view.pan = s.pan;
    view.k = std::clamp(ImGui::GetFontSize() / 16.0f, 0.75f, 2.5f);

    syncOrder(s);
    handleCanvasInput(s, view, hovered, windowFocused);
    view.pan = s.pan;
    recompile(s);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool interactive = hovered && !s.panning && s.dragNode < 0 && s.valueNode < 0;
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushClipRect(cmin, cmax, true);
    list->AddRectFilled(cmin, cmax, IM_COL32(18, 18, 21, 255));
    const float grid = 32.0f * view.k;
    const float startX = std::fmod(s.pan.x, grid);
    const float startY = std::fmod(s.pan.y, grid);
    for (float x = startX; x < cmax.x - cmin.x; x += grid) {
        list->AddLine(ImVec2(cmin.x + x, cmin.y), ImVec2(cmin.x + x, cmax.y), IM_COL32(255, 255, 255, 10));
    }
    for (float y = startY; y < cmax.y - cmin.y; y += grid) {
        list->AddLine(ImVec2(cmin.x, cmin.y + y), ImVec2(cmax.x, cmin.y + y), IM_COL32(255, 255, 255, 10));
    }

    for (const GraphLink& link : s.graph.links) {
        const GraphNode* from = s.graph.find(link.fromNode);
        const GraphNode* to = s.graph.find(link.toNode);
        const NodeSpec* fromSpec = from ? findNodeSpec(from->type) : nullptr;
        if (!from || !to || !fromSpec || link.fromPort < 0 || static_cast<size_t>(link.fromPort) >= fromSpec->outputs.size()) {
            continue;
        }
        const PortSpec& port = fromSpec->outputs[static_cast<size_t>(link.fromPort)];
        const ImU32 colour = port.generic ? typeColour(PortType::Float) : typeColour(port.type);
        drawLink(list, view.outputPos(*from, *fromSpec, link.fromPort), view.inputPos(*to, link.toPort), withAlpha(colour, 0.85f), 2.0f * view.k, view.k);
    }

    for (int id : s.order) {
        const GraphNode* node = s.graph.find(id);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (!spec) {
            continue;
        }
        const bool used = !s.compiled.ok || std::find(s.compiled.usedNodes.begin(), s.compiled.usedNodes.end(), id) != s.compiled.usedNodes.end();
        drawNode(list, view, s, *node, *spec, used, s.compiled.errorFor(id), mouse, interactive);
    }

    if (s.linking) {
        const GraphNode* node = s.graph.find(s.linkNode);
        const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
        if (node && spec) {
            if (s.linkFromOutput && static_cast<size_t>(s.linkPort) < spec->outputs.size()) {
                drawLink(list, view.outputPos(*node, *spec, s.linkPort), mouse, IM_COL32(255, 255, 255, 200), 2.0f * view.k, view.k);
            } else if (!s.linkFromOutput && static_cast<size_t>(s.linkPort) < spec->inputs.size()) {
                drawLink(list, mouse, view.inputPos(*node, s.linkPort), IM_COL32(255, 255, 255, 200), 2.0f * view.k, view.k);
            }
        }
    }
    list->PopClipRect();

    if (interactive && !s.linking) {
        const PortHit port = findPort(s, view, mouse);
        if (port.node >= 0) {
            const GraphNode* node = s.graph.find(port.node);
            const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
            if (spec) {
                const PortSpec& info = port.output ? spec->outputs[static_cast<size_t>(port.index)] : spec->inputs[static_cast<size_t>(port.index)];
                ImGui::SetTooltip("%s (%s)", info.name, info.generic ? "Float / Vec2 / Vec3 / Vec4" : portTypeName(info.type));
            }
        } else {
            const int nodeId = findNodeAt(s, view, mouse);
            const std::string error = nodeId >= 0 ? s.compiled.errorFor(nodeId) : std::string();
            if (!error.empty()) {
                ImGui::SetTooltip("%s", error.c_str());
            }
        }
    }

    if (s.openAddMenu) {
        ImGui::OpenPopup("##addnode");
        s.openAddMenu = false;
    }
    if (s.openNodeMenu) {
        ImGui::OpenPopup("##nodemenu");
        s.openNodeMenu = false;
    }
    if (s.openTypedValue) {
        ImGui::OpenPopup("##nodevalue");
        s.openTypedValue = false;
    }
    if (s.openChoiceMenu) {
        ImGui::OpenPopup("##nodechoice");
        s.openChoiceMenu = false;
    }
    drawAddMenu(s);
    drawNodeMenu(s);
    drawValuePopups(s);
}

bool inputLabel(const char* id, std::string& value, size_t capacity, const char* hint) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (capacity > sizeof(buffer) - 1) {
        capacity = sizeof(buffer) - 1;
    }
    if (ImGui::InputTextWithHint(id, hint, buffer, capacity + 1)) {
        value = buffer;
        return true;
    }
    return false;
}

}

void App::openNodeEditor(const std::string& stem) {
    if (!nodes_) {
        nodes_ = std::make_unique<NodeEditorState>();
    }
    nodesOpen_ = true;
    nodesFocus_ = true;
    if (!stem.empty() && (!nodes_->hasGraph || nodes_->stem != stem)) {
        openStem(*nodes_, stem);
    }
}

bool App::importNodeGraph(const std::string& path) {
    std::string name;
    std::string error;
    if (!importGraph(graphsFolder(), pathFromUtf8(path), name, error)) {
        toast(std::string(tr("Could not import the effect: ")) + error);
        return false;
    }
    syncNodeEffects(graphEffectName(name));
    toast(std::string(tr("Imported effect: ")) + name);
    if (nodesOpen_ && nodes_) {
        nodes_->filesAt = -1.0e9;
        openStem(*nodes_, name);
    }
    return true;
}

void App::drawNodeEditor() {
    if (!nodes_) {
        nodesHasFocus_ = false;
        return;
    }
    NodeEditorState& s = *nodes_;
    if (!nodesOpen_) {
        if (s.hadFocus || s.hasGraph) {
            if (writeCurrent(s)) {
                s.hadFocus = false;
            }
            if (!s.syncName.empty()) {
                syncNodeEffects(s.syncName);
                s.syncName.clear();
            }
        }
        nodesHasFocus_ = false;
        return;
    }
    s.hadFocus = true;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(1240.0f, viewport->WorkSize.x * 0.92f), std::min(760.0f, viewport->WorkSize.y * 0.88f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (nodesFocus_) {
        ImGui::SetNextWindowFocus();
        nodesFocus_ = false;
    }
    const std::string title = std::string(tr("Node effect editor")) + "###nodeeditor";
    if (!ImGui::Begin(title.c_str(), &nodesOpen_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        nodesHasFocus_ = false;
        return;
    }
    nodesHasFocus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    refreshFiles(s);
    const std::string folder = graphsFolder().string();

    if (ImGui::Button(tr("New"))) {
        createGraph(s, "New effect", nullptr);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##nodeopen", s.hasGraph ? s.stem.c_str() : tr("Open an effect..."))) {
        for (const std::string& stem : s.files) {
            if (ImGui::Selectable(stem.c_str(), s.hasGraph && stem == s.stem)) {
                openStem(s, stem);
            }
        }
        if (s.files.empty()) {
            ImGui::TextDisabled("%s", tr("No node effects yet"));
        }
        ImGui::EndCombo();
    }
    if (s.hasGraph) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##nodename", s.nameBuffer, sizeof(s.nameBuffer));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            renameCurrent(s, s.nameBuffer);
        }
        ImGui::SetItemTooltip("%s", tr("Name of the effect. It appears in the library as Node: and the name."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Duplicate"))) {
            createGraph(s, s.stem + " copy", &s.graph);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete"))) {
            s.openDeleteConfirm = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Arrange"))) {
            arrangeGraph(s.graph);
            s.pan = ImVec2(24.0f, 24.0f);
        }
        ImGui::SetItemTooltip("%s", tr("Place the nodes in columns from the inputs to the output"));
        ImGui::SameLine();
        ImGui::BeginDisabled(s.undo.empty() && s.graph == s.committed);
        if (ImGui::Button(tr("Undo"))) {
            undoStep(s);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(s.redo.empty());
        if (ImGui::Button(tr("Redo"))) {
            redoStep(s);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const std::string addLabel = std::string(tr("Add to layer ")) + std::to_string(selectedLayer_ + 1);
        if (ui::accentButton(addLabel.c_str(), false)) {
            writeCurrent(s);
            syncNodeEffects(graphEffectName(s.stem));
            s.syncName.clear();
            const int index = effectIndexByName(graphEffectName(s.stem));
            if (index >= 0) {
                addEffectToLayer(index, selectedLayer_);
            }
        }
    }

    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputTextWithHint("##nodepath", tr("File path"), s.pathBuffer, sizeof(s.pathBuffer));
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.hasGraph || s.pathBuffer[0] == '\0');
    if (ImGui::Button(tr("Export"))) {
        writeCurrent(s);
        std::string error;
        if (exportGraph(graphsFolder(), s.stem, pathFromUtf8(s.pathBuffer), error)) {
            toast(std::string(tr("Exported: ")) + s.stem);
        } else {
            setStatus(s, error);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.pathBuffer[0] == '\0');
    if (ImGui::Button(tr("Import"))) {
        importNodeGraph(s.pathBuffer);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("You can also drop a .nyxfx file on the window"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Create examples"))) {
        const int written = writeExampleGraphs(graphsFolder());
        s.filesAt = -1.0e9;
        syncNodeEffects();
        toast(written > 0 ? std::string(tr("Examples created: ")) + std::to_string(written) : std::string(tr("The examples already exist")));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Open folder"))) {
        platform::openFolder(folder);
    }

    if (s.openDeleteConfirm) {
        ImGui::OpenPopup("##deletenode");
        s.openDeleteConfirm = false;
    }
    if (ImGui::BeginPopupModal("##deletenode", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("%s", tr("Delete this effect?"));
        ImGui::TextDisabled("%s", tr("The file is removed from the node effects folder."));
        if (ImGui::Button(tr("Delete"))) {
            std::string error;
            if (deleteGraph(graphsFolder(), s.stem, error)) {
                s.syncName = graphEffectName(s.stem);
                s.hasGraph = false;
                s.graph = GraphDocument();
                s.committed = s.graph;
                s.saved = s.graph;
                s.undo.clear();
                s.redo.clear();
                s.filesAt = -1.0e9;
                syncNodeEffects(s.syncName);
                s.syncName.clear();
            } else {
                setStatus(s, error);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    if (!s.hasGraph) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("No effect is open"));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Create a new effect or open one from the list. Create examples adds four ready made effects."));
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }

    const float statusHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    const float sideWidth = 250.0f * std::clamp(ImGui::GetFontSize() / 16.0f, 0.75f, 2.0f);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize(std::max(120.0f, avail.x - sideWidth - ImGui::GetStyle().ItemSpacing.x), std::max(120.0f, avail.y - statusHeight));
    drawCanvas(s, canvasSize, nodesHasFocus_);

    ImGui::SameLine();
    ImGui::BeginChild("##nodeside", ImVec2(0.0f, canvasSize.y), ImGuiChildFlags_Borders);
    ImGui::SeparatorText(tr("Effect controls"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Control 1 to 4 nodes read these values. A control without a label is not shown in the effect."));
    ImGui::PopTextWrapPos();
    for (int i = 0; i < kGraphControls; ++i) {
        ImGui::PushID(i);
        GraphControl& control = s.graph.controls[static_cast<size_t>(i)];
        ImGui::Text("%s %d", tr("Control"), i + 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputLabel("##label", control.label, kMaxGraphLabel, tr("Label"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##default", &control.value, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("%s", tr("Default value of the control"));
        ImGui::PopID();
    }
    const GraphNode* picked = s.graph.find(s.selected);
    const NodeSpec* pickedSpec = picked ? findNodeSpec(picked->type) : nullptr;
    if (pickedSpec) {
        ImGui::SeparatorText(tr("Selected node"));
        ImGui::Text("%s", pickedSpec->title);
        ImGui::TextDisabled("%s", pickedSpec->category);
        if (pickedSpec->reads > 0) {
            ImGui::TextDisabled("%s %d", tr("Texture reads:"), pickedSpec->reads);
        }
    }
    ImGui::SeparatorText(tr("Mouse and keys"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Right click: add a node. Drag from a pin: connect. Drag a connected input away: remove the link. Double click or Ctrl+click a value to type it. Delete: remove the node. Ctrl+D: duplicate. Middle mouse or empty space: move the view."));
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    if (ImGui::GetTime() > s.statusUntil) {
        s.status.clear();
    }
    if (!s.compiled.ok) {
        std::string message = s.compiled.firstError();
        for (const GraphError& item : s.compiled.errors) {
            const GraphNode* node = s.graph.find(item.node);
            const NodeSpec* spec = node ? findNodeSpec(node->type) : nullptr;
            if (spec) {
                message = std::string(spec->title) + ": " + item.message;
                break;
            }
        }
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", message.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "%s", tr("The effect compiles"));
        ImGui::SameLine();
        ImGui::TextDisabled("%d %s", s.compiled.textureReads, tr("texture reads"));
        if (s.compiled.usesPrevious) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("uses the previous frame"));
        }
    }
    if (!s.status.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%s", s.status.c_str());
    } else {
        ImGui::TextDisabled("%s", s.graph == s.saved ? tr("Saved") : tr("Saving..."));
    }

    commitChanges(s);
    if (s.graph != s.saved) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive()) {
            s.saveTimer = 0.0;
        } else {
            s.saveTimer += static_cast<double>(ImGui::GetIO().DeltaTime);
            if (s.saveTimer >= kSaveDelay) {
                writeCurrent(s);
            }
        }
    }
    if (!s.syncName.empty()) {
        syncNodeEffects(s.syncName);
        s.syncName.clear();
    }
    ImGui::End();
}

}

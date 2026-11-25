#pragma once

#include "core/node_compile.hpp"
#include "core/node_graph.hpp"

#include <imgui.h>

#include <string>
#include <vector>

namespace nvj {

constexpr float kNodeZoomMin = 0.4f;
constexpr float kNodeZoomMax = 2.5f;

struct NodeEditorState {
    bool hasGraph = false;
    std::string stem;
    GraphDocument graph;
    GraphDocument committed;
    GraphDocument saved;
    GraphDocument compiledFor;
    CompiledGraph compiled;
    bool compiledValid = false;
    std::vector<GraphDocument> undo;
    std::vector<GraphDocument> redo;
    double saveTimer = 0.0;
    std::string syncName;
    std::string status;
    double statusUntil = 0.0;

    ImVec2 pan{24.0f, 24.0f};
    float zoom = 1.0f;
    ImVec2 canvasSize{0.0f, 0.0f};
    ImVec2 canvasOrigin{0.0f, 0.0f};
    std::vector<int> order;
    int selected = -1;

    int dragNode = -1;
    ImVec2 dragGrab{0.0f, 0.0f};
    bool panning = false;
    ImVec2 panGrab{0.0f, 0.0f};

    bool linking = false;
    bool linkFromOutput = true;
    int linkNode = -1;
    int linkPort = -1;
    bool linkMoved = false;
    bool linkRestore = false;
    GraphLink linkOld;
    ImVec2 linkStart{0.0f, 0.0f};

    int valueNode = -1;
    int valueIndex = -1;
    float valueAccum = 0.0f;
    float valueMouse = 0.0f;
    float valueWidth = 100.0f;

    int editNode = -1;
    int editIndex = -1;
    float editValue = 0.0f;
    int choiceNode = -1;
    int choiceIndex = -1;
    int menuNode = -1;

    bool openAddMenu = false;
    bool openNodeMenu = false;
    bool openTypedValue = false;
    bool openChoiceMenu = false;
    bool openDeleteConfirm = false;
    ImVec2 menuCanvas{0.0f, 0.0f};
    int menuLinkNode = -1;
    int menuLinkPort = -1;
    bool menuLinkFromOutput = true;
    char search[64] = {};

    std::vector<std::string> files;
    double filesAt = -1.0e9;
    char nameBuffer[64] = {};
    char pathBuffer[512] = {};
    bool hadFocus = false;
};

}

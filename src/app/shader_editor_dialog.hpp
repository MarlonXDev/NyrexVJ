#pragma once

#include "core/shader_editor.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nvj {

struct ShaderEditorDialog {
    enum class Action {
        None,
        New,
        Open,
        Close
    };

    enum class Modal {
        None,
        Unsaved,
        Conflict,
        SaveAs
    };

    bool open = false;
    bool focus = false;
    bool ready = false;
    bool editorActive = false;
    bool needSubmit = true;
    bool preview = true;
    bool showFiles = false;

    shaderedit::EditModel model;
    shaderedit::CompileScheduler scheduler;
    std::vector<shaderedit::Diagnostic> diagnostics;
    std::vector<std::vector<shaderedit::Span>> spans;
    std::uint64_t spansRevision = ~0ull;
    std::uint64_t submittedModelRevision = ~0ull;
    std::uint64_t engineRevision = 0;
    std::uint64_t diagnosticsFor = 0;
    size_t longestLine = 0;
    float contentWidth = 0.0f;

    std::string fileName;
    shaderedit::FileStamp loaded;
    long long loadedMtime = 0;
    unsigned long long loadedSize = 0;
    bool diskChanged = false;
    bool diskGone = false;
    double lastDiskCheck = 0.0;
    std::vector<std::string> files;
    bool filesDirty = true;

    Action action = Action::None;
    std::string actionFile;
    int actionTemplate = 0;
    Modal modal = Modal::None;
    bool modalRequested = false;
    char nameBuffer[96] = {};
    bool replaceExisting = false;
    bool conflictDeleted = false;
    bool saveAfterNameThenAction = false;

    std::string status;
    bool statusBad = false;
    double statusTime = 0.0;

    float scrollTargetX = -1.0f;
    float scrollTargetY = -1.0f;
    bool scrollToCursor = false;
    bool mouseSelecting = false;
    double lastClickTime = -10.0;
    size_t lastClickOffset = 0;
    int clickCount = 0;
    double caretStart = 0.0;
    double clock = 0.0;
    double lastTime = -1.0;
    int jumpLine = -1;
    float testTextLeft = 0.0f;
    float testOriginY = 0.0f;
    float testLineHeight = 0.0f;
};

struct ShaderEditorUiTest {
    struct Step {
        std::function<void()> act;
        std::function<bool()> until;
        int frames = 1;
        std::function<void()> check;
    };

    struct Item {
        std::string name;
        std::string detail;
        bool passed = false;
    };

    bool active = false;
    bool started = false;
    std::vector<Step> steps;
    size_t index = 0;
    int waited = 0;
    int totalFrames = 0;
    std::vector<Item> items;
    int failed = 0;
};

}

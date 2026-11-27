#include "app/app.hpp"

#include "app/shader_editor_dialog.hpp"
#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"

#include <glad/gl.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nvj {

namespace {

namespace fs = std::filesystem;
namespace se = shaderedit;

using Dialog = ShaderEditorDialog;

constexpr size_t kMaxDrawColumns = 1500;
constexpr double kDiskCheckSeconds = 1.0;
constexpr size_t kMaxFiles = 256;
constexpr double kStatusSeconds = 6.0;

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);

struct Commands {
    bool save = false;
    bool compileNow = false;
};

ImU32 colorFor(se::TokenKind kind) {
    switch (kind) {
        case se::TokenKind::Keyword: return IM_COL32(255, 160, 80, 255);
        case se::TokenKind::Type: return IM_COL32(110, 200, 235, 255);
        case se::TokenKind::Builtin: return IM_COL32(150, 190, 255, 255);
        case se::TokenKind::Nyrex: return IM_COL32(255, 120, 200, 255);
        case se::TokenKind::Number: return IM_COL32(150, 220, 140, 255);
        case se::TokenKind::Comment: return IM_COL32(120, 130, 120, 255);
        case se::TokenKind::Preprocessor: return IM_COL32(190, 150, 255, 255);
        case se::TokenKind::Directive: return IM_COL32(255, 215, 90, 255);
        case se::TokenKind::Plain: break;
    }
    return IM_COL32(225, 225, 225, 255);
}

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::string expandTabs(std::string_view text, size_t& column) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\t') {
            const size_t count = static_cast<size_t>(se::kTabWidth) - (column % static_cast<size_t>(se::kTabWidth));
            out.append(count, ' ');
            column += count;
        } else if (c != '\r') {
            out.push_back(c);
            if (!continuation(c)) {
                ++column;
            }
        }
    }
    return out;
}

float widthOf(const std::string& text) {
    if (text.empty()) {
        return 0.0f;
    }
    return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

size_t drawLimit(std::string_view line) {
    if (line.size() <= kMaxDrawColumns) {
        return line.size();
    }
    size_t limit = kMaxDrawColumns;
    while (limit > 0 && continuation(line[limit])) {
        --limit;
    }
    return limit;
}

float prefixWidth(std::string_view line, size_t bytes) {
    size_t column = 0;
    const std::string shown = expandTabs(line.substr(0, std::min(bytes, line.size())), column);
    return widthOf(shown);
}

size_t columnAt(std::string_view line, float x) {
    const size_t limit = drawLimit(line);
    if (x <= 0.0f || limit == 0) {
        return 0;
    }
    std::vector<size_t> bounds;
    for (size_t i = 0; i < limit; ++i) {
        if (!continuation(line[i])) {
            bounds.push_back(i);
        }
    }
    bounds.push_back(limit);
    size_t low = 0;
    size_t high = bounds.size() - 1;
    while (low < high) {
        const size_t mid = (low + high) / 2;
        if (prefixWidth(line, bounds[mid]) < x) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low > 0) {
        const float after = prefixWidth(line, bounds[low]);
        const float before = prefixWidth(line, bounds[low - 1]);
        if (x - before < after - x) {
            --low;
        }
    }
    return bounds[low];
}

void say(Dialog& d, const std::string& text, bool bad) {
    d.status = text;
    d.statusBad = bad;
    d.statusTime = ImGui::GetTime();
}

const char* saveErrorText(se::SaveError error) {
    switch (error) {
        case se::SaveError::BadName: return tr("The name is not allowed");
        case se::SaveError::TooLarge: return tr("The text is larger than 256 KiB");
        case se::SaveError::NoFolder: return tr("The shaders folder cannot be used");
        case se::SaveError::AlreadyExists: return tr("A file with this name exists already");
        case se::SaveError::NotAFile: return tr("That is not a normal file");
        case se::SaveError::CannotWrite: return tr("The file cannot be written, maybe another program has it open");
        default: break;
    }
    return tr("The file could not be saved");
}

const char* nameProblemText(se::NameProblem problem) {
    switch (problem) {
        case se::NameProblem::Empty: return tr("Enter a name");
        case se::NameProblem::TooLong: return tr("The name is too long, 64 characters at most");
        case se::NameProblem::BadCharacter: return tr("Use only letters, digits, space and - _ . ( )");
        case se::NameProblem::BadExtension: return tr("End the name with .glsl, .frag or .fs");
        case se::NameProblem::BadStart: return tr("The name must start with a letter or a digit");
        case se::NameProblem::Reserved: return tr("Windows reserves this name");
        default: break;
    }
    return "";
}

void refreshFiles(Dialog& d) {
    d.files = se::listShaderFiles(fs::path(shaderDirectory()), kMaxFiles);
    d.filesDirty = false;
}

void markLoaded(Dialog& d, const se::FileStamp& stamp) {
    d.loaded = stamp;
    d.loadedMtime = stamp.mtime;
    d.loadedSize = stamp.size;
    d.diskChanged = false;
    d.diskGone = false;
}

void restartCompile(Dialog& d) {
    d.scheduler.reset();
    d.needSubmit = true;
    d.diagnostics.clear();
    d.diagnosticsFor = 0;
}

void startNew(Dialog& d, int index) {
    const std::vector<se::Template>& list = se::templates();
    const size_t at = static_cast<size_t>(std::clamp(index, 0, static_cast<int>(list.size()) - 1));
    d.model.setText(list[at].text);
    d.model.markSaved();
    d.fileName.clear();
    d.loaded = se::FileStamp{};
    d.loadedMtime = 0;
    d.loadedSize = 0;
    d.diskChanged = false;
    d.diskGone = false;
    std::snprintf(d.nameBuffer, sizeof(d.nameBuffer), "%s", list[at].fileName);
    d.scrollTargetX = 0.0f;
    d.scrollTargetY = 0.0f;
    d.model.setCursor(0, false);
    restartCompile(d);
    say(d, tr("New shader from a template. It is not saved yet."), false);
}

bool loadFromDisk(Dialog& d, const std::string& name) {
    const se::ReadResult read = se::readShader(fs::path(shaderDirectory()), name);
    if (!read.ok) {
        say(d, read.tooLarge ? tr("The file is larger than 256 KiB and cannot be opened here") : tr("The file could not be read"), true);
        return false;
    }
    d.model.setText(read.text);
    d.model.markSaved();
    d.fileName = name;
    markLoaded(d, read.stamp);
    std::snprintf(d.nameBuffer, sizeof(d.nameBuffer), "%s", name.c_str());
    d.model.setCursor(0, false);
    d.scrollTargetX = 0.0f;
    d.scrollTargetY = 0.0f;
    restartCompile(d);
    say(d, tr("Opened") + std::string(" ") + name, false);
    return true;
}

void performAction(Dialog& d) {
    const Dialog::Action action = d.action;
    d.action = Dialog::Action::None;
    if (action == Dialog::Action::New) {
        startNew(d, d.actionTemplate);
    } else if (action == Dialog::Action::Open) {
        loadFromDisk(d, d.actionFile);
    } else if (action == Dialog::Action::Close) {
        d.open = false;
    }
}

void requestAction(Dialog& d, Dialog::Action action, const std::string& file, int templateIndex) {
    d.action = action;
    d.actionFile = file;
    d.actionTemplate = templateIndex;
    if (d.model.dirty()) {
        d.modal = Dialog::Modal::Unsaved;
        d.modalRequested = true;
        return;
    }
    performAction(d);
}

enum class SaveStep {
    Done,
    NeedName,
    Conflict,
    Failed
};

SaveStep trySave(Dialog& d, bool overwrite) {
    if (d.fileName.empty()) {
        return SaveStep::NeedName;
    }
    const se::SaveResult result = se::saveShader(fs::path(shaderDirectory()), d.fileName, d.model.text(), &d.loaded, overwrite);
    if (result.ok) {
        markLoaded(d, result.stamp);
        d.model.markSaved();
        d.filesDirty = true;
        say(d, tr("Saved") + std::string(" ") + d.fileName, false);
        return SaveStep::Done;
    }
    if (result.error == se::SaveError::ChangedOnDisk || result.error == se::SaveError::DeletedOnDisk) {
        d.conflictDeleted = result.error == se::SaveError::DeletedOnDisk;
        return SaveStep::Conflict;
    }
    say(d, saveErrorText(result.error), true);
    return SaveStep::Failed;
}

void openSaveAs(Dialog& d) {
    if (d.nameBuffer[0] == '\0') {
        std::snprintf(d.nameBuffer, sizeof(d.nameBuffer), "%s", d.fileName.empty() ? "my_shader.glsl" : d.fileName.c_str());
    }
    d.replaceExisting = false;
    d.modal = Dialog::Modal::SaveAs;
    d.modalRequested = true;
}

void beginSave(Dialog& d) {
    const SaveStep step = trySave(d, false);
    if (step == SaveStep::NeedName) {
        openSaveAs(d);
    } else if (step == SaveStep::Conflict) {
        d.modal = Dialog::Modal::Conflict;
        d.modalRequested = true;
    }
}

void checkDisk(Dialog& d, double now) {
    if (d.fileName.empty() || now - d.lastDiskCheck < kDiskCheckSeconds) {
        return;
    }
    d.lastDiskCheck = now;
    const fs::path file = fs::path(shaderDirectory()) / d.fileName;
    const se::FileStamp quick = se::quickStamp(file);
    if (!quick.exists) {
        d.diskGone = d.loaded.exists;
        return;
    }
    d.diskGone = false;
    if (quick.mtime == d.loadedMtime && quick.size == d.loadedSize) {
        return;
    }
    const se::FileStamp full = se::fullStamp(file);
    if (se::sameFile(full, d.loaded)) {
        d.loadedMtime = quick.mtime;
        d.loadedSize = quick.size;
        d.diskChanged = false;
        return;
    }
    if (!d.model.dirty() && !d.diskChanged) {
        const std::string name = d.fileName;
        const se::ReadResult read = se::readShader(fs::path(shaderDirectory()), name);
        if (read.ok) {
            d.model.setText(read.text);
            d.model.markSaved();
            markLoaded(d, read.stamp);
            restartCompile(d);
            say(d, tr("The file changed on disk and was loaded again"), false);
            return;
        }
    }
    d.diskChanged = true;
}

void premultipliedBlend(const ImDrawList*, const ImDrawCmd*) {
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void updateSpans(Dialog& d) {
    if (d.spansRevision == d.model.revision()) {
        return;
    }
    d.spans = se::highlightAll(d.model.text());
    d.spansRevision = d.model.revision();
    d.longestLine = 0;
    size_t longest = 0;
    for (size_t i = 0; i < d.model.lineCount(); ++i) {
        const size_t length = d.model.lineLength(i);
        if (length > longest) {
            longest = length;
            d.longestLine = i;
        }
    }
    const std::string_view text = d.model.lineText(d.longestLine);
    d.contentWidth = prefixWidth(text, drawLimit(text));
}

int markFor(const Dialog& d, size_t line) {
    int mark = 0;
    for (const se::Diagnostic& diagnostic : d.diagnostics) {
        if (diagnostic.line == static_cast<int>(line) + 1) {
            mark = std::max(mark, diagnostic.severity == se::Severity::Error ? 2 : 1);
        }
    }
    return mark;
}

void drawLineText(ImDrawList* list, ImVec2 position, std::string_view line, const std::vector<se::Span>& spans) {
    const size_t limit = drawLimit(line);
    float x = position.x;
    size_t at = 0;
    size_t column = 0;
    const auto emit = [&](size_t from, size_t to, ImU32 color) {
        if (from >= to) {
            return;
        }
        const std::string shown = expandTabs(line.substr(from, to - from), column);
        if (!shown.empty()) {
            list->AddText(ImVec2(x, position.y), color, shown.data(), shown.data() + shown.size());
            x += widthOf(shown);
        }
    };
    for (const se::Span& span : spans) {
        const size_t start = std::max(at, std::min(static_cast<size_t>(span.start), limit));
        const size_t end = std::min(static_cast<size_t>(span.start) + span.length, limit);
        if (start > at) {
            emit(at, start, colorFor(se::TokenKind::Plain));
            at = start;
        }
        if (end > at) {
            emit(at, end, colorFor(span.kind));
            at = end;
        }
        if (at >= limit) {
            break;
        }
    }
    if (at < limit) {
        emit(at, limit, colorFor(se::TokenKind::Plain));
    }
}

bool handleKeys(Dialog& d, double now, size_t pageLines, Commands& commands) {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl && !io.KeyAlt;
    const bool shift = io.KeyShift;
    se::EditModel& model = d.model;
    const std::uint64_t before = model.revision();
    const size_t cursorBefore = model.cursor();
    const size_t anchorBefore = model.anchor();

    std::string typed;
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const unsigned int c = static_cast<unsigned int>(io.InputQueueCharacters[i]);
        if (c < 32 || c == 127 || c == 0xFFFD) {
            continue;
        }
        char buffer[5] = {};
        const int count = ImTextCharToUtf8(buffer, c);
        typed.append(buffer, static_cast<size_t>(count));
    }
    io.InputQueueCharacters.resize(0);
    if (!typed.empty() && !ctrl) {
        model.insertText(typed);
    }

    const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
    const auto navigate = [&](ImGuiKey key, se::Move plain, se::Move withCtrl) {
        if (pressed(key)) {
            model.move(ctrl ? withCtrl : plain, shift, pageLines);
        }
    };
    navigate(ImGuiKey_LeftArrow, se::Move::Left, se::Move::WordLeft);
    navigate(ImGuiKey_RightArrow, se::Move::Right, se::Move::WordRight);
    navigate(ImGuiKey_UpArrow, se::Move::Up, se::Move::Up);
    navigate(ImGuiKey_DownArrow, se::Move::Down, se::Move::Down);
    navigate(ImGuiKey_Home, se::Move::LineStart, se::Move::DocStart);
    navigate(ImGuiKey_End, se::Move::LineEnd, se::Move::DocEnd);
    navigate(ImGuiKey_PageUp, se::Move::PageUp, se::Move::PageUp);
    navigate(ImGuiKey_PageDown, se::Move::PageDown, se::Move::PageDown);

    if (pressed(ImGuiKey_Backspace)) {
        model.backspace(ctrl);
    }
    if (pressed(ImGuiKey_Delete)) {
        model.erase(ctrl);
    }
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        if (ctrl) {
            commands.compileNow = true;
        } else {
            model.newline();
        }
    }
    if (pressed(ImGuiKey_Tab) && !ctrl) {
        model.indent(shift);
    }
    if (ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            model.selectAll();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            model.selectLine(model.positionOf(model.cursor()).line);
        }
        if (pressed(ImGuiKey_Z) && !shift) {
            model.undo();
        }
        if (pressed(ImGuiKey_Y) || (pressed(ImGuiKey_Z) && shift)) {
            model.redo();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && model.hasSelection()) {
            ImGui::SetClipboardText(model.selectedText().c_str());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X, false) && model.hasSelection()) {
            std::string removed;
            if (model.cutSelection(removed)) {
                ImGui::SetClipboardText(removed.c_str());
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            const char* clipboard = ImGui::GetClipboardText();
            if (clipboard && *clipboard) {
                model.insertText(se::sanitizeText(clipboard));
            }
        }
        if (pressed(ImGuiKey_Slash)) {
            model.toggleComment();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            commands.save = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        commands.compileNow = true;
    }

    const bool changed = model.revision() != before;
    if (changed || model.cursor() != cursorBefore || model.anchor() != anchorBefore) {
        d.caretStart = now;
        d.scrollToCursor = true;
    }
    return changed;
}

void drawEditor(Dialog& d, float height, double now, Commands& commands, bool windowBusy) {
    ImGuiIO& io = ImGui::GetIO();
    se::EditModel& model = d.model;
    const float lineHeight = std::floor(ImGui::GetTextLineHeight() + 3.0f);
    const float padding = 6.0f;
    const ImVec2 childSize(0.0f, height);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.08f, 1.0f));
    ImGui::BeginChild("##shaderCode", childSize, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);
    ImGui::PopStyleColor();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        d.editorActive = ImGui::IsWindowHovered() && !windowBusy;
        if (d.editorActive) {
            d.caretStart = now;
        }
    }
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || windowBusy) {
        d.editorActive = false;
    }

    const float viewHeight = ImGui::GetWindowHeight();
    const size_t pageLines = static_cast<size_t>(std::max(1.0f, std::floor(viewHeight / lineHeight) - 1.0f));
    if (d.editorActive) {
        ImGuiContext& context = *ImGui::GetCurrentContext();
        context.PlatformImeData.WantTextInput = true;
        if (handleKeys(d, now, pageLines, commands)) {
            d.scheduler.edited(now);
        }
    }
    updateSpans(d);

    const size_t lines = model.lineCount();
    const int digits = static_cast<int>(std::to_string(lines).size());
    const float gutter = widthOf(std::string(static_cast<size_t>(std::max(digits, 2)), '0')) + 26.0f;
    const ImVec2 windowPosition = ImGui::GetWindowPos();
    const float windowWidth = ImGui::GetWindowWidth();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float contentWidth = gutter + padding + d.contentWidth + 60.0f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 hitSize(std::max(contentWidth, available.x), std::max(static_cast<float>(lines) * lineHeight + 20.0f, available.y));
    ImGui::InvisibleButton("##shaderHit", hitSize, ImGuiButtonFlags_MouseButtonLeft);
    const float textLeft = origin.x + gutter + padding;
    d.testTextLeft = textLeft;
    d.testOriginY = origin.y;
    d.testLineHeight = lineHeight;

    const auto hitTest = [&](ImVec2 mouse) {
        const float row = std::floor((mouse.y - origin.y) / lineHeight);
        const size_t line = static_cast<size_t>(std::clamp(row, 0.0f, static_cast<float>(lines - 1)));
        const std::string_view text = model.lineText(line);
        return model.offsetOf(se::Position{line, columnAt(text, mouse.x - textLeft)});
    };

    if (ImGui::IsItemActivated() && !windowBusy) {
        d.editorActive = true;
        const ImVec2 mouse = io.MousePos;
        const size_t offset = hitTest(mouse);
        if (now - d.lastClickTime < 0.4 && offset + 1 >= d.lastClickOffset && offset <= d.lastClickOffset + 1) {
            d.clickCount = std::min(d.clickCount + 1, 3);
        } else {
            d.clickCount = 1;
        }
        d.lastClickTime = now;
        d.lastClickOffset = offset;
        if (d.clickCount == 1) {
            model.setCursor(offset, io.KeyShift);
            d.mouseSelecting = true;
        } else if (d.clickCount == 2) {
            model.selectWordAt(offset);
            d.mouseSelecting = false;
        } else {
            model.selectLine(model.positionOf(offset).line);
            d.mouseSelecting = false;
        }
        d.caretStart = now;
    }
    if (d.mouseSelecting && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        model.setCursor(hitTest(io.MousePos), true);
        d.caretStart = now;
        d.scrollToCursor = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        d.mouseSelecting = false;
    }

    if (ImGui::BeginPopupContextItem("##shaderContext")) {
        d.editorActive = true;
        if (ImGui::MenuItem(tr("Cut"), "Ctrl+X", false, model.hasSelection())) {
            std::string removed;
            if (model.cutSelection(removed)) {
                ImGui::SetClipboardText(removed.c_str());
                d.scheduler.edited(now);
            }
        }
        if (ImGui::MenuItem(tr("Copy"), "Ctrl+C", false, model.hasSelection())) {
            ImGui::SetClipboardText(model.selectedText().c_str());
        }
        if (ImGui::MenuItem(tr("Paste"), "Ctrl+V")) {
            const char* clipboard = ImGui::GetClipboardText();
            if (clipboard && *clipboard && model.insertText(se::sanitizeText(clipboard))) {
                d.scheduler.edited(now);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Select all"), "Ctrl+A")) {
            model.selectAll();
        }
        ImGui::EndPopup();
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    const float scrollY = ImGui::GetScrollY();
    const size_t first = static_cast<size_t>(std::max(0.0f, std::floor(scrollY / lineHeight)));
    const size_t last = std::min(lines, static_cast<size_t>(std::ceil((scrollY + viewHeight) / lineHeight)) + 1);
    const float gutterX = windowPosition.x + ImGui::GetStyle().WindowBorderSize;
    const float clipLeft = gutterX + gutter;
    const float clipRight = windowPosition.x + windowWidth;
    const std::pair<size_t, size_t> selection = model.selection();
    const size_t cursorLine = model.positionOf(model.cursor()).line;
    const bool stale = d.diagnosticsFor != model.revision();
    const float spaceWidth = widthOf(" ");

    list->PushClipRect(ImVec2(clipLeft, windowPosition.y), ImVec2(clipRight, windowPosition.y + viewHeight), true);
    for (size_t i = first; i < last; ++i) {
        const float y = origin.y + static_cast<float>(i) * lineHeight;
        const std::string_view text = model.lineText(i);
        const int mark = markFor(d, i);
        if (mark > 0) {
            list->AddRectFilled(ImVec2(clipLeft, y), ImVec2(clipRight, y + lineHeight), mark == 2 ? IM_COL32(120, 30, 30, stale ? 60 : 110) : IM_COL32(120, 90, 20, stale ? 50 : 90));
        } else if (i == cursorLine && !model.hasSelection()) {
            list->AddRectFilled(ImVec2(clipLeft, y), ImVec2(clipRight, y + lineHeight), IM_COL32(255, 255, 255, 12));
        }
        if (model.hasSelection()) {
            const size_t lineStart = model.lineStart(i);
            const size_t lineEnd = lineStart + text.size();
            if (selection.second > lineStart && selection.first <= lineEnd) {
                const size_t from = selection.first > lineStart ? selection.first - lineStart : 0;
                const size_t to = std::min(selection.second, lineEnd) - lineStart;
                const float x0 = textLeft + prefixWidth(text, std::min(from, drawLimit(text)));
                float x1 = textLeft + prefixWidth(text, std::min(to, drawLimit(text)));
                if (selection.second > lineEnd) {
                    x1 += spaceWidth;
                }
                list->AddRectFilled(ImVec2(x0, y), ImVec2(std::max(x1, x0 + 2.0f), y + lineHeight), IM_COL32(70, 110, 170, 150));
            }
        }
        drawLineText(list, ImVec2(textLeft, y + 1.0f), text, i < d.spans.size() ? d.spans[i] : std::vector<se::Span>());
    }
    if (d.editorActive && std::fmod(now - d.caretStart, 1.06) < 0.62) {
        const se::Position position = model.positionOf(model.cursor());
        const std::string_view text = model.lineText(position.line);
        if (position.column <= drawLimit(text)) {
            const float x = textLeft + prefixWidth(text, position.column);
            const float y = origin.y + static_cast<float>(position.line) * lineHeight;
            list->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.5f, y + lineHeight), IM_COL32(255, 255, 255, 255));
        }
    }
    list->PopClipRect();

    list->PushClipRect(ImVec2(gutterX, windowPosition.y), ImVec2(clipLeft, windowPosition.y + viewHeight), true);
    list->AddRectFilled(ImVec2(gutterX, windowPosition.y), ImVec2(clipLeft, windowPosition.y + viewHeight), IM_COL32(24, 24, 28, 255));
    const ImVec2 mouse = io.MousePos;
    for (size_t i = first; i < last; ++i) {
        const float y = origin.y + static_cast<float>(i) * lineHeight;
        char number[24];
        std::snprintf(number, sizeof(number), "%zu", i + 1);
        const float numberWidth = ImGui::CalcTextSize(number).x;
        const int mark = markFor(d, i);
        const ImU32 color = mark == 2 ? IM_COL32(255, 110, 100, 255) : (mark == 1 ? IM_COL32(255, 190, 80, 255) : (i == cursorLine ? IM_COL32(220, 220, 220, 255) : IM_COL32(120, 120, 130, 255)));
        list->AddText(ImVec2(clipLeft - 10.0f - numberWidth, y + 1.0f), color, number);
        if (mark > 0) {
            list->AddCircleFilled(ImVec2(gutterX + 8.0f, y + lineHeight * 0.5f), 3.5f, mark == 2 ? IM_COL32(255, 90, 80, 255) : IM_COL32(255, 180, 60, 255));
            if (mouse.x >= gutterX && mouse.x < gutterX + 18.0f && mouse.y >= y && mouse.y < y + lineHeight && ImGui::IsWindowHovered()) {
                ImGui::BeginTooltip();
                for (const se::Diagnostic& diagnostic : d.diagnostics) {
                    if (diagnostic.line == static_cast<int>(i) + 1) {
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                        ImGui::TextUnformatted(trMessage(diagnostic.message).c_str());
                        ImGui::PopTextWrapPos();
                    }
                }
                ImGui::EndTooltip();
            }
        }
    }
    list->PopClipRect();

    if (d.editorActive) {
        const se::Position position = model.positionOf(model.cursor());
        const std::string_view text = model.lineText(position.line);
        const float caretX = textLeft + prefixWidth(text, std::min(position.column, drawLimit(text)));
        const float caretY = origin.y + static_cast<float>(position.line) * lineHeight;
        ImGuiContext& context = *ImGui::GetCurrentContext();
        context.PlatformImeData.WantVisible = true;
        context.PlatformImeData.InputPos = ImVec2(caretX, caretY);
        context.PlatformImeData.InputLineHeight = lineHeight;
        context.PlatformImeData.ViewportId = ImGui::GetWindowViewport()->ID;
        if (d.scrollToCursor) {
            const float top = caretY - origin.y;
            if (top < scrollY) {
                ImGui::SetScrollY(top);
            } else if (top + lineHeight + 4.0f > scrollY + viewHeight - 16.0f) {
                ImGui::SetScrollY(top + lineHeight + 20.0f - viewHeight);
            }
            const float scrollX = ImGui::GetScrollX();
            const float relative = caretX - origin.x;
            const float visibleWidth = windowWidth - gutter - 24.0f;
            if (relative - gutter - padding < scrollX) {
                ImGui::SetScrollX(std::max(0.0f, relative - gutter - padding - 30.0f));
            } else if (relative - gutter > scrollX + visibleWidth) {
                ImGui::SetScrollX(relative - gutter - visibleWidth + 40.0f);
            }
        }
    }
    d.scrollToCursor = false;
    if (d.scrollTargetY >= 0.0f) {
        ImGui::SetScrollY(d.scrollTargetY);
        ImGui::SetScrollX(std::max(0.0f, d.scrollTargetX));
        d.scrollTargetY = -1.0f;
        d.scrollTargetX = -1.0f;
    }
    if (d.jumpLine >= 0) {
        const float top = static_cast<float>(d.jumpLine) * lineHeight;
        ImGui::SetScrollY(std::max(0.0f, top - viewHeight * 0.35f));
        ImGui::SetScrollX(0.0f);
        d.jumpLine = -1;
    }
    ImGui::EndChild();
}

void drawChecker(ImDrawList* list, ImVec2 min, ImVec2 max) {
    const float tile = 12.0f;
    list->PushClipRect(min, max, true);
    list->AddRectFilled(min, max, IM_COL32(58, 58, 62, 255));
    int row = 0;
    for (float y = min.y; y < max.y; y += tile, ++row) {
        int column = row % 2;
        for (float x = min.x; x < max.x; x += tile, ++column) {
            if (column % 2 == 0) {
                list->AddRectFilled(ImVec2(x, y), ImVec2(x + tile, y + tile), IM_COL32(86, 86, 92, 255));
            }
        }
    }
    list->PopClipRect();
}

}

void App::openShaderEditor(const std::string& fileName) {
    Dialog& d = shaderEditor_;
    d.open = true;
    d.focus = true;
    d.needSubmit = true;
    d.scheduler.reset();
    if (!d.ready) {
        startNew(d, 0);
        d.ready = true;
        d.status.clear();
    }
    d.filesDirty = true;
    if (!fileName.empty() && fileName != d.fileName) {
        requestAction(d, Dialog::Action::Open, fileName, 0);
    }
}

void App::drawShaderEditor() {
    Dialog& d = shaderEditor_;
    if (!d.open) {
        d.lastTime = -1.0;
        d.editorActive = false;
        return;
    }
    const double now = ImGui::GetTime();
    if (d.lastTime < 0.0) {
        d.lastTime = now;
    }
    d.clock += std::clamp(now - d.lastTime, 0.0, 0.25);
    d.lastTime = now;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSize(ImVec2(std::min(1180.0f, viewport->Size.x - 60.0f), std::min(740.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ui::setNextWindowSizeConstraints(ImVec2(760.0f, 460.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (d.focus) {
        ImGui::SetNextWindowFocus();
        d.focus = false;
    }
    std::string title = std::string(tr("Shader editor"));
    if (!d.fileName.empty()) {
        title += " - " + d.fileName;
    } else {
        title += " - " + std::string(tr("not saved yet"));
    }
    if (d.model.dirty()) {
        title += " *";
    }
    title += "###shaderEditorWindow";
    bool keepOpen = true;
    if (!ui::beginTool(title.c_str(), &keepOpen, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }
    if (!keepOpen) {
        requestAction(d, Dialog::Action::Close, std::string(), 0);
    }

    Commands commands;
    const bool modalOpen = d.modal != Dialog::Modal::None;

    if (d.preview) {
        engine_.requestShaderPlayground(d.clock);
    }
    const ShaderPlaygroundStatus& playground = engine_.shaderPlayground();
    if (d.needSubmit && d.preview) {
        d.scheduler.reset();
        d.scheduler.requestNow(now);
        d.needSubmit = false;
    }
    if (d.scheduler.inFlight() && playground.attemptedRevision == d.engineRevision && !playground.compiling) {
        d.scheduler.finish(now);
        d.diagnosticsFor = d.submittedModelRevision;
        if (playground.workingRevision == d.engineRevision || playground.filter || playground.error.empty()) {
            d.diagnostics.clear();
        } else {
            d.diagnostics = se::parseCompileLog(playground.error);
        }
    }
    checkDisk(d, now);

    const float footer = ImGui::GetTextLineHeightWithSpacing() * 1.6f + 8.0f;
    const bool disabledByModal = modalOpen;

    ImGui::BeginDisabled(disabledByModal);
    if (ImGui::Button(tr("New"))) {
        ImGui::OpenPopup("##shaderNewMenu");
    }
    if (ImGui::BeginPopup("##shaderNewMenu")) {
        const std::vector<se::Template>& list = se::templates();
        for (size_t i = 0; i < list.size(); ++i) {
            if (ImGui::MenuItem(tr(list[i].title))) {
                requestAction(d, Dialog::Action::New, std::string(), static_cast<int>(i));
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Open"))) {
        refreshFiles(d);
        ImGui::OpenPopup("##shaderOpenMenu");
    }
    if (ImGui::BeginPopup("##shaderOpenMenu")) {
        if (d.files.empty()) {
            ImGui::TextDisabled("%s", tr("No shader files yet"));
        }
        ImGui::BeginChild("##shaderFileList", ImVec2(300.0f, std::min(320.0f, std::max(40.0f, static_cast<float>(d.files.size()) * ImGui::GetTextLineHeightWithSpacing() + 8.0f))), ImGuiChildFlags_None);
        for (const std::string& name : d.files) {
            if (ImGui::Selectable(name.c_str(), name == d.fileName)) {
                requestAction(d, Dialog::Action::Open, name, 0);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Open shaders folder"))) {
            platform::openFolder(shaderDirectory());
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Save"))) {
        commands.save = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Save as..."))) {
        openSaveAs(d);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(d.fileName.empty());
    if (ImGui::Button(tr("Reload from disk"))) {
        loadFromDisk(d, d.fileName);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!d.model.canUndo());
    if (ImGui::Button(tr("Undo"))) {
        if (d.model.undo()) {
            d.scheduler.edited(now);
            d.scrollToCursor = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!d.model.canRedo());
    if (ImGui::Button(tr("Redo"))) {
        if (d.model.redo()) {
            d.scheduler.edited(now);
            d.scrollToCursor = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!d.preview);
    if (ImGui::Button(tr("Compile now"))) {
        commands.compileNow = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("Live preview"), &d.preview)) {
        d.scheduler.reset();
        d.needSubmit = true;
    }
    ImGui::EndDisabled();

    if (d.diskGone || d.diskChanged) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("%s", d.diskGone ? tr("The file was deleted from disk. Saving writes it again.") : tr("The file changed on disk while you have unsaved edits."));
        ImGui::PopStyleColor();
        if (d.diskChanged) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Load the disk version"))) {
                loadFromDisk(d, d.fileName);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Keep my version"))) {
                d.diskChanged = false;
                d.loadedMtime = 0;
                d.loadedSize = ~0ull;
            }
        }
    }

    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float previewColumn = std::clamp(region.x * 0.38f, 260.0f, 460.0f);
    const float editorWidth = region.x - previewColumn - ImGui::GetStyle().ItemSpacing.x;
    const float bodyHeight = region.y - footer;
    const float problemsHeight = std::clamp(bodyHeight * 0.26f, 70.0f, 190.0f);

    ImGui::BeginGroup();
    ImGui::BeginChild("##shaderLeft", ImVec2(editorWidth, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawEditor(d, bodyHeight - problemsHeight - ImGui::GetStyle().ItemSpacing.y, now, commands, disabledByModal);
    ImGui::BeginChild("##shaderProblems", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (d.diagnostics.empty()) {
        ImGui::TextDisabled("%s", tr("No problems reported"));
    } else {
        ImGui::TextDisabled("%s", tr("Click a problem to jump to its line"));
        for (size_t i = 0; i < d.diagnostics.size(); ++i) {
            const se::Diagnostic& diagnostic = d.diagnostics[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, diagnostic.severity == se::Severity::Error ? kBad : kWarn);
            std::string row;
            if (diagnostic.line > 0) {
                row = std::string(tr("Line")) + " " + std::to_string(diagnostic.line) + ": ";
            }
            if (!diagnostic.pass.empty()) {
                row += diagnostic.pass + " - ";
            }
            row += trMessage(diagnostic.message);
            if (ImGui::Selectable(row.c_str(), false) && diagnostic.line > 0) {
                const size_t line = std::min(static_cast<size_t>(diagnostic.line - 1), d.model.lineCount() - 1);
                d.model.setCursor(d.model.offsetOf(se::Position{line, static_cast<size_t>(std::max(diagnostic.column - 1, 0))}), false);
                d.jumpLine = static_cast<int>(line);
                d.editorActive = true;
                d.caretStart = now;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginChild("##shaderRight", ImVec2(previewColumn, bodyHeight), ImGuiChildFlags_None);
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 size(width, std::floor(width * 9.0f / 16.0f));
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        ImDrawList* list = ImGui::GetWindowDrawList();
        const ImVec2 maxCorner(position.x + size.x, position.y + size.y);
        drawChecker(list, position, maxCorner);
        const GLuint texture = d.preview ? engine_.shaderPlaygroundTexture() : 0;
        if (texture) {
            list->AddCallback(premultipliedBlend, nullptr);
            list->AddImage(ui::texId(texture), position, maxCorner, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), IM_COL32_WHITE);
            list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        } else {
            const char* text = d.preview ? tr("No picture yet") : tr("Live preview is off");
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            list->AddText(ImVec2(position.x + (size.x - textSize.x) * 0.5f, position.y + (size.y - textSize.y) * 0.5f), IM_COL32(200, 200, 200, 255), text);
        }
        list->AddRect(position, maxCorner, IM_COL32(120, 120, 130, 255));
    }

    ImGui::Spacing();
    const bool attempted = playground.attemptedRevision == d.engineRevision && d.engineRevision != 0;
    const bool failed = d.preview && attempted && !playground.compiling && !playground.filter && !playground.error.empty() && playground.workingRevision != d.engineRevision;
    const bool compiling = d.preview && (playground.compiling || d.scheduler.inFlight());
    ImGui::PushTextWrapPos(0.0f);
    if (!d.preview) {
        ImGui::TextDisabled("%s", tr("Live preview is off, nothing is compiled."));
    } else if (failed) {
        const int line = se::firstErrorLine(d.diagnostics);
        char text[160];
        if (line > 0) {
            std::snprintf(text, sizeof(text), tr("Error on line %d"), line);
        } else {
            std::snprintf(text, sizeof(text), "%s", tr("The shader does not compile"));
        }
        ImGui::TextColored(kBad, "%s", text);
        if (playground.hasWorking) {
            ImGui::TextDisabled("%s", tr("The last working version is still shown."));
        } else {
            ImGui::TextDisabled("%s", tr("There is no working version yet."));
        }
    } else if (attempted && playground.filter && !compiling) {
        ImGui::TextColored(kWarn, "%s", tr("Filter files are not previewed here. Save the file and use it from the effect list."));
    } else if (compiling) {
        ImGui::TextColored(kWarn, "%s", tr("Compiling..."));
    } else if (d.scheduler.pending()) {
        ImGui::TextDisabled("%s", tr("Waiting until you stop typing..."));
    } else if (playground.hasWorking && playground.workingRevision == d.engineRevision) {
        ImGui::TextColored(kGood, "%s", tr("Compiled"));
    }
    if (d.preview && !playground.runtimeError.empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(playground.runtimeError).c_str());
    }
    if (d.preview && !playground.note.empty()) {
        ImGui::TextColored(kWarn, "%s", trMessage(playground.note).c_str());
    }
    if (d.preview && playground.paused) {
        ImGui::TextColored(kWarn, "%s", tr("The preview stopped because this shader is too slow."));
        if (ImGui::SmallButton(tr("Resume"))) {
            engine_.resumeShaderPlayground();
        }
    } else if (d.preview && playground.throttled) {
        char text[160];
        std::snprintf(text, sizeof(text), tr("The preview is slowed down: this shader needs %.1f ms, only every %d. frame is drawn."), playground.previewMs, playground.interval);
        ImGui::TextColored(kWarn, "%s", text);
        if (ImGui::SmallButton(tr("Resume"))) {
            engine_.resumeShaderPlayground();
        }
    }
    ImGui::PopTextWrapPos();

    if (d.preview && playground.hasWorking) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("Shown version"));
        ImGui::SameLine();
        std::string kind = playground.shadertoy ? tr("Shadertoy style") : tr("NyrexVJ style");
        if (playground.passCount > 1) {
            kind += ", " + std::to_string(playground.passCount) + " " + tr("passes");
        }
        if (playground.alpha) {
            kind += ", " + std::string(tr("alpha output"));
        }
        ImGui::TextUnformatted(kind.c_str());
        char timing[200];
        std::snprintf(timing, sizeof(timing), tr("Compile %.1f ms, preview %.1f ms per drawn frame"), playground.lastCompileMs, playground.previewMs);
        ImGui::TextDisabled("%s", timing);
        ImGui::TextDisabled("%s", playground.parallelCompile ? tr("The graphics driver compiles in the background.") : tr("The graphics driver compiles on the spot, so the program can stutter a moment."));
    }
    ImGui::EndChild();

    const se::Position cursor = d.model.positionOf(d.model.cursor());
    char position[96];
    std::snprintf(position, sizeof(position), tr("Line %d, column %d, %d lines"), static_cast<int>(cursor.line) + 1, static_cast<int>(cursor.column) + 1, static_cast<int>(d.model.lineCount()));
    ImGui::TextDisabled("%s", position);
    ImGui::SameLine();
    if (!d.status.empty() && now - d.statusTime < kStatusSeconds) {
        ImGui::TextColored(d.statusBad ? kBad : kGood, "  %s", d.status.c_str());
    } else {
        ImGui::TextDisabled("  %s", tr("Ctrl+S saves, F5 compiles, Ctrl+/ comments, Tab indents"));
    }

    if (d.modalRequested) {
        const char* id = d.modal == Dialog::Modal::Unsaved ? "###shaderUnsaved" : (d.modal == Dialog::Modal::Conflict ? "###shaderConflict" : "###shaderSaveAs");
        const std::string title = std::string(d.modal == Dialog::Modal::Unsaved ? tr("Unsaved changes") : (d.modal == Dialog::Modal::Conflict ? tr("The file changed on disk") : tr("Save as"))) + id;
        ImGui::OpenPopup(title.c_str());
        d.modalRequested = false;
    }
    if (d.modal == Dialog::Modal::Unsaved) {
        const std::string title = std::string(tr("Unsaved changes")) + "###shaderUnsaved";
        if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(tr("This shader has changes that are not saved."));
            ImGui::Spacing();
            if (ImGui::Button(tr("Save"))) {
                ImGui::CloseCurrentPopup();
                d.modal = Dialog::Modal::None;
                const SaveStep step = trySave(d, false);
                if (step == SaveStep::Done) {
                    performAction(d);
                } else if (step == SaveStep::NeedName) {
                    d.saveAfterNameThenAction = true;
                    openSaveAs(d);
                } else if (step == SaveStep::Conflict) {
                    d.saveAfterNameThenAction = true;
                    d.modal = Dialog::Modal::Conflict;
                    d.modalRequested = true;
                } else {
                    d.action = Dialog::Action::None;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Discard"))) {
                d.model.markSaved();
                d.modal = Dialog::Modal::None;
                ImGui::CloseCurrentPopup();
                performAction(d);
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                d.action = Dialog::Action::None;
                d.modal = Dialog::Modal::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else if (d.modal == Dialog::Modal::Conflict) {
        const std::string title = std::string(tr("The file changed on disk")) + "###shaderConflict";
        if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(d.conflictDeleted ? tr("The file was deleted from disk after you opened it.") : tr("Another program changed this file after you opened it. Saving now would overwrite that change."));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button(d.conflictDeleted ? tr("Write the file again") : tr("Overwrite"))) {
                const SaveStep step = trySave(d, true);
                d.modal = Dialog::Modal::None;
                ImGui::CloseCurrentPopup();
                if (step == SaveStep::Done && d.saveAfterNameThenAction) {
                    d.saveAfterNameThenAction = false;
                    performAction(d);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Load the disk version"))) {
                d.modal = Dialog::Modal::None;
                d.saveAfterNameThenAction = false;
                d.action = Dialog::Action::None;
                ImGui::CloseCurrentPopup();
                loadFromDisk(d, d.fileName);
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Save as a copy..."))) {
                d.modal = Dialog::Modal::None;
                ImGui::CloseCurrentPopup();
                std::string copy = d.fileName;
                const size_t dot = copy.rfind('.');
                copy = dot == std::string::npos ? copy + "_copy" : copy.substr(0, dot) + "_copy" + copy.substr(dot);
                std::snprintf(d.nameBuffer, sizeof(d.nameBuffer), "%s", copy.c_str());
                openSaveAs(d);
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                d.modal = Dialog::Modal::None;
                d.saveAfterNameThenAction = false;
                d.action = Dialog::Action::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else if (d.modal == Dialog::Modal::SaveAs) {
        const std::string title = std::string(tr("Save as")) + "###shaderSaveAs";
        if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (d.filesDirty) {
                refreshFiles(d);
            }
            ImGui::TextUnformatted(tr("File name"));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            const bool enterPressed = ImGui::InputText("##shaderName", d.nameBuffer, sizeof(d.nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            const se::NameCheck check = se::validateShaderFileName(d.nameBuffer);
            bool exists = false;
            if (check.ok) {
                for (const std::string& name : d.files) {
                    exists = exists || name == check.name;
                }
                ImGui::TextDisabled("%s", (std::string(tr("Will be saved as")) + " " + check.name).c_str());
            } else {
                ImGui::TextColored(kBad, "%s", nameProblemText(check.problem));
            }
            if (exists) {
                ImGui::TextColored(kWarn, "%s", tr("A file with this name exists already."));
                ImGui::Checkbox(tr("Replace the existing file"), &d.replaceExisting);
            }
            ImGui::TextDisabled("%s", shaderDirectory().c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(!check.ok || (exists && !d.replaceExisting));
            const bool confirm = ImGui::Button(tr("Save")) || (enterPressed && check.ok && (!exists || d.replaceExisting));
            ImGui::EndDisabled();
            if (confirm) {
                const se::SaveResult result = se::saveShader(fs::path(shaderDirectory()), check.name, d.model.text(), nullptr, d.replaceExisting);
                if (result.ok) {
                    d.fileName = result.name;
                    markLoaded(d, result.stamp);
                    d.model.markSaved();
                    d.filesDirty = true;
                    say(d, tr("Saved") + std::string(" ") + result.name, false);
                    d.modal = Dialog::Modal::None;
                    ImGui::CloseCurrentPopup();
                    if (d.saveAfterNameThenAction) {
                        d.saveAfterNameThenAction = false;
                        performAction(d);
                    }
                } else {
                    say(d, saveErrorText(result.error), true);
                    d.filesDirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                d.modal = Dialog::Modal::None;
                d.saveAfterNameThenAction = false;
                d.action = Dialog::Action::None;
                ImGui::CloseCurrentPopup();
            }
            if (!d.status.empty() && d.statusBad && now - d.statusTime < kStatusSeconds) {
                ImGui::TextColored(kBad, "%s", d.status.c_str());
            }
            ImGui::EndPopup();
        }
    }

    if (commands.save && !disabledByModal) {
        beginSave(d);
    }
    if (commands.compileNow && d.preview) {
        d.scheduler.requestNow(now);
    }
    if (d.preview && d.scheduler.due(now) && !d.scheduler.inFlight() && !playground.compiling) {
        ++d.engineRevision;
        engine_.submitShaderPlayground(d.model.text(), d.engineRevision);
        d.submittedModelRevision = d.model.revision();
        d.scheduler.begin(now);
    }
    if (!d.open) {
        d.editorActive = false;
        d.scheduler.reset();
        d.needSubmit = true;
    }
    ImGui::End();
}

}

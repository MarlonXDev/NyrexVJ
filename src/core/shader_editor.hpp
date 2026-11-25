#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nvj::shaderedit {

constexpr size_t kMaxTextBytes = 256u * 1024u;
constexpr size_t kMaxUndoSteps = 400;
constexpr size_t kMaxUndoBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxFileNameBytes = 64;
constexpr size_t kMaxDiagnostics = 200;
constexpr size_t kMaxDiagnosticBytes = 400;
constexpr int kTabWidth = 4;

std::string sanitizeText(std::string_view input);

enum class Move {
    Left,
    Right,
    Up,
    Down,
    LineStart,
    LineEnd,
    DocStart,
    DocEnd,
    WordLeft,
    WordRight,
    PageUp,
    PageDown
};

struct Position {
    size_t line = 0;
    size_t column = 0;
};

class EditModel {
public:
    EditModel();

    void setText(std::string_view text);
    const std::string& text() const { return text_; }
    size_t size() const { return text_.size(); }

    size_t lineCount() const { return starts_.size(); }
    size_t lineStart(size_t line) const;
    size_t lineLength(size_t line) const;
    std::string_view lineText(size_t line) const;
    size_t lineOfOffset(size_t offset) const;
    Position positionOf(size_t offset) const;
    size_t offsetOf(Position position) const;

    size_t cursor() const { return cursor_; }
    size_t anchor() const { return anchor_; }
    bool hasSelection() const { return cursor_ != anchor_; }
    std::pair<size_t, size_t> selection() const;
    std::string selectedText() const;

    void setCursor(size_t offset, bool extend);
    void selectAll();
    void selectWordAt(size_t offset);
    void selectLine(size_t line);
    void move(Move movement, bool extend, size_t pageLines = 10);

    bool insertText(std::string_view text);
    bool newline();
    bool backspace(bool word);
    bool erase(bool word);
    bool indent(bool outdent);
    bool toggleComment();
    bool cutSelection(std::string& removed);

    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoDepth() const { return undo_.size(); }

    bool dirty() const;
    void markSaved();
    uint64_t revision() const { return revision_; }

private:
    enum class Kind {
        Other,
        Typing,
        Erasing
    };

    struct Edit {
        size_t position = 0;
        std::string removed;
        std::string inserted;
        size_t cursorBefore = 0;
        size_t anchorBefore = 0;
        size_t cursorAfter = 0;
        size_t anchorAfter = 0;
        Kind kind = Kind::Other;
    };

    void rebuildLines();
    bool apply(size_t position, size_t removeLength, std::string_view inserted, size_t cursorAfter, Kind kind);
    void replaceRaw(size_t position, size_t removeLength, std::string_view inserted);
    void trimHistory();
    size_t stepLeft(size_t offset) const;
    size_t stepRight(size_t offset) const;
    size_t wordLeft(size_t offset) const;
    size_t wordRight(size_t offset) const;

    std::string text_;
    std::vector<size_t> starts_;
    size_t cursor_ = 0;
    size_t anchor_ = 0;
    size_t wantedColumn_ = 0;
    bool keepColumn_ = false;
    bool breakGroup_ = true;
    std::vector<Edit> undo_;
    std::vector<Edit> redo_;
    size_t undoBytes_ = 0;
    uint64_t revision_ = 0;
    unsigned long long savedHash_ = 0;
    size_t savedSize_ = 0;
    mutable uint64_t checkedRevision_ = ~0ull;
    mutable bool dirtyValue_ = false;
};

enum class TokenKind : uint8_t {
    Plain,
    Keyword,
    Type,
    Builtin,
    Nyrex,
    Number,
    Comment,
    Preprocessor,
    Directive
};

struct Span {
    uint32_t start = 0;
    uint32_t length = 0;
    TokenKind kind = TokenKind::Plain;
};

std::vector<Span> highlightLine(std::string_view line, bool& inBlockComment);
std::vector<std::vector<Span>> highlightAll(std::string_view text);

enum class Severity {
    Error,
    Warning
};

struct Diagnostic {
    int line = 0;
    int column = 0;
    Severity severity = Severity::Error;
    std::string pass;
    std::string message;
};

std::vector<Diagnostic> parseCompileLog(std::string_view log);
int firstErrorLine(const std::vector<Diagnostic>& diagnostics);

class CompileScheduler {
public:
    struct Timing {
        double quietSeconds = 0.6;
        double minIntervalSeconds = 0.35;
        double maxWaitSeconds = 2.5;
    };

    CompileScheduler() = default;
    explicit CompileScheduler(Timing timing) : timing_(timing) {}

    void edited(double now);
    void requestNow(double now);
    bool due(double now) const;
    void begin(double now);
    void finish(double now);
    double secondsUntilDue(double now) const;
    bool pending() const { return pending_; }
    bool inFlight() const { return inFlight_; }
    void reset();

private:
    Timing timing_;
    bool pending_ = false;
    bool inFlight_ = false;
    bool forced_ = false;
    double firstEdit_ = 0.0;
    double lastEdit_ = 0.0;
    double lastStart_ = -1.0e9;
};

class PreviewGovernor {
public:
    enum class State {
        Running,
        Throttled,
        Paused
    };

    struct Config {
        double slowMs = 20.0;
        double pauseMs = 120.0;
        double instantPauseMs = 360.0;
        int minReports = 3;
        int maxInterval = 15;
    };

    PreviewGovernor() = default;
    explicit PreviewGovernor(Config config) : config_(config) {}

    void report(double milliseconds);
    bool shouldDraw();
    void resume();
    void reset();
    State state() const { return state_; }
    int interval() const { return interval_; }
    double averageMs() const { return average_; }
    int reports() const { return reports_; }

private:
    Config config_;
    State state_ = State::Running;
    int interval_ = 1;
    int counter_ = 0;
    int reports_ = 0;
    double average_ = 0.0;
};

enum class NameProblem {
    None,
    Empty,
    TooLong,
    BadCharacter,
    BadExtension,
    BadStart,
    Reserved
};

struct NameCheck {
    bool ok = false;
    NameProblem problem = NameProblem::None;
    std::string name;
};

NameCheck validateShaderFileName(std::string_view input);

struct FileStamp {
    bool exists = false;
    long long mtime = 0;
    unsigned long long size = 0;
    unsigned long long hash = 0;
};

unsigned long long hashText(std::string_view text);
FileStamp quickStamp(const std::filesystem::path& file);
FileStamp fullStamp(const std::filesystem::path& file);
bool sameFile(const FileStamp& a, const FileStamp& b);

enum class SaveError {
    None,
    BadName,
    TooLarge,
    NoFolder,
    ChangedOnDisk,
    DeletedOnDisk,
    AlreadyExists,
    NotAFile,
    CannotWrite
};

struct SaveResult {
    bool ok = false;
    SaveError error = SaveError::None;
    std::string name;
    FileStamp stamp;
};

SaveResult saveShader(const std::filesystem::path& folder, std::string_view fileName, std::string_view text, const FileStamp* loaded, bool overwrite);

struct ReadResult {
    bool ok = false;
    bool tooLarge = false;
    std::string text;
    FileStamp stamp;
};

ReadResult readShader(const std::filesystem::path& folder, std::string_view fileName);
std::vector<std::string> listShaderFiles(const std::filesystem::path& folder, size_t limit);

struct Template {
    const char* title;
    const char* fileName;
    const char* text;
};

const std::vector<Template>& templates();

}

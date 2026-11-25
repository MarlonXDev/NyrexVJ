#include "check.hpp"

#include "core/shader_editor.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace nvj::shaderedit;

namespace {

namespace fs = std::filesystem;

struct Random {
    unsigned long long state = 88172645463325252ull;

    unsigned next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>(state >> 33);
    }

    unsigned below(unsigned n) { return n == 0 ? 0 : next() % n; }
};

size_t countLines(const std::string& text) {
    size_t n = 1;
    for (char c : text) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

double seconds(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
}

TokenKind kindAt(const std::vector<Span>& spans, size_t position) {
    for (const Span& span : spans) {
        if (position >= span.start && position < span.start + span.length) {
            return span.kind;
        }
    }
    return TokenKind::Plain;
}

void testSanitize() {
    CHECK(sanitizeText("a\r\nb\rc\nd") == "a\nb\nc\nd");
    CHECK(sanitizeText(std::string("a\0b", 3)) == "ab");
    CHECK(sanitizeText("a\x01\x1f" "b\x7f" "c") == "abc");
    CHECK(sanitizeText("tab\there") == "tab\there");
    CHECK(sanitizeText("m\xC3\xBC" "ll") == "m\xC3\xBC" "ll");
    CHECK(sanitizeText("\xC0\x80") == "??");
    CHECK(sanitizeText("a\xFF" "b") == "a?b");
    CHECK(sanitizeText("\xE2\x82") == "??");
    CHECK(sanitizeText("\xED\xA0\x80") == "???");
    CHECK(sanitizeText("\xF0\x9F\x98\x80") == "\xF0\x9F\x98\x80");
    CHECK(sanitizeText("\xF4\x90\x80\x80") == "????");
    CHECK(sanitizeText("").empty());
}

void testLines() {
    EditModel m;
    CHECK(m.lineCount() == 1);
    CHECK(m.lineLength(0) == 0);
    m.setText("ab\ncde\n\nf");
    CHECK(m.lineCount() == 4);
    CHECK(m.lineText(0) == "ab");
    CHECK(m.lineText(1) == "cde");
    CHECK(m.lineText(2).empty());
    CHECK(m.lineText(3) == "f");
    CHECK(m.lineStart(1) == 3);
    CHECK(m.lineOfOffset(0) == 0);
    CHECK(m.lineOfOffset(2) == 0);
    CHECK(m.lineOfOffset(3) == 1);
    CHECK(m.lineOfOffset(100) == 3);
    CHECK(m.positionOf(5).line == 1);
    CHECK(m.positionOf(5).column == 2);
    CHECK(m.offsetOf(Position{1, 99}) == 6);
    CHECK(m.offsetOf(Position{99, 0}) == 8);
    m.setText("a\nb\n");
    CHECK(m.lineCount() == 3);
    CHECK(m.lineText(2).empty());
    m.setText("x\r\ny");
    CHECK(m.text() == "x\ny");
    CHECK(!m.dirty());
}

void testInsertAndUndo() {
    EditModel m;
    m.setText("hello");
    m.setCursor(5, false);
    CHECK(m.insertText("!"));
    CHECK(m.text() == "hello!");
    CHECK(m.dirty());
    CHECK(m.cursor() == 6);
    CHECK(m.undo());
    CHECK(m.text() == "hello");
    CHECK(!m.dirty());
    CHECK(m.cursor() == 5);
    CHECK(m.redo());
    CHECK(m.text() == "hello!");
    CHECK(!m.canRedo());

    m.setText("");
    for (char c : std::string("abcdef")) {
        m.insertText(std::string(1, c));
    }
    CHECK(m.text() == "abcdef");
    CHECK(m.undoDepth() == 1);
    CHECK(m.undo());
    CHECK(m.text().empty());
    CHECK(m.redo());
    CHECK(m.text() == "abcdef");

    m.insertText("x");
    m.setCursor(0, false);
    m.insertText("y");
    CHECK(m.text() == "yabcdefx");
    CHECK(m.undoDepth() == 3);
    m.undo();
    CHECK(m.text() == "abcdefx");
    m.undo();
    CHECK(m.text() == "abcdef");

    m.insertText("Z");
    CHECK(!m.canRedo());

    m.setText("one two");
    m.setCursor(0, false);
    m.setCursor(3, true);
    CHECK(m.selectedText() == "one");
    CHECK(m.insertText("1"));
    CHECK(m.text() == "1 two");
    CHECK(m.undo());
    CHECK(m.text() == "one two");
    CHECK(m.selectedText() == "one");

    m.setText("abc");
    m.setCursor(3, false);
    m.backspace(false);
    m.backspace(false);
    CHECK(m.text() == "a");
    CHECK(m.undoDepth() == 1);
    m.undo();
    CHECK(m.text() == "abc");
    CHECK(m.cursor() == 3);
    m.setCursor(0, false);
    m.erase(false);
    m.erase(false);
    CHECK(m.text() == "c");
    m.undo();
    CHECK(m.text() == "abc");

    m.setText("abc");
    m.setCursor(1, false);
    m.insertText("1");
    m.markSaved();
    CHECK(!m.dirty());
    m.insertText("2");
    CHECK(m.dirty());
    m.undo();
    CHECK(!m.dirty());
    m.undo();
    CHECK(m.dirty());
    m.redo();
    CHECK(!m.dirty());
}

void testLimits() {
    EditModel m;
    m.setText(std::string(kMaxTextBytes - 2, 'a'));
    m.setCursor(m.size(), false);
    CHECK(m.insertText("bc"));
    CHECK(m.size() == kMaxTextBytes);
    CHECK(!m.insertText("d"));
    CHECK(m.size() == kMaxTextBytes);
    CHECK(!m.newline());
    std::string big(kMaxTextBytes + 500, 'x');
    m.setText(big);
    CHECK(m.size() == kMaxTextBytes);

    EditModel h;
    h.setText("");
    for (int i = 0; i < 1000; ++i) {
        h.setCursor(h.size(), false);
        h.insertText("a");
        h.setCursor(0, false);
    }
    CHECK(h.undoDepth() <= kMaxUndoSteps);
    CHECK(h.undoDepth() > 0);
    for (int i = 0; i < 2000; ++i) {
        h.undo();
    }
    CHECK(!h.canUndo());
    CHECK(h.size() >= 600);

    EditModel b;
    b.setText("");
    const std::string chunk(200000, 'q');
    for (int i = 0; i < 20; ++i) {
        b.setCursor(0, false);
        b.setCursor(b.size(), true);
        b.insertText(chunk);
    }
    CHECK(b.size() == chunk.size());
    CHECK(b.undoDepth() >= 1);
    CHECK(b.undoDepth() < 20);
}

void testNewlineAndBackspace() {
    EditModel m;
    m.setText("    foo");
    m.setCursor(m.size(), false);
    m.newline();
    CHECK(m.text() == "    foo\n    ");
    CHECK(m.cursor() == m.size());

    m.setText("void f() {");
    m.setCursor(m.size(), false);
    m.newline();
    CHECK(m.text() == "void f() {\n    ");

    m.setText("a {}");
    m.setCursor(3, false);
    m.newline();
    CHECK(m.text() == "a {\n    \n}");
    CHECK(m.cursor() == 8);
    m.undo();
    CHECK(m.text() == "a {}");

    m.setText("ab cd");
    m.setCursor(2, false);
    m.setCursor(4, true);
    m.newline();
    CHECK(m.text() == "ab\nd");

    m.setText("        x");
    m.setCursor(8, false);
    m.backspace(false);
    CHECK(m.text() == "    x");
    m.backspace(false);
    CHECK(m.text() == "x");
    m.backspace(false);
    CHECK(m.text() == "x");

    m.setText("abc  def");
    m.setCursor(8, false);
    m.backspace(true);
    CHECK(m.text() == "abc  ");
    m.erase(true);
    CHECK(m.text() == "abc  ");
    m.setCursor(0, false);
    m.erase(true);
    CHECK(m.text() == "  ");

    m.setText("m\xC3\xBC" "ll");
    m.setCursor(3, false);
    m.backspace(false);
    CHECK(m.text() == "mll");
    m.setText("\xC3\xA4x");
    m.setCursor(0, false);
    m.erase(false);
    CHECK(m.text() == "x");
}

void testMovement() {
    EditModel m;
    m.setText("long line here\nab\nlonger line again");
    m.setCursor(10, false);
    m.move(Move::Down, false);
    CHECK(m.positionOf(m.cursor()).line == 1);
    CHECK(m.positionOf(m.cursor()).column == 2);
    m.move(Move::Down, false);
    CHECK(m.positionOf(m.cursor()).line == 2);
    CHECK(m.positionOf(m.cursor()).column == 10);
    m.move(Move::Up, false);
    m.move(Move::Up, false);
    CHECK(m.positionOf(m.cursor()).column == 10);
    m.move(Move::Up, false);
    CHECK(m.cursor() == 0);
    m.move(Move::Down, false, 1);
    m.move(Move::DocEnd, false);
    CHECK(m.cursor() == m.size());
    m.move(Move::Down, false);
    CHECK(m.cursor() == m.size());
    m.move(Move::DocStart, false);
    CHECK(m.cursor() == 0);
    m.move(Move::LineEnd, false);
    CHECK(m.cursor() == 14);
    m.move(Move::LineStart, false);
    CHECK(m.cursor() == 0);
    m.move(Move::PageDown, false, 2);
    CHECK(m.positionOf(m.cursor()).line == 2);
    m.move(Move::PageUp, false, 5);
    CHECK(m.positionOf(m.cursor()).line == 0);

    m.setText("foo.bar  baz(1)");
    m.setCursor(0, false);
    m.move(Move::WordRight, false);
    CHECK(m.cursor() == 3);
    m.move(Move::WordRight, false);
    CHECK(m.cursor() == 4);
    m.move(Move::WordRight, false);
    CHECK(m.cursor() == 7);
    m.move(Move::WordRight, false);
    CHECK(m.cursor() == 12);
    m.move(Move::WordLeft, false);
    CHECK(m.cursor() == 9);
    m.move(Move::WordLeft, false);
    CHECK(m.cursor() == 4);

    m.setText("    indented");
    m.setCursor(10, false);
    m.move(Move::LineStart, false);
    CHECK(m.cursor() == 4);
    m.move(Move::LineStart, false);
    CHECK(m.cursor() == 0);
    m.move(Move::LineStart, false);
    CHECK(m.cursor() == 4);

    m.setText("abcdef");
    m.setCursor(1, false);
    m.move(Move::Right, true);
    m.move(Move::Right, true);
    CHECK(m.selectedText() == "bc");
    m.move(Move::Left, false);
    CHECK(m.cursor() == 1);
    CHECK(!m.hasSelection());
    m.setCursor(1, false);
    m.move(Move::Right, true);
    m.move(Move::Right, true);
    m.move(Move::Right, false);
    CHECK(m.cursor() == 3);

    m.setText("\xC3\xA4\xC3\xB6");
    m.setCursor(0, false);
    m.move(Move::Right, false);
    CHECK(m.cursor() == 2);
    m.move(Move::Right, false);
    CHECK(m.cursor() == 4);
    m.move(Move::Left, false);
    CHECK(m.cursor() == 2);
    m.setCursor(1, false);
    CHECK(m.cursor() == 0);

    m.setText("one two three");
    m.selectWordAt(5);
    CHECK(m.selectedText() == "two");
    m.selectLine(0);
    CHECK(m.selectedText() == "one two three");
    m.selectAll();
    CHECK(m.selectedText() == "one two three");
    std::string removed;
    m.setCursor(0, false);
    m.setCursor(3, true);
    CHECK(m.cutSelection(removed));
    CHECK(removed == "one");
    CHECK(m.text() == " two three");
    CHECK(!m.cutSelection(removed));
}

void testIndentAndComment() {
    EditModel m;
    m.setText("a\nb\nc");
    m.setCursor(0, false);
    m.setCursor(m.size(), true);
    CHECK(m.indent(false));
    CHECK(m.text() == "    a\n    b\n    c");
    CHECK(m.indent(true));
    CHECK(m.text() == "a\nb\nc");
    CHECK(!m.indent(true));
    m.undo();
    CHECK(m.text() == "    a\n    b\n    c");

    m.setText("x\n\ny");
    m.selectAll();
    m.indent(false);
    CHECK(m.text() == "    x\n\n    y");

    m.setText("ab");
    m.setCursor(1, false);
    m.indent(false);
    CHECK(m.text() == "a   b");
    CHECK(m.cursor() == 4);

    m.setText("        z");
    m.setCursor(9, false);
    m.indent(true);
    CHECK(m.text() == "    z");
    CHECK(m.cursor() == 5);
    CHECK(!m.hasSelection());

    m.setText("\tq");
    m.setCursor(2, false);
    m.indent(true);
    CHECK(m.text() == "q");

    m.setText("a\nb");
    m.setCursor(0, false);
    m.setCursor(3, true);
    m.toggleComment();
    CHECK(m.text() == "// a\n// b");
    m.toggleComment();
    CHECK(m.text() == "a\nb");

    m.setText("    a\n\n  b");
    m.selectAll();
    m.toggleComment();
    CHECK(m.text() == "  //   a\n\n  // b");
    m.toggleComment();
    CHECK(m.text() == "    a\n\n  b");

    m.setText("code();");
    m.setCursor(2, false);
    m.toggleComment();
    CHECK(m.text() == "// code();");
    CHECK(m.cursor() == 5);
    m.toggleComment();
    CHECK(m.text() == "code();");
    CHECK(m.cursor() == 2);

    m.setText("\n\n");
    m.selectAll();
    CHECK(!m.toggleComment());
}

void testHighlight() {
    bool block = false;
    std::vector<Span> spans = highlightLine("float x = 1.5e-3; // note", block);
    CHECK(kindAt(spans, 0) == TokenKind::Type);
    CHECK(kindAt(spans, 6) == TokenKind::Plain);
    CHECK(kindAt(spans, 10) == TokenKind::Number);
    CHECK(kindAt(spans, 14) == TokenKind::Number);
    CHECK(kindAt(spans, 19) == TokenKind::Comment);
    CHECK(!block);

    spans = highlightLine("if (x) return sin(uTime) * mod(1., .5);", block);
    CHECK(kindAt(spans, 0) == TokenKind::Keyword);
    CHECK(kindAt(spans, 4) == TokenKind::Plain);
    CHECK(kindAt(spans, 7) == TokenKind::Keyword);
    CHECK(kindAt(spans, 14) == TokenKind::Builtin);
    CHECK(kindAt(spans, 18) == TokenKind::Nyrex);
    CHECK(kindAt(spans, 27) == TokenKind::Builtin);
    CHECK(kindAt(spans, 31) == TokenKind::Number);
    CHECK(kindAt(spans, 35) == TokenKind::Number);

    spans = highlightLine("code /* a */ more /* open", block);
    CHECK(kindAt(spans, 0) == TokenKind::Plain);
    CHECK(kindAt(spans, 6) == TokenKind::Comment);
    CHECK(kindAt(spans, 14) == TokenKind::Plain);
    CHECK(kindAt(spans, 20) == TokenKind::Comment);
    CHECK(block);
    spans = highlightLine("still inside */ float y;", block);
    CHECK(kindAt(spans, 3) == TokenKind::Comment);
    CHECK(kindAt(spans, 16) == TokenKind::Type);
    CHECK(!block);
    block = true;
    spans = highlightLine("all comment", block);
    CHECK(block);
    CHECK(kindAt(spans, 5) == TokenKind::Comment);
    block = false;

    spans = highlightLine("#pragma nyrexvj alpha", block);
    CHECK(kindAt(spans, 0) == TokenKind::Directive);
    CHECK(kindAt(spans, 20) == TokenKind::Directive);
    spans = highlightLine("  #PRAGMA NyrexVJ buffer a // x", block);
    CHECK(kindAt(spans, 4) == TokenKind::Directive);
    CHECK(kindAt(spans, 28) == TokenKind::Comment);
    spans = highlightLine("#define A 2", block);
    CHECK(kindAt(spans, 3) == TokenKind::Preprocessor);
    spans = highlightLine("#version 330 core", block);
    CHECK(kindAt(spans, 1) == TokenKind::Preprocessor);
    spans = highlightLine("x = #;", block);
    CHECK(kindAt(spans, 4) == TokenKind::Plain);

    spans = highlightLine("vec3x v = vec3(0.0); gl_FragCoord; v2", block);
    CHECK(kindAt(spans, 0) == TokenKind::Plain);
    CHECK(kindAt(spans, 10) == TokenKind::Type);
    CHECK(kindAt(spans, 21) == TokenKind::Builtin);
    CHECK(kindAt(spans, 35) == TokenKind::Plain);

    spans = highlightLine("a = 0xFF + 1e+5 + 2.0f;", block);
    CHECK(kindAt(spans, 5) == TokenKind::Number);
    CHECK(kindAt(spans, 9) == TokenKind::Plain);
    CHECK(kindAt(spans, 12) == TokenKind::Number);
    CHECK(kindAt(spans, 14) == TokenKind::Number);
    CHECK(kindAt(spans, 18) == TokenKind::Number);

    const std::string line = "float a = 1.0; // c";
    spans = highlightLine(line, block);
    unsigned covered = 0;
    for (const Span& span : spans) {
        CHECK(span.start == covered);
        covered += span.length;
    }
    CHECK(covered == line.size());
    CHECK(highlightLine("", block).empty());

    const std::vector<std::vector<Span>> all = highlightAll("/* a\nb */ int x;\nfloat y;\n");
    CHECK(all.size() == 4);
    CHECK(kindAt(all[1], 0) == TokenKind::Comment);
    CHECK(kindAt(all[1], 6) == TokenKind::Type);
    CHECK(kindAt(all[2], 0) == TokenKind::Type);
    CHECK(highlightAll("").size() == 1);

    EditModel m;
    m.setText("a\nb\n\nc");
    CHECK(highlightAll(m.text()).size() == m.lineCount());
}

void testLogParser() {
    std::vector<Diagnostic> d = parseCompileLog("0:12(5): error: 'foo' undeclared\n0:20(1): warning: unused\n");
    CHECK(d.size() == 2);
    CHECK(d[0].line == 12);
    CHECK(d[0].column == 5);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[0].message == "'foo' undeclared");
    CHECK(d[1].line == 20);
    CHECK(d[1].severity == Severity::Warning);

    d = parseCompileLog("0(7) : error C1008: undefined variable \"x\"\n0(9) : warning C7050: \"y\" might be used before being initialized");
    CHECK(d.size() == 2);
    CHECK(d[0].line == 7);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[0].message == "undefined variable \"x\"");
    CHECK(d[1].line == 9);
    CHECK(d[1].severity == Severity::Warning);

    d = parseCompileLog("ERROR: 0:33: 'z' : undeclared identifier\nWARNING: 0:34: 'a' : something\n");
    CHECK(d.size() == 2);
    CHECK(d[0].line == 33);
    CHECK(d[0].message == "'z' : undeclared identifier");
    CHECK(d[1].severity == Severity::Warning);
    CHECK(d[1].line == 34);

    d = parseCompileLog("Buffer B:\n0:5(2): error: bad\nImage:\n0:9(1): error: worse\n");
    CHECK(d.size() == 2);
    CHECK(d[0].pass == "Buffer B");
    CHECK(d[0].line == 5);
    CHECK(d[1].pass == "Image");
    CHECK(d[1].line == 9);
    CHECK(firstErrorLine(d) == 5);

    d = parseCompileLog("Line 14: Unknown directive 'bufer'");
    CHECK(d.size() == 1);
    CHECK(d[0].line == 14);
    CHECK(d[0].message == "Unknown directive 'bufer'");

    d = parseCompileLog("error: unresolved external\r\nsome plain text\r\n");
    CHECK(d.size() == 2);
    CHECK(d[0].line == 0);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[1].line == 0);
    CHECK(firstErrorLine(d) == 0);

    d = parseCompileLog("0:3(1): warning: only a warning");
    CHECK(firstErrorLine(d) == 0);

    CHECK(parseCompileLog("").empty());
    CHECK(parseCompileLog("\n\n   \n").empty());

    std::string many;
    for (int i = 0; i < 1000; ++i) {
        many += "0:" + std::to_string(i + 1) + "(1): error: x\n";
    }
    CHECK(parseCompileLog(many).size() == kMaxDiagnostics);

    std::string huge = "0:4(1): error: " + std::string(100000, 'm');
    d = parseCompileLog(huge);
    CHECK(d.size() == 1);
    CHECK(d[0].message.size() <= kMaxDiagnosticBytes);
    CHECK(d[0].line == 4);

    d = parseCompileLog("0:99999999999999999999(1): error: overflow");
    CHECK(d.size() == 1);
    CHECK(d[0].line > 0);
    CHECK(d[0].line <= 9999999);

    Random random;
    for (int round = 0; round < 300; ++round) {
        std::string junk;
        const unsigned length = random.below(400);
        for (unsigned k = 0; k < length; ++k) {
            const char pool[] = "0123456789():\n\r ERRORerrorwarningLine abcBuffer(C)";
            junk.push_back(pool[random.below(sizeof(pool) - 1)]);
        }
        const std::vector<Diagnostic> parsed = parseCompileLog(junk);
        CHECK(parsed.size() <= kMaxDiagnostics);
        for (const Diagnostic& item : parsed) {
            CHECK(item.line >= 0);
            CHECK(item.message.size() <= kMaxDiagnosticBytes);
        }
    }
}

void testScheduler() {
    CompileScheduler s;
    CHECK(!s.due(0.0));
    CHECK(s.secondsUntilDue(0.0) < 0.0);
    s.edited(10.0);
    CHECK(!s.due(10.2));
    CHECK(s.due(10.7));
    s.edited(10.5);
    CHECK(!s.due(10.7));
    CHECK(s.due(11.2));
    CHECK(s.secondsUntilDue(10.6) > 0.4);

    s.begin(11.2);
    CHECK(s.inFlight());
    CHECK(!s.pending());
    s.edited(11.3);
    CHECK(!s.due(20.0));
    s.finish(11.4);
    CHECK(!s.inFlight());
    CHECK(!s.due(11.5));
    CHECK(s.due(12.0));

    s.begin(12.0);
    s.finish(12.0);
    CHECK(!s.pending());
    CHECK(!s.due(100.0));

    CompileScheduler typing;
    for (double t = 0.0; t < 3.0; t += 0.1) {
        typing.edited(t);
    }
    CHECK(typing.due(2.6));
    CHECK(!typing.due(2.4));

    CompileScheduler forced;
    forced.edited(5.0);
    forced.requestNow(5.01);
    CHECK(forced.due(5.02));
    forced.begin(5.02);
    forced.finish(5.03);
    forced.requestNow(5.05);
    CHECK(!forced.due(5.1));
    CHECK(forced.due(5.4));

    CompileScheduler quick({0.1, 0.0, 1.0});
    quick.edited(0.0);
    CHECK(quick.due(0.11));
    quick.reset();
    CHECK(!quick.due(5.0));
}

void testGovernor() {
    PreviewGovernor g;
    for (int i = 0; i < 20; ++i) {
        CHECK(g.shouldDraw());
        g.report(2.0);
    }
    CHECK(g.state() == PreviewGovernor::State::Running);
    CHECK(g.interval() == 1);

    PreviewGovernor slow;
    for (int i = 0; i < 10; ++i) {
        slow.report(40.0);
    }
    CHECK(slow.state() == PreviewGovernor::State::Throttled);
    CHECK(slow.interval() >= 2);
    CHECK(slow.interval() <= 15);
    int drawn = 0;
    for (int i = 0; i < 60; ++i) {
        if (slow.shouldDraw()) {
            ++drawn;
        }
    }
    CHECK(drawn > 0);
    CHECK(drawn < 60);
    for (int i = 0; i < 80; ++i) {
        slow.report(1.0);
    }
    CHECK(slow.state() == PreviewGovernor::State::Running);
    CHECK(slow.interval() == 1);

    PreviewGovernor heavy;
    heavy.report(150.0);
    heavy.report(150.0);
    CHECK(heavy.state() == PreviewGovernor::State::Running);
    heavy.report(150.0);
    CHECK(heavy.state() == PreviewGovernor::State::Paused);
    PreviewGovernor instant;
    instant.report(30.0);
    CHECK(instant.state() == PreviewGovernor::State::Running);
    instant.report(500.0);
    CHECK(instant.state() == PreviewGovernor::State::Paused);
    PreviewGovernor single;
    single.report(500.0);
    CHECK(single.state() == PreviewGovernor::State::Paused);
    CHECK(!single.shouldDraw());
    PreviewGovernor edge;
    edge.report(359.0);
    CHECK(edge.state() == PreviewGovernor::State::Running);
    edge.resume();
    CHECK(edge.state() == PreviewGovernor::State::Running);
    CHECK(!heavy.shouldDraw());
    CHECK(!heavy.shouldDraw());
    heavy.resume();
    CHECK(heavy.state() == PreviewGovernor::State::Running);
    CHECK(heavy.shouldDraw());
    heavy.report(-5.0);
    heavy.report(std::numeric_limits<double>::quiet_NaN());
    CHECK(heavy.reports() == 0);
}

void testNames() {
    CHECK(validateShaderFileName("plasma").ok);
    CHECK(validateShaderFileName("plasma").name == "plasma.glsl");
    CHECK(validateShaderFileName("  plasma.FRAG ").name == "plasma.frag");
    CHECK(validateShaderFileName("a b-c_d(2).fs").ok);
    CHECK(validateShaderFileName("v1.2.glsl").ok);
    CHECK(validateShaderFileName("").problem == NameProblem::Empty);
    CHECK(validateShaderFileName("   ").problem == NameProblem::Empty);
    CHECK(validateShaderFileName("../evil.glsl").problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName("a/b.glsl").problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName("a\\b.glsl").problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName("c:evil").problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName("a*b").problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName("m\xC3\xBC" "ll").problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName(std::string("a\0b", 3)).problem == NameProblem::BadCharacter);
    CHECK(validateShaderFileName("a.exe").problem == NameProblem::BadExtension);
    CHECK(validateShaderFileName("a.glsl.txt").problem == NameProblem::BadExtension);
    CHECK(validateShaderFileName(".glsl").problem == NameProblem::BadStart);
    CHECK(validateShaderFileName(".hidden").problem == NameProblem::BadStart);
    CHECK(validateShaderFileName("a..b.glsl").problem == NameProblem::BadStart);
    CHECK(validateShaderFileName("end .glsl").problem == NameProblem::BadStart);
    CHECK(validateShaderFileName("CON").problem == NameProblem::Reserved);
    CHECK(validateShaderFileName("nul.glsl").problem == NameProblem::Reserved);
    CHECK(validateShaderFileName("Com1").problem == NameProblem::Reserved);
    CHECK(validateShaderFileName("lpt9.frag").problem == NameProblem::Reserved);
    CHECK(validateShaderFileName("console").ok);
    CHECK(validateShaderFileName(std::string(59, 'a')).ok);
    CHECK(validateShaderFileName(std::string(60, 'a')).problem == NameProblem::TooLong);
    CHECK(validateShaderFileName(std::string(64, 'a') + ".fs").problem == NameProblem::TooLong);
}

struct TempFolder {
    fs::path path;

    TempFolder() {
        path = fs::temp_directory_path() / ("nvj_shaderedit_test_" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string lowerCopy(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

fs::path u8path(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

void writeRaw(const fs::path& file, const std::string& data) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << data;
}

void testFiles() {
    TempFolder temp;
    const fs::path folder = temp.path / "shaders";

    SaveResult r = saveShader(folder, "one", "void main() {}\n", nullptr, false);
    CHECK(r.ok);
    CHECK(r.name == "one.glsl");
    CHECK(fs::exists(folder / "one.glsl"));
    CHECK(r.stamp.exists);
    CHECK(r.stamp.size == 15);

    ReadResult read = readShader(folder, "one.glsl");
    CHECK(read.ok);
    CHECK(read.text == "void main() {}\n");
    CHECK(sameFile(read.stamp, r.stamp));

    r = saveShader(folder, "one", "x", nullptr, false);
    CHECK(!r.ok);
    CHECK(r.error == SaveError::AlreadyExists);
    CHECK(readShader(folder, "one.glsl").text == "void main() {}\n");

    const FileStamp loaded = read.stamp;
    r = saveShader(folder, "one.glsl", "void main() { }\n", &loaded, false);
    CHECK(r.ok);
    CHECK(readShader(folder, "one.glsl").text == "void main() { }\n");
    CHECK(!sameFile(r.stamp, loaded));

    r = saveShader(folder, "one.glsl", "again", &loaded, false);
    CHECK(!r.ok);
    CHECK(r.error == SaveError::ChangedOnDisk);
    CHECK(readShader(folder, "one.glsl").text == "void main() { }\n");

    r = saveShader(folder, "one.glsl", "forced", &loaded, true);
    CHECK(r.ok);
    CHECK(readShader(folder, "one.glsl").text == "forced");

    const FileStamp current = fullStamp(folder / "one.glsl");
    writeRaw(folder / "one.glsl", "external edit");
    r = saveShader(folder, "one.glsl", "mine", &current, false);
    CHECK(r.error == SaveError::ChangedOnDisk);
    writeRaw(folder / "one.glsl", "forced");
    r = saveShader(folder, "one.glsl", "mine", &current, false);
    CHECK(r.ok);

    const FileStamp before = fullStamp(folder / "one.glsl");
    std::error_code ec;
    fs::remove(folder / "one.glsl", ec);
    r = saveShader(folder, "one.glsl", "mine", &before, false);
    CHECK(!r.ok);
    CHECK(r.error == SaveError::DeletedOnDisk);
    CHECK(!fs::exists(folder / "one.glsl"));
    r = saveShader(folder, "one.glsl", "mine", &before, true);
    CHECK(r.ok);

    CHECK(saveShader(folder, "bad/name", "x", nullptr, false).error == SaveError::BadName);
    CHECK(saveShader(folder, "..", "x", &before, false).error == SaveError::BadName);
    CHECK(saveShader(folder, "../up.glsl", "x", &before, false).error == SaveError::BadName);
    CHECK(saveShader(folder, "a.txt", "x", &before, false).error == SaveError::BadName);
    CHECK(saveShader(folder, "big", std::string(kMaxTextBytes + 1, 'a'), nullptr, false).error == SaveError::TooLarge);
    CHECK(!fs::exists(folder / "big.glsl"));
    CHECK(saveShader(folder, "limit", std::string(kMaxTextBytes, 'a'), nullptr, false).ok);

    fs::create_directories(folder / "dir.glsl", ec);
    CHECK(saveShader(folder, "dir.glsl", "x", nullptr, true).error == SaveError::NotAFile);
    fs::create_symlink(folder / "limit.glsl", folder / "link.glsl", ec);
    if (!ec) {
        CHECK(saveShader(folder, "link.glsl", "x", nullptr, true).error == SaveError::NotAFile);
        CHECK(readShader(folder, "limit.glsl").ok);
    }

    const fs::path blocker = temp.path / "blocker";
    writeRaw(blocker, "i am a file");
    CHECK(saveShader(blocker, "x", "y", nullptr, false).error == SaveError::NoFolder);

    r = saveShader(folder, "crlf", "a\r\nb\r\n", nullptr, false);
    CHECK(r.ok);
    CHECK(readShader(folder, "crlf.glsl").text == "a\nb\n");
    CHECK(r.stamp.size == 4);

    writeRaw(folder / "windows.glsl", "x\r\ny\r\n");
    read = readShader(folder, "windows.glsl");
    CHECK(read.ok);
    CHECK(read.text == "x\ny\n");
    CHECK(read.stamp.size == 6);
    const FileStamp rawStamp = read.stamp;
    CHECK(saveShader(folder, "windows.glsl", read.text, &rawStamp, false).ok);

    writeRaw(folder / u8path("m\xC3\xBC" "ll.glsl"), "u");
    read = readShader(folder, "m\xC3\xBC" "ll.glsl");
    CHECK(read.ok);
    const FileStamp umlaut = read.stamp;
    CHECK(saveShader(folder, "m\xC3\xBC" "ll.glsl", "u2", &umlaut, false).ok);
    CHECK(readShader(folder, "m\xC3\xBC" "ll.glsl").text == "u2");

    writeRaw(folder / "huge.glsl", std::string(kMaxTextBytes + 10, 'z'));
    read = readShader(folder, "huge.glsl");
    CHECK(!read.ok);
    CHECK(read.tooLarge);
    CHECK(!readShader(folder, "missing.glsl").ok);
    CHECK(!readShader(folder, "../x.glsl").ok);
    CHECK(!readShader(folder, "notes.txt").ok);

    bool leftover = false;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
        if (entry.path().extension() == ".tmp") {
            leftover = true;
        }
    }
    CHECK(!leftover);

    const std::vector<std::string> names = listShaderFiles(folder, 100);
    CHECK(!names.empty());
    for (size_t i = 1; i < names.size(); ++i) {
        CHECK(lowerCopy(names[i - 1]) <= lowerCopy(names[i]));
    }
    bool hasOne = false;
    bool hasText = false;
    for (const std::string& name : names) {
        hasOne = hasOne || name == "one.glsl";
        hasText = hasText || name == "notes.txt";
    }
    CHECK(hasOne);
    CHECK(!hasText);
    CHECK(listShaderFiles(folder, 2).size() == 2);
    CHECK(listShaderFiles(temp.path / "none", 10).empty());

    const FileStamp quick = quickStamp(folder / "one.glsl");
    CHECK(quick.exists);
    CHECK(quick.hash == 0);
    CHECK(!quickStamp(folder / "dir.glsl").exists);
    CHECK(!quickStamp(folder / "nothing.glsl").exists);
    CHECK(hashText("a") != hashText("b"));
    CHECK(hashText("") == hashText(""));
}

void testTemplates() {
    CHECK(templates().size() >= 4);
    for (const Template& t : templates()) {
        CHECK(validateShaderFileName(t.fileName).ok);
        CHECK(std::string(t.text).size() < kMaxTextBytes);
        CHECK(!std::string(t.title).empty());
        CHECK(sanitizeText(t.text) == std::string(t.text));
    }
}

void testRandomEditing() {
    Random random;
    for (int run = 0; run < 25; ++run) {
        EditModel m;
        const std::string start = "float a;\nvoid main() {\n    a = 1.0;\n}\n";
        m.setText(start);
        for (int step = 0; step < 300; ++step) {
            switch (random.below(14)) {
                case 0:
                    m.insertText(std::string(1, static_cast<char>('a' + random.below(26))));
                    break;
                case 1:
                    m.insertText("x\ny");
                    break;
                case 2:
                    m.newline();
                    break;
                case 3:
                    m.backspace(random.below(2) == 0);
                    break;
                case 4:
                    m.erase(random.below(2) == 0);
                    break;
                case 5:
                    m.move(static_cast<Move>(random.below(12)), random.below(2) == 0, 3);
                    break;
                case 6:
                    m.indent(random.below(2) == 0);
                    break;
                case 7:
                    m.toggleComment();
                    break;
                case 8:
                    m.setCursor(random.below(static_cast<unsigned>(m.size() + 2)), random.below(2) == 0);
                    break;
                case 9:
                    m.selectAll();
                    break;
                case 10:
                    m.insertText("\xC3\xA4");
                    break;
                case 11: {
                    std::string removed;
                    m.cutSelection(removed);
                    break;
                }
                case 12:
                    m.selectWordAt(random.below(static_cast<unsigned>(m.size() + 1)));
                    break;
                default:
                    m.insertText("\r\n\t{}");
                    break;
            }
            CHECK(m.cursor() <= m.size());
            CHECK(m.anchor() <= m.size());
            CHECK(m.lineCount() == countLines(m.text()));
        }
        const std::string finalText = m.text();
        while (m.undo()) {
        }
        CHECK(m.text() == start);
        while (m.redo()) {
        }
        CHECK(m.text() == finalText);
    }
}

void testPerformance() {
    std::string big;
    while (big.size() < kMaxTextBytes - 100) {
        big += "    vec3 col = mix(vec3(0.1, 0.2, 0.3), vec3(uTime), smoothstep(0.0, 1.0, sin(p.x * 3.14159))); // line\n";
    }
    auto t0 = std::chrono::steady_clock::now();
    const std::vector<std::vector<Span>> spans = highlightAll(big);
    const double highlightSeconds = seconds(t0);
    CHECK(!spans.empty());
    CHECK(highlightSeconds < 1.0);

    EditModel m;
    m.setText(big);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 300; ++i) {
        m.setCursor(m.size() / 2, false);
        m.insertText("a");
        m.setCursor(0, false);
        m.backspace(false);
        m.erase(false);
    }
    const double editSeconds = seconds(t0);
    CHECK(editSeconds < 3.0);

    t0 = std::chrono::steady_clock::now();
    std::string log;
    for (int i = 0; i < 200; ++i) {
        log += "0:" + std::to_string(i + 1) + "(3): error: something is wrong on this line\n";
    }
    for (int i = 0; i < 200; ++i) {
        parseCompileLog(log);
    }
    CHECK(seconds(t0) < 2.0);
    std::printf("highlight 256 KiB %.1f ms, 300 edits %.1f ms\n", highlightSeconds * 1000.0, editSeconds * 1000.0);
}

}

int main() {
    testSanitize();
    testLines();
    testInsertAndUndo();
    testLimits();
    testNewlineAndBackspace();
    testMovement();
    testIndentAndComment();
    testHighlight();
    testLogParser();
    testScheduler();
    testGovernor();
    testNames();
    testFiles();
    testTemplates();
    testRandomEditing();
    testPerformance();
    return testing::finish();
}

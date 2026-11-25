#include "core/shader_editor.hpp"

#include "core/recovery.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <system_error>
#include <unordered_set>

namespace nvj::shaderedit {

namespace {

namespace fs = std::filesystem;

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

bool isWordChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 0x80u;
}

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

size_t sequenceLength(std::string_view s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const auto cont = [&](size_t k) {
        return i + k < s.size() && isContinuation(s[i + k]);
    };
    if (c >= 0xC2 && c <= 0xDF) {
        return cont(1) ? 2 : 0;
    }
    if (c >= 0xE0 && c <= 0xEF) {
        if (!cont(1) || !cont(2)) {
            return 0;
        }
        const unsigned char second = static_cast<unsigned char>(s[i + 1]);
        if ((c == 0xE0 && second < 0xA0) || (c == 0xED && second >= 0xA0)) {
            return 0;
        }
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (!cont(1) || !cont(2) || !cont(3)) {
            return 0;
        }
        const unsigned char second = static_cast<unsigned char>(s[i + 1]);
        if ((c == 0xF0 && second < 0x90) || (c == 0xF4 && second >= 0x90)) {
            return 0;
        }
        return 4;
    }
    return 0;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

fs::path pathOf(std::string_view name) {
    return fs::path(std::u8string(name.begin(), name.end()));
}

std::string utf8Of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

bool allowedExtension(const std::string& extension) {
    return extension == ".glsl" || extension == ".frag" || extension == ".fs";
}

bool lenientNameOk(std::string_view name) {
    if (name.empty() || name.size() > 255 || name == "." || name == "..") {
        return false;
    }
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || u == 0x7F) {
            return false;
        }
    }
    if (name.find("..") != std::string_view::npos) {
        return false;
    }
    const size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return false;
    }
    return allowedExtension(lower(name.substr(dot)));
}

const std::unordered_set<std::string>& keywordSet() {
    static const std::unordered_set<std::string> set = {
        "if", "else", "for", "while", "do", "break", "continue", "return", "discard", "switch", "case", "default",
        "const", "in", "out", "inout", "uniform", "varying", "attribute", "struct", "precision", "highp", "mediump",
        "lowp", "flat", "smooth", "layout", "true", "false", "centroid", "invariant"};
    return set;
}

const std::unordered_set<std::string>& typeSet() {
    static const std::unordered_set<std::string> set = {
        "void", "bool", "int", "uint", "float", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3",
        "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3",
        "mat3x4", "mat4x2", "mat4x3", "mat4x4", "sampler2D", "sampler3D", "samplerCube", "sampler2DShadow",
        "isampler2D", "usampler2D"};
    return set;
}

const std::unordered_set<std::string>& builtinSet() {
    static const std::unordered_set<std::string> set = {
        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "asinh", "acosh",
        "atanh", "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign", "floor", "trunc", "round",
        "roundEven", "ceil", "fract", "mod", "modf", "min", "max", "clamp", "mix", "step", "smoothstep", "isnan",
        "isinf", "length", "distance", "dot", "cross", "normalize", "faceforward", "reflect", "refract", "matrixCompMult",
        "outerProduct", "transpose", "determinant", "inverse", "lessThan", "lessThanEqual", "greaterThan",
        "greaterThanEqual", "equal", "notEqual", "any", "all", "not", "texture", "textureLod", "textureGrad",
        "textureOffset", "texelFetch", "textureSize", "textureProj", "dFdx", "dFdy", "fwidth", "floatBitsToInt",
        "floatBitsToUint", "intBitsToFloat", "uintBitsToFloat", "packHalf2x16", "unpackHalf2x16"};
    return set;
}

const std::unordered_set<std::string>& nyrexSet() {
    static const std::unordered_set<std::string> set = {
        "uTime", "uBeat", "uBpm", "uRes", "uColorA", "uColorB", "uRainbow", "uAudio", "uKick", "uScale", "vUv",
        "fragColor", "centered", "duo", "pal", "rainbow", "spec", "wave", "PI", "hash21", "h22", "vnoise", "fbm", "ss",
        "rot", "sdBox", "mainImage", "fragCoord", "iResolution", "iTime", "iFrame", "iTimeDelta", "iSampleRate",
        "iMouse", "iDate", "iChannel0", "iChannel1", "iChannel2", "iChannel3", "iChannelResolution", "iChannelTime",
        "uChannel0", "uChannel1", "uChannel2", "uChannel3", "uChannelRes", "uChannelTime", "uFrame", "uDelta"};
    return set;
}

TokenKind classify(std::string_view word) {
    const std::string key(word);
    if (keywordSet().count(key)) {
        return TokenKind::Keyword;
    }
    if (typeSet().count(key)) {
        return TokenKind::Type;
    }
    if (builtinSet().count(key) || key.rfind("gl_", 0) == 0) {
        return TokenKind::Builtin;
    }
    if (nyrexSet().count(key)) {
        return TokenKind::Nyrex;
    }
    return TokenKind::Plain;
}

bool readUnsigned(std::string_view s, size_t& i, int& value) {
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        return false;
    }
    long long v = 0;
    size_t start = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        if (i - start < 9) {
            v = v * 10 + (s[i] - '0');
        }
        ++i;
    }
    value = static_cast<int>(std::min<long long>(v, 9999999));
    return true;
}

void skipBlank(std::string_view s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
}

bool parseLocation(std::string_view s, size_t& i, int& line, int& column) {
    const size_t begin = i;
    int a = 0;
    int b = 0;
    int c = 0;
    if (!readUnsigned(s, i, a)) {
        return false;
    }
    if (i < s.size() && s[i] == ':') {
        ++i;
        if (!readUnsigned(s, i, b)) {
            i = begin;
            return false;
        }
        column = 0;
        if (i < s.size() && s[i] == '(') {
            size_t j = i + 1;
            if (readUnsigned(s, j, c) && j < s.size() && s[j] == ')') {
                column = c;
                i = j + 1;
            }
        }
        skipBlank(s, i);
        if (i < s.size() && s[i] == ':') {
            ++i;
        }
        line = b;
        return true;
    }
    if (i < s.size() && s[i] == '(') {
        ++i;
        if (!readUnsigned(s, i, b) || i >= s.size() || s[i] != ')') {
            i = begin;
            return false;
        }
        ++i;
        skipBlank(s, i);
        if (i < s.size() && s[i] == ':') {
            ++i;
        }
        line = b;
        column = 0;
        return true;
    }
    i = begin;
    return false;
}

bool startsWithNoCase(std::string_view s, size_t i, std::string_view word) {
    if (s.size() - i < word.size()) {
        return false;
    }
    for (size_t k = 0; k < word.size(); ++k) {
        if (std::tolower(static_cast<unsigned char>(s[i + k])) != word[k]) {
            return false;
        }
    }
    return true;
}

std::string clipMessage(std::string_view text) {
    std::string out(text);
    out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return c == '\r' || c == '\0'; }), out.end());
    if (out.size() > kMaxDiagnosticBytes) {
        out.resize(kMaxDiagnosticBytes);
        while (!out.empty() && isContinuation(out.back())) {
            out.pop_back();
        }
    }
    return out;
}

std::string trim(std::string_view s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

}

std::string sanitizeText(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size();) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '\r') {
            out.push_back('\n');
            ++i;
            if (i < input.size() && input[i] == '\n') {
                ++i;
            }
            continue;
        }
        if (c == '\n' || c == '\t') {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            ++i;
            continue;
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        const size_t length = sequenceLength(input, i);
        if (length == 0) {
            out.push_back('?');
            ++i;
            continue;
        }
        out.append(input.substr(i, length));
        i += length;
    }
    return out;
}

EditModel::EditModel() {
    starts_.push_back(0);
    savedHash_ = hashText(text_);
}

void EditModel::setText(std::string_view text) {
    text_ = sanitizeText(text);
    if (text_.size() > kMaxTextBytes) {
        text_.resize(kMaxTextBytes);
        while (!text_.empty() && (isContinuation(text_.back()) || (static_cast<unsigned char>(text_.back()) >= 0xC0u))) {
            text_.pop_back();
        }
    }
    undo_.clear();
    redo_.clear();
    undoBytes_ = 0;
    cursor_ = 0;
    anchor_ = 0;
    keepColumn_ = false;
    breakGroup_ = true;
    ++revision_;
    rebuildLines();
    markSaved();
}

void EditModel::rebuildLines() {
    starts_.clear();
    starts_.push_back(0);
    for (size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            starts_.push_back(i + 1);
        }
    }
}

size_t EditModel::lineStart(size_t line) const {
    return starts_[std::min(line, starts_.size() - 1)];
}

size_t EditModel::lineLength(size_t line) const {
    line = std::min(line, starts_.size() - 1);
    const size_t begin = starts_[line];
    const size_t end = line + 1 < starts_.size() ? starts_[line + 1] - 1 : text_.size();
    return end - begin;
}

std::string_view EditModel::lineText(size_t line) const {
    line = std::min(line, starts_.size() - 1);
    return std::string_view(text_).substr(starts_[line], lineLength(line));
}

size_t EditModel::lineOfOffset(size_t offset) const {
    offset = std::min(offset, text_.size());
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
    return static_cast<size_t>(it - starts_.begin()) - 1;
}

Position EditModel::positionOf(size_t offset) const {
    offset = std::min(offset, text_.size());
    const size_t line = lineOfOffset(offset);
    return Position{line, offset - starts_[line]};
}

size_t EditModel::offsetOf(Position position) const {
    const size_t line = std::min(position.line, starts_.size() - 1);
    size_t offset = starts_[line] + std::min(position.column, lineLength(line));
    while (offset > 0 && offset < text_.size() && isContinuation(text_[offset])) {
        --offset;
    }
    return offset;
}

std::pair<size_t, size_t> EditModel::selection() const {
    return {std::min(cursor_, anchor_), std::max(cursor_, anchor_)};
}

std::string EditModel::selectedText() const {
    const auto [a, b] = selection();
    return text_.substr(a, b - a);
}

size_t EditModel::stepLeft(size_t offset) const {
    if (offset == 0) {
        return 0;
    }
    --offset;
    while (offset > 0 && isContinuation(text_[offset])) {
        --offset;
    }
    return offset;
}

size_t EditModel::stepRight(size_t offset) const {
    if (offset >= text_.size()) {
        return text_.size();
    }
    ++offset;
    while (offset < text_.size() && isContinuation(text_[offset])) {
        ++offset;
    }
    return offset;
}

size_t EditModel::wordLeft(size_t offset) const {
    while (offset > 0 && isSpace(text_[offset - 1])) {
        --offset;
    }
    if (offset == 0) {
        return 0;
    }
    const bool word = isWordChar(text_[offset - 1]);
    while (offset > 0 && !isSpace(text_[offset - 1]) && isWordChar(text_[offset - 1]) == word) {
        --offset;
    }
    return offset;
}

size_t EditModel::wordRight(size_t offset) const {
    while (offset < text_.size() && isSpace(text_[offset])) {
        ++offset;
    }
    if (offset >= text_.size()) {
        return text_.size();
    }
    const bool word = isWordChar(text_[offset]);
    while (offset < text_.size() && !isSpace(text_[offset]) && isWordChar(text_[offset]) == word) {
        ++offset;
    }
    return offset;
}

void EditModel::setCursor(size_t offset, bool extend) {
    offset = std::min(offset, text_.size());
    while (offset > 0 && offset < text_.size() && isContinuation(text_[offset])) {
        --offset;
    }
    cursor_ = offset;
    if (!extend) {
        anchor_ = cursor_;
    }
    keepColumn_ = false;
    breakGroup_ = true;
}

void EditModel::selectAll() {
    anchor_ = 0;
    cursor_ = text_.size();
    keepColumn_ = false;
    breakGroup_ = true;
}

void EditModel::selectWordAt(size_t offset) {
    offset = std::min(offset, text_.size());
    size_t a = offset;
    size_t b = offset;
    while (a > 0 && isWordChar(text_[a - 1])) {
        --a;
    }
    while (b < text_.size() && isWordChar(text_[b])) {
        ++b;
    }
    if (a == b) {
        a = offset;
        b = stepRight(offset);
    }
    anchor_ = a;
    cursor_ = b;
    keepColumn_ = false;
    breakGroup_ = true;
}

void EditModel::selectLine(size_t line) {
    line = std::min(line, starts_.size() - 1);
    anchor_ = starts_[line];
    cursor_ = line + 1 < starts_.size() ? starts_[line + 1] : text_.size();
    keepColumn_ = false;
    breakGroup_ = true;
}

void EditModel::move(Move movement, bool extend, size_t pageLines) {
    size_t target = cursor_;
    bool vertical = false;
    const auto [selA, selB] = selection();
    switch (movement) {
        case Move::Left:
            target = (!extend && hasSelection()) ? selA : stepLeft(cursor_);
            break;
        case Move::Right:
            target = (!extend && hasSelection()) ? selB : stepRight(cursor_);
            break;
        case Move::WordLeft:
            target = wordLeft(cursor_);
            break;
        case Move::WordRight:
            target = wordRight(cursor_);
            break;
        case Move::LineStart: {
            const size_t line = lineOfOffset(cursor_);
            const size_t begin = starts_[line];
            size_t firstText = begin;
            while (firstText < begin + lineLength(line) && isBlank(text_[firstText])) {
                ++firstText;
            }
            target = cursor_ == firstText ? begin : firstText;
            break;
        }
        case Move::LineEnd: {
            const size_t line = lineOfOffset(cursor_);
            target = starts_[line] + lineLength(line);
            break;
        }
        case Move::DocStart:
            target = 0;
            break;
        case Move::DocEnd:
            target = text_.size();
            break;
        case Move::Up:
        case Move::Down:
        case Move::PageUp:
        case Move::PageDown: {
            vertical = true;
            const Position here = positionOf(cursor_);
            if (!keepColumn_) {
                wantedColumn_ = here.column;
            }
            const size_t step = (movement == Move::PageUp || movement == Move::PageDown) ? std::max<size_t>(pageLines, 1) : 1;
            const bool up = movement == Move::Up || movement == Move::PageUp;
            if (up) {
                if (here.line == 0) {
                    target = 0;
                } else {
                    target = offsetOf(Position{here.line > step ? here.line - step : 0, wantedColumn_});
                }
            } else {
                if (here.line + 1 >= starts_.size()) {
                    target = text_.size();
                } else {
                    target = offsetOf(Position{std::min(here.line + step, starts_.size() - 1), wantedColumn_});
                }
            }
            break;
        }
    }
    cursor_ = target;
    if (!extend) {
        anchor_ = cursor_;
    }
    keepColumn_ = vertical;
    breakGroup_ = true;
}

void EditModel::replaceRaw(size_t position, size_t removeLength, std::string_view inserted) {
    text_.replace(position, removeLength, inserted);
    rebuildLines();
}

bool EditModel::apply(size_t position, size_t removeLength, std::string_view inserted, size_t cursorAfter, Kind kind) {
    if (position > text_.size() || removeLength > text_.size() - position) {
        return false;
    }
    if (text_.size() - removeLength + inserted.size() > kMaxTextBytes) {
        return false;
    }
    if (removeLength == 0 && inserted.empty()) {
        return false;
    }
    Edit edit;
    edit.position = position;
    edit.removed = text_.substr(position, removeLength);
    edit.inserted = std::string(inserted);
    edit.cursorBefore = cursor_;
    edit.anchorBefore = anchor_;
    edit.kind = kind;
    replaceRaw(position, removeLength, inserted);
    cursor_ = std::min(cursorAfter, text_.size());
    anchor_ = cursor_;
    edit.cursorAfter = cursor_;
    edit.anchorAfter = anchor_;
    redo_.clear();
    ++revision_;
    keepColumn_ = false;
    bool merged = false;
    if (kind != Kind::Other && !breakGroup_ && !undo_.empty() && undo_.back().kind == kind) {
        Edit& last = undo_.back();
        if (kind == Kind::Typing && edit.removed.empty() && last.removed.empty() && last.position + last.inserted.size() == edit.position) {
            last.inserted += edit.inserted;
            last.cursorAfter = edit.cursorAfter;
            last.anchorAfter = edit.anchorAfter;
            undoBytes_ += edit.inserted.size();
            merged = true;
        } else if (kind == Kind::Erasing && edit.inserted.empty() && last.inserted.empty()) {
            if (edit.position + edit.removed.size() == last.position) {
                last.position = edit.position;
                last.removed = edit.removed + last.removed;
                last.cursorAfter = edit.cursorAfter;
                last.anchorAfter = edit.anchorAfter;
                undoBytes_ += edit.removed.size();
                merged = true;
            } else if (edit.position == last.position) {
                last.removed += edit.removed;
                last.cursorAfter = edit.cursorAfter;
                last.anchorAfter = edit.anchorAfter;
                undoBytes_ += edit.removed.size();
                merged = true;
            }
        }
    }
    if (!merged) {
        undoBytes_ += edit.removed.size() + edit.inserted.size();
        undo_.push_back(std::move(edit));
    }
    breakGroup_ = false;
    trimHistory();
    return true;
}

void EditModel::trimHistory() {
    while (!undo_.empty() && (undo_.size() > kMaxUndoSteps || undoBytes_ > kMaxUndoBytes)) {
        const Edit& first = undo_.front();
        const size_t bytes = first.removed.size() + first.inserted.size();
        undoBytes_ = undoBytes_ > bytes ? undoBytes_ - bytes : 0;
        undo_.erase(undo_.begin());
    }
}

bool EditModel::insertText(std::string_view text) {
    const std::string clean = sanitizeText(text);
    if (clean.empty()) {
        return false;
    }
    const auto [a, b] = selection();
    const bool typing = !hasSelection() && clean.size() <= 4 && clean.find('\n') == std::string::npos;
    return apply(a, b - a, clean, a + clean.size(), typing ? Kind::Typing : Kind::Other);
}

bool EditModel::newline() {
    const auto [a, b] = selection();
    const size_t line = lineOfOffset(a);
    const size_t begin = starts_[line];
    size_t indentEnd = begin;
    while (indentEnd < a && isBlank(text_[indentEnd])) {
        ++indentEnd;
    }
    const std::string base = text_.substr(begin, indentEnd - begin);
    size_t before = a;
    while (before > begin && isBlank(text_[before - 1])) {
        --before;
    }
    const bool opens = before > begin && text_[before - 1] == '{';
    size_t after = b;
    while (after < text_.size() && isBlank(text_[after])) {
        ++after;
    }
    const bool closes = after < text_.size() && text_[after] == '}';
    const std::string extra(opens ? kTabWidth : 0, ' ');
    std::string inserted = "\n" + base + extra;
    size_t cursorAfter = a + inserted.size();
    if (opens && closes) {
        inserted += "\n" + base;
    }
    return apply(a, after > b && closes && opens ? after - a : b - a, inserted, cursorAfter, Kind::Other);
}

bool EditModel::backspace(bool word) {
    if (hasSelection()) {
        const auto [a, b] = selection();
        return apply(a, b - a, "", a, Kind::Other);
    }
    if (cursor_ == 0) {
        return false;
    }
    size_t from = word ? wordLeft(cursor_) : stepLeft(cursor_);
    if (!word) {
        const size_t line = lineOfOffset(cursor_);
        const size_t begin = starts_[line];
        const size_t column = cursor_ - begin;
        bool onlyBlanks = column > 0;
        for (size_t i = begin; i < cursor_ && onlyBlanks; ++i) {
            onlyBlanks = text_[i] == ' ';
        }
        if (onlyBlanks && column > 0) {
            const size_t stop = ((column - 1) / kTabWidth) * kTabWidth;
            from = begin + stop;
        }
    }
    return apply(from, cursor_ - from, "", from, word ? Kind::Other : Kind::Erasing);
}

bool EditModel::erase(bool word) {
    if (hasSelection()) {
        const auto [a, b] = selection();
        return apply(a, b - a, "", a, Kind::Other);
    }
    if (cursor_ >= text_.size()) {
        return false;
    }
    const size_t to = word ? wordRight(cursor_) : stepRight(cursor_);
    return apply(cursor_, to - cursor_, "", cursor_, word ? Kind::Other : Kind::Erasing);
}

bool EditModel::cutSelection(std::string& removed) {
    if (!hasSelection()) {
        return false;
    }
    removed = selectedText();
    const auto [a, b] = selection();
    return apply(a, b - a, "", a, Kind::Other);
}

bool EditModel::indent(bool outdent) {
    const auto [a, b] = selection();
    const size_t firstLine = lineOfOffset(a);
    size_t lastLine = lineOfOffset(b);
    if (b > a && b == starts_[lastLine] && lastLine > firstLine) {
        --lastLine;
    }
    const bool block = lastLine > firstLine || (outdent);
    if (!block) {
        const size_t column = a - starts_[firstLine];
        const size_t spaces = kTabWidth - (column % kTabWidth);
        return apply(a, b - a, std::string(spaces, ' '), a + spaces, Kind::Other);
    }
    const size_t blockStart = starts_[firstLine];
    const size_t blockEnd = starts_[lastLine] + lineLength(lastLine);
    std::string result;
    for (size_t line = firstLine; line <= lastLine; ++line) {
        std::string_view body = lineText(line);
        if (outdent) {
            size_t remove = 0;
            if (!body.empty() && body[0] == '\t') {
                remove = 1;
            } else {
                while (remove < body.size() && remove < static_cast<size_t>(kTabWidth) && body[remove] == ' ') {
                    ++remove;
                }
            }
            result.append(body.substr(remove));
        } else {
            if (!body.empty()) {
                result.append(kTabWidth, ' ');
            }
            result.append(body);
        }
        if (line < lastLine) {
            result.push_back('\n');
        }
    }
    const std::string original = text_.substr(blockStart, blockEnd - blockStart);
    if (result == original) {
        return false;
    }
    if (!apply(blockStart, blockEnd - blockStart, result, blockStart + result.size(), Kind::Other)) {
        return false;
    }
    if (a == b) {
        const size_t removed = original.size() - result.size();
        cursor_ = anchor_ = a - std::min(removed, a - blockStart);
    } else {
        anchor_ = blockStart;
        cursor_ = blockStart + result.size();
    }
    return true;
}

bool EditModel::toggleComment() {
    const auto [a, b] = selection();
    const size_t firstLine = lineOfOffset(a);
    size_t lastLine = lineOfOffset(b);
    if (b > a && b == starts_[lastLine] && lastLine > firstLine) {
        --lastLine;
    }
    bool allCommented = true;
    bool anyText = false;
    size_t minIndent = static_cast<size_t>(-1);
    for (size_t line = firstLine; line <= lastLine; ++line) {
        const std::string_view body = lineText(line);
        size_t indent = 0;
        while (indent < body.size() && isBlank(body[indent])) {
            ++indent;
        }
        if (indent == body.size()) {
            continue;
        }
        anyText = true;
        minIndent = std::min(minIndent, indent);
        if (body.substr(indent, 2) != "//") {
            allCommented = false;
        }
    }
    if (!anyText) {
        return false;
    }
    std::string result;
    for (size_t line = firstLine; line <= lastLine; ++line) {
        std::string body(lineText(line));
        size_t indent = 0;
        while (indent < body.size() && isBlank(body[indent])) {
            ++indent;
        }
        if (indent < body.size()) {
            if (allCommented) {
                body.erase(indent, 2);
                if (indent < body.size() && body[indent] == ' ') {
                    body.erase(indent, 1);
                }
            } else {
                body.insert(std::min(minIndent, body.size()), "// ");
            }
        }
        result += body;
        if (line < lastLine) {
            result.push_back('\n');
        }
    }
    const size_t blockStart = starts_[firstLine];
    const size_t blockEnd = starts_[lastLine] + lineLength(lastLine);
    const size_t originalSize = blockEnd - blockStart;
    if (!apply(blockStart, originalSize, result, blockStart + result.size(), Kind::Other)) {
        return false;
    }
    if (a == b) {
        const long long shifted = static_cast<long long>(a) + static_cast<long long>(result.size()) - static_cast<long long>(originalSize);
        cursor_ = anchor_ = static_cast<size_t>(std::max<long long>(shifted, static_cast<long long>(blockStart)));
    } else {
        anchor_ = blockStart;
        cursor_ = blockStart + result.size();
    }
    return true;
}

bool EditModel::undo() {
    if (undo_.empty()) {
        return false;
    }
    Edit edit = std::move(undo_.back());
    undo_.pop_back();
    const size_t bytes = edit.removed.size() + edit.inserted.size();
    undoBytes_ = undoBytes_ > bytes ? undoBytes_ - bytes : 0;
    replaceRaw(edit.position, edit.inserted.size(), edit.removed);
    cursor_ = std::min(edit.cursorBefore, text_.size());
    anchor_ = std::min(edit.anchorBefore, text_.size());
    redo_.push_back(std::move(edit));
    ++revision_;
    breakGroup_ = true;
    keepColumn_ = false;
    return true;
}

bool EditModel::redo() {
    if (redo_.empty()) {
        return false;
    }
    Edit edit = std::move(redo_.back());
    redo_.pop_back();
    replaceRaw(edit.position, edit.removed.size(), edit.inserted);
    cursor_ = std::min(edit.cursorAfter, text_.size());
    anchor_ = std::min(edit.anchorAfter, text_.size());
    undoBytes_ += edit.removed.size() + edit.inserted.size();
    undo_.push_back(std::move(edit));
    ++revision_;
    breakGroup_ = true;
    keepColumn_ = false;
    return true;
}

bool EditModel::dirty() const {
    if (checkedRevision_ != revision_) {
        dirtyValue_ = text_.size() != savedSize_ || hashText(text_) != savedHash_;
        checkedRevision_ = revision_;
    }
    return dirtyValue_;
}

void EditModel::markSaved() {
    breakGroup_ = true;
    savedHash_ = hashText(text_);
    savedSize_ = text_.size();
    checkedRevision_ = revision_;
    dirtyValue_ = false;
}

std::vector<Span> highlightLine(std::string_view line, bool& inBlockComment) {
    const size_t n = line.size();
    std::vector<TokenKind> kinds(n, TokenKind::Plain);
    size_t i = 0;
    const auto paint = [&](size_t from, size_t to, TokenKind kind) {
        for (size_t k = from; k < to && k < n; ++k) {
            kinds[k] = kind;
        }
    };
    if (inBlockComment) {
        const size_t end = line.find("*/");
        if (end == std::string_view::npos) {
            paint(0, n, TokenKind::Comment);
            i = n;
        } else {
            paint(0, end + 2, TokenKind::Comment);
            inBlockComment = false;
            i = end + 2;
        }
    }
    bool firstToken = i == 0;
    while (i < n) {
        const char c = line[i];
        if (isBlank(c)) {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '/') {
            paint(i, n, TokenKind::Comment);
            break;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '*') {
            const size_t end = line.find("*/", i + 2);
            if (end == std::string_view::npos) {
                paint(i, n, TokenKind::Comment);
                inBlockComment = true;
                break;
            }
            paint(i, end + 2, TokenKind::Comment);
            i = end + 2;
            firstToken = false;
            continue;
        }
        if (c == '#' && firstToken) {
            size_t end = n;
            const size_t lineComment = line.find("//", i);
            const size_t blockComment = line.find("/*", i);
            end = std::min({end, lineComment == std::string_view::npos ? n : lineComment, blockComment == std::string_view::npos ? n : blockComment});
            size_t word = i + 1;
            skipBlank(line, word);
            bool directive = false;
            if (startsWithNoCase(line, word, "pragma")) {
                size_t next = word + 6;
                skipBlank(line, next);
                directive = startsWithNoCase(line, next, "nyrexvj");
            }
            paint(i, end, directive ? TokenKind::Directive : TokenKind::Preprocessor);
            i = end;
            firstToken = false;
            continue;
        }
        firstToken = false;
        const bool digitStart = std::isdigit(static_cast<unsigned char>(c)) != 0;
        const bool dotDigit = c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(line[i + 1]));
        if (digitStart || dotDigit) {
            size_t j = i + 1;
            while (j < n) {
                const char d = line[j];
                if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' || d == '_') {
                    ++j;
                } else if ((d == '+' || d == '-') && (line[j - 1] == 'e' || line[j - 1] == 'E') && !(j >= 2 && line[j - 2] == 'x')) {
                    ++j;
                } else {
                    break;
                }
            }
            paint(i, j, TokenKind::Number);
            i = j;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i + 1;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) {
                ++j;
            }
            paint(i, j, classify(line.substr(i, j - i)));
            i = j;
            continue;
        }
        ++i;
    }
    std::vector<Span> spans;
    size_t start = 0;
    for (size_t k = 1; k <= n; ++k) {
        if (k == n || kinds[k] != kinds[start]) {
            spans.push_back(Span{static_cast<uint32_t>(start), static_cast<uint32_t>(k - start), kinds[start]});
            start = k;
        }
    }
    return spans;
}

std::vector<std::vector<Span>> highlightAll(std::string_view text) {
    std::vector<std::vector<Span>> result;
    bool block = false;
    size_t begin = 0;
    while (true) {
        const size_t end = text.find('\n', begin);
        const std::string_view line = end == std::string_view::npos ? text.substr(begin) : text.substr(begin, end - begin);
        result.push_back(highlightLine(line, block));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

std::vector<Diagnostic> parseCompileLog(std::string_view log) {
    std::vector<Diagnostic> result;
    std::string pass;
    size_t begin = 0;
    while (begin <= log.size() && result.size() < kMaxDiagnostics) {
        size_t end = log.find('\n', begin);
        if (end == std::string_view::npos) {
            end = log.size();
        }
        const std::string text = trim(log.substr(begin, end - begin));
        begin = end + 1;
        if (text.empty()) {
            continue;
        }
        const std::string lowerText = lower(text);
        if (text.back() == ':' && text.size() <= 24 && std::isalpha(static_cast<unsigned char>(text[0])) && text.find(':') == text.size() - 1 && lowerText != "error:" && lowerText != "warning:") {
            pass = text.substr(0, text.size() - 1);
            continue;
        }
        Diagnostic diagnostic;
        diagnostic.pass = pass;
        size_t i = 0;
        if (startsWithNoCase(text, 0, "line ")) {
            size_t j = 5;
            int number = 0;
            if (readUnsigned(text, j, number)) {
                skipBlank(text, j);
                if (j < text.size() && text[j] == ':') {
                    ++j;
                }
                diagnostic.line = number;
                skipBlank(text, j);
                diagnostic.message = clipMessage(std::string_view(text).substr(j));
                result.push_back(std::move(diagnostic));
                continue;
            }
        }
        bool prefixed = false;
        if (startsWithNoCase(text, 0, "error:")) {
            i = 6;
            prefixed = true;
        } else if (startsWithNoCase(text, 0, "warning:")) {
            i = 8;
            prefixed = true;
            diagnostic.severity = Severity::Warning;
        }
        skipBlank(text, i);
        int line = 0;
        int column = 0;
        size_t position = i;
        if (parseLocation(text, position, line, column)) {
            diagnostic.line = line;
            diagnostic.column = column;
            skipBlank(text, position);
            if (startsWithNoCase(text, position, "warning")) {
                diagnostic.severity = Severity::Warning;
                position += 7;
            } else if (startsWithNoCase(text, position, "error")) {
                diagnostic.severity = Severity::Error;
                position += 5;
            }
            skipBlank(text, position);
            if (position < text.size() && std::toupper(static_cast<unsigned char>(text[position])) == 'C') {
                size_t code = position + 1;
                while (code < text.size() && std::isdigit(static_cast<unsigned char>(text[code]))) {
                    ++code;
                }
                if (code > position + 1) {
                    position = code;
                }
            }
            skipBlank(text, position);
            if (position < text.size() && text[position] == ':') {
                ++position;
            }
            skipBlank(text, position);
            diagnostic.message = clipMessage(std::string_view(text).substr(position));
        } else {
            if (!prefixed && startsWithNoCase(text, 0, "warning")) {
                diagnostic.severity = Severity::Warning;
            }
            diagnostic.message = clipMessage(text);
        }
        result.push_back(std::move(diagnostic));
    }
    return result;
}

int firstErrorLine(const std::vector<Diagnostic>& diagnostics) {
    for (const Diagnostic& d : diagnostics) {
        if (d.severity == Severity::Error && d.line > 0) {
            return d.line;
        }
    }
    return 0;
}

void CompileScheduler::edited(double now) {
    if (!pending_) {
        firstEdit_ = now;
    }
    pending_ = true;
    lastEdit_ = now;
}

void CompileScheduler::requestNow(double now) {
    if (!pending_) {
        firstEdit_ = now;
    }
    pending_ = true;
    forced_ = true;
    lastEdit_ = now;
}

bool CompileScheduler::due(double now) const {
    if (!pending_ || inFlight_) {
        return false;
    }
    if (now - lastStart_ < timing_.minIntervalSeconds) {
        return false;
    }
    if (forced_) {
        return true;
    }
    return now - lastEdit_ >= timing_.quietSeconds || now - firstEdit_ >= timing_.maxWaitSeconds;
}

void CompileScheduler::begin(double now) {
    inFlight_ = true;
    pending_ = false;
    forced_ = false;
    lastStart_ = now;
}

void CompileScheduler::finish(double) {
    inFlight_ = false;
}

double CompileScheduler::secondsUntilDue(double now) const {
    if (!pending_) {
        return -1.0;
    }
    double at = forced_ ? now : std::min(lastEdit_ + timing_.quietSeconds, firstEdit_ + timing_.maxWaitSeconds);
    at = std::max(at, lastStart_ + timing_.minIntervalSeconds);
    return std::max(0.0, at - now);
}

void CompileScheduler::reset() {
    pending_ = false;
    inFlight_ = false;
    forced_ = false;
}

void PreviewGovernor::report(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
        return;
    }
    ++reports_;
    average_ = reports_ == 1 ? milliseconds : average_ * 0.7 + milliseconds * 0.3;
    if (milliseconds >= config_.instantPauseMs) {
        state_ = State::Paused;
        return;
    }
    if (reports_ < config_.minReports) {
        return;
    }
    if (average_ > config_.pauseMs) {
        state_ = State::Paused;
        return;
    }
    if (average_ > config_.slowMs) {
        state_ = State::Throttled;
        interval_ = std::clamp(static_cast<int>(average_ / 8.0) + 1, 2, config_.maxInterval);
    } else if (average_ < config_.slowMs * 0.6 && state_ == State::Throttled) {
        interval_ = std::max(1, interval_ - 1);
        if (interval_ == 1) {
            state_ = State::Running;
        }
    }
}

bool PreviewGovernor::shouldDraw() {
    if (state_ == State::Paused) {
        return false;
    }
    if (++counter_ >= interval_) {
        counter_ = 0;
        return true;
    }
    return false;
}

void PreviewGovernor::resume() {
    reset();
}

void PreviewGovernor::reset() {
    state_ = State::Running;
    interval_ = 1;
    counter_ = 0;
    reports_ = 0;
    average_ = 0.0;
}

NameCheck validateShaderFileName(std::string_view input) {
    NameCheck check;
    std::string name = trim(input);
    if (name.empty()) {
        check.problem = NameProblem::Empty;
        return check;
    }
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool allowed = (std::isalnum(u) != 0 && u < 0x80) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')';
        if (!allowed) {
            check.problem = NameProblem::BadCharacter;
            return check;
        }
    }
    const size_t dot = name.rfind('.');
    std::string stem = name;
    std::string extension = ".glsl";
    if (dot != std::string::npos && dot != 0) {
        extension = lower(name.substr(dot));
        stem = name.substr(0, dot);
        if (!allowedExtension(extension)) {
            check.problem = NameProblem::BadExtension;
            return check;
        }
    }
    if (stem.empty() || stem.front() == '.' || stem.front() == ' ' || stem.back() == ' ' || stem.back() == '.' || stem.find("..") != std::string::npos) {
        check.problem = NameProblem::BadStart;
        return check;
    }
    if (stem.size() + extension.size() > kMaxFileNameBytes) {
        check.problem = NameProblem::TooLong;
        return check;
    }
    std::string first = stem.substr(0, stem.find('.'));
    while (!first.empty() && first.back() == ' ') {
        first.pop_back();
    }
    first = lower(first);
    static const std::set<std::string> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    if (reserved.count(first)) {
        check.problem = NameProblem::Reserved;
        return check;
    }
    check.ok = true;
    check.name = stem + extension;
    return check;
}

unsigned long long hashText(std::string_view text) {
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

FileStamp quickStamp(const fs::path& file) {
    FileStamp stamp;
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(file, ec);
    if (ec || status.type() != fs::file_type::regular) {
        return stamp;
    }
    stamp.exists = true;
    stamp.size = static_cast<unsigned long long>(fs::file_size(file, ec));
    if (ec) {
        stamp.size = 0;
    }
    ec.clear();
    stamp.mtime = static_cast<long long>(fs::last_write_time(file, ec).time_since_epoch().count());
    return stamp;
}

FileStamp fullStamp(const fs::path& file) {
    FileStamp stamp = quickStamp(file);
    if (!stamp.exists) {
        return stamp;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return stamp;
    }
    std::string data(std::min<unsigned long long>(stamp.size, kMaxTextBytes + 1), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    stamp.hash = hashText(data);
    return stamp;
}

bool sameFile(const FileStamp& a, const FileStamp& b) {
    return a.exists == b.exists && a.size == b.size && a.hash == b.hash;
}

SaveResult saveShader(const fs::path& folder, std::string_view fileName, std::string_view text, const FileStamp* loaded, bool overwrite) {
    SaveResult result;
    std::string name(fileName);
    if (loaded) {
        if (!lenientNameOk(name)) {
            result.error = SaveError::BadName;
            return result;
        }
    } else {
        const NameCheck check = validateShaderFileName(name);
        if (!check.ok) {
            result.error = SaveError::BadName;
            return result;
        }
        name = check.name;
    }
    result.name = name;
    const std::string clean = sanitizeText(text);
    if (clean.size() > kMaxTextBytes) {
        result.error = SaveError::TooLarge;
        return result;
    }
    std::error_code ec;
    if (!fs::exists(folder, ec)) {
        fs::create_directories(folder, ec);
        if (ec || !fs::is_directory(folder, ec)) {
            result.error = SaveError::NoFolder;
            return result;
        }
    } else if (!fs::is_directory(folder, ec)) {
        result.error = SaveError::NoFolder;
        return result;
    }
    const fs::path target = folder / pathOf(name);
    const fs::file_status status = fs::symlink_status(target, ec);
    const bool exists = !ec && status.type() != fs::file_type::not_found;
    if (exists && status.type() != fs::file_type::regular) {
        result.error = SaveError::NotAFile;
        return result;
    }
    if (!overwrite) {
        const FileStamp disk = fullStamp(target);
        if (!loaded) {
            if (disk.exists) {
                result.error = SaveError::AlreadyExists;
                return result;
            }
        } else if (!disk.exists) {
            result.error = SaveError::DeletedOnDisk;
            return result;
        } else if (!sameFile(*loaded, disk)) {
            result.error = SaveError::ChangedOnDisk;
            return result;
        }
    }
    if (!writeFileAtomic(target, clean)) {
        result.error = SaveError::CannotWrite;
        return result;
    }
    result.stamp = fullStamp(target);
    result.ok = true;
    return result;
}

ReadResult readShader(const fs::path& folder, std::string_view fileName) {
    ReadResult result;
    const std::string name(fileName);
    if (!lenientNameOk(name)) {
        return result;
    }
    const fs::path target = folder / pathOf(name);
    const FileStamp quick = quickStamp(target);
    if (!quick.exists) {
        return result;
    }
    if (quick.size > kMaxTextBytes) {
        result.tooLarge = true;
        return result;
    }
    std::ifstream in(target, std::ios::binary);
    if (!in) {
        return result;
    }
    std::string data(static_cast<size_t>(quick.size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    result.stamp = quick;
    result.stamp.hash = hashText(data);
    result.stamp.size = data.size();
    result.text = sanitizeText(data);
    result.ok = true;
    return result;
}

std::vector<std::string> listShaderFiles(const fs::path& folder, size_t limit) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return names;
    }
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc) {
            continue;
        }
        const std::string name = utf8Of(it->path().filename());
        if (lenientNameOk(name)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        const std::string la = lower(a);
        const std::string lb = lower(b);
        return la != lb ? la < lb : a < b;
    });
    if (names.size() > limit) {
        names.resize(limit);
    }
    return names;
}

const std::vector<Template>& templates() {
    static const std::vector<Template> list = {
        {"NyrexVJ style", "my_shader.glsl",
         "void main() {\n"
         "    vec2 p = centered();\n"
         "    float d = length(p);\n"
         "    float rings = 0.5 + 0.5 * sin(d * 18.0 - uTime * 3.0 - uBeat * 0.5);\n"
         "    float pulse = 0.4 + uKick * 0.6 + uAudio.x * 0.5;\n"
         "    vec3 col = duo(rings) * pulse;\n"
         "    col += pal(uTime * 0.05 + d) * smoothstep(0.6, 0.0, d) * 0.4;\n"
         "    fragColor = vec4(col, 1.0);\n"
         "}\n"},
        {"Shadertoy style", "shadertoy_style.glsl",
         "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
         "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
         "    float a = atan(uv.y, uv.x);\n"
         "    float r = length(uv);\n"
         "    float v = 0.5 + 0.5 * sin(8.0 * a + 10.0 * r - iTime * 2.0);\n"
         "    vec3 col = 0.5 + 0.5 * cos(iTime + vec3(0.0, 2.0, 4.0) + r * 6.0);\n"
         "    fragColor = vec4(col * v * smoothstep(0.9, 0.2, r), 1.0);\n"
         "}\n"},
        {"Transparent background", "transparent_ring.glsl",
         "#pragma nyrexvj alpha\n"
         "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
         "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\n"
         "    float r = length(uv);\n"
         "    float ring = smoothstep(0.02, 0.0, abs(r - 0.25 - 0.05 * sin(iTime * 3.0)));\n"
         "    fragColor = vec4(1.0, 0.45, 0.1, ring);\n"
         "}\n"},
        {"Two passes with a buffer", "trails.glsl",
         "#pragma nyrexvj buffer a\n"
         "#pragma nyrexvj channel 0 buffer a\n"
         "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
         "    vec2 uv = fragCoord / iResolution.xy;\n"
         "    vec2 spot = 0.5 + 0.3 * vec2(cos(iTime * 1.7), sin(iTime * 2.3));\n"
         "    float glow = smoothstep(0.03, 0.0, length(uv - spot));\n"
         "    fragColor = texture(iChannel0, uv) * 0.96 + vec4(glow, glow * 0.5, glow * 0.2, 0.0);\n"
         "}\n"
         "\n"
         "#pragma nyrexvj image\n"
         "#pragma nyrexvj channel 0 buffer a\n"
         "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
         "    fragColor = vec4(texture(iChannel0, fragCoord / iResolution.xy).rgb, 1.0);\n"
         "}\n"}};
    return list;
}

}

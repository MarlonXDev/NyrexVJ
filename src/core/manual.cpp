#include "core/manual.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

extern const unsigned char nvjManualEn[];
extern const unsigned long long nvjManualEnSize;
extern const unsigned char nvjManualDe[];
extern const unsigned long long nvjManualDeSize;

namespace nvj {

namespace {

std::string rtrim(const std::string& text) {
    size_t end = text.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string() : text.substr(0, end + 1);
}

std::string ltrim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : text.substr(start);
}

std::string trim(const std::string& text) {
    return ltrim(rtrim(text));
}

bool startsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::string lower(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z') {
            result.push_back(static_cast<char>(c + 32));
        } else if (c == 0xC3 && i + 1 < text.size()) {
            const unsigned char next = static_cast<unsigned char>(text[i + 1]);
            result.push_back(static_cast<char>(c));
            if (next >= 0x80 && next <= 0x9E && next != 0x97) {
                result.push_back(static_cast<char>(next + 0x20));
            } else {
                result.push_back(static_cast<char>(next));
            }
            ++i;
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

void appendSpan(ManualText& out, const std::string& text, SpanStyle style, const std::string& target = std::string()) {
    if (text.empty()) {
        return;
    }
    if (!out.empty() && out.back().style == style && style != SpanStyle::Link) {
        out.back().text += text;
        return;
    }
    ManualSpan span;
    span.text = text;
    span.style = style;
    span.target = target;
    out.push_back(std::move(span));
}

std::vector<std::string> splitCells(const std::string& line) {
    std::vector<std::string> cells;
    std::string current;
    std::string body = trim(line);
    if (!body.empty() && body.front() == '|') {
        body.erase(body.begin());
    }
    bool escaped = false;
    for (char c : body) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            current.push_back(c);
            escaped = true;
        } else if (c == '|') {
            cells.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!trim(current).empty()) {
        cells.push_back(trim(current));
    }
    return cells;
}

bool isSeparatorRow(const std::vector<std::string>& cells) {
    if (cells.empty()) {
        return false;
    }
    for (const std::string& cell : cells) {
        if (cell.empty()) {
            return false;
        }
        for (char c : cell) {
            if (c != '-' && c != ':' && c != ' ') {
                return false;
            }
        }
    }
    return true;
}

bool isNumberedItem(const std::string& line, size_t& contentStart) {
    size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    if (i == 0 || i > 3 || i + 1 >= line.size() || line[i] != '.' || line[i + 1] != ' ') {
        return false;
    }
    contentStart = i + 2;
    return true;
}

bool isFigureLine(const std::string& line, std::string& alt, std::string& path) {
    if (!startsWith(line, "![")) {
        return false;
    }
    const size_t close = line.find("](");
    if (close == std::string::npos || line.back() != ')') {
        return false;
    }
    alt = line.substr(2, close - 2);
    path = line.substr(close + 2, line.size() - close - 3);
    return !path.empty();
}

std::string headingId(std::string& title) {
    title = trim(title);
    const size_t open = title.rfind("{#");
    if (open != std::string::npos && !title.empty() && title.back() == '}') {
        std::string id = title.substr(open + 2, title.size() - open - 3);
        title = trim(title.substr(0, open));
        return trim(id);
    }
    return std::string();
}

struct Builder {
    Manual result;
    ManualChapter* chapter = nullptr;
    std::string paragraph;
    std::vector<std::string> listItems;
    BlockKind listKind = BlockKind::Bullets;
    bool inList = false;

    void flushParagraph() {
        if (!paragraph.empty() && chapter) {
            ManualBlock block;
            block.kind = BlockKind::Paragraph;
            block.items.push_back(parseInline(paragraph));
            chapter->blocks.push_back(std::move(block));
        }
        paragraph.clear();
    }

    void flushList() {
        if (inList && chapter && !listItems.empty()) {
            ManualBlock block;
            block.kind = listKind;
            for (const std::string& item : listItems) {
                block.items.push_back(parseInline(item));
            }
            chapter->blocks.push_back(std::move(block));
        }
        listItems.clear();
        inList = false;
    }

    void flushAll() {
        flushParagraph();
        flushList();
    }

    void startChapter(const std::string& title, const std::string& id) {
        flushAll();
        ManualChapter next;
        next.title = title;
        next.id = id.empty() ? slugify(title) : id;
        result.chapters.push_back(std::move(next));
        chapter = &result.chapters.back();
    }
};

}

size_t Manual::indexOf(const std::string& chapterId) const {
    for (size_t i = 0; i < chapters.size(); ++i) {
        if (chapters[i].id == chapterId) {
            return i;
        }
    }
    return chapters.size();
}

std::string slugify(const std::string& title) {
    std::string slug;
    bool pendingDash = false;
    for (size_t i = 0; i < title.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(title[i]);
        std::string add;
        if (std::isalnum(c) && c < 0x80) {
            add.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == 0xC3 && i + 1 < title.size()) {
            const unsigned char next = static_cast<unsigned char>(title[i + 1]);
            ++i;
            if (next == 0xA4 || next == 0x84) {
                add = "ae";
            } else if (next == 0xB6 || next == 0x96) {
                add = "oe";
            } else if (next == 0xBC || next == 0x9C) {
                add = "ue";
            } else if (next == 0x9F) {
                add = "ss";
            } else {
                pendingDash = !slug.empty();
                continue;
            }
        } else if (c >= 0x80) {
            continue;
        } else {
            pendingDash = !slug.empty();
            continue;
        }
        if (pendingDash) {
            slug.push_back('-');
            pendingDash = false;
        }
        slug += add;
    }
    return slug;
}

ManualText parseInline(const std::string& text) {
    ManualText out;
    std::string plain;
    auto flushPlain = [&]() {
        appendSpan(out, plain, SpanStyle::Normal);
        plain.clear();
    };
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            plain.push_back(text[i + 1]);
            i += 2;
        } else if (c == '`') {
            const size_t close = text.find('`', i + 1);
            if (close == std::string::npos) {
                plain.push_back(c);
                ++i;
            } else {
                flushPlain();
                appendSpan(out, text.substr(i + 1, close - i - 1), SpanStyle::Code);
                i = close + 1;
            }
        } else if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            const size_t close = text.find("**", i + 2);
            if (close == std::string::npos || close == i + 2) {
                plain.push_back(c);
                ++i;
            } else {
                flushPlain();
                appendSpan(out, text.substr(i + 2, close - i - 2), SpanStyle::Bold);
                i = close + 2;
            }
        } else if (c == '[') {
            const size_t mid = text.find("](", i + 1);
            const size_t end = mid == std::string::npos ? std::string::npos : text.find(')', mid + 2);
            if (mid == std::string::npos || end == std::string::npos || mid == i + 1) {
                plain.push_back(c);
                ++i;
            } else {
                flushPlain();
                ManualSpan span;
                span.text = text.substr(i + 1, mid - i - 1);
                span.style = SpanStyle::Link;
                span.target = text.substr(mid + 2, end - mid - 2);
                out.push_back(std::move(span));
                i = end + 1;
            }
        } else {
            plain.push_back(c);
            ++i;
        }
    }
    flushPlain();
    return out;
}

std::string plainText(const ManualText& text) {
    std::string result;
    for (const ManualSpan& span : text) {
        result += span.text;
    }
    return result;
}

Manual parseManual(const std::string& markdown) {
    Builder builder;
    const std::string source = markdown.size() > kMaxManualBytes ? markdown.substr(0, kMaxManualBytes) : markdown;
    std::vector<std::string> lines;
    {
        std::istringstream stream(source);
        std::string line;
        while (std::getline(stream, line)) {
            std::replace(line.begin(), line.end(), '\t', ' ');
            lines.push_back(rtrim(line));
        }
    }
    bool inCode = false;
    bool inComment = false;
    ManualBlock code;
    std::vector<std::string> noteLines;
    std::vector<std::string> tableLines;

    auto flushNote = [&]() {
        if (noteLines.empty()) {
            return;
        }
        if (builder.chapter) {
            ManualBlock block;
            block.kind = BlockKind::Note;
            std::string joined;
            for (const std::string& piece : noteLines) {
                if (!joined.empty()) {
                    joined.push_back(' ');
                }
                joined += piece;
            }
            if (startsWith(joined, "Tip:") || startsWith(joined, "Tipp:")) {
                block.note = NoteKind::Tip;
                joined = trim(joined.substr(joined.find(':') + 1));
            } else if (startsWith(joined, "Warning:") || startsWith(joined, "Warnung:")) {
                block.note = NoteKind::Warning;
                joined = trim(joined.substr(joined.find(':') + 1));
            } else if (startsWith(joined, "Note:") || startsWith(joined, "Hinweis:")) {
                block.note = NoteKind::Note;
                joined = trim(joined.substr(joined.find(':') + 1));
            }
            block.items.push_back(parseInline(joined));
            builder.chapter->blocks.push_back(std::move(block));
        }
        noteLines.clear();
    };

    auto flushTable = [&]() {
        if (tableLines.empty()) {
            return;
        }
        if (builder.chapter) {
            ManualBlock block;
            block.kind = BlockKind::Table;
            std::vector<std::vector<std::string>> rows;
            for (const std::string& row : tableLines) {
                std::vector<std::string> cells = splitCells(row);
                if (!isSeparatorRow(cells)) {
                    rows.push_back(std::move(cells));
                }
            }
            size_t columns = 0;
            for (const std::vector<std::string>& row : rows) {
                columns = std::max(columns, row.size());
            }
            block.columns = static_cast<int>(columns);
            for (std::vector<std::string>& row : rows) {
                row.resize(columns);
                for (const std::string& cell : row) {
                    block.items.push_back(parseInline(cell));
                }
            }
            if (!rows.empty() && columns > 0) {
                builder.chapter->blocks.push_back(std::move(block));
            }
        }
        tableLines.clear();
    };

    auto flushOthers = [&]() {
        builder.flushAll();
        flushNote();
        flushTable();
    };

    for (const std::string& line : lines) {
        if (inCode) {
            if (startsWith(ltrim(line), "```")) {
                inCode = false;
                if (builder.chapter) {
                    if (!code.text.empty() && code.text.back() == '\n') {
                        code.text.pop_back();
                    }
                    builder.chapter->blocks.push_back(code);
                }
                code = ManualBlock();
            } else {
                code.text += line;
                code.text.push_back('\n');
            }
            continue;
        }
        if (inComment) {
            if (line.find("-->") != std::string::npos) {
                inComment = false;
            }
            continue;
        }
        const std::string trimmed = ltrim(line);
        if (startsWith(trimmed, "<!--")) {
            if (trimmed.find("-->") == std::string::npos) {
                inComment = true;
            }
            continue;
        }
        if (startsWith(trimmed, "```")) {
            flushOthers();
            inCode = true;
            code = ManualBlock();
            code.kind = BlockKind::Code;
            continue;
        }
        if (trimmed.empty()) {
            flushOthers();
            continue;
        }
        if (!tableLines.empty() && trimmed.front() != '|') {
            flushTable();
        }
        if (!noteLines.empty() && !startsWith(trimmed, ">")) {
            flushNote();
        }
        if (trimmed.front() == '#') {
            size_t level = 0;
            while (level < trimmed.size() && trimmed[level] == '#') {
                ++level;
            }
            if (level <= 3 && level < trimmed.size() && trimmed[level] == ' ') {
                flushOthers();
                std::string title = trimmed.substr(level + 1);
                const std::string id = headingId(title);
                if (level == 1) {
                    builder.startChapter(title, id);
                } else if (builder.chapter) {
                    ManualBlock block;
                    block.kind = BlockKind::Subheading;
                    block.level = static_cast<int>(level);
                    block.id = id.empty() ? slugify(title) : id;
                    block.items.push_back(parseInline(title));
                    builder.chapter->blocks.push_back(std::move(block));
                }
                continue;
            }
        }
        if (trimmed.front() == '|') {
            builder.flushAll();
            flushNote();
            tableLines.push_back(trimmed);
            continue;
        }
        if (trimmed.front() == '>') {
            builder.flushAll();
            std::string body = trimmed.substr(1);
            noteLines.push_back(trim(body));
            continue;
        }
        std::string alt;
        std::string path;
        if (isFigureLine(trimmed, alt, path)) {
            flushOthers();
            if (builder.chapter) {
                ManualBlock block;
                block.kind = BlockKind::Figure;
                block.alt = alt;
                block.text = path;
                builder.chapter->blocks.push_back(std::move(block));
            }
            continue;
        }
        size_t contentStart = 0;
        const bool bullet = (startsWith(trimmed, "- ") || startsWith(trimmed, "* ")) && line.size() == trimmed.size();
        const bool numbered = line.size() == trimmed.size() && isNumberedItem(trimmed, contentStart);
        if (bullet || numbered) {
            builder.flushParagraph();
            const BlockKind kind = bullet ? BlockKind::Bullets : BlockKind::Numbered;
            if (builder.inList && builder.listKind != kind) {
                builder.flushList();
            }
            builder.inList = true;
            builder.listKind = kind;
            builder.listItems.push_back(trim(bullet ? trimmed.substr(2) : trimmed.substr(contentStart)));
            continue;
        }
        if (builder.inList && line.size() != trimmed.size() && !builder.listItems.empty()) {
            builder.listItems.back() += " " + trimmed;
            continue;
        }
        builder.flushList();
        if (!builder.paragraph.empty()) {
            builder.paragraph.push_back(' ');
        }
        builder.paragraph += trimmed;
    }
    flushOthers();
    return std::move(builder.result);
}

const Manual& manual(bool german) {
    static const Manual english = parseManual(std::string(reinterpret_cast<const char*>(nvjManualEn), static_cast<size_t>(nvjManualEnSize)));
    static const Manual deutsch = parseManual(std::string(reinterpret_cast<const char*>(nvjManualDe), static_cast<size_t>(nvjManualDeSize)));
    return german && !deutsch.chapters.empty() ? deutsch : english;
}

bool isInternalTarget(const std::string& target) {
    return !target.empty() && target.front() == '#';
}

bool isAllowedManualLink(const std::string& target) {
    return target.rfind("https://github.com/", 0) == 0 && target.size() > 19 && target.find(' ') == std::string::npos;
}

ManualLocation resolveTarget(const Manual& manual, const std::string& target) {
    ManualLocation location;
    if (!isInternalTarget(target)) {
        return location;
    }
    std::string body = target.substr(1);
    std::string section;
    const size_t slash = body.find('/');
    if (slash != std::string::npos) {
        section = body.substr(slash + 1);
        body = body.substr(0, slash);
    }
    const size_t index = manual.indexOf(body);
    if (index >= manual.chapters.size()) {
        return location;
    }
    if (!section.empty()) {
        bool found = false;
        for (const ManualBlock& block : manual.chapters[index].blocks) {
            if (block.kind == BlockKind::Subheading && block.id == section) {
                found = true;
                break;
            }
        }
        if (!found) {
            return location;
        }
    }
    location.chapter = index;
    location.section = section;
    location.valid = true;
    return location;
}

namespace {

std::string blockText(const ManualBlock& block) {
    std::string result;
    switch (block.kind) {
    case BlockKind::Code:
        return block.text;
    case BlockKind::Figure:
        return block.alt;
    default:
        break;
    }
    for (const ManualText& item : block.items) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += plainText(item);
    }
    return result;
}

std::vector<std::string> splitWords(const std::string& query) {
    std::vector<std::string> words;
    std::istringstream stream(lower(query));
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

std::string snippetAround(const std::string& text, const std::string& word) {
    const std::string haystack = lower(text);
    size_t at = haystack.find(word);
    if (at == std::string::npos) {
        at = 0;
    }
    size_t begin = at > 40 ? at - 40 : 0;
    while (begin > 0 && begin < text.size() && (static_cast<unsigned char>(text[begin]) & 0xC0) == 0x80) {
        --begin;
    }
    size_t end = std::min(text.size(), begin + 110);
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        ++end;
    }
    std::string snippet = text.substr(begin, end - begin);
    std::replace(snippet.begin(), snippet.end(), '\n', ' ');
    return (begin > 0 ? "..." : "") + snippet + (end < text.size() ? "..." : "");
}

}

std::vector<ManualHit> searchManual(const Manual& manual, const std::string& query) {
    std::vector<ManualHit> hits;
    const std::vector<std::string> words = splitWords(query);
    if (words.empty()) {
        return hits;
    }
    for (size_t c = 0; c < manual.chapters.size(); ++c) {
        const ManualChapter& chapter = manual.chapters[c];
        const std::string title = lower(chapter.title);
        std::string headings;
        std::string body;
        for (const ManualBlock& block : chapter.blocks) {
            if (block.kind == BlockKind::Subheading) {
                headings += lower(blockText(block)) + "\n";
            } else {
                body += lower(blockText(block)) + "\n";
            }
        }
        int score = 0;
        bool all = true;
        for (const std::string& word : words) {
            if (title.find(word) != std::string::npos) {
                score += 10;
            } else if (headings.find(word) != std::string::npos) {
                score += 5;
            } else if (body.find(word) != std::string::npos) {
                score += 1;
            } else {
                all = false;
                break;
            }
        }
        if (!all) {
            continue;
        }
        ManualHit hit;
        hit.chapter = c;
        hit.score = score;
        std::string currentSection;
        bool located = false;
        for (const ManualBlock& block : chapter.blocks) {
            if (block.kind == BlockKind::Subheading) {
                currentSection = block.id;
            }
            const std::string text = blockText(block);
            if (lower(text).find(words.front()) != std::string::npos) {
                hit.section = block.kind == BlockKind::Subheading ? block.id : currentSection;
                hit.snippet = block.kind == BlockKind::Subheading ? std::string() : snippetAround(text, words.front());
                located = true;
                break;
            }
        }
        if (!located) {
            hit.section.clear();
        }
        hits.push_back(std::move(hit));
    }
    std::stable_sort(hits.begin(), hits.end(), [](const ManualHit& a, const ManualHit& b) { return a.score > b.score; });
    return hits;
}

std::string manualOutline(const Manual& manual) {
    std::ostringstream out;
    for (const ManualChapter& chapter : manual.chapters) {
        out << "C " << chapter.id << "|" << chapter.title << "\n";
        for (const ManualBlock& block : chapter.blocks) {
            size_t spans = 0;
            size_t links = 0;
            for (const ManualText& item : block.items) {
                spans += item.size();
                for (const ManualSpan& span : item) {
                    if (span.style == SpanStyle::Link) {
                        ++links;
                    }
                }
            }
            switch (block.kind) {
            case BlockKind::Subheading:
                out << "S" << block.level << " " << block.id << "|" << plainText(block.items.empty() ? ManualText() : block.items.front()) << "\n";
                break;
            case BlockKind::Paragraph:
                out << "B paragraph spans=" << spans << " links=" << links << "\n";
                break;
            case BlockKind::Bullets:
                out << "B bullets items=" << block.items.size() << " spans=" << spans << " links=" << links << "\n";
                break;
            case BlockKind::Numbered:
                out << "B numbered items=" << block.items.size() << " spans=" << spans << " links=" << links << "\n";
                break;
            case BlockKind::Table:
                out << "B table cells=" << block.items.size() << " columns=" << block.columns << " spans=" << spans << " links=" << links << "\n";
                break;
            case BlockKind::Code: {
                size_t codeLines = 1;
                for (char ch : block.text) {
                    if (ch == '\n') {
                        ++codeLines;
                    }
                }
                out << "B code lines=" << codeLines << "\n";
                break;
            }
            case BlockKind::Note:
                out << "B note " << (block.note == NoteKind::Tip ? "tip" : block.note == NoteKind::Warning ? "warning" : "note") << " spans=" << spans << " links=" << links << "\n";
                break;
            case BlockKind::Figure:
                out << "B figure " << block.text << "|" << block.alt << "\n";
                break;
            }
        }
    }
    return out.str();
}

}

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nvj {

enum class SpanStyle {
    Normal,
    Bold,
    Code,
    Link,
};

struct ManualSpan {
    std::string text;
    SpanStyle style = SpanStyle::Normal;
    std::string target;
};

using ManualText = std::vector<ManualSpan>;

enum class BlockKind {
    Subheading,
    Paragraph,
    Bullets,
    Numbered,
    Table,
    Code,
    Note,
    Figure,
};

enum class NoteKind {
    Note,
    Tip,
    Warning,
};

struct ManualBlock {
    BlockKind kind = BlockKind::Paragraph;
    int level = 0;
    std::string id;
    NoteKind note = NoteKind::Note;
    std::vector<ManualText> items;
    int columns = 0;
    std::string text;
    std::string alt;
};

struct ManualChapter {
    std::string id;
    std::string title;
    std::vector<ManualBlock> blocks;
};

struct Manual {
    std::vector<ManualChapter> chapters;
    size_t indexOf(const std::string& chapterId) const;
};

constexpr size_t kMaxManualBytes = 1024u * 1024u;

Manual parseManual(const std::string& markdown);
const Manual& manual(bool german);

ManualText parseInline(const std::string& text);
std::string plainText(const ManualText& text);
std::string slugify(const std::string& title);

struct ManualLocation {
    size_t chapter = 0;
    std::string section;
    bool valid = false;
};

ManualLocation resolveTarget(const Manual& manual, const std::string& target);
bool isInternalTarget(const std::string& target);
bool isAllowedManualLink(const std::string& target);

struct ManualHit {
    size_t chapter = 0;
    std::string section;
    int score = 0;
    std::string snippet;
};

std::vector<ManualHit> searchManual(const Manual& manual, const std::string& query);

std::string manualOutline(const Manual& manual);

}

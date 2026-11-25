#include "check.hpp"
#include "core/manual.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef NVJ_MANUAL_FIXTURES
#define NVJ_MANUAL_FIXTURES "tests/data"
#endif

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::uint32_t rngState = 987654321u;

std::uint32_t nextRandom() {
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

void testInline() {
    using nvj::SpanStyle;
    nvj::ManualText text = nvj::parseInline("plain **bold** `code` [label](#target) end");
    CHECK(text.size() == 7);
    CHECK(text[0].text == "plain " && text[0].style == SpanStyle::Normal);
    CHECK(text[1].text == "bold" && text[1].style == SpanStyle::Bold);
    CHECK(text[2].text == " ");
    CHECK(text[3].text == "code" && text[3].style == SpanStyle::Code);
    CHECK(text[5].text == "label" && text[5].style == SpanStyle::Link && text[5].target == "#target");
    CHECK(text[6].text == " end");
    CHECK(nvj::plainText(text) == "plain bold code label end");

    CHECK(nvj::plainText(nvj::parseInline("a \\*b\\* c")) == "a *b* c");
    CHECK(nvj::plainText(nvj::parseInline("lone ** marker")) == "lone ** marker");
    CHECK(nvj::plainText(nvj::parseInline("open `tick")) == "open `tick");
    CHECK(nvj::plainText(nvj::parseInline("broken [link(x)")) == "broken [link(x)");
    CHECK(nvj::plainText(nvj::parseInline("empty [](#x) link")) == "empty [](#x) link");
    CHECK(nvj::plainText(nvj::parseInline("`**not bold**`")) == "**not bold**");
    CHECK(nvj::parseInline("").empty());
    CHECK(nvj::parseInline("**a****b**").size() == 1);
    CHECK(nvj::plainText(nvj::parseInline("**a****b**")) == "ab");
}

void testSlug() {
    CHECK(nvj::slugify("Hello World") == "hello-world");
    CHECK(nvj::slugify("  Spaces,  and, commas!  ") == "spaces-and-commas");
    CHECK(nvj::slugify("Straße, Überschrift") == "strasse-ueberschrift");
    CHECK(nvj::slugify("Köln Ärger Öl") == "koeln-aerger-oel");
    CHECK(nvj::slugify("日本語") == "");
    CHECK(nvj::slugify("a---b") == "a-b");
}

void testBlocks() {
    const nvj::Manual m = nvj::parseManual(readFile(std::string(NVJ_MANUAL_FIXTURES) + "/manual_fixture.md"));
    CHECK(m.chapters.size() == 2);
    CHECK(m.chapters[0].id == "first");
    CHECK(m.chapters[0].title == "First chapter");
    CHECK(m.chapters[1].id == "strasse-ueberschrift");
    CHECK(m.indexOf("first") == 0);
    CHECK(m.indexOf("nothing") == m.chapters.size());

    const std::vector<nvj::ManualBlock>& blocks = m.chapters[0].blocks;
    CHECK(blocks.size() == 11);
    if (blocks.size() == 11) {
        CHECK(blocks[0].kind == nvj::BlockKind::Paragraph);
        CHECK(nvj::plainText(blocks[0].items[0]).find("Second line of the same paragraph.") != std::string::npos);
        CHECK(blocks[1].kind == nvj::BlockKind::Subheading && blocks[1].level == 2 && blocks[1].id == "sec-a");
        CHECK(blocks[2].kind == nvj::BlockKind::Bullets && blocks[2].items.size() == 3);
        CHECK(nvj::plainText(blocks[2].items[1]) == "item two with code continued line");
        CHECK(blocks[3].kind == nvj::BlockKind::Numbered && blocks[3].items.size() == 2);
        CHECK(blocks[4].kind == nvj::BlockKind::Subheading && blocks[4].level == 3 && blocks[4].id == "deeper-heading");
        CHECK(blocks[5].kind == nvj::BlockKind::Table && blocks[5].columns == 2 && blocks[5].items.size() == 6);
        CHECK(nvj::plainText(blocks[5].items[2]) == "A");
        CHECK(blocks[5].items[2][0].style == nvj::SpanStyle::Code);
        CHECK(blocks[6].kind == nvj::BlockKind::Code && blocks[6].text == "code line 1\n  code line 2");
        CHECK(blocks[7].kind == nvj::BlockKind::Note && blocks[7].note == nvj::NoteKind::Tip);
        CHECK(nvj::plainText(blocks[7].items[0]) == "a tip with bold.");
        CHECK(blocks[8].kind == nvj::BlockKind::Note && blocks[8].note == nvj::NoteKind::Warning);
        CHECK(nvj::plainText(blocks[8].items[0]) == "a warning over two lines.");
        CHECK(blocks[9].kind == nvj::BlockKind::Note && blocks[9].note == nvj::NoteKind::Note);
        CHECK(blocks[10].kind == nvj::BlockKind::Figure && blocks[10].text == "assets/manual/main.webp" && blocks[10].alt == "A picture");
    }

    const std::vector<nvj::ManualBlock>& second = m.chapters[1].blocks;
    CHECK(second.size() == 2);
    if (second.size() == 2) {
        const std::string text = nvj::plainText(second[1].items[0]);
        CHECK(text.find("Escaped *stars* and a lone ** marker") != std::string::npos);
        CHECK(text.find("[broken link.") != std::string::npos);
        CHECK(text.find("Ä, ö und ß") != std::string::npos);
    }

    std::string expected = readFile(std::string(NVJ_MANUAL_FIXTURES) + "/manual_fixture.outline.txt");
    expected.erase(std::remove(expected.begin(), expected.end(), '\r'), expected.end());
    CHECK(!expected.empty());
    CHECK(nvj::manualOutline(m) == expected);
}

void testTargetsAndSearch() {
    const nvj::Manual m = nvj::parseManual(readFile(std::string(NVJ_MANUAL_FIXTURES) + "/manual_fixture.md"));
    CHECK(nvj::isInternalTarget("#first"));
    CHECK(!nvj::isInternalTarget("https://github.com"));
    CHECK(!nvj::isInternalTarget(""));
    nvj::ManualLocation a = nvj::resolveTarget(m, "#first");
    CHECK(a.valid && a.chapter == 0 && a.section.empty());
    nvj::ManualLocation b = nvj::resolveTarget(m, "#first/sec-a");
    CHECK(b.valid && b.chapter == 0 && b.section == "sec-a");
    nvj::ManualLocation c = nvj::resolveTarget(m, "#strasse-ueberschrift/sec-b");
    CHECK(c.valid && c.chapter == 1 && c.section == "sec-b");
    CHECK(!nvj::resolveTarget(m, "#first/missing").valid);
    CHECK(!nvj::resolveTarget(m, "#missing").valid);
    CHECK(!nvj::resolveTarget(m, "https://example.org").valid);

    CHECK(nvj::searchManual(m, "").empty());
    CHECK(nvj::searchManual(m, "   ").empty());
    CHECK(nvj::searchManual(m, "zzzzzz").empty());
    std::vector<nvj::ManualHit> hits = nvj::searchManual(m, "FIRST");
    CHECK(!hits.empty() && hits.front().chapter == 0 && hits.front().score >= 10);
    hits = nvj::searchManual(m, "section b");
    CHECK(hits.size() == 2 && hits.front().chapter == 1);
    hits = nvj::searchManual(m, "ÜBERSCHRIFT");
    CHECK(hits.size() == 1 && hits.front().chapter == 1);
    hits = nvj::searchManual(m, "ä");
    CHECK(hits.size() == 1 && hits.front().chapter == 1);
    hits = nvj::searchManual(m, "stars marker");
    CHECK(hits.size() == 1 && hits.front().chapter == 1 && !hits.front().snippet.empty());
    hits = nvj::searchManual(m, "tip");
    CHECK(hits.size() == 1 && hits.front().chapter == 0);
    hits = nvj::searchManual(m, "deeper");
    CHECK(hits.size() == 1 && hits.front().section == "deeper-heading");
    hits = nvj::searchManual(m, "code");
    CHECK(!hits.empty());
    nvj::Manual empty;
    CHECK(nvj::searchManual(empty, "anything").empty());
}

void testEdgeCases() {
    CHECK(nvj::parseManual("").chapters.empty());
    CHECK(nvj::parseManual("just text\n\nno chapter").chapters.empty());
    CHECK(nvj::parseManual("## section only").chapters.empty());
    nvj::Manual one = nvj::parseManual("# Only\n");
    CHECK(one.chapters.size() == 1 && one.chapters[0].blocks.empty() && one.chapters[0].id == "only");
    nvj::Manual crlf = nvj::parseManual("# A {#a}\r\n\r\ntext\r\nmore\r\n\r\n- x\r\n- y\r\n");
    CHECK(crlf.chapters.size() == 1 && crlf.chapters[0].blocks.size() == 2);
    nvj::Manual open = nvj::parseManual("# A\n\n```\nnever closed\n");
    CHECK(open.chapters.size() == 1 && open.chapters[0].blocks.empty());
    nvj::Manual comment = nvj::parseManual("# A\n<!--\nhidden\n-->\nshown\n");
    CHECK(comment.chapters.size() == 1 && comment.chapters[0].blocks.size() == 1);
    nvj::Manual ragged = nvj::parseManual("# A\n\n| a | b | c |\n| --- | --- |\n| 1 | 2 |\n");
    CHECK(ragged.chapters[0].blocks.size() == 1 && ragged.chapters[0].blocks[0].columns == 3 && ragged.chapters[0].blocks[0].items.size() == 6);
    nvj::Manual mixed = nvj::parseManual("# A\n\n- a\n1. b\n- c\n");
    CHECK(mixed.chapters[0].blocks.size() == 3);
    nvj::Manual deep = nvj::parseManual("# A\n\n#### too deep\n");
    CHECK(deep.chapters[0].blocks.size() == 1 && deep.chapters[0].blocks[0].kind == nvj::BlockKind::Paragraph);
    std::string big = "# Big\n\n";
    while (big.size() < nvj::kMaxManualBytes + 5000) {
        big += "line of text that repeats many times\n\n";
    }
    nvj::Manual capped = nvj::parseManual(big);
    CHECK(capped.chapters.size() == 1);
    std::size_t total = 0;
    for (const nvj::ManualBlock& block : capped.chapters[0].blocks) {
        total += block.items.empty() ? 0 : nvj::plainText(block.items[0]).size();
    }
    CHECK(total <= nvj::kMaxManualBytes);
}

void testHostile() {
    const char alphabet[] = "#*`[]()|>-!\\{}\n  abAB12.:_~\xC3\xA4\xC3\x9F";
    const std::size_t alphabetSize = sizeof(alphabet) - 1;
    int survived = 0;
    for (int round = 0; round < 4000; ++round) {
        std::string text = round % 2 == 0 ? "# T\n\n" : "";
        const std::size_t length = 1 + nextRandom() % 400;
        for (std::size_t i = 0; i < length; ++i) {
            text.push_back(alphabet[nextRandom() % alphabetSize]);
        }
        nvj::Manual parsed = nvj::parseManual(text);
        std::string outline = nvj::manualOutline(parsed);
        std::vector<nvj::ManualHit> hits = nvj::searchManual(parsed, "a b");
        for (const nvj::ManualChapter& chapter : parsed.chapters) {
            for (const nvj::ManualBlock& block : chapter.blocks) {
                if (block.kind == nvj::BlockKind::Table) {
                    CHECK(block.columns > 0 && block.items.size() % static_cast<std::size_t>(block.columns) == 0);
                }
                for (const nvj::ManualText& item : block.items) {
                    for (const nvj::ManualSpan& span : item) {
                        CHECK(!span.text.empty());
                    }
                }
            }
        }
        (void)outline;
        (void)hits;
        ++survived;
    }
    CHECK(survived == 4000);
    std::printf("manual hostile text: %d inputs parsed\n", survived);
}

std::set<std::string> sectionIds(const nvj::ManualChapter& chapter) {
    std::set<std::string> ids;
    for (const nvj::ManualBlock& block : chapter.blocks) {
        if (block.kind == nvj::BlockKind::Subheading) {
            ids.insert(block.id);
        }
    }
    return ids;
}

void checkRealManual(const nvj::Manual& m, const char* name) {
    std::printf("manual %s: %zu chapters\n", name, m.chapters.size());
    CHECK(m.chapters.size() >= 20);
    std::set<std::string> chapterIds;
    std::size_t words = 0;
    for (const nvj::ManualChapter& chapter : m.chapters) {
        CHECK(!chapter.id.empty());
        CHECK(!chapter.title.empty());
        CHECK(chapterIds.insert(chapter.id).second);
        bool hasText = false;
        std::set<std::string> ids;
        for (const nvj::ManualBlock& block : chapter.blocks) {
            if (block.kind == nvj::BlockKind::Subheading) {
                CHECK(!block.id.empty());
                CHECK(ids.insert(block.id).second);
            }
            if (block.kind == nvj::BlockKind::Paragraph || block.kind == nvj::BlockKind::Bullets || block.kind == nvj::BlockKind::Numbered) {
                hasText = true;
            }
            if (block.kind == nvj::BlockKind::Table) {
                CHECK(block.columns >= 2 && block.items.size() % static_cast<std::size_t>(block.columns) == 0);
            }
            for (const nvj::ManualText& item : block.items) {
                for (const nvj::ManualSpan& span : item) {
                    CHECK(!span.text.empty());
                    CHECK(span.text.find("**") == std::string::npos);
                    CHECK(span.text.find('`') == std::string::npos);
                    if (span.style == nvj::SpanStyle::Link) {
                        CHECK(!span.target.empty());
                        if (nvj::isInternalTarget(span.target)) {
                            CHECK(nvj::resolveTarget(m, span.target).valid);
                        } else {
                            CHECK(span.target.rfind("https://github.com/MarlonXDev/NyrexVJ", 0) == 0 || span.target.rfind("https://", 0) == 0);
                        }
                    }
                    if (span.style != nvj::SpanStyle::Code) {
                        std::istringstream counter(span.text);
                        std::string word;
                        while (counter >> word) {
                            ++words;
                        }
                    }
                }
            }
        }
        CHECK(hasText);
    }
    std::printf("manual %s: about %zu words\n", name, words);
    CHECK(words >= 3000);
}

void testRealManuals() {
    const nvj::Manual& english = nvj::manual(false);
    const nvj::Manual& german = nvj::manual(true);
    checkRealManual(english, "en");
    checkRealManual(german, "de");
    CHECK(&english != &german);
    CHECK(english.chapters.size() == german.chapters.size());
    const std::size_t count = std::min(english.chapters.size(), german.chapters.size());
    for (std::size_t i = 0; i < count; ++i) {
        const nvj::ManualChapter& a = english.chapters[i];
        const nvj::ManualChapter& b = german.chapters[i];
        CHECK(a.id == b.id);
        CHECK(sectionIds(a) == sectionIds(b));
        CHECK(a.blocks.size() == b.blocks.size());
        const std::size_t blocks = std::min(a.blocks.size(), b.blocks.size());
        for (std::size_t k = 0; k < blocks; ++k) {
            CHECK(a.blocks[k].kind == b.blocks[k].kind);
            CHECK(a.blocks[k].items.size() == b.blocks[k].items.size());
            CHECK(a.blocks[k].columns == b.blocks[k].columns);
            if (a.blocks[k].kind == nvj::BlockKind::Figure) {
                CHECK(a.blocks[k].text == b.blocks[k].text);
            }
        }
    }
    std::vector<nvj::ManualHit> hits = nvj::searchManual(english, "timeline");
    CHECK(!hits.empty());
    hits = nvj::searchManual(german, "Zeitleiste");
    CHECK(!hits.empty());
    hits = nvj::searchManual(english, "blackout");
    CHECK(!hits.empty());
}

}

int main() {
    testInline();
    testSlug();
    testBlocks();
    testTargetsAndSearch();
    testEdgeCases();
    testHostile();
    testRealManuals();
    return testing::finish();
}

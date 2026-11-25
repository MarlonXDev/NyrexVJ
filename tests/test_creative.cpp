#include "check.hpp"

#include "core/effect_chain.hpp"
#include "core/generator_files.hpp"
#include "core/particles.hpp"
#include "core/text_doc.hpp"
#include "core/text_font.hpp"
#include "core/text_layout.hpp"
#include "render/effect_library.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace {

namespace fs = std::filesystem;
using namespace nvj;

void setDataDir(const fs::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

fs::path scratch(const std::string& name) {
    const fs::path path = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
    return path;
}

void writeBytes(const fs::path& file, const std::string& data) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string readBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::uint64_t hashBytes(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char b : bytes) {
        hash ^= b;
        hash *= 1099511628211ull;
    }
    return hash;
}

fs::path fontPath(const char* name) {
    return fs::path(NVJ_TEST_FONT_DIR) / name;
}

text::Atlas syntheticAtlas() {
    text::Atlas atlas;
    atlas.width = 256;
    atlas.height = 256;
    atlas.pixels.assign(256 * 256, 255);
    atlas.pixelHeight = 100.0f;
    atlas.ascent = 80.0f;
    atlas.descent = -20.0f;
    atlas.lineGap = 0.0f;
    std::u32string chars = U" ?";
    for (char32_t c = U'0'; c <= U'9'; ++c) {
        chars.push_back(c);
    }
    for (char32_t c = U'A'; c <= U'Z'; ++c) {
        chars.push_back(c);
    }
    for (char32_t c = U'a'; c <= U'z'; ++c) {
        chars.push_back(c);
    }
    std::sort(chars.begin(), chars.end());
    float u = 0.0f;
    for (char32_t c : chars) {
        text::GlyphInfo glyph;
        glyph.codepoint = c;
        glyph.advance = 50.0f;
        if (c != U' ') {
            glyph.x0 = 5.0f;
            glyph.x1 = 45.0f;
            glyph.y0 = -80.0f;
            glyph.y1 = 0.0f;
            glyph.u0 = u;
            glyph.u1 = u + 0.01f;
            glyph.v0 = 0.0f;
            glyph.v1 = 0.1f;
            u += 0.012f;
        }
        atlas.glyphs.push_back(glyph);
    }
    return atlas;
}

text::Layout lay(const std::u32string& s, text::LayoutParams params = text::LayoutParams()) {
    static const text::Atlas atlas = syntheticAtlas();
    return text::layoutText(atlas, s, params);
}

void testUtf8() {
    using namespace text;
    DecodeResult r = decodeUtf8("Hallo");
    CHECK(r.text == U"Hallo" && !r.hadInvalid && !r.truncated);
    r = decodeUtf8("Gr\xC3\xBC\xC3\x9F" "e \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xB5");
    CHECK(r.text.size() == 10);
    CHECK(r.text[2] == 0x00FC && r.text[3] == 0x00DF);
    CHECK(r.text[6] == 0x65E5 && r.text[9] == 0x1F3B5);
    CHECK(!r.hadInvalid);
    CHECK(encodeUtf8(r.text) == "Gr\xC3\xBC\xC3\x9F" "e \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xB5");

    r = decodeUtf8("a\x80" "b");
    CHECK(r.text == U"ab" && r.hadInvalid);
    r = decodeUtf8("a\xC0\x80" "b");
    CHECK(r.hadInvalid && r.text.find(U'\0') == std::u32string::npos);
    r = decodeUtf8("a\xE2\x82");
    CHECK(r.hadInvalid && r.text == U"a");
    r = decodeUtf8("a\xED\xA0\x80" "b");
    CHECK(r.hadInvalid && r.text.find(static_cast<char32_t>(0xD800)) == std::u32string::npos);
    r = decodeUtf8("a\xF4\x90\x80\x80" "b");
    CHECK(r.hadInvalid);
    r = decodeUtf8("a\xFF\xFE" "b");
    CHECK(r.hadInvalid && r.text == U"ab");

    r = decodeUtf8(std::string("a\0b", 3));
    CHECK(r.text == U"ab");
    r = decodeUtf8("a\r\nb\tc\x07" "d\x7F" "e");
    CHECK(r.text == U"a\nb cde");
    r = decodeUtf8("a\xE2\x80\x8B" "b\xEF\xBB\xBF" "c\xE2\x80\xAE" "d");
    CHECK(r.text == U"abcd");

    r = decodeUtf8(std::string(kMaxTextBytes + 50, 'x'));
    CHECK(r.truncated && r.text.size() <= kMaxTextCodepoints);
    r = decodeUtf8(std::string(1500, 'x'));
    CHECK(r.text.size() == 1000 && r.truncated);
    std::string cut(kMaxTextBytes - 1, 'x');
    cut += "\xC3\xBC";
    r = decodeUtf8(cut);
    CHECK(r.truncated);
    CHECK(decodeUtf8("").text.empty());
}

void testLayout() {
    using namespace text;
    CHECK(lay(U"").glyphs.empty());
    Layout one = lay(U"AB");
    CHECK(one.glyphs.size() == 2 && one.lines == 1 && one.visibleCount == 2);
    CHECK(std::fabs(one.glyphs[0].x0 - (-45.0f)) < 0.001f);
    CHECK(std::fabs(one.glyphs[1].x1 - 45.0f) < 0.001f);
    CHECK(std::fabs(one.width - 100.0f) < 0.001f);
    CHECK(std::fabs(one.height - 100.0f) < 0.001f);

    LayoutParams left;
    left.hAlign = HAlign::Left;
    Layout l = lay(U"AB", left);
    CHECK(std::fabs(l.glyphs[0].x0 - 5.0f) < 0.001f);
    LayoutParams right;
    right.hAlign = HAlign::Right;
    Layout rr = lay(U"AB", right);
    CHECK(std::fabs(rr.glyphs[1].x1 - (-5.0f)) < 0.001f);

    LayoutParams top;
    top.vAlign = VAlign::Top;
    CHECK(std::fabs(lay(U"A", top).glyphs[0].y0 - 0.0f) < 0.001f);
    CHECK(std::fabs(lay(U"A").glyphs[0].y0 - (-50.0f)) < 0.001f);
    LayoutParams bottom;
    bottom.vAlign = VAlign::Bottom;
    CHECK(std::fabs(lay(U"A", bottom).glyphs[0].y0 - (-100.0f)) < 0.001f);

    Layout two = lay(U"A\nB");
    CHECK(two.lines == 2 && two.glyphs.size() == 2);
    CHECK(std::fabs((two.glyphs[1].y0 - two.glyphs[0].y0) - 100.0f) < 0.001f);
    CHECK(two.glyphs[0].line == 0 && two.glyphs[1].line == 1);
    LayoutParams spaced;
    spaced.lineSpacing = 1.5f;
    Layout twoSpaced = lay(U"A\nB", spaced);
    CHECK(std::fabs((twoSpaced.glyphs[1].y0 - twoSpaced.glyphs[0].y0) - 150.0f) < 0.001f);

    LayoutParams tracked;
    tracked.hAlign = HAlign::Left;
    tracked.tracking = 0.1f;
    Layout t = lay(U"AB", tracked);
    CHECK(std::fabs((t.glyphs[1].x0 - t.glyphs[0].x0) - 60.0f) < 0.001f);

    LayoutParams wrap;
    wrap.wrapWidth = 250.0f;
    wrap.hAlign = HAlign::Left;
    Layout w = lay(U"AAAA BBBB CCCC", wrap);
    CHECK(w.lines == 3 && w.glyphs.size() == 12);
    int perLine[3] = {0, 0, 0};
    for (const PlacedGlyph& g : w.glyphs) {
        CHECK(g.line >= 0 && g.line < 3);
        if (g.line >= 0 && g.line < 3) {
            ++perLine[g.line];
        }
        CHECK(g.x1 <= wrap.wrapWidth + 0.001f);
    }
    CHECK(perLine[0] == 4 && perLine[1] == 4 && perLine[2] == 4);
    CHECK(std::fabs(w.glyphs[4].x0 - 5.0f) < 0.001f);

    LayoutParams narrow;
    narrow.wrapWidth = 120.0f;
    narrow.hAlign = HAlign::Left;
    Layout longWord = lay(U"AAAAAAAAAA", narrow);
    CHECK(longWord.lines == 5 && longWord.glyphs.size() == 10);

    LayoutParams carry;
    carry.wrapWidth = 200.0f;
    carry.hAlign = HAlign::Left;
    Layout c = lay(U"AA BBBBB", carry);
    CHECK(c.lines == 3 && c.glyphs.size() == 7);
    CHECK(c.glyphs[2].line == 1 && std::fabs(c.glyphs[2].x0 - 5.0f) < 0.001f);

    std::u32string many;
    for (int i = 0; i < 100; ++i) {
        many += U"A\n";
    }
    Layout capped = lay(many);
    CHECK(capped.lines <= kMaxLines && capped.truncatedLines);

    Layout unknown = lay(U"A\u00a7");
    CHECK(unknown.glyphs.size() == 2);
    CHECK(lay(U"   ").glyphs.empty());
    Layout spacesOnly = lay(U"A B");
    CHECK(spacesOnly.glyphs.size() == 2 && spacesOnly.glyphs[1].index == 1);

    Layout again = lay(U"AAAA BBBB CCCC", wrap);
    bool same = again.glyphs.size() == w.glyphs.size();
    for (size_t i = 0; same && i < w.glyphs.size(); ++i) {
        same = w.glyphs[i].x0 == again.glyphs[i].x0 && w.glyphs[i].y0 == again.glyphs[i].y0;
    }
    CHECK(same);

    Atlas empty;
    CHECK(layoutText(empty, U"A", LayoutParams()).glyphs.empty());
}

void testAnimation() {
    using namespace text;
    PlacedGlyph first;
    first.index = 0;
    PlacedGlyph fifth;
    fifth.index = 4;
    AnimationInput in;
    in.animation = Animation::None;
    GlyphPose p = poseGlyph(first, 10, in);
    CHECK(p.alpha == 1.0f && p.scale == 1.0f && p.offsetX == 0.0f && p.offsetY == 0.0f);

    in.animation = Animation::Typewriter;
    in.time = 0.0;
    CHECK(poseGlyph(first, 10, in).alpha == 0.0f);
    in.time = 0.1;
    CHECK(poseGlyph(first, 10, in).alpha == 1.0f);
    CHECK(poseGlyph(fifth, 10, in).alpha == 0.0f);
    in.time = 10.0 / 12.0 + 0.2;
    CHECK(poseGlyph(fifth, 10, in).alpha == 1.0f);
    in.time = 10.0 / 12.0 + 1.5 + 0.01;
    CHECK(poseGlyph(first, 10, in).alpha == 1.0f);
    CHECK(poseGlyph(fifth, 10, in).alpha == 0.0f);

    in.animation = Animation::Fade;
    in.time = 0.3;
    const float a0 = poseGlyph(first, 10, in).alpha;
    const float a4 = poseGlyph(fifth, 10, in).alpha;
    CHECK(a0 > a4 && a0 <= 1.0f && a4 >= 0.0f);
    in.time = 1.0;
    CHECK(poseGlyph(first, 10, in).alpha == 1.0f);

    in.animation = Animation::SlideUp;
    in.time = 0.05;
    const GlyphPose slide = poseGlyph(first, 10, in);
    CHECK(slide.offsetY < 0.0f && slide.offsetY >= -0.5f);
    in.time = 1.0;
    CHECK(poseGlyph(first, 10, in).offsetY == 0.0f);

    in.animation = Animation::Wave;
    in.amount = 0.5f;
    bool bounded = true;
    for (int i = 0; i < 200; ++i) {
        in.time = i * 0.037;
        const float y = poseGlyph(fifth, 10, in).offsetY;
        bounded = bounded && std::fabs(y) <= 0.3f * 0.5f + 1e-5f;
    }
    CHECK(bounded);

    in.animation = Animation::Pulse;
    in.amount = 1.0f;
    in.beat = 4.0;
    const float onBeat = poseGlyph(first, 10, in).scale;
    in.beat = 4.5;
    const float midBeat = poseGlyph(first, 10, in).scale;
    in.beat = 4.99;
    const float lateBeat = poseGlyph(first, 10, in).scale;
    CHECK(onBeat > midBeat && midBeat > lateBeat && std::fabs(onBeat - 1.6f) < 0.001f && lateBeat >= 1.0f);
    in.amount = 0.0f;
    in.beat = 4.0;
    CHECK(poseGlyph(first, 10, in).scale == 1.0f);

    in.animation = Animation::Audio;
    in.amount = 0.5f;
    in.level = 0.0f;
    CHECK(poseGlyph(first, 10, in).scale == 1.0f);
    in.level = 1.0f;
    CHECK(poseGlyph(first, 10, in).scale > 1.0f);
    in.level = 7.0f;
    CHECK(poseGlyph(first, 10, in).scale <= 1.0f + 0.5f * 0.8f * 1.0f + 1e-4f);

    in.animation = Animation::Bounce;
    in.amount = 1.0f;
    bool inRange = true;
    for (int i = 0; i < 100; ++i) {
        in.beat = i * 0.11;
        const float y = poseGlyph(fifth, 10, in).offsetY;
        inRange = inRange && y >= 0.0f && y <= 0.5f + 1e-5f;
    }
    CHECK(inRange);

    for (int a = 0; a < kAnimationCount; ++a) {
        const Animation animation = static_cast<Animation>(a);
        CHECK(animationFromName(animationName(animation)) == animation);
        AnimationInput bad;
        bad.animation = animation;
        bad.time = std::nan("");
        bad.beat = INFINITY;
        bad.level = std::nan("");
        bad.speed = std::nan("");
        bad.amount = std::nan("");
        const GlyphPose pose = poseGlyph(first, 0, bad);
        CHECK(std::isfinite(pose.alpha) && std::isfinite(pose.scale) && std::isfinite(pose.offsetX) && std::isfinite(pose.offsetY));
    }
    CHECK(animationFromName("nonsense") == Animation::None);
}

void testFont() {
    using namespace text;
    const fs::path roboto = fontPath("Roboto-Medium.ttf");
    FontFace face;
    std::string error;
    const bool haveFont = loadFontFile(roboto, face, error);
    CHECK(haveFont);
    if (!haveFont) {
        std::fprintf(stderr, "font test skipped: %s (%s)\n", roboto.string().c_str(), error.c_str());
        return;
    }
    CHECK(face.loaded());
    CHECK(face.hasGlyph(U'A') && face.hasGlyph(U'\u00e4') && face.hasGlyph(U'\u00df'));
    CHECK(!face.hasGlyph(U'\u65e5'));
    float ascent = 0;
    float descent = 0;
    float gap = 0;
    face.metrics(64.0f, ascent, descent, gap);
    CHECK(ascent > 30.0f && descent < 0.0f && gap >= 0.0f);

    BakeRequest request;
    request.text = U"Hello W\u00f6rld \u00c4\u00d6\u00dc \u00df \u00e9 \u65e5";
    request.pixelHeight = 64.0f;
    request.faces = {&face};
    Atlas atlas;
    CHECK(bakeAtlas(request, atlas, error));
    CHECK(atlas.width > 0 && atlas.width <= kMaxAtlasSide && atlas.width == atlas.height);
    CHECK(atlas.pixels.size() == static_cast<size_t>(atlas.width) * static_cast<size_t>(atlas.height));
    CHECK(std::any_of(atlas.pixels.begin(), atlas.pixels.end(), [](unsigned char v) { return v > 200; }));
    CHECK(atlas.find(U'H') != nullptr && atlas.find(U'\u00f6') != nullptr && atlas.find(U' ') != nullptr);
    CHECK(atlas.find(U'Z') == nullptr);
    CHECK(std::find(atlas.missing.begin(), atlas.missing.end(), U'\u65e5') != atlas.missing.end());
    CHECK(atlas.find(U'\u65e5') == nullptr);
    bool uvOk = true;
    bool sorted = true;
    for (size_t i = 0; i < atlas.glyphs.size(); ++i) {
        const GlyphInfo& g = atlas.glyphs[i];
        uvOk = uvOk && g.u0 >= 0.0f && g.u1 <= 1.0f && g.v0 >= 0.0f && g.v1 <= 1.0f && g.u1 >= g.u0 && g.v1 >= g.v0;
        sorted = sorted && (i == 0 || atlas.glyphs[i - 1].codepoint < g.codepoint);
    }
    CHECK(uvOk && sorted);
    CHECK(atlas.find(U'H')->advance > 10.0f);
    CHECK(atlas.kerningFor(U'Z', U'Z') == 0.0f);
    CHECK(atlas.bytes() == atlas.pixels.size());

    Atlas again;
    CHECK(bakeAtlas(request, again, error));
    CHECK(hashBytes(again.pixels) == hashBytes(atlas.pixels) && again.glyphs.size() == atlas.glyphs.size());

    const Layout layout = layoutText(atlas, request.text, LayoutParams());
    CHECK(layout.glyphs.size() > 10 && layout.lines == 1);

    BakeRequest nothing;
    nothing.text = U"\u65e5\u672c";
    nothing.faces = {&face};
    Atlas none;
    CHECK(bakeAtlas(nothing, none, error));
    CHECK(none.missing.size() == 2 && none.find(U'?') != nullptr && none.find(U'\u65E5') == nullptr);
    const Layout fallbackLayout = layoutText(none, nothing.text, LayoutParams());
    CHECK(fallbackLayout.glyphs.size() == 2 && fallbackLayout.glyphs[0].codepoint == U'\u65E5');
    BakeRequest noFace;
    noFace.text = U"A";
    CHECK(!bakeAtlas(noFace, none, error));

    BakeRequest tiny;
    std::u32string unique;
    for (char32_t c = 0x21; c < 0x7F; ++c) {
        unique.push_back(c);
    }
    for (char32_t c = 0xA1; c < 0x180; ++c) {
        if (face.hasGlyph(c)) {
            unique.push_back(c);
        }
    }
    tiny.text = unique;
    tiny.pixelHeight = 320.0f;
    tiny.maxSide = 256;
    tiny.faces = {&face};
    Atlas squeezed;
    const bool fit = bakeAtlas(tiny, squeezed, error);
    CHECK(!fit || (squeezed.width <= 256 && squeezed.pixelHeight >= static_cast<float>(kMinPixelHeight)));
    CHECK(fit || !error.empty());

    BakeRequest huge;
    for (int i = 0; i < 1200; ++i) {
        huge.text.push_back(static_cast<char32_t>(0x4E00 + i));
    }
    huge.faces = {&face};
    CHECK(!bakeAtlas(huge, none, error));

    BakeRequest sizeClamp;
    sizeClamp.text = U"A";
    sizeClamp.pixelHeight = 100000.0f;
    sizeClamp.faces = {&face};
    Atlas clamped;
    CHECK(bakeAtlas(sizeClamp, clamped, error) && clamped.pixelHeight <= static_cast<float>(kMaxPixelHeight));
    sizeClamp.pixelHeight = -5.0f;
    CHECK(bakeAtlas(sizeClamp, clamped, error) && clamped.pixelHeight >= static_cast<float>(kMinPixelHeight));

    FontFace droid;
    if (loadFontFile(fontPath("DroidSans.ttf"), droid, error)) {
        char32_t onlySecond = 0;
        for (char32_t c = 0x100; c < 0x600 && onlySecond == 0; ++c) {
            if (!face.hasGlyph(c) && droid.hasGlyph(c)) {
                onlySecond = c;
            }
        }
        if (onlySecond != 0) {
            BakeRequest fallback;
            fallback.text = std::u32string(U"A") + onlySecond;
            fallback.faces = {&face, &droid};
            Atlas both;
            CHECK(bakeAtlas(fallback, both, error));
            CHECK(both.find(onlySecond) != nullptr && both.missing.empty());
        }
    }

    std::string text;
    std::vector<unsigned char> bytes;
    CHECK(readFontFile(roboto, bytes, error));
    FontFace fromBytes;
    CHECK(fromBytes.load(bytes, 0, error));

    FontFace bad;
    CHECK(!loadFontFile(fontPath("does-not-exist.ttf"), bad, error));
    CHECK(!bad.load(std::vector<unsigned char>(), 0, error));
    CHECK(!bad.load(std::vector<unsigned char>(100, 0), 0, error));
    std::vector<unsigned char> noise(5000);
    std::mt19937 rng(7);
    for (unsigned char& b : noise) {
        b = static_cast<unsigned char>(rng());
    }
    CHECK(!bad.load(noise, 0, error));
    std::vector<unsigned char> huge2(kMaxFontBytes + 1, 0);
    CHECK(!bad.load(std::move(huge2), 0, error));
    for (size_t cutAt : {size_t(100), size_t(500), size_t(5000), size_t(100000), bytes.size() - 1}) {
        std::vector<unsigned char> cutBytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cutAt));
        FontFace partial;
        CHECK(!partial.load(std::move(cutBytes), 0, error));
    }
    std::vector<unsigned char> wrongTag = bytes;
    wrongTag[0] = 'X';
    CHECK(!bad.load(std::move(wrongTag), 0, error));

    int rejected = 0;
    int accepted = 0;
    std::mt19937 fuzz(12345);
    for (int round = 0; round < 1500; ++round) {
        std::vector<unsigned char> damaged = bytes;
        const int flips = 1 + static_cast<int>(fuzz() % 6);
        const size_t window = round % 3 == 0 ? 400 : (round % 3 == 1 ? 6000 : damaged.size());
        for (int k = 0; k < flips; ++k) {
            damaged[fuzz() % window] = static_cast<unsigned char>(fuzz());
        }
        FontFace candidate;
        if (candidate.load(std::move(damaged), 0, error)) {
            ++accepted;
            BakeRequest probe;
            probe.text = U"Test \u00E4\u00DF Wg";
            probe.faces = {&candidate};
            Atlas probeAtlas;
            bakeAtlas(probe, probeAtlas, error);
        } else {
            ++rejected;
        }
    }
    CHECK(rejected + accepted == 1500 && rejected > 0 && accepted > 0);

    const fs::path folder = scratch("nvj_creative_fonts");
    setDataDir(folder);
    CHECK(availableFonts().size() <= 64 + 5);
    fs::create_directories(fs::path(fontsDirectory()));
    writeBytes(fs::path(fontsDirectory()) / "Mine.ttf", readBytes(roboto));
    writeBytes(fs::path(fontsDirectory()) / "notes.txt", "x");
    writeBytes(fs::path(fontsDirectory()) / "Broken.otf", "not a font");
    const std::vector<FontEntry> fonts = availableFonts();
    CHECK(std::any_of(fonts.begin(), fonts.end(), [](const FontEntry& f) { return f.name == "Mine.ttf" && f.user; }));
    CHECK(std::none_of(fonts.begin(), fonts.end(), [](const FontEntry& f) { return f.name == "notes.txt"; }));
    FontEntry picked;
    CHECK(resolveFont("Mine.ttf", picked) && picked.user);
    CHECK(!resolveFont("No Such Font", picked));
    FontFace brokenFace;
    CHECK(!loadFontFile(fs::path(fontsDirectory()) / "Broken.otf", brokenFace, error));
    CHECK(firstExisting({fs::path("/nonexistent/a"), roboto}) == roboto);
    CHECK(firstExisting({fs::path("/nonexistent/a")}).empty());
    CHECK(!cjkFontCandidates().empty());
}

void testNames() {
    using namespace gen;
    CHECK(cleanName("Hello") == "Hello");
    CHECK(cleanName("../etc/passwd") == ".._etc_passwd" || cleanName("../etc/passwd").find('/') == std::string::npos);
    CHECK(cleanName("a/b\\c").find('/') == std::string::npos && cleanName("a/b\\c").find('\\') == std::string::npos);
    CHECK(cleanName("CON") == "_CON" && cleanName("nul.txt") == "_nul.txt" && cleanName("COM1") == "_COM1" && cleanName("lpt9") == "_lpt9");
    CHECK(cleanName("   ").empty() && cleanName("...").empty() && cleanName("").empty());
    CHECK(cleanName(std::string(300, 'a')).size() == kMaxNameBytes);
    std::string multi;
    for (int i = 0; i < 40; ++i) {
        multi += "\xC3\xBC";
    }
    const std::string cutMulti = cleanName(multi);
    CHECK(cutMulti.size() <= kMaxNameBytes && cutMulti.size() % 2 == 0);
    CHECK(nameIsUsable("Mein Text") && !nameIsUsable("a/b") && !nameIsUsable("") && !nameIsUsable("CON"));
    CHECK(uniqueName("Name", {}) == "Name");
    CHECK(uniqueName("Name", {"name"}) == "Name 2");
    CHECK(uniqueName("Name", {"Name", "Name 2"}) == "Name 3");
    CHECK(uniqueName("", {}) == "Untitled");
    const std::string longName(kMaxNameBytes, 'x');
    CHECK(uniqueName(longName, {longName}).size() <= kMaxNameBytes);
}

void testTextDoc() {
    using namespace text;
    const fs::path folder = scratch("nvj_creative_text");
    setDataDir(folder);

    TextDoc doc;
    doc.text = "Gr\xC3\xBC\xC3\x9F" "e\nZeile 2";
    doc.font = "Serif";
    doc.size = 0.3f;
    doc.hAlign = HAlign::Left;
    doc.vAlign = VAlign::Bottom;
    doc.lineSpacing = 1.4f;
    doc.tracking = 0.2f;
    doc.wrap = 0.6f;
    doc.posX = -0.25f;
    doc.posY = 0.5f;
    doc.shadow = 0.7f;
    doc.kerning = false;
    doc.color = ColorMode::Rainbow;
    doc.animation = Animation::Wave;
    doc.animSpeed = 2.0f;
    doc.animAmount = 0.25f;
    const std::string json = textDocToJson(doc);
    TextDoc back;
    std::string error;
    CHECK(textDocFromJson(json, back, error));
    CHECK(back.text == doc.text && back.font == "Serif" && back.size == 0.3f && back.hAlign == HAlign::Left && back.vAlign == VAlign::Bottom);
    CHECK(back.lineSpacing == 1.4f && back.tracking == 0.2f && back.wrap == 0.6f && back.posX == -0.25f && back.posY == 0.5f);
    CHECK(back.shadow == 0.7f && !back.kerning && back.color == ColorMode::Rainbow && back.animation == Animation::Wave);
    CHECK(back.animSpeed == 2.0f && back.animAmount == 0.25f);

    nlohmann::json node = nlohmann::json::parse(json);
    node["size"] = 1e30;
    node["tracking"] = -50;
    node["wrap"] = 9;
    node["animationSpeed"] = 0;
    node["extra"] = {{"deep", {1, 2, 3}}};
    CHECK(textDocFromJson(node.dump(), back, error));
    CHECK(back.size == 0.9f && back.tracking == -0.5f && back.wrap == 1.0f && back.animSpeed == 0.1f);

    node["font"] = 5;
    node["align"] = "sideways";
    node["animation"] = "explode";
    CHECK(textDocFromJson(node.dump(), back, error));
    CHECK(back.font == "Sans" && back.hAlign == HAlign::Center && back.animation == Animation::None);

    std::vector<std::string> warnings;
    node["text"] = std::string(5000, 'a');
    CHECK(textDocFromJson(node.dump(), back, error, &warnings));
    CHECK(back.text.size() <= kMaxTextBytes && !warnings.empty());

    CHECK(!textDocFromJson("", back, error));
    CHECK(!textDocFromJson("[1,2]", back, error));
    CHECK(!textDocFromJson("{\"format\":\"nyrexvj-show\",\"version\":1,\"text\":\"x\"}", back, error));
    CHECK(!textDocFromJson("{\"format\":\"nyrexvj-text\",\"version\":99,\"text\":\"x\"}", back, error));
    CHECK(!textDocFromJson("{\"format\":\"nyrexvj-text\",\"text\":\"x\"}", back, error));
    CHECK(!textDocFromJson("{\"format\":\"nyrexvj-text\",\"version\":1}", back, error));
    CHECK(!textDocFromJson("{\"format\":\"nyrexvj-text\",\"version\":1,\"text\":\"\xFF\xFE\"}", back, error));
    CHECK(!textDocFromJson(std::string(gen::kMaxDocBytes + 1, ' '), back, error));
    CHECK(textDocFromJson("{\"format\":\"nyrexvj-text\",\"version\":1,\"text\":\"\"}", back, error) && back.text.empty());

    TextDoc messy;
    messy.text = "A\x01\x02" "B";
    messy.size = 99.0f;
    messy.animSpeed = 0.0f;
    clampDoc(messy);
    CHECK(messy.text == "AB" && messy.size == 0.9f && messy.animSpeed == 0.1f);

    CHECK(saveTextDoc("Titel", doc, error));
    CHECK(fs::exists(textDocPath("Titel")));
    CHECK(listTextDocs() == std::vector<std::string>{"Titel"});
    TextDoc loaded;
    CHECK(loadTextDoc("Titel", loaded, error) && loaded.text == doc.text);
    CHECK(!saveTextDoc("../escape", doc, error));
    CHECK(!saveTextDoc("a/b", doc, error));
    CHECK(!saveTextDoc("CON", doc, error));
    CHECK(!saveTextDoc("", doc, error));
    CHECK(!loadTextDoc("../Titel", loaded, error));
    CHECK(!loadTextDoc("Missing", loaded, error));
    writeBytes(textDocPath("Kaputt"), "{not json");
    CHECK(!loadTextDoc("Kaputt", loaded, error));
    writeBytes(textDocPath("Gross"), std::string(gen::kMaxDocBytes + 10, ' '));
    CHECK(!loadTextDoc("Gross", loaded, error));
    writeBytes(fs::path(textsDirectory()) / "bad:name.nyxtext", "{}");
    writeBytes(fs::path(textsDirectory()) / "other.txt", "{}");
    const std::vector<std::string> names = listTextDocs();
    CHECK(std::find(names.begin(), names.end(), "Kaputt") != names.end());
    CHECK(std::none_of(names.begin(), names.end(), [](const std::string& n) { return n == "other"; }));
    CHECK(deleteTextDoc("Titel") && !fs::exists(textDocPath("Titel")));
    CHECK(!deleteTextDoc("../Titel"));

    for (int i = 0; i < 100; ++i) {
        writeBytes(textDocPath("Bulk" + std::to_string(1000 + i)), "{}");
    }
    CHECK(listTextDocs().size() <= gen::kMaxFiles);
    CHECK(exampleTextDoc().text == "NyrexVJ");

    for (int m = 0; m < 3; ++m) {
        const ColorMode mode = static_cast<ColorMode>(m);
        CHECK(colorModeFromName(colorModeName(mode)) == mode);
    }
    for (int m = 0; m < 3; ++m) {
        CHECK(hAlignFromName(hAlignName(static_cast<HAlign>(m))) == static_cast<HAlign>(m));
        CHECK(vAlignFromName(vAlignName(static_cast<VAlign>(m))) == static_cast<VAlign>(m));
    }
}

particles::Params steady() {
    particles::Params p;
    p.rate = 100.0f;
    p.burst = 0.0f;
    p.life = 2.0f;
    p.seed = 99;
    p.turbulence = 0.3f;
    return p;
}

std::uint64_t runSliced(const particles::Params& params, double seconds, double slice, float level = 0.0f) {
    particles::System system(2048);
    system.setParams(params);
    system.reset();
    for (double t = 0.0; t <= seconds + 1e-9; t += slice) {
        particles::Drive drive;
        drive.clock = t;
        drive.level = level;
        system.advance(drive);
    }
    return system.stateHash();
}

void testParticles() {
    using namespace particles;
    CHECK(System(1000000).limit() == kHardMaxParticles);
    CHECK(System(1).limit() == kMinLimit);
    CHECK(System(-5).limit() == kMinLimit);
    CHECK(gpuBytes(16384) == 16384 * sizeof(Vertex) && gpuBytes(0) == static_cast<size_t>(kMinLimit) * sizeof(Vertex));
    CHECK(sizeof(Vertex) == 24);

    const std::uint64_t a = runSliced(steady(), 3.0, 1.0 / 60.0);
    const std::uint64_t b = runSliced(steady(), 3.0, 1.0 / 60.0);
    CHECK(a == b);
    Params otherSeed = steady();
    otherSeed.seed = 100;
    CHECK(runSliced(otherSeed, 3.0, 1.0 / 60.0) != a);
    CHECK(runSliced(steady(), 3.0, 1.0 / 30.0) == a);
    CHECK(runSliced(steady(), 3.0, 1.0 / 120.0) == a);
    Params reactiveRate = steady();
    reactiveRate.levelRate = 2.0f;
    CHECK(runSliced(reactiveRate, 3.0, 1.0 / 60.0, 0.5f) != runSliced(reactiveRate, 3.0, 1.0 / 60.0, 0.0f));
    CHECK(runSliced(steady(), 3.0, 1.0 / 60.0, 0.5f) == a);

    {
        System s(2048);
        s.setParams(steady());
        s.reset();
        int peak = 0;
        for (int i = 0; i <= 600; ++i) {
            Drive d;
            d.clock = i / 60.0;
            s.advance(d);
            peak = std::max(peak, s.alive());
        }
        CHECK(peak > 100 && peak < 300);
        CHECK(s.spawned() > 900 && s.spawned() < 1100);
        CHECK(s.dropped() == 0);
        std::vector<Vertex> out;
        Drive d;
        d.clock = 10.0;
        s.collect(out, d);
        CHECK(static_cast<int>(out.size()) == s.alive());
        bool ok = true;
        for (const Vertex& v : out) {
            ok = ok && std::isfinite(v.x) && std::isfinite(v.y) && v.size > 0.0f && v.alpha >= 0.0f && v.alpha <= 1.0f && v.t >= 0.0f && v.t <= 1.0f && v.hue >= 0.0f && v.hue < 1.0f;
        }
        CHECK(ok);
        bool aged = true;
        for (const Particle& p : s.particles()) {
            aged = aged && p.age < p.life;
        }
        CHECK(aged);
    }

    {
        Params flood = steady();
        flood.rate = 2000.0f;
        flood.life = 10.0f;
        System s(100);
        s.setParams(flood);
        s.reset();
        bool capped = true;
        for (int i = 0; i <= 600; ++i) {
            Drive d;
            d.clock = i / 60.0;
            s.advance(d);
            capped = capped && s.alive() <= 100;
        }
        CHECK(capped && s.alive() == 100 && s.dropped() > 0 && s.limit() == kMinLimit + 36);
        s.setLimit(64);
        CHECK(s.alive() == 64 && s.limit() == 64);
    }

    {
        System s(2048);
        s.setParams(steady());
        s.reset();
        Drive d;
        d.clock = 5.0;
        s.advance(d);
        d.clock = 6.0;
        s.advance(d);
        const long long spawnedAfterOneSecondJump = s.spawned();
        CHECK(spawnedAfterOneSecondJump <= 8 * 2 + 4);
        d.clock = 2.0;
        s.advance(d);
        CHECK(s.alive() == 0 && s.spawned() == 0);
        d.clock = 2.0 + 1.0 / 60.0;
        s.advance(d);
        d.clock = 100.0;
        s.advance(d);
        CHECK(s.alive() == 0 && s.spawned() == 0);
        d.clock = std::nan("");
        s.advance(d);
        d.clock = 100.0;
        d.beat = INFINITY;
        s.advance(d);
        CHECK(s.alive() == 0);
        d.beat = 0.0;
        d.level = std::nan("");
        d.clock = 100.5;
        s.advance(d);
        std::vector<Vertex> out;
        s.collect(out, d);
        bool finite = true;
        for (const Vertex& v : out) {
            finite = finite && std::isfinite(v.size) && std::isfinite(v.alpha);
        }
        CHECK(finite);
    }

    {
        Params burst = steady();
        burst.rate = 0.0f;
        burst.burst = 100.0f;
        System s(2048);
        s.setParams(burst);
        s.reset();
        Drive d;
        d.beat = 0.2;
        d.clock = 0.0;
        s.advance(d);
        CHECK(s.spawned() == 0);
        d.beat = 0.9;
        d.clock = 0.1;
        s.advance(d);
        CHECK(s.spawned() == 0);
        d.beat = 1.0;
        d.clock = 0.2;
        s.advance(d);
        CHECK(s.spawned() == 100);
        d.beat = 1.5;
        d.clock = 0.3;
        s.advance(d);
        CHECK(s.spawned() == 100);
        d.beat = 2.1;
        d.clock = 0.4;
        s.advance(d);
        CHECK(s.spawned() == 200);
        d.beat = 0.5;
        d.clock = 0.5;
        s.advance(d);
        CHECK(s.spawned() == 200);
        d.beat = 1.0;
        d.clock = 0.6;
        s.advance(d);
        CHECK(s.spawned() == 300);
        burst.burst = 100000.0f;
        s.setParams(burst);
        CHECK(s.params().burst == static_cast<float>(kMaxBurst));
    }

    {
        Params reactive = steady();
        reactive.levelRate = 2.0f;
        reactive.turbulence = 0.0f;
        System quiet(2048);
        System loud(2048);
        quiet.setParams(reactive);
        loud.setParams(reactive);
        quiet.reset();
        loud.reset();
        for (int i = 0; i <= 120; ++i) {
            Drive d;
            d.clock = i / 60.0;
            d.level = 0.0f;
            quiet.advance(d);
            d.level = 1.0f;
            loud.advance(d);
        }
        CHECK(loud.spawned() > quiet.spawned() * 2);
        Params sized = reactive;
        sized.levelSize = 1.0f;
        System sys(2048);
        sys.setParams(sized);
        sys.reset();
        Drive d;
        for (int i = 0; i <= 30; ++i) {
            d.clock = i / 60.0;
            sys.advance(d);
        }
        std::vector<Vertex> small;
        std::vector<Vertex> big;
        d.level = 0.0f;
        sys.collect(small, d);
        d.level = 1.0f;
        sys.collect(big, d);
        CHECK(!small.empty() && small.size() == big.size() && big[0].size > small[0].size * 1.9f);
    }

    for (Shape shape : {Shape::Point, Shape::Line, Shape::Ring, Shape::Box}) {
        Params p = steady();
        p.shape = shape;
        p.extent = 0.5f;
        System s(2048);
        s.setParams(p);
        s.setAspect(2.0f);
        s.reset();
        for (int i = 0; i <= 120; ++i) {
            Drive d;
            d.clock = i / 60.0;
            s.advance(d);
        }
        bool inside = s.alive() > 0;
        for (const Particle& pt : s.particles()) {
            inside = inside && std::fabs(pt.x) <= 2.0f * 1.35f && std::fabs(pt.y) <= 1.35f;
        }
        CHECK(inside);
        CHECK(shapeFromName(shapeName(shape)) == shape);
    }
    for (ColorMode m : {ColorMode::Gradient, ColorMode::Rainbow, ColorMode::White}) {
        CHECK(colorModeFromName(colorModeName(m)) == m);
    }

    {
        Params hostile;
        hostile.rate = std::nan("");
        hostile.life = INFINITY;
        hostile.speed = -INFINITY;
        hostile.size = std::nan("");
        hostile.gravity = 1e30f;
        System s(2048);
        s.setParams(hostile);
        s.reset();
        for (int i = 0; i <= 300; ++i) {
            Drive d;
            d.clock = i / 60.0;
            s.advance(d);
        }
        std::vector<Vertex> out;
        s.collect(out, Drive());
        bool finite = true;
        for (const Vertex& v : out) {
            finite = finite && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.size);
        }
        CHECK(finite && s.alive() <= 2048);
        CHECK(s.params().gravity == 3.0f && std::isfinite(s.params().rate) && std::isfinite(s.params().life));
    }

    CHECK(builtinPresets().size() == 6);
    for (const Preset& preset : builtinPresets()) {
        System s(kDefaultLimit);
        s.setParams(preset.params);
        s.reset();
        int peak = 0;
        for (int i = 0; i <= 600; ++i) {
            Drive d;
            d.clock = i / 60.0;
            d.beat = d.clock * 2.0;
            d.level = 0.5f;
            s.advance(d);
            peak = std::max(peak, s.alive());
        }
        CHECK(peak > 5 && peak <= kDefaultLimit);
        Params found;
        CHECK(findBuiltin(preset.name, found) && found.seed == preset.params.seed);
        std::string error;
        Params parsed;
        CHECK(paramsFromJson(paramsToJson(preset.params), parsed, error));
        CHECK(parsed.rate == preset.params.rate && parsed.shape == preset.params.shape && parsed.color == preset.params.color && parsed.seed == preset.params.seed && parsed.additive == preset.params.additive);
    }
    Params none;
    CHECK(!findBuiltin("Nope", none));

    std::string error;
    Params parsed;
    std::vector<std::string> warnings;
    nlohmann::json node = nlohmann::json::parse(paramsToJson(Params()));
    node["rate"] = 1e12;
    node["life"] = 0;
    node["shape"] = "cube";
    node["seed"] = 4294967295ull;
    node["levelRate"] = "loud";
    CHECK(paramsFromJson(node.dump(), parsed, error, &warnings));
    CHECK(parsed.rate == 2000.0f && parsed.life == 0.2f && parsed.shape == Shape::Point && parsed.seed == 4294967295u && parsed.levelRate == 0.0f);
    CHECK(warnings.empty());
    node["life"] = 10;
    CHECK(paramsFromJson(node.dump(), parsed, error, &warnings));
    CHECK(!warnings.empty());
    node["seed"] = -4;
    CHECK(paramsFromJson(node.dump(), parsed, error) && parsed.seed == 1);
    CHECK(!paramsFromJson("{}", parsed, error));
    CHECK(!paramsFromJson("nope", parsed, error));
    CHECK(!paramsFromJson("{\"format\":\"nyrexvj-particles\",\"version\":7}", parsed, error));
    CHECK(!paramsFromJson("{\"format\":\"nyrexvj-particles\"}", parsed, error));
    CHECK(!paramsFromJson(std::string(gen::kMaxDocBytes + 1, ' '), parsed, error));

    const fs::path folder = scratch("nvj_creative_particles");
    setDataDir(folder);
    Params mine = steady();
    mine.shape = Shape::Ring;
    CHECK(saveParticleDoc("Mein Effekt", mine, error));
    CHECK(listParticleDocs() == std::vector<std::string>{"Mein Effekt"});
    Params loaded;
    CHECK(loadParticleDoc("Mein Effekt", loaded, error) && loaded.shape == Shape::Ring && loaded.seed == 99);
    CHECK(!saveParticleDoc("../x", mine, error) && !loadParticleDoc("../x", loaded, error) && !loadParticleDoc("Missing", loaded, error));
    CHECK(deleteParticleDoc("Mein Effekt") && listParticleDocs().empty());
}

void testChains() {
    using namespace chain;
    const fs::path folder = scratch("nvj_creative_chains");
    setDataDir(folder);
    const std::vector<EffectDef>& library = effectLibrary();
    CHECK(library.size() >= 3);
    const int mirror = effectIndexByName("Mirror");
    const int kaleido = effectIndexByName("Kaleidoscope");
    CHECK(mirror >= 0 && kaleido >= 0);

    Slots slots{};
    setEffect(slots[0], mirror);
    slots[0].params[1] = 0.25f;
    slots[0].reactBand = BandKick;
    slots[0].reactMode = ReactGate;
    slots[0].reactAmount = 0.8f;
    setEffect(slots[2], kaleido);
    slots[2].enabled = false;
    slots[2].params[0] = 0.9f;
    CHECK(usedSlots(slots) == 2);

    std::string error;
    std::vector<std::string> warnings;
    CHECK(saveChain("Spiegel Kick", slots, "Spiegelt auf dem Kick", error));
    CHECK(fs::exists(chainPath("Spiegel Kick")));
    ChainDoc loaded;
    CHECK(loadChain("Spiegel Kick", loaded, error, &warnings));
    CHECK(loaded.description == "Spiegelt auf dem Kick" && loaded.missing.empty());
    CHECK(loaded.slots[0].effect == mirror && loaded.slots[0].params[1] == 0.25f && loaded.slots[0].reactBand == BandKick);
    CHECK(loaded.slots[0].reactMode == ReactGate && loaded.slots[0].reactAmount == 0.8f);
    CHECK(loaded.slots[1].effect < 0);
    CHECK(loaded.slots[2].effect == kaleido && !loaded.slots[2].enabled && loaded.slots[2].params[0] == 0.9f);

    Slots empty{};
    CHECK(!saveChain("Leer", empty, "", error) && !error.empty());
    CHECK(!saveChain("../x", slots, "", error) && !saveChain("a/b", slots, "", error) && !saveChain("CON", slots, "", error));
    CHECK(!loadChain("../Spiegel Kick", loaded, error) && !loadChain("Nope", loaded, error));

    CHECK(saveChain("Kaleido Tief", slots, "Tunnel look", error));
    std::vector<ChainInfo> all = listChains();
    CHECK(all.size() == 2 && all[0].readable);
    CHECK(searchChains(all, "").size() == 2);
    CHECK(searchChains(all, "   ").size() == 2);
    CHECK(searchChains(all, "spiegel").size() == 1 && searchChains(all, "SPIEGEL")[0].name == "Spiegel Kick");
    CHECK(searchChains(all, "tunnel").size() == 1 && searchChains(all, "tunnel")[0].name == "Kaleido Tief");
    CHECK(searchChains(all, "kaleidoscope").size() == 2);
    CHECK(searchChains(all, "mirror kaleidoscope").size() == 2);
    CHECK(searchChains(all, "mirror tunnel").size() == 1);
    CHECK(searchChains(all, "doesnotexist").empty());
    CHECK(all[0].effects.size() == 2 && all[0].effects[0] == "Mirror");

    nlohmann::json node = nlohmann::json::parse(chainToJson(slots, ""));
    node["slots"][1] = {{"effect", "Totally Unknown Effect"}, {"enabled", true}, {"params", {0.1, 0.2, 0.3, 0.4}}};
    node["slots"][3] = {{"effect", mirror >= 0 ? library[static_cast<size_t>(mirror)].name : "Mirror"}, {"params", {5, -5, "x", 0.5}}, {"react", {{"band", "kick"}, {"param", 99}, {"amount", 9}, {"mode", "nonsense"}}}};
    warnings.clear();
    ChainDoc mixed;
    CHECK(chainFromJson(node.dump(), mixed, error, &warnings));
    CHECK(mixed.missing.size() == 1 && mixed.missing[0] == "Totally Unknown Effect");
    CHECK(mixed.slots[1].effect < 0 && !warnings.empty());
    CHECK(mixed.slots[3].params[0] == 1.0f && mixed.slots[3].params[1] == 0.0f && mixed.slots[3].reactParam <= kParamCount - 1 && mixed.slots[3].reactAmount == 1.0f);

    node["slots"].push_back({{"effect", "Mirror"}});
    node["slots"].push_back({{"effect", "Mirror"}});
    warnings.clear();
    CHECK(chainFromJson(node.dump(), mixed, error, &warnings));
    CHECK(std::any_of(warnings.begin(), warnings.end(), [](const std::string& w) { return w.find("more than four") != std::string::npos; }));

    CHECK(!chainFromJson("", mixed, error));
    CHECK(!chainFromJson("{}", mixed, error));
    CHECK(!chainFromJson("{\"format\":\"nyrexvj-chain\",\"version\":1}", mixed, error));
    CHECK(!chainFromJson("{\"format\":\"nyrexvj-chain\",\"version\":3,\"slots\":[]}", mixed, error));
    CHECK(!chainFromJson("{\"format\":\"nyrexvj-chain\",\"slots\":[]}", mixed, error));
    CHECK(!chainFromJson("{\"format\":\"nyrexvj-preset\",\"version\":1,\"slots\":[]}", mixed, error));
    CHECK(!chainFromJson(std::string(gen::kMaxDocBytes + 1, ' '), mixed, error));
    CHECK(chainFromJson("{\"format\":\"nyrexvj-chain\",\"version\":1,\"slots\":[1,\"x\",null,[]]}", mixed, error) && usedSlots(mixed.slots) == 0);

    node = nlohmann::json::parse(chainToJson(slots, ""));
    node["description"] = std::string("Zeile\neins \xFF\xFE ") + std::string(400, 'u') + "\xC3\xBC";
    warnings.clear();
    const std::string withInvalid = "{\"format\":\"nyrexvj-chain\",\"version\":1,\"slots\":[],\"description\":\"a\xFF\xFE\"}";
    CHECK(!chainFromJson(withInvalid, mixed, error, &warnings));
    CHECK(chainFromJson(node.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), mixed, error, &warnings));
    CHECK(mixed.description.size() <= kMaxDescriptionBytes && mixed.description.find('\n') == std::string::npos);
    CHECK(!text::decodeUtf8(mixed.description).hadInvalid);
    std::string accents;
    for (int i = 0; i < 150; ++i) {
        accents += "\xC3\xA4";
    }
    CHECK(saveChain("Umlaute", slots, accents, error));
    ChainDoc accentDoc;
    CHECK(loadChain("Umlaute", accentDoc, error));
    CHECK(accentDoc.description.size() == 200 && !text::decodeUtf8(accentDoc.description).hadInvalid);

    const fs::path outside = scratch("nvj_creative_chains_out");
    CHECK(exportChain("Spiegel Kick", outside, error));
    CHECK(fs::exists(outside / "Spiegel Kick.nyxchain"));
    CHECK(exportChain("Spiegel Kick", outside / "Kopie.nyxchain", error));
    CHECK(!exportChain("Spiegel Kick", outside / "Kopie.txt", error));
    CHECK(!exportChain("Nope", outside, error));
    std::string imported;
    warnings.clear();
    CHECK(importChain(outside / "Kopie.nyxchain", imported, error, &warnings) && imported == "Kopie");
    CHECK(importChain(outside / "Kopie.nyxchain", imported, error) && imported == "Kopie 2");
    CHECK(importChain(outside / "Spiegel Kick.nyxchain", imported, error) && imported == "Spiegel Kick 2");
    ChainDoc copy;
    CHECK(loadChain("Kopie 2", copy, error) && copy.slots[0].effect == mirror && copy.description == "Spiegelt auf dem Kick");

    writeBytes(outside / "wrong.txt", readBytes(outside / "Kopie.nyxchain"));
    CHECK(!importChain(outside / "wrong.txt", imported, error));
    writeBytes(outside / "broken.nyxchain", "{{{");
    CHECK(!importChain(outside / "broken.nyxchain", imported, error));
    writeBytes(outside / "huge.nyxchain", std::string(gen::kMaxDocBytes + 5, ' '));
    CHECK(!importChain(outside / "huge.nyxchain", imported, error));
    CHECK(!importChain(outside / "absent.nyxchain", imported, error));
    writeBytes(outside / "foreign.nyxchain", "{\"format\":\"nyrexvj-chain\",\"version\":1,\"slots\":[{\"effect\":\"Nonexistent One\"},{\"effect\":\"Nonexistent Two\"}]}");
    CHECK(!importChain(outside / "foreign.nyxchain", imported, error));
    writeBytes(outside / "CON.nyxchain", readBytes(outside / "Kopie.nyxchain"));
    CHECK(importChain(outside / "CON.nyxchain", imported, error) && imported == "_CON" && fs::exists(chainPath("_CON")));
    for (const fs::directory_entry& entry : fs::directory_iterator(chainsDirectory())) {
        const std::string name = entry.path().filename().string();
        CHECK(name.find("..") == std::string::npos && entry.path().parent_path() == fs::path(chainsDirectory()));
    }

    writeBytes(chainPath("Defekt"), "not json at all");
    writeBytes(chainPath("Riesig"), std::string(gen::kMaxDocBytes + 100, 'x'));
    bool sawBroken = false;
    bool sawHuge = false;
    for (const ChainInfo& info : listChains()) {
        if (info.name == "Defekt") {
            sawBroken = !info.readable;
        }
        if (info.name == "Riesig") {
            sawHuge = !info.readable;
        }
    }
    CHECK(sawBroken && sawHuge);

    for (int i = 0; i < 80; ++i) {
        std::string ignored;
        saveChain("Bulk " + std::to_string(100 + i), slots, "", ignored);
    }
    CHECK(listChains().size() <= gen::kMaxFiles);
    std::string full;
    CHECK(!importChain(outside / "Kopie.nyxchain", full, error) && !error.empty());

    CHECK(deleteChain("Spiegel Kick") && !fs::exists(chainPath("Spiegel Kick")) && !deleteChain("Spiegel Kick") && !deleteChain("../x"));
}

}

int main() {
    testUtf8();
    testLayout();
    testAnimation();
    testFont();
    testNames();
    testTextDoc();
    testParticles();
    testChains();
    return testing::finish();
}

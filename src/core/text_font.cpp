#include "core/text_font.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace nvj::text {

namespace fs = std::filesystem;

namespace {

constexpr size_t kGuardBytes = 1u << 18;
constexpr std::uint32_t kMaxPoints = 16384;
constexpr int kMaxCompositeDepth = 6;
constexpr int kMaxCompositeParts = 128;

std::uint32_t be32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint32_t be16(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 8) | static_cast<std::uint32_t>(p[1]);
}

struct Table {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    bool present = false;
};

struct Tables {
    std::uint64_t glyf = 0;
    std::uint64_t glyfLength = 0;
    std::uint64_t loca = 0;
    std::uint32_t locaFormat = 0;
    std::uint32_t glyphs = 0;
    bool trueType = false;
};

}

struct FontFace::Impl {
    std::vector<unsigned char> bytes;
    size_t size = 0;
    stbtt_fontinfo info{};
    int collectionIndex = 0;
    Tables tables;
    bool ok = false;
};

namespace {

bool glyphRange(const FontFace::Impl& impl, std::uint32_t glyph, std::uint64_t& start, std::uint64_t& end) {
    const Tables& t = impl.tables;
    if (glyph >= t.glyphs) {
        return false;
    }
    const unsigned char* at = impl.bytes.data() + t.loca;
    if (t.locaFormat == 0) {
        start = static_cast<std::uint64_t>(be16(at + static_cast<size_t>(glyph) * 2)) * 2;
        end = static_cast<std::uint64_t>(be16(at + static_cast<size_t>(glyph) * 2 + 2)) * 2;
    } else {
        start = be32(at + static_cast<size_t>(glyph) * 4);
        end = be32(at + static_cast<size_t>(glyph) * 4 + 4);
    }
    return end >= start && end <= t.glyfLength;
}

bool validateGlyph(const FontFace::Impl& impl, std::uint32_t glyph, int depth, int& budget) {
    if (!impl.tables.trueType) {
        return glyph < impl.tables.glyphs;
    }
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    if (!glyphRange(impl, glyph, start, end)) {
        return false;
    }
    const std::uint64_t length = end - start;
    if (length == 0) {
        return true;
    }
    if (length < 10) {
        return false;
    }
    const unsigned char* d = impl.bytes.data() + impl.tables.glyf + start;
    const int contours = static_cast<std::int16_t>(be16(d));
    if (contours >= 0) {
        if (contours == 0) {
            return true;
        }
        const std::uint64_t n = static_cast<std::uint64_t>(contours);
        if (10 + 2 * n + 2 > length) {
            return false;
        }
        std::uint32_t previous = 0;
        for (std::uint64_t i = 0; i < n; ++i) {
            const std::uint32_t e = be16(d + 10 + 2 * i);
            if (i > 0 && e <= previous) {
                return false;
            }
            previous = e;
        }
        const std::uint32_t points = previous + 1;
        if (points > kMaxPoints) {
            return false;
        }
        const std::uint64_t instructions = be16(d + 10 + 2 * n);
        std::uint64_t cursor = 10 + 2 * n + 2 + instructions;
        if (cursor > length) {
            return false;
        }
        std::vector<unsigned char> flags(points);
        std::uint32_t repeat = 0;
        unsigned char current = 0;
        for (std::uint32_t i = 0; i < points; ++i) {
            if (repeat == 0) {
                if (cursor >= length) {
                    return false;
                }
                current = d[cursor++];
                if (current & 8) {
                    if (cursor >= length) {
                        return false;
                    }
                    repeat = d[cursor++];
                }
            } else {
                --repeat;
            }
            flags[i] = current;
        }
        for (std::uint32_t i = 0; i < points; ++i) {
            const unsigned char f = flags[i];
            cursor += (f & 2) ? 1 : ((f & 16) ? 0 : 2);
        }
        for (std::uint32_t i = 0; i < points; ++i) {
            const unsigned char f = flags[i];
            cursor += (f & 4) ? 1 : ((f & 32) ? 0 : 2);
        }
        return cursor <= length;
    }
    std::uint64_t cursor = 10;
    for (;;) {
        if (cursor + 4 > length) {
            return false;
        }
        const std::uint32_t flags = be16(d + cursor);
        const std::uint32_t child = be16(d + cursor + 2);
        cursor += 4;
        if (!(flags & 2)) {
            return false;
        }
        cursor += (flags & 1) ? 4 : 2;
        if (flags & 8) {
            cursor += 2;
        } else if (flags & 64) {
            cursor += 4;
        } else if (flags & 128) {
            cursor += 8;
        }
        if (cursor > length || --budget < 0 || depth >= kMaxCompositeDepth) {
            return false;
        }
        if (!validateGlyph(impl, child, depth + 1, budget)) {
            return false;
        }
        if (!(flags & 32)) {
            break;
        }
    }
    return true;
}

bool validateFont(FontFace::Impl& impl, int offset, std::string& error) {
    const std::vector<unsigned char>& bytes = impl.bytes;
    const std::uint64_t size = impl.size;
    const std::string bad = "The file is not a font that can be used";
    if (static_cast<std::uint64_t>(offset) + 12 > size) {
        error = bad;
        return false;
    }
    const unsigned char* head = bytes.data() + offset;
    const std::uint32_t version = be32(head);
    const bool trueType = version == 0x00010000u || version == 0x74727565u;
    const bool cff = version == 0x4F54544Fu;
    if (!trueType && !cff) {
        error = bad;
        return false;
    }
    const std::uint32_t tableCount = be16(head + 4);
    if (tableCount == 0 || tableCount > 64 || static_cast<std::uint64_t>(offset) + 12 + static_cast<std::uint64_t>(tableCount) * 16 > size) {
        error = bad;
        return false;
    }
    Table cmap;
    Table headTable;
    Table maxp;
    Table loca;
    Table glyf;
    Table hhea;
    Table hmtx;
    Table cffTable;
    for (std::uint32_t i = 0; i < tableCount; ++i) {
        const unsigned char* record = head + 12 + static_cast<size_t>(i) * 16;
        Table table;
        table.offset = be32(record + 8);
        table.length = be32(record + 12);
        table.present = true;
        if (table.offset > size || table.length > size - table.offset) {
            error = bad;
            return false;
        }
        switch (be32(record)) {
            case 0x636D6170u: cmap = table; break;
            case 0x68656164u: headTable = table; break;
            case 0x6D617870u: maxp = table; break;
            case 0x6C6F6361u: loca = table; break;
            case 0x676C7966u: glyf = table; break;
            case 0x68686561u: hhea = table; break;
            case 0x686D7478u: hmtx = table; break;
            case 0x43464620u: cffTable = table; break;
            default: break;
        }
    }
    if (!cmap.present || !headTable.present || !maxp.present || !hhea.present || !hmtx.present || headTable.length < 54 || maxp.length < 6 || hhea.length < 36 || cmap.length < 4) {
        error = bad;
        return false;
    }
    if (trueType && (!glyf.present || !loca.present)) {
        error = bad;
        return false;
    }
    if (cff && !cffTable.present) {
        error = bad;
        return false;
    }
    const std::uint32_t unitsPerEm = be16(bytes.data() + headTable.offset + 18);
    if (unitsPerEm < 16 || unitsPerEm > 16384) {
        error = bad;
        return false;
    }
    const int ascent = static_cast<std::int16_t>(be16(bytes.data() + hhea.offset + 4));
    const int descent = static_cast<std::int16_t>(be16(bytes.data() + hhea.offset + 6));
    if (ascent - descent < static_cast<int>(unitsPerEm / 8) || ascent - descent > static_cast<int>(unitsPerEm) * 8) {
        error = bad;
        return false;
    }
    const std::uint32_t glyphs = be16(bytes.data() + maxp.offset + 4);
    const std::uint32_t metricCount = be16(bytes.data() + hhea.offset + 34);
    if (glyphs == 0 || metricCount == 0 || metricCount > glyphs || hmtx.length < static_cast<std::uint64_t>(metricCount) * 4 + static_cast<std::uint64_t>(glyphs - metricCount) * 2) {
        error = bad;
        return false;
    }
    impl.tables = Tables{};
    impl.tables.glyphs = glyphs;
    impl.tables.trueType = trueType;
    if (trueType) {
        const std::uint32_t format = be16(bytes.data() + headTable.offset + 50);
        if (format > 1) {
            error = bad;
            return false;
        }
        const std::uint64_t entry = format == 0 ? 2 : 4;
        if (loca.length < (static_cast<std::uint64_t>(glyphs) + 1) * entry) {
            error = bad;
            return false;
        }
        impl.tables.glyf = glyf.offset;
        impl.tables.glyfLength = glyf.length;
        impl.tables.loca = loca.offset;
        impl.tables.locaFormat = format;
        std::uint64_t last = 0;
        for (std::uint32_t i = 0; i <= glyphs; ++i) {
            const unsigned char* at = bytes.data() + loca.offset + static_cast<std::uint64_t>(i) * entry;
            const std::uint64_t value = format == 0 ? static_cast<std::uint64_t>(be16(at)) * 2 : be32(at);
            if (value > glyf.length || value < last) {
                error = bad;
                return false;
            }
            last = value;
        }
    }
    const unsigned char* map = bytes.data() + cmap.offset;
    const std::uint32_t subtables = be16(map + 2);
    if (subtables == 0 || subtables > 64 || 4 + static_cast<std::uint64_t>(subtables) * 8 > cmap.length) {
        error = bad;
        return false;
    }
    for (std::uint32_t i = 0; i < subtables; ++i) {
        const std::uint64_t at = be32(map + 4 + static_cast<size_t>(i) * 8 + 4);
        if (at + 6 > cmap.length) {
            error = bad;
            return false;
        }
        const std::uint32_t format = be16(map + at);
        std::uint64_t span = be16(map + at + 2);
        if (format == 12 || format == 13) {
            if (at + 16 > cmap.length) {
                error = bad;
                return false;
            }
            span = be32(map + at + 4);
            const std::uint64_t groups = be32(map + at + 12);
            if (16 + groups * 12 > span) {
                error = bad;
                return false;
            }
        } else if (format == 4) {
            if (span < 16 || at + 16 > cmap.length) {
                error = bad;
                return false;
            }
            const std::uint64_t segments = be16(map + at + 6) / 2;
            if (segments == 0 || 16 + segments * 8 > span) {
                error = bad;
                return false;
            }
        } else if (format == 6) {
            if (span < 10 || 10 + static_cast<std::uint64_t>(be16(map + at + 8)) * 2 > span) {
                error = bad;
                return false;
            }
        }
        if (span < 6 || at + span > cmap.length) {
            error = bad;
            return false;
        }
    }
    return true;
}

}

FontFace::FontFace() = default;
FontFace::~FontFace() = default;
FontFace::FontFace(FontFace&&) noexcept = default;
FontFace& FontFace::operator=(FontFace&&) noexcept = default;

bool FontFace::load(std::vector<unsigned char> bytes, int collectionIndex, std::string& error) {
    impl_.reset();
    if (bytes.size() < 64 || bytes.size() > kMaxFontBytes) {
        error = "The font file is empty or too large";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->size = bytes.size();
    impl->bytes = std::move(bytes);
    impl->bytes.resize(impl->size + kGuardBytes, 0);
    impl->collectionIndex = std::max(collectionIndex, 0);
    const int offset = stbtt_GetFontOffsetForIndex(impl->bytes.data(), impl->collectionIndex);
    if (offset < 0 || static_cast<size_t>(offset) >= impl->size) {
        error = "The file is not a font that can be used";
        return false;
    }
    if (!validateFont(*impl, offset, error)) {
        return false;
    }
    if (!stbtt_InitFont(&impl->info, impl->bytes.data(), offset)) {
        error = "The file is not a font that can be used";
        return false;
    }
    impl->info.gpos = 0;
    impl->info.svg = 0;
    if (impl->info.kern != 0) {
        const unsigned char* kern = impl->bytes.data() + impl->info.kern;
        std::uint64_t kernLength = 0;
        const unsigned char* directory = impl->bytes.data() + offset;
        const std::uint32_t count = be16(directory + 4);
        for (std::uint32_t i = 0; i < count; ++i) {
            const unsigned char* record = directory + 12 + static_cast<size_t>(i) * 16;
            if (be32(record) == 0x6B65726Eu) {
                kernLength = be32(record + 12);
            }
        }
        if (kernLength < 18 || 18 + static_cast<std::uint64_t>(be16(kern + 10)) * 6 > kernLength) {
            impl->info.kern = 0;
        }
    }
    impl->ok = true;
    impl_ = std::move(impl);
    return true;
}

bool FontFace::loaded() const {
    return impl_ && impl_->ok;
}

bool FontFace::hasGlyph(char32_t codepoint) const {
    if (!loaded() || codepoint > 0x10FFFF) {
        return false;
    }
    const int index = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint));
    if (index <= 0 || static_cast<std::uint32_t>(index) >= impl_->tables.glyphs) {
        return false;
    }
    int budget = kMaxCompositeParts;
    return validateGlyph(*impl_, static_cast<std::uint32_t>(index), 0, budget);
}

bool FontFace::boxSane(char32_t codepoint, float pixelHeight) const {
    if (!loaded()) {
        return false;
    }
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (stbtt_GetCodepointBox(&impl_->info, static_cast<int>(codepoint), &x0, &y0, &x1, &y1) == 0) {
        return true;
    }
    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, pixelHeight);
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    const float width = static_cast<float>(x1 - x0) * scale;
    const float height = static_cast<float>(y1 - y0) * scale;
    return std::isfinite(scale) && width <= pixelHeight * 8.0f && height <= pixelHeight * 8.0f;
}

float FontFace::kerning(char32_t left, char32_t right, float pixelHeight) const {
    if (!loaded() || impl_->info.kern == 0) {
        return 0.0f;
    }
    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, pixelHeight);
    return static_cast<float>(stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(left), static_cast<int>(right))) * scale;
}

void FontFace::metrics(float pixelHeight, float& ascent, float& descent, float& lineGap) const {
    ascent = pixelHeight * 0.8f;
    descent = -pixelHeight * 0.2f;
    lineGap = 0.0f;
    if (!loaded()) {
        return;
    }
    int a = 0;
    int d = 0;
    int g = 0;
    stbtt_GetFontVMetrics(&impl_->info, &a, &d, &g);
    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, pixelHeight);
    ascent = static_cast<float>(a) * scale;
    descent = static_cast<float>(d) * scale;
    lineGap = static_cast<float>(g) * scale;
}

bool readFontFile(const fs::path& path, std::vector<unsigned char>& bytes, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        error = "The font file could not be found";
        return false;
    }
    const auto size = fs::file_size(path, ec);
    if (ec || size < 64 || size > kMaxFontBytes) {
        error = "The font file is empty or too large";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "The font file could not be read";
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<size_t>(in.gcount()) != bytes.size()) {
        error = "The font file could not be read";
        bytes.clear();
        return false;
    }
    return true;
}

bool loadFontFile(const fs::path& path, FontFace& face, std::string& error) {
    std::vector<unsigned char> bytes;
    if (!readFontFile(path, bytes, error)) {
        return false;
    }
    return face.load(std::move(bytes), 0, error);
}

const GlyphInfo* Atlas::find(char32_t codepoint) const {
    auto it = std::lower_bound(glyphs.begin(), glyphs.end(), codepoint, [](const GlyphInfo& glyph, char32_t value) { return glyph.codepoint < value; });
    if (it == glyphs.end() || it->codepoint != codepoint) {
        return nullptr;
    }
    return &*it;
}

float Atlas::kerningFor(char32_t left, char32_t right) const {
    auto it = std::lower_bound(kerning.begin(), kerning.end(), std::make_pair(left, right), [](const KerningPair& pair, const std::pair<char32_t, char32_t>& key) {
        return pair.left != key.first ? pair.left < key.first : pair.right < key.second;
    });
    if (it == kerning.end() || it->left != left || it->right != right) {
        return 0.0f;
    }
    return it->amount;
}

struct AtlasBaker {
    static const FontFace::Impl* impl(const FontFace& face) { return face.impl_.get(); }
};

namespace {

bool drawable(char32_t cp) {
    if (cp == U'\n' || cp == U'\r' || cp == U'\t') {
        return false;
    }
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
        return false;
    }
    return true;
}

struct FaceRange {
    size_t face = 0;
    std::vector<int> codepoints;
    std::vector<stbtt_packedchar> packed;
};

bool tryPack(const BakeRequest& request, float pixelHeight, int side, std::vector<FaceRange>& ranges, std::vector<unsigned char>& pixels) {
    pixels.assign(static_cast<size_t>(side) * static_cast<size_t>(side), 0);
    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, pixels.data(), side, side, side, 2, nullptr)) {
        return false;
    }
    bool ok = true;
    for (FaceRange& range : ranges) {
        if (range.codepoints.empty()) {
            continue;
        }
        const FontFace::Impl* font = AtlasBaker::impl(*request.faces[range.face]);
        range.packed.assign(range.codepoints.size(), stbtt_packedchar{});
        stbtt_pack_range packRange{};
        packRange.font_size = pixelHeight;
        packRange.first_unicode_codepoint_in_range = 0;
        packRange.array_of_unicode_codepoints = range.codepoints.data();
        packRange.num_chars = static_cast<int>(range.codepoints.size());
        packRange.chardata_for_range = range.packed.data();
        if (!stbtt_PackFontRanges(&context, font->bytes.data(), font->collectionIndex, &packRange, 1)) {
            ok = false;
            break;
        }
    }
    stbtt_PackEnd(&context);
    return ok;
}

}

bool bakeAtlas(const BakeRequest& request, Atlas& atlas, std::string& error) {
    atlas = Atlas{};
    if (request.faces.empty() || request.faces.front() == nullptr || !request.faces.front()->loaded()) {
        error = "No font is available";
        return false;
    }
    std::set<char32_t> wanted;
    for (char32_t cp : request.text) {
        if (drawable(cp)) {
            wanted.insert(cp);
        }
        if (wanted.size() > kMaxGlyphs) {
            error = "The text uses too many different characters";
            return false;
        }
    }
    wanted.insert(U' ');
    wanted.insert(U'?');
    float pixelHeight = std::clamp(request.pixelHeight, static_cast<float>(kMinPixelHeight), static_cast<float>(kMaxPixelHeight));
    const int maxSide = std::clamp(request.maxSide, 64, kMaxAtlasSide);

    std::vector<FaceRange> ranges(request.faces.size());
    for (size_t i = 0; i < ranges.size(); ++i) {
        ranges[i].face = i;
    }
    std::vector<char32_t> missing;
    for (char32_t cp : wanted) {
        bool placed = false;
        for (size_t i = 0; i < request.faces.size(); ++i) {
            const FontFace* face = request.faces[i];
            if (face != nullptr && face->hasGlyph(cp) && face->boxSane(cp, pixelHeight)) {
                ranges[i].codepoints.push_back(static_cast<int>(cp));
                placed = true;
                break;
            }
        }
        if (!placed) {
            missing.push_back(cp);
        }
    }
    if (std::all_of(ranges.begin(), ranges.end(), [](const FaceRange& r) { return r.codepoints.empty(); })) {
        error = "The font has none of the characters of the text";
        return false;
    }

    std::vector<unsigned char> pixels;
    int usedSide = 0;
    bool packed = false;
    const size_t glyphCount = wanted.size() - missing.size();
    while (!packed && pixelHeight >= static_cast<float>(kMinPixelHeight)) {
        const double area = static_cast<double>(glyphCount) * (static_cast<double>(pixelHeight) + 4.0) * (static_cast<double>(pixelHeight) * 0.75 + 4.0) * 1.15;
        int side = 128;
        while (static_cast<double>(side) * static_cast<double>(side) < area && side < maxSide) {
            side *= 2;
        }
        side = std::min(side, maxSide);
        for (;; side *= 2) {
            if (tryPack(request, pixelHeight, side, ranges, pixels)) {
                packed = true;
                usedSide = side;
                break;
            }
            if (side >= maxSide) {
                break;
            }
        }
        if (!packed) {
            pixelHeight = std::floor(pixelHeight * 0.85f);
        }
    }
    if (!packed) {
        error = "The characters do not fit into the picture memory of the text";
        return false;
    }

    atlas.width = usedSide;
    atlas.height = usedSide;
    atlas.pixels = std::move(pixels);
    atlas.pixelHeight = pixelHeight;
    request.faces.front()->metrics(pixelHeight, atlas.ascent, atlas.descent, atlas.lineGap);
    for (const FaceRange& range : ranges) {
        for (size_t i = 0; i < range.codepoints.size(); ++i) {
            const stbtt_packedchar& pc = range.packed[i];
            GlyphInfo glyph;
            glyph.codepoint = static_cast<char32_t>(range.codepoints[i]);
            glyph.u0 = static_cast<float>(pc.x0) / static_cast<float>(usedSide);
            glyph.v0 = static_cast<float>(pc.y0) / static_cast<float>(usedSide);
            glyph.u1 = static_cast<float>(pc.x1) / static_cast<float>(usedSide);
            glyph.v1 = static_cast<float>(pc.y1) / static_cast<float>(usedSide);
            glyph.x0 = pc.xoff;
            glyph.y0 = pc.yoff;
            glyph.x1 = pc.xoff2;
            glyph.y1 = pc.yoff2;
            glyph.advance = pc.xadvance;
            atlas.glyphs.push_back(glyph);
        }
    }
    std::sort(atlas.glyphs.begin(), atlas.glyphs.end(), [](const GlyphInfo& a, const GlyphInfo& b) { return a.codepoint < b.codepoint; });
    atlas.missing = std::move(missing);

    if (request.useKerning) {
        std::set<std::pair<char32_t, char32_t>> seen;
        for (size_t i = 0; i + 1 < request.text.size(); ++i) {
            const char32_t left = request.text[i];
            const char32_t right = request.text[i + 1];
            if (!drawable(left) || !drawable(right) || !seen.insert({left, right}).second) {
                continue;
            }
            for (const FontFace* face : request.faces) {
                if (face != nullptr && face->hasGlyph(left) && face->hasGlyph(right)) {
                    const float amount = face->kerning(left, right, pixelHeight);
                    if (std::fabs(amount) > 0.01f) {
                        atlas.kerning.push_back({left, right, amount});
                    }
                    break;
                }
            }
        }
        std::sort(atlas.kerning.begin(), atlas.kerning.end(), [](const KerningPair& a, const KerningPair& b) { return a.left != b.left ? a.left < b.left : a.right < b.right; });
    }
    return true;
}

std::string fontsDirectory() {
    return (fs::path(userDataDir()) / "fonts").string();
}

std::vector<fs::path> cjkFontCandidates() {
    std::vector<fs::path> paths;
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const fs::path root = fs::path(windir && *windir ? windir : "C:\\Windows") / "Fonts";
    for (const char* name : {"YuGothR.ttc", "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc", "YuGothL.ttc", "YuGothB.ttc"}) {
        paths.push_back(root / name);
    }
#elif defined(__APPLE__)
    paths.push_back("/System/Library/Fonts/Hiragino Sans GB.ttc");
    paths.push_back("/Library/Fonts/Arial Unicode.ttf");
#else
    for (const char* name : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf", "/usr/share/fonts/truetype/takao-gothic/TakaoGothic.ttf", "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf"}) {
        paths.push_back(name);
    }
#endif
    return paths;
}

fs::path firstExisting(const std::vector<fs::path>& candidates) {
    for (const fs::path& path : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec) && !ec) {
            return path;
        }
    }
    return fs::path();
}

namespace {

struct Alias {
    const char* name;
    std::vector<fs::path> paths;
};

std::vector<Alias> systemAliases() {
    std::vector<Alias> aliases;
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const fs::path root = fs::path(windir && *windir ? windir : "C:\\Windows") / "Fonts";
    aliases.push_back({"Sans", {root / "segoeui.ttf", root / "arial.ttf", root / "tahoma.ttf"}});
    aliases.push_back({"Sans Bold", {root / "segoeuib.ttf", root / "arialbd.ttf", root / "tahomabd.ttf"}});
    aliases.push_back({"Serif", {root / "georgia.ttf", root / "times.ttf"}});
    aliases.push_back({"Mono", {root / "consola.ttf", root / "cour.ttf"}});
    aliases.push_back({"Display", {root / "impact.ttf", root / "bahnschrift.ttf", root / "arialbd.ttf"}});
#elif defined(__APPLE__)
    aliases.push_back({"Sans", {"/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf"}});
    aliases.push_back({"Sans Bold", {"/Library/Fonts/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"}});
    aliases.push_back({"Serif", {"/Library/Fonts/Times New Roman.ttf", "/System/Library/Fonts/Times.ttc"}});
    aliases.push_back({"Mono", {"/System/Library/Fonts/Menlo.ttc", "/Library/Fonts/Courier New.ttf"}});
    aliases.push_back({"Display", {"/Library/Fonts/Impact.ttf", "/Library/Fonts/Arial Bold.ttf"}});
#else
    aliases.push_back({"Sans", {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"}});
    aliases.push_back({"Sans Bold", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"}});
    aliases.push_back({"Serif", {"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", "/usr/share/fonts/dejavu/DejaVuSerif.ttf"}});
    aliases.push_back({"Mono", {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf"}});
    aliases.push_back({"Display", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"}});
#endif
    return aliases;
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool fontExtension(const fs::path& path) {
    const std::string ext = lowerAscii(path.extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

}

std::vector<FontEntry> availableFonts() {
    std::vector<FontEntry> fonts;
    for (const Alias& alias : systemAliases()) {
        const fs::path found = firstExisting(alias.paths);
        if (!found.empty()) {
            fonts.push_back({alias.name, found, false});
        }
    }
    std::error_code ec;
    std::vector<FontEntry> user;
    const fs::path folder = fontsDirectory();
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end && user.size() < 64; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc || !fontExtension(it->path())) {
            continue;
        }
        const std::u8string name = it->path().filename().u8string();
        user.push_back({std::string(name.begin(), name.end()), it->path(), true});
    }
    std::sort(user.begin(), user.end(), [](const FontEntry& a, const FontEntry& b) { return lowerAscii(a.name) < lowerAscii(b.name); });
    fonts.insert(fonts.end(), user.begin(), user.end());
    return fonts;
}

bool resolveFont(const std::string& name, FontEntry& entry) {
    const std::vector<FontEntry> fonts = availableFonts();
    if (fonts.empty()) {
        return false;
    }
    if (name.empty()) {
        entry = fonts.front();
        return true;
    }
    for (const FontEntry& font : fonts) {
        if (font.name == name) {
            entry = font;
            return true;
        }
    }
    return false;
}

}

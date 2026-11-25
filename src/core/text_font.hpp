#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nvj::text {

constexpr size_t kMaxFontBytes = 64ull * 1024ull * 1024ull;
constexpr int kMinPixelHeight = 12;
constexpr int kMaxPixelHeight = 320;
constexpr int kMaxAtlasSide = 2048;
constexpr size_t kMaxGlyphs = 1024;

class FontFace {
public:
    FontFace();
    ~FontFace();
    FontFace(FontFace&&) noexcept;
    FontFace& operator=(FontFace&&) noexcept;
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    bool load(std::vector<unsigned char> bytes, int collectionIndex, std::string& error);
    bool loaded() const;
    bool hasGlyph(char32_t codepoint) const;
    bool boxSane(char32_t codepoint, float pixelHeight) const;
    float kerning(char32_t left, char32_t right, float pixelHeight) const;
    void metrics(float pixelHeight, float& ascent, float& descent, float& lineGap) const;

    struct Impl;

private:
    friend struct AtlasBaker;
    std::unique_ptr<Impl> impl_;
};

bool readFontFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string& error);
bool loadFontFile(const std::filesystem::path& path, FontFace& face, std::string& error);

struct GlyphInfo {
    char32_t codepoint = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float advance = 0.0f;
};

struct KerningPair {
    char32_t left = 0;
    char32_t right = 0;
    float amount = 0.0f;
};

struct Atlas {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    float pixelHeight = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    std::vector<GlyphInfo> glyphs;
    std::vector<KerningPair> kerning;
    std::vector<char32_t> missing;

    const GlyphInfo* find(char32_t codepoint) const;
    float kerningFor(char32_t left, char32_t right) const;
    float lineHeight() const { return ascent - descent + lineGap; }
    size_t bytes() const { return pixels.size(); }
};

struct BakeRequest {
    std::u32string text;
    float pixelHeight = 64.0f;
    int maxSide = kMaxAtlasSide;
    bool useKerning = true;
    std::vector<const FontFace*> faces;
};

bool bakeAtlas(const BakeRequest& request, Atlas& atlas, std::string& error);

struct FontEntry {
    std::string name;
    std::filesystem::path path;
    bool user = false;
};

std::string fontsDirectory();
std::vector<FontEntry> availableFonts();
bool resolveFont(const std::string& name, FontEntry& entry);
std::vector<std::filesystem::path> cjkFontCandidates();
std::filesystem::path firstExisting(const std::vector<std::filesystem::path>& candidates);

}

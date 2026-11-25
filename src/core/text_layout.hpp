#pragma once

#include "core/text_font.hpp"

#include <string>
#include <vector>

namespace nvj::text {

constexpr size_t kMaxTextBytes = 2000;
constexpr size_t kMaxTextCodepoints = 1000;
constexpr int kMaxLines = 40;

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct DecodeResult {
    std::u32string text;
    bool hadInvalid = false;
    bool truncated = false;
};

DecodeResult decodeUtf8(const std::string& utf8);
std::string encodeUtf8(const std::u32string& text);

struct LayoutParams {
    float wrapWidth = 0.0f;
    float lineSpacing = 1.0f;
    float tracking = 0.0f;
    HAlign hAlign = HAlign::Center;
    VAlign vAlign = VAlign::Middle;
    bool kerning = true;
};

struct PlacedGlyph {
    char32_t codepoint = 0;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    int line = 0;
    int index = 0;
    int indexInLine = 0;
    int lineLength = 0;
};

struct Layout {
    std::vector<PlacedGlyph> glyphs;
    float width = 0.0f;
    float height = 0.0f;
    int lines = 0;
    int visibleCount = 0;
    bool truncatedLines = false;
};

Layout layoutText(const Atlas& atlas, const std::u32string& text, const LayoutParams& params);

enum class Animation { None, Typewriter, Fade, SlideUp, Wave, Pulse, Audio, Bounce };

constexpr int kAnimationCount = 8;
const char* animationName(Animation animation);
Animation animationFromName(const std::string& name);

struct AnimationInput {
    Animation animation = Animation::None;
    double time = 0.0;
    double beat = 0.0;
    float level = 0.0f;
    float speed = 1.0f;
    float amount = 0.5f;
};

struct GlyphPose {
    float alpha = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scale = 1.0f;
};

GlyphPose poseGlyph(const PlacedGlyph& glyph, int glyphCount, const AnimationInput& input);

}

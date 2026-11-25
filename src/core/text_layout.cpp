#include "core/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace nvj::text {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isSpace(char32_t cp) {
    return cp == U' ' || cp == 0x00A0 || cp == 0x3000 || (cp >= 0x2000 && cp <= 0x200A);
}

bool breaksAfter(char32_t cp) {
    return isSpace(cp) || cp == U'-' || (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

float smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

struct Item {
    char32_t cp = 0;
    const GlyphInfo* glyph = nullptr;
    float advance = 0.0f;
    float natural = 0.0f;
    bool space = false;
    bool drawable = false;
};

float frac(double value) {
    return static_cast<float>(value - std::floor(value));
}

}

DecodeResult decodeUtf8(const std::string& utf8) {
    DecodeResult result;
    size_t length = utf8.size();
    if (length > kMaxTextBytes) {
        length = kMaxTextBytes;
        result.truncated = true;
    }
    size_t i = 0;
    while (i < length) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            result.hadInvalid = true;
            ++i;
            continue;
        }
        if (i + extra >= length) {
            result.hadInvalid = true;
            break;
        }
        bool good = true;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
            if ((cc & 0xC0) != 0x80) {
                good = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        static const char32_t minimum[4] = {0, 0x80, 0x800, 0x10000};
        if (!good || cp < minimum[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            result.hadInvalid = true;
            ++i;
            continue;
        }
        i += extra + 1;
        if (cp == U'\r') {
            continue;
        }
        if (cp == U'\t') {
            cp = U' ';
        }
        if (cp != U'\n' && (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))) {
            continue;
        }
        if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) {
            continue;
        }
        result.text.push_back(cp);
        if (result.text.size() >= kMaxTextCodepoints) {
            if (i < length) {
                result.truncated = true;
            }
            break;
        }
    }
    return result;
}

std::string encodeUtf8(const std::u32string& text) {
    std::string out;
    for (char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

Layout layoutText(const Atlas& atlas, const std::u32string& text, const LayoutParams& params) {
    Layout layout;
    if (atlas.width <= 0 || atlas.glyphs.empty() || text.empty()) {
        return layout;
    }
    const float spacing = std::clamp(params.lineSpacing, 0.5f, 3.0f);
    const float lineStep = std::max(atlas.lineHeight(), atlas.pixelHeight * 0.5f) * spacing;
    const float tracking = std::clamp(params.tracking, -0.5f, 2.0f) * atlas.pixelHeight;
    const float wrap = params.wrapWidth > 0.0f ? params.wrapWidth : 0.0f;
    const GlyphInfo* fallback = atlas.find(U'?');

    std::vector<std::vector<Item>> lines;
    std::vector<Item> current;
    float width = 0.0f;
    int lastBreak = -1;
    bool wrapped = false;
    bool stop = false;

    auto flush = [&](bool byWrap) {
        lines.push_back(std::move(current));
        current.clear();
        width = 0.0f;
        lastBreak = -1;
        wrapped = byWrap;
        if (static_cast<int>(lines.size()) >= kMaxLines) {
            layout.truncatedLines = true;
            stop = true;
        }
    };

    for (char32_t cp : text) {
        if (stop) {
            break;
        }
        if (cp == U'\n') {
            flush(false);
            continue;
        }
        const GlyphInfo* glyph = atlas.find(cp);
        if (glyph == nullptr) {
            glyph = fallback;
        }
        if (glyph == nullptr) {
            continue;
        }
        Item item;
        item.cp = cp;
        item.glyph = glyph;
        item.space = isSpace(cp);
        item.natural = glyph->advance;
        item.advance = glyph->advance + tracking;
        item.drawable = !item.space && glyph->u1 > glyph->u0 && glyph->v1 > glyph->v0;
        if (params.kerning && !current.empty()) {
            const float kern = atlas.kerningFor(current.back().cp, cp);
            current.back().advance += kern;
            width += kern;
        }
        if (wrap > 0.0f && !item.space && !current.empty() && width + item.natural > wrap) {
            if (lastBreak >= 0 && static_cast<size_t>(lastBreak) + 1 < current.size()) {
                std::vector<Item> carry(current.begin() + lastBreak + 1, current.end());
                current.resize(static_cast<size_t>(lastBreak) + 1);
                flush(true);
                if (stop) {
                    break;
                }
                current = std::move(carry);
                for (size_t k = 0; k < current.size(); ++k) {
                    width += current[k].advance;
                    if (breaksAfter(current[k].cp)) {
                        lastBreak = static_cast<int>(k);
                    }
                }
            } else {
                flush(true);
                if (stop) {
                    break;
                }
            }
        }
        if (item.space && current.empty() && wrapped) {
            continue;
        }
        current.push_back(item);
        width += item.advance;
        if (breaksAfter(cp)) {
            lastBreak = static_cast<int>(current.size()) - 1;
        }
    }
    if (!stop) {
        lines.push_back(std::move(current));
    } else if (!current.empty() && static_cast<int>(lines.size()) < kMaxLines) {
        lines.push_back(std::move(current));
    }

    layout.lines = static_cast<int>(lines.size());
    const float blockHeight = static_cast<float>(std::max(layout.lines - 1, 0)) * lineStep + (atlas.ascent - atlas.descent);
    float top = 0.0f;
    if (params.vAlign == VAlign::Middle) {
        top = -blockHeight * 0.5f;
    } else if (params.vAlign == VAlign::Bottom) {
        top = -blockHeight;
    }
    int ordinal = 0;
    float maxWidth = 0.0f;
    for (size_t row = 0; row < lines.size(); ++row) {
        const std::vector<Item>& line = lines[row];
        float pen = 0.0f;
        float visibleEnd = 0.0f;
        std::vector<float> starts(line.size(), 0.0f);
        for (size_t k = 0; k < line.size(); ++k) {
            starts[k] = pen;
            if (!line[k].space) {
                visibleEnd = pen + line[k].natural;
            }
            pen += line[k].advance;
        }
        maxWidth = std::max(maxWidth, visibleEnd);
        float offset = 0.0f;
        if (params.hAlign == HAlign::Center) {
            offset = -visibleEnd * 0.5f;
        } else if (params.hAlign == HAlign::Right) {
            offset = -visibleEnd;
        }
        const float baseline = top + atlas.ascent + static_cast<float>(row) * lineStep;
        int inLine = 0;
        int drawableInLine = 0;
        for (const Item& item : line) {
            if (item.drawable) {
                ++drawableInLine;
            }
        }
        for (size_t k = 0; k < line.size(); ++k) {
            const Item& item = line[k];
            if (!item.drawable) {
                continue;
            }
            PlacedGlyph placed;
            placed.codepoint = item.cp;
            placed.x0 = offset + starts[k] + item.glyph->x0;
            placed.x1 = offset + starts[k] + item.glyph->x1;
            placed.y0 = baseline + item.glyph->y0;
            placed.y1 = baseline + item.glyph->y1;
            placed.u0 = item.glyph->u0;
            placed.v0 = item.glyph->v0;
            placed.u1 = item.glyph->u1;
            placed.v1 = item.glyph->v1;
            placed.cx = offset + starts[k] + item.natural * 0.5f;
            placed.cy = baseline - (atlas.ascent + atlas.descent) * 0.5f;
            placed.line = static_cast<int>(row);
            placed.index = ordinal++;
            placed.indexInLine = inLine++;
            placed.lineLength = drawableInLine;
            layout.glyphs.push_back(placed);
        }
    }
    layout.width = maxWidth;
    layout.height = blockHeight;
    layout.visibleCount = ordinal;
    return layout;
}

const char* animationName(Animation animation) {
    switch (animation) {
        case Animation::Typewriter: return "typewriter";
        case Animation::Fade: return "fade";
        case Animation::SlideUp: return "slide";
        case Animation::Wave: return "wave";
        case Animation::Pulse: return "pulse";
        case Animation::Audio: return "audio";
        case Animation::Bounce: return "bounce";
        case Animation::None: break;
    }
    return "none";
}

Animation animationFromName(const std::string& name) {
    for (int i = 0; i < kAnimationCount; ++i) {
        const Animation animation = static_cast<Animation>(i);
        if (name == animationName(animation)) {
            return animation;
        }
    }
    return Animation::None;
}

GlyphPose poseGlyph(const PlacedGlyph& glyph, int glyphCount, const AnimationInput& input) {
    GlyphPose pose;
    const int count = std::max(glyphCount, 1);
    const float index = static_cast<float>(glyph.index);
    const float speed = std::isfinite(input.speed) ? std::clamp(input.speed, 0.1f, 4.0f) : 1.0f;
    const float amount = std::isfinite(input.amount) ? std::clamp(input.amount, 0.0f, 1.0f) : 0.0f;
    const double time = std::isfinite(input.time) ? std::max(input.time, 0.0) : 0.0;
    const double beatPosition = std::isfinite(input.beat) ? input.beat : 0.0;
    const float levelValue = std::isfinite(input.level) ? input.level : 0.0f;
    switch (input.animation) {
        case Animation::None:
            break;
        case Animation::Typewriter: {
            const double perSecond = 12.0 * speed;
            const double period = static_cast<double>(count) / perSecond + 1.5;
            const double t = std::fmod(time, period);
            pose.alpha = index < t * perSecond ? 1.0f : 0.0f;
            break;
        }
        case Animation::Fade:
        case Animation::SlideUp: {
            const double stagger = 0.08 / speed;
            const double duration = 0.5 / speed;
            const double period = static_cast<double>(count) * stagger + duration + 1.5;
            const double t = std::fmod(time, period);
            const float progress = smooth(static_cast<float>((t - static_cast<double>(index) * stagger) / duration));
            pose.alpha = progress;
            if (input.animation == Animation::SlideUp) {
                pose.offsetY = (1.0f - progress) * -0.5f;
            }
            break;
        }
        case Animation::Wave:
            pose.offsetY = std::sin(static_cast<float>(2.0 * kPi * (time * 0.5 * speed - static_cast<double>(index) * 0.12))) * 0.3f * amount;
            break;
        case Animation::Pulse: {
            const float phase = frac(beatPosition);
            const float envelope = std::pow(1.0f - phase, 3.0f);
            pose.scale = 1.0f + amount * 0.6f * envelope;
            break;
        }
        case Animation::Audio: {
            const float level = std::clamp(levelValue, 0.0f, 1.0f);
            const float shape = 0.7f + 0.3f * std::sin(index * 0.7f);
            pose.scale = 1.0f + amount * 0.8f * level * shape;
            break;
        }
        case Animation::Bounce: {
            const float phase = frac(beatPosition - static_cast<double>(index) * 0.07);
            pose.offsetY = amount * 0.5f * 4.0f * phase * (1.0f - phase);
            break;
        }
    }
    return pose;
}

}

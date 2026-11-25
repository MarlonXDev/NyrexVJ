#pragma once

#include "core/text_layout.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace nvj::text {

constexpr const char* kTextFormat = "nyrexvj-text";
constexpr int kTextVersion = 1;
constexpr const char* kTextExtension = ".nyxtext";
constexpr const char* kTextFolder = "texts";

enum class ColorMode { Layer, White, Rainbow };

struct TextDoc {
    std::string text = "NyrexVJ";
    std::string font = "Sans";
    float size = 0.2f;
    HAlign hAlign = HAlign::Center;
    VAlign vAlign = VAlign::Middle;
    float lineSpacing = 1.0f;
    float tracking = 0.0f;
    float wrap = 0.9f;
    float posX = 0.0f;
    float posY = 0.0f;
    float shadow = 0.0f;
    bool kerning = true;
    ColorMode color = ColorMode::Layer;
    Animation animation = Animation::None;
    float animSpeed = 1.0f;
    float animAmount = 0.5f;
};

const char* colorModeName(ColorMode mode);
ColorMode colorModeFromName(const std::string& name);
const char* hAlignName(HAlign align);
HAlign hAlignFromName(const std::string& name);
const char* vAlignName(VAlign align);
VAlign vAlignFromName(const std::string& name);

void clampDoc(TextDoc& doc);
std::string textDocToJson(const TextDoc& doc);
bool textDocFromJson(const std::string& json, TextDoc& doc, std::string& error, std::vector<std::string>* warnings = nullptr);

std::string textsDirectory();
std::vector<std::string> listTextDocs();
bool loadTextDoc(const std::string& name, TextDoc& doc, std::string& error);
bool saveTextDoc(const std::string& name, const TextDoc& doc, std::string& error);
bool deleteTextDoc(const std::string& name);
std::filesystem::path textDocPath(const std::string& name);
TextDoc exampleTextDoc();

}

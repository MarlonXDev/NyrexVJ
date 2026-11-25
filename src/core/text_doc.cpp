#include "core/text_doc.hpp"

#include "core/generator_files.hpp"
#include "core/recovery.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace nvj::text {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

float number(const json& node, const char* key, float fallback, float low, float high) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_number()) {
        return fallback;
    }
    const double value = it->get<double>();
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(static_cast<float>(value), low, high);
}

std::string stringValue(const json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

}

const char* colorModeName(ColorMode mode) {
    switch (mode) {
        case ColorMode::White: return "white";
        case ColorMode::Rainbow: return "rainbow";
        case ColorMode::Layer: break;
    }
    return "layer";
}

ColorMode colorModeFromName(const std::string& name) {
    if (name == "white") {
        return ColorMode::White;
    }
    if (name == "rainbow") {
        return ColorMode::Rainbow;
    }
    return ColorMode::Layer;
}

const char* hAlignName(HAlign align) {
    switch (align) {
        case HAlign::Left: return "left";
        case HAlign::Right: return "right";
        case HAlign::Center: break;
    }
    return "center";
}

HAlign hAlignFromName(const std::string& name) {
    if (name == "left") {
        return HAlign::Left;
    }
    if (name == "right") {
        return HAlign::Right;
    }
    return HAlign::Center;
}

const char* vAlignName(VAlign align) {
    switch (align) {
        case VAlign::Top: return "top";
        case VAlign::Bottom: return "bottom";
        case VAlign::Middle: break;
    }
    return "middle";
}

VAlign vAlignFromName(const std::string& name) {
    if (name == "top") {
        return VAlign::Top;
    }
    if (name == "bottom") {
        return VAlign::Bottom;
    }
    return VAlign::Middle;
}

void clampDoc(TextDoc& doc) {
    const DecodeResult decoded = decodeUtf8(doc.text);
    doc.text = encodeUtf8(decoded.text);
    if (doc.font.size() > 120) {
        doc.font.resize(120);
    }
    doc.size = std::clamp(doc.size, 0.02f, 0.9f);
    doc.lineSpacing = std::clamp(doc.lineSpacing, 0.5f, 3.0f);
    doc.tracking = std::clamp(doc.tracking, -0.5f, 2.0f);
    doc.wrap = std::clamp(doc.wrap, 0.0f, 1.0f);
    doc.posX = std::clamp(doc.posX, -1.0f, 1.0f);
    doc.posY = std::clamp(doc.posY, -1.0f, 1.0f);
    doc.shadow = std::clamp(doc.shadow, 0.0f, 1.0f);
    doc.animSpeed = std::clamp(doc.animSpeed, 0.1f, 4.0f);
    doc.animAmount = std::clamp(doc.animAmount, 0.0f, 1.0f);
}

std::string textDocToJson(const TextDoc& source) {
    TextDoc doc = source;
    clampDoc(doc);
    json node;
    node["format"] = kTextFormat;
    node["version"] = kTextVersion;
    node["text"] = doc.text;
    node["font"] = doc.font;
    node["size"] = doc.size;
    node["align"] = hAlignName(doc.hAlign);
    node["vertical"] = vAlignName(doc.vAlign);
    node["lineSpacing"] = doc.lineSpacing;
    node["tracking"] = doc.tracking;
    node["wrap"] = doc.wrap;
    node["x"] = doc.posX;
    node["y"] = doc.posY;
    node["shadow"] = doc.shadow;
    node["kerning"] = doc.kerning;
    node["color"] = colorModeName(doc.color);
    node["animation"] = animationName(doc.animation);
    node["animationSpeed"] = doc.animSpeed;
    node["animationAmount"] = doc.animAmount;
    return node.dump(2);
}

bool textDocFromJson(const std::string& text, TextDoc& doc, std::string& error, std::vector<std::string>* warnings) {
    if (text.size() > gen::kMaxDocBytes) {
        error = "The file is too large";
        return false;
    }
    json node = json::parse(text, nullptr, false, false);
    if (node.is_discarded() || !node.is_object()) {
        error = "The text file is not valid";
        return false;
    }
    auto format = node.find("format");
    if (format == node.end() || !format->is_string() || format->get<std::string>() != kTextFormat) {
        error = "This is not a NyrexVJ text file";
        return false;
    }
    auto version = node.find("version");
    if (version == node.end() || !version->is_number_integer() || version->get<int>() < 1) {
        error = "The text file has no version";
        return false;
    }
    if (version->get<int>() > kTextVersion) {
        error = "The text file was made by a newer version";
        return false;
    }
    TextDoc result;
    auto body = node.find("text");
    if (body == node.end() || !body->is_string()) {
        error = "The text file has no text";
        return false;
    }
    const DecodeResult decoded = decodeUtf8(body->get<std::string>());
    if (warnings) {
        if (decoded.hadInvalid) {
            warnings->push_back("The text had characters that are not valid and they were left out");
        }
        if (decoded.truncated) {
            warnings->push_back("The text was too long and was shortened");
        }
    }
    result.text = encodeUtf8(decoded.text);
    const std::string font = stringValue(node, "font");
    if (!font.empty()) {
        result.font = font;
    }
    result.size = number(node, "size", result.size, 0.02f, 0.9f);
    result.hAlign = hAlignFromName(stringValue(node, "align"));
    result.vAlign = vAlignFromName(stringValue(node, "vertical"));
    result.lineSpacing = number(node, "lineSpacing", result.lineSpacing, 0.5f, 3.0f);
    result.tracking = number(node, "tracking", result.tracking, -0.5f, 2.0f);
    result.wrap = number(node, "wrap", result.wrap, 0.0f, 1.0f);
    result.posX = number(node, "x", result.posX, -1.0f, 1.0f);
    result.posY = number(node, "y", result.posY, -1.0f, 1.0f);
    result.shadow = number(node, "shadow", result.shadow, 0.0f, 1.0f);
    auto kerning = node.find("kerning");
    if (kerning != node.end() && kerning->is_boolean()) {
        result.kerning = kerning->get<bool>();
    }
    result.color = colorModeFromName(stringValue(node, "color"));
    result.animation = animationFromName(stringValue(node, "animation"));
    result.animSpeed = number(node, "animationSpeed", result.animSpeed, 0.1f, 4.0f);
    result.animAmount = number(node, "animationAmount", result.animAmount, 0.0f, 1.0f);
    clampDoc(result);
    doc = result;
    return true;
}

std::string textsDirectory() {
    return gen::folderPath(kTextFolder).string();
}

fs::path textDocPath(const std::string& name) {
    return gen::folderPath(kTextFolder) / (name + kTextExtension);
}

std::vector<std::string> listTextDocs() {
    return gen::listStems(gen::folderPath(kTextFolder), kTextExtension);
}

bool loadTextDoc(const std::string& name, TextDoc& doc, std::string& error) {
    if (!gen::nameIsUsable(name)) {
        error = "The name is not valid";
        return false;
    }
    std::string text;
    if (!gen::readLimited(textDocPath(name), text, error)) {
        return false;
    }
    return textDocFromJson(text, doc, error);
}

bool saveTextDoc(const std::string& name, const TextDoc& doc, std::string& error) {
    if (!gen::nameIsUsable(name)) {
        error = "The name is not valid";
        return false;
    }
    std::error_code ec;
    fs::create_directories(gen::folderPath(kTextFolder), ec);
    if (!writeFileAtomic(textDocPath(name), textDocToJson(doc))) {
        error = "The file could not be saved";
        return false;
    }
    return true;
}

bool deleteTextDoc(const std::string& name) {
    if (!gen::nameIsUsable(name)) {
        return false;
    }
    std::error_code ec;
    return fs::remove(textDocPath(name), ec) && !ec;
}

TextDoc exampleTextDoc() {
    TextDoc doc;
    doc.text = "NyrexVJ";
    doc.animation = Animation::Pulse;
    doc.animAmount = 0.4f;
    return doc;
}

}

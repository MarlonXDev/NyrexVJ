#include "render/shader_source.hpp"

#include <algorithm>
#include <cctype>

namespace nvj {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool identifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string withoutComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
        } else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                out.push_back(text[i] == '\n' ? '\n' : ' ');
                ++i;
            }
            i = std::min(i + 2, text.size());
            out.push_back(' ');
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}

size_t skipSpace(const std::string& text, size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos;
}

bool hasFunction(const std::string& text, const std::string& name, bool needVoid) {
    size_t pos = 0;
    while ((pos = text.find(name, pos)) != std::string::npos) {
        const size_t end = pos + name.size();
        const bool leftOk = pos == 0 || !identifierChar(text[pos - 1]);
        const bool rightOk = end >= text.size() || !identifierChar(text[end]);
        if (leftOk && rightOk) {
            const size_t open = skipSpace(text, end);
            if (open < text.size() && text[open] == '(') {
                if (!needVoid) {
                    return true;
                }
                size_t back = pos;
                while (back > 0 && std::isspace(static_cast<unsigned char>(text[back - 1]))) {
                    --back;
                }
                if (back >= 4 && text.compare(back - 4, 4, "void") == 0 && (back == 4 || !identifierChar(text[back - 5]))) {
                    return true;
                }
            }
        }
        pos = end;
    }
    return false;
}

}

bool isShaderFileName(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string extension = lower(name.substr(dot));
    return extension == ".glsl" || extension == ".frag" || extension == ".fs";
}

std::string shaderClipName(const std::string& stem) {
    return "Shader: " + stem;
}

ShaderSource prepareShaderSource(const std::string& input) {
    ShaderSource result;
    if (input.size() > kMaxShaderFileBytes) {
        result.error = "The shader file is larger than 256 KiB";
        return result;
    }
    if (input.find('\0') != std::string::npos) {
        result.error = "The shader file is not a text file";
        return result;
    }
    std::string text;
    text.reserve(input.size());
    size_t start = 0;
    if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xEF && static_cast<unsigned char>(input[1]) == 0xBB && static_cast<unsigned char>(input[2]) == 0xBF) {
        start = 3;
    }
    for (size_t i = start; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '\r') {
            text.push_back('\n');
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                ++i;
            }
        } else if (static_cast<unsigned char>(c) >= 0x80) {
            text.push_back(' ');
        } else {
            text.push_back(c);
        }
    }

    std::string cleaned;
    cleaned.reserve(text.size());
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        const bool last = lineEnd == std::string::npos;
        if (last) {
            lineEnd = text.size();
        }
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        const size_t first = line.find_first_not_of(" \t");
        const bool version = first != std::string::npos && line.compare(first, 8, "#version") == 0;
        if (!version) {
            cleaned += line;
        }
        if (last) {
            break;
        }
        cleaned.push_back('\n');
        lineStart = lineEnd + 1;
    }

    const std::string code = withoutComments(cleaned);
    const bool hasMain = hasFunction(code, "main", true);
    const bool hasImage = hasFunction(code, "mainImage", false);
    if (!hasMain && !hasImage) {
        result.error = "The shader needs a void main() or a mainImage function";
        return result;
    }
    result.shadertoy = !hasMain && hasImage;
    result.body = cleaned;
    result.ok = true;
    return result;
}

}

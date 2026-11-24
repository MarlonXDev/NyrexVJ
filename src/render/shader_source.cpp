#include "render/shader_source.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>

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

struct Directive {
    std::vector<std::string> tokens;
    std::string name;
};

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::vector<std::string> tokenize(const std::string& text, size_t from) {
    std::vector<std::string> tokens;
    size_t i = from;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::string token;
        if (text[i] == '"') {
            ++i;
            while (i < text.size() && text[i] != '"') {
                token.push_back(text[i]);
                ++i;
            }
            if (i < text.size()) {
                ++i;
            }
            tokens.push_back(token.empty() ? std::string("\"\"") : "\"" + token);
            continue;
        }
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
            token.push_back(text[i]);
            ++i;
        }
        tokens.push_back(token);
    }
    return tokens;
}

std::optional<Directive> parseDirective(const std::string& line) {
    size_t i = skipSpace(line, 0);
    if (i >= line.size() || line[i] != '#') {
        return std::nullopt;
    }
    i = skipSpace(line, i + 1);
    if (line.compare(i, 6, "pragma") != 0 || (i + 6 < line.size() && identifierChar(line[i + 6]))) {
        return std::nullopt;
    }
    i = skipSpace(line, i + 6);
    if (lower(line.substr(i, 7)) != "nyrexvj" || (i + 7 < line.size() && identifierChar(line[i + 7]))) {
        return std::nullopt;
    }
    Directive directive;
    directive.tokens = tokenize(line, i + 7);
    directive.name = directive.tokens.empty() ? std::string() : lower(directive.tokens[0]);
    return directive;
}

bool isQuoted(const std::string& token) {
    return !token.empty() && token[0] == '"';
}

std::string unquoted(const std::string& token) {
    if (!isQuoted(token)) {
        return token;
    }
    const std::string inner = token.substr(1);
    return inner == "\"" ? std::string() : inner;
}

std::string lineError(int line, const std::string& message) {
    return "Line " + std::to_string(line) + ": " + message;
}

bool parseBufferLetter(const std::string& token, int& buffer) {
    if (token.size() != 1) {
        return false;
    }
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
    if (c < 'a' || c >= static_cast<char>('a' + kMaxShaderBuffers)) {
        return false;
    }
    buffer = c - 'a';
    return true;
}

bool parseNumber(const std::string& token, float& value) {
    if (token.empty()) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0') {
        return false;
    }
    value = static_cast<float>(parsed);
    return std::isfinite(parsed) != 0;
}

struct Section {
    enum class Kind { Preamble, Common, Buffer, Image };
    Kind kind = Kind::Preamble;
    int buffer = -1;
    float scale = 1.0f;
    int firstLine = 1;
    std::string code;
    std::array<ShaderInput, kShaderChannels> inputs;
    bool seen = false;
};

bool parseChannel(const Directive& directive, int line, ShaderInput& input, int& channel, std::string& error) {
    const std::vector<std::string>& t = directive.tokens;
    if (t.size() < 3) {
        error = lineError(line, "channel needs an index and a source, for example: channel 1 buffer a");
        return false;
    }
    float index = 0.0f;
    if (!parseNumber(t[1], index) || index < 0.0f || index >= static_cast<float>(kShaderChannels) || index != static_cast<float>(static_cast<int>(index))) {
        error = lineError(line, "the channel index must be 0, 1, 2 or 3");
        return false;
    }
    channel = static_cast<int>(index);
    input = ShaderInput{};
    size_t next = 3;
    const std::string source = lower(t[2]);
    if (source == "none") {
        input.kind = ShaderInputKind::None;
    } else if (source == "audio") {
        input.kind = ShaderInputKind::Audio;
    } else if (source == "buffer") {
        int buffer = -1;
        if (t.size() < 4 || !parseBufferLetter(t[3], buffer)) {
            error = lineError(line, "channel buffer needs a letter from a to d");
            return false;
        }
        input.kind = ShaderInputKind::Buffer;
        input.buffer = buffer;
        next = 4;
    } else if (source == "media") {
        if (t.size() < 4 || unquoted(t[3]).empty()) {
            error = lineError(line, "channel media needs the name of an image or video clip, for example: channel 2 media \"Video: loop\"");
            return false;
        }
        input.kind = ShaderInputKind::Media;
        input.media = unquoted(t[3]);
        next = 4;
    } else {
        error = lineError(line, "unknown channel source '" + t[2] + "', use audio, buffer, media or none");
        return false;
    }
    for (; next < t.size(); ++next) {
        const std::string option = lower(t[next]);
        if (option == "nearest") {
            input.nearest = true;
        } else if (option == "linear") {
            input.nearest = false;
        } else if (option == "repeat") {
            input.repeat = true;
        } else if (option == "clamp") {
            input.repeat = false;
        } else {
            error = lineError(line, "unknown channel option '" + t[next] + "', use nearest, linear, repeat or clamp");
            return false;
        }
    }
    return true;
}

bool styleOf(const std::string& code, bool& shadertoy) {
    const std::string stripped = withoutComments(code);
    const bool hasMain = hasFunction(stripped, "main", true);
    const bool hasImage = hasFunction(stripped, "mainImage", false);
    if (!hasMain && !hasImage) {
        return false;
    }
    shadertoy = !hasMain && hasImage;
    return true;
}

bool buildPlan(const std::string& cleaned, const std::string& code, ShaderSource& result) {
    const std::vector<std::string> sourceLines = splitLines(cleaned);
    const std::vector<std::string> codeLines = splitLines(code);
    std::vector<std::optional<Directive>> directives(sourceLines.size());
    bool anyDirective = false;
    bool sectioned = false;
    for (size_t i = 0; i < sourceLines.size() && i < codeLines.size(); ++i) {
        directives[i] = parseDirective(codeLines[i]);
        if (directives[i]) {
            anyDirective = true;
            const std::string& name = directives[i]->name;
            sectioned = sectioned || name == "common" || name == "buffer" || name == "image";
        }
    }
    if (!anyDirective) {
        return false;
    }

    auto plan = std::make_shared<ShaderPlan>();
    std::vector<Section> sections;
    sections.emplace_back();
    sections.back().kind = sectioned ? Section::Kind::Preamble : Section::Kind::Image;
    if (!sectioned) {
        sections.back().inputs[0].kind = ShaderInputKind::Audio;
    }
    std::array<bool, kMaxShaderBuffers> bufferSeen{};
    bool commonSeen = false;
    bool imageSeen = false;

    for (size_t i = 0; i < sourceLines.size(); ++i) {
        const int line = static_cast<int>(i) + 1;
        const bool lastLine = i + 1 >= sourceLines.size();
        if (!directives[i]) {
            sections.back().code += sourceLines[i];
            if (!lastLine) {
                sections.back().code.push_back('\n');
            }
            continue;
        }
        const Directive& d = *directives[i];
        if (!lastLine) {
            sections.back().code.push_back('\n');
        }
        if (d.name == "alpha") {
            plan->alpha = true;
            if (d.tokens.size() > 2) {
                result.error = lineError(line, "alpha takes at most one word: straight or premultiplied");
                return true;
            }
            if (d.tokens.size() == 2) {
                const std::string mode = lower(d.tokens[1]);
                if (mode == "premultiplied") {
                    plan->premultiplied = true;
                } else if (mode != "straight") {
                    result.error = lineError(line, "unknown alpha mode '" + d.tokens[1] + "', use straight or premultiplied");
                    return true;
                }
            }
        } else if (d.name == "common") {
            if (commonSeen) {
                result.error = lineError(line, "the common section is defined twice");
                return true;
            }
            commonSeen = true;
            Section section;
            section.kind = Section::Kind::Common;
            section.firstLine = line + 1;
            sections.push_back(section);
        } else if (d.name == "image") {
            if (imageSeen) {
                result.error = lineError(line, "the image section is defined twice");
                return true;
            }
            imageSeen = true;
            Section section;
            section.kind = Section::Kind::Image;
            section.firstLine = line + 1;
            section.inputs[0].kind = ShaderInputKind::Audio;
            sections.push_back(section);
        } else if (d.name == "buffer") {
            int buffer = -1;
            if (d.tokens.size() < 2 || !parseBufferLetter(d.tokens[1], buffer)) {
                result.error = lineError(line, "buffer needs a letter from a to d");
                return true;
            }
            if (bufferSeen[static_cast<size_t>(buffer)]) {
                result.error = lineError(line, std::string("buffer ") + shaderBufferLetter(buffer) + " is defined twice");
                return true;
            }
            bufferSeen[static_cast<size_t>(buffer)] = true;
            Section section;
            section.kind = Section::Kind::Buffer;
            section.buffer = buffer;
            section.firstLine = line + 1;
            section.inputs[0].kind = ShaderInputKind::Audio;
            if (d.tokens.size() == 4 && lower(d.tokens[2]) == "scale") {
                float scale = 1.0f;
                if (!parseNumber(d.tokens[3], scale) || scale < kMinShaderBufferScale || scale > 1.0f) {
                    result.error = lineError(line, "the buffer scale must be a number from 0.25 to 1");
                    return true;
                }
                section.scale = scale;
            } else if (d.tokens.size() != 2) {
                result.error = lineError(line, "use: buffer a or buffer a scale 0.5");
                return true;
            }
            sections.push_back(section);
        } else if (d.name == "channel") {
            Section& current = sections.back();
            if (current.kind == Section::Kind::Preamble || current.kind == Section::Kind::Common) {
                result.error = lineError(line, "channel belongs inside a buffer or image section");
                return true;
            }
            ShaderInput input;
            int channel = 0;
            std::string error;
            if (!parseChannel(d, line, input, channel, error)) {
                result.error = error;
                return true;
            }
            current.inputs[static_cast<size_t>(channel)] = input;
        } else if (d.name.empty()) {
            result.error = lineError(line, "#pragma nyrexvj needs a directive: alpha, common, buffer, image or channel");
            return true;
        } else {
            result.error = lineError(line, "unknown directive '" + d.tokens[0] + "', use alpha, common, buffer, image or channel");
            return true;
        }
    }

    if (sectioned) {
        const bool preambleBlank = sections.front().code.find_first_not_of(" \t\n") == std::string::npos;
        if (!preambleBlank && commonSeen) {
            result.error = "Code before the first section marker counts as common code and cannot be combined with a common section";
            return true;
        }
        if (!imageSeen) {
            result.error = "The file has sections but no image section (#pragma nyrexvj image)";
            return true;
        }
        plan->commonLine = 1;
        plan->common = preambleBlank ? std::string() : sections.front().code;
        for (const Section& section : sections) {
            if (section.kind == Section::Kind::Common) {
                plan->commonLine = section.firstLine;
                plan->common = section.code;
            }
        }
    }

    std::vector<const Section*> ordered;
    for (int b = 0; b < kMaxShaderBuffers; ++b) {
        for (const Section& section : sections) {
            if (section.kind == Section::Kind::Buffer && section.buffer == b) {
                ordered.push_back(&section);
            }
        }
    }
    for (const Section& section : sections) {
        if (section.kind == Section::Kind::Image) {
            ordered.push_back(&section);
        }
    }

    bool shadertoy = false;
    bool styleSet = false;
    for (const Section* section : ordered) {
        ShaderPassSource pass;
        pass.image = section->kind == Section::Kind::Image;
        pass.buffer = section->buffer;
        pass.firstLine = section->firstLine;
        pass.scale = section->scale;
        pass.code = section->code;
        pass.inputs = section->inputs;
        bool passToy = false;
        if (!styleOf(pass.code, passToy)) {
            result.error = pass.label() + " needs a void main() or a mainImage function";
            return true;
        }
        if (styleSet && passToy != shadertoy) {
            result.error = "All sections must use the same style: either void main() or mainImage";
            return true;
        }
        styleSet = true;
        shadertoy = passToy;
        for (const ShaderInput& input : pass.inputs) {
            if (input.kind == ShaderInputKind::Buffer && !bufferSeen[static_cast<size_t>(input.buffer)]) {
                result.error = pass.label() + " reads buffer " + shaderBufferLetter(input.buffer) + ", but the file has no such buffer";
                return true;
            }
        }
        plan->passes.push_back(std::move(pass));
    }
    if (static_cast<int>(plan->mediaNames().size()) > kMaxShaderMediaInputs) {
        result.error = "A shader can use at most " + std::to_string(kMaxShaderMediaInputs) + " different images or videos";
        return true;
    }
    result.shadertoy = shadertoy;
    result.advanced = true;
    result.plan = plan;
    result.ok = true;
    return true;
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

char shaderBufferLetter(int buffer) {
    return static_cast<char>('A' + std::clamp(buffer, 0, kMaxShaderBuffers - 1));
}

float fitShaderScale(float requested, int width, int height, int bytesPerPixel, int copies, unsigned long long available) {
    const float start = std::clamp(requested, kMinShaderBufferScale, 1.0f);
    const std::array<float, 5> steps = {1.0f, 0.75f, 0.5f, 0.35f, kMinShaderBufferScale};
    std::vector<float> candidates;
    candidates.push_back(start);
    for (const float step : steps) {
        if (step < start) {
            candidates.push_back(step);
        }
    }
    for (const float scale : candidates) {
        const unsigned long long w = static_cast<unsigned long long>(std::max(1.0f, std::round(static_cast<float>(width) * scale)));
        const unsigned long long h = static_cast<unsigned long long>(std::max(1.0f, std::round(static_cast<float>(height) * scale)));
        if (w * h * static_cast<unsigned long long>(bytesPerPixel) * static_cast<unsigned long long>(copies) <= available) {
            return scale;
        }
    }
    return 0.0f;
}

std::string ShaderPassSource::label() const {
    return image ? std::string("Image") : std::string("Buffer ") + shaderBufferLetter(buffer);
}

int ShaderPlan::bufferCount() const {
    int count = 0;
    for (const ShaderPassSource& pass : passes) {
        count += pass.image ? 0 : 1;
    }
    return count;
}

std::vector<std::string> ShaderPlan::mediaNames() const {
    std::vector<std::string> names;
    for (const ShaderPassSource& pass : passes) {
        for (const ShaderInput& input : pass.inputs) {
            if (input.kind == ShaderInputKind::Media && std::find(names.begin(), names.end(), input.media) == names.end()) {
                names.push_back(input.media);
            }
        }
    }
    return names;
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
    ShaderSource advanced;
    if (buildPlan(cleaned, code, advanced)) {
        advanced.body = cleaned;
        return advanced;
    }
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

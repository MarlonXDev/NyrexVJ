#include "render/isf.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

namespace nvj {

namespace {

using json = nlohmann::json;

struct Fail {
    std::string message;
};

[[noreturn]] void fail(const std::string& message) {
    throw Fail{message};
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool identChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool validName(const std::string& name) {
    if (name.empty() || name.size() > 32 || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), identChar) && name.find("__") == std::string::npos && name.compare(0, 3, "gl_") != 0;
}

bool isfBuiltinName(const std::string& name) {
    static const char* const kNames[] = {"TIME", "TIMEDELTA", "RENDERSIZE", "PASSINDEX", "FRAMEINDEX", "DATE", "IMG_SIZE", "IMG_PIXEL", "IMG_NORM_PIXEL", "IMG_THIS_PIXEL", "IMG_THIS_NORM_PIXEL", "texture2D"};
    for (const char* builtin : kNames) {
        if (name == builtin) {
            return true;
        }
    }
    return name.compare(0, 4, "isf_") == 0 || name.compare(0, 3, "vv_") == 0 || isReservedShaderName(name);
}

std::string cleanText(const std::string& text, size_t maxBytes) {
    std::string out;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7f || c == '"' || c == '\\') {
            if (c == '\n' || c == '\t' || c == ' ') {
                out.push_back(' ');
            }
            continue;
        }
        out.push_back(c);
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return std::string();
    }
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > maxBytes) {
        out.resize(maxBytes);
    }
    return out;
}

std::string number(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

std::string glslFloat(double value) {
    std::string text = number(value);
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    return text;
}

int lineOf(const std::string& text, size_t position) {
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(std::min(position, text.size())), '\n'));
}

struct Rewrite {
    std::string text;
    std::set<std::string> idents;
    bool foundMain = false;
};

Rewrite rewriteBody(const std::string& body) {
    Rewrite result;
    result.text.reserve(body.size() + 64);
    size_t i = 0;
    size_t lastEnd = std::string::npos;
    std::string lastIdent;
    while (i < body.size()) {
        const char c = body[i];
        if (c == '/' && i + 1 < body.size() && body[i + 1] == '/') {
            const size_t end = body.find('\n', i);
            const size_t stop = end == std::string::npos ? body.size() : end;
            result.text.append(body, i, stop - i);
            i = stop;
        } else if (c == '/' && i + 1 < body.size() && body[i + 1] == '*') {
            size_t end = body.find("*/", i + 2);
            end = end == std::string::npos ? body.size() : end + 2;
            result.text.append(body, i, end - i);
            i = end;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t j = i;
            while (j < body.size() && (std::isalnum(static_cast<unsigned char>(body[j])) || body[j] == '.' || body[j] == '_' || ((body[j] == '+' || body[j] == '-') && j > i && (body[j - 1] == 'e' || body[j - 1] == 'E') && !(body[i] == '0' && j > i + 1 && (body[i + 1] == 'x' || body[i + 1] == 'X'))))) {
                ++j;
            }
            result.text.append(body, i, j - i);
            i = j;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < body.size() && identChar(body[j])) {
                ++j;
            }
            const std::string ident = body.substr(i, j - i);
            result.idents.insert(ident);
            bool adjacent = lastEnd != std::string::npos;
            for (size_t k = lastEnd; adjacent && k < i; ++k) {
                adjacent = std::isspace(static_cast<unsigned char>(body[k])) != 0;
            }
            size_t next = j;
            while (next < body.size() && std::isspace(static_cast<unsigned char>(body[next]))) {
                ++next;
            }
            if (ident == "gl_FragColor") {
                result.text += "fragColor";
            } else if (ident == "main" && adjacent && lastIdent == "void" && next < body.size() && body[next] == '(') {
                result.text += "isf_main";
                result.foundMain = true;
            } else {
                result.text += ident;
            }
            lastEnd = j;
            lastIdent = ident;
            i = j;
        } else {
            result.text.push_back(c);
            ++i;
        }
    }
    return result;
}

struct Evaluator {
    std::string text;
    size_t pos = 0;
    double width = 0.0;
    double height = 0.0;
    const std::map<std::string, double>* variables = nullptr;
    bool failed = false;

    void skip() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }
    bool accept(char c) {
        skip();
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    double expression() {
        double value = term();
        while (!failed) {
            if (accept('+')) {
                value += term();
            } else if (accept('-')) {
                value -= term();
            } else {
                break;
            }
        }
        return value;
    }
    double term() {
        double value = factor();
        while (!failed) {
            if (accept('*')) {
                value *= factor();
            } else if (accept('/')) {
                const double divisor = factor();
                if (divisor == 0.0) {
                    failed = true;
                    return 0.0;
                }
                value /= divisor;
            } else {
                break;
            }
        }
        return value;
    }
    double factor() {
        skip();
        if (pos >= text.size()) {
            failed = true;
            return 0.0;
        }
        if (accept('-')) {
            return -factor();
        }
        if (accept('(')) {
            const double value = expression();
            if (!accept(')')) {
                failed = true;
            }
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.') {
            char* end = nullptr;
            const double value = std::strtod(text.c_str() + pos, &end);
            const size_t used = static_cast<size_t>(end - (text.c_str() + pos));
            if (used == 0) {
                failed = true;
                return 0.0;
            }
            pos += used;
            return value;
        }
        const bool dollar = text[pos] == '$';
        if (dollar) {
            ++pos;
        }
        size_t j = pos;
        while (j < text.size() && identChar(text[j])) {
            ++j;
        }
        if (j == pos) {
            failed = true;
            return 0.0;
        }
        const std::string name = text.substr(pos, j - pos);
        pos = j;
        if (!dollar && accept('(')) {
            std::vector<double> args;
            if (!accept(')')) {
                args.push_back(expression());
                while (accept(',') && !failed) {
                    args.push_back(expression());
                }
                if (!accept(')')) {
                    failed = true;
                }
            }
            if (failed) {
                return 0.0;
            }
            if (name == "floor" && args.size() == 1) {
                return std::floor(args[0]);
            }
            if (name == "ceil" && args.size() == 1) {
                return std::ceil(args[0]);
            }
            if (name == "round" && args.size() == 1) {
                return std::round(args[0]);
            }
            if (name == "abs" && args.size() == 1) {
                return std::fabs(args[0]);
            }
            if (name == "min" && args.size() == 2) {
                return std::min(args[0], args[1]);
            }
            if (name == "max" && args.size() == 2) {
                return std::max(args[0], args[1]);
            }
            failed = true;
            return 0.0;
        }
        if (name == "WIDTH") {
            return width;
        }
        if (name == "HEIGHT") {
            return height;
        }
        if (variables) {
            const auto it = variables->find(name);
            if (it != variables->end()) {
                return it->second;
            }
        }
        failed = true;
        return 0.0;
    }
};

bool evaluate(const std::string& expression, double width, double height, const std::map<std::string, double>& variables, double& value) {
    Evaluator evaluator;
    evaluator.text = expression;
    evaluator.width = width;
    evaluator.height = height;
    evaluator.variables = &variables;
    value = evaluator.expression();
    evaluator.skip();
    return !evaluator.failed && evaluator.pos == evaluator.text.size() && std::isfinite(value);
}

bool ratioOf(const json& item, const char* key, bool horizontal, const std::map<std::string, double>& variables, double& ratio) {
    ratio = 1.0;
    if (!item.contains(key)) {
        return true;
    }
    std::string expression;
    if (item[key].is_string()) {
        expression = item[key].get<std::string>();
    } else if (item[key].is_number()) {
        expression = number(item[key].get<double>());
    } else {
        return false;
    }
    if (expression.size() > 128) {
        return false;
    }
    double small = 0.0;
    double large = 0.0;
    if (!evaluate(expression, 1000.0, 500.0, variables, small) || !evaluate(expression, 2000.0, 1000.0, variables, large)) {
        return false;
    }
    const double base1 = horizontal ? 1000.0 : 500.0;
    const double base2 = horizontal ? 2000.0 : 1000.0;
    const double r1 = small / base1;
    const double r2 = large / base2;
    if (!(r1 > 0.0) || !(r2 > 0.0) || std::fabs(r1 - r2) > 0.02 * std::max(r1, r2)) {
        return false;
    }
    ratio = r2;
    return true;
}

struct Candidate {
    ShaderParam param;
    std::string original;
    std::string defaultText;
};

struct ImageUse {
    std::string name;
    enum class Kind { Picture, Media, Buffer, From, To, None } kind = Kind::None;
    std::string media;
    int buffer = -1;
};

int depthOf(const std::string& text, bool& ok) {
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
            deepest = std::max(deepest, depth);
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    ok = deepest <= kIsfMaxDepth;
    return deepest;
}

double readNumber(const json& value, const std::string& where) {
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        fail(where + " must be a number");
    }
    const double result = value.get<double>();
    if (std::fabs(result) > 1.0e6) {
        fail(where + " is outside -1000000 to 1000000");
    }
    return result;
}

double readBoolOrNumber(const json& value, const std::string& where) {
    if (value.is_boolean()) {
        return value.get<bool>() ? 1.0 : 0.0;
    }
    return readNumber(value, where) >= 0.5 ? 1.0 : 0.0;
}

void readArray(const json& value, size_t count, size_t minimum, const std::string& where, std::array<float, 4>& out) {
    if (!value.is_array() || value.size() < minimum || value.size() > count) {
        fail(where + " must be a list of " + std::to_string(minimum == count ? count : minimum) + (minimum == count ? "" : " or " + std::to_string(count)) + " numbers");
    }
    for (size_t i = 0; i < value.size(); ++i) {
        out[i] = static_cast<float>(readNumber(value[i], where));
    }
}

std::string readString(const json& item, const char* key, size_t maxBytes) {
    if (!item.contains(key)) {
        return std::string();
    }
    if (!item[key].is_string()) {
        fail(std::string(key) + " must be text");
    }
    return cleanText(item[key].get<std::string>(), maxBytes);
}

}

bool looksLikeIsf(const std::string& text) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    if (i + 1 >= text.size() || text[i] != '/' || text[i + 1] != '*') {
        return false;
    }
    i += 2;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    return i < text.size() && text[i] == '{';
}

IsfResult translateIsf(const std::string& text) {
    IsfResult result;
    if (!looksLikeIsf(text)) {
        return result;
    }
    result.isIsf = true;
    try {
        size_t open = text.find("/*");
        const size_t close = text.find("*/", open + 2);
        if (close == std::string::npos) {
            fail("The ISF header comment is not closed with */");
        }
        const std::string headerText = text.substr(open + 2, close - open - 2);
        if (headerText.size() > kIsfMaxHeaderBytes) {
            fail("The ISF header is larger than 64 KiB");
        }
        bool depthOk = true;
        depthOf(headerText, depthOk);
        if (!depthOk) {
            fail("The ISF header is nested deeper than " + std::to_string(kIsfMaxDepth) + " levels");
        }
        json header;
        try {
            header = json::parse(headerText);
        } catch (const json::parse_error& error) {
            const size_t byte = std::min<size_t>(error.byte > 0 ? error.byte - 1 : 0, headerText.size());
            fail("The ISF header is not valid JSON, near line " + std::to_string(lineOf(text, open + 2 + byte)));
        }
        if (!header.is_object()) {
            fail("The ISF header must be a JSON object");
        }

        ShaderInfo& info = result.info;
        std::string version;
        if (header.contains("ISFVSN") && header["ISFVSN"].is_string()) {
            version = cleanText(header["ISFVSN"].get<std::string>(), 16);
        } else if (header.contains("VSN") && header["VSN"].is_string()) {
            version = cleanText(header["VSN"].get<std::string>(), 16);
        }
        info.format = version.empty() ? "ISF" : "ISF " + version;
        if (!version.empty() && std::isdigit(static_cast<unsigned char>(version[0])) && version[0] > '2') {
            info.notes.push_back("This file says ISF version " + version + ", NyrexVJ knows version 2. Unknown parts are ignored.");
        }
        info.description = readString(header, "DESCRIPTION", kIsfMaxTextBytes);
        info.credit = readString(header, "CREDIT", kIsfMaxTextBytes);
        if (header.contains("CATEGORIES")) {
            if (!header["CATEGORIES"].is_array()) {
                fail("CATEGORIES must be a list of texts");
            }
            for (const json& item : header["CATEGORIES"]) {
                if (item.is_string() && static_cast<int>(info.categories.size()) < kIsfMaxCategories) {
                    const std::string category = cleanText(item.get<std::string>(), 40);
                    if (!category.empty()) {
                        info.categories.push_back(category);
                    }
                }
            }
        }

        const size_t bodyStartRaw = close + 2;
        const size_t lineEnd = text.find('\n', bodyStartRaw);
        if (lineEnd == std::string::npos) {
            fail("The ISF file has a header but no shader code");
        }
        const size_t headEnd = lineEnd + 1;
        Rewrite body = rewriteBody(text.substr(headEnd));
        if (body.text.find_first_not_of(" \t\n") == std::string::npos) {
            fail("The ISF file has a header but no shader code");
        }
        static const char* const kLegacy[] = {"varying", "attribute", "isf_vertShaderInit", "vv_vertShaderInit", "gl_TexCoord", "gl_MultiTexCoord0", "gl_Vertex", "gl_ModelViewProjectionMatrix", "gl_ModelViewMatrix", "gl_ProjectionMatrix", "ftransform", "sampler2DRect", "texture2DRect", "texture2DRectProj"};
        for (const char* legacy : kLegacy) {
            if (body.idents.count(legacy)) {
                fail(std::string("This ISF file uses '") + legacy + "', which belongs to a vertex shader or to old OpenGL. NyrexVJ reads only the fragment shader of an ISF file and uses OpenGL 3.3");
            }
        }
        if (!body.foundMain) {
            fail("The ISF shader has no void main() function");
        }
        if (body.idents.count("DATE")) {
            info.notes.push_back("The shader reads DATE. NyrexVJ gives it a fixed value (year 2026, month 1, day 1, 0 seconds), the real date and the time of day are not available.");
        }

        std::set<std::string> taken;
        std::vector<Candidate> candidates;
        std::vector<std::string> images;
        std::vector<std::string> imageTypes;
        std::map<std::string, double> variables;
        bool pictureInput = false;
        const json empty = json::array();
        const json& inputs = header.contains("INPUTS") ? header["INPUTS"] : empty;
        if (!inputs.is_array()) {
            fail("INPUTS must be a list");
        }
        if (static_cast<int>(inputs.size()) > kIsfMaxInputs) {
            fail("The ISF file has more than " + std::to_string(kIsfMaxInputs) + " inputs");
        }
        const auto claim = [&](const std::string& name, const std::string& where) {
            if (!validName(name)) {
                fail(where + " has the name '" + cleanText(name, 40) + "', which is not usable (letters, digits and underscore, up to 32 characters)");
            }
            if (isfBuiltinName(name)) {
                fail(where + " uses the name '" + name + "', which is reserved by ISF, GLSL or NyrexVJ");
            }
            if (!taken.insert(name).second) {
                fail("The name '" + name + "' is used twice in the ISF file");
            }
        };
        for (size_t n = 0; n < inputs.size(); ++n) {
            const json& item = inputs[n];
            const std::string where = "INPUTS[" + std::to_string(n) + "]";
            if (!item.is_object() || !item.contains("NAME") || !item["NAME"].is_string() || !item.contains("TYPE") || !item["TYPE"].is_string()) {
                fail(where + " needs a NAME and a TYPE as text");
            }
            const std::string name = item["NAME"].get<std::string>();
            claim(name, where);
            const std::string type = lower(item["TYPE"].get<std::string>());
            const std::string label = readString(item, "LABEL", 40);
            Candidate candidate;
            ShaderParam& param = candidate.param;
            param.name = name;
            param.label = label.empty() ? name : label;
            if (type == "image" || type == "audio" || type == "audiofft" || type == "cube") {
                images.push_back(name);
                imageTypes.push_back(type);
                pictureInput = pictureInput || (type == "image" && name == "inputImage");
                continue;
            }
            const std::string field = where + "." + name;
            if (type == "float") {
                param.kind = ShaderParamKind::Float;
                param.min[0] = item.contains("MIN") ? static_cast<float>(readNumber(item["MIN"], field + ".MIN")) : 0.0f;
                param.max[0] = item.contains("MAX") ? static_cast<float>(readNumber(item["MAX"], field + ".MAX")) : 1.0f;
                if (!(param.min[0] < param.max[0])) {
                    fail(field + ": MIN must be smaller than MAX");
                }
                float def = item.contains("DEFAULT") ? static_cast<float>(readNumber(item["DEFAULT"], field + ".DEFAULT")) : 0.0f;
                if (def < param.min[0] || def > param.max[0]) {
                    info.notes.push_back("The default of '" + name + "' is outside its range and was moved into it.");
                    def = std::clamp(def, param.min[0], param.max[0]);
                }
                param.def[0] = def;
                variables[name] = def;
            } else if (type == "long") {
                if (item.contains("VALUES")) {
                    const json& values = item["VALUES"];
                    if (!values.is_array() || values.empty() || values.size() > static_cast<size_t>(kMaxShaderListItems)) {
                        fail(field + ".VALUES must be a list of 1 to " + std::to_string(kMaxShaderListItems) + " numbers");
                    }
                    param.kind = ShaderParamKind::List;
                    for (size_t v = 0; v < values.size(); ++v) {
                        param.values.push_back(static_cast<float>(std::round(readNumber(values[v], field + ".VALUES"))));
                        std::string itemLabel = number(param.values.back());
                        if (item.contains("LABELS") && item["LABELS"].is_array() && v < item["LABELS"].size() && item["LABELS"][v].is_string()) {
                            const std::string given = cleanText(item["LABELS"][v].get<std::string>(), 40);
                            if (!given.empty()) {
                                itemLabel = given;
                            }
                        }
                        param.labels.push_back(itemLabel);
                    }
                    param.min[0] = 0.0f;
                    param.max[0] = static_cast<float>(values.size() - 1);
                    param.def[0] = 0.0f;
                    if (item.contains("DEFAULT")) {
                        const float wanted = static_cast<float>(std::round(readNumber(item["DEFAULT"], field + ".DEFAULT")));
                        const auto found = std::find(param.values.begin(), param.values.end(), wanted);
                        if (found == param.values.end()) {
                            info.notes.push_back("The default of '" + name + "' is not in its VALUES, the first entry is used.");
                        } else {
                            param.def[0] = static_cast<float>(found - param.values.begin());
                        }
                    }
                    variables[name] = param.values[static_cast<size_t>(param.def[0])];
                } else {
                    param.kind = ShaderParamKind::Int;
                    param.min[0] = item.contains("MIN") ? std::round(static_cast<float>(readNumber(item["MIN"], field + ".MIN"))) : 0.0f;
                    param.max[0] = item.contains("MAX") ? std::round(static_cast<float>(readNumber(item["MAX"], field + ".MAX"))) : 1.0f;
                    if (!(param.min[0] < param.max[0])) {
                        fail(field + ": MIN must be smaller than MAX");
                    }
                    param.def[0] = std::clamp(item.contains("DEFAULT") ? std::round(static_cast<float>(readNumber(item["DEFAULT"], field + ".DEFAULT"))) : 0.0f, param.min[0], param.max[0]);
                    variables[name] = param.def[0];
                }
            } else if (type == "bool" || type == "event") {
                param.kind = ShaderParamKind::Bool;
                param.min[0] = 0.0f;
                param.max[0] = 1.0f;
                param.def[0] = item.contains("DEFAULT") ? static_cast<float>(readBoolOrNumber(item["DEFAULT"], field + ".DEFAULT")) : 0.0f;
                variables[name] = param.def[0];
                if (type == "event") {
                    info.notes.push_back("The event input '" + name + "' is a switch here. It is never pulsed by the program.");
                }
            } else if (type == "point2d") {
                param.kind = ShaderParamKind::Point;
                param.count = 2;
                param.min = {0.0f, 0.0f, 0.0f, 0.0f};
                param.max = {1920.0f, 1080.0f, 0.0f, 0.0f};
                if (item.contains("MIN")) {
                    readArray(item["MIN"], 2, 2, field + ".MIN", param.min);
                }
                if (item.contains("MAX")) {
                    readArray(item["MAX"], 2, 2, field + ".MAX", param.max);
                } else {
                    info.notes.push_back("The point input '" + name + "' has no MAX, so the range 0 to 1920 by 0 to 1080 is assumed.");
                }
                for (size_t c = 0; c < 2; ++c) {
                    if (!(param.min[c] < param.max[c])) {
                        fail(field + ": MIN must be smaller than MAX");
                    }
                }
                param.def = param.min;
                if (item.contains("DEFAULT")) {
                    readArray(item["DEFAULT"], 2, 2, field + ".DEFAULT", param.def);
                }
                for (size_t c = 0; c < 2; ++c) {
                    param.def[c] = std::clamp(param.def[c], param.min[c], param.max[c]);
                }
            } else if (type == "color") {
                param.kind = ShaderParamKind::Color;
                param.count = 4;
                param.min = {0.0f, 0.0f, 0.0f, 0.0f};
                param.max = {1.0f, 1.0f, 1.0f, 1.0f};
                param.def = {1.0f, 1.0f, 1.0f, 1.0f};
                if (item.contains("DEFAULT")) {
                    std::array<float, 4> given{0.0f, 0.0f, 0.0f, 1.0f};
                    readArray(item["DEFAULT"], 4, 3, field + ".DEFAULT", given);
                    param.def = given;
                }
                for (float& c : param.def) {
                    c = std::clamp(c, 0.0f, 1.0f);
                }
            } else {
                fail(where + " has the type '" + cleanText(item["TYPE"].get<std::string>(), 24) + "', which NyrexVJ does not know. Known types: float, long, bool, event, point2D, color, image");
            }
            candidates.push_back(std::move(candidate));
        }

        bool startImage = false;
        bool endImage = false;
        for (const std::string& name : images) {
            startImage = startImage || name == "startImage";
            endImage = endImage || name == "endImage";
        }
        const bool transition = startImage || endImage;
        if (transition && !(startImage && endImage)) {
            fail("This ISF file has only one of startImage and endImage. A transition needs both");
        }
        if (transition && pictureInput) {
            fail("An ISF transition cannot also have an inputImage");
        }
        if (transition && header.contains("IMPORTED")) {
            fail("An ISF transition cannot import pictures. NyrexVJ transitions read the outgoing and the incoming picture only");
        }
        if (transition) {
            for (auto it = candidates.begin(); it != candidates.end();) {
                if (it->param.name == "progress" && it->param.kind == ShaderParamKind::Float) {
                    it = candidates.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::vector<ImageUse> uses;
        const auto used = [&](const std::string& name) { return body.idents.count(name) > 0; };
        if (transition) {
            ImageUse from;
            from.name = "startImage";
            from.kind = ImageUse::Kind::From;
            uses.push_back(from);
            ImageUse to;
            to.name = "endImage";
            to.kind = ImageUse::Kind::To;
            uses.push_back(to);
        }
        if (pictureInput && used("inputImage")) {
            ImageUse use;
            use.name = "inputImage";
            use.kind = ImageUse::Kind::Picture;
            uses.push_back(use);
        }
        if (header.contains("IMPORTED")) {
            const json& imported = header["IMPORTED"];
            std::vector<std::pair<std::string, json>> entries;
            if (imported.is_object()) {
                for (auto it = imported.begin(); it != imported.end(); ++it) {
                    entries.emplace_back(it.key(), it.value());
                }
            } else if (imported.is_array()) {
                for (const json& item : imported) {
                    if (!item.is_object() || !item.contains("NAME") || !item["NAME"].is_string()) {
                        fail("Every IMPORTED entry needs a NAME");
                    }
                    entries.emplace_back(item["NAME"].get<std::string>(), item);
                }
            } else {
                fail("IMPORTED must be an object or a list");
            }
            if (static_cast<int>(entries.size()) > kIsfMaxImported) {
                fail("The ISF file imports more than " + std::to_string(kIsfMaxImported) + " pictures");
            }
            for (const auto& entry : entries) {
                claim(entry.first, "IMPORTED");
                ImageUse use;
                use.name = entry.first;
                const json& value = entry.second;
                std::string path;
                if (value.is_object() && value.contains("PATH") && value["PATH"].is_string()) {
                    path = value["PATH"].get<std::string>();
                }
                const bool cube = value.is_object() && value.contains("TYPE") && value["TYPE"].is_string() && lower(value["TYPE"].get<std::string>()) == "cube";
                const size_t slash = path.find_last_of("/\\");
                std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
                const size_t dot = file.find_last_of('.');
                if (dot != std::string::npos && dot > 0) {
                    file.resize(dot);
                }
                file = cleanText(file, 60);
                if (cube || file.empty()) {
                    info.notes.push_back("The imported picture '" + entry.first + "' is not usable (" + (cube ? "cube maps are not supported" : "no file name") + "), it reads transparent black.");
                } else {
                    use.kind = ImageUse::Kind::Media;
                    use.media = file;
                    info.notes.push_back("The imported picture '" + entry.first + "' is read from the Library clip 'Image: " + file + "'. Copy the picture into the media folder under that name.");
                }
                if (used(use.name)) {
                    uses.push_back(use);
                }
            }
        }
        for (size_t n = 0; n < images.size(); ++n) {
            if (images[n] == "inputImage" && imageTypes[n] == "image") {
                continue;
            }
            if (transition && (images[n] == "startImage" || images[n] == "endImage")) {
                continue;
            }
            const std::string& type = imageTypes[n];
            info.notes.push_back(std::string("The ") + (type == "audiofft" ? "audioFFT" : type) + " input '" + images[n] + "' " + (type == "image" ? "has no source in NyrexVJ yet" : "is not supported yet") + " and reads transparent black.");
            if (used(images[n])) {
                ImageUse use;
                use.name = images[n];
                uses.push_back(use);
            }
        }

        struct PassInfo {
            std::string target;
            double scale = 1.0;
        };
        std::vector<PassInfo> passes;
        const json* passList = header.contains("PASSES") ? &header["PASSES"] : nullptr;
        if (passList && !passList->is_array()) {
            fail("PASSES must be a list");
        }
        if (passList && static_cast<int>(passList->size()) > kIsfMaxPasses) {
            fail("The ISF file has more than " + std::to_string(kIsfMaxPasses) + " passes");
        }
        if (!passList || passList->empty()) {
            passes.push_back(PassInfo{});
        } else {
            for (size_t n = 0; n < passList->size(); ++n) {
                const json& item = (*passList)[n];
                const std::string where = "PASSES[" + std::to_string(n) + "]";
                if (!item.is_object()) {
                    fail(where + " must be an object");
                }
                PassInfo pass;
                const bool last = n + 1 == passList->size();
                if (item.contains("TARGET")) {
                    if (!item["TARGET"].is_string()) {
                        fail(where + ".TARGET must be text");
                    }
                    pass.target = item["TARGET"].get<std::string>();
                    claim(pass.target, where + ".TARGET");
                }
                if (last && !pass.target.empty()) {
                    fail("The last pass of an ISF file must not have a TARGET, it is the picture that is shown");
                }
                if (!last && pass.target.empty()) {
                    fail(where + " has no TARGET, so it would draw nowhere. Only the last pass may go without a TARGET");
                }
                if (!pass.target.empty()) {
                    double rw = 1.0;
                    double rh = 1.0;
                    if (!ratioOf(item, "WIDTH", true, variables, rw) || !ratioOf(item, "HEIGHT", false, variables, rh)) {
                        info.notes.push_back("The size of '" + pass.target + "' is a fixed size or an expression that is not understood. The full size of the picture is used.");
                        rw = 1.0;
                        rh = 1.0;
                    }
                    double scale = std::max(rw, rh);
                    if (std::fabs(rw - rh) > 0.01) {
                        info.notes.push_back("The buffer '" + pass.target + "' has a different width and height factor. The larger one is used for both.");
                    }
                    if (scale > 1.0 || scale < kMinShaderBufferScale) {
                        info.notes.push_back("The size of '" + pass.target + "' is outside a quarter to the full picture and was limited to that.");
                        scale = std::clamp(scale, static_cast<double>(kMinShaderBufferScale), 1.0);
                    }
                    pass.scale = scale;
                }
                passes.push_back(pass);
            }
        }
        int bufferCount = 0;
        for (const PassInfo& pass : passes) {
            bufferCount += pass.target.empty() ? 0 : 1;
        }
        if (bufferCount > kMaxShaderBuffers) {
            fail("The ISF file renders into " + std::to_string(bufferCount) + " buffers, NyrexVJ supports " + std::to_string(kMaxShaderBuffers));
        }
        int buffer = 0;
        for (const PassInfo& pass : passes) {
            if (pass.target.empty()) {
                continue;
            }
            if (used(pass.target)) {
                ImageUse use;
                use.name = pass.target;
                use.kind = ImageUse::Kind::Buffer;
                use.buffer = buffer;
                uses.push_back(use);
            }
            ++buffer;
        }
        if (transition) {
            for (const PassInfo& pass : passes) {
                if (!pass.target.empty()) {
                    fail("An ISF transition cannot have passes with a TARGET. NyrexVJ transitions have one pass and no buffers");
                }
            }
        }
        if (static_cast<int>(uses.size()) > kShaderChannels) {
            fail("The ISF shader reads " + std::to_string(uses.size()) + " different pictures, NyrexVJ can read " + std::to_string(kShaderChannels) + " in one shader");
        }

        std::vector<ShaderParam> live;
        std::string baked;
        int slots = 0;
        std::vector<std::string> bakedNames;
        for (Candidate& candidate : candidates) {
            ShaderParam& param = candidate.param;
            if (static_cast<int>(live.size()) < kMaxShaderParams && slots + param.count <= kShaderValueCount) {
                param.slot = slots;
                slots += param.count;
                live.push_back(param);
                continue;
            }
            bakedNames.push_back(param.name);
            switch (param.kind) {
            case ShaderParamKind::Float:
                baked += "const float " + param.name + " = " + glslFloat(param.def[0]) + ";\n";
                break;
            case ShaderParamKind::Int:
                baked += "const int " + param.name + " = " + number(param.def[0]) + ";\n";
                break;
            case ShaderParamKind::List:
                baked += "const int " + param.name + " = " + number(param.values[static_cast<size_t>(param.def[0])]) + ";\n";
                break;
            case ShaderParamKind::Bool:
                baked += "const bool " + param.name + " = " + (param.def[0] >= 0.5f ? "true" : "false") + ";\n";
                break;
            case ShaderParamKind::Point:
                baked += "const vec2 " + param.name + " = vec2(" + glslFloat(param.def[0]) + ", " + glslFloat(param.def[1]) + ");\n";
                break;
            case ShaderParamKind::Color:
                baked += "const vec4 " + param.name + " = vec4(" + glslFloat(param.def[0]) + ", " + glslFloat(param.def[1]) + ", " + glslFloat(param.def[2]) + ", " + glslFloat(param.def[3]) + ");\n";
                break;
            }
        }
        if (!bakedNames.empty()) {
            std::string names;
            for (size_t n = 0; n < bakedNames.size(); ++n) {
                names += (n ? ", " : "") + bakedNames[n];
            }
            info.notes.push_back("A shader has room for " + std::to_string(kMaxShaderParams) + " controls. These inputs use their default value: " + names + ".");
        }
        const bool filter = pictureInput;
        if (filter) {
            ShaderPlan probe;
            probe.params = live;
            const std::vector<int> controls = shaderFilterParams(probe);
            std::string names;
            for (size_t n = 0; n < live.size(); ++n) {
                if (std::find(controls.begin(), controls.end(), static_cast<int>(n)) == controls.end()) {
                    names += (names.empty() ? "" : ", ") + live[n].name;
                }
            }
            if (!names.empty()) {
                info.notes.push_back("An effect has 4 controls. These inputs keep their default value: " + names + ".");
            }
        }

        std::string out;
        out.reserve(text.size() + 2048);
        out.append(text, 0, headEnd);
        out += "#define RENDERSIZE uRes\n#define TIME uTime\n#define TIMEDELTA uDelta\n#define FRAMEINDEX uFrame\n#define PASSINDEX isf_PassIndex\n";
        out += "#define DATE vec4(2026.0, 1.0, 1.0, 0.0)\n#define isf_FragNormCoord vUv\n#define vv_FragNormCoord vUv\n#define texture2D texture\n";
        out += "#define IMG_SIZE(i) vec2(textureSize(i, 0))\n#define IMG_PIXEL(i, p) texture(i, (p) / vec2(textureSize(i, 0)))\n#define IMG_NORM_PIXEL(i, p) texture(i, p)\n";
        out += "#define IMG_THIS_PIXEL(i) texture(i, vUv)\n#define IMG_THIS_NORM_PIXEL(i) texture(i, vUv)\nint isf_PassIndex = 0;\n";
        for (size_t n = 0; n < uses.size(); ++n) {
            out += "#define " + uses[n].name + " uChannel" + std::to_string(n) + "\n";
        }
        if (transition) {
            out += "#define progress uProgress\n";
        }
        out += baked;
        out += "#line " + std::to_string(lineOf(text, headEnd)) + "\n";
        out += body.text;
        if (out.back() != '\n') {
            out.push_back('\n');
        }
        out += "#pragma nyrexvj plain\n";
        if (transition) {
            out += "#pragma nyrexvj transition\n";
        } else if (filter) {
            out += "#pragma nyrexvj filter\n";
        } else {
            out += "#pragma nyrexvj alpha\n";
        }
        for (const ShaderParam& param : live) {
            out += "#pragma nyrexvj param " + param.name + " ";
            const std::string label = "\"" + cleanText(param.label, 40) + "\"";
            switch (param.kind) {
            case ShaderParamKind::Float:
                out += "float " + label + " " + number(param.min[0]) + " " + number(param.max[0]) + " " + number(param.def[0]);
                break;
            case ShaderParamKind::Int:
                out += "int " + label + " " + number(param.min[0]) + " " + number(param.max[0]) + " " + number(param.def[0]);
                break;
            case ShaderParamKind::Bool:
                out += "bool " + label + (param.def[0] >= 0.5f ? " on" : " off");
                break;
            case ShaderParamKind::Point:
                out += "point " + label + " " + number(param.min[0]) + " " + number(param.min[1]) + " " + number(param.max[0]) + " " + number(param.max[1]) + " " + number(param.def[0]) + " " + number(param.def[1]);
                break;
            case ShaderParamKind::Color:
                out += "color " + label + " " + number(param.def[0]) + " " + number(param.def[1]) + " " + number(param.def[2]) + " " + number(param.def[3]);
                break;
            case ShaderParamKind::List:
                out += "list " + label + " " + number(param.def[0]);
                for (size_t n = 0; n < param.values.size(); ++n) {
                    out += " \"" + cleanText(param.labels[n], 40) + "\" " + number(param.values[n]);
                }
                break;
            }
            out += "\n";
        }
        const auto channels = [&]() {
            std::string lines;
            for (size_t n = 0; n < uses.size(); ++n) {
                lines += "#pragma nyrexvj channel " + std::to_string(n) + " ";
                switch (uses[n].kind) {
                case ImageUse::Kind::Picture:
                    lines += "picture";
                    break;
                case ImageUse::Kind::From:
                    lines += "from";
                    break;
                case ImageUse::Kind::To:
                    lines += "to";
                    break;
                case ImageUse::Kind::Media:
                    lines += "media \"" + uses[n].media + "\"";
                    break;
                case ImageUse::Kind::Buffer:
                    lines += std::string("buffer ") + static_cast<char>('a' + uses[n].buffer);
                    break;
                case ImageUse::Kind::None:
                    lines += "none";
                    break;
                }
                lines += "\n";
            }
            return lines;
        };
        int bufferLetter = 0;
        for (size_t n = 0; n < passes.size(); ++n) {
            if (!passes[n].target.empty()) {
                out += std::string("#pragma nyrexvj buffer ") + static_cast<char>('a' + bufferLetter);
                if (passes[n].scale < 0.999) {
                    out += " scale " + number(std::round(passes[n].scale * 10000.0) / 10000.0);
                }
                ++bufferLetter;
            } else {
                out += "#pragma nyrexvj image";
            }
            out += "\n" + channels();
            out += "void main() { isf_PassIndex = " + std::to_string(n) + "; isf_main(); }\n";
        }
        result.text = out;
        result.ok = true;
    } catch (const Fail& failure) {
        result.error = failure.message;
        result.ok = false;
    } catch (const json::exception&) {
        result.error = "The ISF header could not be read";
        result.ok = false;
    }
    return result;
}

}

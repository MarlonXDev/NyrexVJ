#include "check.hpp"

#include "core/preset.hpp"
#include "render/clip_library.hpp"
#include "render/isf.hpp"
#include "render/shader_source.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readFile(const std::string& name) {
    std::ifstream in(std::string(NVJ_ISF_EXAMPLES) + "/" + name, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string wrap(const std::string& header, const std::string& body = "void main() { gl_FragColor = vec4(1.0); }\n") {
    return "/*{" + header + "}*/\n" + body;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

int lineOf(const std::string& text, size_t position) {
    int line = 1;
    for (size_t i = 0; i < position && i < text.size(); ++i) {
        line += text[i] == '\n' ? 1 : 0;
    }
    return line;
}

void testDetection() {
    CHECK(nvj::looksLikeIsf("/*{ \"INPUTS\": [] }*/\nvoid main() {}"));
    CHECK(nvj::looksLikeIsf("\n\n  /*  {\n}*/ void main() {}"));
    CHECK(!nvj::looksLikeIsf("// comment\nvoid main() {}"));
    CHECK(!nvj::looksLikeIsf("/* plain comment */\nvoid main() {}"));
    CHECK(!nvj::looksLikeIsf("void main() { }\n/*{ }*/"));
    CHECK(!nvj::looksLikeIsf(""));
    CHECK(!nvj::translateIsf("void main() {}").isIsf);
    const nvj::ShaderSource native = nvj::prepareShaderSource("void main() { fragColor = vec4(1.0); }\n");
    CHECK(native.ok && !native.plan);
    CHECK(nvj::isShaderFileName("tunnel.fs"));
}

void testPlasma() {
    const std::string text = readFile("isf_plasma.fs");
    CHECK(!text.empty());
    const nvj::ShaderSource source = nvj::prepareShaderSource(text);
    CHECK(source.ok);
    CHECK(source.advanced && source.plan != nullptr);
    if (!source.plan) {
        return;
    }
    const nvj::ShaderPlan& plan = *source.plan;
    CHECK(!plan.filter);
    CHECK(plan.plain);
    CHECK(plan.alpha);
    CHECK(plan.passes.size() == 1 && plan.passes[0].image);
    CHECK(plan.params.size() == 5);
    CHECK(plan.valueCount() == 8);
    CHECK(plan.info.format == "ISF 2.0");
    CHECK(contains(plan.info.description, "plasma"));
    CHECK(plan.info.categories.size() == 1 && plan.info.categories[0] == "Generator");
    if (plan.params.size() == 5) {
        CHECK(plan.params[0].name == "speed" && plan.params[0].kind == nvj::ShaderParamKind::Float && plan.params[0].slot == 0);
        CHECK(plan.params[0].label == "Speed" && plan.params[0].min[0] == 0.0f && plan.params[0].max[0] == 4.0f && plan.params[0].def[0] == 1.0f);
        CHECK(plan.params[2].kind == nvj::ShaderParamKind::List && plan.params[2].labels.size() == 3 && plan.params[2].labels[1] == "Rings");
        CHECK(plan.params[2].values.size() == 3 && plan.params[2].values[2] == 2.0f && plan.params[2].def[0] == 0.0f);
        CHECK(plan.params[3].kind == nvj::ShaderParamKind::Color && plan.params[3].slot == 3 && plan.params[3].count == 4);
        CHECK(std::fabs(plan.params[3].def[1] - 0.45f) < 1.0e-6f);
        CHECK(plan.params[4].kind == nvj::ShaderParamKind::Bool && plan.params[4].slot == 7 && plan.params[4].def[0] == 0.0f);
    }
    const std::string pass = nvj::planPassFragmentSource(plan, false, 0);
    CHECK(contains(pass, "uniform float speed;"));
    CHECK(contains(pass, "uniform int mode;"));
    CHECK(contains(pass, "uniform vec4 tint;"));
    CHECK(contains(pass, "uniform bool invert;"));
    CHECK(!contains(pass, "float hash21(") && !contains(pass, "const float PI"));
    CHECK(contains(pass, "isf_main"));
    CHECK(contains(pass, "fragColor"));
    CHECK(!contains(pass, "gl_FragColor"));
    const std::string body = source.plan->common;
    const size_t bodyAt = text.find("void main()");
    const std::string lineMarker = "#line " + std::to_string(lineOf(text, bodyAt) - 1) + "\n\nvoid isf_main()";
    CHECK(contains(body, lineMarker));
    CHECK(source.plan->passes[0].firstLine > lineOf(text, bodyAt));
}

void testFeedbackGenerator() {
    const nvj::ShaderSource source = nvj::prepareShaderSource(readFile("isf_feedback_dots.fs"));
    CHECK(source.ok && source.plan);
    if (!source.plan) {
        return;
    }
    const nvj::ShaderPlan& plan = *source.plan;
    CHECK(plan.passes.size() == 2);
    CHECK(plan.bufferCount() == 1);
    if (plan.passes.size() == 2) {
        CHECK(!plan.passes[0].image && plan.passes[0].buffer == 0 && std::fabs(plan.passes[0].scale - 0.5f) < 1.0e-4f);
        CHECK(plan.passes[1].image);
        CHECK(plan.passes[0].inputs[0].kind == nvj::ShaderInputKind::Buffer && plan.passes[0].inputs[0].buffer == 0);
        CHECK(plan.passes[1].inputs[0].kind == nvj::ShaderInputKind::Buffer && plan.passes[1].inputs[0].buffer == 0);
        CHECK(contains(plan.passes[0].code, "isf_PassIndex = 0"));
        CHECK(contains(plan.passes[1].code, "isf_PassIndex = 1"));
    }
    CHECK(plan.params.size() == 4);
}

void testFilters() {
    const nvj::ShaderSource shift = nvj::prepareShaderSource(readFile("isf_color_shift.fs"));
    CHECK(shift.ok && shift.plan && shift.plan->filter);
    if (shift.plan) {
        CHECK(!shift.plan->alpha);
        CHECK(shift.plan->params.size() == 3);
        CHECK(nvj::shaderFilterParams(*shift.plan).size() == 3);
        CHECK(shift.plan->passes.size() == 1 && shift.plan->passes[0].inputs[0].kind == nvj::ShaderInputKind::Picture);
        const std::string pass = nvj::planPassFragmentSource(*shift.plan, false, 0);
        CHECK(contains(pass, "uniform sampler2D uPicture;") && contains(pass, "uniform float uMix;"));
        CHECK(contains(pass, "#define inputImage uChannel0"));
        CHECK(contains(pass, "nyrexUserMain();"));
    }
    const nvj::ShaderSource blur = nvj::prepareShaderSource(readFile("isf_blur.fs"));
    CHECK(blur.ok && blur.plan && blur.plan->filter);
    if (blur.plan && blur.plan->passes.size() == 2) {
        CHECK(blur.plan->bufferCount() == 1);
        CHECK(std::fabs(blur.plan->passes[0].scale - 0.5f) < 1.0e-4f);
        for (const nvj::ShaderPassSource& pass : blur.plan->passes) {
            CHECK(pass.inputs[0].kind == nvj::ShaderInputKind::Picture);
            CHECK(pass.inputs[1].kind == nvj::ShaderInputKind::Buffer && pass.inputs[1].buffer == 0);
        }
    } else {
        CHECK(false);
    }
    const nvj::ShaderSource trails = nvj::prepareShaderSource(readFile("isf_trails.fs"));
    CHECK(trails.ok && trails.plan && trails.plan->filter && trails.plan->bufferCount() == 1);

    const nvj::ShaderSource notAClip = nvj::prepareShaderSource(wrap("\"INPUTS\": [{\"NAME\": \"inputImage\", \"TYPE\": \"image\"}]", "void main() { gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage); }\n"));
    CHECK(notAClip.ok && notAClip.plan && notAClip.plan->filter);

    std::string many = "\"INPUTS\": [{\"NAME\": \"inputImage\", \"TYPE\": \"image\"}";
    for (int i = 0; i < 6; ++i) {
        many += ",{\"NAME\": \"v" + std::to_string(i) + "\", \"TYPE\": \"float\", \"DEFAULT\": 0.5}";
    }
    many += "]";
    const nvj::ShaderSource limited = nvj::prepareShaderSource(wrap(many, "void main() { gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage) * v0 * v5; }\n"));
    CHECK(limited.ok && limited.plan);
    if (limited.plan) {
        CHECK(limited.plan->params.size() == 6);
        CHECK(nvj::shaderFilterParams(*limited.plan).size() == 4);
        bool noted = false;
        for (const std::string& note : limited.plan->info.notes) {
            noted = noted || (contains(note, "4 controls") && contains(note, "v4") && contains(note, "v5"));
        }
        CHECK(noted);
    }
}

void testUnsupportedAreShown() {
    std::string header = "\"INPUTS\": ["
                         "{\"NAME\": \"inputImage\", \"TYPE\": \"image\"},"
                         "{\"NAME\": \"mask\", \"TYPE\": \"image\"},"
                         "{\"NAME\": \"fft\", \"TYPE\": \"audioFFT\"},"
                         "{\"NAME\": \"hit\", \"TYPE\": \"event\"},"
                         "{\"NAME\": \"pos\", \"TYPE\": \"point2D\"}],"
                         "\"IMPORTED\": {\"tex\": {\"PATH\": \"../../secret/tex.png\"}}";
    const nvj::ShaderSource source = nvj::prepareShaderSource(wrap(header, "void main() { gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage) + IMG_THIS_NORM_PIXEL(mask) + IMG_THIS_NORM_PIXEL(fft) + IMG_THIS_NORM_PIXEL(tex); }\n"));
    CHECK(source.ok && source.plan);
    if (!source.plan) {
        return;
    }
    const std::vector<std::string>& notes = source.plan->info.notes;
    const auto has = [&](const std::string& part) {
        for (const std::string& note : notes) {
            if (contains(note, part)) {
                return true;
            }
        }
        return false;
    };
    CHECK(has("'mask'") && has("no source"));
    CHECK(has("audioFFT") && has("'fft'"));
    CHECK(has("event input 'hit'"));
    CHECK(has("point input 'pos'") && has("1920"));
    CHECK(has("Image: tex") && !has(".."));
    const nvj::ShaderSource dated = nvj::prepareShaderSource(wrap("", "void main() { gl_FragColor = vec4(DATE.x / 3000.0); }\n"));
    CHECK(dated.ok && dated.plan);
    bool dateNoted = false;
    if (dated.plan) {
        for (const std::string& note : dated.plan->info.notes) {
            dateNoted = dateNoted || contains(note, "reads DATE");
        }
    }
    CHECK(dateNoted);
    const nvj::ShaderPlan& plan = *source.plan;
    CHECK(plan.passes.size() == 1);
    int pictures = 0;
    int media = 0;
    int none = 0;
    for (const nvj::ShaderInput& input : plan.passes[0].inputs) {
        pictures += input.kind == nvj::ShaderInputKind::Picture ? 1 : 0;
        media += input.kind == nvj::ShaderInputKind::Media ? 1 : 0;
        none += input.kind == nvj::ShaderInputKind::None ? 1 : 0;
    }
    CHECK(pictures == 1 && media == 1);
    CHECK(plan.mediaNames().size() == 1 && plan.mediaNames()[0] == "tex");

    std::string inputs = "\"INPUTS\": [";
    for (int i = 0; i < 11; ++i) {
        inputs += std::string(i ? "," : "") + "{\"NAME\": \"f" + std::to_string(i) + "\", \"TYPE\": \"float\", \"DEFAULT\": 0.25}";
    }
    inputs += "]";
    const nvj::ShaderSource baked = nvj::prepareShaderSource(wrap(inputs, "void main() { gl_FragColor = vec4(f0 + f10); }\n"));
    CHECK(baked.ok && baked.plan);
    if (baked.plan) {
        CHECK(baked.plan->params.size() == 8);
        bool noted = false;
        for (const std::string& note : baked.plan->info.notes) {
            noted = noted || (contains(note, "f8") && contains(note, "f10") && contains(note, "default"));
        }
        CHECK(noted);
        CHECK(contains(baked.plan->common, "const float f9 = 0.25;"));
    }

    std::string colours = "\"INPUTS\": [";
    for (int i = 0; i < 5; ++i) {
        colours += std::string(i ? "," : "") + "{\"NAME\": \"c" + std::to_string(i) + "\", \"TYPE\": \"color\"}";
    }
    colours += "]";
    const nvj::ShaderSource sixteen = nvj::prepareShaderSource(wrap(colours, "void main() { gl_FragColor = c0 + c4; }\n"));
    CHECK(sixteen.ok && sixteen.plan && sixteen.plan->params.size() == 4 && sixteen.plan->valueCount() == 16);
    CHECK(sixteen.plan && contains(sixteen.plan->common, "const vec4 c4 = vec4(1.0, 1.0, 1.0, 1.0);"));
}

struct ErrorCase {
    const char* name;
    std::string text;
    const char* expected;
};

std::string nested(int depth) {
    return "\"X\": " + std::string(static_cast<size_t>(depth), '[') + std::string(static_cast<size_t>(depth), ']');
}

void testErrors() {
    const std::string body = "void main() { gl_FragColor = vec4(1.0); }\n";
    std::vector<ErrorCase> cases;
    cases.push_back({"unclosed comment", "/*{ \"INPUTS\": [] \nvoid main() {}\n", "not closed"});
    cases.push_back({"bad json line", "/*{\n\"INPUTS\": [\n  {\"NAME\": }\n]\n}*/\n" + body, "near line 3"});
    cases.push_back({"deep nesting", wrap(nested(13)), "nested deeper"});
    cases.push_back({"huge header", wrap("\"DESCRIPTION\": \"" + std::string(70000, 'x') + "\""), "larger than 64 KiB"});
    cases.push_back({"inputs not a list", wrap("\"INPUTS\": {}"), "INPUTS must be a list"});
    cases.push_back({"input without type", wrap("\"INPUTS\": [{\"NAME\": \"a\"}]"), "needs a NAME and a TYPE"});
    cases.push_back({"input not an object", wrap("\"INPUTS\": [3]"), "needs a NAME and a TYPE"});
    cases.push_back({"bad name digit", wrap("\"INPUTS\": [{\"NAME\": \"1a\", \"TYPE\": \"float\"}]"), "not usable"});
    cases.push_back({"bad name double underscore", wrap("\"INPUTS\": [{\"NAME\": \"a__b\", \"TYPE\": \"float\"}]"), "not usable"});
    cases.push_back({"bad name gl prefix", wrap("\"INPUTS\": [{\"NAME\": \"gl_x\", \"TYPE\": \"float\"}]"), "not usable"});
    cases.push_back({"bad name too long", wrap("\"INPUTS\": [{\"NAME\": \"" + std::string(40, 'a') + "\", \"TYPE\": \"float\"}]"), "not usable"});
    cases.push_back({"reserved name", wrap("\"INPUTS\": [{\"NAME\": \"uTime\", \"TYPE\": \"float\"}]"), "reserved"});
    cases.push_back({"isf builtin name", wrap("\"INPUTS\": [{\"NAME\": \"TIME\", \"TYPE\": \"float\"}]"), "reserved"});
    cases.push_back({"glsl keyword name", wrap("\"INPUTS\": [{\"NAME\": \"float\", \"TYPE\": \"float\"}]"), "reserved"});
    cases.push_back({"glsl reserved word name", wrap("\"INPUTS\": [{\"NAME\": \"filter\", \"TYPE\": \"bool\"}]"), "reserved"});
    cases.push_back({"duplicate name", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"float\"},{\"NAME\": \"a\", \"TYPE\": \"bool\"}]"), "used twice"});
    cases.push_back({"unknown type", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"vec3\"}]"), "does not know"});
    cases.push_back({"min not below max", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"float\", \"MIN\": 2, \"MAX\": 2}]"), "MIN must be smaller"});
    cases.push_back({"min not a number", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"float\", \"MIN\": \"zero\"}]"), "must be a number"});
    cases.push_back({"huge number", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"float\", \"MAX\": 1e12}]"), "outside"});
    cases.push_back({"point with three numbers", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"point2D\", \"DEFAULT\": [1, 2, 3]}]"), "list of 2"});
    cases.push_back({"color with two numbers", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"color\", \"DEFAULT\": [1, 2]}]"), "numbers"});
    cases.push_back({"long with 17 values", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"long\", \"VALUES\": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}]"), "VALUES"});
    cases.push_back({"long with text values", wrap("\"INPUTS\": [{\"NAME\": \"a\", \"TYPE\": \"long\", \"VALUES\": [\"a\"]}]"), "must be a number"});
    cases.push_back({"transition", wrap("\"INPUTS\": [{\"NAME\": \"startImage\", \"TYPE\": \"image\"},{\"NAME\": \"endImage\", \"TYPE\": \"image\"},{\"NAME\": \"progress\", \"TYPE\": \"float\"}]"), "transition"});
    cases.push_back({"passes not a list", wrap("\"PASSES\": 3"), "PASSES must be a list"});
    cases.push_back({"last pass with target", wrap("\"PASSES\": [{\"TARGET\": \"a\"}]"), "must not have a TARGET"});
    cases.push_back({"middle pass without target", wrap("\"PASSES\": [{}, {}]"), "no TARGET"});
    cases.push_back({"duplicate target", wrap("\"PASSES\": [{\"TARGET\": \"a\"},{\"TARGET\": \"a\"},{}]"), "used twice"});
    cases.push_back({"five buffers", wrap("\"PASSES\": [{\"TARGET\": \"a\"},{\"TARGET\": \"b\"},{\"TARGET\": \"c\"},{\"TARGET\": \"d\"},{\"TARGET\": \"e\"},{}]"), "5 buffers"});
    std::string seventeen = "\"PASSES\": [";
    for (int i = 0; i < 17; ++i) {
        seventeen += std::string(i ? "," : "") + "{}";
    }
    cases.push_back({"seventeen passes", wrap(seventeen + "]"), "more than 16 passes"});
    cases.push_back({"varying", wrap("", "varying vec2 left;\nvoid main() { gl_FragColor = vec4(left, 0.0, 1.0); }\n"), "'varying'"});
    cases.push_back({"gl_TexCoord", wrap("", "void main() { gl_FragColor = vec4(gl_TexCoord[0].xy, 0.0, 1.0); }\n"), "gl_TexCoord"});
    cases.push_back({"sampler2DRect", wrap("", "uniform sampler2DRect r;\nvoid main() { gl_FragColor = vec4(1.0); }\n"), "sampler2DRect"});
    cases.push_back({"vertex shader init", wrap("", "void main() { isf_vertShaderInit(); gl_FragColor = vec4(1.0); }\n"), "vertex shader"});
    cases.push_back({"no main", wrap("", "vec4 helper() { return vec4(1.0); }\n"), "no void main()"});
    cases.push_back({"header only", "/*{ }*/", "no shader code"});
    cases.push_back({"header and blank", "/*{ }*/\n\n  \n", "no shader code"});
    std::string imports = "\"IMPORTED\": {";
    for (int i = 0; i < 9; ++i) {
        imports += std::string(i ? "," : "") + "\"p" + std::to_string(i) + "\": {\"PATH\": \"a.png\"}";
    }
    cases.push_back({"nine imports", wrap(imports + "}"), "imports more than 8"});
    cases.push_back({"imported entry without name", wrap("\"IMPORTED\": [{\"PATH\": \"a.png\"}]"), "needs a NAME"});
    std::string sixtyFive = "\"INPUTS\": [";
    for (int i = 0; i < 65; ++i) {
        sixtyFive += std::string(i ? "," : "") + "{\"NAME\": \"n" + std::to_string(i) + "\", \"TYPE\": \"bool\"}";
    }
    cases.push_back({"sixty five inputs", wrap(sixtyFive + "]"), "more than 64 inputs"});
    cases.push_back({"categories not a list", wrap("\"CATEGORIES\": 5"), "CATEGORIES"});
    cases.push_back({"description not text", wrap("\"DESCRIPTION\": 5"), "must be text"});
    std::string images = "\"INPUTS\": [{\"NAME\": \"inputImage\", \"TYPE\": \"image\"}], \"IMPORTED\": {";
    for (int i = 0; i < 4; ++i) {
        images += std::string(i ? "," : "") + "\"t" + std::to_string(i) + "\": {\"PATH\": \"t" + std::to_string(i) + ".png\"}";
    }
    images += "}";
    cases.push_back({"five images read", wrap(images, "void main() { gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage) + IMG_THIS_NORM_PIXEL(t0) + IMG_THIS_NORM_PIXEL(t1) + IMG_THIS_NORM_PIXEL(t2) + IMG_THIS_NORM_PIXEL(t3); }\n"), "5 different pictures"});
    for (const ErrorCase& item : cases) {
        const nvj::ShaderSource source = nvj::prepareShaderSource(item.text);
        const bool failed = !source.ok && contains(source.error, item.expected);
        if (!failed) {
            std::fprintf(stderr, "error case '%s': ok=%d error='%s'\n", item.name, source.ok ? 1 : 0, source.error.substr(0, 200).c_str());
        }
        CHECK(failed);
        const nvj::IsfResult direct = nvj::translateIsf(item.text);
        CHECK(!direct.ok && direct.isIsf);
    }
}

void testBufferSizes() {
    struct Size {
        const char* width;
        const char* height;
        float scale;
        const char* note;
    };
    const std::vector<Size> sizes = {
        {"$WIDTH/2", "$HEIGHT/2", 0.5f, nullptr},
        {"$WIDTH*0.25", "$HEIGHT*0.25", 0.25f, nullptr},
        {"floor($WIDTH/3.0)", "floor($HEIGHT/3.0)", 0.3333f, nullptr},
        {"$WIDTH/16", "$HEIGHT/16", 0.25f, "outside a quarter"},
        {"64", "64", 1.0f, "fixed size"},
        {"garbage(", "$HEIGHT", 1.0f, "fixed size"},
        {"$WIDTH/2", "$HEIGHT/4", 0.5f, "different width and height"},
        {"$WIDTH*$k", "$HEIGHT*$k", 0.5f, nullptr},
    };
    for (const Size& size : sizes) {
        const std::string header = std::string("\"INPUTS\": [{\"NAME\": \"k\", \"TYPE\": \"float\", \"DEFAULT\": 0.5}], \"PASSES\": [{\"TARGET\": \"b\", \"WIDTH\": \"") + size.width + "\", \"HEIGHT\": \"" + size.height + "\"}, {}]";
        const nvj::ShaderSource source = nvj::prepareShaderSource(wrap(header, "void main() { gl_FragColor = IMG_THIS_NORM_PIXEL(b) * k; }\n"));
        CHECK(source.ok && source.plan && source.plan->passes.size() == 2);
        if (!source.plan || source.plan->passes.size() != 2) {
            continue;
        }
        CHECK(std::fabs(source.plan->passes[0].scale - size.scale) < 0.002f);
        bool noted = false;
        for (const std::string& note : source.plan->info.notes) {
            noted = noted || (size.note && contains(note, size.note));
        }
        CHECK(size.note == nullptr ? source.plan->info.notes.empty() : noted);
    }
}

void testParamDirective() {
    const std::string code = "void main() { fragColor = vec4(1.0); }\n";
    const auto source = [&](const std::string& lines) { return nvj::prepareShaderSource(lines + code); };
    const nvj::ShaderSource good = source(
        "#pragma nyrexvj param speed float \"Speed\" 0 4 1\n"
        "#pragma nyrexvj param steps int \"Steps\" 1 9 3\n"
        "#pragma nyrexvj param flash bool \"Flash\" on\n"
        "#pragma nyrexvj param pos point \"Position\" 0 0 100 50 10 20\n"
        "#pragma nyrexvj param tint color \"Tint\" 1 0.5 0 1\n"
        "#pragma nyrexvj param mode list \"Mode\" 1 \"A\" 10 \"B\" 20 \"C\" 30\n");
    CHECK(good.ok && good.plan && good.plan->params.size() == 6);
    if (good.plan && good.plan->params.size() == 6) {
        CHECK(good.plan->valueCount() == 10);
        CHECK(good.plan->params[3].slot == 3 && good.plan->params[4].slot == 5 && good.plan->params[5].slot == 9);
        CHECK(good.plan->params[5].def[0] == 1.0f && good.plan->params[5].values[2] == 30.0f);
        CHECK(good.plan->params[2].def[0] == 1.0f);
    }
    struct Bad {
        const char* line;
        const char* expected;
    };
    const std::vector<Bad> bad = {
        {"#pragma nyrexvj param speed\n", "needs a name"},
        {"#pragma nyrexvj param 1x float \"S\" 0 1 0\n", "not a valid name"},
        {"#pragma nyrexvj param a__b float \"S\" 0 1 0\n", "not a valid name"},
        {"#pragma nyrexvj param uTime float \"S\" 0 1 0\n", "already used"},
        {"#pragma nyrexvj param mainImage float \"S\" 0 1 0\n", "already used"},
        {"#pragma nyrexvj param a vec3 \"S\" 0 1 0\n", "unknown parameter kind"},
        {"#pragma nyrexvj param a float S 0 1 0\n", "in quotes"},
        {"#pragma nyrexvj param a float \"\" 0 1 0\n", "1 to 40"},
        {"#pragma nyrexvj param a float \"S\" 0 1\n", "use: param"},
        {"#pragma nyrexvj param a float \"S\" 1 1 1\n", "must be smaller"},
        {"#pragma nyrexvj param a float \"S\" 0 1 2\n", "between the minimum"},
        {"#pragma nyrexvj param a float \"S\" 0 1e9 0\n", "from -1000000"},
        {"#pragma nyrexvj param a int \"S\" 0.4 0.45 0\n", "must be smaller"},
        {"#pragma nyrexvj param a bool \"S\" maybe\n", "on or off"},
        {"#pragma nyrexvj param a point \"S\" 0 0 1 1 0\n", "use: param"},
        {"#pragma nyrexvj param a point \"S\" 0 0 0 1 0 0\n", "minimum < maximum"},
        {"#pragma nyrexvj param a color \"S\" 1 0 0\n", "use: param"},
        {"#pragma nyrexvj param a list \"S\" 0 \"A\"\n", "use: param"},
        {"#pragma nyrexvj param a list \"S\" 3 \"A\" 1 \"B\" 2\n", "number of an entry"},
        {"#pragma nyrexvj param a list \"S\" 0 A 1 \"B\" 2\n", "label in quotes"},
        {"#pragma nyrexvj param a float \"S\" 0 1 0\n#pragma nyrexvj param a bool \"T\" on\n", "defined twice"},
        {"#pragma nyrexvj param a color \"A\" 1 1 1 1\n#pragma nyrexvj param b color \"B\" 1 1 1 1\n#pragma nyrexvj param c color \"C\" 1 1 1 1\n#pragma nyrexvj param d color \"D\" 1 1 1 1\n#pragma nyrexvj param e color \"E\" 1 1 1 1\n", "16 numbers"},
        {"#pragma nyrexvj plain now\n", "takes no words"},
        {"#pragma nyrexvj filter on\n", "takes no words"},
        {"#pragma nyrexvj channel 0 picture\n", "needs the line #pragma nyrexvj filter"},
    };
    for (const Bad& item : bad) {
        const nvj::ShaderSource result = source(item.line);
        if (result.ok || !contains(result.error, item.expected)) {
            std::fprintf(stderr, "param case '%s' -> ok=%d '%s'\n", item.line, result.ok ? 1 : 0, result.error.c_str());
        }
        CHECK(!result.ok && contains(result.error, item.expected));
    }
    std::string nine;
    for (int i = 0; i < 9; ++i) {
        nine += "#pragma nyrexvj param p" + std::to_string(i) + " bool \"P\" off\n";
    }
    const nvj::ShaderSource tooMany = source(nine);
    CHECK(!tooMany.ok && contains(tooMany.error, "at most 8"));
    std::string longList = "#pragma nyrexvj param a list \"S\" 0";
    for (int i = 0; i < 17; ++i) {
        longList += " \"E" + std::to_string(i) + "\" " + std::to_string(i);
    }
    const nvj::ShaderSource listResult = source(longList + "\n");
    CHECK(!listResult.ok && contains(listResult.error, "at most 16"));

    const nvj::ShaderSource plain = source("#pragma nyrexvj plain\n");
    CHECK(plain.ok && plain.plan && plain.plan->plain && !plain.plan->filter);
    if (plain.plan) {
        const std::string text = nvj::planPassFragmentSource(*plain.plan, false, 0);
        CHECK(!contains(text, "float hash21(") && contains(text, "uniform vec4 uAudio;"));
    }
    const nvj::ShaderSource normal = source("#pragma nyrexvj alpha\n");
    CHECK(normal.ok && normal.plan);
    if (normal.plan) {
        CHECK(contains(nvj::planPassFragmentSource(*normal.plan, false, 0), "float hash21("));
    }
}

void testValues() {
    nvj::ShaderPlan plan;
    nvj::ShaderParam amount;
    amount.name = "amount";
    amount.kind = nvj::ShaderParamKind::Float;
    amount.slot = 0;
    amount.min = {0.0f, 0.0f, 0.0f, 0.0f};
    amount.max = {10.0f, 0.0f, 0.0f, 0.0f};
    amount.def = {2.0f, 0.0f, 0.0f, 0.0f};
    nvj::ShaderParam mode;
    mode.name = "mode";
    mode.kind = nvj::ShaderParamKind::List;
    mode.slot = 1;
    mode.values = {5.0f, 6.0f, 7.0f};
    mode.labels = {"a", "b", "c"};
    mode.max = {2.0f, 0.0f, 0.0f, 0.0f};
    mode.def = {1.0f, 0.0f, 0.0f, 0.0f};
    nvj::ShaderParam tint;
    tint.name = "tint";
    tint.kind = nvj::ShaderParamKind::Color;
    tint.slot = 2;
    tint.count = 4;
    tint.max = {1.0f, 1.0f, 1.0f, 1.0f};
    tint.def = {0.1f, 0.2f, 0.3f, 0.4f};
    plan.params = {amount, mode, tint};

    nvj::ShaderValues stored = nvj::unsetShaderValues();
    CHECK(nvj::shaderValuesUnset(stored));
    nvj::ShaderValues resolved = nvj::resolveShaderValues(plan, stored);
    CHECK(resolved[0] == 2.0f && resolved[1] == 1.0f && std::fabs(resolved[5] - 0.4f) < 1.0e-6f);
    stored[0] = 99.0f;
    stored[1] = 7.4f;
    stored[3] = -4.0f;
    CHECK(!nvj::shaderValuesUnset(stored));
    resolved = nvj::resolveShaderValues(plan, stored);
    CHECK(resolved[0] == 10.0f);
    CHECK(resolved[1] == 2.0f);
    CHECK(resolved[3] == 0.0f);
    CHECK(std::fabs(resolved[2] - 0.1f) < 1.0e-6f);
    stored[0] = std::nanf("");
    CHECK(nvj::resolveShaderValues(plan, stored)[0] == 2.0f);

    CHECK(nvj::shaderParamFromControl(amount, 0.5f) == 5.0f);
    CHECK(nvj::shaderParamFromControl(amount, -3.0f) == 0.0f && nvj::shaderParamFromControl(amount, 7.0f) == 10.0f);
    CHECK(std::fabs(nvj::shaderControlFromParam(amount, nvj::shaderParamFromControl(amount, 0.3f)) - 0.3f) < 1.0e-6f);
    CHECK(nvj::shaderParamFromControl(mode, 0.0f) == 0.0f && nvj::shaderParamFromControl(mode, 0.5f) == 1.0f && nvj::shaderParamFromControl(mode, 1.0f) == 2.0f);
    for (int i = 0; i < 3; ++i) {
        CHECK(nvj::shaderParamFromControl(mode, nvj::shaderControlFromParam(mode, static_cast<float>(i))) == static_cast<float>(i));
    }
    nvj::ShaderParam flag;
    flag.kind = nvj::ShaderParamKind::Bool;
    flag.max = {1.0f, 0.0f, 0.0f, 0.0f};
    CHECK(nvj::shaderParamFromControl(flag, 0.49f) == 0.0f && nvj::shaderParamFromControl(flag, 0.5f) == 1.0f);
    nvj::ShaderPlan manyPlan;
    for (int i = 0; i < 6; ++i) {
        nvj::ShaderParam item = i == 2 ? tint : amount;
        item.name = "p" + std::to_string(i);
        manyPlan.params.push_back(item);
    }
    CHECK(nvj::shaderFilterParams(manyPlan).size() == 4);
    CHECK(nvj::shaderFilterParams(manyPlan)[2] == 3);
}

void testPresetValues() {
    nvj::Composition comp;
    comp.layers[1].playback[2].values[0] = 0.75f;
    comp.layers[1].playback[2].values[5] = -2.5f;
    comp.layers[1].playback[2].speed = 2.0f;
    const std::string text = nvj::compositionToJson(comp);
    CHECK(contains(text, "\"values\""));
    nvj::Composition back;
    std::string error;
    CHECK(nvj::parseUserPreset("values", text, back, error));
    CHECK(back.layers[1].playback[2].values[0] == 0.75f);
    CHECK(back.layers[1].playback[2].values[5] == -2.5f);
    CHECK(back.layers[1].playback[2].values[1] == nvj::kClipValueUnset);
    CHECK(back.layers[1].playback[2].speed == 2.0f);
    CHECK(back.layers[0].playback[0].isDefault());
    nvj::Composition plain;
    CHECK(!contains(nvj::compositionToJson(plain), "\"values\""));
    std::string hostile = text;
    const size_t at = hostile.find("0.75");
    if (at != std::string::npos) {
        hostile.replace(at, 4, "1e39");
    }
    nvj::Composition guarded;
    CHECK(nvj::parseUserPreset("values", hostile, guarded, error));
    CHECK(guarded.layers[1].playback[2].values[0] == nvj::kClipValueUnset);
}

std::uint32_t next(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void testHostileInput() {
    const std::vector<std::string> files = {"isf_plasma.fs", "isf_feedback_dots.fs", "isf_color_shift.fs", "isf_blur.fs", "isf_trails.fs"};
    const auto begin = std::chrono::steady_clock::now();
    size_t calls = 0;
    size_t accepted = 0;
    std::uint32_t state = 2463534242u;
    for (const std::string& name : files) {
        const std::string text = readFile(name);
        CHECK(!text.empty());
        for (size_t length = 0; length <= text.size(); ++length) {
            const nvj::ShaderSource source = nvj::prepareShaderSource(text.substr(0, length));
            CHECK(source.ok || !source.error.empty());
            accepted += source.ok ? 1 : 0;
            ++calls;
        }
        for (int round = 0; round < 3000; ++round) {
            std::string mutated = text;
            const int changes = 1 + static_cast<int>(next(state) % 4u);
            for (int c = 0; c < changes; ++c) {
                const size_t at = next(state) % mutated.size();
                const unsigned kind = next(state) % 4u;
                if (kind == 0) {
                    mutated[at] = static_cast<char>(next(state) % 256u);
                } else if (kind == 1) {
                    mutated.erase(at, 1 + next(state) % 8u);
                } else if (kind == 2) {
                    mutated.insert(at, std::string(1 + next(state) % 6u, "{}[]\":,*/\\0e-"[next(state) % 13u]));
                } else {
                    mutated.insert(at, "\"TARGET\": \"q\", \"WIDTH\": \"$WIDTH*$HEIGHT\", ");
                }
                if (mutated.empty()) {
                    mutated = "x";
                }
            }
            const nvj::ShaderSource source = nvj::prepareShaderSource(mutated);
            CHECK(source.ok || !source.error.empty());
            accepted += source.ok ? 1 : 0;
            ++calls;
        }
    }
    for (int round = 0; round < 2000; ++round) {
        std::string garbage = "/*{";
        const size_t size = next(state) % 600u;
        for (size_t i = 0; i < size; ++i) {
            garbage.push_back("{}[]\",:0123456789.-eE/*\n \\tabc$"[next(state) % 31u]);
        }
        garbage += round % 2 ? "*/\nvoid main() { gl_FragColor = vec4(1.0); }\n" : "";
        const nvj::ShaderSource source = nvj::prepareShaderSource(garbage);
        CHECK(source.ok || !source.error.empty());
        ++calls;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::printf("hostile input: %zu calls, %zu accepted, %.2f s\n", calls, accepted, seconds);
    CHECK(calls > 20000);
    CHECK(seconds < 60.0);
    std::string deep = "/*{";
    for (int i = 0; i < 30000; ++i) {
        deep.push_back('[');
    }
    deep += "}*/\nvoid main() {}\n";
    const nvj::ShaderSource deepSource = nvj::prepareShaderSource(deep);
    CHECK(!deepSource.ok && contains(deepSource.error, "nested deeper"));
    std::string longLine = "/*{ \"DESCRIPTION\": \"x\" }*/\n";
    longLine += "void main() { gl_FragColor = vec4(" + std::string(200000, '1') + "); }\n";
    const nvj::ShaderSource longSource = nvj::prepareShaderSource(longLine);
    CHECK(longSource.ok || !longSource.error.empty());
}

}

int main() {
    testDetection();
    testPlasma();
    testFeedbackGenerator();
    testFilters();
    testUnsupportedAreShown();
    testErrors();
    testBufferSizes();
    testParamDirective();
    testValues();
    testPresetValues();
    testHostileInput();
    return testing::finish();
}

#include "check.hpp"

#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/shader_source.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

void setDataDir(const std::string& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

int countLines(const std::string& text) {
    int lines = 0;
    for (const char c : text) {
        lines += c == '\n' ? 1 : 0;
    }
    return lines;
}

const nvj::ClipDef* findClip(const std::string& name) {
    const int index = nvj::clipIndexByName(name);
    return index < 0 ? nullptr : &nvj::clipLibrary()[static_cast<size_t>(index)];
}

void testNames() {
    CHECK(nvj::isShaderFileName("a.glsl"));
    CHECK(nvj::isShaderFileName("b.FRAG"));
    CHECK(nvj::isShaderFileName("c.fs"));
    CHECK(!nvj::isShaderFileName("d.txt"));
    CHECK(!nvj::isShaderFileName("glsl"));
    CHECK(nvj::shaderClipName("tunnel") == "Shader: tunnel");
}

void testPrepare() {
    const nvj::ShaderSource native = nvj::prepareShaderSource("void main() {\n    fragColor = vec4(1.0);\n}\n");
    CHECK(native.ok);
    CHECK(!native.shadertoy);
    CHECK(native.error.empty());

    const nvj::ShaderSource toy = nvj::prepareShaderSource("void mainImage(out vec4 c, in vec2 p) {\n    c = vec4(p / iResolution.xy, 0.0, 1.0);\n}\n");
    CHECK(toy.ok);
    CHECK(toy.shadertoy);

    const nvj::ShaderSource both = nvj::prepareShaderSource("void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\nvoid main() { mainImage(fragColor, gl_FragCoord.xy); }\n");
    CHECK(both.ok);
    CHECK(!both.shadertoy);

    const std::string versioned = "#version 330 core\n  #version 450\nvoid main() {\n}\n";
    const nvj::ShaderSource stripped = nvj::prepareShaderSource(versioned);
    CHECK(stripped.ok);
    CHECK(stripped.body.find("#version") == std::string::npos);
    CHECK(countLines(stripped.body) == countLines(versioned));

    const nvj::ShaderSource crlf = nvj::prepareShaderSource("void main()\r\n{\r\n}\r\n");
    CHECK(crlf.ok);
    CHECK(crlf.body.find('\r') == std::string::npos);
    CHECK(countLines(crlf.body) == 3);

    const nvj::ShaderSource unicode = nvj::prepareShaderSource("\xEF\xBB\xBF// K\xC3\xA4se\nvoid main() {}\n");
    CHECK(unicode.ok);
    for (const char c : unicode.body) {
        CHECK(static_cast<unsigned char>(c) < 0x80);
    }
    CHECK(unicode.body.rfind("//", 0) == 0);

    CHECK(!nvj::prepareShaderSource("").ok);
    CHECK(!nvj::prepareShaderSource("float x = 1.0;\n").ok);
    CHECK(!nvj::prepareShaderSource("// void main() {}\n").ok);
    CHECK(!nvj::prepareShaderSource("/* void main() {} */\nfloat x;\n").ok);
    CHECK(!nvj::prepareShaderSource("void mainImageX(out vec4 c, in vec2 p) {}\n").ok);
    CHECK(!nvj::prepareShaderSource("void remain() {}\n").ok);
    CHECK(nvj::prepareShaderSource("void main ( ) {}\n").ok);
    CHECK(nvj::prepareShaderSource("void\n main() {}\n").ok);

    std::string binary = "void main() {}";
    binary.push_back('\0');
    const nvj::ShaderSource bin = nvj::prepareShaderSource(binary);
    CHECK(!bin.ok);
    CHECK(!bin.error.empty());

    const nvj::ShaderSource huge = nvj::prepareShaderSource(std::string(nvj::kMaxShaderFileBytes + 1, ' '));
    CHECK(!huge.ok);
    CHECK(huge.error == "The shader file is larger than 256 KiB");
}

void testFragmentSource() {
    nvj::ClipDef toy;
    toy.name = "Shader: toy";
    toy.kind = nvj::ClipKind::ShaderFile;
    toy.shadertoy = true;
    toy.body = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }";
    const std::string toySource = nvj::clipFragmentSource(toy);
    CHECK(toySource.rfind("#version 330 core", 0) == 0);
    CHECK(toySource.find("#define iResolution") != std::string::npos);
    CHECK(toySource.find("#line 1") != std::string::npos);
    CHECK(toySource.find("mainImage(shaderColor") != std::string::npos);
    CHECK(toySource.find("uAudioTex") != std::string::npos);
    CHECK(toySource.find("float hash21") == std::string::npos);

    nvj::ClipDef native;
    native.name = "Shader: native";
    native.kind = nvj::ClipKind::ShaderFile;
    native.body = "void main() { fragColor = vec4(duo(0.5), 1.0); }";
    const std::string nativeSource = nvj::clipFragmentSource(native);
    CHECK(nativeSource.find("float hash21") != std::string::npos);
    CHECK(nativeSource.find("#line 1") != std::string::npos);
    CHECK(nativeSource.find(native.body) != std::string::npos);

    CHECK(nvj::clipFragmentSource(nvj::clipLibrary()[0]).find("#line 1") == std::string::npos);
}


std::string imageBody() {
    return "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n";
}

void testAdvancedParse() {
    const nvj::ShaderSource legacy = nvj::prepareShaderSource("#pragma optimize(off)\nvoid main() { fragColor = vec4(1.0); }\n");
    CHECK(legacy.ok);
    CHECK(!legacy.advanced);
    CHECK(legacy.plan == nullptr);

    CHECK(!nvj::prepareShaderSource("// #pragma nyrexvj alpha\n" + imageBody()).advanced);
    CHECK(!nvj::prepareShaderSource("/* #pragma nyrexvj alpha\n*/\n" + imageBody()).advanced);
    CHECK(!nvj::prepareShaderSource("#pragma nyrexvjx alpha\n" + imageBody()).advanced);

    const nvj::ShaderSource alpha = nvj::prepareShaderSource("#pragma nyrexvj alpha\n" + imageBody());
    CHECK(alpha.ok);
    CHECK(alpha.advanced);
    CHECK(alpha.shadertoy);
    CHECK(alpha.plan != nullptr);
    if (alpha.plan) {
        CHECK(alpha.plan->alpha);
        CHECK(!alpha.plan->premultiplied);
        CHECK(alpha.plan->passes.size() == 1);
        CHECK(alpha.plan->bufferCount() == 0);
        if (!alpha.plan->passes.empty()) {
            const nvj::ShaderPassSource& image = alpha.plan->passes[0];
            CHECK(image.image);
            CHECK(image.firstLine == 1);
            CHECK(image.inputs[0].kind == nvj::ShaderInputKind::Audio);
            CHECK(image.inputs[1].kind == nvj::ShaderInputKind::None);
            CHECK(image.code.find("nyrexvj") == std::string::npos);
            CHECK(countLines(image.code) == 2);
        }
    }
    const nvj::ShaderSource premultiplied = nvj::prepareShaderSource("  #  pragma   NyrexVJ   ALPHA   Premultiplied\n" + imageBody());
    CHECK(premultiplied.ok && premultiplied.plan && premultiplied.plan->alpha && premultiplied.plan->premultiplied);
    CHECK(nvj::prepareShaderSource("#pragma nyrexvj alpha straight\n" + imageBody()).ok);
    CHECK(!nvj::prepareShaderSource("#pragma nyrexvj alpha soft\n" + imageBody()).ok);
    CHECK(!nvj::prepareShaderSource("#pragma nyrexvj alpha straight extra\n" + imageBody()).ok);

    const std::string multi =
        "const float K = 2.0;\n"
        "#pragma nyrexvj image\n"
        "#pragma nyrexvj channel 0 buffer b\n"
        "#pragma nyrexvj channel 1 media \"Video: loop\" nearest repeat\n"
        "void mainImage(out vec4 c, in vec2 p) { c = vec4(K); }\n"
        "#pragma nyrexvj buffer b scale 0.5\n"
        "#pragma nyrexvj channel 0 buffer a\n"
        "#pragma nyrexvj channel 1 buffer b\n"
        "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.5); }\n"
        "#pragma nyrexvj buffer a\n"
        "#pragma nyrexvj channel 0 none\n"
        "#pragma nyrexvj channel 3 audio linear clamp\n"
        "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.25); }\n";
    const nvj::ShaderSource parsed = nvj::prepareShaderSource(multi);
    CHECK(parsed.ok);
    CHECK(parsed.error.empty());
    CHECK(parsed.advanced);
    CHECK(parsed.shadertoy);
    CHECK(parsed.plan != nullptr);
    if (parsed.plan && parsed.plan->passes.size() == 3) {
        const nvj::ShaderPlan& plan = *parsed.plan;
        CHECK(plan.bufferCount() == 2);
        CHECK(plan.common.find("const float K") != std::string::npos);
        CHECK(plan.commonLine == 1);
        CHECK(plan.passes[0].buffer == 0 && !plan.passes[0].image);
        CHECK(plan.passes[1].buffer == 1);
        CHECK(plan.passes[2].image);
        CHECK(plan.passes[0].label() == "Buffer A");
        CHECK(plan.passes[2].label() == "Image");
        CHECK(plan.passes[1].scale == 0.5f);
        CHECK(plan.passes[0].scale == 1.0f);
        CHECK(plan.passes[0].inputs[0].kind == nvj::ShaderInputKind::None);
        CHECK(plan.passes[0].inputs[3].kind == nvj::ShaderInputKind::Audio);
        CHECK(plan.passes[1].inputs[0].kind == nvj::ShaderInputKind::Buffer && plan.passes[1].inputs[0].buffer == 0);
        CHECK(plan.passes[1].inputs[1].kind == nvj::ShaderInputKind::Buffer && plan.passes[1].inputs[1].buffer == 1);
        CHECK(plan.passes[2].inputs[0].buffer == 1);
        CHECK(plan.passes[2].inputs[1].kind == nvj::ShaderInputKind::Media);
        CHECK(plan.passes[2].inputs[1].media == "Video: loop");
        CHECK(plan.passes[2].inputs[1].nearest);
        CHECK(plan.passes[2].inputs[1].repeat);
        CHECK(plan.passes[2].firstLine == 3);
        CHECK(plan.passes[1].firstLine == 7);
        CHECK(plan.passes[0].firstLine == 11);
        CHECK(plan.mediaNames().size() == 1);
        for (const nvj::ShaderPassSource& pass : plan.passes) {
            CHECK(pass.code.find("nyrexvj") == std::string::npos);
        }
        CHECK(plan.common.find("nyrexvj") == std::string::npos);
    } else {
        CHECK(false);
    }
    CHECK(countLines(parsed.body) == countLines(multi));

    const nvj::ShaderSource commonSection = nvj::prepareShaderSource(
        "#pragma nyrexvj common\nfloat shared() { return 1.0; }\n#pragma nyrexvj buffer a\nvoid main() { fragColor = vec4(shared()); }\n#pragma nyrexvj image\nvoid main() { fragColor = vec4(1.0); }\n");
    CHECK(commonSection.ok && !commonSection.shadertoy && commonSection.plan);
    if (commonSection.plan) {
        CHECK(commonSection.plan->commonLine == 2);
        CHECK(commonSection.plan->common.find("shared") != std::string::npos);
    }

    const nvj::ShaderSource onlyImage = nvj::prepareShaderSource("#pragma nyrexvj image\n#pragma nyrexvj channel 2 media Clip\n" + imageBody());
    CHECK(onlyImage.ok && onlyImage.plan && onlyImage.plan->passes.size() == 1 && onlyImage.plan->passes[0].inputs[2].media == "Clip");

    const auto fails = [](const std::string& text, const std::string& part) {
        const nvj::ShaderSource source = nvj::prepareShaderSource(text);
        return !source.ok && source.error.find(part) != std::string::npos;
    };
    const std::string toy = imageBody();
    CHECK(fails("#pragma nyrexvj\n" + toy, "needs a directive"));
    CHECK(fails("#pragma nyrexvj bloom\n" + toy, "unknown directive 'bloom'"));
    CHECK(fails("#pragma nyrexvj buffer e\n" + toy, "letter from a to d"));
    CHECK(fails("#pragma nyrexvj buffer\n" + toy, "letter from a to d"));
    CHECK(fails("#pragma nyrexvj buffer a scale 0.1\n" + toy + "#pragma nyrexvj image\n" + toy, "0.25 to 1"));
    CHECK(fails("#pragma nyrexvj buffer a scale 1.5\n" + toy + "#pragma nyrexvj image\n" + toy, "0.25 to 1"));
    CHECK(fails("#pragma nyrexvj buffer a scale abc\n" + toy + "#pragma nyrexvj image\n" + toy, "0.25 to 1"));
    CHECK(fails("#pragma nyrexvj buffer a wide\n" + toy + "#pragma nyrexvj image\n" + toy, "use: buffer a"));
    CHECK(fails("#pragma nyrexvj buffer a\n" + toy + "#pragma nyrexvj buffer A\n" + toy + "#pragma nyrexvj image\n" + toy, "defined twice"));
    CHECK(fails("#pragma nyrexvj image\n" + toy + "#pragma nyrexvj image\n" + toy, "image section is defined twice"));
    CHECK(fails("#pragma nyrexvj common\n#pragma nyrexvj common\n" + toy, "common section is defined twice"));
    CHECK(fails("#pragma nyrexvj buffer a\n" + toy, "no image section"));
    CHECK(fails("float x;\n#pragma nyrexvj common\nfloat y;\n#pragma nyrexvj image\n" + toy, "cannot be combined"));
    CHECK(fails("#pragma nyrexvj channel 0 audio\n#pragma nyrexvj buffer a\n" + toy + "#pragma nyrexvj image\n" + toy, "inside a buffer or image section"));
    CHECK(fails("#pragma nyrexvj common\n#pragma nyrexvj channel 0 audio\n#pragma nyrexvj image\n" + toy, "inside a buffer or image section"));
    CHECK(fails("#pragma nyrexvj channel 4 audio\n" + toy, "0, 1, 2 or 3"));
    CHECK(fails("#pragma nyrexvj channel -1 audio\n" + toy, "0, 1, 2 or 3"));
    CHECK(fails("#pragma nyrexvj channel 1.5 audio\n" + toy, "0, 1, 2 or 3"));
    CHECK(fails("#pragma nyrexvj channel x audio\n" + toy, "0, 1, 2 or 3"));
    CHECK(fails("#pragma nyrexvj channel 1\n" + toy, "index and a source"));
    CHECK(fails("#pragma nyrexvj channel 1 camera\n" + toy, "unknown channel source"));
    CHECK(fails("#pragma nyrexvj channel 1 buffer\n" + toy, "letter from a to d"));
    CHECK(fails("#pragma nyrexvj channel 1 buffer z\n" + toy, "letter from a to d"));
    CHECK(fails("#pragma nyrexvj channel 1 media\n" + toy, "name of an image or video"));
    CHECK(fails("#pragma nyrexvj channel 1 media \"\"\n" + toy, "name of an image or video"));
    CHECK(fails("#pragma nyrexvj channel 1 audio mirror\n" + toy, "unknown channel option 'mirror'"));
    CHECK(fails("#pragma nyrexvj channel 0 buffer c\n" + toy, "no such buffer"));
    CHECK(fails("#pragma nyrexvj buffer a\nfloat x;\n#pragma nyrexvj image\n" + toy, "Buffer A needs a void main()"));
    CHECK(fails("#pragma nyrexvj buffer a\n" + toy + "#pragma nyrexvj image\nvoid main() { fragColor = vec4(1.0); }\n", "same style"));
    CHECK(fails("#pragma nyrexvj alpha\nfloat x;\n", "Image needs a void main()"));

    std::string many = "#pragma nyrexvj buffer a\n";
    for (int i = 0; i < 4; ++i) {
        many += "#pragma nyrexvj channel " + std::to_string(i) + " media m" + std::to_string(i) + "\n";
    }
    many += toy + "#pragma nyrexvj image\n#pragma nyrexvj channel 0 media m4\n";
    CHECK(fails(many + toy, "at most 4 different"));

    std::string ok4 = "#pragma nyrexvj image\n";
    for (int i = 0; i < 4; ++i) {
        ok4 += "#pragma nyrexvj channel " + std::to_string(i) + " media m" + std::to_string(i) + "\n";
    }
    CHECK(nvj::prepareShaderSource(ok4 + toy).ok);

    std::string all = "";
    for (int b = 0; b < nvj::kMaxShaderBuffers; ++b) {
        all += std::string("#pragma nyrexvj buffer ") + static_cast<char>('a' + b) + "\n" + toy;
    }
    all += "#pragma nyrexvj image\n" + toy;
    const nvj::ShaderSource full = nvj::prepareShaderSource(all);
    CHECK(full.ok && full.plan && full.plan->passes.size() == 5 && full.plan->bufferCount() == 4);
}

void testPassSource() {
    const nvj::ShaderSource parsed = nvj::prepareShaderSource(
        "#pragma nyrexvj alpha\n"
        "float shared() { return 1.0; }\n"
        "#pragma nyrexvj buffer a\n"
        "void mainImage(out vec4 c, in vec2 p) { c = vec4(shared()); }\n"
        "#pragma nyrexvj image\n"
        "#pragma nyrexvj channel 0 buffer a\n"
        "void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel0, p / iResolution.xy); }\n");
    CHECK(parsed.ok);
    nvj::ClipDef def;
    def.name = "Shader: multi";
    def.kind = nvj::ClipKind::ShaderFile;
    def.shadertoy = parsed.shadertoy;
    def.body = parsed.body;
    def.plan = parsed.plan;
    CHECK(def.passCount() == 2);
    const std::string buffer = nvj::clipPassFragmentSource(def, 0);
    const std::string image = nvj::clipPassFragmentSource(def, 1);
    CHECK(buffer.rfind("#version 330 core", 0) == 0);
    CHECK(image.rfind("#version 330 core", 0) == 0);
    CHECK(buffer.find("uChannel0") != std::string::npos);
    CHECK(buffer.find("#define iFrame uFrame") != std::string::npos);
    CHECK(buffer.find("int(uTime * 60.0)") == std::string::npos);
    CHECK(buffer.find("#line 1\n\nfloat shared()") != std::string::npos);
    CHECK(buffer.find("#line 4\nvoid mainImage") != std::string::npos);
    CHECK(image.find("#line 6\n\nvoid mainImage") != std::string::npos);
    CHECK(buffer.find("fragColor = shaderColor;") != std::string::npos);
    CHECK(image.find("shaderColor.rgb * alphaValue") != std::string::npos);
    CHECK(buffer.find("nyrexvj") == std::string::npos);
    CHECK(image.find("nyrexvj") == std::string::npos);
    CHECK(image.find("vec4(shaderColor.rgb, 1.0)") == std::string::npos);

    nvj::ShaderPlan variant = *parsed.plan;
    variant.alpha = false;
    nvj::ClipDef plain = def;
    plain.plan = std::make_shared<const nvj::ShaderPlan>(variant);
    CHECK(nvj::clipPassFragmentSource(plain, 1).find("vec4(shaderColor.rgb, 1.0)") != std::string::npos);
    variant.alpha = true;
    variant.premultiplied = true;
    plain.plan = std::make_shared<const nvj::ShaderPlan>(variant);
    const std::string pre = nvj::clipPassFragmentSource(plain, 1);
    CHECK(pre.find("fragColor = vec4(shaderColor.rgb, alphaValue);") != std::string::npos);

    const nvj::ShaderSource native = nvj::prepareShaderSource("#pragma nyrexvj alpha\nvoid main() { fragColor = vec4(duo(0.5), 0.5); }\n");
    CHECK(native.ok && !native.shadertoy);
    nvj::ClipDef nativeDef;
    nativeDef.name = "Shader: native";
    nativeDef.kind = nvj::ClipKind::ShaderFile;
    nativeDef.body = native.body;
    nativeDef.plan = native.plan;
    const std::string nativeSource = nvj::clipPassFragmentSource(nativeDef, 0);
    CHECK(nativeSource.find("float hash21") != std::string::npos);
    CHECK(nativeSource.find("#define main nyrexUserMain") != std::string::npos);
    CHECK(nativeSource.find("nyrexUserMain();") != std::string::npos);
    CHECK(nativeSource.find("uChannelRes") != std::string::npos);

    CHECK(nvj::clipFragmentSource(nativeDef).find("nyrexUserMain") != std::string::npos);
    nvj::ClipDef legacy;
    legacy.name = "Shader: legacy";
    legacy.kind = nvj::ClipKind::ShaderFile;
    legacy.body = "void main() { fragColor = vec4(1.0); }";
    CHECK(legacy.passCount() == 1);
    CHECK(nvj::clipFragmentSource(legacy).find("uChannel0") == std::string::npos);
}

unsigned long long fnv(const std::string& text) {
    unsigned long long hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

void testLegacySourceUnchanged() {
    nvj::ClipDef toy;
    toy.name = "Shader: toy";
    toy.kind = nvj::ClipKind::ShaderFile;
    toy.shadertoy = true;
    toy.body = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }";
    CHECK(fnv(nvj::clipFragmentSource(toy)) == 0x2b08ef1efb1a9f4aull);

    nvj::ClipDef native;
    native.name = "Shader: native";
    native.kind = nvj::ClipKind::ShaderFile;
    native.body = "void main() { fragColor = vec4(duo(0.5), 1.0); }";
    CHECK(fnv(nvj::clipFragmentSource(native)) == 0x3a822131c53b2f0cull);

    nvj::ClipDef builtin;
    builtin.name = "Builtin";
    builtin.body = "void main() { fragColor = vec4(duo(0.5), 1.0); }";
    CHECK(fnv(nvj::clipFragmentSource(builtin)) == 0x45523cd7551459e2ull);

    const nvj::ShaderSource prepared = nvj::prepareShaderSource("void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n// #pragma nyrexvj alpha\n");
    CHECK(prepared.ok && !prepared.advanced && prepared.plan == nullptr);
}

void testFitScale() {
    const unsigned long long full = 1920ull * 1080ull * 8ull * 2ull;
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, full) == 1.0f);
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, full - 1) == 0.75f);
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, 10ull * 1024ull * 1024ull) == 0.5f);
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, 5ull * 1024ull * 1024ull) == 0.35f);
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, 2ull * 1024ull * 1024ull) == 0.25f);
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, 100) == 0.0f);
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, 0) == 0.0f);
    CHECK(nvj::fitShaderScale(0.5f, 1920, 1080, 8, 2, full) == 0.5f);
    CHECK(nvj::fitShaderScale(0.5f, 1920, 1080, 8, 2, 1920ull * 1080ull * 8ull * 2ull / 4ull - 1) == 0.35f);
    CHECK(nvj::fitShaderScale(0.25f, 1920, 1080, 8, 2, 1000) == 0.0f);
    CHECK(nvj::fitShaderScale(5.0f, 64, 64, 8, 2, 1ull << 30) == 1.0f);
    CHECK(nvj::fitShaderScale(0.0f, 64, 64, 8, 2, 1ull << 30) == 0.25f);
    CHECK(nvj::fitShaderScale(1.0f, 1, 1, 4, 1, 4) == 1.0f);
    CHECK(nvj::fitShaderScale(1.0f, 1, 1, 4, 1, 3) == 0.0f);
    const unsigned long long budget = nvj::kShaderBufferBudgetBytes;
    CHECK(nvj::fitShaderScale(1.0f, 1920, 1080, 8, 2, budget) == 1.0f);
    const unsigned long long fourK = 3840ull * 2160ull * 8ull * 2ull;
    CHECK(nvj::fitShaderScale(1.0f, 3840, 2160, 8, 2, budget) == 1.0f);
    CHECK(nvj::fitShaderScale(1.0f, 3840, 2160, 8, 2, budget - 4 * fourK) == 0.0f);
    CHECK(nvj::fitShaderScale(1.0f, 3840, 2160, 8, 2, budget - 3 * fourK) == 1.0f);
    CHECK(nvj::fitShaderScale(1.0f, 3840, 2160, 8, 2, budget - 5 * fourK / 2) == 1.0f);
    CHECK(nvj::fitShaderScale(1.0f, 3840, 2160, 8, 2, fourK - 1) == 0.75f);
}

void testLibrary() {
    const fs::path root = fs::temp_directory_path() / "nyrexvj_shader_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "shaders", ec);
    setDataDir(root.string());

    const size_t builtin = nvj::clipLibrary().size();
    writeFile(root / "shaders" / "good.glsl", "void main() { fragColor = vec4(pal(uTime), 1.0); }\n");
    writeFile(root / "shaders" / "toy.frag", "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.5); }\n");
    writeFile(root / "shaders" / "bad.glsl", "float x = 1.0;\n");
    writeFile(root / "shaders" / "huge.glsl", std::string(nvj::kMaxShaderFileBytes + 10, 'a'));
    writeFile(root / "shaders" / "notes.txt", "void main() {}\n");

    CHECK(nvj::shaderDirectory() == (root / "shaders").string());
    CHECK(nvj::rescanLibrary() == 4);
    CHECK(nvj::clipLibrary().size() == builtin + 4);
    CHECK(nvj::rescanLibrary() == 0);

    const nvj::ClipDef* good = findClip("Shader: good");
    CHECK(good != nullptr);
    if (good) {
        CHECK(good->kind == nvj::ClipKind::ShaderFile);
        CHECK(good->isShaderFile());
        CHECK(good->error.empty());
        CHECK(!good->shadertoy);
        CHECK(!good->body.empty());
        CHECK(!good->path.empty());
    }
    const nvj::ClipDef* toy = findClip("shader: TOY");
    CHECK(toy != nullptr && toy->shadertoy && toy->error.empty());
    const nvj::ClipDef* bad = findClip("Shader: bad");
    CHECK(bad != nullptr && !bad->error.empty() && bad->body.empty());
    const nvj::ClipDef* huge = findClip("Shader: huge");
    CHECK(huge != nullptr && huge->error == "The shader file is larger than 256 KiB");
    CHECK(findClip("Shader: notes") == nullptr);

    nvj::ShaderScan quiet = nvj::refreshShaderClips();
    CHECK(quiet.changed.empty());
    CHECK(quiet.added == 0);

    const int goodIndex = nvj::clipIndexByName("Shader: good");
    writeFile(root / "shaders" / "good.glsl", "void main() { fragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n");
    fs::last_write_time(root / "shaders" / "good.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(30), ec);
    writeFile(root / "shaders" / "fresh.glsl", "void main() { fragColor = vec4(0.0); }\n");
    nvj::ShaderScan changed = nvj::refreshShaderClips();
    CHECK(changed.added == 1);
    CHECK(changed.changed.size() == 1);
    if (!changed.changed.empty()) {
        CHECK(static_cast<int>(changed.changed[0]) == goodIndex);
    }
    const nvj::ClipDef* updated = findClip("Shader: good");
    CHECK(updated != nullptr && updated->body.find("1.0, 0.0, 0.0") != std::string::npos);
    CHECK(findClip("Shader: fresh") != nullptr);

    writeFile(root / "shaders" / "bad.glsl", "void main() { fragColor = vec4(0.0); }\n");
    fs::last_write_time(root / "shaders" / "bad.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(60), ec);
    nvj::ShaderScan fixed = nvj::refreshShaderClips();
    CHECK(fixed.changed.size() == 1);
    const nvj::ClipDef* nowGood = findClip("Shader: bad");
    CHECK(nowGood != nullptr && nowGood->error.empty() && !nowGood->body.empty());

    fs::remove(root / "shaders" / "toy.frag", ec);
    nvj::ShaderScan gone = nvj::refreshShaderClips();
    CHECK(gone.changed.size() == 1);
    const nvj::ClipDef* missing = findClip("Shader: toy");
    CHECK(missing != nullptr && missing->error == "The shader file is gone");
    nvj::ShaderScan again = nvj::refreshShaderClips();
    CHECK(again.changed.empty());

    const std::string first = nvj::createExampleShader();
    CHECK(first == "example.glsl");
    const std::string second = nvj::createExampleShader();
    CHECK(second == "example (2).glsl");
    CHECK(fs::exists(root / "shaders" / "example.glsl", ec));
    std::ifstream stream(root / "shaders" / "example.glsl", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const nvj::ShaderSource example = nvj::prepareShaderSource(text);
    CHECK(example.ok);
    CHECK(!example.shadertoy);
}

void testImport() {
    const fs::path root = fs::temp_directory_path() / "nyrexvj_shader_test";
    const fs::path source = fs::temp_directory_path() / "nyrexvj_shader_import";
    std::error_code ec;
    fs::remove_all(source, ec);
    fs::create_directories(source, ec);
    writeFile(source / "dropped.fs", "void main() { fragColor = vec4(0.2); }\n");
    const nvj::ImportResult result = nvj::importAsset((source / "dropped.fs").string());
    CHECK(result.kind == nvj::ImportKind::Shader);
    CHECK(result.added);
    CHECK(fs::exists(root / "shaders" / "dropped.fs", ec));
    const nvj::ImportResult same = nvj::importAsset((source / "dropped.fs").string());
    CHECK(same.kind == nvj::ImportKind::Shader);
    CHECK(!same.added);

    writeFile(source / "big.glsl", std::string(nvj::kMaxShaderFileBytes + 1, 'x'));
    CHECK(nvj::importAsset((source / "big.glsl").string()).kind == nvj::ImportKind::TooLarge);
    fs::remove_all(source, ec);
    fs::remove_all(root, ec);
}

}

int main() {
    testNames();
    testPrepare();
    testAdvancedParse();
    testPassSource();
    testLegacySourceUnchanged();
    testFitScale();
    testLibrary();
    testFragmentSource();
    testImport();
    return testing::finish();
}

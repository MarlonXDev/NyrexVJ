#include "check.hpp"

#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/shader_source.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    testLibrary();
    testFragmentSource();
    testImport();
    return testing::finish();
}

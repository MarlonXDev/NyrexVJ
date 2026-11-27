#include "check.hpp"

#include "core/composition.hpp"
#include "core/preset.hpp"
#include "render/clip_library.hpp"
#include "render/isf.hpp"
#include "render/shader_source.hpp"
#include "render/transition_library.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

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

std::string readExample(const std::string& name) {
    std::ifstream in(std::string(NVJ_EXAMPLES) + "/" + name, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const std::string kBody = "void main() {\n    fragColor = mix(colFrom(vUv), colTo(vUv), uProgress);\n}\n";

void testPragma() {
    const nvj::ShaderSource plain = nvj::prepareShaderSource("#pragma nyrexvj transition\n" + kBody);
    CHECK(plain.ok);
    CHECK(plain.plan != nullptr);
    if (plain.plan) {
        CHECK(plain.plan->transition);
        CHECK(!plain.plan->filter);
        CHECK(plain.plan->passes.size() == 1);
        if (!plain.plan->passes.empty()) {
            const nvj::ShaderPassSource& pass = plain.plan->passes[0];
            CHECK(pass.image);
            CHECK(pass.inputs[0].kind == nvj::ShaderInputKind::From);
            CHECK(pass.inputs[1].kind == nvj::ShaderInputKind::To);
            CHECK(pass.inputs[2].kind == nvj::ShaderInputKind::None);
        }
    }
    const std::string source = plain.plan ? nvj::planPassFragmentSource(*plain.plan, plain.shadertoy, 0) : std::string();
    CHECK(contains(source, "uniform float uProgress"));
    CHECK(contains(source, "vec4 colFrom"));
    CHECK(contains(source, "vec4 colTo"));
    CHECK(contains(source, "fragColor = vec4(fragColor.rgb, 1.0)"));

    const nvj::ShaderSource withParams = nvj::prepareShaderSource("#pragma nyrexvj transition\n#pragma nyrexvj param softness float \"Softness\" 0 0.5 0.15\n" + kBody);
    CHECK(withParams.ok && withParams.plan && withParams.plan->params.size() == 1);

    const nvj::ShaderSource ordinary = nvj::prepareShaderSource("void main() { fragColor = vec4(1.0); }\n");
    CHECK(ordinary.ok && !ordinary.plan);
}

void testRefusals() {
    struct Case {
        const char* name;
        std::string text;
        const char* expected;
    };
    const std::string head = "#pragma nyrexvj transition\n";
    const std::vector<Case> cases = {
        {"filter", head + "#pragma nyrexvj filter\n" + kBody, "transition"},
        {"alpha", head + "#pragma nyrexvj alpha\n" + kBody, "transition"},
        {"picture", head + "#pragma nyrexvj channel 0 picture\n" + kBody, "picture"},
        {"buffer", head + "#pragma nyrexvj buffer a\nvoid main() { fragColor = vec4(1.0); }\n#pragma nyrexvj image\n#pragma nyrexvj channel 0 buffer a\n" + kBody, "transition"},
        {"from without transition", "#pragma nyrexvj channel 0 from\n" + kBody, "nyrexvj transition"},
        {"to without transition", "#pragma nyrexvj channel 1 to\n" + kBody, "nyrexvj transition"},
        {"reserved name", head + "#pragma nyrexvj param uProgress float \"X\" 0 1 0\n" + kBody, "already used"},
        {"reserved from name", head + "#pragma nyrexvj param colFrom float \"X\" 0 1 0\n" + kBody, "already used"},
    };

    for (const Case& item : cases) {
        const nvj::ShaderSource source = nvj::prepareShaderSource(item.text);
        const bool refused = !source.ok && contains(source.error, item.expected);
        if (!refused) {
            std::fprintf(stderr, "case '%s': ok=%d error='%s'\n", item.name, source.ok ? 1 : 0, source.error.substr(0, 200).c_str());
        }
        CHECK(refused);
    }
}

void testIsf() {
    const nvj::ShaderSource slide = nvj::prepareShaderSource(readExample("isf_transition_slide.fs"));
    CHECK(slide.ok);
    CHECK(slide.plan != nullptr);
    if (slide.plan) {
        CHECK(slide.plan->transition);
        CHECK(!slide.plan->filter);
        CHECK(slide.plan->params.size() == 1);
        CHECK(slide.plan->passes.size() == 1);
        const std::string source = nvj::planPassFragmentSource(*slide.plan, slide.shadertoy, 0);
        CHECK(contains(source, "#define progress uProgress"));
        CHECK(contains(source, "#define startImage uChannel0"));
        CHECK(contains(source, "#define endImage uChannel1"));
    }
    const nvj::ShaderSource iris = nvj::prepareShaderSource(readExample("transition_iris.glsl"));
    CHECK(iris.ok && iris.plan && iris.plan->transition);
    CHECK(iris.plan && iris.plan->params.size() == 3);
}

void testLibrary() {
    const fs::path root = fs::temp_directory_path() / "nvj_transition_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    setDataDir(root.string());
    fs::create_directories(fs::path(nvj::shaderDirectory()), ec);
    const fs::path folder(nvj::shaderDirectory());

    CHECK(nvj::transitionLibrary().empty());
    nvj::TransitionScan none = nvj::refreshCustomTransitions();
    CHECK(none.added == 0);

    writeFile(folder / "wipe.glsl", "#pragma nyrexvj transition\n" + kBody);
    writeFile(folder / "slide.fs", readExample("isf_transition_slide.fs"));
    writeFile(folder / "ordinary.glsl", "void main() { fragColor = vec4(1.0); }\n");
    writeFile(folder / "tint.fs", readExample("isf_color_shift.fs"));
    writeFile(folder / "broken.glsl", "#pragma nyrexvj transition\n#pragma nyrexvj bogus\n" + kBody);

    const nvj::TransitionScan first = nvj::refreshCustomTransitions();
    CHECK(first.added == 3);
    CHECK(nvj::customTransitionIndex("Transition: wipe") >= 0);
    CHECK(nvj::customTransitionIndex("Transition: slide") >= 0);
    CHECK(nvj::customTransitionIndex("Transition: broken") >= 0);
    CHECK(nvj::customTransitionIndex("Transition: ordinary") < 0);
    CHECK(nvj::customTransitionIndex("Transition: tint") < 0);

    const int wipe = nvj::customTransitionIndex("Transition: wipe");
    const int broken = nvj::customTransitionIndex("Transition: broken");
    if (wipe >= 0 && broken >= 0) {
        CHECK(nvj::transitionLibrary()[static_cast<size_t>(wipe)].usable());
        CHECK(!nvj::transitionLibrary()[static_cast<size_t>(broken)].usable());
        CHECK(!nvj::transitionLibrary()[static_cast<size_t>(broken)].error.empty());
    }

    const nvj::TransitionScan idle = nvj::refreshCustomTransitions();
    CHECK(idle.added == 0 && idle.changed.empty());

    const int clipIndex = nvj::clipIndexByName("Transition: wipe");
    CHECK(clipIndex < 0);

    const unsigned long long before = wipe >= 0 ? nvj::transitionLibrary()[static_cast<size_t>(wipe)].version : 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeFile(folder / "wipe.glsl", "#pragma nyrexvj transition\n#pragma nyrexvj param amount float \"Amount\" 0 1 0.5\n" + kBody + "\n");
    const nvj::TransitionScan changed = nvj::refreshCustomTransitions();
    CHECK(changed.changed.size() == 1);
    if (wipe >= 0) {
        const nvj::TransitionDef& def = nvj::transitionLibrary()[static_cast<size_t>(wipe)];
        CHECK(def.version > before);
        CHECK(def.usable() && def.plan && def.plan->params.size() == 1);
    }

    writeFile(folder / "wipe.glsl", "#pragma nyrexvj transition\nvoid main() { broken(; }\n");
    nvj::refreshCustomTransitions();
    if (wipe >= 0) {
        CHECK(nvj::transitionLibrary()[static_cast<size_t>(wipe)].usable());
    }

    writeFile(folder / "wipe.glsl", "void main() { fragColor = vec4(1.0); }\n");
    nvj::refreshCustomTransitions();
    if (wipe >= 0) {
        CHECK(!nvj::transitionLibrary()[static_cast<size_t>(wipe)].usable());
        CHECK(contains(nvj::transitionLibrary()[static_cast<size_t>(wipe)].error, "not a transition"));
    }

    fs::remove(folder / "slide.fs", ec);
    nvj::refreshCustomTransitions();
    const int slide = nvj::customTransitionIndex("Transition: slide");
    CHECK(slide >= 0);
    if (slide >= 0) {
        CHECK(!nvj::transitionLibrary()[static_cast<size_t>(slide)].usable());
        CHECK(contains(nvj::transitionLibrary()[static_cast<size_t>(slide)].error, "gone"));
    }

    const std::string created = nvj::createExampleTransition();
    CHECK(!created.empty());
    const std::string second = nvj::createExampleTransition();
    CHECK(!second.empty() && second != created);
    nvj::refreshCustomTransitions();
    const nvj::ShaderSource example = nvj::prepareShaderSource(nvj::exampleTransitionText());
    CHECK(example.ok && example.plan && example.plan->transition && example.plan->params.size() == 2);

    fs::remove_all(root, ec);
}

void testLimits() {
    const fs::path root = fs::temp_directory_path() / "nvj_transition_limit";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    setDataDir(root.string());
    fs::create_directories(fs::path(nvj::shaderDirectory()), ec);
    const fs::path folder(nvj::shaderDirectory());
    const size_t existing = nvj::transitionLibrary().size();
    for (int i = 0; i < 40; ++i) {
        writeFile(folder / ("limit_" + std::to_string(100 + i) + ".glsl"), "#pragma nyrexvj transition\n" + kBody);
    }
    nvj::refreshCustomTransitions();
    CHECK(nvj::transitionLibrary().size() <= nvj::kMaxCustomTransitions);
    CHECK(nvj::transitionLibrary().size() >= existing);
    fs::remove_all(root, ec);
}

void testPersistence() {
    nvj::Composition comp;
    nvj::normalizeDecks(comp);
    const std::string plain = nvj::compositionToJson(comp);
    CHECK(!contains(plain, "customTransition"));
    CHECK(!contains(plain, "transitionValues"));

    comp.deckSettings.customTransition = "Transition: wipe";
    comp.deckSettings.transitionValues[0] = 0.25f;
    comp.deckSettings.transitionValues[2] = 0.75f;
    const std::string text = nvj::compositionToJson(comp);
    CHECK(contains(text, "customTransition"));
    CHECK(contains(text, "transitionValues"));

    nvj::Composition loaded;
    std::string error;
    CHECK(nvj::compositionFromJson(text, loaded, error));
    CHECK(loaded.deckSettings.customTransition == "Transition: wipe");
    CHECK(loaded.deckSettings.transitionValues[0] == 0.25f);
    CHECK(loaded.deckSettings.transitionValues[1] == nvj::kClipValueUnset);
    CHECK(loaded.deckSettings.transitionValues[2] == 0.75f);

    comp.deckSettings.transitionValues = nvj::unsetClipValues();
    const std::string noValues = nvj::compositionToJson(comp);
    CHECK(contains(noValues, "customTransition"));
    CHECK(!contains(noValues, "transitionValues"));

    nvj::Composition bad;
    bad.deckSettings.customTransition = "Shader: not a transition";
    nvj::normalizeDecks(bad);
    CHECK(bad.deckSettings.customTransition.empty());
    bad.deckSettings.customTransition = "Transition: " + std::string(200, 'x');
    nvj::normalizeDecks(bad);
    CHECK(bad.deckSettings.customTransition.empty());
    bad.deckSettings.customTransition = "Transition: ok";
    bad.deckSettings.transitionValues[3] = 1.0e30f;
    nvj::normalizeDecks(bad);
    CHECK(bad.deckSettings.customTransition == "Transition: ok");
    CHECK(bad.deckSettings.transitionValues[3] == nvj::kClipValueUnset);

    std::string hostile = text;
    const size_t at = hostile.find("Transition: wipe");
    if (at != std::string::npos) {
        hostile.replace(at, 16, "Evil: wipe");
    }
    nvj::Composition rejected;
    CHECK(nvj::compositionFromJson(hostile, rejected, error));
    CHECK(rejected.deckSettings.customTransition.empty());
}

}

int main() {
    testPragma();
    testRefusals();
    testIsf();
    testLibrary();
    testLimits();
    testPersistence();
    return testing::finish();
}

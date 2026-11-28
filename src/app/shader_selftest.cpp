#include "app/shader_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "render/clip_library.hpp"
#include "core/color_lut.hpp"
#include "core/render_policy.hpp"
#include "render/effect_library.hpp"
#include "render/transition_library.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

void setDataDirectory(const std::string& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void clearDataDirectory() {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", "");
#else
    unsetenv("NYREXVJ_DATA_DIR");
#endif
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

const char* kBlue = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 0.0, 1.0, 1.0); }\n";
const char* kLegacyZeroAlpha = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 0.0); }\n";
const char* kLegacyNative = "void main() { fragColor = vec4(0.0, 1.0, 0.0, 0.25); }\n";
const char* kAlphaStraight = "#pragma nyrexvj alpha\nvoid mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 0.5); }\n";
const char* kAlphaPremultiplied = "#pragma nyrexvj alpha premultiplied\nvoid mainImage(out vec4 c, in vec2 p) { c = vec4(0.5, 0.0, 0.0, 0.5); }\n";
const char* kAlphaNative = "#pragma nyrexvj alpha\nvoid main() { fragColor = vec4(1.0, 0.0, 0.0, 0.5); }\n";
const char* kAlphaZero = "#pragma nyrexvj alpha\nvoid mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 0.0); }\n";

const char* kAccumulate =
    "#pragma nyrexvj buffer a\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel0, p / iResolution.xy) + vec4(0.1, 0.0, 0.0, 0.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, p / iResolution.xy).r, 0.0, 0.0, 1.0); }\n";

const char* kOrder =
    "#pragma nyrexvj buffer a\n"
    "#pragma nyrexvj channel 0 buffer b\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, p / iResolution.xy).r + 0.1, 0.0, 0.0, 1.0); }\n"
    "#pragma nyrexvj buffer b\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, p / iResolution.xy).r + 0.1, 0.0, 0.0, 1.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer b\n"
    "#pragma nyrexvj channel 1 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(texture(iChannel0, uv).r, texture(iChannel1, uv).r, 0.0, 1.0); }\n";

const char* kFrame =
    "#pragma nyrexvj image\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(float(iFrame) * 0.1, iTimeDelta * 10.0, 0.0, 1.0); }\n";

const char* kScale =
    "#pragma nyrexvj buffer a scale 0.5\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(iChannelResolution[0].x / iResolution.x, iChannelResolution[0].y / iResolution.y, 0.0, 1.0); }\n";

const char* kRepeat =
    "#pragma nyrexvj buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(p.x / iResolution.x, 0.0, 0.0, 1.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a repeat\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, vec2(1.25, 0.5)).r, 0.0, 0.0, 1.0); }\n";

const char* kClamp =
    "#pragma nyrexvj buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(p.x / iResolution.x, 0.0, 0.0, 1.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a clamp\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, vec2(1.25, 0.5)).r, 0.0, 0.0, 1.0); }\n";

const char* kNearest =
    "#pragma nyrexvj buffer a scale 0.25\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(mod(floor(p.x), 2.0), 0.0, 0.0, 1.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a nearest\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, vec2(121.25 / 240.0, 0.5)).r, 0.0, 0.0, 1.0); }\n";

const char* kLinear =
    "#pragma nyrexvj buffer a scale 0.25\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(mod(floor(p.x), 2.0), 0.0, 0.0, 1.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a linear\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, vec2(121.25 / 240.0, 0.5)).r, 0.0, 0.0, 1.0); }\n";

const char* kMedia =
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 1 media \"Image: split\"\n"
    "void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel1, p / iResolution.xy); }\n";

const char* kBroken =
    "#pragma nyrexvj buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n"
    "#pragma nyrexvj buffer b\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0) +; }\n"
    "#pragma nyrexvj image\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n";

const char* kReloadedPlain = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 1.0, 0.0, 1.0); }\n";

const char* kFourBuffers =
    "#pragma nyrexvj buffer a\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = texture(iChannel0, uv + vec2(0.001)) * 0.98 + vec4(0.02 * step(0.5, fract(uv.x * 8.0 + iTime)), 0.0, 0.0, 0.0); }\n"
    "#pragma nyrexvj buffer b\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "#pragma nyrexvj channel 1 buffer b\n"
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = mix(texture(iChannel1, uv), texture(iChannel0, uv), 0.1); }\n"
    "#pragma nyrexvj buffer c\n"
    "#pragma nyrexvj channel 0 buffer b\n"
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = texture(iChannel0, uv).gbra; }\n"
    "#pragma nyrexvj buffer d\n"
    "#pragma nyrexvj channel 0 buffer c\n"
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = texture(iChannel0, uv * 0.99 + 0.005); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer d\n"
    "#pragma nyrexvj channel 1 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(texture(iChannel0, uv).rgb + texture(iChannel1, uv).rgb, 1.0); }\n";

const char* kSinglePassBench = "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0); }\n";

const char* kIsfSolid =
    "/*{\"DESCRIPTION\":\"solid\",\"INPUTS\":["
    "{\"NAME\":\"level\",\"TYPE\":\"float\",\"MIN\":0.0,\"MAX\":1.0,\"DEFAULT\":0.5},"
    "{\"NAME\":\"tint\",\"TYPE\":\"color\",\"DEFAULT\":[1.0,0.5,0.0,1.0]},"
    "{\"NAME\":\"flip\",\"TYPE\":\"bool\",\"DEFAULT\":false},"
    "{\"NAME\":\"which\",\"TYPE\":\"long\",\"VALUES\":[0,1,2],\"LABELS\":[\"a\",\"b\",\"c\"],\"DEFAULT\":0}]}*/\n"
    "void main() { vec3 c = tint.rgb * level; if (flip) { c = c.bgr; } if (which == 2) { c = vec3(0.0, 0.0, 1.0); } gl_FragColor = vec4(c, 1.0); }\n";

const char* kIsfSolidReloaded =
    "/*{\"INPUTS\":[{\"NAME\":\"level\",\"TYPE\":\"float\",\"MIN\":0.0,\"MAX\":1.0,\"DEFAULT\":1.0}]}*/\n"
    "void main() { gl_FragColor = vec4(0.0, level, 0.0, 1.0); }\n";

const char* kIsfBroken =
    "/*{\"INPUTS\":[]}*/\n"
    "void main() {\n"
    "    gl_FragColor = vec4(1.0) +;\n"
    "}\n";

const char* kIsfLegacy =
    "/*{\"INPUTS\":[]}*/\n"
    "varying vec2 uv;\n"
    "void main() { gl_FragColor = vec4(uv, 0.0, 1.0); }\n";

const char* kIsfAccumulate =
    "/*{\"INPUTS\":[],\"PASSES\":[{\"TARGET\":\"acc\",\"PERSISTENT\":true,\"WIDTH\":\"$WIDTH/2\",\"HEIGHT\":\"$HEIGHT/2\"},{}]}*/\n"
    "void main() { if (PASSINDEX == 0) { gl_FragColor = IMG_THIS_NORM_PIXEL(acc) + vec4(0.1, 0.0, 0.0, 0.0); } else { gl_FragColor = vec4(IMG_THIS_NORM_PIXEL(acc).r, 0.0, 0.0, 1.0); } }\n";

const char* kIsfAccumulateFull =
    "/*{\"INPUTS\":[],\"PASSES\":[{\"TARGET\":\"acc\",\"PERSISTENT\":true},{}]}*/\n"
    "void main() { if (PASSINDEX == 0) { gl_FragColor = IMG_THIS_NORM_PIXEL(acc) + vec4(0.1, 0.0, 0.0, 0.0); } else { gl_FragColor = vec4(IMG_THIS_NORM_PIXEL(acc).r, 0.0, 0.0, 1.0); } }\n";

const char* kIsfImported =
    "/*{\"INPUTS\":[],\"IMPORTED\":{\"pic\":{\"PATH\":\"split.png\"}}}*/\n"
    "void main() { gl_FragColor = IMG_THIS_NORM_PIXEL(pic); }\n";

const char* kIsfSwap =
    "/*{\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"},{\"NAME\":\"amount\",\"TYPE\":\"float\",\"MIN\":0.0,\"MAX\":1.0,\"DEFAULT\":0.5}]}*/\n"
    "void main() { vec4 c = IMG_THIS_NORM_PIXEL(inputImage); gl_FragColor = vec4(mix(c.rgb, c.bgr, amount), c.a); }\n";

const char* kIsfSwapReloaded =
    "/*{\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"}]}*/\n"
    "void main() { vec4 c = IMG_THIS_NORM_PIXEL(inputImage); gl_FragColor = vec4(c.g, c.r, c.b, c.a); }\n";

const char* kIsfFilterBroken =
    "/*{\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"}]}*/\n"
    "void main() {\n"
    "    gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage) +;\n"
    "}\n";

const char* kIsfBlur =
    "/*{\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"},{\"NAME\":\"radius\",\"TYPE\":\"float\",\"MIN\":0.0,\"MAX\":8.0,\"DEFAULT\":3.0}],"
    "\"PASSES\":[{\"TARGET\":\"sideways\",\"WIDTH\":\"$WIDTH/2\",\"HEIGHT\":\"$HEIGHT/2\"},{}]}*/\n"
    "void main() {\n"
    "    vec2 px = 1.0 / RENDERSIZE;\n"
    "    vec4 sum = vec4(0.0);\n"
    "    float total = 0.0;\n"
    "    for (int i = -6; i <= 6; ++i) {\n"
    "        float w = exp(-float(i * i) / 18.0);\n"
    "        float d = float(i) * radius;\n"
    "        if (PASSINDEX == 0) { sum += IMG_NORM_PIXEL(inputImage, isf_FragNormCoord + vec2(d * px.x, 0.0)) * w; }\n"
    "        else { sum += IMG_NORM_PIXEL(sideways, isf_FragNormCoord + vec2(0.0, d * px.y)) * w; }\n"
    "        total += w;\n"
    "    }\n"
    "    gl_FragColor = sum / total;\n"
    "}\n";

const char* kIsfTrail =
    "/*{\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"}],\"PASSES\":[{\"TARGET\":\"trail\",\"PERSISTENT\":true},{}]}*/\n"
    "void main() { if (PASSINDEX == 0) { gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage) * 0.5 + IMG_THIS_NORM_PIXEL(trail) * 0.5; } else { gl_FragColor = IMG_THIS_NORM_PIXEL(trail); } }\n";

const char* kRed = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 1.0); }\n";
const char* kSplitRedBlue = "void mainImage(out vec4 c, in vec2 p) { c = p.x < iResolution.x * 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0); }\n";

class Runner {
public:
    Runner(Engine& engine, json& report) : engine_(engine), report_(report) {}

    bool prepare() {
        root_ = fs::temp_directory_path() / "nyrexvj_shader_selftest";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "shaders", ec);
        fs::create_directories(root_ / "media", ec);
        setDataDirectory(root_.string());
        engine_.comp = Composition{};
        engine_.blackout = false;
        engine_.paused = false;
        engine_.setResolutionIndex(0);
        return true;
    }

    void finish() {
        clearDataDirectory();
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    int add(const std::string& stem, const std::string& text) {
        writeText(root_ / "shaders" / (stem + ".glsl"), text);
        rescanLibrary();
        engine_.syncLibrary();
        return clipIndexByName(shaderClipName(stem));
    }

    int addFile(const std::string& fileName, const std::string& text) {
        writeText(root_ / "shaders" / fileName, text);
        rescanLibrary();
        engine_.syncLibrary();
        return clipIndexByName(shaderClipName(fs::path(fileName).stem().string()));
    }

    int addFilter(const std::string& stem, const std::string& text) {
        writeText(root_ / "shaders" / (stem + ".fs"), text);
        rescanLibrary();
        engine_.syncLibrary();
        refreshIsfEffects();
        engine_.syncEffects();
        return effectIndexByName(isfEffectName(stem));
    }

    std::string addTransition(const std::string& stem, const std::string& text) {
        writeText(root_ / "shaders" / (stem + ".glsl"), text);
        rescanLibrary();
        engine_.syncLibrary();
        refreshCustomTransitions();
        return transitionClipName(stem);
    }

    void useEffect(int layer, int effect) {
        setEffect(engine_.comp.layers[static_cast<size_t>(layer)].fx[0], effect);
    }

    int lit(int minimumSum) {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        int count = 0;
        if (engine_.readOutput(pixels, width, height)) {
            for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
                count += static_cast<int>(pixels[i]) + static_cast<int>(pixels[i + 1]) + static_cast<int>(pixels[i + 2]) >= minimumSum ? 1 : 0;
            }
        }
        return count;
    }

    void step(int count) {
        stepWith(count, 1.0 / 60.0);
    }

    void stepWith(int count, double dt) {
        for (int i = 0; i < count; ++i) {
            engine_.setAudio(virtualAudio(engine_.beat(), engine_.time()));
            engine_.update(dt);
            engine_.render();
            glFinish();
        }
    }

    void clearAll() {
        engine_.comp = Composition{};
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    void place(int layer, int clip) {
        engine_.comp.layers[static_cast<size_t>(layer)].cells[0] = clip;
        engine_.trigger(layer, 0);
    }

    Pixel centre() {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        Pixel result;
        if (engine_.readOutput(pixels, width, height) && width > 0 && height > 0) {
            result = at(pixels, width, height, width / 2, height / 2);
        }
        return result;
    }

    Pixel sample(double fx, double fy) {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        Pixel result;
        if (engine_.readOutput(pixels, width, height) && width > 0 && height > 0) {
            result = at(pixels, width, height, static_cast<int>(fx * width), static_cast<int>(fy * height));
        }
        return result;
    }

    bool near(const Pixel& pixel, int r, int g, int b, int tolerance) const {
        return std::abs(pixel.r - r) <= tolerance && std::abs(pixel.g - g) <= tolerance && std::abs(pixel.b - b) <= tolerance;
    }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Shader self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void expect(const std::string& name, const Pixel& pixel, int r, int g, int b, int tolerance) {
        const std::string detail = "got (" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + ") expected (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ") +-" + std::to_string(tolerance);
        record(name, near(pixel, r, g, b, tolerance), detail);
    }

    int failed() const { return failed_; }
    json& items() { return items_; }
    const fs::path& root() const { return root_; }
    Engine& engine() { return engine_; }

private:
    static Pixel at(const std::vector<unsigned char>& pixels, int width, int height, int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
        Pixel pixel;
        pixel.r = pixels[index];
        pixel.g = pixels[index + 1];
        pixel.b = pixels[index + 2];
        pixel.a = pixels[index + 3];
        return pixel;
    }

    Engine& engine_;
    json& report_;
    fs::path root_;
    json items_ = json::array();
    int failed_ = 0;
};

void testAlpha(Runner& run) {
    const int blue = run.add("blue", kBlue);
    const int legacy = run.add("legacy_zero", kLegacyZeroAlpha);
    const int legacyNative = run.add("legacy_native", kLegacyNative);
    const int straight = run.add("alpha_straight", kAlphaStraight);
    const int premultiplied = run.add("alpha_premultiplied", kAlphaPremultiplied);
    const int native = run.add("alpha_native", kAlphaNative);
    const int zero = run.add("alpha_zero", kAlphaZero);
    run.record("clips found", blue >= 0 && legacy >= 0 && legacyNative >= 0 && straight >= 0 && premultiplied >= 0 && native >= 0 && zero >= 0, "all alpha test shaders are in the library");

    run.clearAll();
    run.place(0, legacy);
    run.step(3);
    run.expect("old shader keeps alpha 1", run.centre(), 255, 0, 0, 2);

    run.clearAll();
    run.place(0, blue);
    run.place(1, legacyNative);
    run.step(3);
    run.expect("old native shader: its alpha is passed through unchanged as premultiplied", run.centre(), 0, 255, 191, 3);

    run.clearAll();
    run.place(0, blue);
    run.place(1, straight);
    run.step(3);
    run.expect("alpha 0.5 over blue (straight)", run.centre(), 128, 0, 128, 3);

    run.clearAll();
    run.place(0, blue);
    run.place(1, premultiplied);
    run.step(3);
    run.expect("alpha 0.5 over blue (premultiplied)", run.centre(), 128, 0, 128, 3);

    run.clearAll();
    run.place(0, blue);
    run.place(1, native);
    run.step(3);
    run.expect("alpha 0.5 over blue (native style)", run.centre(), 128, 0, 128, 3);

    run.clearAll();
    run.place(0, blue);
    run.place(1, zero);
    run.step(3);
    run.expect("alpha 0 shows the layer below", run.centre(), 0, 0, 255, 2);

    run.clearAll();
    run.place(0, straight);
    run.step(3);
    const Pixel alone = run.centre();
    run.expect("alpha 0.5 alone on black", alone, 128, 0, 0, 3);
}

void testPasses(Runner& run) {
    const int accumulate = run.add("accumulate", kAccumulate);
    run.clearAll();
    run.place(0, accumulate);
    run.step(1);
    run.expect("buffer feedback after 1 frame", run.centre(), 26, 0, 0, 3);
    run.step(2);
    run.expect("buffer feedback after 3 frames", run.centre(), 77, 0, 0, 3);
    run.step(2);
    run.expect("buffer feedback after 5 frames", run.centre(), 128, 0, 0, 4);
    const size_t held = run.engine().shaderBufferBytes();
    run.record("buffer memory is counted", held > 0 && run.engine().gpuMemory().shaderBuffers == held, std::to_string(held) + " bytes");
    run.engine().trigger(0, 0);
    run.step(1);
    run.expect("trigger restarts the buffers", run.centre(), 26, 0, 0, 3);

    const int order = run.add("order", kOrder);
    run.clearAll();
    run.place(0, order);
    run.step(3);
    run.expect("buffer order (earlier buffer current frame, later buffer previous frame)", run.centre(), 153, 128, 0, 4);

    const int frame = run.add("frame", kFrame);
    run.clearAll();
    run.place(0, frame);
    run.step(6);
    const Pixel counter = run.centre();
    run.expect("iFrame counts rendered frames and iTimeDelta is the clock step", counter, 128, 42, 0, 5);

    const int scale = run.add("scale", kScale);
    run.clearAll();
    run.place(0, scale);
    run.step(2);
    run.expect("buffer scale 0.5", run.centre(), 128, 128, 0, 3);

    run.clearAll();
    run.place(0, run.add("repeat", kRepeat));
    run.step(2);
    run.expect("channel repeat wraps", run.centre(), 64, 0, 0, 4);

    run.clearAll();
    run.place(0, run.add("clamp", kClamp));
    run.step(2);
    run.expect("channel clamp holds the edge", run.centre(), 255, 0, 0, 3);

    run.clearAll();
    run.place(0, run.add("nearest", kNearest));
    run.step(2);
    run.expect("channel nearest", run.centre(), 255, 0, 0, 3);

    run.clearAll();
    run.place(0, run.add("linear", kLinear));
    run.step(2);
    run.expect("channel linear", run.centre(), 191, 0, 0, 5);
}

void testMedia(Runner& run) {
    std::vector<unsigned char> pixels(16u * 9u * 3u);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 16; ++x) {
            unsigned char* p = &pixels[(static_cast<size_t>(y) * 16u + static_cast<size_t>(x)) * 3u];
            p[0] = x < 8 ? 255 : 0;
            p[1] = x < 8 ? 0 : 255;
            p[2] = 0;
        }
    }
    const std::string path = (run.root() / "media" / "split.png").string();
    const bool written = stbi_write_png(path.c_str(), 16, 9, 3, pixels.data(), 16 * 3) != 0;
    run.record("test image written", written, path);
    rescanLibrary();
    run.engine().syncLibrary();
    const int media = run.add("media_input", kMedia);
    run.clearAll();
    run.place(0, media);
    bool seen = false;
    const auto start = std::chrono::steady_clock::now();
    Pixel left;
    Pixel right;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
        run.step(2);
        left = run.sample(0.25, 0.5);
        right = run.sample(0.75, 0.5);
        if (left.r > 200 && right.g > 200) {
            seen = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    run.record("image from the media library as a channel", seen, "left (" + std::to_string(left.r) + "," + std::to_string(left.g) + ") right (" + std::to_string(right.r) + "," + std::to_string(right.g) + ")");
    run.clearAll();
    run.step(2);
}

void testErrors(Runner& run) {
    const int broken = run.add("broken", kBroken);
    run.clearAll();
    run.place(0, broken);
    run.step(2);
    const std::string error = run.engine().clipError(broken);
    const bool named = error.find("Buffer B") != std::string::npos && error.find("Buffer A") == std::string::npos && error.find("Image:") == std::string::npos;
    run.record("compile error names the failing pass", named, error.substr(0, 160));
    bool line = false;
    const size_t at = error.find("Buffer B");
    for (size_t i = at == std::string::npos ? 0 : at; i < error.size(); ++i) {
        if (error[i] == '4' && (i == 0 || !std::isdigit(static_cast<unsigned char>(error[i - 1]))) && (i + 1 >= error.size() || !std::isdigit(static_cast<unsigned char>(error[i + 1])))) {
            line = true;
        }
    }
    run.record("compile error shows the line number of the file", line, "the error is on line 4");

    const int blue = clipIndexByName(shaderClipName("blue"));
    run.clearAll();
    run.place(0, broken);
    run.place(1, blue);
    run.step(3);
    run.expect("a broken shader does not stop other layers", run.centre(), 0, 0, 255, 2);
}

void testReloadAndRelease(Runner& run) {
    const int accumulate = clipIndexByName(shaderClipName("accumulate"));
    run.clearAll();
    run.place(0, accumulate);
    run.step(3);
    writeText(run.root() / "shaders" / "accumulate.glsl", kReloadedPlain);
    std::error_code ec;
    fs::last_write_time(run.root() / "shaders" / "accumulate.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(120), ec);
    const ShaderScan scan = refreshShaderClips();
    run.engine().syncLibrary();
    run.engine().reloadShaderClips(scan.changed);
    run.step(3);
    run.expect("hot reload from multipass to a plain shader", run.centre(), 0, 255, 0, 2);
    run.record("reload released the old buffers", run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");

    const int order = clipIndexByName(shaderClipName("order"));
    run.clearAll();
    run.place(0, order);
    run.step(2);
    const size_t active = run.engine().shaderBufferBytes();
    run.engine().clearLayer(0);
    run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
    run.record("buffers are released when the shader is not drawn", active > 0 && run.engine().shaderBufferBytes() == 0, std::to_string(active) + " bytes before, " + std::to_string(run.engine().shaderBufferBytes()) + " after");
}

void testBudget(Runner& run) {
    const int scale = clipIndexByName(shaderClipName("scale"));
    const int blue = clipIndexByName(shaderClipName("blue"));
    run.engine().setShaderBufferBudget(1500000);
    run.clearAll();
    run.place(0, scale);
    run.step(2);
    run.expect("buffer scale is lowered to fit a small budget", run.centre(), 89, 89, 0, 4);
    run.record("the lower resolution is reported", !run.engine().shaderNote(scale).empty() && run.engine().shaderRuntimeError(scale).empty(), run.engine().shaderNote(scale));
    run.record("memory stays inside the budget", run.engine().shaderBufferBytes() <= 1500000, std::to_string(run.engine().shaderBufferBytes()) + " bytes");

    run.engine().setShaderBufferBudget(1000);
    run.clearAll();
    run.place(0, blue);
    run.place(1, scale);
    run.step(3);
    run.expect("a shader without buffer memory draws nothing and the other layer stays", run.centre(), 0, 0, 255, 2);
    run.record("the missing memory is reported", !run.engine().shaderRuntimeError(scale).empty(), run.engine().shaderRuntimeError(scale));
    run.record("no memory is held when the budget is refused", run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");

    run.engine().setShaderBufferBudget(kShaderBufferBudgetBytes);
    run.clearAll();
    run.place(0, scale);
    run.step(2);
    run.expect("the shader recovers when the budget is back", run.centre(), 128, 128, 0, 3);
    run.record("the error is cleared again", run.engine().shaderRuntimeError(scale).empty(), "no error");
}

const char* kTransitionOld =
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; float g = step(0.5, fract((uv.x + uv.y) * 8.0)); c = vec4(mix(vec3(0.9, 0.3, 0.1), vec3(1.0, 0.8, 0.2), g), 1.0); }\n";

const char* kTransitionNew =
    "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; float g = step(0.5, fract(uv.x * 10.0)); c = vec4(mix(vec3(0.0, 0.3, 1.0), vec3(0.1, 0.9, 0.8), g), 1.0); }\n";

void testTransitionTypes(Runner& run) {
    const int before = run.add("transition_old", kTransitionOld);
    const int after = run.add("transition_new", kTransitionNew);
    for (int t = 0; t < TransitionTypeCount; ++t) {
        run.clearAll();
        run.engine().comp.deckSettings.transition = t;
        run.engine().comp.deckSettings.seconds = 1.0f;
        run.engine().comp.deckSettings.syncBeats = false;
        run.place(0, before);
        run.step(10);
        const Pixel old = run.centre();
        run.engine().switchDeck(1);
        run.place(0, after);
        run.step(2);
        const bool running = run.engine().transitioning();
        const Pixel start = run.centre();
        run.step(28);
        const Pixel middle = run.centre();
        run.step(60);
        const Pixel end = run.centre();
        const std::string name = std::string("transition ") + transitionName(t);
        const std::string detail = "start (" + std::to_string(start.r) + "," + std::to_string(start.g) + "," + std::to_string(start.b) + "), middle (" + std::to_string(middle.r) + "," + std::to_string(middle.g) + "," + std::to_string(middle.b) + "), end (" + std::to_string(end.r) + "," + std::to_string(end.g) + "," + std::to_string(end.b) + ")";
        const bool startsOld = old.r > 150 && std::abs(start.r - old.r) <= 12 && std::abs(start.g - old.g) <= 12 && std::abs(start.b - old.b) <= 12;
        const bool endsNew = (run.near(end, 0, 77, 255, 6) || run.near(end, 26, 230, 204, 6)) && !run.engine().transitioning();
        run.record(name + " starts on the old deck, runs and ends on the new one", running && startsOld && endsNew, detail);
    }
}

void testTransition(Runner& run) {
    const int accumulate = run.add("accumulate_again", kAccumulate);
    run.clearAll();
    run.engine().comp.deckSettings.seconds = 4.0f;
    run.engine().comp.deckSettings.syncBeats = false;
    run.place(0, accumulate);
    run.step(5);
    run.expect("before the deck switch", run.centre(), 128, 0, 0, 4);
    const bool switched = run.engine().switchDeck(1);
    run.step(1);
    run.record("deck switch with a transition started", switched && run.engine().transitioning(), "switch");
    run.expect("the old deck keeps its buffers while it fades out", run.centre(), 153, 0, 0, 6);
    run.step(300);
    run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
    run.record("the buffers of the old deck are released after the transition", run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");
    run.engine().switchDeck(0);
    run.step(2);
}


const char* kRedSolid = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 1.0); }\n";

const char* kHardWipe =
    "#pragma nyrexvj transition\n"
    "void main() { fragColor = vUv.x < uProgress ? colTo(vUv) : colFrom(vUv); }\n";

const char* kCrossMix =
    "#pragma nyrexvj transition\n"
    "void main() { fragColor = mix(colFrom(vUv), colTo(vUv), uProgress); }\n";

const char* kAmountMix =
    "#pragma nyrexvj transition\n"
    "#pragma nyrexvj param amount float \"Amount\" 0 1 0.5\n"
    "void main() { fragColor = mix(colFrom(vUv), colTo(vUv), amount); }\n";

const char* kClearAlpha =
    "#pragma nyrexvj transition\n"
    "void main() { fragColor = vec4(colTo(vUv).rgb, 0.0); }\n";

const char* kBrokenCode =
    "#pragma nyrexvj transition\n"
    "void main() { fragColor = notAFunction(vUv); }\n";

const char* kProgressRed =
    "#pragma nyrexvj transition\n"
    "void main() { fragColor = vec4(uProgress, 0.0, 0.0, 1.0); }\n";

void useCustom(Runner& run, const std::string& name, float seconds) {
    DeckSettings& settings = run.engine().comp.deckSettings;
    settings = DeckSettings{};
    settings.customTransition = name;
    settings.seconds = seconds;
    settings.syncBeats = false;
}

void testCustomTransition(Runner& run) {
    const int red = run.add("ct_red", kRedSolid);
    const int blue = run.add("ct_blue", kBlue);
    const std::string wipe = run.addTransition("ct_wipe", kHardWipe);
    const std::string mix = run.addTransition("ct_mix", kCrossMix);
    run.record("custom transitions are found", customTransitionIndex(wipe) >= 0 && customTransitionIndex(mix) >= 0, "two files");
    run.record("a transition file is not in the clip library", clipIndexByName(shaderClipName("ct_wipe")) < 0, "kept out of the clips");

    run.step(300);
    run.clearAll();
    useCustom(run, wipe, 1.0f);
    run.place(0, red);
    run.step(10);
    run.expect("custom wipe: old deck before the switch", run.centre(), 255, 0, 0, 2);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(2);
    run.record("custom wipe: the transition runs", run.engine().transitioning() && run.engine().activeCustomTransition() == wipe, run.engine().activeCustomTransition());
    run.expect("custom wipe: start shows the old deck", run.sample(0.9, 0.5), 255, 0, 0, 3);
    run.step(28);
    const float progress = run.engine().transitionProgress();
    const Pixel left = run.sample(0.05, 0.5);
    const Pixel right = run.sample(0.95, 0.5);
    run.record("custom wipe: middle shows the new deck left and the old deck right", run.near(left, 0, 0, 255, 3) && run.near(right, 255, 0, 0, 3) && progress > 0.2f && progress < 0.8f, "progress " + std::to_string(progress));
    run.step(60);
    run.expect("custom wipe: ends on the new deck", run.centre(), 0, 0, 255, 2);
    run.record("custom wipe: the transition is over", !run.engine().transitioning(), "idle");

    run.clearAll();
    useCustom(run, mix, 1.0f);
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(30);
    const Pixel middle = run.centre();
    run.record("custom mix: uProgress blends both decks", middle.r > 40 && middle.b > 40 && std::abs(middle.r + middle.b - 255) <= 8 && middle.a == 255, "got (" + std::to_string(middle.r) + "," + std::to_string(middle.g) + "," + std::to_string(middle.b) + ")");
    run.step(60);
    run.expect("custom mix: ends on the new deck", run.centre(), 0, 0, 255, 2);

    const std::string amount = run.addTransition("ct_amount", kAmountMix);
    run.clearAll();
    useCustom(run, amount, 1.0f);
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(20);
    const Pixel half = run.centre();
    run.record("custom parameter: the default of the file is used", std::abs(half.r - 128) <= 6 && std::abs(half.b - 128) <= 6, "got (" + std::to_string(half.r) + "," + std::to_string(half.g) + "," + std::to_string(half.b) + ")");
    run.engine().switchDeck(0);
    run.step(80);
    useCustom(run, amount, 1.0f);
    run.engine().comp.deckSettings.transitionValues[0] = 0.0f;
    run.engine().switchDeck(1);
    run.step(20);
    run.expect("custom parameter: the stored value of the deck is used", run.centre(), 255, 0, 0, 3);
    run.step(80);
    run.engine().switchDeck(0);
    run.step(80);
}

void testCustomTransitionSwitching(Runner& run) {
    const int red = run.add("cs_red", kRedSolid);
    const int blue = run.add("cs_blue", kBlue);
    const std::string mix = run.addTransition("cs_mix", kCrossMix);

    run.clearAll();
    useCustom(run, mix, 1.0f);
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(25);
    const bool running = run.engine().transitioning();
    const Pixel before = run.centre();
    run.engine().switchDeck(0);
    run.step(1);
    const Pixel after = run.centre();
    run.record("switch while a custom transition runs: no jump of more than a few levels", running && run.engine().transitioning() && std::abs(before.r - after.r) <= 40 && std::abs(before.b - after.b) <= 40, "before (" + std::to_string(before.r) + "," + std::to_string(before.b) + ") after (" + std::to_string(after.r) + "," + std::to_string(after.b) + ")");
    run.step(90);
    run.expect("switch while a custom transition runs: ends on the deck that was asked for", run.centre(), 255, 0, 0, 3);
    run.record("switch while a custom transition runs: nothing is left running", !run.engine().transitioning(), "idle");

    for (int i = 0; i < 6; ++i) {
        run.engine().switchDeck(i % 2 == 0 ? 1 : 0);
        run.step(7);
    }
    run.step(120);
    run.record("six quick deck switches end in one steady picture", !run.engine().transitioning() && run.centre().a == 255, "idle");

    run.clearAll();
    useCustom(run, mix, 1.0f);
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(10);
    run.engine().comp.deckSettings.customTransition.clear();
    run.engine().comp.deckSettings.transition = TransitionWipe;
    run.engine().switchDeck(0);
    run.step(2);
    run.record("changing to a built-in transition during a custom one starts the built-in", run.engine().transitioning() && run.engine().activeCustomTransition().empty(), "built-in");
    run.step(120);
    run.expect("the built-in transition ends on the requested deck", run.centre(), 255, 0, 0, 3);
}

void testCustomTransitionFallback(Runner& run) {
    const int red = run.add("cf_red", kRedSolid);
    const int blue = run.add("cf_blue", kBlue);
    const std::string broken = run.addTransition("cf_broken", kBrokenCode);

    run.clearAll();
    useCustom(run, broken, 1.0f);
    run.engine().comp.deckSettings.transition = TransitionFade;
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(2);
    run.record("compile error: the built-in transition runs instead", run.engine().transitioning() && run.engine().activeCustomTransition().empty(), "fallback");
    run.record("compile error: the error is kept for the interface", !run.engine().customTransitionError(broken).empty(), run.engine().customTransitionError(broken).substr(0, 80));
    run.step(30);
    const Pixel middle = run.centre();
    run.record("compile error: the fade still blends both decks", middle.r > 20 && middle.b > 20, "got (" + std::to_string(middle.r) + "," + std::to_string(middle.g) + "," + std::to_string(middle.b) + ")");
    run.step(60);
    run.expect("compile error: ends on the new deck", run.centre(), 0, 0, 255, 3);
    run.engine().switchDeck(0);
    run.step(80);

    const std::string gone = run.addTransition("cf_gone", kCrossMix);
    run.clearAll();
    useCustom(run, gone, 1.0f);
    run.engine().comp.deckSettings.transition = TransitionDissolve;
    run.place(0, red);
    run.step(5);
    std::error_code ec;
    fs::remove(run.root() / "shaders" / "cf_gone.glsl", ec);
    rescanLibrary();
    refreshCustomTransitions();
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(2);
    run.record("a deleted transition file: the built-in transition runs instead", run.engine().transitioning() && run.engine().activeCustomTransition().empty(), "fallback");
    run.step(90);
    run.expect("a deleted transition file: ends on the new deck", run.centre(), 0, 0, 255, 3);
    run.engine().switchDeck(0);
    run.step(80);

    const std::string unknown = "Transition: never_existed";
    run.clearAll();
    useCustom(run, unknown, 1.0f);
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(90);
    run.expect("an unknown transition name: the deck switch still completes", run.centre(), 0, 0, 255, 3);
    run.engine().switchDeck(0);
    run.step(80);

    const std::string repaired = run.addTransition("cf_repair", kBrokenCode);
    run.clearAll();
    useCustom(run, repaired, 0.5f);
    run.place(0, red);
    run.step(5);
    run.addTransition("cf_repair", kCrossMix);
    run.step(3);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(3);
    run.record("a repaired file is picked up without restarting", run.engine().activeCustomTransition() == repaired && run.engine().customTransitionError(repaired).empty(), run.engine().customTransitionError(repaired));
    run.step(60);
    run.engine().switchDeck(0);
    run.step(60);
}

void testCustomTransitionAlpha(Runner& run) {
    const int blue = run.add("ca_blue", kBlue);
    const int straight = run.add("ca_alpha", kAlphaStraight);
    const int green = run.add("ca_green", "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 1.0, 0.0, 1.0); }\n");
    const std::string mix = run.addTransition("ca_mix", kCrossMix);
    const std::string clear = run.addTransition("ca_clear", kClearAlpha);

    run.clearAll();
    useCustom(run, mix, 1.0f);
    run.place(0, blue);
    run.place(1, straight);
    run.step(5);
    run.expect("alpha deck before the transition", run.centre(), 128, 0, 128, 3);
    run.engine().switchDeck(1);
    run.place(0, green);
    run.step(2);
    const Pixel start = run.centre();
    run.record("alpha deck: the transition starts from the composited picture", run.near(start, 128, 0, 128, 14) && start.a == 255, "got (" + std::to_string(start.r) + "," + std::to_string(start.g) + "," + std::to_string(start.b) + "," + std::to_string(start.a) + ")");
    run.step(30);
    const Pixel middle = run.centre();
    run.record("alpha deck: every pixel stays opaque in the middle", middle.a == 255, "alpha " + std::to_string(middle.a));
    run.step(60);
    run.expect("alpha deck: ends on the new deck", run.centre(), 0, 255, 0, 3);
    run.record("alpha deck: the output is opaque at the end", run.centre().a == 255, "alpha");
    run.engine().switchDeck(0);
    run.step(80);

    run.clearAll();
    useCustom(run, clear, 1.0f);
    run.place(0, blue);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, green);
    run.step(20);
    const Pixel written = run.centre();
    run.record("a transition that writes alpha 0 still gives an opaque picture", written.a == 255 && written.g > 100, "got (" + std::to_string(written.r) + "," + std::to_string(written.g) + "," + std::to_string(written.b) + "," + std::to_string(written.a) + ")");
    run.step(80);
    run.engine().switchDeck(0);
    run.step(80);
}

void testCustomTransitionFeedback(Runner& run) {
    const int accumulate = run.add("cb_accumulate", kAccumulate);
    const int blue = run.add("cb_blue", kBlue);
    const std::string mix = run.addTransition("cb_mix", kCrossMix);

    run.clearAll();
    useCustom(run, mix, 4.0f);
    run.place(0, accumulate);
    run.step(5);
    run.expect("feedback deck before the custom transition", run.centre(), 128, 0, 0, 4);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(1);
    run.record("feedback deck: the custom transition started", run.engine().transitioning() && run.engine().activeCustomTransition() == mix, "custom");
    run.expect("feedback deck: the old deck keeps its buffers while the custom transition runs", run.centre(), 153, 0, 0, 8);
    run.step(300);
    run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
    run.record("feedback deck: the transition is over and the old deck released its buffers", !run.engine().transitioning() && run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");
    run.engine().switchDeck(0);
    run.step(300);
}

unsigned char previewRed(Engine& engine) {
    const GLuint texture = engine.transitionPreviewTexture();
    if (texture == 0) {
        return 255;
    }
    GLint width = 0;
    GLint height = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const size_t index = (static_cast<size_t>(height / 2) * static_cast<size_t>(width) + static_cast<size_t>(width / 2)) * 4u;
    return pixels.size() > index ? pixels[index] : static_cast<unsigned char>(255);
}

void testCustomTransitionPreview(Runner& run) {
    const std::string solid = run.addTransition("cp_progress", kProgressRed);
    run.clearAll();
    run.engine().comp.deckSettings = DeckSettings{};
    ShaderValues values = unsetShaderValues();
    run.engine().requestTransitionPreview(solid, 0, 0.0f, values);
    run.step(1);
    run.record("preview: a texture exists", run.engine().transitionPreviewTexture() != 0, "texture");
    const unsigned char atStart = previewRed(run.engine());
    run.engine().requestTransitionPreview(solid, 0, 1.0f, values);
    run.step(1);
    const unsigned char atEnd = previewRed(run.engine());
    run.engine().requestTransitionPreview(solid, 0, 0.5f, values);
    run.step(1);
    const unsigned char atHalf = previewRed(run.engine());
    run.record("preview: uProgress follows the slider", atStart <= 4 && atEnd >= 250 && std::abs(static_cast<int>(atHalf) - 128) <= 6, "0: " + std::to_string(atStart) + ", 0.5: " + std::to_string(atHalf) + ", 1: " + std::to_string(atEnd));
    run.engine().requestTransitionPreview(std::string(), TransitionFade, 0.0f, values);
    run.step(1);
    run.record("preview: a built-in transition renders too", run.engine().transitionPreviewTexture() != 0, "built-in");
    run.engine().requestTransitionPreview("Transition: never_existed", TransitionFade, 0.3f, values);
    run.step(1);
    run.record("preview: an unknown name falls back to the built-in transition", run.engine().transitionPreviewTexture() != 0, "fallback");
}

void testBuiltinsAfterCustom(Runner& run) {
    const int red = run.add("cu_red", kRedSolid);
    const int blue = run.add("cu_blue", kBlue);
    run.clearAll();
    run.engine().comp.deckSettings = DeckSettings{};
    run.engine().comp.deckSettings.seconds = 1.0f;
    run.engine().comp.deckSettings.syncBeats = false;
    run.place(0, red);
    run.step(5);
    run.engine().switchDeck(1);
    run.place(0, blue);
    run.step(2);
    run.record("built-in transitions run unchanged next to custom ones", run.engine().transitioning() && run.engine().activeCustomTransition().empty(), "built-in");
    run.step(90);
    run.expect("built-in fade ends on the new deck", run.centre(), 0, 0, 255, 2);
    run.engine().switchDeck(0);
    run.step(80);
}


const char* kFbGrey = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.8, 0.8, 0.8, 1.0); }\n";
const char* kFbBlack = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 0.0, 0.0, 1.0); }\n";
const char* kFbBar = "void mainImage(out vec4 c, in vec2 p) { float x = p.x / iResolution.x; c = vec4(vec3(step(abs(x - 0.5), 0.05) * 0.8), 1.0); }\n";

struct FeedbackClips {
    int grey = -1;
    int black = -1;
    int bar = -1;
};

FeedbackClips feedbackClips(Runner& run) {
    FeedbackClips clips;
    clips.grey = run.add("fb_grey", kFbGrey);
    clips.black = run.add("fb_black", kFbBlack);
    clips.bar = run.add("fb_bar", kFbBar);
    return clips;
}

void feedbackDefaults(Runner& run) {
    Engine& engine = run.engine();
    engine.setFeedbackFrozen(false);
    engine.setFeedbackReuse(true);
    engine.setFeedbackBudget(kDefaultFeedbackBudgetBytes);
    engine.setQuality(QualityFull, 16.0);
    engine.paused = false;
    engine.resetFeedback();
}

EffectSlot& feedbackEffect(Runner& run, int layer, const char* name, float a, float b, float c, float d) {
    EffectSlot& slot = run.engine().comp.layers[static_cast<size_t>(layer)].fx[0];
    setEffect(slot, effectIndexByName(name));
    slot.params = {a, b, c, d};
    return slot;
}

void charge(Runner& run, const FeedbackClips& clips, int frames) {
    run.place(0, clips.grey);
    run.step(frames);
}

void drainGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

std::string pixelText(const Pixel& pixel) {
    return "(" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + ")";
}

constexpr float kHalfLifeOneSecondEcho = 0.5695f;
constexpr float kHalfLifeOneSecondAccumulate = 0.3015f;

void testFeedbackDecay(Runner& run, const FeedbackClips& clips) {
    run.clearAll();
    feedbackDefaults(run);
    run.engine().setResolutionIndex(0);

    feedbackEffect(run, 0, "Trails", 1.0f, 0.5f, 0.5f, 0.5f);
    charge(run, clips, 90);
    run.place(0, clips.black);
    run.step(900);
    const Pixel trails = run.centre();
    const int trailsLit = run.lit(30);
    const int total = run.engine().width() * run.engine().height();
    run.record("feedback_trails_8bit_residue_documented", trails.r > 8 && trailsLit > total * 9 / 10, "after 15 s of black the old Trails still shows " + pixelText(trails) + " on " + std::to_string(trailsLit * 100 / std::max(total, 1)) + " % of the picture");

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    charge(run, clips, 120);
    run.expect("feedback_echo_charged", run.centre(), 204, 204, 204, 4);
    run.place(0, clips.black);
    run.step(60);
    run.expect("feedback_echo_half_after_one_second_60fps", run.centre(), 102, 102, 102, 10);
    run.step(840);
    const int echoLit = run.lit(3);
    run.record("feedback_echo_fades_to_black", echoLit < total / 50, "after 15 s the lit part is " + std::to_string(echoLit * 100 / std::max(total, 1)) + " % of the picture (Trails above: " + std::to_string(trailsLit * 100 / std::max(total, 1)) + " %)");

    for (const int rate : {30, 120}) {
        run.clearAll();
        feedbackDefaults(run);
        feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
        charge(run, clips, 120);
        run.place(0, clips.black);
        run.stepWith(rate, 1.0 / static_cast<double>(rate));
        run.expect("feedback_echo_half_after_one_second_" + std::to_string(rate) + "fps", run.centre(), 102, 102, 102, 12);
    }

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    charge(run, clips, 120);
    run.place(0, clips.black);
    run.step(2);
    run.engine().paused = true;
    run.step(180);
    const Pixel held = run.centre();
    run.record("feedback_echo_holds_while_paused", held.r >= 190, "got " + pixelText(held) + " after three paused seconds");
    run.engine().paused = false;
    run.step(60);
    const Pixel resumed = run.centre();
    run.record("feedback_echo_resumes_after_pause", resumed.r >= 80 && resumed.r <= 125, "got " + pixelText(resumed) + " one second after the pause");
}

void testFeedbackControls(Runner& run, const FeedbackClips& clips) {
    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    charge(run, clips, 120);
    run.place(0, clips.black);
    run.step(30);
    run.engine().setFeedbackFrozen(true);
    run.step(2);
    const Pixel before = run.centre();
    run.step(180);
    const Pixel during = run.centre();
    run.record("feedback_freeze_holds_the_picture", std::abs(before.r - during.r) <= 3 && during.r > 100, "frozen at " + pixelText(before) + " and still " + pixelText(during) + " three seconds later");
    run.engine().setFeedbackFrozen(false);
    run.step(60);
    const Pixel released = run.centre();
    run.record("feedback_unfreeze_fades_again", released.r < during.r * 65 / 100, "after unfreezing " + pixelText(released) + " from " + pixelText(during));
    charge(run, clips, 120);
    run.place(0, clips.black);
    run.step(3);
    run.engine().resetFeedback();
    run.step(1);
    run.expect("feedback_reset_clears", run.centre(), 0, 0, 0, 2);

    run.clearAll();
    feedbackDefaults(run);
    const int accumulate = run.add("fb_accumulate", kAccumulate);
    if (accumulate >= 0) {
        run.place(0, accumulate);
        run.step(20);
        const Pixel full = run.centre();
        run.engine().resetFeedback();
        run.step(1);
        const Pixel again = run.centre();
        run.record("feedback_reset_restarts_shader_buffers", full.r >= 250 && again.r <= 80, "shader buffer was " + pixelText(full) + " and starts again at " + pixelText(again));
    }
}

void testFeedbackEffects(Runner& run, const FeedbackClips& clips) {
    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Motion Smear", 0.7f, 0.0f, 0.0f, 0.0f);
    run.place(0, clips.black);
    run.step(30);
    run.place(0, clips.grey);
    run.step(3);
    const Pixel early = run.centre();
    run.step(57);
    const Pixel oneSecond = run.centre();
    run.step(540);
    const Pixel late = run.centre();
    run.record("feedback_smear_rises_gradually", early.r >= 8 && early.r <= 16 && oneSecond.r >= 128 && oneSecond.r <= 153 && late.r >= 200, "three frames after the cut " + pixelText(early) + " (about 12 expected), after one second " + pixelText(oneSecond) + " (about 141), after ten seconds " + pixelText(late));

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Accumulate", kHalfLifeOneSecondAccumulate, 0.0f, 0.0f, 0.0f);
    run.place(0, clips.black);
    run.step(30);
    run.place(0, clips.grey);
    run.step(60);
    run.expect("feedback_accumulate_average_half_after_one_second", run.centre(), 102, 102, 102, 12);

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Accumulate", kHalfLifeOneSecondAccumulate, 0.5f, 0.0f, 0.0f);
    charge(run, clips, 120);
    run.place(0, clips.black);
    run.step(60);
    run.expect("feedback_accumulate_lighten_half_after_one_second", run.centre(), 102, 102, 102, 12);

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Accumulate", kHalfLifeOneSecondAccumulate, 1.0f, 0.0f, 0.0f);
    charge(run, clips, 300);
    const Pixel exposed = run.centre();
    run.record("feedback_accumulate_exposure_gets_brighter_than_the_source", exposed.r >= 240, "a grey of 204 reached " + pixelText(exposed));

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Feedback Transform", 0.5f, 0.5f, 0.5f, 0.5f);
    charge(run, clips, 120);
    const Pixel held = run.centre();
    run.place(0, clips.black);
    run.step(60);
    const Pixel transformed = run.centre();
    run.record("feedback_transform_neutral_holds_and_fades", held.r >= 200 && held.r <= 208 && transformed.r >= 93 && transformed.r <= 117, "a neutral Feedback Transform holds " + pixelText(held) + " on a steady picture and left " + pixelText(transformed) + " one second after the picture went black (about 105 expected)");

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Time Echo", 0.7f, 1.0f, 0.0f, 0.0f);
    run.place(0, clips.bar);
    run.step(150);
    const Pixel right = run.sample(0.8, 0.5);
    const Pixel left = run.sample(0.2, 0.5);
    run.record("feedback_echo_follows_its_angle", right.r >= 120 && left.r <= 4, "a bar at the centre echoed to the right " + pixelText(right) + ", nothing to the left " + pixelText(left));

    run.clearAll();
    feedbackDefaults(run);
    EffectSlot& echoSlot = feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    charge(run, clips, 60);
    run.place(0, clips.black);
    run.step(1);
    echoSlot.params[0] = 0.0f;
    run.step(40);
    const int total = run.engine().width() * run.engine().height();
    run.record("feedback_short_half_life_follows_the_control", run.lit(3) < total / 50, "a half-life of 0.04 s left " + std::to_string(run.lit(3) * 100 / std::max(total, 1)) + " % lit after 40 frames");
}

void testFeedbackBudget(Runner& run, const FeedbackClips& clips) {
    Engine& engine = run.engine();
    run.clearAll();
    feedbackDefaults(run);
    engine.setResolutionIndex(0);
    const size_t full = static_cast<size_t>(engine.width()) * static_cast<size_t>(engine.height()) * 4u;

    for (int layer = 0; layer < 4; ++layer) {
        run.engine().comp.layers[static_cast<size_t>(layer)].opacity = 1.0f;
        feedbackEffect(run, layer, "Trails", 0.7f, 0.5f, 0.5f, 0.5f);
        run.place(layer, clips.grey);
    }
    engine.setFeedbackBudget(0);
    run.step(8);
    FeedbackStats open = engine.feedbackStats();
    run.record("feedback_four_layers_without_a_limit", open.live == 4 && open.liveBytes == 4 * full && open.reduced == 0 && open.denied == 0, std::to_string(open.live) + " buffers, " + std::to_string(open.liveBytes) + " bytes, expected " + std::to_string(4 * full));

    const unsigned long long tight = 5ull * 1024ull * 1024ull;
    engine.setFeedbackBudget(tight);
    run.step(8);
    FeedbackStats limited = engine.feedbackStats();
    run.record("feedback_budget_is_kept", limited.total() <= tight && limited.live == 4 && limited.reduced >= 1 && limited.denied == 0, std::to_string(limited.live) + " buffers, " + std::to_string(limited.liveBytes) + " of " + std::to_string(tight) + " bytes, " + std::to_string(limited.reduced) + " smaller than the picture");
    run.record("feedback_budget_shows_in_the_memory_total", engine.gpuMemory().feedback == limited.total(), "memory overview " + std::to_string(engine.gpuMemory().feedback) + " bytes, stats " + std::to_string(limited.total()));
    run.record("feedback_budget_keeps_the_picture", run.lit(30) > 0, "the output is not black");

    run.clearAll();
    feedbackDefaults(run);
    engine.setResolutionIndex(0);
    feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    engine.setFeedbackBudget(600ull * 1024ull);
    charge(run, clips, 120);
    const FeedbackStats halfSize = engine.feedbackStats();
    run.place(0, clips.black);
    run.step(60);
    const Pixel small = run.centre();
    run.record("feedback_smaller_buffer_still_works", halfSize.live == 1 && halfSize.reduced == 1 && small.r >= 85 && small.r <= 125, std::to_string(halfSize.liveBytes) + " bytes instead of " + std::to_string(full) + ", centre " + pixelText(small) + " after one second");
    run.step(840);
    const int smallLit = run.lit(3);
    const Pixel smallEnd = run.centre();
    const int pixelCount = engine.width() * engine.height();
    run.record("feedback_smaller_buffer_fades_out_without_a_stall", smallLit < pixelCount / 4, "fifteen seconds after the picture went black the half size buffer leaves " + pixelText(smallEnd) + " in the centre and " + std::to_string(smallLit * 100 / std::max(pixelCount, 1)) + " % of the picture one step above black (the full size buffer: 0 %)");

    run.clearAll();
    feedbackDefaults(run);
    engine.setResolutionIndex(0);
    feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    engine.setFeedbackBudget(100ull * 1024ull);
    charge(run, clips, 30);
    const FeedbackStats refused = engine.feedbackStats();
    run.expect("feedback_refused_buffer_shows_the_live_picture", run.centre(), 204, 204, 204, 4);
    run.place(0, clips.black);
    run.step(2);
    run.expect("feedback_refused_buffer_leaves_no_echo", run.centre(), 0, 0, 0, 2);
    run.record("feedback_refused_buffer_is_counted", refused.live == 0 && refused.denied >= 1 && engine.gpuMemory().feedback == 0, std::to_string(refused.live) + " buffers, " + std::to_string(static_cast<long long>(refused.denied)) + " refused");

    engine.setFeedbackBudget(0);
    charge(run, clips, 20);
    const FeedbackStats restored = engine.feedbackStats();
    run.record("feedback_budget_off_gives_full_size_again", restored.live == 1 && restored.reduced == 0 && restored.liveBytes == full, std::to_string(restored.liveBytes) + " bytes, expected " + std::to_string(full));
    feedbackDefaults(run);
}

void testFeedbackResolutions(Runner& run, const FeedbackClips& clips) {
    Engine& engine = run.engine();
    run.clearAll();
    feedbackDefaults(run);
    run.place(0, clips.grey);
    run.place(1, clips.bar);
    run.place(2, clips.grey);
    feedbackEffect(run, 0, "Trails", 0.7f, 0.5f, 0.5f, 0.5f);
    feedbackEffect(run, 1, "Time Echo", 0.5f, 0.3f, 0.2f, 0.2f);
    feedbackEffect(run, 2, "Accumulate", 0.4f, 0.0f, 0.0f, 0.0f);
    EffectSlot& master = engine.comp.master.fx[0];
    setEffect(master, effectIndexByName("Time Echo"));
    int errors = 0;
    for (const int index : {0, 1, 2, 3, 0, 2, 1, 0}) {
        drainGlErrors();
        engine.setResolutionIndex(index);
        run.step(5);
        const FeedbackStats stats = engine.feedbackStats();
        const size_t one = static_cast<size_t>(engine.width()) * static_cast<size_t>(engine.height()) * 4u;
        const bool sized = stats.live == 4 && stats.liveBytes == 4 * one && stats.reduced == 0 && stats.pooled <= static_cast<int>(kFeedbackPoolMax);
        const bool seen = engine.gpuMemory().feedback == stats.total();
        const bool visible = run.lit(30) > 0;
        const bool clean = glGetError() == GL_NO_ERROR;
        const bool good = sized && seen && visible && clean;
        errors += good ? 0 : 1;
        run.record("feedback_resolution_" + std::to_string(engine.width()) + "x" + std::to_string(engine.height()) + "_" + std::to_string(index), good, std::to_string(stats.live) + " buffers, " + std::to_string(stats.liveBytes) + " bytes, expected " + std::to_string(4 * one) + ", pooled " + std::to_string(stats.pooled) + ", picture " + (visible ? "lit" : "black") + ", GL " + (clean ? "clean" : "error"));
    }
    engine.setResolutionIndex(0);
    run.step(3);

    const size_t base = static_cast<size_t>(engine.width()) * static_cast<size_t>(engine.height()) * 4u;
    engine.setQuality(QualityBalanced, 16.0);
    run.step(6);
    const FeedbackStats scaled = engine.feedbackStats();
    const float balancedScale = fixedQualityScale(QualityBalanced);
    const size_t layerSize = static_cast<size_t>(scaledDimension(engine.width(), balancedScale)) * static_cast<size_t>(scaledDimension(engine.height(), balancedScale)) * 4u;
    run.record("feedback_follows_the_layer_resolution", scaled.live == 4 && scaled.liveBytes == 3 * layerSize + base && scaled.reduced == 0, std::to_string(scaled.liveBytes) + " bytes with the balanced layer size, expected " + std::to_string(3 * layerSize + base));
    engine.setQuality(QualityFull, 16.0);
    run.step(6);
    const FeedbackStats back = engine.feedbackStats();
    run.record("feedback_returns_to_full_layer_resolution", back.live == 4 && back.liveBytes == 4 * base, std::to_string(back.liveBytes) + " bytes, expected " + std::to_string(4 * base));
    feedbackDefaults(run);
}

void testFeedbackReuse(Runner& run, const FeedbackClips& clips) {
    Engine& engine = run.engine();
    run.clearAll();
    feedbackDefaults(run);
    engine.setResolutionIndex(0);
    EffectSlot& slot = feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    run.place(0, clips.grey);
    run.step(10);
    const FeedbackStats start = engine.feedbackStats();
    for (int i = 0; i < 40; ++i) {
        slot.enabled = i % 2 == 1;
        run.step(2);
    }
    slot.enabled = true;
    run.step(3);
    const FeedbackStats withReuse = engine.feedbackStats();
    run.record("feedback_buffers_are_reused", withReuse.created - start.created <= 2 && withReuse.reused - start.reused >= 15, std::to_string(static_cast<long long>(withReuse.created - start.created)) + " made and " + std::to_string(static_cast<long long>(withReuse.reused - start.reused)) + " reused over 20 off and on switches");

    engine.setFeedbackReuse(false);
    run.record("feedback_reuse_off_empties_the_pool", engine.feedbackStats().pooled == 0, "pool holds " + std::to_string(engine.feedbackStats().pooled));
    const FeedbackStats plain = engine.feedbackStats();
    for (int i = 0; i < 40; ++i) {
        slot.enabled = i % 2 == 1;
        run.step(2);
    }
    slot.enabled = true;
    run.step(3);
    const FeedbackStats without = engine.feedbackStats();
    run.record("feedback_reuse_off_makes_new_buffers", without.created - plain.created >= 15 && without.reused == plain.reused && without.pooled == 0, std::to_string(static_cast<long long>(without.created - plain.created)) + " made and " + std::to_string(static_cast<long long>(without.reused - plain.reused)) + " reused");
    engine.setFeedbackReuse(true);

    slot.enabled = false;
    run.step(3);
    const FeedbackStats parked = engine.feedbackStats();
    run.record("feedback_released_buffer_waits_in_the_pool", parked.pooled == 1 && parked.live == 0, std::to_string(parked.pooled) + " pooled, " + std::to_string(parked.live) + " live");
    run.step(static_cast<int>(kFeedbackPoolIdleFrames) + 20);
    const FeedbackStats idle = engine.feedbackStats();
    run.record("feedback_idle_pool_is_released", idle.pooled == 0 && idle.total() == 0 && engine.gpuMemory().feedback == 0, std::to_string(idle.pooled) + " pooled, " + std::to_string(idle.total()) + " bytes");
    slot.enabled = true;

    run.clearAll();
    feedbackDefaults(run);
    feedbackEffect(run, 0, "Time Echo", kHalfLifeOneSecondEcho, 0.0f, 0.0f, 0.0f);
    run.place(0, clips.grey);
    run.step(6);
    DeckSettings& deck = engine.comp.deckSettings;
    deck = DeckSettings{};
    deck.seconds = 0.3f;
    deck.syncBeats = false;
    bool clean = true;
    drainGlErrors();
    for (int i = 0; i < 6; ++i) {
        engine.switchDeck((i + 1) % 2);
        run.step(30);
        clean = clean && glGetError() == GL_NO_ERROR;
    }
    engine.switchDeck(0);
    run.step(40);
    run.record("feedback_deck_switches_stay_clean", clean && engine.feedbackStats().total() <= 4 * static_cast<size_t>(engine.width()) * static_cast<size_t>(engine.height()) * 4u, std::to_string(engine.feedbackStats().total()) + " bytes after six switches");
}

void testFeedbackSoak(Runner& run, const FeedbackClips& clips) {
    Engine& engine = run.engine();
    run.clearAll();
    feedbackDefaults(run);
    engine.setResolutionIndex(0);
    int frames = 1200;
    if (const char* text = std::getenv("NYREXVJ_FEEDBACK_SOAK_FRAMES")) {
        frames = std::clamp(std::atoi(text), 100, 200000);
    }
    const char* names[] = {"Trails", "Time Echo", "Motion Smear", "Accumulate"};
    DeckSettings& deck = engine.comp.deckSettings;
    deck = DeckSettings{};
    deck.seconds = 0.0f;
    deck.syncBeats = false;
    const auto configure = [&]() {
        for (int layer = 0; layer < 4; ++layer) {
            feedbackEffect(run, layer, names[layer], 0.6f, 0.5f, 0.5f, 0.4f);
            run.place(layer, layer % 2 == 0 ? clips.grey : clips.bar);
        }
        setEffect(engine.comp.master.fx[0], effectIndexByName("Hue Feedback"));
    };
    engine.switchDeck(1);
    run.step(2);
    configure();
    run.step(2);
    engine.switchDeck(0);
    run.step(2);
    configure();
    run.step(2);
    deck.seconds = 0.2f;

    std::string lastAction = "none";
    std::string blackAfter;
    unsigned long long seed = 12345;
    const auto next = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>(seed >> 33);
    };
    const unsigned long long budgets[] = {0ull, 3ull * 1024ull * 1024ull, 12ull * 1024ull * 1024ull, kDefaultFeedbackBudgetBytes};
    size_t peakFeedback = 0;
    int glErrors = 0;
    int blackFrames = 0;
    drainGlErrors();
    for (int frame = 0; frame < frames; ++frame) {
        run.step(1);
        if (frame % 100 == 99) {
            const FeedbackStats stats = engine.feedbackStats();
            peakFeedback = std::max(peakFeedback, stats.total());
            glErrors += glGetError() == GL_NO_ERROR ? 0 : 1;
            if (run.lit(3) == 0) {
                ++blackFrames;
                blackAfter += (blackAfter.empty() ? "" : ", ") + std::string("frame ") + std::to_string(frame) + " after " + lastAction;
            }
        }
        if (frame % 40 == 39) {
            switch (next() % 9) {
            case 0: {
                const int index = static_cast<int>(next() % 2);
                engine.setResolutionIndex(index);
                lastAction = "resolution " + std::to_string(index);
                break;
            }
            case 1:
                engine.setFeedbackFrozen(!engine.feedbackFrozen());
                lastAction = engine.feedbackFrozen() ? "freeze on" : "freeze off";
                break;
            case 2:
                engine.resetFeedback();
                lastAction = "reset";
                break;
            case 3: {
                const size_t layer = next() % 4;
                EffectSlot& slot = engine.comp.layers[layer].fx[0];
                slot.enabled = !slot.enabled;
                lastAction = std::string("layer ") + std::to_string(layer) + (slot.enabled ? " effect on" : " effect off");
                break;
            }
            case 4: {
                const size_t layer = next() % 4;
                const size_t param = next() % 4;
                const float value = static_cast<float>(next() % 101) / 100.0f;
                engine.comp.layers[layer].fx[0].params[param] = value;
                lastAction = "layer " + std::to_string(layer) + " parameter " + std::to_string(param) + " = " + std::to_string(value);
                break;
            }
            case 5: {
                const int index = static_cast<int>(next() % 2);
                engine.switchDeck(index);
                lastAction = "deck " + std::to_string(index);
                break;
            }
            case 6: {
                const int mode = static_cast<int>(next() % 3);
                engine.setQuality(mode, 16.0);
                lastAction = "quality " + std::to_string(mode);
                break;
            }
            case 7: {
                const unsigned long long value = budgets[next() % 4];
                engine.setFeedbackBudget(value);
                lastAction = "budget " + std::to_string(value);
                break;
            }
            default: {
                const bool reuse = next() % 2 == 0;
                engine.setFeedbackReuse(reuse);
                lastAction = reuse ? "reuse on" : "reuse off";
                break;
            }
            }
        }
    }
    const size_t ceiling = (10u + kFeedbackPoolMax) * static_cast<size_t>(1280) * 720u * 4u;
    run.record("feedback_soak_memory_stays_below_the_ceiling", peakFeedback <= ceiling, std::to_string(frames) + " frames with resolution, deck, quality, budget, freeze and reset changes: peak " + std::to_string(peakFeedback) + " bytes, ceiling " + std::to_string(ceiling));
    run.record("feedback_soak_no_gl_errors_and_a_picture", glErrors == 0 && blackFrames == 0, std::to_string(glErrors) + " GL errors, " + std::to_string(blackFrames) + " black samples" + (blackAfter.empty() ? "" : " (" + blackAfter + ")"));

    engine.setFeedbackFrozen(false);
    engine.setFeedbackBudget(kDefaultFeedbackBudgetBytes);
    engine.switchDeck(0);
    run.step(30);
    for (int layer = 0; layer < 4; ++layer) {
        engine.comp.layers[static_cast<size_t>(layer)].fx[0].enabled = false;
    }
    engine.comp.master.fx[0].enabled = false;
    run.step(static_cast<int>(kFeedbackPoolIdleFrames) + 60);
    const FeedbackStats end = engine.feedbackStats();
    run.record("feedback_soak_releases_everything", end.live == 0 && end.pooled == 0 && engine.gpuMemory().feedback == 0, std::to_string(end.live) + " live, " + std::to_string(end.pooled) + " pooled, " + std::to_string(engine.gpuMemory().feedback) + " bytes after every effect was switched off");
    feedbackDefaults(run);
}

void testFeedbackLab(Runner& run) {
    const FeedbackClips clips = feedbackClips(run);
    if (clips.grey < 0 || clips.black < 0 || clips.bar < 0) {
        run.record("feedback_clips", false, "the test clips did not load");
        return;
    }
    testFeedbackDecay(run, clips);
    testFeedbackControls(run, clips);
    testFeedbackEffects(run, clips);
    testFeedbackBudget(run, clips);
    testFeedbackResolutions(run, clips);
    testFeedbackReuse(run, clips);
    testFeedbackSoak(run, clips);
    run.clearAll();
    feedbackDefaults(run);
}

bool mentionsLine(const std::string& text, int number) {
    const std::string wanted = std::to_string(number);
    for (size_t i = 0; i + wanted.size() <= text.size(); ++i) {
        if (text.compare(i, wanted.size(), wanted) != 0) {
            continue;
        }
        const bool before = i == 0 || !std::isdigit(static_cast<unsigned char>(text[i - 1]));
        const bool after = i + wanted.size() >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i + wanted.size()]));
        if (before && after) {
            return true;
        }
    }
    return false;
}

void testIsfGenerator(Runner& run) {
    const int solid = run.add("isf_solid", kIsfSolid);
    run.record("ISF generator is in the clip library", solid >= 0, "isf_solid");
    run.clearAll();
    run.place(0, solid);
    run.step(3);
    run.expect("ISF generator draws with the defaults of the file", run.centre(), 128, 64, 0, 3);
    run.record("ISF generator has no error", run.engine().clipError(solid).empty() && run.engine().shaderRuntimeError(solid).empty(), run.engine().clipError(solid));

    Layer& layer = run.engine().comp.layers[0];
    layer.playback[0].values[0] = 1.0f;
    run.step(2);
    run.expect("a stored value of the cell changes the picture", run.centre(), 255, 128, 0, 3);

    layer.playback[0].values[1] = 0.0f;
    layer.playback[0].values[2] = 1.0f;
    layer.playback[0].values[3] = 0.0f;
    layer.playback[0].values[4] = 1.0f;
    run.step(2);
    run.expect("a colour value is passed on as four numbers", run.centre(), 0, 255, 0, 3);

    layer.playback[0].values[1] = 1.0f;
    layer.playback[0].values[2] = 0.5f;
    layer.playback[0].values[3] = 0.0f;
    layer.playback[0].values[5] = 1.0f;
    run.step(2);
    run.expect("a bool value is passed on", run.centre(), 0, 128, 255, 3);

    layer.playback[0].values[6] = 2.0f;
    run.step(2);
    run.expect("a list value is passed on", run.centre(), 0, 0, 255, 3);

    layer.playback[0].values = unsetClipValues();
    run.step(2);
    run.expect("unset values fall back to the defaults of the file", run.centre(), 128, 64, 0, 3);

    run.clearAll();
    run.place(0, solid);
    run.place(1, solid);
    run.engine().comp.layers[1].playback[0].values[0] = 1.0f;
    run.engine().comp.layers[1].blend = BlendNormal;
    run.step(3);
    run.expect("every cell keeps its own values", run.centre(), 255, 128, 0, 3);
    run.engine().clearLayer(1);
    run.step(2);
    run.expect("the other cell still has its defaults", run.centre(), 128, 64, 0, 3);

    const int accumulate = run.add("isf_accumulate", kIsfAccumulate);
    const int accumulateFull = run.add("isf_accumulate_full", kIsfAccumulateFull);
    run.clearAll();
    run.place(0, accumulate);
    run.step(1);
    run.expect("ISF persistent buffer after 1 frame", run.centre(), 26, 0, 0, 3);
    run.step(4);
    run.expect("ISF persistent buffer keeps its content over frames", run.centre(), 128, 0, 0, 4);
    const unsigned long long half = run.engine().shaderBufferBytes();
    run.clearAll();
    run.place(0, accumulateFull);
    run.step(2);
    const unsigned long long full = run.engine().shaderBufferBytes();
    const double ratio = full > 0 ? static_cast<double>(half) / static_cast<double>(full) : 0.0;
    run.record("ISF WIDTH and HEIGHT halve the buffer", half > 0 && ratio > 0.2 && ratio < 0.3, std::to_string(half) + " bytes at half size, " + std::to_string(full) + " bytes at full size");

    run.clearAll();
    run.place(0, run.add("isf_imported", kIsfImported));
    bool seen = false;
    const auto start = std::chrono::steady_clock::now();
    Pixel left;
    Pixel right;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
        run.step(2);
        left = run.sample(0.25, 0.5);
        right = run.sample(0.75, 0.5);
        if (left.r > 200 && right.g > 200) {
            seen = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    run.record("ISF imported image comes from the media library", seen, "left (" + std::to_string(left.r) + "," + std::to_string(left.g) + ") right (" + std::to_string(right.r) + "," + std::to_string(right.g) + ")");
}

void testIsfFilter(Runner& run) {
    const int blue = clipIndexByName(shaderClipName("blue"));
    const int swap = run.addFilter("isf_swap", kIsfSwap);
    run.record("ISF filter is in the effect list", swap >= 0 && effectLibrary()[static_cast<size_t>(std::max(swap, 0))].isIsf, isfEffectName("isf_swap"));
    run.record("ISF filter is not a clip", clipIndexByName(shaderClipName("isf_swap")) < 0, "not in the clip library");
    run.record("the first control starts at the default of the file", swap >= 0 && std::fabs(effectLibrary()[static_cast<size_t>(swap)].defaults[0] - 0.5f) < 0.001f, "0.5");

    run.clearAll();
    run.place(0, blue);
    run.useEffect(0, swap);
    run.step(3);
    run.expect("ISF filter changes the layer picture", run.centre(), 128, 0, 128, 3);
    run.record("ISF filter has no error", run.engine().effectError(swap).empty(), run.engine().effectError(swap));

    run.engine().comp.layers[0].fx[0].params[0] = 1.0f;
    run.step(2);
    run.expect("the first control sets the first parameter", run.centre(), 255, 0, 0, 3);

    run.engine().comp.layers[0].fx[0].params[0] = 0.0f;
    run.step(2);
    run.expect("the control at zero leaves the picture alone", run.centre(), 0, 0, 255, 3);

    run.engine().comp.layers[0].fx[0].enabled = false;
    run.engine().comp.layers[0].fx[0].params[0] = 1.0f;
    run.step(2);
    run.expect("a switched off ISF filter does nothing", run.centre(), 0, 0, 255, 3);

    run.clearAll();
    run.place(0, blue);
    run.useEffect(0, swap);
    EffectSlot& gated = run.engine().comp.layers[0].fx[0];
    gated.params[0] = 1.0f;
    gated.reactBand = BandLevel;
    gated.reactMode = ReactGate;
    gated.reactAmount = 0.0f;
    run.step(6);
    const Pixel mixed = run.centre();
    run.record("the mix of an ISF filter follows an audio gate", mixed.r > 4 && mixed.r < 160 && std::abs(mixed.r + mixed.b - 255) <= 4, "got (" + std::to_string(mixed.r) + "," + std::to_string(mixed.g) + "," + std::to_string(mixed.b) + "), a part of the filtered and a part of the original picture");

    const int split = run.add("split_red_blue", kSplitRedBlue);
    const int blur = run.addFilter("isf_blur_test", kIsfBlur);
    run.clearAll();
    run.place(0, split);
    run.step(2);
    const Pixel sharp = run.sample(0.49, 0.5);
    run.useEffect(0, blur);
    run.step(3);
    const Pixel soft = run.sample(0.49, 0.5);
    run.expect("two pass ISF blur keeps far areas unchanged", run.sample(0.1, 0.5), 255, 0, 0, 4);
    run.expect("two pass ISF blur mixes both sides at the edge", run.centre(), 128, 0, 128, 14);
    run.record("two pass ISF blur softens a hard edge", sharp.b < 10 && soft.b > 20 && soft.r > 20, "sharp blue " + std::to_string(sharp.b) + ", soft red " + std::to_string(soft.r) + " blue " + std::to_string(soft.b));
    run.record("two pass ISF blur holds its buffer", run.engine().shaderBufferBytes() > 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");
    run.clearAll();
    run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
    run.record("the buffer of the filter is released when it is not used", run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");

    const int trail = run.addFilter("isf_trail_test", kIsfTrail);
    run.clearAll();
    run.place(0, blue);
    run.useEffect(0, trail);
    run.step(1);
    run.expect("ISF persistent filter buffer after 1 frame", run.centre(), 0, 0, 128, 4);
    run.step(2);
    run.expect("ISF persistent filter buffer builds up over frames", run.centre(), 0, 0, 223, 6);

    run.clearAll();
    run.place(0, blue);
    run.useEffect(1, swap);
    run.engine().comp.layers[1].fx[0].params[0] = 1.0f;
    run.step(3);
    run.expect("a filter on an empty layer does not touch the layers below", run.centre(), 0, 0, 255, 3);
}

void testIsfFallbacks(Runner& run) {
    const int blue = clipIndexByName(shaderClipName("blue"));
    const int broken = run.add("isf_broken", kIsfBroken);
    run.clearAll();
    run.place(0, blue);
    run.place(1, broken);
    run.step(3);
    const std::string error = run.engine().clipError(broken);
    run.record("a compile error in an ISF file names the line of the file", !error.empty() && mentionsLine(error, 3), error.substr(0, 200));
    run.expect("a broken ISF file does not stop other layers", run.centre(), 0, 0, 255, 2);

    const int legacy = run.add("isf_legacy", kIsfLegacy);
    run.clearAll();
    run.place(0, blue);
    run.place(1, legacy);
    run.step(3);
    const std::string unsupported = run.engine().clipError(legacy);
    run.record("an unsupported ISF feature is named", unsupported.find("varying") != std::string::npos, unsupported.substr(0, 200));
    run.expect("an unsupported ISF file draws nothing and the other layer stays", run.centre(), 0, 0, 255, 2);

    const int swap = effectIndexByName(isfEffectName("isf_swap"));
    const int bad = run.addFilter("isf_filter_broken", kIsfFilterBroken);
    run.record("a broken ISF filter is listed in the effect list", bad >= 0, isfEffectName("isf_filter_broken"));
    run.clearAll();
    run.place(0, blue);
    run.useEffect(0, bad);
    run.step(3);
    run.expect("a broken ISF filter passes the picture through", run.centre(), 0, 0, 255, 2);
    run.record("the compile error of a filter names the line", mentionsLine(run.engine().effectError(bad), 3), run.engine().effectError(bad).substr(0, 200));
    run.record("the compile error of a filter is shown", !run.engine().effectError(bad).empty(), run.engine().effectError(bad).substr(0, 200));
    run.useEffect(0, swap);
    run.step(2);
    run.expect("a working ISF filter still works next to a broken one", run.centre(), 128, 0, 128, 3);
}

void testIsfReload(Runner& run) {
    const int solid = clipIndexByName(shaderClipName("isf_solid"));
    run.clearAll();
    run.place(0, solid);
    run.step(2);
    writeText(run.root() / "shaders" / "isf_solid.glsl", kIsfSolidReloaded);
    std::error_code ec;
    fs::last_write_time(run.root() / "shaders" / "isf_solid.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(240), ec);
    const ShaderScan scan = refreshShaderClips();
    run.engine().syncLibrary();
    run.engine().reloadShaderClips(scan.changed);
    run.step(3);
    run.expect("hot reload of an ISF generator", run.centre(), 0, 255, 0, 3);

    const int swap = effectIndexByName(isfEffectName("isf_swap"));
    const int blue = clipIndexByName(shaderClipName("blue"));
    run.clearAll();
    run.place(0, blue);
    run.useEffect(0, swap);
    run.step(2);
    writeText(run.root() / "shaders" / "isf_swap.fs", kIsfSwapReloaded);
    fs::last_write_time(run.root() / "shaders" / "isf_swap.fs", fs::file_time_type::clock::now() + std::chrono::seconds(240), ec);
    const IsfScan isf = refreshIsfEffects();
    run.engine().syncEffects();
    run.engine().reloadNodeEffects(isf.changed);
    run.step(3);
    run.record("a changed ISF filter file is noticed", !isf.changed.empty(), std::to_string(isf.changed.size()) + " changed");
    run.expect("hot reload of an ISF filter", run.centre(), 0, 0, 255, 3);

    fs::remove(run.root() / "shaders" / "isf_swap.fs", ec);
    const IsfScan gone = refreshIsfEffects();
    run.engine().reloadNodeEffects(gone.changed);
    run.step(2);
    run.record("a removed ISF filter file is reported", !run.engine().effectError(swap).empty(), run.engine().effectError(swap));
    run.expect("a removed ISF filter passes the picture through", run.centre(), 0, 0, 255, 2);
}

void testIsfExamples(Runner& run) {
    const char* configured = std::getenv("NYREXVJ_SHADER_EXAMPLES");
    const fs::path directory = configured && *configured ? fs::path(configured) : fs::path("docs") / "shaders";
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        logLine("Shader self test: the ISF example folder " + directory.string() + " was not found, the example files are not checked");
        return;
    }
    const auto read = [&](const char* name) {
        std::ifstream in(directory / name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const int red = run.add("example_red", kRed);
    const int split = run.add("example_split", kSplitRedBlue);
    const int blue = clipIndexByName(shaderClipName("blue"));

    const int plasma = run.addFile("example_plasma.fs", read("isf_plasma.fs"));
    run.clearAll();
    run.place(0, plasma);
    run.step(8);
    run.record("example isf_plasma.fs compiles and draws", plasma >= 0 && run.engine().clipError(plasma).empty() && run.engine().shaderRuntimeError(plasma).empty() && run.lit(60) > 1000, plasma >= 0 ? run.engine().clipError(plasma) : std::string("missing"));
    run.engine().comp.layers[0].playback[0].values[2] = 2.0f;
    run.engine().comp.layers[0].playback[0].values[5] = 1.0f;
    run.step(3);
    run.record("example isf_plasma.fs draws with another pattern and invert", run.engine().shaderRuntimeError(plasma).empty() && run.lit(60) > 1000, "mode 2, invert on");

    const int dots = run.addFile("example_dots.fs", read("isf_feedback_dots.fs"));
    run.clearAll();
    run.place(0, dots);
    run.step(30);
    run.record("example isf_feedback_dots.fs compiles and leaves a trail", dots >= 0 && run.engine().clipError(dots).empty() && run.engine().shaderRuntimeError(dots).empty() && run.lit(40) > 200, dots >= 0 ? run.engine().clipError(dots) : std::string("missing"));

    const int shift = run.addFilter("example_color_shift", read("isf_color_shift.fs"));
    run.clearAll();
    run.place(0, red);
    run.useEffect(0, shift);
    run.step(3);
    run.expect("example isf_color_shift.fs mixes the colour channels", run.centre(), 102, 51, 102, 5);
    run.record("example isf_color_shift.fs has no error", shift >= 0 && run.engine().effectError(shift).empty(), shift >= 0 ? run.engine().effectError(shift) : std::string("missing"));

    const int blur = run.addFilter("example_blur", read("isf_blur.fs"));
    run.clearAll();
    run.place(0, split);
    run.useEffect(0, blur);
    run.step(3);
    run.expect("example isf_blur.fs mixes both sides at the edge", run.centre(), 128, 0, 128, 16);
    run.record("example isf_blur.fs has no error", blur >= 0 && run.engine().effectError(blur).empty(), blur >= 0 ? run.engine().effectError(blur) : std::string("missing"));

    const int trails = run.addFilter("example_trails", read("isf_trails.fs"));
    run.clearAll();
    run.place(0, blue);
    run.useEffect(0, trails);
    run.step(3);
    run.expect("example isf_trails.fs builds up a trail", run.centre(), 0, 0, 223, 6);
    run.record("example isf_trails.fs has no error", trails >= 0 && run.engine().effectError(trails).empty(), trails >= 0 ? run.engine().effectError(trails) : std::string("missing"));
    run.clearAll();
    run.step(2);
}


const char* kGraphStripes = "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; float s = step(0.5, fract(uv.x * 8.0 + uv.y * 4.0)); c = vec4(vec3(s, 0.3 + 0.4 * s, 1.0 - s), 1.0); }\n";
const char* kGraphAlpha = "#pragma nyrexvj alpha\nvoid mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(uv.x * 0.8, 0.4, uv.y, 0.25 + 0.5 * uv.x); }\n";

struct GraphClips {
    int plain = -1;
    int stripes = -1;
    int alpha = -1;
    int bar = -1;
};

GraphClips graphClips(Runner& run) {
    GraphClips clips;
    clips.plain = run.add("graph_plain", kSinglePassBench);
    clips.stripes = run.add("graph_stripes", kGraphStripes);
    clips.alpha = run.add("graph_alpha", kGraphAlpha);
    clips.bar = run.add("graph_bar", kFbBar);
    return clips;
}

struct GraphShot {
    std::vector<unsigned char> off;
    std::vector<unsigned char> on;
    int width = 0;
    int height = 0;
    GraphStats stats;
    bool ok = false;
};

GraphShot graphPair(Runner& run) {
    Engine& engine = run.engine();
    GraphShot shot;
    engine.setAudio(virtualAudio(engine.beat(), engine.time()));
    engine.update(1.0 / 60.0);
    int width = 0;
    int height = 0;
    engine.setGraphOptimizer(false);
    engine.render();
    glFinish();
    const bool offOk = engine.readOutput(shot.off, width, height);
    engine.setGraphOptimizer(true);
    engine.render();
    glFinish();
    shot.stats = engine.graphStats();
    int onWidth = 0;
    int onHeight = 0;
    const bool onOk = engine.readOutput(shot.on, onWidth, onHeight);
    shot.width = width;
    shot.height = height;
    shot.ok = offOk && onOk && width == onWidth && height == onHeight && shot.off.size() == shot.on.size() && !shot.on.empty();
    return shot;
}

int graphDifferences(const GraphShot& shot, int* largest = nullptr) {
    int count = 0;
    int worst = 0;
    const size_t n = std::min(shot.on.size(), shot.off.size());
    for (size_t i = 0; i < n; ++i) {
        const int d = std::abs(static_cast<int>(shot.on[i]) - static_cast<int>(shot.off[i]));
        count += d != 0 ? 1 : 0;
        worst = std::max(worst, d);
    }
    if (largest) {
        *largest = worst;
    }
    return count + static_cast<int>(shot.on.size() > shot.off.size() ? shot.on.size() - n : shot.off.size() - n);
}

int graphAlphaMinimum(const GraphShot& shot) {
    int lowest = 255;
    for (size_t i = 3; i < shot.on.size(); i += 4) {
        lowest = std::min(lowest, static_cast<int>(shot.on[i]));
    }
    return lowest;
}

int graphLit(const GraphShot& shot) {
    int count = 0;
    for (size_t i = 0; i + 3 < shot.on.size(); i += 4) {
        count += (shot.on[i] + shot.on[i + 1] + shot.on[i + 2]) > 30 ? 1 : 0;
    }
    return count;
}

void graphDefaults(Runner& run) {
    Engine& engine = run.engine();
    engine.setGraphOptimizer(true);
    engine.setColorStage(false, nullptr, 1.0f);
    engine.setQuality(QualityFull, 16.0);
    engine.blackout = false;
    engine.holdOutput = false;
    engine.paused = false;
    feedbackDefaults(run);
    run.clearAll();
    engine.setResolutionIndex(1);
    engine.comp.deckSettings = DeckSettings{};
    engine.comp.deckSettings.syncBeats = false;
    engine.comp.deckSettings.seconds = 600.0f;
    if (engine.comp.activeDeck != 0) {
        engine.switchDeck(0);
    }
}

void graphCheck(Runner& run, const std::string& name, int expectedFinalSkips, int expectedCopySkips, bool needLight = true) {
    GraphShot shot = graphPair(run);
    int largest = 0;
    const int differing = shot.ok ? graphDifferences(shot, &largest) : -1;
    const bool skipsOk = (expectedFinalSkips < 0 || static_cast<int>(shot.stats.finalSkipped) == expectedFinalSkips) && (expectedCopySkips < 0 || static_cast<int>(shot.stats.copiesSkipped) == expectedCopySkips);
    run.record("graph_" + name, shot.ok && differing == 0 && skipsOk && (!needLight || graphLit(shot) > 0),
        "bytes that differ " + std::to_string(differing) + ", largest " + std::to_string(largest) + ", final passes skipped " + std::to_string(shot.stats.finalSkipped) + " (wanted " + std::to_string(expectedFinalSkips) + "), copies skipped " + std::to_string(shot.stats.copiesSkipped) + " (wanted " + std::to_string(expectedCopySkips) + "), lit pixels " + std::to_string(graphLit(shot)));
}

void testGraphBasics(Runner& run, const GraphClips& clips) {
    Engine& engine = run.engine();
    graphDefaults(run);
    run.place(0, clips.plain);
    run.step(3);
    graphCheck(run, "single_layer", 1, 0);

    run.place(1, clips.stripes);
    run.place(2, clips.alpha);
    run.place(3, clips.plain);
    engine.comp.layers[0].opacity = 0.9f;
    engine.comp.layers[1].opacity = 0.7f;
    engine.comp.layers[2].opacity = 0.5f;
    engine.comp.layers[3].opacity = 0.3f;
    run.step(3);
    graphCheck(run, "four_layers_normal", 1, 0);
    GraphShot alphaShot = graphPair(run);
    run.record("graph_alpha_is_kept_at_255", alphaShot.ok && graphAlphaMinimum(alphaShot) == 255, "lowest alpha " + std::to_string(graphAlphaMinimum(alphaShot)));

    run.clearAll();
    engine.setResolutionIndex(1);
    for (int mode = 0; mode < BlendModeCount; ++mode) {
        engine.comp.layers[0] = Layer{};
        engine.comp.layers[1] = Layer{};
        run.place(0, clips.plain);
        run.place(1, clips.stripes);
        engine.comp.layers[1].blend = mode;
        engine.comp.layers[1].opacity = 0.8f;
        run.step(2);
        graphCheck(run, "blend_mode_" + std::to_string(mode), 1, 0);
    }

    const std::vector<std::array<int, 3>> orders = {
        {BlendNormal, BlendDifference, BlendExclusion},
        {BlendSubtract, BlendNormal, BlendLighten},
        {BlendDifference, BlendDifference, BlendDifference},
        {BlendAdd, BlendSubtract, BlendMultiply},
        {BlendSoftLight, BlendOverlay, BlendNormal},
    };
    int index = 0;
    for (const std::array<int, 3>& order : orders) {
        run.clearAll();
        engine.setResolutionIndex(1);
        run.place(0, clips.plain);
        run.place(1, clips.stripes);
        run.place(2, clips.alpha);
        for (int l = 0; l < 3; ++l) {
            engine.comp.layers[static_cast<size_t>(l)].blend = order[static_cast<size_t>(l)];
            engine.comp.layers[static_cast<size_t>(l)].opacity = 0.85f - 0.2f * static_cast<float>(l);
        }
        run.step(2);
        graphCheck(run, "blend_order_" + std::to_string(index++), 1, 0);
    }

    run.clearAll();
    engine.setResolutionIndex(1);
    run.place(0, clips.plain);
    run.place(1, clips.stripes);
    run.place(2, clips.plain);
    engine.comp.layers[1].opacity = 0.0f;
    engine.comp.layers[1].blend = BlendDifference;
    run.step(2);
    graphCheck(run, "hidden_layer_with_swap_blend", 1, 0);
    engine.comp.layers[2].bypass = true;
    run.step(2);
    graphCheck(run, "bypassed_layer", 1, 0);
    engine.comp.layers[2].bypass = false;
    engine.comp.layers[0].solo = true;
    run.step(2);
    graphCheck(run, "solo_layer", 1, 0);
    run.clearAll();
    engine.setResolutionIndex(1);
    run.step(2);
    GraphShot empty = graphPair(run);
    run.record("graph_empty_composition", empty.ok && graphDifferences(empty) == 0 && empty.stats.finalSkipped == 1, "differences " + std::to_string(graphDifferences(empty)) + ", skipped " + std::to_string(empty.stats.finalSkipped));
}

void testGraphFallbacks(Runner& run, const GraphClips& clips) {
    Engine& engine = run.engine();
    graphDefaults(run);
    run.place(0, clips.plain);
    run.place(1, clips.stripes);
    engine.comp.layers[1].blend = BlendOverlay;
    engine.comp.layers[1].opacity = 0.6f;
    run.step(3);

    const std::vector<std::pair<std::string, ColorGrade>> grades = {
        {"hue", {0.2f, 1.0f, 0.0f, 1.0f}},
        {"saturation", {0.0f, 0.5f, 0.0f, 1.0f}},
        {"brightness", {0.0f, 1.0f, 0.1f, 1.0f}},
        {"contrast", {0.0f, 1.0f, 0.0f, 1.4f}},
        {"all", {-0.1f, 1.3f, -0.05f, 0.8f}},
        {"tiny_hue", {0.00002f, 1.0f, 0.0f, 1.0f}},
    };
    for (const auto& entry : grades) {
        engine.comp.master.grade = entry.second;
        run.step(2);
        graphCheck(run, "master_grade_" + entry.first, 0, 0);
    }
    engine.comp.master.grade = ColorGrade{};

    for (const float level : {0.0f, 0.4f, 0.9f}) {
        engine.comp.master.level = level;
        run.step(2);
        graphCheck(run, "master_level_" + std::to_string(static_cast<int>(level * 10.0f)), 0, 0, level > 0.0f);
    }
    engine.comp.master.level = 1.0f;

    engine.blackout = true;
    run.step(2);
    {
        GraphShot shot = graphPair(run);
        run.record("graph_blackout", shot.ok && graphDifferences(shot) == 0 && shot.stats.finalSkipped == 0, "differences " + std::to_string(graphDifferences(shot)) + ", skipped " + std::to_string(shot.stats.finalSkipped));
    }
    engine.blackout = false;
    engine.holdOutput = true;
    run.step(2);
    {
        GraphShot shot = graphPair(run);
        run.record("graph_hold_output", shot.ok && graphDifferences(shot) == 0 && shot.stats.finalSkipped == 0, "differences " + std::to_string(graphDifferences(shot)) + ", skipped " + std::to_string(shot.stats.finalSkipped));
    }
    engine.holdOutput = false;

    EffectSlot& slot = engine.comp.master.fx[0];
    setEffect(slot, effectIndexByName("Invert"));
    slot.enabled = true;
    run.step(2);
    graphCheck(run, "master_effect", 0, 0);
    slot.enabled = false;
    run.step(2);
    graphCheck(run, "master_effect_off", 1, 0);
    slot.enabled = true;
    setEffect(engine.comp.master.fx[3], effectIndexByName("Mirror"));
    run.step(2);
    graphCheck(run, "master_two_effects", 0, 0);
    engine.comp.master.fx = {};
    EffectSlot& layerSlot = engine.comp.layers[0].fx[0];
    setEffect(layerSlot, effectIndexByName("Kaleidoscope"));
    layerSlot.enabled = true;
    run.step(2);
    graphCheck(run, "layer_effect", 1, 0);
    engine.comp.layers[0].fx = {};

    engine.setQuality(QualityPerformance, 16.0);
    run.step(8);
    graphCheck(run, "reduced_layer_resolution", 1, 0);
    engine.setQuality(QualityFull, 16.0);
    run.step(8);

    for (const int resolution : {0, 2, 1}) {
        engine.setResolutionIndex(resolution);
        run.place(0, clips.plain);
        run.place(1, clips.stripes);
        run.step(3);
        graphCheck(run, "resolution_" + std::to_string(resolution), 1, 0);
    }
}

CubeLut graphLut(int size, bool invert) {
    CubeLut lut;
    lut.title = invert ? "graph invert" : "graph tint";
    lut.size = size;
    lut.rgb.resize(lut.entries() * 3);
    size_t at = 0;
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const float fr = static_cast<float>(r) / static_cast<float>(size - 1);
                const float fg = static_cast<float>(g) / static_cast<float>(size - 1);
                const float fb = static_cast<float>(b) / static_cast<float>(size - 1);
                lut.rgb[at++] = invert ? 1.0f - fr : std::min(1.0f, fr * 1.2f);
                lut.rgb[at++] = invert ? 1.0f - fg : fg * 0.8f;
                lut.rgb[at++] = invert ? 1.0f - fb : std::min(1.0f, fb + 0.1f);
            }
        }
    }
    return lut;
}

void testGraphColourStage(Runner& run, const GraphClips& clips) {
    Engine& engine = run.engine();
    graphDefaults(run);
    run.place(0, clips.plain);
    run.place(1, clips.stripes);
    engine.comp.layers[1].blend = BlendDifference;
    engine.comp.layers[1].opacity = 0.7f;
    const CubeLut invert = graphLut(9, true);
    const CubeLut tint = graphLut(17, false);

    engine.setColorStage(true, &invert, 1.0f);
    run.step(3);
    graphCheck(run, "colour_direct", 1, 1);
    engine.setColorStage(true, &tint, 0.5f);
    run.step(3);
    graphCheck(run, "colour_direct_half_strength", 1, 1);

    engine.comp.master.grade = ColorGrade{0.1f, 1.2f, 0.0f, 1.1f};
    run.step(2);
    graphCheck(run, "colour_graded_master", 0, 1);
    engine.comp.master.grade = ColorGrade{};
    engine.comp.master.level = 0.6f;
    run.step(2);
    graphCheck(run, "colour_dimmed_master", 0, 1);
    engine.comp.master.level = 1.0f;
    setEffect(engine.comp.master.fx[0], effectIndexByName("Invert"));
    engine.comp.master.fx[0].enabled = true;
    run.step(2);
    graphCheck(run, "colour_master_effect", 0, 1);
    engine.comp.master.fx = {};

    engine.comp.layers[1].blend = BlendNormal;
    run.step(2);
    graphCheck(run, "colour_even_swaps", 1, 1);

    engine.setColorStage(true, &invert, 1.0f);
    run.step(3);
    {
        engine.setGraphOptimizer(false);
        engine.render();
        const std::uint64_t legacy = engine.colorStageBytes();
        engine.setGraphOptimizer(true);
        engine.render();
        const std::uint64_t optimized = engine.colorStageBytes();
        run.record("graph_colour_stage_memory",
            legacy == colorStageBytes(engine.width(), engine.height(), invert.size) && optimized == cubeGpuBytes(invert.size) && optimized < legacy,
            "optimizer off " + std::to_string(legacy) + " B, on " + std::to_string(optimized) + " B");
    }
    engine.setColorStage(false, nullptr, 1.0f);
    run.step(2);
    graphCheck(run, "colour_stage_off_again", 1, 0);
}

void testGraphTransitions(Runner& run, const GraphClips& clips) {
    Engine& engine = run.engine();
    const std::string custom = run.addTransition("graph_tr_mix", kCrossMix);
    const CubeLut tint = graphLut(9, false);

    const auto start = [&](int builtin, const std::string& customName) {
        graphDefaults(run);
        run.place(0, clips.plain);
        run.place(1, clips.alpha);
        engine.comp.layers[1].opacity = 0.8f;
        run.step(3);
        engine.comp.deckSettings.transition = builtin;
        engine.comp.deckSettings.customTransition = customName;
        engine.comp.deckSettings.seconds = 600.0f;
        engine.comp.deckSettings.syncBeats = false;
    };
    const auto enter = [&]() {
        engine.switchDeck(1);
        run.place(0, clips.stripes);
        run.place(1, clips.plain);
        engine.comp.layers[1].blend = BlendOverlay;
        engine.comp.layers[1].opacity = 0.6f;
        run.step(4);
    };

    start(TransitionFade, "");
    enter();
    run.record("graph_transition_running", engine.transitioning(), "transitioning");
    graphCheck(run, "transition_fade_neutral", 2, 0);

    engine.comp.master.grade = ColorGrade{0.0f, 1.3f, 0.0f, 1.0f};
    run.step(2);
    graphCheck(run, "transition_current_graded", 1, 0);
    engine.comp.master.grade = ColorGrade{};
    setEffect(engine.comp.master.fx[1], effectIndexByName("Mirror"));
    engine.comp.master.fx[1].enabled = true;
    run.step(2);
    graphCheck(run, "transition_current_with_effect", 1, 0);
    engine.comp.master.fx = {};

    engine.setColorStage(true, &tint, 1.0f);
    run.step(2);
    graphCheck(run, "transition_with_colour_stage", 2, 1);
    engine.setColorStage(false, nullptr, 1.0f);

    engine.switchDeck(0);
    run.step(2);
    run.record("graph_transition_switch_back_frozen", engine.transitioning(), "still transitioning");
    graphCheck(run, "transition_after_switch_in_the_middle", -1, -1);

    for (const int type : {TransitionRadial, TransitionGlitch}) {
        start(type, "");
        enter();
        graphCheck(run, "transition_builtin_" + std::to_string(type), 2, 0);
    }

    if (!custom.empty()) {
        start(TransitionFade, custom);
        enter();
        graphCheck(run, "transition_custom_mix", 2, 0);
        engine.setColorStage(true, &tint, 0.8f);
        run.step(2);
        graphCheck(run, "transition_custom_with_colour_stage", 2, 1);
        engine.setColorStage(false, nullptr, 1.0f);
    }

    start(TransitionFade, "");
    engine.comp.master.grade = ColorGrade{0.1f, 1.0f, 0.0f, 1.0f};
    enter();
    graphCheck(run, "transition_new_deck_neutral_old_graded", -1, 0);

    engine.comp.deckSettings.seconds = 0.2f;
    run.step(60);
    engine.switchDeck(0);
    run.step(40);
    graphDefaults(run);
}

void testGraphSequences(Runner& run, const GraphClips& clips) {
    Engine& engine = run.engine();
    struct Scene {
        std::string name;
        std::function<void()> build;
    };
    const std::vector<Scene> scenes = {
        {"layer_trails", [&]() {
             run.place(0, clips.bar);
             feedbackEffect(run, 0, "Trails", 0.7f, 0.5f, 0.5f, 0.4f);
         }},
        {"master_trails", [&]() {
             run.place(0, clips.bar);
             setEffect(engine.comp.master.fx[0], effectIndexByName("Trails"));
             engine.comp.master.fx[0].enabled = true;
             engine.comp.master.fx[0].params = {0.6f, 0.5f, 0.5f, 0.4f};
         }},
        {"hidden_feedback_layer_with_swap_blend", [&]() {
             run.place(0, clips.bar);
             run.place(1, clips.stripes);
             feedbackEffect(run, 1, "Trails", 0.8f, 0.5f, 0.5f, 0.4f);
             engine.comp.layers[1].opacity = 0.0f;
             engine.comp.layers[1].blend = BlendDifference;
         }},
        {"three_layers_two_feedback", [&]() {
             run.place(0, clips.bar);
             run.place(1, clips.stripes);
             run.place(2, clips.plain);
             feedbackEffect(run, 0, "Trails", 0.6f, 0.5f, 0.5f, 0.4f);
             feedbackEffect(run, 2, "Trails", 0.5f, 0.5f, 0.5f, 0.4f);
             engine.comp.layers[1].blend = BlendExclusion;
             engine.comp.layers[1].opacity = 0.5f;
             engine.comp.layers[2].opacity = 0.4f;
         }},
    };
    constexpr int kFrames = 20;
    for (const Scene& scene : scenes) {
        std::array<std::vector<std::vector<unsigned char>>, 2> runs;
        for (int mode = 0; mode < 2; ++mode) {
            graphDefaults(run);
            engine.setGraphOptimizer(mode == 1);
            scene.build();
            engine.resetFeedback();
            run.step(1);
            engine.resetFeedback();
            for (int frame = 0; frame < kFrames; ++frame) {
                run.step(1);
                std::vector<unsigned char> pixels;
                int width = 0;
                int height = 0;
                engine.readOutput(pixels, width, height);
                runs[static_cast<size_t>(mode)].push_back(std::move(pixels));
            }
        }
        int differing = 0;
        int firstBad = -1;
        for (int frame = 0; frame < kFrames; ++frame) {
            if (runs[0][static_cast<size_t>(frame)] != runs[1][static_cast<size_t>(frame)] || runs[0][static_cast<size_t>(frame)].empty()) {
                ++differing;
                firstBad = firstBad < 0 ? frame : firstBad;
            }
        }
        run.record("graph_sequence_" + scene.name, differing == 0, std::to_string(differing) + " of " + std::to_string(kFrames) + " frames differ, first " + std::to_string(firstBad));
    }
    graphDefaults(run);
}

void testGraphSwitching(Runner& run, const GraphClips& clips) {
    Engine& engine = run.engine();
    graphDefaults(run);
    run.place(0, clips.plain);
    run.step(3);
    engine.setGraphOptimizer(true);
    engine.resetGraphStats();
    run.step(10);
    const GraphStats on = engine.graphStats();
    run.record("graph_counts_skipped_passes", on.frames == 10 && on.finalSkipped == 10 && on.copiesSkipped == 0, "frames " + std::to_string(on.frames) + ", skipped " + std::to_string(on.finalSkipped));
    engine.setGraphOptimizer(false);
    run.step(10);
    const GraphStats off = engine.graphStats();
    run.record("graph_off_skips_nothing", off.frames == 10 && off.finalSkipped == 0 && off.copiesSkipped == 0, "frames " + std::to_string(off.frames) + ", skipped " + std::to_string(off.finalSkipped));
    engine.setGraphOptimizer(true);
    engine.setGraphOptimizer(true);
    run.step(2);
    run.record("graph_switch_resets_the_counters_once", engine.graphStats().frames == 2, "frames " + std::to_string(engine.graphStats().frames));
    for (int i = 0; i < 12; ++i) {
        engine.setGraphOptimizer(i % 2 == 0);
        run.step(1);
    }
    graphCheck(run, "after_switching_back_and_forth", 1, 0);
    graphDefaults(run);
}

void testGraphOptimizer(Runner& run) {
    const GraphClips clips = graphClips(run);
    if (clips.plain < 0 || clips.stripes < 0 || clips.alpha < 0 || clips.bar < 0) {
        run.record("graph_clips", false, "the test clips did not load");
        return;
    }
    testGraphBasics(run, clips);
    testGraphFallbacks(run, clips);
    testGraphColourStage(run, clips);
    testGraphTransitions(run, clips);
    testGraphSequences(run, clips);
    testGraphSwitching(run, clips);
    run.clearAll();
    graphDefaults(run);
}

struct BenchCase {
    std::string name;
    std::string clipText;
    std::string filterText;
};

const char* kHeavyTransition =
    "#pragma nyrexvj transition\n"
    "void main() {\n"
    "    vec4 a = vec4(0.0); vec4 b = vec4(0.0);\n"
    "    for (int i = 0; i < 16; ++i) {\n"
    "        float f = float(i) / 16.0 - 0.5;\n"
    "        vec2 o = vec2(f, f * 0.5) * 0.02 * uProgress;\n"
    "        a += colFrom(vUv + o); b += colTo(vUv - o);\n"
    "    }\n"
    "    fragColor = mix(a, b, smoothstep(0.0, 1.0, uProgress)) / 16.0;\n"
    "}\n";

const char* kIrisTransition =
    "#pragma nyrexvj transition\n"
    "#pragma nyrexvj param softness float \"Softness\" 0 0.5 0.12\n"
    "void main() {\n"
    "    vec2 p = vUv - 0.5;\n"
    "    p.x *= uRes.x / uRes.y;\n"
    "    float radius = uProgress * 1.2;\n"
    "    float mask = 1.0 - ss(radius - softness, radius + softness, length(p));\n"
    "    fragColor = mix(colFrom(vUv), colTo(vUv), mask);\n"
    "}\n";

void benchTransition(Runner& run, json& result, const std::string& name, int frames, const std::string& custom, int builtin, int from, int to) {
    run.clearAll();
    run.engine().setResolutionIndex(1);
    run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
    DeckSettings& settings = run.engine().comp.deckSettings;
    settings = DeckSettings{};
    settings.transition = builtin;
    settings.customTransition = custom;
    settings.seconds = static_cast<float>(frames + 30) / 60.0f;
    settings.syncBeats = false;
    run.engine().switchDeck(0);
    run.place(0, from);
    double prewarmWorst = 0.0;
    for (int i = 0; i < 6; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        run.step(1);
        prewarmWorst = std::max(prewarmWorst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    }
    const bool steady = custom.empty() && builtin < 0;
    double startWorst = 0.0;
    if (!steady) {
        run.engine().switchDeck(1);
        run.place(0, to);
        for (int i = 0; i < 3; ++i) {
            const auto begin = std::chrono::steady_clock::now();
            run.step(1);
            startWorst = std::max(startWorst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        }
    }
    double total = 0.0;
    double worst = 0.0;
    for (int i = 0; i < frames; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        run.step(1);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        total += ms;
        worst = std::max(worst, ms);
    }
    json item;
    item["name"] = name;
    item["averageMs"] = total / static_cast<double>(std::max(frames, 1));
    item["worstMs"] = worst;
    item["transitioning"] = run.engine().transitioning();
    item["firstFramesAfterChoosingWorstMs"] = prewarmWorst;
    item["firstFramesOfTheSwitchWorstMs"] = startWorst;
    result.push_back(std::move(item));
    logLine("Shader benchmark " + name + ": " + std::to_string(total / std::max(frames, 1)) + " ms per frame");
    run.step(60);
    run.engine().switchDeck(0);
    run.step(static_cast<int>(settings.seconds * 60.0f) + 30);
}

void benchFeedbackCase(Runner& run, json& result, const std::string& name, int frames, const FeedbackClips& clips, const char* effect, int layers, bool frozen) {
    Engine& engine = run.engine();
    run.clearAll();
    feedbackDefaults(run);
    engine.setResolutionIndex(1);
    run.step(static_cast<int>(kFeedbackPoolIdleFrames) + 10);
    engine.setFeedbackFrozen(frozen);
    for (int layer = 0; layer < layers; ++layer) {
        if (effect) {
            feedbackEffect(run, layer, effect, 0.6f, 0.5f, 0.5f, 0.4f);
        }
        run.place(layer, layer % 2 == 0 ? clips.grey : clips.bar);
    }
    const auto first = std::chrono::steady_clock::now();
    run.step(1);
    const double firstMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - first).count();
    run.step(6);
    double total = 0.0;
    double worst = 0.0;
    for (int i = 0; i < frames; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        run.step(1);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        total += ms;
        worst = std::max(worst, ms);
    }
    json item;
    item["name"] = name;
    item["averageMs"] = total / static_cast<double>(std::max(frames, 1));
    item["worstMs"] = worst;
    item["firstFrameMs"] = firstMs;
    item["feedbackBytes"] = engine.feedbackStats().total();
    result.push_back(std::move(item));
    logLine("Shader benchmark " + name + ": " + std::to_string(total / std::max(frames, 1)) + " ms per frame, first frame " + std::to_string(firstMs) + " ms, " + std::to_string(engine.feedbackStats().total()) + " feedback bytes");
    engine.setFeedbackFrozen(false);
}

void benchFeedbackLab(Runner& run, json& result, int frames) {
    const FeedbackClips clips = feedbackClips(run);
    if (clips.grey < 0 || clips.bar < 0) {
        return;
    }
    benchFeedbackCase(run, result, "bench_feedback_none", frames, clips, nullptr, 1, false);
    benchFeedbackCase(run, result, "bench_feedback_trails", frames, clips, "Trails", 1, false);
    benchFeedbackCase(run, result, "bench_feedback_time_echo", frames, clips, "Time Echo", 1, false);
    benchFeedbackCase(run, result, "bench_feedback_motion_smear", frames, clips, "Motion Smear", 1, false);
    benchFeedbackCase(run, result, "bench_feedback_transform", frames, clips, "Feedback Transform", 1, false);
    benchFeedbackCase(run, result, "bench_feedback_accumulate", frames, clips, "Accumulate", 1, false);
    benchFeedbackCase(run, result, "bench_feedback_time_echo_frozen", frames, clips, "Time Echo", 1, true);
    benchFeedbackCase(run, result, "bench_feedback_none_four_layers", frames, clips, nullptr, 4, false);
    benchFeedbackCase(run, result, "bench_feedback_time_echo_four_layers", frames, clips, "Time Echo", 4, false);
    run.clearAll();
    feedbackDefaults(run);
}

double graphPercentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const size_t low = static_cast<size_t>(std::floor(position));
    const size_t high = static_cast<size_t>(std::ceil(position));
    return values[low] + (values[high] - values[low]) * (position - static_cast<double>(low));
}

json graphStatistics(const std::vector<double>& samples) {
    json item;
    double total = 0.0;
    for (const double value : samples) {
        total += value;
    }
    item["samples"] = samples.size();
    item["averageMs"] = samples.empty() ? 0.0 : total / static_cast<double>(samples.size());
    item["p50Ms"] = graphPercentile(samples, 0.50);
    item["p95Ms"] = graphPercentile(samples, 0.95);
    item["p99Ms"] = graphPercentile(samples, 0.99);
    item["worstMs"] = samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end());
    return item;
}

void benchGraphCase(Runner& run, json& result, const std::string& name, int frames, const std::function<void()>& build) {
    Engine& engine = run.engine();
    graphDefaults(run);
    build();
    run.step(10);
    constexpr int kRounds = 4;
    const int block = std::max(frames / kRounds, 20);
    std::array<std::vector<double>, 2> samples;
    std::array<size_t, 2> total{};
    std::array<size_t, 2> targets{};
    std::array<GraphStats, 2> stats{};
    for (int round = 0; round < kRounds; ++round) {
        for (int mode = 0; mode < 2; ++mode) {
            const bool on = (round % 2 == 0) ? mode == 1 : mode == 0;
            engine.setGraphOptimizer(on);
            run.step(4);
            for (int i = 0; i < block; ++i) {
                const auto begin = std::chrono::steady_clock::now();
                run.step(1);
                samples[on ? 1 : 0].push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
            }
            const GpuMemory memory = engine.gpuMemory();
            total[on ? 1 : 0] = memory.total();
            targets[on ? 1 : 0] = memory.targets;
            stats[on ? 1 : 0] = engine.graphStats();
        }
    }
    const GraphShot shot = graphPair(run);
    json item;
    item["name"] = name;
    item["optimizerOff"] = graphStatistics(samples[0]);
    item["optimizerOn"] = graphStatistics(samples[1]);
    item["optimizerOff"]["gpuMemoryBytes"] = total[0];
    item["optimizerOn"]["gpuMemoryBytes"] = total[1];
    item["optimizerOff"]["targetBytes"] = targets[0];
    item["optimizerOn"]["targetBytes"] = targets[1];
    item["optimizerOn"]["finalPassesSkippedPerFrame"] = stats[1].frames > 0 ? static_cast<double>(stats[1].finalSkipped) / static_cast<double>(stats[1].frames) : 0.0;
    item["optimizerOn"]["copiesSkippedPerFrame"] = stats[1].frames > 0 ? static_cast<double>(stats[1].copiesSkipped) / static_cast<double>(stats[1].frames) : 0.0;
    item["pixelDifferences"] = shot.ok ? graphDifferences(shot) : -1;
    result.push_back(std::move(item));
    logLine("Shader benchmark " + name + ": off " + std::to_string(result.back()["optimizerOff"]["averageMs"].get<double>()) + " ms (p99 " + std::to_string(result.back()["optimizerOff"]["p99Ms"].get<double>()) + "), on " + std::to_string(result.back()["optimizerOn"]["averageMs"].get<double>()) + " ms (p99 " + std::to_string(result.back()["optimizerOn"]["p99Ms"].get<double>()) + "), pixel differences " + std::to_string(result.back()["pixelDifferences"].get<int>()));
    engine.setColorStage(false, nullptr, 1.0f);
    graphDefaults(run);
}

void benchGraphLab(Runner& run, json& result, int frames) {
    const GraphClips clips = graphClips(run);
    if (clips.plain < 0 || clips.stripes < 0 || clips.alpha < 0) {
        return;
    }
    Engine& engine = run.engine();
    const int samples = std::max(frames, 160);
    const CubeLut lut = graphLut(17, false);
    benchGraphCase(run, result, "bench_graph_one_layer", samples, [&]() { run.place(0, clips.plain); });
    const auto four = [&]() {
        run.place(0, clips.plain);
        run.place(1, clips.stripes);
        run.place(2, clips.alpha);
        run.place(3, clips.plain);
        engine.comp.layers[1].blend = BlendOverlay;
        engine.comp.layers[1].opacity = 0.7f;
        engine.comp.layers[2].blend = BlendNormal;
        engine.comp.layers[2].opacity = 0.6f;
        engine.comp.layers[3].blend = BlendDifference;
        engine.comp.layers[3].opacity = 0.4f;
    };
    benchGraphCase(run, result, "bench_graph_four_layers", samples, four);
    benchGraphCase(run, result, "bench_graph_four_layers_lut", samples, [&]() {
        four();
        engine.setColorStage(true, &lut, 1.0f);
    });
    benchGraphCase(run, result, "bench_graph_four_layers_master_effect", samples, [&]() {
        four();
        setEffect(engine.comp.master.fx[0], effectIndexByName("Invert"));
        engine.comp.master.fx[0].enabled = true;
    });
    benchGraphCase(run, result, "bench_graph_transition", samples, [&]() {
        four();
        engine.comp.deckSettings.transition = TransitionFade;
        engine.comp.deckSettings.seconds = 600.0f;
        engine.comp.deckSettings.syncBeats = false;
        engine.switchDeck(1);
        run.place(0, clips.stripes);
        run.place(1, clips.plain);
        engine.comp.layers[1].blend = BlendOverlay;
        engine.comp.layers[1].opacity = 0.5f;
    });
    engine.setColorStage(false, nullptr, 1.0f);
    graphDefaults(run);
}

json benchmark(Runner& run, int frames) {
    json result = json::array();
    run.engine().setResolutionIndex(1);
    const std::string isfSingle = "/*{\"INPUTS\":[]}*/\nvoid main() { vec2 uv = isf_FragNormCoord; gl_FragColor = vec4(uv, 0.5 + 0.5 * sin(TIME), 1.0); }\n";
    const std::string isfParams =
        "/*{\"INPUTS\":[{\"NAME\":\"speed\",\"TYPE\":\"float\",\"MIN\":0.0,\"MAX\":4.0,\"DEFAULT\":1.0},{\"NAME\":\"tint\",\"TYPE\":\"color\",\"DEFAULT\":[1.0,0.5,0.2,1.0]}]}*/\n"
        "void main() { vec2 uv = isf_FragNormCoord; gl_FragColor = vec4(tint.rgb * (0.5 + 0.5 * sin(TIME * speed + uv.x * 6.0)), 1.0); }\n";
    const std::vector<BenchCase> cases = {
        {"bench_single", kSinglePassBench, ""},
        {"bench_alpha", std::string("#pragma nyrexvj alpha\n") + kSinglePassBench, ""},
        {"bench_two_buffers", kOrder, ""},
        {"bench_four_buffers", kFourBuffers, ""},
        {"bench_isf_single", isfSingle, ""},
        {"bench_isf_params", isfParams, ""},
        {"bench_isf_accumulate", kIsfAccumulate, ""},
        {"bench_filter_swap", kSinglePassBench, kIsfSwap},
        {"bench_filter_blur", kSinglePassBench, kIsfBlur},
        {"bench_filter_trail", kSinglePassBench, kIsfTrail},
    };
    for (const BenchCase& entry : cases) {
        const int clip = run.add(entry.name, entry.clipText);
        if (clip < 0) {
            continue;
        }
        int effect = -1;
        if (!entry.filterText.empty()) {
            effect = run.addFilter(entry.name + "_filter", entry.filterText);
            if (effect < 0) {
                continue;
            }
        }
        run.clearAll();
        run.engine().setResolutionIndex(1);
        run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
        run.place(0, clip);
        if (effect >= 0) {
            run.useEffect(0, effect);
        }
        run.step(6);
        double total = 0.0;
        double worst = 0.0;
        for (int i = 0; i < frames; ++i) {
            const auto begin = std::chrono::steady_clock::now();
            run.step(1);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            total += ms;
            worst = std::max(worst, ms);
        }
        json item;
        item["name"] = entry.name;
        item["averageMs"] = total / static_cast<double>(std::max(frames, 1));
        item["worstMs"] = worst;
        item["bufferBytes"] = run.engine().shaderBufferBytes();
        result.push_back(std::move(item));
        logLine("Shader benchmark " + entry.name + ": " + std::to_string(total / std::max(frames, 1)) + " ms per frame");
    }
    const int trFrom = run.add("bench_tr_from", kSinglePassBench);
    const int trTo = run.add("bench_tr_to", std::string(kSinglePassBench) + "\n");
    const std::string trMix = run.addTransition("bench_tr_mix", kCrossMix);
    const std::string trIris = run.addTransition("bench_tr_iris", kIrisTransition);
    const std::string trHeavy = run.addTransition("bench_tr_heavy", kHeavyTransition);
    if (trFrom >= 0 && trTo >= 0) {
        benchTransition(run, result, "bench_transition_none", frames, std::string(), -1, trFrom, trTo);
        benchTransition(run, result, "bench_transition_builtin_fade", frames, std::string(), TransitionFade, trFrom, trTo);
        benchTransition(run, result, "bench_transition_builtin_radial", frames, std::string(), TransitionRadial, trFrom, trTo);
        benchTransition(run, result, "bench_transition_custom_mix", frames, trMix, TransitionFade, trFrom, trTo);
        benchTransition(run, result, "bench_transition_custom_iris", frames, trIris, TransitionFade, trFrom, trTo);
        benchTransition(run, result, "bench_transition_custom_heavy16", frames, trHeavy, TransitionFade, trFrom, trTo);
    }
    benchFeedbackLab(run, result, frames);
    benchGraphLab(run, result, frames);
    return result;
}

}

int runShaderSelfTest(Engine& engine, int benchFrames, const std::string& outputPath) {
    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    Runner run(engine, report);
    run.prepare();
    testAlpha(run);
    testPasses(run);
    testMedia(run);
    testErrors(run);
    testReloadAndRelease(run);
    testBudget(run);
    testIsfGenerator(run);
    testIsfFilter(run);
    testIsfFallbacks(run);
    testIsfReload(run);
    testIsfExamples(run);
    testTransitionTypes(run);
    testTransition(run);
    testCustomTransition(run);
    testCustomTransitionSwitching(run);
    testCustomTransitionFallback(run);
    testCustomTransitionAlpha(run);
    testCustomTransitionFeedback(run);
    testCustomTransitionPreview(run);
    testBuiltinsAfterCustom(run);
    testFeedbackLab(run);
    testGraphOptimizer(run);
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["checks"] = run.items();
    report["failed"] = run.failed();
    report["passed"] = run.failed() == 0;
    if (benchFrames > 0) {
        report["benchmark"] = benchmark(run, benchFrames);
        report["benchmarkNote"] = "wall clock per frame at the resolution shown, including CPU rasterisation when no graphics card is present";
        report["benchmarkResolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    }
    run.finish();
    const std::string text = report.dump(2);
    logLine("Shader self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return run.failed() == 0 ? 0 : 1;
}

}

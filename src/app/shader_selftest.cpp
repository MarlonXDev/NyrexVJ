#include "app/shader_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

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
        for (int i = 0; i < count; ++i) {
            engine_.setAudio(virtualAudio(engine_.beat(), engine_.time()));
            engine_.update(1.0 / 60.0);
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

struct BenchCase {
    std::string name;
    std::string clipText;
    std::string filterText;
};

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

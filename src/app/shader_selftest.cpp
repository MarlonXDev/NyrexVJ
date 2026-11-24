#include "app/shader_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "render/clip_library.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

json benchmark(Runner& run, int frames) {
    json result = json::array();
    run.engine().setResolutionIndex(1);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"bench_single", kSinglePassBench},
        {"bench_alpha", std::string("#pragma nyrexvj alpha\n") + kSinglePassBench},
        {"bench_two_buffers", kOrder},
        {"bench_four_buffers", kFourBuffers},
    };
    for (const auto& entry : cases) {
        const int clip = run.add(entry.first, entry.second);
        if (clip < 0) {
            continue;
        }
        run.clearAll();
        run.engine().setResolutionIndex(1);
        run.step(static_cast<int>(kShaderIdleReleaseFrames) + 10);
        run.place(0, clip);
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
        item["name"] = entry.first;
        item["averageMs"] = total / static_cast<double>(std::max(frames, 1));
        item["worstMs"] = worst;
        item["bufferBytes"] = run.engine().shaderBufferBytes();
        result.push_back(std::move(item));
        logLine("Shader benchmark " + entry.first + ": " + std::to_string(total / std::max(frames, 1)) + " ms per frame");
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

#include "app/color_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/color_lut.hpp"
#include "core/color_model.hpp"
#include "core/log.hpp"
#include "media/video_recorder.hpp"
#include "media/video_types.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "render/color_gpu.hpp"
#include "render/engine.hpp"
#include "render/media_player.hpp"
#include "render/shader_source.hpp"
#include "render/video_gpu.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

struct Shot {
    bool ok = false;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    std::string route;
    std::string path;
    std::string error;

    Pixel at(double fx, double fy) const {
        if (width <= 0 || height <= 0) {
            return Pixel{};
        }
        const int x = std::clamp(static_cast<int>(fx * width), 0, width - 1);
        const int y = std::clamp(static_cast<int>(fy * height), 0, height - 1);
        const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
        return Pixel{pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    }
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

std::string show(const Pixel& p) {
    return "(" + std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b) + "," + std::to_string(p.a) + ")";
}

std::string number(double value, int digits = 3) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

const char* kSolid = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.2, 0.5, 0.8, 1.0); }\n";
const char* kRed = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 1.0); }\n";
const char* kHalf = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.5, 0.25, 0.0, 0.5); }\n";
const char* kRamp = "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(uv.x, uv.y, 0.25, 1.0); }\n";

CubeLut makeLut(int size, const char* title, Color3 (*function)(const Color3&)) {
    CubeLut lut;
    lut.size = size;
    lut.title = title;
    const double last = static_cast<double>(size - 1);
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const Color3 out = function({r / last, g / last, b / last});
                lut.rgb.push_back(static_cast<float>(out[0]));
                lut.rgb.push_back(static_cast<float>(out[1]));
                lut.rgb.push_back(static_cast<float>(out[2]));
            }
        }
    }
    return lut;
}

Color3 invertColor(const Color3& c) {
    return {1.0 - c[0], 1.0 - c[1], 1.0 - c[2]};
}

Color3 mixedColor(const Color3& c) {
    return {std::pow(c[0], 1.6) * 0.8 + 0.15 * c[1], 0.5 * c[2] + 0.5 * std::sqrt(c[0]), std::clamp(c[1] * c[1] + 0.3 * c[2], 0.0, 1.0)};
}

class Probe {
public:
    explicit Probe(Engine& engine) : engine_(engine) {}

    Engine& engine() { return engine_; }
    json& summary() { return summary_; }
    json& items() { return items_; }
    int failed() const { return failed_; }
    long long frames() const { return frames_; }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Colour self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void prepare() {
        root_ = fs::temp_directory_path() / "nyrexvj_color_selftest";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "shaders", ec);
        setDataDirectory(root_.string());
        engine_.comp = Composition{};
        engine_.blackout = false;
        engine_.paused = false;
        engine_.setGpuTiming(false);
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
        solid_ = addClip("cs_solid", kSolid);
        red_ = addClip("cs_red", kRed);
        half_ = addClip("cs_half", kHalf);
        ramp_ = addClip("cs_ramp", kRamp);
    }

    void finish() {
        engine_.setColorStage(false, nullptr, 1.0f);
        engine_.setHdrToSdr(std::nullopt);
        engine_.paused = false;
        reset();
        step(4);
        clearDataDirectory();
        std::error_code ec;
        fs::remove_all(root_, ec);
        rescanLibrary();
        engine_.syncLibrary();
    }

    bool ready() const { return solid_ >= 0 && red_ >= 0 && half_ >= 0 && ramp_ >= 0; }
    int solid() const { return solid_; }
    int red() const { return red_; }
    int half() const { return half_; }
    int ramp() const { return ramp_; }
    const fs::path& root() const { return root_; }

    int addClip(const std::string& stem, const std::string& text) {
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
            ++frames_;
        }
    }

    void reset() {
        engine_.comp = Composition{};
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    void place(int layer, int clip, int column = 0) {
        engine_.comp.layers[static_cast<size_t>(layer)].setCell(static_cast<size_t>(column), clip);
        engine_.trigger(layer, column);
    }

    Shot capture() {
        Shot shot;
        shot.ok = engine_.readOutput(shot.pixels, shot.width, shot.height) && shot.width > 0 && shot.height > 0;
        return shot;
    }

    Shot show(int clip, int frames = 4) {
        reset();
        place(0, clip);
        step(frames);
        return capture();
    }

private:
    Engine& engine_;
    json summary_ = json::object();
    json items_ = json::array();
    int failed_ = 0;
    long long frames_ = 0;
    fs::path root_;
    int solid_ = -1;
    int red_ = -1;
    int half_ = -1;
    int ramp_ = -1;
};

int maxDifference(const Shot& a, const Shot& b) {
    if (a.pixels.size() != b.pixels.size()) {
        return 255;
    }
    int worst = 0;
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<int>(a.pixels[i]) - static_cast<int>(b.pixels[i])));
    }
    return worst;
}

bool identical(const Shot& a, const Shot& b) {
    return a.ok && b.ok && a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() && std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size()) == 0;
}

int distinctLevels(const Shot& shot, double fy, int channel) {
    std::set<int> levels;
    for (int x = 0; x < shot.width; ++x) {
        const Pixel p = shot.at((x + 0.5) / shot.width, fy);
        levels.insert(channel == 0 ? p.r : (channel == 1 ? p.g : p.b));
    }
    return static_cast<int>(levels.size());
}

void testDepths(Probe& p) {
    Engine& e = p.engine();
    const FramebufferDepth window = measureDefaultFramebuffer();
    p.record("the window frame buffer reports its colour depth", window.ok, describeDepth(window));
    p.summary()["windowDepth"] = describeDepth(window);

    p.step(2);
    const FramebufferDepth master = e.outputDepth();
    p.record("the master picture is 8 bit integer per channel", master.ok && master.red == 8 && master.green == 8 && master.blue == 8 && !master.floating, describeDepth(master));
    p.summary()["masterDepth"] = describeDepth(master);

    RenderTarget half;
    const bool created = half.createFloat(64, 64);
    const FramebufferDepth floating = created ? measureFramebuffer(half.fbo) : FramebufferDepth{};
    half.destroy();
    p.record("the float chain target keeps 16 bit floating point", created && floating.ok && floating.red == 16 && floating.floating, describeDepth(floating));
    p.summary()["floatTargetDepth"] = describeDepth(floating);

    const Shot ramp = p.show(p.ramp());
    const int levels = distinctLevels(ramp, 0.5, 0);
    p.record("a full width ramp has at most 256 levels in the master picture", ramp.ok && levels >= 250 && levels <= 256, std::to_string(levels) + " distinct levels over " + std::to_string(ramp.width) + " pixels");
    p.summary()["rampLevels"] = levels;
}

void testOffState(Probe& p, const Shot& baseline) {
    Engine& e = p.engine();
    p.record("without the colour stage nothing is allocated", e.colorStageBytes() == 0 && !e.colorStageStatus().requested && !e.colorStageActive(), "bytes " + std::to_string(e.colorStageBytes()));

    const CubeLut invert = makeLut(9, "invert", invertColor);
    e.setColorStage(true, &invert, 1.0f);
    p.step(3);
    const bool wasActive = e.colorStageActive();
    e.setColorStage(false, nullptr, 1.0f);
    p.step(3);
    const Shot afterOff = p.show(p.ramp());
    p.record("switching the colour stage on and off leaves the picture bit identical", wasActive && identical(baseline, afterOff) && e.colorStageBytes() == 0, "stage was " + std::string(wasActive ? "active" : "inactive") + ", largest difference " + std::to_string(maxDifference(baseline, afterOff)));

    e.setColorStage(true, nullptr, 1.0f);
    const Shot noLut = p.show(p.ramp());
    p.record("the colour stage without a LUT leaves the picture bit identical", identical(baseline, noLut) && !e.colorStageActive() && e.colorStageStatus().requested, "largest difference " + std::to_string(maxDifference(baseline, noLut)));

    e.setColorStage(true, &invert, 0.0f);
    const Shot zero = p.show(p.ramp());
    p.record("a LUT at strength 0 leaves the picture bit identical", identical(baseline, zero) && !e.colorStageActive(), "largest difference " + std::to_string(maxDifference(baseline, zero)));
    e.setColorStage(false, nullptr, 1.0f);
}

void testLutAccuracy(Probe& p, const Shot& baseline) {
    Engine& e = p.engine();
    const CubeLut identity = makeIdentityLut(17);
    e.setColorStage(true, &identity, 1.0f);
    const Shot same = p.show(p.ramp());
    const int diff = maxDifference(baseline, same);
    const int levels = distinctLevels(same, 0.5, 0);
    p.record("an identity LUT keeps the ramp within one level", e.colorStageActive() && diff <= 1 && levels >= 250, "largest difference " + std::to_string(diff) + ", " + std::to_string(levels) + " levels");
    p.summary()["identityLutMaxDifference"] = diff;

    const CubeLut invert = makeLut(2, "invert", invertColor);
    e.setColorStage(true, &invert, 1.0f);
    const Shot solid = p.show(p.solid());
    const Pixel inverted = solid.at(0.5, 0.5);
    p.record("an inverting LUT turns a solid colour around", std::abs(inverted.r - 204) <= 1 && std::abs(inverted.g - 128) <= 1 && std::abs(inverted.b - 51) <= 1, "got " + show(inverted) + " expected (204,128,51)");
    const Shot invertedRamp = p.show(p.ramp());
    int worst = 0;
    for (int i = 0; i < 64; ++i) {
        const double fx = (i + 0.5) / 64.0;
        const Pixel a = baseline.at(fx, 0.5);
        const Pixel b = invertedRamp.at(fx, 0.5);
        worst = std::max({worst, std::abs(255 - a.r - b.r), std::abs(255 - a.g - b.g), std::abs(255 - a.b - b.b)});
    }
    p.record("an inverting LUT turns the whole ramp around within one level", worst <= 1, "largest difference " + std::to_string(worst));

    const CubeLut mixed = makeLut(33, "mixed", mixedColor);
    e.setColorStage(true, &mixed, 1.0f);
    const Shot graded = p.show(p.ramp());
    int gpuWorst = 0;
    double gpuSum = 0.0;
    int count = 0;
    for (int j = 0; j < 12; ++j) {
        for (int i = 0; i < 16; ++i) {
            const double fx = (i + 0.5) / 16.0;
            const double fy = (j + 0.5) / 12.0;
            const Pixel in = baseline.at(fx, fy);
            const Pixel out = graded.at(fx, fy);
            const Color3 expected = sampleCube(mixed, {in.r / 255.0, in.g / 255.0, in.b / 255.0});
            const int dr = std::abs(out.r - static_cast<int>(std::lround(expected[0] * 255.0)));
            const int dg = std::abs(out.g - static_cast<int>(std::lround(expected[1] * 255.0)));
            const int db = std::abs(out.b - static_cast<int>(std::lround(expected[2] * 255.0)));
            gpuWorst = std::max({gpuWorst, dr, dg, db});
            gpuSum += dr + dg + db;
            count += 3;
        }
    }
    p.record("a 33 point LUT with crossing channels matches the processor model", gpuWorst <= 2, "largest difference " + std::to_string(gpuWorst) + ", mean " + number(gpuSum / std::max(count, 1)) + " levels over 192 pixels");
    p.summary()["mixedLutMaxDifference"] = gpuWorst;

    const CubeParse domain = parseCube("LUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 0.5 0.5 0.5\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    bool domainOk = domain.ok;
    int domainWorst = 0;
    if (domainOk) {
        e.setColorStage(true, &domain.lut, 1.0f);
        const Shot clipped = p.show(p.ramp());
        for (int i = 0; i < 64; ++i) {
            const double fx = (i + 0.5) / 64.0;
            const Pixel in = baseline.at(fx, 0.5);
            const Pixel out = clipped.at(fx, 0.5);
            const Color3 expected = sampleCube(domain.lut, {in.r / 255.0, in.g / 255.0, in.b / 255.0});
            domainWorst = std::max(domainWorst, std::abs(out.r - static_cast<int>(std::lround(expected[0] * 255.0))));
        }
    }
    p.record("a LUT with a smaller input domain clips like the processor model", domainOk && domainWorst <= 2, "largest difference " + std::to_string(domainWorst));

    e.setColorStage(true, &invert, 0.5f);
    const Shot half = p.show(p.ramp());
    int halfWorst = 0;
    for (int i = 0; i < 64; ++i) {
        const Pixel out = half.at((i + 0.5) / 64.0, 0.5);
        halfWorst = std::max({halfWorst, std::abs(out.r - 128), std::abs(out.g - 128), std::abs(out.b - 128)});
    }
    p.record("an inverting LUT at half strength gives middle grey", halfWorst <= 2, "largest distance from 128 is " + std::to_string(halfWorst));

    e.setColorStage(false, nullptr, 1.0f);
    const Shot alphaBefore = p.show(p.half());
    e.setColorStage(true, &invert, 1.0f);
    const Shot alphaAfter = p.show(p.half());
    const Pixel a = alphaBefore.at(0.5, 0.5);
    const Pixel b = alphaAfter.at(0.5, 0.5);
    p.record("the LUT leaves the alpha channel alone", a.a == b.a && (a.r != b.r || a.g != b.g || a.b != b.b), "before " + show(a) + " after " + show(b));
    e.setColorStage(false, nullptr, 1.0f);
}

void testLifecycle(Probe& p) {
    Engine& e = p.engine();
    const CubeLut lut = makeLut(33, "mixed", mixedColor);
    e.setColorStage(true, &lut, 1.0f);
    p.step(3);
    const std::uint64_t expected = e.graphOptimizer() ? cubeGpuBytes(33) : colorStageBytes(e.width(), e.height(), 33);
    p.record("the colour stage reports the memory it uses", e.colorStageBytes() == expected && expected > 0, "measured " + std::to_string(e.colorStageBytes()) + " B, planned " + std::to_string(expected) + " B");
    p.summary()["stageBytes33"] = e.colorStageBytes();

    const CubeLut invert = makeLut(2, "invert", invertColor);
    e.setColorStage(true, &invert, 1.0f);
    p.show(p.solid());
    const bool resized = e.setResolutionIndex(1);
    p.place(0, p.solid());
    p.step(4);
    const Shot small = p.capture();
    const Pixel inverted = small.at(0.5, 0.5);
    const std::uint64_t smallBytes = e.graphOptimizer() ? cubeGpuBytes(2) : colorStageBytes(e.width(), e.height(), 2);
    p.record("the colour stage follows a change of the picture size", resized && std::abs(inverted.r - 204) <= 1 && std::abs(inverted.g - 128) <= 1 && std::abs(inverted.b - 51) <= 1 && e.colorStageBytes() == smallBytes, "got " + show(inverted) + " at " + std::to_string(small.width) + "x" + std::to_string(small.height) + ", " + std::to_string(e.colorStageBytes()) + " B");
    e.setResolutionIndex(0);
    p.step(3);

    e.setColorStage(false, nullptr, 1.0f);
    p.step(2);
    p.record("switching the colour stage off frees its memory", e.colorStageBytes() == 0, "bytes " + std::to_string(e.colorStageBytes()));

    CubeLut broken;
    broken.size = 4;
    e.setColorStage(true, &broken, 1.0f);
    p.record("a broken LUT is refused and the picture stays untouched", !e.colorStageActive() && !e.colorStageStatus().error.empty(), "error: " + e.colorStageStatus().error);
    e.setColorStage(false, nullptr, 1.0f);
}

void testTransition(Probe& p) {
    Engine& e = p.engine();
    const CubeLut invert = makeLut(2, "invert", invertColor);
    p.reset();
    p.place(0, p.red());
    e.comp.deckSettings.seconds = 1.5f;
    e.setColorStage(true, &invert, 1.0f);
    p.step(6);
    const Pixel before = p.capture().at(0.5, 0.5);
    const bool started = e.switchDeck(1);
    std::vector<Pixel> samples;
    for (int i = 0; i < 22; ++i) {
        p.step(5);
        samples.push_back(p.capture().at(0.5, 0.5));
    }
    p.step(60);
    const Pixel after = p.capture().at(0.5, 0.5);
    bool channelsHold = true;
    bool rising = true;
    for (size_t i = 0; i < samples.size(); ++i) {
        channelsHold = channelsHold && samples[i].g >= 253 && samples[i].b >= 253;
        rising = rising && (i == 0 || samples[i].r >= samples[i - 1].r - 2);
    }
    const bool passed = started && before.r <= 2 && before.g >= 253 && before.b >= 253 && channelsHold && rising && after.r >= 253 && after.g >= 253 && after.b >= 253 && samples.front().r < samples.back().r;
    p.record("the colour stage is applied after the deck transition", passed, "start " + show(before) + ", first " + show(samples.front()) + ", last " + show(samples.back()) + ", end " + show(after));
    e.switchDeck(0);
    p.step(120);
    e.setColorStage(false, nullptr, 1.0f);
}

struct HarnessCase {
    std::string name;
    HdrToSdr params;
};

double hdrHarness(Probe& p, const HdrToSdr& params, std::string& problem) {
    static const char* kVertex = R"glsl(#version 330 core
void main() {
    vec2 q = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(q * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";
    const std::string fragment = std::string("#version 330 core\n") + colorGlslCommon() + colorGlslHdr() + R"glsl(
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(cmHdrToSdr(uColor), 1.0);
}
)glsl";
    std::string log;
    const GLuint program = buildProgram(kVertex, fragment, &log);
    if (program == 0) {
        problem = log;
        return 1.0;
    }
    RenderTarget target;
    FullscreenTriangle triangle;
    if (!target.createFloat(2, 2)) {
        glDeleteProgram(program);
        problem = "no float target";
        return 1.0;
    }
    triangle.create();
    HdrUniforms uniforms;
    uniforms.bind(program);
    const GLint colorLocation = glGetUniformLocation(program, "uColor");
    std::vector<Color3> inputs;
    for (int i = 0; i <= 20; ++i) {
        const double v = i / 20.0;
        inputs.push_back({v, v, v});
    }
    const std::array<Color3, 14> colours = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {1.0, 0.0, 1.0}, {0.58, 0.30, 0.10}, {0.90, 0.20, 0.70}, {0.40, 0.55, 0.75}, {0.75, 0.75, 0.30}, {0.20, 0.05, 0.05}, {0.05, 0.30, 0.10}, {0.7, 0.2, 0.0}, {0.3, 0.8, 0.9}}};
    inputs.insert(inputs.end(), colours.begin(), colours.end());
    double worst = 0.0;
    glDisable(GL_BLEND);
    for (const Color3& input : inputs) {
        target.bind();
        glUseProgram(program);
        uniforms.upload(&params);
        glUniform3f(colorLocation, static_cast<float>(input[0]), static_cast<float>(input[1]), static_cast<float>(input[2]));
        triangle.draw();
        float out[4] = {0, 0, 0, 0};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, out);
        const Color3 expected = hdrToSdr(input, params);
        for (int c = 0; c < 3; ++c) {
            worst = std::max(worst, std::fabs(static_cast<double>(out[c]) - expected[static_cast<size_t>(c)]));
        }
    }
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    triangle.destroy();
    target.destroy();
    glDeleteProgram(program);
    (void)p;
    return worst;
}

void testHdrShader(Probe& p) {
    std::vector<HarnessCase> cases;
    HdrToSdr pq;
    pq.source = Transfer::Pq;
    cases.push_back({"PQ, soft shoulder", pq});
    HdrToSdr pqFilmic = pq;
    pqFilmic.mapper = ToneMapper::Filmic;
    cases.push_back({"PQ, filmic", pqFilmic});
    HdrToSdr pqDim = pq;
    pqDim.referenceWhite = 100.0;
    pqDim.peak = 4000.0;
    cases.push_back({"PQ, white 100, peak 4000", pqDim});
    HdrToSdr pqP3 = pq;
    pqP3.primaries = Primaries::DisplayP3;
    cases.push_back({"PQ, Display P3 primaries", pqP3});
    HdrToSdr hlg;
    hlg.source = Transfer::Hlg;
    cases.push_back({"HLG, soft shoulder", hlg});
    HdrToSdr hlgPeak = hlg;
    hlgPeak.peak = 600.0;
    hlgPeak.mapper = ToneMapper::Filmic;
    cases.push_back({"HLG, peak 600, filmic", hlgPeak});
    json report = json::object();
    for (const HarnessCase& test : cases) {
        std::string problem;
        const double error = hdrHarness(p, sanitized(test.params), problem);
        report[test.name] = error;
        p.record("the HDR shader matches the processor model: " + test.name, problem.empty() && error <= 0.006, problem.empty() ? "largest difference " + number(error, 5) + " (limit 0.006)" : problem);
    }
    p.summary()["hdrShaderMaxError"] = report;
}

Shot captureVideo(Probe& p, int clip, VideoDecodeMode mode, bool gpuConvert) {
    Engine& e = p.engine();
    Shot shot;
    e.clearLayer(0);
    e.setVideoSettings(mode == VideoDecodeMode::ForceSoftware ? VideoDecodeMode::Auto : VideoDecodeMode::ForceSoftware, true, !gpuConvert);
    p.step(3);
    e.setVideoSettings(mode, true, gpuConvert);
    e.clearLayer(0);
    e.comp.layers[0].cells[0] = clip;
    e.paused = true;
    e.trigger(0, 0);
    const auto start = Clock::now();
    MediaPlayer* player = e.mediaPlayer(clip);
    while (player != nullptr && !player->ready()) {
        p.step(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        if (player->state() == MediaState::Failed) {
            shot.error = player->error();
            return shot;
        }
        if (Clock::now() - start > std::chrono::seconds(30)) {
            shot.error = "the video did not become ready";
            return shot;
        }
    }
    p.step(4);
    if (player == nullptr || !e.readOutput(shot.pixels, shot.width, shot.height)) {
        shot.error = "the output could not be read";
        return shot;
    }
    shot.route = uploadRouteName(player->uploadRoute());
    shot.path = decodePathName(player->status().path);
    shot.ok = true;
    return shot;
}

void testHdrVideo(Probe& p, const std::string& directory, const std::string& file, Transfer source) {
    Engine& e = p.engine();
    const fs::path path = fs::path(directory) / file;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        p.record("the HDR test clip " + file + " is available", false, "not found in " + directory);
        return;
    }
    importAsset(path.string());
    rescanLibrary();
    e.syncLibrary();
    const std::string stem = path.stem().string();
    const int clip = clipIndexByName(videoClipName(stem));
    if (clip < 0) {
        p.record("the HDR test clip " + file + " is in the library", false, "clip not found");
        return;
    }
    p.record("the HDR test clip " + file + " is in the library", true, "clip " + std::to_string(clip));

    e.setHdrToSdr(std::nullopt);
    const Shot off = captureVideo(p, clip, VideoDecodeMode::Auto, true);
    HdrToSdr params;
    params.source = source;
    params.primaries = Primaries::Rec2020;
    e.setHdrToSdr(sanitized(params));
    const Shot on = captureVideo(p, clip, VideoDecodeMode::Auto, true);
    if (!off.ok || !on.ok || off.pixels.size() != on.pixels.size()) {
        p.record("the HDR video " + file + " renders with and without tone mapping", false, off.ok ? on.error : off.error);
        e.setHdrToSdr(std::nullopt);
        return;
    }
    double offLuma = 0.0;
    double onLuma = 0.0;
    int inBand = 0;
    int total = 0;
    double errorSum = 0.0;
    const HdrToSdr used = sanitized(params);
    int flat = 0;
    double changeSum = 0.0;
    for (int j = 0; j < 72; ++j) {
        for (int i = 0; i < 128; ++i) {
            const double fx = (i + 0.5) / 128.0;
            const double fy = (j + 0.5) / 72.0;
            const Pixel a = off.at(fx, fy);
            const Pixel b = on.at(fx, fy);
            offLuma += 0.2126 * a.r + 0.7152 * a.g + 0.0722 * a.b;
            onLuma += 0.2126 * b.r + 0.7152 * b.g + 0.0722 * b.b;
            changeSum += std::abs(b.r - a.r) + std::abs(b.g - a.g) + std::abs(b.b - a.b);
            ++total;
            bool locallyFlat = true;
            for (const std::array<double, 2>& offset : {std::array<double, 2>{-0.4, 0.0}, {0.4, 0.0}, {0.0, -0.4}, {0.0, 0.4}}) {
                const Pixel n = off.at(fx + offset[0] / 128.0, fy + offset[1] / 72.0);
                locallyFlat = locallyFlat && std::abs(n.r - a.r) <= 1 && std::abs(n.g - a.g) <= 1 && std::abs(n.b - a.b) <= 1;
            }
            if (!locallyFlat) {
                continue;
            }
            ++flat;
            std::array<double, 3> low = {1e9, 1e9, 1e9};
            std::array<double, 3> high = {-1e9, -1e9, -1e9};
            std::array<double, 3> centre = {0, 0, 0};
            for (int corner = 0; corner < 8; ++corner) {
                const Color3 input = {(a.r + ((corner & 1) ? 1.0 : -1.0)) / 255.0, (a.g + ((corner & 2) ? 1.0 : -1.0)) / 255.0, (a.b + ((corner & 4) ? 1.0 : -1.0)) / 255.0};
                const Color3 out = hdrToSdr({std::clamp(input[0], 0.0, 1.0), std::clamp(input[1], 0.0, 1.0), std::clamp(input[2], 0.0, 1.0)}, used);
                for (size_t c = 0; c < 3; ++c) {
                    low[c] = std::min(low[c], out[c] * 255.0);
                    high[c] = std::max(high[c], out[c] * 255.0);
                }
            }
            const Color3 mid = hdrToSdr({a.r / 255.0, a.g / 255.0, a.b / 255.0}, used);
            for (size_t c = 0; c < 3; ++c) {
                centre[c] = mid[c] * 255.0;
            }
            const std::array<int, 3> got = {b.r, b.g, b.b};
            bool inside = true;
            for (size_t c = 0; c < 3; ++c) {
                inside = inside && got[c] >= low[c] - 3.0 && got[c] <= high[c] + 3.0;
                errorSum += std::fabs(got[c] - centre[c]);
            }
            inBand += inside ? 1 : 0;
        }
    }
    const double fraction = static_cast<double>(inBand) / std::max(flat, 1);
    offLuma /= std::max(total, 1);
    onLuma /= std::max(total, 1);
    const double meanError = errorSum / std::max(flat * 3, 1);
    const bool gpuRoute = off.route.find("gpu") != std::string::npos || off.route.find("convert") != std::string::npos;
    const double meanChange = changeSum / std::max(total * 3, 1);
    p.record("tone mapping changes the picture of " + file, meanChange > 3.0, "mean change " + number(meanChange, 1) + " levels per channel, mean luma without " + number(offLuma, 1) + " and with " + number(onLuma, 1) + ", route " + off.route);
    p.record("the tone mapped picture of " + file + " matches the processor model", flat >= 300 && fraction >= 0.97 && meanError < 2.0, number(fraction * 100.0, 1) + " % of " + std::to_string(flat) + " flat samples (of " + std::to_string(total) + ") inside the quantisation band, mean error " + number(meanError, 2) + " levels");
    p.summary()["hdrClips"][file] = {{"route", off.route}, {"flatSamples", flat}, {"bandFraction", fraction}, {"meanError", meanError}, {"meanChange", meanChange}, {"lumaOff", offLuma}, {"lumaOn", onLuma}, {"gpuRoute", gpuRoute}};

    const Shot software = captureVideo(p, clip, VideoDecodeMode::ForceSoftware, false);
    e.setHdrToSdr(std::nullopt);
    const Shot softwareOff = captureVideo(p, clip, VideoDecodeMode::ForceSoftware, false);
    const bool same = software.ok && softwareOff.ok && maxDifference(software, softwareOff) <= 1;
    p.record("on the processor conversion path the HDR option changes nothing for " + file, same, "largest difference " + std::to_string(software.ok && softwareOff.ok ? maxDifference(software, softwareOff) : -1) + ", route " + software.route);
    p.summary()["hdrClips"][file]["processorPathDifference"] = software.ok && softwareOff.ok ? maxDifference(software, softwareOff) : -1;

    e.paused = false;
    e.setVideoSettings(VideoDecodeMode::Auto, true, true);
    e.clearLayer(0);
}

void testSdrVideoUnchanged(Probe& p, const std::string& directory, const std::string& file) {
    Engine& e = p.engine();
    const fs::path path = fs::path(directory) / file;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        p.record("the SDR test clip " + file + " is available", false, "not found in " + directory);
        return;
    }
    importAsset(path.string());
    rescanLibrary();
    e.syncLibrary();
    const int clip = clipIndexByName(videoClipName(path.stem().string()));
    if (clip < 0) {
        p.record("the SDR test clip " + file + " is in the library", false, "clip not found");
        return;
    }
    e.setHdrToSdr(std::nullopt);
    const Shot off = captureVideo(p, clip, VideoDecodeMode::Auto, true);
    e.setHdrToSdr(sanitized(HdrToSdr{}));
    const Shot on = captureVideo(p, clip, VideoDecodeMode::Auto, true);
    e.setHdrToSdr(std::nullopt);
    const bool same = off.ok && on.ok && identical(off, on);
    p.record("the HDR option leaves an SDR video bit identical: " + file, same, same ? "route " + off.route + ", no pixel differs" : (off.ok && on.ok ? "largest difference " + std::to_string(maxDifference(off, on)) : "capture failed"));
    e.paused = false;
    e.setVideoSettings(VideoDecodeMode::Auto, true, true);
    e.clearLayer(0);
}

double frameMs(Probe& p, int frames) {
    p.step(10);
    const auto begin = Clock::now();
    p.step(frames);
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count() / std::max(frames, 1);
}

void benchmark(Probe& p, int frames) {
    Engine& e = p.engine();
    p.reset();
    for (int l = 0; l < 4; ++l) {
        p.place(l, p.ramp());
    }
    const CubeLut lut = makeLut(33, "mixed", mixedColor);
    e.setColorStage(false, nullptr, 1.0f);
    const double off = frameMs(p, frames);
    e.setColorStage(true, &lut, 1.0f);
    const double on = frameMs(p, frames);
    e.setColorStage(false, nullptr, 1.0f);
    const double offAgain = frameMs(p, frames);
    json result;
    result["frames"] = frames;
    result["picture"] = std::to_string(e.width()) + "x" + std::to_string(e.height());
    result["layers"] = 4;
    result["msOff"] = off;
    result["msOn"] = on;
    result["msOffAgain"] = offAgain;
    result["overheadMs"] = on - (off + offAgain) / 2.0;
    result["note"] = "measured on the software OpenGL renderer of this machine, not a graphics card number";
    p.summary()["benchmark"] = result;
    p.record("the colour stage costs a measurable and bounded amount of frame time", on > 0.0 && on < 1000.0 && std::fabs(offAgain - off) < std::max(off, 1.0), "without " + number(off, 2) + " ms, with " + number(on, 2) + " ms, without again " + number(offAgain, 2) + " ms per frame, " + result["picture"].get<std::string>() + ", 4 ramp layers, software OpenGL");
}

}

int runColorSelfTest(Engine& engine, const std::string& clipDirectory, const std::string& outputPath) {
    json report = json::object();
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["version"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const Composition savedComposition = engine.comp;
    Probe probe(engine);
    probe.prepare();
    if (!probe.ready()) {
        probe.record("the test clips are in the library", false, "a clip is missing");
    } else {
        probe.step(8);
        testDepths(probe);
        const Shot baseline = probe.show(probe.ramp());
        testOffState(probe, baseline);
        testLutAccuracy(probe, baseline);
        testLifecycle(probe);
        testTransition(probe);
        testHdrShader(probe);
        const RecordCapabilities video = probeRecordCapabilities();
        const char* required = std::getenv("NYREXVJ_REQUIRE_VIDEO");
        if (video.ffmpegLoaded) {
            testHdrVideo(probe, clipDirectory, "test_hdr_pq.mp4", Transfer::Pq);
            testHdrVideo(probe, clipDirectory, "test_hdr_hlg.mp4", Transfer::Hlg);
            testSdrVideoUnchanged(probe, clipDirectory, "test_h264.mp4");
        } else if (required != nullptr && *required != '\0') {
            probe.record("the FFmpeg libraries are loaded", false, video.problem);
        } else {
            probe.summary()["videoSkipped"] = "FFmpeg is not available: " + video.problem;
            logLine("Colour self test: the video checks are skipped, FFmpeg is not available: " + video.problem);
        }
        benchmark(probe, 40);
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        probe.record("the graphics state is left clean", glGetError() == GL_NO_ERROR && program == 0, "no error and no program left bound");
    }
    probe.finish();
    engine.applyComposition(savedComposition);
    report["summary"] = probe.summary();
    report["frames"] = probe.frames();
    report["checks"] = probe.items();
    report["failed"] = probe.failed();
    report["passed"] = probe.failed() == 0;
    const std::string text = report.dump(2);
    logLine("Colour self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return probe.failed() == 0 ? 0 : 1;
}

}

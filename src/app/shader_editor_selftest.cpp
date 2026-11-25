#include "app/shader_editor_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "core/shader_editor.hpp"
#include "render/clip_library.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

double since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string fixed(double value, int digits = 1) {
    char text[40];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

const char* kRed = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 1.0); }\n";
const char* kGreen = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 1.0, 0.0, 1.0); }\n";
const char* kBlue = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 0.0, 1.0, 1.0); }\n";

const char* kBrokenLine4 =
    "// a shader with a mistake\n"
    "\n"
    "void mainImage(out vec4 c, in vec2 p) {\n"
    "    c = vec4(1.0) +;\n"
    "}\n";

const char* kBrokenBuffer =
    "#pragma nyrexvj buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n"
    "#pragma nyrexvj buffer b\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0) +; }\n"
    "#pragma nyrexvj image\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n";

const char* kBadDirective = "// first\n#pragma nyrexvj bogus\nvoid mainImage(out vec4 c, in vec2 p) { c = vec4(1.0); }\n";

const char* kLinkFailure =
    "uniform vec4 hugeUniformArray[200000];\n"
    "void mainImage(out vec4 c, in vec2 p) { c = hugeUniformArray[int(p.x) % 200000]; }\n";

const char* kAccumulate =
    "#pragma nyrexvj buffer a\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel0, p / iResolution.xy) + vec4(0.1, 0.0, 0.0, 0.0); }\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 buffer a\n"
    "void mainImage(out vec4 c, in vec2 p) { c = vec4(texture(iChannel0, p / iResolution.xy).r, 0.0, 0.0, 1.0); }\n";

const char* kFilter =
    "#pragma nyrexvj filter\n"
    "#pragma nyrexvj image\n"
    "#pragma nyrexvj channel 0 picture\n"
    "void mainImage(out vec4 c, in vec2 p) { c = texture(iChannel0, p / iResolution.xy).bgra; }\n";

const char* kHeavy =
    "void mainImage(out vec4 c, in vec2 p) {\n"
    "    float s = 0.0;\n"
    "    for (int i = 0; i < 1500; ++i) {\n"
    "        s += sin(float(i) * 0.013 + p.x * 0.002 + iTime) * cos(float(i) * 0.007 + p.y * 0.003);\n"
    "    }\n"
    "    c = vec4(vec3(0.5 + 0.5 * sin(s)), 1.0);\n"
    "}\n";

class Probe {
public:
    Probe(Engine& engine, json& report) : engine_(engine), report_(report) {}

    void prepare() {
        root_ = fs::temp_directory_path() / "nyrexvj_shadereditor_selftest";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "shaders", ec);
        setDataDirectory(root_.string());
        engine_.comp = Composition{};
        engine_.blackout = false;
        engine_.paused = false;
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    void finish() {
        engine_.releaseShaderPlayground();
        clearDataDirectory();
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    void step(int count, bool playground = true) {
        for (int i = 0; i < count; ++i) {
            if (playground) {
                engine_.requestShaderPlayground(clock_);
            }
            engine_.setAudio(virtualAudio(engine_.beat(), engine_.time()));
            engine_.update(1.0 / 60.0);
            engine_.render();
            glFinish();
            clock_ += 1.0 / 60.0;
            ++frames_;
        }
    }

    bool submit(const std::string& text) {
        ++revision_;
        engine_.submitShaderPlayground(text, revision_);
        return settle();
    }

    bool settle(int maxFrames = 900) {
        lastSettleFrames_ = 0;
        const auto begin = Clock::now();
        for (int i = 0; i < maxFrames; ++i) {
            step(1);
            ++lastSettleFrames_;
            const ShaderPlaygroundStatus& s = engine_.shaderPlayground();
            if (!s.compiling && s.attemptedRevision == revision_) {
                lastSettleMs_ = since(begin);
                return true;
            }
        }
        lastSettleMs_ = since(begin);
        return false;
    }

    const ShaderPlaygroundStatus& status() const { return engine_.shaderPlayground(); }

    bool readPlayground(std::vector<unsigned char>& pixels, int& width, int& height) {
        const GLuint texture = engine_.shaderPlaygroundTexture();
        if (!texture) {
            return false;
        }
        width = engine_.shaderPlaygroundWidth();
        height = engine_.shaderPlaygroundHeight();
        pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (complete) {
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        return complete;
    }

    Pixel centre() {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        Pixel result;
        if (readPlayground(pixels, width, height)) {
            result = pixelAt(pixels, width, width / 2, height / 2);
        }
        return result;
    }

    Pixel previewAt(double fx, double fy) {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        Pixel result;
        if (readPlayground(pixels, width, height)) {
            result = pixelAt(pixels, width, static_cast<int>(fx * width), static_cast<int>(fy * height));
        }
        return result;
    }

    int visible() {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        int count = 0;
        if (readPlayground(pixels, width, height)) {
            for (size_t i = 3; i < pixels.size(); i += 4) {
                count += pixels[i] > 8 ? 1 : 0;
            }
        }
        return count;
    }

    int pixelCount() const { return engine_.shaderPlaygroundWidth() * engine_.shaderPlaygroundHeight(); }

    Pixel outputCentre() {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        Pixel result;
        if (engine_.readOutput(pixels, width, height) && width > 0 && height > 0) {
            result = pixelAt(pixels, width, width / 2, height / 2);
        }
        return result;
    }

    int addClip(const std::string& stem, const std::string& text) {
        writeText(root_ / "shaders" / (stem + ".glsl"), text);
        rescanLibrary();
        engine_.syncLibrary();
        return clipIndexByName(shaderClipName(stem));
    }

    void place(int layer, int clip) {
        engine_.comp.layers[static_cast<size_t>(layer)].cells[0] = clip;
        engine_.trigger(layer, 0);
    }

    void clearLayers() {
        engine_.comp = Composition{};
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    static bool near(const Pixel& pixel, int r, int g, int b, int tolerance) {
        return std::abs(pixel.r - r) <= tolerance && std::abs(pixel.g - g) <= tolerance && std::abs(pixel.b - b) <= tolerance;
    }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Shader editor self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void info(const std::string& name, const std::string& detail) {
        json item;
        item["name"] = name;
        item["info"] = true;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        logLine("Shader editor self test " + name + ": " + detail);
    }

    void expect(const std::string& name, const Pixel& pixel, int r, int g, int b, int tolerance) {
        const std::string detail = "got (" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + ") expected (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ") +-" + std::to_string(tolerance);
        record(name, near(pixel, r, g, b, tolerance), detail);
    }

    int failed() const { return failed_; }
    json& items() { return items_; }
    const fs::path& root() const { return root_; }
    Engine& engine() { return engine_; }
    std::uint64_t revision() const { return revision_; }
    int lastSettleFrames() const { return lastSettleFrames_; }
    double lastSettleMs() const { return lastSettleMs_; }
    unsigned long long frames() const { return frames_; }

private:
    static Pixel pixelAt(const std::vector<unsigned char>& pixels, int width, int x, int y) {
        const int height = static_cast<int>(pixels.size() / 4u) / std::max(1, width);
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, std::max(0, height - 1));
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
    std::uint64_t revision_ = 0;
    double clock_ = 0.0;
    int lastSettleFrames_ = 0;
    double lastSettleMs_ = 0.0;
    unsigned long long frames_ = 0;
};

std::string clip(const std::string& text, size_t length = 220) {
    std::string flat;
    for (const char c : text) {
        flat += (c == '\n' || c == '\r') ? ' ' : c;
    }
    return flat.size() > length ? flat.substr(0, length) : flat;
}

int errorLine(const std::string& log, const std::string& pass = std::string()) {
    for (const shaderedit::Diagnostic& d : shaderedit::parseCompileLog(log)) {
        if (d.severity == shaderedit::Severity::Error && d.line > 0 && (pass.empty() || d.pass == pass)) {
            return d.line;
        }
    }
    return 0;
}

void testValidAndInvalid(Probe& run) {
    run.step(2);
    run.record("the playground stays idle until a shader is sent", !run.status().hasWorking && run.engine().shaderPlaygroundTexture() == 0, "no program and no picture before the first submit");
    const bool compiled = run.submit(kRed);
    run.record("a valid shader compiles", compiled && run.status().hasWorking && run.status().workingRevision == run.revision() && run.status().error.empty(), "revision " + std::to_string(run.status().workingRevision) + " in " + fixed(run.status().lastCompileMs) + " ms, " + std::to_string(run.lastSettleFrames()) + " frames");
    run.step(2);
    run.expect("the preview shows the shader", run.centre(), 255, 0, 0, 2);
    run.record("the playground reports the driver capability", true, std::string("parallel compile ") + (run.status().parallelCompile ? "available" : "not available (synchronous compile)"));

    const std::uint64_t working = run.status().workingRevision;
    const unsigned long long failuresBefore = run.status().failures;
    const bool settled = run.submit(kBrokenLine4);
    run.step(3);
    run.record("a broken shader is reported", settled && !run.status().error.empty() && run.status().failures == failuresBefore + 1, clip(run.status().error));
    run.record("the error is on the line of the file", errorLine(run.status().error) == 4, "line " + std::to_string(errorLine(run.status().error)) + " of 4 expected");
    run.record("the last working version is kept", run.status().hasWorking && run.status().workingRevision == working, "revision " + std::to_string(run.status().workingRevision));
    run.expect("the preview keeps drawing the last working version", run.centre(), 255, 0, 0, 2);

    const bool fixedOk = run.submit(kGreen);
    run.record("the fixed shader recovers", fixedOk && run.status().error.empty() && run.status().workingRevision == run.revision(), "revision " + std::to_string(run.status().workingRevision));
    run.step(2);
    run.expect("the preview shows the fixed shader", run.centre(), 0, 255, 0, 2);

    const bool directive = run.submit(kBadDirective);
    run.record("a directive mistake is reported with its line", directive && errorLine(run.status().error) == 2, clip(run.status().error));
    const bool buffer = run.submit(kBrokenBuffer);
    run.record("a mistake in a buffer names the pass and the line", buffer && errorLine(run.status().error, "Buffer B") == 4, clip(run.status().error));
    run.record("the last working version survives both", run.status().hasWorking && run.status().workingRevision == run.revision() - 2, "revision " + std::to_string(run.status().workingRevision));
    run.step(2);
    run.expect("and it still draws", run.centre(), 0, 255, 0, 2);

    const std::uint64_t kept = run.status().workingRevision;
    const bool link = run.submit(kLinkFailure);
    run.record("a link failure of the driver is reported", link && !run.status().error.empty(), clip(run.status().error));
    run.record("the link failure keeps the working version", run.status().hasWorking && run.status().workingRevision == kept, "revision " + std::to_string(run.status().workingRevision));

    const std::string huge(shaderedit::kMaxTextBytes + 4096, 'a');
    const bool big = run.submit(huge);
    run.record("a text over the size limit is refused", big && run.status().error.find("KiB") != std::string::npos && run.status().workingRevision == kept, clip(run.status().error));
    const bool empty = run.submit(std::string());
    run.record("an empty text is a normal error", empty && !run.status().error.empty() && run.status().workingRevision == kept, clip(run.status().error));
}

void testTemplates(Probe& run) {
    int index = 0;
    for (const shaderedit::Template& entry : shaderedit::templates()) {
        const std::string label = "template " + std::to_string(++index) + " (" + entry.title + ")";
        const bool settled = run.submit(entry.text);
        run.step(8);
        const ShaderPlaygroundStatus& s = run.status();
        run.record(label + " compiles", settled && s.error.empty() && s.workingRevision == run.revision(), s.error.empty() ? "passes " + std::to_string(s.passCount) + ", " + fixed(s.lastCompileMs) + " ms" : clip(s.error));
        const int seen = run.visible();
        run.record(label + " draws something", seen > 0, std::to_string(seen) + " of " + std::to_string(run.pixelCount()) + " pixels visible");
        if (std::string(entry.text).find("#pragma nyrexvj alpha") != std::string::npos) {
            run.record(label + " keeps its transparent parts", seen < run.pixelCount(), std::to_string(seen) + " of " + std::to_string(run.pixelCount()) + " pixels visible");
            run.record(label + " is reported as using alpha", s.alpha, s.alpha ? "alpha on" : "alpha off");
        }
    }
}

void testMultipass(Probe& run) {
    const bool accumulated = run.submit(kAccumulate);
    run.record("a buffer shader compiles in the playground", accumulated && run.status().error.empty() && run.status().advanced && run.status().passCount == 2, "passes " + std::to_string(run.status().passCount));
    run.step(2);
    const Pixel early = run.centre();
    run.step(6);
    const Pixel later = run.centre();
    run.record("the buffer keeps its picture from frame to frame", later.r > early.r + 20, "red " + std::to_string(early.r) + " then " + std::to_string(later.r));
    const size_t held = run.engine().shaderBufferBytes();
    run.record("the playground buffers count in the shader memory", held > 0, std::to_string(held) + " bytes");

    const bool swapped = run.submit(kAccumulate);
    run.record("a swap to a new shader compiles", swapped && run.status().workingRevision == run.revision(), "compiles " + std::to_string(run.status().compiles));
    run.step(1);
    const Pixel restarted = run.centre();
    run.record("the restarted buffer starts low again", restarted.r < later.r, "red " + std::to_string(restarted.r) + " after the swap, " + std::to_string(later.r) + " before");

    run.engine().setShaderBufferBudget(1000);
    run.step(3);
    run.record("a refused buffer budget is reported", !run.status().runtimeError.empty(), clip(run.status().runtimeError));
    run.record("a refused budget holds no buffer memory", run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes");
    run.engine().setShaderBufferBudget(kShaderBufferBudgetBytes);
    run.step(8);
    run.record("the preview recovers when the budget is back", run.status().runtimeError.empty() && run.centre().r > 0, "red " + std::to_string(run.centre().r));

    const std::uint64_t kept = run.status().workingRevision;
    const bool filtered = run.submit(kFilter);
    run.record("a filter file is not previewed and says so", filtered && run.status().filter && run.status().workingRevision == kept, std::string("filter ") + (run.status().filter ? "recognised" : "not recognised"));
}

void testGovernor(Probe& run) {
    run.submit(kRed);
    run.step(3);
    const auto lightStart = Clock::now();
    run.step(20, false);
    const double lightMs = since(lightStart) / 20.0;
    const auto lightPlaygroundStart = Clock::now();
    run.step(20);
    const double lightWithMs = since(lightPlaygroundStart) / 20.0;

    const bool heavyOk = run.submit(kHeavy);
    run.record("a heavy shader compiles", heavyOk && run.status().error.empty(), "compile " + fixed(run.status().lastCompileMs) + " ms");
    const auto heavyStart = Clock::now();
    bool reacted = false;
    int frames = 0;
    for (int i = 0; i < 120 && !reacted; ++i) {
        run.step(1);
        ++frames;
        reacted = run.status().throttled || run.status().paused;
    }
    const double heavyMs = since(heavyStart) / std::max(1, frames);
    run.record("the cost control reacts to a heavy preview", reacted, std::string(run.status().paused ? "paused" : (run.status().throttled ? "throttled, every " + std::to_string(run.status().interval) + ". frame" : "not reacted")) + " after " + std::to_string(frames) + " frames, preview " + fixed(run.status().previewMs) + " ms");
    run.info("frame time without the playground", fixed(lightMs) + " ms per frame (wall clock, software rendering when no graphics card is present)");
    run.info("frame time with a light preview", fixed(lightWithMs) + " ms per frame");
    run.info("frame time while the heavy preview is running", fixed(heavyMs) + " ms per frame on average over " + std::to_string(frames) + " frames");

    run.step(30);
    const auto settledStart = Clock::now();
    run.step(10);
    const double settledMs = since(settledStart) / 10.0;
    run.info("frame time after the cost control reacted", fixed(settledMs) + " ms per frame");
    run.record("the cost control lowers the load", settledMs < heavyMs || run.status().paused, fixed(settledMs) + " ms after, " + fixed(heavyMs) + " ms during the first frames");

    const bool wasPaused = run.status().paused;
    run.engine().resumeShaderPlayground();
    run.step(1);
    run.record("resume clears the pause", !run.status().paused, wasPaused ? "was paused" : "was only throttled");

    run.submit(kRed);
    run.step(3);
    run.record("a new working version resets the cost control", !run.status().throttled && !run.status().paused && run.status().interval == 1, "interval " + std::to_string(run.status().interval));
}

void testIsolation(Probe& run) {
    const int blue = run.addClip("isolation_blue", kBlue);
    run.clearLayers();
    run.place(0, blue);
    run.submit(kRed);
    run.step(3);
    run.expect("the output keeps its own shader while the preview shows another", run.outputCentre(), 0, 0, 255, 2);
    run.expect("and the preview shows its own", run.centre(), 255, 0, 0, 2);
    run.submit(kBrokenLine4);
    run.step(3);
    run.expect("a broken preview does not touch the output", run.outputCentre(), 0, 0, 255, 2);
    run.engine().setShaderBufferBudget(1000);
    run.submit(kAccumulate);
    run.step(3);
    run.expect("a refused memory budget of the preview does not stop the output", run.outputCentre(), 0, 0, 255, 2);
    run.engine().setShaderBufferBudget(kShaderBufferBudgetBytes);

    run.engine().releaseShaderPlayground();
    run.step(4, false);
    run.record("the playground releases everything when it is not requested", !run.status().active && run.engine().shaderPlaygroundTexture() == 0 && run.engine().shaderBufferBytes() == 0, std::to_string(run.engine().shaderBufferBytes()) + " bytes held");
    run.expect("the output is unchanged after the release", run.outputCentre(), 0, 0, 255, 2);

    run.step(2);
    run.record("a new request starts without an old program", !run.status().hasWorking, "no working version after the window was closed");
    const bool again = run.submit(kGreen);
    run.record("and the next shader compiles again", again && run.status().hasWorking, "revision " + std::to_string(run.status().workingRevision));
    run.step(2);
    run.expect("with its own picture", run.centre(), 0, 255, 0, 2);
}

void testSaveAndReload(Probe& run) {
    const fs::path folder = shaderDirectory();
    const shaderedit::SaveResult first = shaderedit::saveShader(folder, "editor_saved", kRed, nullptr, false);
    run.record("save writes a new shader asset", first.ok && first.name == "editor_saved.glsl" && fs::exists(folder / "editor_saved.glsl"), first.name);
    rescanLibrary();
    run.engine().syncLibrary();
    const int clipIndex = clipIndexByName(shaderClipName("editor_saved"));
    run.record("the saved shader is in the library", clipIndex >= 0, "clip index " + std::to_string(clipIndex));
    run.clearLayers();
    run.place(0, clipIndex);
    run.step(3, false);
    run.expect("the saved shader draws as a clip", run.outputCentre(), 255, 0, 0, 2);

    const shaderedit::SaveResult again = shaderedit::saveShader(folder, "editor_saved", kBlue, nullptr, false);
    run.record("a second save under the same name does not overwrite", !again.ok && again.error == shaderedit::SaveError::AlreadyExists && readText(folder / "editor_saved.glsl") == kRed, "refused, file unchanged");

    const shaderedit::ReadResult loaded = shaderedit::readShader(folder, "editor_saved.glsl");
    const shaderedit::SaveResult updated = shaderedit::saveShader(folder, "editor_saved.glsl", kGreen, &loaded.stamp, false);
    run.record("saving a loaded file with an unchanged disk version works", loaded.ok && updated.ok, std::to_string(static_cast<int>(updated.error)));
    std::error_code ec;
    fs::last_write_time(folder / "editor_saved.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(120), ec);
    const ShaderScan scan = refreshShaderClips();
    run.engine().syncLibrary();
    run.engine().reloadShaderClips(scan.changed);
    run.step(3, false);
    run.record("the library notices the saved file", !scan.changed.empty(), std::to_string(scan.changed.size()) + " changed");
    run.expect("hot reload shows the saved change", run.outputCentre(), 0, 255, 0, 2);

    const shaderedit::ReadResult mine = shaderedit::readShader(folder, "editor_saved.glsl");
    writeText(folder / "editor_saved.glsl", std::string("// edited by another program\n") + kRed);
    fs::last_write_time(folder / "editor_saved.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(240), ec);
    const shaderedit::SaveResult conflict = shaderedit::saveShader(folder, "editor_saved.glsl", kBlue, &mine.stamp, false);
    run.record("a changed disk version is detected before saving", !conflict.ok && conflict.error == shaderedit::SaveError::ChangedOnDisk, std::to_string(static_cast<int>(conflict.error)));
    run.record("the conflict leaves the other program's file alone", readText(folder / "editor_saved.glsl").rfind("// edited by another program", 0) == 0, "file unchanged");
    const shaderedit::SaveResult forced = shaderedit::saveShader(folder, "editor_saved.glsl", kBlue, &mine.stamp, true);
    run.record("overwrite after the conflict works", forced.ok && readText(folder / "editor_saved.glsl") == kBlue, std::to_string(static_cast<int>(forced.error)));

    const shaderedit::SaveResult bad = shaderedit::saveShader(folder, "../outside", kRed, nullptr, false);
    run.record("a name that leaves the folder is refused", !bad.ok && !fs::exists(folder.parent_path() / "outside.glsl"), std::to_string(static_cast<int>(bad.error)));
    const shaderedit::SaveResult big = shaderedit::saveShader(folder, "too_big", std::string(shaderedit::kMaxTextBytes + 1, 'x'), nullptr, false);
    run.record("a text over the limit is not written", !big.ok && big.error == shaderedit::SaveError::TooLarge && !fs::exists(folder / "too_big.glsl"), std::to_string(static_cast<int>(big.error)));
}

json benchmark(Probe& run, int frames) {
    json result;
    run.clearLayers();
    run.engine().releaseShaderPlayground();
    run.step(5, false);
    const auto offStart = Clock::now();
    run.step(frames, false);
    const double offMs = since(offStart) / frames;

    run.submit(kAccumulate);
    run.step(5);
    const auto onStart = Clock::now();
    run.step(frames);
    const double onMs = since(onStart) / frames;
    const double previewMs = run.status().previewMs;

    std::vector<double> compileMs;
    for (int i = 0; i < 8; ++i) {
        const std::string text = std::string("// variant ") + std::to_string(i) + "\nvoid mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(uv, 0.5 + 0.5 * sin(iTime + " + std::to_string(i) + ".0), 1.0); }\n";
        run.submit(text);
        compileMs.push_back(run.status().lastCompileMs);
    }
    std::sort(compileMs.begin(), compileMs.end());
    result["frames"] = frames;
    result["frameMsPlaygroundOff"] = offMs;
    result["frameMsPlaygroundOn"] = onMs;
    result["previewGpuMs"] = previewMs;
    result["compileMsMedian"] = compileMs[compileMs.size() / 2];
    result["compileMsMax"] = compileMs.back();
    result["parallelCompile"] = run.status().parallelCompile;
    result["note"] = "wall clock per frame at the output resolution, including CPU rasterisation when no graphics card is present; compile time is the time from submit to the finished program";
    return result;
}

}

int runShaderEditorSelfTest(Engine& engine, int benchFrames, const std::string& outputPath) {
    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    Probe run(engine, report);
    run.prepare();
    testValidAndInvalid(run);
    testTemplates(run);
    testMultipass(run);
    testGovernor(run);
    testIsolation(run);
    testSaveAndReload(run);
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    report["checks"] = run.items();
    report["failed"] = run.failed();
    report["passed"] = run.failed() == 0;
    if (benchFrames > 0) {
        report["benchmark"] = benchmark(run, benchFrames);
    }
    run.finish();
    const std::string text = report.dump(2);
    logLine("Shader editor self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return run.failed() == 0 ? 0 : 1;
}

}

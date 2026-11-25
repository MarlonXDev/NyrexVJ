#include "app/generator_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "core/particles.hpp"
#include "core/text_doc.hpp"
#include "core/text_font.hpp"
#include "render/clip_library.hpp"
#include "render/generators.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
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

const char* kBlue = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 0.0, 1.0, 1.0); }\n";

void setDataDirectory(const std::string& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void restoreDataDirectory(const std::string& previous, bool existed) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", existed ? previous.c_str() : "");
#else
    if (existed) {
        setenv("NYREXVJ_DATA_DIR", previous.c_str(), 1);
    } else {
        unsetenv("NYREXVJ_DATA_DIR");
    }
#endif
}

void writeText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string hashOf(const std::vector<unsigned char>& pixels) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : pixels) {
        hash = (hash ^ value) * 1099511628211ull;
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

std::string fixed(double value, int digits = 2) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

struct Frame {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;

    bool valid() const { return width > 0 && height > 0; }

    void at(double fx, double fy, int& r, int& g, int& b) const {
        const int x = std::clamp(static_cast<int>(fx * width), 0, width - 1);
        const int y = std::clamp(static_cast<int>(fy * height), 0, height - 1);
        const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
        r = pixels[index];
        g = pixels[index + 1];
        b = pixels[index + 2];
    }

    int count(int minimumSum) const {
        int total = 0;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            total += static_cast<int>(pixels[i]) + static_cast<int>(pixels[i + 1]) + static_cast<int>(pixels[i + 2]) >= minimumSum ? 1 : 0;
        }
        return total;
    }

    int whites() const {
        int total = 0;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            total += (pixels[i] > 200 && pixels[i + 1] > 200 && pixels[i + 2] > 200) ? 1 : 0;
        }
        return total;
    }
};

class Runner {
public:
    explicit Runner(Engine& engine) : engine_(engine) {}

    void prepare() {
        const char* current = std::getenv("NYREXVJ_DATA_DIR");
        previousExisted_ = current != nullptr;
        previous_ = current != nullptr ? current : "";
        savedComposition_ = engine_.comp;
        savedLimit_ = engine_.particleLimit();
        root_ = fs::temp_directory_path() / "nyrexvj_generator_selftest";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        setDataDirectory(root_.string());
        engine_.comp = Composition{};
        engine_.blackout = false;
        engine_.paused = false;
        engine_.setResolutionIndex(0);
        engine_.setParticleLimit(particles::kDefaultLimit);
    }

    void finish() {
        engine_.applyComposition(savedComposition_);
        engine_.setParticleLimit(savedLimit_);
        restoreDataDirectory(previous_, previousExisted_);
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    void rescan() {
        rescanLibrary();
        refreshGeneratorClips();
        engine_.syncLibrary();
    }

    int textClip(const std::string& stem) {
        return clipIndexByName(textClipName(stem));
    }

    int particleClip(const std::string& stem) {
        return clipIndexByName(particleClipName(stem));
    }

    void addShader(const std::string& stem, const std::string& body) {
        writeText(root_ / "shaders" / (stem + ".glsl"), body);
    }

    void setAudio(float level, float kick = 0.0f) {
        audio_ = AudioFeatures{};
        audio_.active = true;
        audio_.level = level;
        audio_.bass = level;
        audio_.mid = level;
        audio_.high = level;
        audio_.kick = kick;
    }

    void step(int count) {
        for (int i = 0; i < count; ++i) {
            engine_.setAudio(audio_);
            engine_.update(1.0 / 60.0);
            engine_.render();
            glFinish();
        }
    }

    bool settle(int clip, int maxFrames = 600) {
        for (int i = 0; i < maxFrames; ++i) {
            step(1);
            GeneratorPlayer* player = engine_.generatorPlayer(clip);
            if (player != nullptr && (player->state() == GeneratorState::Ready || player->state() == GeneratorState::Failed)) {
                step(2);
                return player->state() == GeneratorState::Ready;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
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

    Frame read() {
        Frame frame;
        if (!engine_.readOutput(frame.pixels, frame.width, frame.height)) {
            frame = Frame{};
        }
        return frame;
    }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Generator self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void note(const std::string& name, const std::string& detail) {
        json item;
        item["name"] = name;
        item["skipped"] = true;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        logLine("Generator self test " + name + ": skipped (" + detail + ")");
    }

    Engine& engine() { return engine_; }
    const fs::path& root() const { return root_; }
    int failed() const { return failed_; }
    json& items() { return items_; }

private:
    Engine& engine_;
    fs::path root_;
    std::string previous_;
    Composition savedComposition_;
    int savedLimit_ = particles::kDefaultLimit;
    bool previousExisted_ = false;
    json items_ = json::array();
    int failed_ = 0;
    AudioFeatures audio_;
};

text::TextDoc probeDoc(const std::string& body) {
    text::TextDoc doc;
    doc.text = body;
    doc.font = "Sans";
    doc.size = 0.3f;
    doc.color = text::ColorMode::White;
    doc.hAlign = text::HAlign::Center;
    doc.vAlign = text::VAlign::Middle;
    doc.animation = text::Animation::None;
    doc.wrap = 0.9f;
    return doc;
}

particles::Params probeParticles() {
    particles::Params params;
    params.rate = 300.0f;
    params.burst = 0.0f;
    params.life = 2.0f;
    params.speed = 0.25f;
    params.spread = 360.0f;
    params.originX = 0.0f;
    params.originY = 0.0f;
    params.size = 0.03f;
    params.color = particles::ColorMode::White;
    params.additive = false;
    params.softness = 0.2f;
    params.seed = 7;
    return params;
}

void save(const std::string& name, const text::TextDoc& doc) {
    std::string error;
    text::saveTextDoc(name, doc, error);
}

void save(const std::string& name, const particles::Params& params) {
    std::string error;
    particles::saveParticleDoc(name, params, error);
}

void testText(Runner& run) {
    const bool hasFont = !text::availableFonts().empty();
    run.addShader("blue", kBlue);
    save("probe", probeDoc("HELLO"));
    save("audio", [] {
        text::TextDoc doc = probeDoc("HELLO");
        doc.animation = text::Animation::Audio;
        doc.animAmount = 1.0f;
        return doc;
    }());
    run.rescan();
    const int probe = run.textClip("probe");
    const int audio = run.textClip("audio");
    const int blue = clipIndexByName(shaderClipName("blue"));
    run.record("text clips are in the library", probe >= 0 && audio >= 0 && blue >= 0, "a saved text file becomes a clip after a rescan");
    if (!hasFont) {
        run.note("text drawing", "no font on this computer");
        return;
    }

    run.clearAll();
    run.setAudio(0.0f);
    run.place(0, probe);
    const bool ready = run.settle(probe);
    const Frame alone = run.read();
    const int lit = alone.valid() ? alone.count(60) : 0;
    const double share = alone.valid() ? static_cast<double>(lit) / (static_cast<double>(alone.width) * alone.height) : 0.0;
    run.record("text appears as a normal clip", ready && share > 0.01 && share < 0.4, "letters cover " + fixed(share * 100.0) + " % of the picture");

    int r = 0;
    int g = 0;
    int b = 0;
    if (alone.valid()) {
        alone.at(0.02, 0.02, r, g, b);
    }
    run.record("background of a text clip is transparent black", r + g + b <= 3, "corner pixel (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");

    run.clearAll();
    run.place(0, blue);
    run.place(1, probe);
    run.step(4);
    const Frame over = run.read();
    int cr = 0;
    int cg = 0;
    int cb = 0;
    if (over.valid()) {
        over.at(0.02, 0.02, cr, cg, cb);
    }
    run.record("text over another layer: the layer below stays visible", over.valid() && cr <= 3 && cg <= 3 && cb >= 252, "corner pixel (" + std::to_string(cr) + "," + std::to_string(cg) + "," + std::to_string(cb) + ")");
    run.record("text over another layer: letters are on top", over.valid() && over.whites() > 300, std::to_string(over.valid() ? over.whites() : 0) + " near-white pixels");

    run.clearAll();
    run.place(0, audio);
    run.settle(audio);
    run.setAudio(0.0f);
    run.step(20);
    const int quiet = run.read().count(60);
    run.setAudio(1.0f);
    run.step(20);
    const int loud = run.read().count(60);
    run.record("audio level changes the text", loud > quiet * 1.05, "lit pixels quiet " + std::to_string(quiet) + ", loud " + std::to_string(loud));
    run.setAudio(0.0f);
}

void testTextLimitsAndErrors(Runner& run) {
    if (text::availableFonts().empty()) {
        return;
    }
    std::string many;
    for (int i = 0; i < 400; ++i) {
        many += "Mm Wg Qj 0123456789 ";
    }
    text::TextDoc huge = probeDoc(many);
    huge.size = 0.9f;
    save("huge", huge);
    text::TextDoc missing = probeDoc("fallback");
    missing.font = "Surely Not Installed Font 12345";
    save("missingfont", missing);
    writeText(run.root() / "texts" / "broken.nyxtext", "{ this is not json");
    run.rescan();
    const int hugeClip = run.textClip("huge");
    const int missingClip = run.textClip("missingfont");
    const int brokenClip = run.textClip("broken");
    const int blue = clipIndexByName(shaderClipName("blue"));

    run.clearAll();
    run.setAudio(0.0f);
    run.place(0, blue);
    run.place(1, hugeClip);
    const bool hugeReady = run.settle(hugeClip, 900);
    GeneratorPlayer* player = run.engine().generatorPlayer(hugeClip);
    auto* textPlayer = dynamic_cast<TextPlayer*>(player);
    const int side = textPlayer != nullptr ? textPlayer->atlasSide() : 0;
    const unsigned long long bytes = player != nullptr ? player->gpuBytes() : 0ull;
    run.record("very long text stays inside the atlas limit", (hugeReady || (player != nullptr && player->state() == GeneratorState::Failed)) && side <= text::kMaxAtlasSide && bytes <= static_cast<unsigned long long>(text::kMaxAtlasSide) * text::kMaxAtlasSide, "atlas " + std::to_string(side) + " px, " + std::to_string(bytes / 1024) + " KiB, state " + (hugeReady ? "ready" : "failed"));
    const Frame frame = run.read();
    int r = 0;
    int g = 0;
    int b = 0;
    if (frame.valid()) {
        frame.at(0.01, 0.99, r, g, b);
    }
    run.record("very long text does not stop the show", frame.valid() && (b >= 200 || frame.count(60) > 0), "output still renders, corner (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");

    run.clearAll();
    run.place(0, blue);
    run.place(1, missingClip);
    const bool missingReady = run.settle(missingClip);
    const Frame missingFrame = run.read();
    run.record("an unknown font name falls back instead of failing", missingReady && missingFrame.valid() && missingFrame.whites() > 100, std::string(missingReady ? "text is drawn with the fallback font" : "text could not be drawn") + ", " + std::to_string(missingFrame.valid() ? missingFrame.whites() : 0) + " near-white pixels");

    run.clearAll();
    run.place(0, blue);
    run.place(1, brokenClip);
    run.step(8);
    const Frame brokenFrame = run.read();
    int br = 0;
    int bg = 0;
    int bb = 0;
    if (brokenFrame.valid()) {
        brokenFrame.at(0.5, 0.5, br, bg, bb);
    }
    const std::string brokenError = run.engine().clipError(brokenClip);
    run.record("a damaged text file shows an error and the layer below keeps playing", !brokenError.empty() && bb >= 252, "error '" + brokenError + "', centre (" + std::to_string(br) + "," + std::to_string(bg) + "," + std::to_string(bb) + ")");
}

void testReload(Runner& run) {
    if (text::availableFonts().empty()) {
        return;
    }
    const int probe = run.textClip("probe");
    run.clearAll();
    run.setAudio(0.0f);
    run.place(0, probe);
    run.settle(probe);
    const int before = run.read().count(60);
    save("probe", probeDoc("HELLO HELLO"));
    const ShaderScan scan = refreshGeneratorClips();
    run.engine().reloadGeneratorClips(scan.changed);
    run.settle(probe);
    run.step(10);
    const int after = run.read().count(60);
    run.record("saving a text file reloads the running clip", !scan.changed.empty() && after != before, "lit pixels " + std::to_string(before) + " then " + std::to_string(after));
}

void testParticles(Runner& run) {
    save("steady", probeParticles());
    particles::Params level = probeParticles();
    level.rate = 20.0f;
    level.levelRate = 4.0f;
    save("level", level);
    particles::Params burst = probeParticles();
    burst.rate = 0.0f;
    burst.burst = 200.0f;
    burst.life = 3.0f;
    save("bursty", burst);
    particles::Params dense = probeParticles();
    dense.rate = 2000.0f;
    dense.life = 10.0f;
    save("dense", dense);
    run.rescan();
    const int steady = run.particleClip("steady");
    const int levelClip = run.particleClip("level");
    const int bursty = run.particleClip("bursty");
    const int denseClip = run.particleClip("dense");
    run.record("particle files are clips", steady >= 0 && levelClip >= 0 && bursty >= 0 && denseClip >= 0, "four test files found next to the six built-in presets");

    run.clearAll();
    run.setAudio(0.0f);
    run.place(0, steady);
    run.step(45);
    const Frame first = run.read();
    run.clearAll();
    run.place(0, steady);
    run.step(45);
    const Frame second = run.read();
    run.record("particles are drawn", first.valid() && first.count(60) > 30, std::to_string(first.valid() ? first.count(60) : 0) + " lit pixels");
    run.record("the same start gives the same picture", first.valid() && hashOf(first.pixels) == hashOf(second.pixels), hashOf(first.pixels) + " and " + hashOf(second.pixels));

    run.clearAll();
    run.place(0, steady);
    run.step(40);
    const Frame onBlackSource = run.read();
    int r = 0;
    int g = 0;
    int b = 0;
    if (onBlackSource.valid()) {
        onBlackSource.at(0.01, 0.01, r, g, b);
    }
    run.record("area without particles is transparent black", r + g + b <= 3, "corner pixel (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");

    run.clearAll();
    run.place(0, clipIndexByName(shaderClipName("blue")));
    run.place(1, steady);
    run.step(40);
    const Frame mixed = run.read();
    int mr = 0;
    int mg = 0;
    int mb = 0;
    if (mixed.valid()) {
        mixed.at(0.01, 0.01, mr, mg, mb);
    }
    run.record("particles over another layer: the layer below stays visible", mixed.valid() && mr <= 3 && mg <= 3 && mb >= 252 && mixed.whites() > 20, "corner (" + std::to_string(mr) + "," + std::to_string(mg) + "," + std::to_string(mb) + "), " + std::to_string(mixed.valid() ? mixed.whites() : 0) + " near-white pixels");

    run.clearAll();
    run.place(0, levelClip);
    run.setAudio(0.0f);
    run.step(90);
    auto* quietPlayer = dynamic_cast<ParticlePlayer*>(run.engine().generatorPlayer(levelClip));
    const int quiet = quietPlayer != nullptr ? quietPlayer->alive(0) : -1;
    run.clearAll();
    run.place(0, levelClip);
    run.setAudio(1.0f);
    run.step(90);
    const int loud = quietPlayer != nullptr ? quietPlayer->alive(0) : -1;
    run.record("audio level adds particles", quiet >= 0 && loud > quiet * 2, "alive quiet " + std::to_string(quiet) + ", loud " + std::to_string(loud));
    run.setAudio(0.0f);

    run.clearAll();
    run.place(0, bursty);
    run.step(120);
    auto* burstPlayer = dynamic_cast<ParticlePlayer*>(run.engine().generatorPlayer(bursty));
    const int burstAlive = burstPlayer != nullptr ? burstPlayer->alive(0) : -1;
    run.record("the beat fires bursts without any steady rate", burstAlive >= 150, "alive after four seconds of beats " + std::to_string(burstAlive));

    run.engine().setParticleLimit(128);
    run.clearAll();
    run.place(0, denseClip);
    run.step(150);
    auto* densePlayer = dynamic_cast<ParticlePlayer*>(run.engine().generatorPlayer(denseClip));
    const int denseAlive = densePlayer != nullptr ? densePlayer->alive(0) : -1;
    const long long dropped = densePlayer != nullptr ? densePlayer->dropped() : -1;
    const unsigned long long buffer = densePlayer != nullptr ? densePlayer->gpuBytes() : 0ull;
    run.record("the particle limit holds", denseAlive >= 0 && denseAlive <= 128 && dropped > 0, "alive " + std::to_string(denseAlive) + " of 128, dropped " + std::to_string(dropped));
    run.record("graphics memory follows the limit", buffer > 0 && buffer <= particles::gpuBytes(128), std::to_string(buffer) + " bytes for a limit of 128");

    run.engine().setParticleLimit(5);
    const int low = run.engine().particleLimit();
    run.engine().setParticleLimit(1000000000);
    const int high = run.engine().particleLimit();
    run.record("the limit is clamped to a safe range", low == particles::kMinLimit && high == particles::kHardMaxParticles, "5 became " + std::to_string(low) + ", 1000000000 became " + std::to_string(high));
    run.engine().setParticleLimit(particles::kDefaultLimit);
}

void testShaderFailure(Runner& run) {
    const int steady = run.particleClip("steady");
    const int blue = clipIndexByName(shaderClipName("blue"));
    GeneratorGl& gl = run.engine().generatorGl();
    gl.simulateCompileFailure();
    run.clearAll();
    run.place(0, blue);
    run.place(1, steady);
    run.step(10);
    const Frame frame = run.read();
    int r = 0;
    int g = 0;
    int b = 0;
    if (frame.valid()) {
        frame.at(0.5, 0.5, r, g, b);
    }
    run.record("a broken text and particle shader does not stop the show", frame.valid() && b >= 252 && r <= 3 && g <= 3, "layer below still plays, centre (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")");
    const std::string message = run.engine().clipError(steady);
    run.record("the broken shader is reported on the clip", !message.empty(), message);
    gl.allowRetry();
    run.step(30);
    const Frame recovered = run.read();
    run.record("the clips work again after a retry", recovered.valid() && recovered.whites() > 10, std::to_string(recovered.valid() ? recovered.whites() : 0) + " near-white pixels");
}

json benchmark(Runner& run, int frames) {
    json result = json::object();
    Engine& engine = run.engine();
    auto timed = [&]() {
        run.step(20);
        const auto start = std::chrono::steady_clock::now();
        run.step(frames);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
    };
    const int blue = clipIndexByName(shaderClipName("blue"));
    run.clearAll();
    run.setAudio(0.5f);
    run.place(0, blue);
    {
        json item;
        item["milliseconds"] = timed();
        result["baseline"] = item;
    }
    if (!text::availableFonts().empty()) {
        const int probe = run.textClip("probe");
        run.place(1, probe);
        run.settle(probe);
        json item;
        item["milliseconds"] = timed();
        result["text"] = item;
        run.clearAll();
        run.place(0, blue);
    }
    particles::Params full = probeParticles();
    full.rate = 2000.0f;
    full.burst = static_cast<float>(particles::kMaxBurst);
    full.life = 10.0f;
    full.size = 0.02f;
    save("benchfull", full);
    run.rescan();
    const int clip = run.particleClip("benchfull");
    run.clearAll();
    run.place(0, blue);
    run.place(1, clip);
    for (const int limit : {512, 2048, 8192, 16384}) {
        engine.setParticleLimit(limit);
        for (int i = 0; i < 700; ++i) {
            run.step(1);
            auto* warm = dynamic_cast<ParticlePlayer*>(engine.generatorPlayer(clip));
            if (warm != nullptr && warm->alive(1) >= limit) {
                break;
            }
        }
        json item;
        item["milliseconds"] = timed();
        auto* player = dynamic_cast<ParticlePlayer*>(engine.generatorPlayer(clip));
        item["alive"] = player != nullptr ? player->alive(1) : 0;
        item["limit"] = limit;
        item["generatorGpuBytes"] = engine.gpuMemory().generators;
        result["particles" + std::to_string(limit)] = item;
    }
    engine.setParticleLimit(particles::kDefaultLimit);
    return result;
}

}

int runGeneratorSelfTest(Engine& engine, int benchFrames, const std::string& outputPath) {
    json report;
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    Runner run(engine);
    run.prepare();
    testText(run);
    testTextLimitsAndErrors(run);
    testReload(run);
    testParticles(run);
    testShaderFailure(run);
    report["resolution"] = std::to_string(engine.width()) + "x" + std::to_string(engine.height());
    if (benchFrames > 0) {
        report["benchmark"] = benchmark(run, benchFrames);
        report["benchmarkNote"] = "wall clock milliseconds per frame at the resolution shown, with the whole composition, including CPU rasterisation when no graphics card is present";
    }
    report["checks"] = run.items();
    report["failed"] = run.failed();
    report["passed"] = run.failed() == 0;
    run.finish();
    const std::string text = report.dump(2);
    logLine("Generator self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return run.failed() == 0 ? 0 : 1;
}

}

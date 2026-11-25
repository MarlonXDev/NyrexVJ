#include "app/gpu_particle_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/gpu_particles.hpp"
#include "core/log.hpp"
#include "core/particles.hpp"
#include "render/clip_library.hpp"
#include "render/generators.hpp"
#include "render/gpu_particles.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

class Report {
public:
    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("GPU particle self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void skip(const std::string& name, const std::string& detail) {
        json item;
        item["name"] = name;
        item["skipped"] = true;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        logLine("GPU particle self test " + name + ": skipped (" + detail + ")");
    }

    int failed() const { return failed_; }
    json& items() { return items_; }

private:
    json items_ = json::array();
    int failed_ = 0;
};

std::string fixed(double value, int digits = 2) {
    char text[40];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

double since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

particles::Drive driveAt(int tick, float level) {
    particles::Drive drive;
    drive.clock = static_cast<double>(tick) / 60.0;
    drive.beat = drive.clock * 2.0;
    drive.level = level;
    return drive;
}

float wave(int tick) {
    return 0.5f + 0.5f * std::sin(static_cast<float>(tick) * 0.1f);
}

particles::Params probe() {
    particles::Params p;
    p.gpu = true;
    p.gpuCount = 4096;
    p.rate = 2500.0f;
    p.burst = 1500.0f;
    p.life = 2.0f;
    p.lifeJitter = 0.4f;
    p.speed = 0.5f;
    p.speedJitter = 0.5f;
    p.spread = 360.0f;
    p.gravity = 0.4f;
    p.drag = 0.3f;
    p.turbulence = 0.4f;
    p.swirl = 0.6f;
    p.shape = particles::Shape::Ring;
    p.extent = 0.2f;
    p.originX = 0.3f;
    p.originY = 0.0f;
    p.levelRate = 0.5f;
    p.levelSpeed = 0.5f;
    p.seed = 424242;
    return p;
}

particles::Params nebula() {
    particles::Params p;
    particles::findBuiltin("GPU Nebula", p);
    return p;
}

struct Pixels {
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;

    int lit(int minimumSum = 30) const {
        int total = 0;
        for (size_t i = 0; i + 3 < data.size(); i += 4) {
            total += static_cast<int>(data[i]) + static_cast<int>(data[i + 1]) + static_cast<int>(data[i + 2]) >= minimumSum ? 1 : 0;
        }
        return total;
    }

    std::uint64_t hash() const {
        std::uint64_t value = 1469598103934665603ull;
        for (const unsigned char byte : data) {
            value = (value ^ byte) * 1099511628211ull;
        }
        return value;
    }
};

Pixels capture(RenderTarget& target) {
    Pixels pixels;
    pixels.width = target.width;
    pixels.height = target.height;
    pixels.data.resize(static_cast<size_t>(target.width) * static_cast<size_t>(target.height) * 4u);
    target.bind();
    glReadPixels(0, 0, target.width, target.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return pixels;
}

void clearTarget(RenderTarget& target) {
    target.bind();
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

GpuParticleView viewFor(const RenderTarget& target, float level) {
    GpuParticleView view;
    view.aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
    view.level = level;
    return view;
}

struct Comparison {
    int aliveGpu = 0;
    int aliveReference = 0;
    int slots = 0;
    double positionError = 0.0;
    double ageError = 0.0;
    std::vector<gpuparticles::State> gpu;
};

bool compareRun(GpuParticleGl& gl, const particles::Params& params, int ticks, Comparison& out, std::string& error) {
    GpuParticleSystem system(gl);
    if (!system.create(params, error)) {
        return false;
    }
    const float aspect = 16.0f / 9.0f;
    gpuparticles::Reference reference(system.params(), system.capacity(), aspect);
    for (int tick = 0; tick <= ticks; ++tick) {
        if (!system.advance(driveAt(tick, wave(tick)), aspect)) {
            error = "the update pass failed";
            return false;
        }
        for (const gpuparticles::Step& step : system.lastSteps()) {
            reference.apply(step);
        }
    }
    system.readback(out.gpu);
    out.slots = static_cast<int>(out.gpu.size());
    out.aliveReference = reference.alive(static_cast<std::uint32_t>(out.slots));
    for (int i = 0; i < out.slots; ++i) {
        const gpuparticles::State& g = out.gpu[static_cast<size_t>(i)];
        const gpuparticles::State& r = reference.states()[static_cast<size_t>(i)];
        out.aliveGpu += g.life > 0.0f ? 1 : 0;
        if (g.life > 0.0f && r.life > 0.0f) {
            out.positionError = std::max(out.positionError, static_cast<double>(std::max(std::fabs(g.x - r.x), std::fabs(g.y - r.y))));
            out.ageError = std::max(out.ageError, static_cast<double>(std::fabs(g.age - r.age)));
        }
    }
    return true;
}

bool sameBytes(const std::vector<gpuparticles::State>& a, const std::vector<gpuparticles::State>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(gpuparticles::State)) == 0;
}

bool allFinite(const std::vector<gpuparticles::State>& states) {
    for (const gpuparticles::State& s : states) {
        if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.vx) || !std::isfinite(s.vy) || !std::isfinite(s.age) || !std::isfinite(s.life) || !std::isfinite(s.size)) {
            return false;
        }
    }
    return true;
}

bool stateIsClean() {
    GLint program = 0;
    GLint vao = 0;
    GLint feedback = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, &feedback);
    return program == 0 && vao == 0 && feedback == 0 && !glIsEnabled(GL_RASTERIZER_DISCARD) && !glIsEnabled(GL_BLEND);
}

void testCompare(Report& report, GpuParticleGl& gl) {
    Comparison a;
    std::string error;
    if (!compareRun(gl, probe(), 150, a, error)) {
        report.record("the GPU run can start", false, error);
        return;
    }
    const int difference = std::abs(a.aliveGpu - a.aliveReference);
    report.record("the GPU matches the CPU reference", a.slots > 1000 && a.aliveGpu > 200 && difference <= 3 && a.positionError < 0.005 && a.ageError < 1e-4, std::to_string(a.slots) + " slots, alive " + std::to_string(a.aliveGpu) + " on the GPU and " + std::to_string(a.aliveReference) + " in the reference, largest position difference " + fixed(a.positionError, 6) + ", largest age difference " + fixed(a.ageError, 8));

    Comparison b;
    compareRun(gl, probe(), 150, b, error);
    report.record("the same seed gives the same bytes", sameBytes(a.gpu, b.gpu), std::to_string(a.gpu.size()) + " slots compared bit for bit");

    particles::Params other = probe();
    other.seed = 424243;
    Comparison c;
    compareRun(gl, other, 150, c, error);
    report.record("another seed gives another picture", !sameBytes(a.gpu, c.gpu), "seed 424242 against 424243");

    particles::Params quiet = probe();
    quiet.levelRate = 0.0f;
    quiet.levelSpeed = 0.0f;
    Comparison d;
    Comparison e;
    {
        GpuParticleSystem s1(gl);
        GpuParticleSystem s2(gl);
        s1.create(quiet, error);
        s2.create(quiet, error);
        for (int tick = 0; tick <= 100; ++tick) {
            s1.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
            s2.advance(driveAt(tick, 1.0f), 16.0f / 9.0f);
        }
        s1.readback(d.gpu);
        s2.readback(e.gpu);
    }
    report.record("a parameter without audio ignores the level", sameBytes(d.gpu, e.gpu), "levelRate and levelSpeed are zero");

    particles::Params reactive = probe();
    reactive.levelRate = 2.0f;
    reactive.burst = 0.0f;
    std::uint64_t quietSpawn = 0;
    std::uint64_t loudSpawn = 0;
    {
        GpuParticleSystem s1(gl);
        GpuParticleSystem s2(gl);
        s1.create(reactive, error);
        s2.create(reactive, error);
        for (int tick = 0; tick <= 60; ++tick) {
            s1.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
            s2.advance(driveAt(tick, 1.0f), 16.0f / 9.0f);
        }
        quietSpawn = s1.spawned();
        loudSpawn = s2.spawned();
    }
    report.record("louder audio spawns more particles", loudSpawn > quietSpawn * 2 && loudSpawn < quietSpawn * 4, std::to_string(quietSpawn) + " at level 0 and " + std::to_string(loudSpawn) + " at level 1 in one second");

    particles::Params beat = probe();
    beat.rate = 0.0f;
    beat.burst = 3000.0f;
    beat.gpuCount = 16384;
    std::uint64_t spawned = 0;
    {
        GpuParticleSystem s(gl);
        s.create(beat, error);
        for (int tick = 0; tick <= 180; ++tick) {
            s.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
        }
        spawned = s.spawned();
    }
    report.record("every beat adds one burst", spawned >= 3000 * 5 && spawned <= 3000 * 7 && spawned % 3000 == 0, std::to_string(spawned) + " particles in three seconds at two beats per second");
}

void testRing(Report& report, GpuParticleGl& gl) {
    particles::Params p = probe();
    p.gpuCount = 1024;
    p.rate = 20000.0f;
    p.burst = 0.0f;
    p.life = 6.0f;
    std::string error;
    GpuParticleSystem system(gl);
    if (!system.create(p, error)) {
        report.record("a small pool can be created", false, error);
        return;
    }
    for (int tick = 0; tick <= 240; ++tick) {
        system.advance(driveAt(tick, 0.5f), 16.0f / 9.0f);
    }
    std::vector<gpuparticles::State> states;
    system.readback(states);
    int alive = 0;
    for (const gpuparticles::State& s : states) {
        alive += s.life > 0.0f ? 1 : 0;
    }
    report.record("the ring recycles the oldest slots", system.used() == 1024 && system.recycled() > 50000 && allFinite(states) && alive > 100 && alive <= 1024, std::to_string(system.spawned()) + " spawned into 1024 slots, " + std::to_string(system.recycled()) + " recycled, " + std::to_string(alive) + " alive");
    report.record("the graphics state is left clean", stateIsClean() && glGetError() == GL_NO_ERROR, "no program, vertex array, feedback buffer, discard or blend left behind");
}

void testBudget(Report& report, GpuParticleGl& gl) {
    auto& budget = *gl.budget;
    const std::uint64_t saved = budget.limit();
    budget.setLimit(1024ull * 1024ull);
    std::string error;
    particles::Params big = probe();
    big.gpuCount = particles::kGpuMaxCount;
    {
        GpuParticleSystem one(gl);
        const bool created = one.create(big, error);
        report.record("a pool is cut to the memory budget", created && one.capacity() == 16384 && budget.used() == 16384ull * gpuparticles::kBytesPerParticle && one.gpuBytes() == budget.used(), "one megabyte allows " + std::to_string(created ? one.capacity() : 0) + " particles at 64 bytes each");
        GpuParticleSystem two(gl);
        const bool refused = !two.create(big, error);
        report.record("a second pool is refused when the budget is used up", refused && !error.empty() && budget.used() == one.gpuBytes(), error);
    }
    report.record("the budget is returned when pools end", budget.used() == 0, "used " + std::to_string(budget.used()) + " bytes afterwards");
    budget.setLimit(1000);
    GpuParticleSystem tiny(gl);
    report.record("a budget below one granule creates nothing", !tiny.create(big, error) && budget.used() == 0, error);
    budget.setLimit(saved);

    GpuParticleSystem reuse(gl);
    particles::Params p = probe();
    reuse.create(p, error);
    for (int tick = 0; tick <= 30; ++tick) {
        reuse.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
    }
    particles::Params changed = p;
    changed.speed = 0.9f;
    const bool same = reuse.reconfigure(changed) && reuse.used() == 0 && reuse.params().speed == 0.9f;
    particles::Params resized = p;
    resized.gpuCount = 8192;
    report.record("changing a parameter keeps the buffers", same && !reuse.reconfigure(resized), "a different particle count needs new buffers");
}

void testPixels(Report& report, GpuParticleGl& gl) {
    RenderTarget target;
    if (!target.create(640, 360)) {
        report.skip("the picture", "no render target could be made");
        return;
    }
    auto render = [&](const particles::Params& params, float level, int ticks) {
        Pixels result;
        GpuParticleSystem system(gl);
        std::string error;
        if (!system.create(params, error)) {
            return result;
        }
        clearTarget(target);
        for (int tick = 0; tick <= ticks; ++tick) {
            system.advance(driveAt(tick, level), 16.0f / 9.0f);
        }
        clearTarget(target);
        system.draw(viewFor(target, level));
        glFinish();
        result = capture(target);
        return result;
    };
    particles::Params p = nebula();
    p.gpuCount = 65536;
    const Pixels first = render(p, 0.4f, 150);
    const Pixels second = render(p, 0.4f, 150);
    report.record("particles are visible", first.lit() > 2000, std::to_string(first.lit()) + " of " + std::to_string(first.width * first.height) + " pixels lit");
    report.record("the same clip draws the same picture twice", first.hash() == second.hash() && first.hash() != 0, "picture hash " + std::to_string(first.hash()));
    const Pixels loud = render(p, 1.0f, 150);
    report.record("louder audio changes the picture", loud.hash() != first.hash() && loud.lit() > 500, std::to_string(first.lit()) + " pixels lit at level 0.4 and " + std::to_string(loud.lit()) + " at level 1");

    particles::Params alpha = p;
    alpha.additive = false;
    const Pixels mixed = render(alpha, 0.4f, 150);
    report.record("alpha blending draws a different picture", mixed.lit() > 500 && mixed.hash() != first.hash(), std::to_string(mixed.lit()) + " pixels lit");
    report.record("the graphics state is left clean after drawing", stateIsClean() && glGetError() == GL_NO_ERROR, "no program, vertex array or blend left behind");
    target.destroy();
}

void testPlayer(Report& report, Engine& engine) {
    GeneratorGl& gl = engine.generatorGl();
    std::string error;
    if (!gl.ensure(error)) {
        report.record("the generator shaders compile", false, error);
        return;
    }
    RenderTarget target;
    if (!target.create(480, 270)) {
        report.skip("the player", "no render target could be made");
        return;
    }
    const std::uint64_t savedLimit = gl.gpuParticles.budget->limit();
    gl.gpuParticles.allowRetry();
    ParticlePlayer player("probe", std::string(), false, particles::kDefaultLimit);
    particles::Params p = nebula();
    p.gpuCount = 32768;
    player.setParams(p);
    player.poll(270);
    GeneratorFrame frame;
    frame.width = target.width;
    frame.height = target.height;
    frame.level = 0.5f;
    auto drawFrames = [&](int count) {
        bool drawn = true;
        for (int i = 0; i < count; ++i) {
            frame.clock = static_cast<double>(i) / 60.0;
            frame.beat = frame.clock * 2.0;
            clearTarget(target);
            drawn = player.draw(gl, frame, 1) && drawn;
        }
        glFinish();
        return drawn;
    };

    player.setGpu(true);
    bool drawn = drawFrames(120);
    Pixels onGpu = capture(target);
    report.record("a GPU clip runs on the GPU when it is turned on", drawn && player.backend().gpu && player.backend().note.empty() && player.backend().capacity == 32768 && onGpu.lit() > 500 && player.gpuBytes() == gpuparticles::bytesFor(32768), "capacity " + std::to_string(player.backend().capacity) + ", " + std::to_string(onGpu.lit()) + " pixels lit, " + std::to_string(player.gpuBytes()) + " bytes");

    player.setGpu(false);
    drawn = drawFrames(120);
    Pixels onCpu = capture(target);
    report.record("it falls back to the CPU when the setting is off", drawn && !player.backend().gpu && !player.backend().note.empty() && onCpu.lit() > 100 && player.gpuBytes() <= 4ull * 1024ull * 1024ull && gl.gpuParticles.budget->used() == 0, player.backend().note + ", " + std::to_string(onCpu.lit()) + " pixels lit, graphics memory returned");

    player.setGpu(true);
    gl.gpuParticles.simulateFailure();
    drawn = drawFrames(60);
    report.record("it falls back to the CPU when the shaders do not compile", drawn && !player.backend().gpu && !player.backend().note.empty(), player.backend().note);
    gl.gpuParticles.allowRetry();
    player.setGpu(false);
    player.setGpu(true);
    drawn = drawFrames(60);
    report.record("it returns to the GPU after the fault is gone", drawn && player.backend().gpu, player.backend().gpu ? "GPU again" : player.backend().note);

    gl.gpuParticles.budget->setLimit(4096);
    player.setGpu(false);
    player.setGpu(true);
    drawn = drawFrames(60);
    report.record("it falls back to the CPU when the budget is too small", drawn && !player.backend().gpu && !player.backend().note.empty() && gl.gpuParticles.budget->used() == 0, player.backend().note);
    gl.gpuParticles.budget->setLimit(savedLimit);

    ParticlePlayer plain("plain", "Sparks", true, particles::kDefaultLimit);
    plain.setGpu(true);
    plain.poll(270);
    drawn = plain.draw(gl, frame, 1);
    report.record("a normal particle clip never uses the GPU backend", drawn && !plain.backend().gpu && plain.backend().note.empty(), "the Sparks preset runs on the CPU with the setting on");

    gl.gpuParticles.allowRetry();
    player.release();
    report.record("graphics memory is returned when a clip is released", gl.gpuParticles.budget->used() == 0 && player.gpuSystem(1) == nullptr, "used " + std::to_string(gl.gpuParticles.budget->used()) + " bytes");
    target.destroy();
}

void testEngine(Report& report, Engine& engine) {
    const bool savedEnabled = engine.gpuParticlesEnabled();
    const int savedBudget = engine.gpuParticleBudgetMb();
    const Composition savedComposition = engine.comp;
    engine.comp = Composition{};
    engine.setResolutionIndex(0);
    for (int l = 0; l < kLayerCount; ++l) {
        engine.clearLayer(l);
    }
    rescanLibrary();
    refreshGeneratorClips();
    engine.syncLibrary();
    const int clip = clipIndexByName(particleClipName("GPU Nebula"));
    if (clip < 0) {
        report.record("the GPU presets are in the clip library", false, "GPU Nebula was not found");
        engine.applyComposition(savedComposition);
        return;
    }
    report.record("the GPU presets are in the clip library", clipIndexByName(particleClipName("GPU Beat Burst")) >= 0 && clipIndexByName(particleClipName("GPU Rain")) >= 0, "three clips next to the six CPU presets");
    AudioFeatures audio;
    audio.active = true;
    audio.level = 0.5f;
    audio.bass = 0.5f;
    audio.mid = 0.5f;
    audio.high = 0.5f;
    auto step = [&](int count) {
        for (int i = 0; i < count; ++i) {
            engine.setAudio(audio);
            engine.update(1.0 / 60.0);
            engine.render();
            glFinish();
        }
    };
    auto lit = [&]() {
        std::vector<unsigned char> pixels;
        int w = 0;
        int h = 0;
        if (!engine.readOutput(pixels, w, h)) {
            return -1;
        }
        int total = 0;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            total += static_cast<int>(pixels[i]) + static_cast<int>(pixels[i + 1]) + static_cast<int>(pixels[i + 2]) >= 30 ? 1 : 0;
        }
        return total;
    };
    engine.setGpuParticles(true, 64);
    engine.comp.layers[1].cells[0] = clip;
    engine.trigger(1, 0);
    step(200);
    const int onGpu = lit();
    const size_t gpuBytes = engine.gpuMemory().generators;
    report.record("the engine draws the GPU clip on the GPU", onGpu > 1000 && gpuBytes >= gpuparticles::bytesFor(262144) && engine.gpuParticleBytes() == gpuparticles::bytesFor(262144), std::to_string(onGpu) + " pixels lit, " + std::to_string(gpuBytes / 1024 / 1024) + " MB counted for generators, budget used " + std::to_string(engine.gpuParticleBytes() / 1024 / 1024) + " MB");
    engine.setGpuParticles(false, 64);
    step(200);
    const int onCpu = lit();
    report.record("the engine switches to the CPU without a restart", onCpu > 100 && engine.gpuParticleBytes() == 0 && engine.gpuMemory().generators < gpuBytes, std::to_string(onCpu) + " pixels lit, budget used " + std::to_string(engine.gpuParticleBytes()) + " bytes");
    engine.setGpuParticles(true, 8);
    step(120);
    report.record("a budget smaller than the clip is respected", engine.gpuParticleBytes() <= 8ull * 1024ull * 1024ull && lit() > 100, "budget " + std::to_string(engine.gpuParticleBudgetMb()) + " MB, used " + std::to_string(engine.gpuParticleBytes() / 1024) + " KB");
    engine.setGpuParticles(savedEnabled, savedBudget);
    engine.applyComposition(savedComposition);
    step(5);
}

json benchmarkCapacity(GpuParticleGl& gl, int capacity, int frames, RenderTarget& hd, RenderTarget& uhd) {
    json item = json::object();
    item["capacity"] = capacity;
    item["stateMegabytes"] = static_cast<double>(gpuparticles::bytesFor(capacity)) / (1024.0 * 1024.0);
    particles::Params p = nebula();
    p.gpuCount = capacity;
    p.rate = std::min(particles::kGpuMaxRate, static_cast<float>(capacity) * 0.5f);
    p.burst = 0.0f;
    p.levelRate = 0.0f;
    std::string error;
    GpuParticleSystem system(gl);
    if (!system.create(p, error)) {
        item["error"] = error;
        return item;
    }
    int tick = 0;
    for (; tick < 900 && system.used() < capacity; ++tick) {
        system.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
    }
    for (int i = 0; i < 20; ++i, ++tick) {
        system.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
    }
    glFinish();
    const auto updateStart = Clock::now();
    for (int i = 0; i < frames; ++i, ++tick) {
        system.advance(driveAt(tick, 0.0f), 16.0f / 9.0f);
        glFinish();
    }
    item["updateMs"] = since(updateStart) / frames;
    std::vector<gpuparticles::State> states;
    system.readback(states);
    int alive = 0;
    for (const gpuparticles::State& s : states) {
        alive += s.life > 0.0f ? 1 : 0;
    }
    item["alive"] = alive;
    item["used"] = system.used();
    for (RenderTarget* target : {&hd, &uhd}) {
        clearTarget(*target);
        system.draw(viewFor(*target, 0.0f));
        glFinish();
        const auto drawStart = Clock::now();
        for (int i = 0; i < frames; ++i) {
            clearTarget(*target);
            system.draw(viewFor(*target, 0.0f));
            glFinish();
        }
        item[target == &hd ? "drawMs1080p" : "drawMs4k"] = since(drawStart) / frames;
    }
    item["frameMs1080p"] = item["updateMs"].get<double>() + item["drawMs1080p"].get<double>();
    item["frameMs4k"] = item["updateMs"].get<double>() + item["drawMs4k"].get<double>();
    return item;
}

json benchmark(GpuParticleGl& gl, int frames) {
    json result = json::object();
    RenderTarget hd;
    RenderTarget uhd;
    const bool made = hd.createFloat(1920, 1080) && uhd.createFloat(3840, 2160);
    if (!made) {
        result["error"] = "the render targets could not be made";
        hd.destroy();
        uhd.destroy();
        return result;
    }
    result["renderTargetMegabytes1080p"] = static_cast<double>(hd.bytes()) / (1024.0 * 1024.0);
    result["renderTargetMegabytes4k"] = static_cast<double>(uhd.bytes()) / (1024.0 * 1024.0);
    result["renderTargetFormat"] = "RGBA16F";
    const std::uint64_t saved = gl.budget->limit();
    gl.budget->setLimit(static_cast<std::uint64_t>(gpuparticles::kMaxBudgetMb) * 1024ull * 1024ull);
    json rows = json::array();
    for (const int capacity : {16384, 65536, 262144, 1048576}) {
        rows.push_back(benchmarkCapacity(gl, capacity, frames, hd, uhd));
    }
    gl.budget->setLimit(saved);
    result["capacities"] = rows;

    particles::Params cpu = particles::cpuEquivalent(nebula(), particles::kHardMaxParticles);
    cpu.rate = 2000.0f;
    cpu.life = 10.0f;
    particles::System system(particles::kHardMaxParticles);
    system.setParams(cpu);
    system.reset();
    std::vector<particles::Vertex> vertices;
    int tick = 0;
    for (; tick < 900 && system.alive() < particles::kHardMaxParticles; ++tick) {
        system.advance(driveAt(tick, 0.0f));
    }
    const auto start = Clock::now();
    for (int i = 0; i < frames; ++i, ++tick) {
        const particles::Drive drive = driveAt(tick, 0.0f);
        system.advance(drive);
        system.collect(vertices, drive);
    }
    json cpuRow = json::object();
    cpuRow["alive"] = system.alive();
    cpuRow["advanceAndCollectMs"] = since(start) / frames;
    result["cpuReference"] = cpuRow;
    hd.destroy();
    uhd.destroy();
    return result;
}

}

int runGpuParticleSelfTest(Engine& engine, int benchFrames, const std::string& outputPath) {
    json report = json::object();
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["version"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    Report checks;
    GpuParticleGl local;
    std::string error;
    const bool ready = local.ensure(error, particleFragmentSource());
    checks.record("the GPU particle shaders compile on this driver", ready, ready ? "transform feedback update pass and instanced draw pass" : error);
    if (ready) {
        testCompare(checks, local);
        testRing(checks, local);
        testBudget(checks, local);
        testPixels(checks, local);
        testPlayer(checks, engine);
        testEngine(checks, engine);
        if (benchFrames > 0) {
            report["benchmark"] = benchmark(local, benchFrames);
            report["benchmarkNote"] = "wall clock milliseconds per frame with glFinish after every pass, state buffers use 64 bytes per particle, the draw uses the Nebula preset with additive blending into an RGBA16F target";
        }
    }
    local.release();
    report["checks"] = checks.items();
    report["failed"] = checks.failed();
    report["passed"] = checks.failed() == 0;
    const std::string text = report.dump(2);
    logLine("GPU particle self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return checks.failed() == 0 ? 0 : 1;
}

}

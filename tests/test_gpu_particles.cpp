#include "check.hpp"

#include "core/gpu_particles.hpp"
#include "core/particles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace nvj;
using namespace nvj::gpuparticles;
using particles::Drive;
using particles::Params;

Params gpuParams() {
    Params p;
    p.gpu = true;
    p.rate = 3000.0f;
    p.life = 2.0f;
    p.speed = 0.5f;
    p.turbulence = 0.3f;
    p.swirl = 0.5f;
    p.originX = 0.5f;
    p.levelSpeed = 0.8f;
    p.seed = 77;
    return p;
}

Drive driveAt(int tick, float level = 0.0f, double beatsPerSecond = 2.0) {
    Drive d;
    d.clock = static_cast<double>(tick) / 60.0;
    d.beat = d.clock * beatsPerSecond;
    d.level = level;
    return d;
}

std::uint64_t runHash(const Params& params, int capacity, int ticks, float level, float aspect, int* aliveOut = nullptr, std::uint64_t* spawnedOut = nullptr) {
    Scheduler scheduler;
    scheduler.configure(params, capacity);
    scheduler.reset();
    Reference reference(params, capacity, aspect);
    std::vector<Step> steps;
    for (int tick = 0; tick <= ticks; ++tick) {
        scheduler.advance(driveAt(tick, level), steps);
        for (const Step& step : steps) {
            reference.apply(step);
        }
    }
    if (aliveOut) {
        *aliveOut = reference.alive(static_cast<std::uint32_t>(scheduler.used()));
    }
    if (spawnedOut) {
        *spawnedOut = scheduler.spawned();
    }
    return reference.stateHash(static_cast<std::uint32_t>(scheduler.used()));
}

void testHashAndMemory() {
    CHECK(hash(1) == hash(1));
    CHECK(hash(1) != hash(2));
    std::set<std::uint32_t> seen;
    bool inRange = true;
    for (std::uint32_t i = 0; i < 5000; ++i) {
        const std::uint32_t h = hash(i);
        seen.insert(h);
        const float u = unit(h);
        inRange = inRange && u >= 0.0f && u < 1.0f;
    }
    CHECK(inRange);
    CHECK(seen.size() > 4990);

    CHECK(kBytesPerParticle == 64);
    CHECK(bytesFor(1024) == 65536);
    CHECK(bytesFor(-5) == 0);
    CHECK(clampBudgetMb(1) == kMinBudgetMb && clampBudgetMb(100000) == kMaxBudgetMb && clampBudgetMb(64) == 64);

    const std::uint64_t mb = 1024ull * 1024ull;
    CHECK(capacityFor(65536, 64 * mb) == 65536);
    CHECK(capacityFor(particles::kGpuMaxCount, 64 * mb) == 1048576);
    CHECK(capacityFor(particles::kGpuMaxCount, 32 * mb) == 524288);
    CHECK(capacityFor(100000, 1 * mb) == 16384);
    CHECK(capacityFor(100000, 10000) == 0);
    CHECK(capacityFor(5, 64 * mb) == particles::kGpuMinCount);
    CHECK(capacityFor(200000000, 1024 * mb) == particles::kGpuMaxCount);

    Budget budget;
    budget.setLimit(10 * mb);
    CHECK(budget.available() == 10 * mb && budget.used() == 0);
    CHECK(budget.tryReserve(6 * mb));
    CHECK(!budget.tryReserve(6 * mb));
    CHECK(budget.used() == 6 * mb && budget.available() == 4 * mb);
    CHECK(budget.tryReserve(4 * mb));
    CHECK(budget.available() == 0);
    budget.release(6 * mb);
    CHECK(budget.used() == 4 * mb);
    budget.release(100 * mb);
    CHECK(budget.used() == 0);
    budget.setLimit(2 * mb);
    CHECK(budget.tryReserve(2 * mb) && !budget.tryReserve(1));
    budget.setLimit(1 * mb);
    CHECK(budget.available() == 0 && !budget.tryReserve(1));
}

void testScheduler() {
    Params p = gpuParams();
    p.rate = 600.0f;
    p.burst = 0.0f;
    Scheduler s;
    s.configure(p, 65536);
    s.reset();
    std::vector<Step> steps;
    int total = 0;
    for (int tick = 0; tick <= 60; ++tick) {
        s.advance(driveAt(tick), steps);
        total += static_cast<int>(steps.size());
    }
    CHECK(total == 60);
    CHECK(s.spawned() >= 590 && s.spawned() <= 600);
    CHECK(s.used() == static_cast<int>(s.spawned()));
    CHECK(s.recycled() == 0);

    Params loud = p;
    loud.levelRate = 2.0f;
    Scheduler a;
    a.configure(loud, 65536);
    a.reset();
    Scheduler b;
    b.configure(loud, 65536);
    b.reset();
    for (int tick = 0; tick <= 60; ++tick) {
        a.advance(driveAt(tick, 0.0f), steps);
        b.advance(driveAt(tick, 1.0f), steps);
    }
    CHECK(b.spawned() > a.spawned() * 2);

    Params burst = p;
    burst.rate = 0.0f;
    burst.burst = 5000.0f;
    Scheduler c;
    c.configure(burst, 65536);
    c.reset();
    int beats = 0;
    for (int tick = 0; tick <= 120; ++tick) {
        c.advance(driveAt(tick, 0.0f, 2.0), steps);
    }
    beats = static_cast<int>(c.spawned() / 5000);
    CHECK(beats >= 3 && beats <= 5);
    CHECK(c.spawned() % 5000 == 0);

    Scheduler small;
    small.configure(burst, 1024);
    small.reset();
    for (int tick = 0; tick <= 120; ++tick) {
        small.advance(driveAt(tick, 0.0f, 2.0), steps);
        for (const Step& step : steps) {
            CHECK(step.spawn <= 1024 && step.used <= 1024 && step.head < 1024);
        }
    }
    CHECK(small.used() == 1024);
    CHECK(small.recycled() == small.spawned() - 1024);

    Scheduler wrap;
    Params fast = p;
    fast.rate = 30000.0f;
    wrap.configure(fast, 1536);
    wrap.reset();
    bool headsValid = true;
    std::set<std::uint32_t> heads;
    for (int tick = 0; tick <= 30; ++tick) {
        wrap.advance(driveAt(tick), steps);
        for (const Step& step : steps) {
            headsValid = headsValid && step.head < 1536 && step.spawn <= 1536;
            heads.insert(step.head);
        }
    }
    CHECK(headsValid && heads.size() > 3);

    Scheduler jump;
    jump.configure(p, 65536);
    jump.reset();
    jump.advance(driveAt(0), steps);
    jump.advance(driveAt(30), steps);
    CHECK(static_cast<int>(steps.size()) == particles::kMaxStepsPerCall);
    jump.advance(driveAt(10), steps);
    CHECK(jump.used() <= 10);
    CHECK(static_cast<int>(steps.size()) <= 1);

    Drive bad;
    bad.clock = std::nan("");
    Scheduler guard;
    guard.configure(p, 4096);
    guard.reset();
    CHECK(guard.advance(bad, steps) == 0 && guard.used() == 0);
    bad.clock = 1.0;
    bad.beat = INFINITY;
    CHECK(guard.advance(bad, steps) == 0);
    Drive weird = driveAt(0);
    weird.level = std::nanf("");
    guard.advance(weird, steps);
    weird = driveAt(1);
    weird.level = 50.0f;
    guard.advance(weird, steps);
    CHECK(steps.empty() || (steps[0].level >= 0.0f && steps[0].level <= 1.0f));
}

void testReference() {
    const Params p = gpuParams();
    int aliveA = 0;
    int aliveB = 0;
    std::uint64_t spawnedA = 0;
    const std::uint64_t a = runHash(p, 16384, 120, 0.3f, 16.0f / 9.0f, &aliveA, &spawnedA);
    const std::uint64_t b = runHash(p, 16384, 120, 0.3f, 16.0f / 9.0f, &aliveB);
    CHECK(a == b && aliveA == aliveB);
    CHECK(aliveA > 100 && aliveA <= static_cast<int>(spawnedA));

    Params other = p;
    other.seed = 78;
    CHECK(runHash(other, 16384, 120, 0.3f, 16.0f / 9.0f) != a);
    CHECK(runHash(p, 16384, 120, 0.9f, 16.0f / 9.0f) != a);
    CHECK(runHash(p, 16384, 120, 0.3f, 4.0f / 3.0f) != a);

    Params still = p;
    still.turbulence = 0.0f;
    still.swirl = 0.0f;
    CHECK(runHash(still, 16384, 120, 0.3f, 16.0f / 9.0f) != a);

    Scheduler scheduler;
    Params tight = p;
    tight.rate = 60000.0f;
    tight.life = 8.0f;
    scheduler.configure(tight, 2048);
    scheduler.reset();
    Reference reference(tight, 2048, 16.0f / 9.0f);
    std::vector<Step> steps;
    bool finite = true;
    for (int tick = 0; tick <= 300; ++tick) {
        scheduler.advance(driveAt(tick, 0.5f), steps);
        for (const Step& step : steps) {
            reference.apply(step);
        }
        for (const State& s : reference.states()) {
            finite = finite && std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.vx) && std::isfinite(s.vy) && std::isfinite(s.age) && std::isfinite(s.life) && std::isfinite(s.size);
        }
    }
    CHECK(finite);
    CHECK(scheduler.used() == 2048);
    CHECK(scheduler.recycled() > 0);
    CHECK(reference.alive(2048) > 1000);

    Params fall = p;
    fall.gravity = 3.0f;
    fall.speed = 0.0f;
    fall.originY = 0.0f;
    fall.life = 10.0f;
    fall.swirl = 0.0f;
    fall.turbulence = 0.0f;
    fall.rate = 5000.0f;
    Scheduler fs;
    fs.configure(fall, 8192);
    fs.reset();
    Reference fr(fall, 8192, 16.0f / 9.0f);
    for (int tick = 0; tick <= 240; ++tick) {
        fs.advance(driveAt(tick), steps);
        for (const Step& step : steps) {
            fr.apply(step);
        }
    }
    bool inside = true;
    for (const State& s : fr.states()) {
        if (s.life > 0.0f) {
            inside = inside && std::fabs(s.y) <= 1.35f + 0.1f;
        }
    }
    CHECK(inside);
    CHECK(fr.alive(8192) < static_cast<int>(fs.spawned()));

    Params fastLevel = p;
    fastLevel.levelSpeed = 2.0f;
    fastLevel.turbulence = 0.0f;
    fastLevel.swirl = 0.0f;
    fastLevel.drag = 0.0f;
    fastLevel.speedJitter = 0.0f;
    fastLevel.spread = 0.0f;
    fastLevel.lifeJitter = 0.0f;
    fastLevel.rate = 600.0f;
    float quiet = 0.0f;
    float loudSpeed = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
        Scheduler ss;
        ss.configure(fastLevel, 4096);
        ss.reset();
        Reference rr(fastLevel, 4096, 16.0f / 9.0f);
        for (int tick = 0; tick <= 2; ++tick) {
            ss.advance(driveAt(tick, pass == 0 ? 0.0f : 1.0f), steps);
            for (const Step& step : steps) {
                rr.apply(step);
            }
        }
        const State& first = rr.states()[0];
        (pass == 0 ? quiet : loudSpeed) = std::sqrt(first.vx * first.vx + first.vy * first.vy);
    }
    CHECK(quiet > 0.4f && quiet < 0.6f);
    CHECK(loudSpeed > quiet * 2.5f);
}

void testParams() {
    Params p = gpuParams();
    p.gpuCount = 300000;
    p.rate = 150000.0f;
    p.burst = 40000.0f;
    std::string error;
    Params back;
    CHECK(particles::paramsFromJson(particles::paramsToJson(p), back, error));
    CHECK(back.gpu && back.gpuCount == 300000 && back.rate == 150000.0f && back.burst == 40000.0f && back.swirl == p.swirl);

    Params cpu = p;
    cpu.gpu = false;
    particles::clampParams(cpu);
    CHECK(cpu.rate == particles::kCpuMaxRate && cpu.burst == static_cast<float>(particles::kMaxBurst));

    Params old;
    CHECK(particles::paramsFromJson("{\"format\":\"nyrexvj-particles\",\"version\":1,\"rate\":100}", old, error));
    CHECK(!old.gpu && old.swirl == 0.0f && old.gpuCount == particles::kGpuDefaultCount);

    Params wild;
    CHECK(particles::paramsFromJson("{\"format\":\"nyrexvj-particles\",\"version\":1,\"backend\":\"gpu\",\"rate\":1e12,\"gpuCount\":5,\"swirl\":99}", wild, error));
    CHECK(wild.gpu && wild.rate == particles::kGpuMaxRate && wild.gpuCount == particles::kGpuMinCount && wild.swirl == 4.0f);
    CHECK(particles::paramsFromJson("{\"format\":\"nyrexvj-particles\",\"version\":1,\"backend\":\"gpu\",\"gpuCount\":99999999999}", wild, error));
    CHECK(wild.gpuCount == particles::kGpuMaxCount);
    CHECK(particles::paramsFromJson("{\"format\":\"nyrexvj-particles\",\"version\":1,\"backend\":\"tpu\"}", wild, error) && !wild.gpu);

    const Params fallback = particles::cpuEquivalent(p, 2048);
    CHECK(!fallback.gpu);
    CHECK(fallback.rate * fallback.life <= 2048.0f * 0.9f + 0.5f);
    CHECK(fallback.burst <= static_cast<float>(particles::kMaxBurst));
    Params gentle = gpuParams();
    gentle.rate = 100.0f;
    gentle.life = 1.0f;
    CHECK(particles::cpuEquivalent(gentle, 2048).rate == 100.0f);

    CHECK(particles::gpuPresets().size() == 3);
    for (const particles::Preset& preset : particles::gpuPresets()) {
        CHECK(preset.name.rfind("GPU ", 0) == 0);
        CHECK(preset.params.gpu);
        CHECK(preset.params.rate <= particles::kGpuMaxRate && preset.params.gpuCount >= particles::kGpuMinCount && preset.params.gpuCount <= particles::kGpuMaxCount);
        CHECK(capacityFor(preset.params.gpuCount, 64ull * 1024ull * 1024ull) == preset.params.gpuCount);
        Params found;
        CHECK(particles::findBuiltin(preset.name, found) && found.gpu && found.seed == preset.params.seed);
        Params parsed;
        CHECK(particles::paramsFromJson(particles::paramsToJson(preset.params), parsed, error));
        CHECK(parsed.gpu && parsed.gpuCount == preset.params.gpuCount && parsed.swirl == preset.params.swirl && parsed.seed == preset.params.seed);
        const Params equal = particles::cpuEquivalent(preset.params, 2048);
        particles::System system(2048);
        system.setParams(equal);
        system.reset();
        int peak = 0;
        for (int tick = 0; tick <= 600; ++tick) {
            Drive d;
            d.clock = tick / 60.0;
            d.beat = d.clock * 2.0;
            d.level = 0.5f;
            system.advance(d);
            peak = std::max(peak, system.alive());
        }
        CHECK(peak > 20 && peak <= 2048);
    }
}

void testCpuSwirl() {
    Params flat;
    flat.turbulence = 0.0f;
    flat.speed = 0.0f;
    flat.originY = 0.0f;
    flat.shape = particles::Shape::Ring;
    flat.extent = 0.3f;
    flat.drag = 0.0f;
    Params turning = flat;
    turning.swirl = 1.5f;
    particles::System a(2048);
    a.setParams(flat);
    a.reset();
    particles::System b(2048);
    b.setParams(turning);
    b.reset();
    for (int tick = 0; tick <= 90; ++tick) {
        Drive d = driveAt(tick);
        a.advance(d);
        b.advance(d);
    }
    CHECK(a.stateHash() != b.stateHash());
    bool finite = true;
    float speed = 0.0f;
    for (const particles::Particle& p : b.particles()) {
        finite = finite && std::isfinite(p.x) && std::isfinite(p.vx);
        speed = std::max(speed, std::fabs(p.vx) + std::fabs(p.vy));
    }
    CHECK(finite && speed > 0.05f);
    particles::System c(2048);
    c.setParams(turning);
    c.reset();
    for (int tick = 0; tick <= 90; ++tick) {
        c.advance(driveAt(tick));
    }
    CHECK(b.stateHash() == c.stateHash());
}

}

int main() {
    testHashAndMemory();
    testScheduler();
    testReference();
    testParams();
    testCpuSwirl();
    return testing::finish();
}

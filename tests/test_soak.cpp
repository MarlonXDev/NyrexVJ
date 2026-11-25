#include "check.hpp"
#include "core/growth_check.hpp"
#include "core/soak_plan.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct Lcg {
    uint64_t state = 12345;

    double next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
    }
};

std::vector<double> series(size_t count, double start, double slope, double noise, uint64_t seed) {
    Lcg random;
    random.state = seed;
    std::vector<double> values;
    for (size_t i = 0; i < count; ++i) {
        values.push_back(start + slope * static_cast<double>(i) + (random.next() - 0.5) * 2.0 * noise);
    }
    return values;
}

void testMedian() {
    CHECK(nvj::medianOf({}) == 0.0);
    CHECK(nvj::medianOf({4.0}) == 4.0);
    CHECK(nvj::medianOf({3.0, 1.0, 2.0}) == 2.0);
    CHECK(nvj::medianOf({4.0, 1.0, 3.0, 2.0}) == 2.5);
    CHECK(nvj::medianOf({1.0, 1000.0, 2.0, 3.0, 2.0}) == 2.0);
}

void testTooFewSamples() {
    const nvj::GrowthLimits limits{10.0, 0.05};
    CHECK(!nvj::assessGrowth({}, 0.25, limits).enough);
    CHECK(!nvj::assessGrowth(series(5, 100.0, 50.0, 0.0, 1), 0.0, limits).enough);
    CHECK(!nvj::assessGrowth(series(5, 100.0, 50.0, 0.0, 1), 0.0, limits).leak);
    CHECK(!nvj::assessGrowth(series(7, 100.0, 50.0, 0.0, 1), 0.5, limits).enough);
    CHECK(nvj::assessGrowth(series(6, 100.0, 0.0, 0.0, 1), 0.0, limits).enough);
}

void testFlat() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    const nvj::GrowthResult exact = nvj::assessGrowth(series(40, 500.0, 0.0, 0.0, 2), 0.25, limits);
    CHECK(exact.enough && !exact.leak && exact.growth == 0.0);
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        const nvj::GrowthResult noisy = nvj::assessGrowth(series(60, 500.0, 0.0, 8.0, seed), 0.25, limits);
        CHECK(noisy.enough);
        CHECK(!noisy.leak);
    }
}

void testSteadyLeak() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        const nvj::GrowthResult leak = nvj::assessGrowth(series(60, 500.0, 5.0, 8.0, seed), 0.25, limits);
        CHECK(leak.leak);
        CHECK(leak.growth > leak.limit);
    }
}

void testWarmupIsIgnored() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    std::vector<double> values;
    for (int i = 0; i < 15; ++i) {
        values.push_back(100.0 + 40.0 * i);
    }
    for (int i = 0; i < 45; ++i) {
        values.push_back(700.0);
    }
    CHECK(!nvj::assessGrowth(values, 0.25, limits).leak);
    CHECK(nvj::assessGrowth(values, 0.0, limits).leak);
}

void testLateStep() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    std::vector<double> values(40, 500.0);
    for (int i = 0; i < 20; ++i) {
        values.push_back(700.0);
    }
    CHECK(nvj::assessGrowth(values, 0.25, limits).leak);
}

void testSingleSpikeIsNotALeak() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    std::vector<double> values(60, 500.0);
    values[58] = 5000.0;
    values[59] = 5000.0;
    CHECK(!nvj::assessGrowth(values, 0.25, limits).leak);
    values[30] = 90000.0;
    CHECK(!nvj::assessGrowth(values, 0.25, limits).leak);
}

void testShrinkingIsNotALeak() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    CHECK(!nvj::assessGrowth(series(60, 900.0, -4.0, 2.0, 5), 0.25, limits).leak);
}

void testLimits() {
    const std::vector<double> values = series(60, 100.0, 0.2, 0.0, 7);
    const nvj::GrowthResult strict = nvj::assessGrowth(values, 0.0, nvj::GrowthLimits{1.0, 0.0});
    CHECK(strict.leak);
    const nvj::GrowthResult absolute = nvj::assessGrowth(values, 0.0, nvj::GrowthLimits{50.0, 0.0});
    CHECK(!absolute.leak);
    CHECK(std::fabs(absolute.limit - 50.0) < 1e-9);
    const nvj::GrowthResult relative = nvj::assessGrowth(values, 0.0, nvj::GrowthLimits{0.0, 0.5});
    CHECK(!relative.leak);
    CHECK(relative.limit > 50.0);
    const nvj::GrowthResult zeroStart = nvj::assessGrowth(series(30, 0.0, 1.0, 0.0, 9), 0.0, nvj::GrowthLimits{0.0, 0.5});
    CHECK(zeroStart.leak);
}

void testBadWarmupValues() {
    const nvj::GrowthLimits limits{16.0, 0.10};
    const std::vector<double> values = series(60, 500.0, 0.0, 0.0, 3);
    CHECK(nvj::assessGrowth(values, -1.0, limits).enough);
    CHECK(nvj::assessGrowth(values, 5.0, limits).enough);
    CHECK(!nvj::assessGrowth(values, 5.0, limits).leak);
    CHECK(!nvj::assessGrowth(series(50, 500.0, 0.0, 0.0, 3), 5.0, limits).enough);
    CHECK(nvj::assessGrowth(values, std::nan(""), limits).enough);
}

void testSpecParsing() {
    nvj::SoakPlan plan;
    std::string error;
    CHECK(nvj::parseSoakSpec("60", plan, error));
    CHECK(plan.seconds == 60.0 && plan.fps == 60);
    CHECK(nvj::parseSoakSpec("3600,30", plan, error));
    CHECK(plan.seconds == 3600.0 && plan.fps == 30);
    CHECK(std::fabs(plan.sampleSeconds - 60.0) < 1e-9);
    CHECK(nvj::parseSoakSpec("5", plan, error));
    CHECK(std::fabs(plan.sampleSeconds - 0.5) < 1e-9);
    for (const char* good : {"60,30", "60,60", "60,90", "60,120", "60,144"}) {
        CHECK(nvj::parseSoakSpec(good, plan, error));
    }
    CHECK(plan.fps == 144);
    CHECK(nvj::parseSoakSpec("86400,30", plan, error));
    CHECK(nvj::parseSoakSpec("90.5,60", plan, error));
    CHECK(plan.seconds == 90.5 && plan.fps == 60);

    for (const char* bad : {"", ",", "abc", "4", "4.9", "86401", "-60", "60,", "60,abc", "60,23", "60,24", "60,45", "60,0", "60,-60", "60,145", "60,360", "60,361", "60,59.5", "60,30,1", "nan", "inf", "1e999", " 60", "60 ", "0x3C", "6 0", "60\n"}) {
        error.clear();
        CHECK(!nvj::parseSoakSpec(bad, plan, error));
        CHECK(!error.empty());
        CHECK(plan.seconds == 60.0 && plan.fps == 60);
    }
}

void testSampleSeconds() {
    CHECK(nvj::soakSampleSeconds(5.0) == 0.5);
    CHECK(nvj::soakSampleSeconds(60.0) == 2.0);
    CHECK(nvj::soakSampleSeconds(900.0) == 30.0);
    CHECK(nvj::soakSampleSeconds(86400.0) == 60.0);
}

void testEvents() {
    const nvj::SoakPlan plan;
    const nvj::SoakEvents first = nvj::soakEventsAt(plan, 0);
    CHECK(!first.look && !first.deck && !first.quality);
    const nvj::SoakEvents negative = nvj::soakEventsAt(plan, -3);
    CHECK(!negative.look && !negative.deck && !negative.quality);
    int looks = 0;
    int decks = 0;
    int qualities = 0;
    for (int second = 0; second < 660; ++second) {
        const nvj::SoakEvents events = nvj::soakEventsAt(plan, second);
        looks += events.look ? 1 : 0;
        decks += events.deck ? 1 : 0;
        qualities += events.quality ? 1 : 0;
    }
    CHECK(looks == 32);
    CHECK(decks == 94);
    CHECK(qualities == 59);
    CHECK(nvj::soakEventsAt(plan, 140).look && nvj::soakEventsAt(plan, 140).deck);
    nvj::SoakPlan off;
    off.lookEvery = 0;
    off.deckEvery = 0;
    off.qualityEvery = 0;
    for (int second = 0; second < 100; ++second) {
        const nvj::SoakEvents events = nvj::soakEventsAt(off, second);
        CHECK(!events.look && !events.deck && !events.quality);
    }
}

}

int main() {
    testSpecParsing();
    testSampleSeconds();
    testEvents();
    testMedian();
    testTooFewSamples();
    testFlat();
    testSteadyLeak();
    testWarmupIsIgnored();
    testLateStep();
    testSingleSpikeIsNotALeak();
    testShrinkingIsNotALeak();
    testLimits();
    testBadWarmupValues();
    return testing::finish();
}

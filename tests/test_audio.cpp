#include "audio/audio_analyzer.hpp"
#include "audio/audio_system.hpp"
#include "audio/synthetic_source.hpp"
#include "core/composition.hpp"
#include "check.hpp"

#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979;

void runSine(nvj::AudioAnalyzer& analyzer, double frequency, double amplitude, double seconds, int rate) {
    const int chunk = 800;
    double phase = 0.0;
    std::vector<float> block(chunk);
    for (double t = 0.0; t < seconds; t += static_cast<double>(chunk) / rate) {
        for (int i = 0; i < chunk; ++i) {
            block[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(phase));
            phase += 2.0 * kPi * frequency / rate;
        }
        analyzer.push(block.data(), block.size());
        analyzer.advance(static_cast<double>(chunk) / rate);
    }
}

void testBands() {
    {
        nvj::AudioAnalyzer analyzer;
        analyzer.configure(48000);
        runSine(analyzer, 80.0, 0.5, 1.0, 48000);
        const nvj::AudioFeatures& f = analyzer.features();
        CHECK(f.bass > 0.6f);
        CHECK(f.high < 0.35f);
        CHECK(f.level > 0.4f);
    }
    {
        nvj::AudioAnalyzer analyzer;
        analyzer.configure(48000);
        runSine(analyzer, 6000.0, 0.5, 1.0, 48000);
        const nvj::AudioFeatures& f = analyzer.features();
        CHECK(f.high > 0.6f);
        CHECK(f.bass < 0.35f);
    }
    {
        nvj::AudioAnalyzer analyzer;
        analyzer.configure(44100);
        runSine(analyzer, 1000.0, 0.5, 1.0, 44100);
        const nvj::AudioFeatures& f = analyzer.features();
        CHECK(f.mid > 0.6f);
        CHECK(f.bass < 0.35f);
        CHECK(f.high < 0.35f);
    }
    {
        nvj::AudioAnalyzer analyzer;
        analyzer.configure(48000);
        for (int i = 0; i < 120; ++i) {
            analyzer.advance(1.0 / 60.0);
        }
        const nvj::AudioFeatures& f = analyzer.features();
        CHECK(f.bass < 0.05f && f.mid < 0.05f && f.high < 0.05f && f.level < 0.05f);
        CHECK(f.beatCount == 0);
    }
    {
        nvj::AudioAnalyzer analyzer;
        analyzer.configure(48000);
        runSine(analyzer, 80.0, 0.5, 0.6, 48000);
        for (int i = 0; i < 90; ++i) {
            analyzer.advance(1.0 / 60.0);
        }
        CHECK(analyzer.features().bass < 0.1f);
    }
}

void testSpectrumShape() {
    nvj::AudioAnalyzer analyzer;
    analyzer.configure(48000);
    runSine(analyzer, 1000.0, 0.5, 1.0, 48000);
    const nvj::AudioFeatures& f = analyzer.features();
    int peakBin = 0;
    for (int i = 0; i < nvj::kSpectrumBins; ++i) {
        if (f.spectrum[static_cast<size_t>(i)] > f.spectrum[static_cast<size_t>(peakBin)]) {
            peakBin = i;
        }
    }
    const double expected = std::log(1000.0 / 30.0) / std::log(16000.0 / 30.0) * nvj::kSpectrumBins;
    CHECK(std::fabs(static_cast<double>(peakBin) - expected) < 6.0);
    for (float value : f.spectrum) {
        CHECK(value >= 0.0f && value <= 1.0f);
    }
    for (float value : f.wave) {
        CHECK(value >= 0.0f && value <= 1.0f);
    }
}

void testTempo(float bpm) {
    nvj::AudioSystem system;
    CHECK(system.select(nvj::AudioSystem::demoId()));
    CHECK(system.active());
    for (int i = 0; i < 60 * 14; ++i) {
        system.update(1.0 / 60.0, bpm);
    }
    const nvj::AudioFeatures& f = system.features();
    CHECK(f.active);
    CHECK(f.beatCount >= static_cast<unsigned>(bpm / 60.0f * 14.0f * 0.7f));
    CHECK(f.bpmConfidence > 0.5f);
    CHECK(std::fabs(f.bpm - bpm) < bpm * 0.04f);
}

void testSystem() {
    nvj::AudioSystem system;
    CHECK(!system.active());
    CHECK(system.select(""));
    CHECK(!system.select("out:does-not-exist"));
    CHECK(!system.error().empty());
    CHECK(system.select(nvj::AudioSystem::demoId()));
    for (int i = 0; i < 180; ++i) {
        system.update(1.0 / 60.0, 120.0f);
    }
    CHECK(system.features().bass > 0.3f);
    CHECK(system.select(""));
    for (int i = 0; i < 240; ++i) {
        system.update(1.0 / 60.0, 120.0f);
    }
    CHECK(!system.features().active);
    CHECK(system.features().bass < 0.05f);
}

void testVirtualAudio() {
    nvj::AudioFeatures a = nvj::virtualAudio(0.0, 0.0);
    nvj::AudioFeatures b = nvj::virtualAudio(0.5, 0.25);
    CHECK(!a.active);
    CHECK(a.kick > 0.95f);
    CHECK(b.kick < a.kick);
    for (int i = 0; i < nvj::kSpectrumBins; ++i) {
        CHECK(a.spectrum[static_cast<size_t>(i)] >= 0.0f && a.spectrum[static_cast<size_t>(i)] <= 1.0f);
    }
    CHECK(a.band(nvj::BandOff) == 0.0f);
    CHECK(a.band(nvj::BandKick) == a.kick);
}

void testNames() {
    for (int i = 0; i < nvj::AudioBandCount; ++i) {
        CHECK(nvj::audioBandFromName(nvj::audioBandName(i)) == i);
    }
    for (int i = 0; i < nvj::ModTargetCount; ++i) {
        CHECK(nvj::modTargetFromName(nvj::modTargetName(i)) == i);
    }
    CHECK(nvj::audioBandFromName("nonsense") == nvj::BandOff);
}

}

int main() {
    testBands();
    testSpectrumShape();
    testTempo(128.0f);
    testTempo(100.0f);
    testTempo(150.0f);
    testSystem();
    testVirtualAudio();
    testNames();
    return testing::finish();
}

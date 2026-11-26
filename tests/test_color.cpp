#include "check.hpp"

#include "core/color_lut.hpp"
#include "core/color_model.hpp"
#include "core/output_map_json.hpp"
#include "core/settings.hpp"
#include "core/venue_setup.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace nvj;

bool near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

bool nearColor(const Color3& a, const Color3& b, double eps) {
    return near(a[0], b[0], eps) && near(a[1], b[1], eps) && near(a[2], b[2], eps);
}

std::uint32_t lcg(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

double unit(std::uint32_t& state) {
    return static_cast<double>(lcg(state) >> 8) / 16777216.0;
}

void testNames() {
    for (Primaries p : {Primaries::Rec709, Primaries::DisplayP3, Primaries::Rec2020}) {
        CHECK(primariesFromName(primariesName(p), Primaries::Rec709) == p);
        CHECK(std::string(primariesLabel(p)).size() > 3);
    }
    CHECK(primariesFromName("nonsense", Primaries::DisplayP3) == Primaries::DisplayP3);
    CHECK(primariesFromName("", Primaries::Rec2020) == Primaries::Rec2020);
    for (Transfer t : {Transfer::Srgb, Transfer::Gamma22, Transfer::Gamma24, Transfer::Gamma26, Transfer::Linear, Transfer::Pq, Transfer::Hlg}) {
        CHECK(transferFromName(transferName(t), Transfer::Srgb) == t);
        CHECK(std::string(transferLabel(t)).size() >= 2);
    }
    CHECK(transferFromName("x", Transfer::Gamma24) == Transfer::Gamma24);
    for (ToneMapper m : {ToneMapper::Shoulder, ToneMapper::Filmic}) {
        CHECK(toneMapperFromName(toneMapperName(m), ToneMapper::Shoulder) == m);
    }
    CHECK(toneMapperFromName("x", ToneMapper::Filmic) == ToneMapper::Filmic);
    for (int i = 0; i < kDisplayTransferCount; ++i) {
        CHECK(displayTransferIndex(displayTransferAt(i)) == i);
        CHECK(isDisplayTransfer(displayTransferAt(i)));
    }
    CHECK(displayTransferAt(-5) == Transfer::Srgb);
    CHECK(displayTransferAt(99) == Transfer::Srgb);
    CHECK(!isDisplayTransfer(Transfer::Pq));
    CHECK(!isDisplayTransfer(Transfer::Hlg));
    CHECK(!isDisplayTransfer(Transfer::Linear));
}

void testMatrices() {
    const Matrix3 m709 = rgbToXyz(Primaries::Rec709);
    const Matrix3 expected709{0.4124, 0.3576, 0.1805, 0.2126, 0.7152, 0.0722, 0.0193, 0.1192, 0.9505};
    for (size_t i = 0; i < 9; ++i) {
        CHECK(near(m709[i], expected709[i], 6e-4));
    }
    const Color3 w709 = applyMatrix(m709, {1.0, 1.0, 1.0});
    CHECK(near(w709[0], 0.3127 / 0.3290, 1e-9));
    CHECK(near(w709[1], 1.0, 1e-9));
    CHECK(near(w709[2], (1.0 - 0.3127 - 0.3290) / 0.3290, 1e-9));
    for (Primaries p : {Primaries::Rec709, Primaries::DisplayP3, Primaries::Rec2020}) {
        const Color3 white = applyMatrix(rgbToXyz(p), {1.0, 1.0, 1.0});
        CHECK(near(white[0], w709[0], 1e-9) && near(white[1], w709[1], 1e-9) && near(white[2], w709[2], 1e-9));
        const Color3 back = applyMatrix(xyzToRgb(p), white);
        CHECK(nearColor(back, {1.0, 1.0, 1.0}, 1e-9));
    }
    const Matrix3 to2020 = primariesMatrix(Primaries::Rec709, Primaries::Rec2020);
    const Matrix3 expected2020{0.6274, 0.3293, 0.0433, 0.0691, 0.9195, 0.0114, 0.0164, 0.0880, 0.8956};
    for (size_t i = 0; i < 9; ++i) {
        CHECK(near(to2020[i], expected2020[i], 6e-4));
    }
    const Matrix3 toP3 = primariesMatrix(Primaries::Rec709, Primaries::DisplayP3);
    const Matrix3 expectedP3{0.8225, 0.1774, 0.0, 0.0332, 0.9669, 0.0, 0.0171, 0.0724, 0.9108};
    for (size_t i = 0; i < 9; ++i) {
        CHECK(near(toP3[i], expectedP3[i], 6e-4));
    }
    const Matrix3 identity = primariesMatrix(Primaries::DisplayP3, Primaries::DisplayP3);
    for (size_t i = 0; i < 9; ++i) {
        CHECK(identity[i] == (i % 4 == 0 ? 1.0 : 0.0));
    }
    const Color3 luma2020 = lumaCoefficients(Primaries::Rec2020);
    CHECK(near(luma2020[0], 0.2627, 5e-4) && near(luma2020[1], 0.6780, 5e-4) && near(luma2020[2], 0.0593, 5e-4));
    const Color3 luma709 = lumaCoefficients(Primaries::Rec709);
    CHECK(near(luma709[0], 0.2126, 5e-4) && near(luma709[1], 0.7152, 5e-4) && near(luma709[2], 0.0722, 5e-4));
    std::uint32_t state = 7;
    for (int i = 0; i < 500; ++i) {
        const Color3 c{unit(state), unit(state), unit(state)};
        const Color3 there = applyMatrix(primariesMatrix(Primaries::Rec709, Primaries::Rec2020), c);
        const Color3 back = applyMatrix(primariesMatrix(Primaries::Rec2020, Primaries::Rec709), there);
        CHECK(nearColor(back, c, 1e-9));
        const Color3 viaP3 = applyMatrix(primariesMatrix(Primaries::DisplayP3, Primaries::Rec709), applyMatrix(primariesMatrix(Primaries::Rec709, Primaries::DisplayP3), c));
        CHECK(nearColor(viaP3, c, 1e-9));
    }
    const Color3 red709 = applyMatrix(primariesMatrix(Primaries::Rec709, Primaries::DisplayP3), {1.0, 0.0, 0.0});
    CHECK(red709[0] < 1.0 && red709[1] > 0.0);
    const Color3 red709in2020 = applyMatrix(primariesMatrix(Primaries::Rec709, Primaries::Rec2020), {1.0, 0.0, 0.0});
    CHECK(red709in2020[0] < 1.0 && red709in2020[1] > 0.0 && red709in2020[2] > 0.0);
    const Color3 red2020in709 = applyMatrix(primariesMatrix(Primaries::Rec2020, Primaries::Rec709), {1.0, 0.0, 0.0});
    CHECK(red2020in709[0] > 1.0 && red2020in709[1] < 0.0);
}

void testSdrTransfers() {
    CHECK(near(sdrToLinear(Transfer::Srgb, 0.5), 0.214041, 1e-5));
    CHECK(near(sdrToLinear(Transfer::Srgb, 1.0), 1.0, 1e-12));
    CHECK(sdrToLinear(Transfer::Srgb, 0.0) == 0.0);
    CHECK(near(sdrToLinear(Transfer::Srgb, 0.04045), 0.04045 / 12.92, 1e-9));
    CHECK(near(sdrToLinear(Transfer::Gamma22, 0.5), std::pow(0.5, 2.2), 1e-12));
    CHECK(near(sdrToLinear(Transfer::Gamma24, 0.5), std::pow(0.5, 2.4), 1e-12));
    CHECK(near(sdrToLinear(Transfer::Gamma26, 0.5), std::pow(0.5, 2.6), 1e-12));
    CHECK(near(sdrToLinear(Transfer::Linear, 0.37), 0.37, 1e-15));
    CHECK(near(sdrToLinear(Transfer::Srgb, -3.0), 0.0, 0.0));
    CHECK(near(sdrToLinear(Transfer::Srgb, 7.0), 1.0, 1e-12));
    CHECK(linearToSdr(Transfer::Srgb, -1.0) == 0.0);
    CHECK(near(linearToSdr(Transfer::Srgb, 4.0), 1.0, 1e-12));
    for (Transfer t : {Transfer::Srgb, Transfer::Gamma22, Transfer::Gamma24, Transfer::Gamma26, Transfer::Linear}) {
        double previous = -1.0;
        for (int i = 0; i <= 1000; ++i) {
            const double v = i / 1000.0;
            const double linear = sdrToLinear(t, v);
            CHECK(linear >= previous);
            previous = linear;
            CHECK(near(linearToSdr(t, linear), v, 1e-9));
        }
    }
    CHECK(near(linearToSdr(Transfer::Srgb, 0.0031308), 12.92 * 0.0031308, 1e-9));
    CHECK(near(linearToSdr(Transfer::Srgb, 0.0031309), 1.055 * std::pow(0.0031309, 1.0 / 2.4) - 0.055, 1e-9));
}

void testPq() {
    CHECK(near(nitsToPq(0.0), 0.0, 1e-6));
    CHECK(pqToNits(nitsToPq(0.0)) == 0.0);
    CHECK(near(nitsToPq(10000.0), 1.0, 1e-9));
    CHECK(near(nitsToPq(100.0), 0.5081, 5e-4));
    CHECK(near(nitsToPq(203.0), 0.5807, 5e-4));
    CHECK(near(nitsToPq(1000.0), 0.7518, 5e-4));
    CHECK(near(pqToNits(0.5), 92.25, 0.1));
    CHECK(pqToNits(0.0) == 0.0);
    CHECK(near(pqToNits(1.0), 10000.0, 1e-6));
    CHECK(pqToNits(-1.0) == 0.0);
    CHECK(near(pqToNits(9.0), 10000.0, 1e-6));
    double previous = -1.0;
    for (int i = 0; i <= 2000; ++i) {
        const double e = i / 2000.0;
        const double nits = pqToNits(e);
        CHECK(nits >= previous);
        previous = nits;
        CHECK(near(nitsToPq(nits), e, i == 0 ? 1e-6 : 1e-9));
    }
    for (double nits = 0.001; nits < 10000.0; nits *= 1.37) {
        const double back = pqToNits(nitsToPq(nits));
        CHECK(near(back, nits, nits * 1e-6 + 1e-9));
    }
}

void testHlg() {
    CHECK(near(hlgOetf(1.0 / 12.0), 0.5, 1e-6));
    CHECK(near(hlgOetf(1.0), 1.0, 1e-6));
    CHECK(near(hlgOetf(0.0), 0.0, 1e-12));
    CHECK(near(hlgInverseOetf(0.5), 1.0 / 12.0, 1e-6));
    CHECK(near(hlgInverseOetf(1.0), 1.0, 1e-6));
    CHECK(hlgInverseOetf(-1.0) == 0.0);
    for (int i = 0; i <= 1000; ++i) {
        const double e = i / 1000.0;
        CHECK(near(hlgOetf(hlgInverseOetf(e)), e, 5e-5));
    }
    CHECK(near(hlgSystemGamma(1000.0), 1.2, 1e-12));
    CHECK(near(hlgSystemGamma(400.0), 1.2 + 0.42 * std::log10(0.4), 1e-12));
    CHECK(near(hlgSystemGamma(100.0), hlgSystemGamma(400.0), 1e-12));
    CHECK(near(hlgSystemGamma(9000.0), hlgSystemGamma(2000.0), 1e-12));
}

void testToneMappers() {
    const double white = 1000.0 / 203.0;
    for (ToneMapper m : {ToneMapper::Shoulder, ToneMapper::Filmic}) {
        CHECK(toneMap(m, 0.0, white) == 0.0);
        CHECK(toneMap(m, -4.0, white) == 0.0);
        CHECK(near(toneMap(m, white, white), 1.0, 1e-12));
        CHECK(toneMap(m, white * 10.0, white) == 1.0);
        double previous = -1.0;
        for (int i = 0; i <= 4000; ++i) {
            const double v = i * white * 1.2 / 4000.0;
            const double out = toneMap(m, v, white);
            CHECK(out >= previous);
            CHECK(out >= 0.0 && out <= 1.0);
            previous = out;
        }
    }
    for (double v : {0.0, 0.1, 0.4, 0.79, kShoulderKnee}) {
        CHECK(toneMapShoulder(v, white) == v);
    }
    CHECK(near(toneMapShoulder(kShoulderKnee + 1e-9, white), kShoulderKnee, 1e-8));
    const double slopeBelow = (toneMapShoulder(kShoulderKnee, white) - toneMapShoulder(kShoulderKnee - 1e-4, white)) / 1e-4;
    const double slopeAbove = (toneMapShoulder(kShoulderKnee + 1e-4, white) - toneMapShoulder(kShoulderKnee, white)) / 1e-4;
    CHECK(near(slopeBelow, 1.0, 1e-6));
    CHECK(near(slopeAbove, 1.0, 0.08));
    CHECK(toneMapShoulder(1.0, white) > 0.88 && toneMapShoulder(1.0, white) < 0.93);
    CHECK(toneMapShoulder(3.0, 0.5) == 1.0);
    CHECK(toneMapShoulder(0.5, 0.5) == 0.5);
    CHECK(toneMapShoulder(0.9, 0.5) == 0.9);
    CHECK(toneMapShoulder(1.4, 0.5) == 1.0);
    CHECK(toneMapFilmic(1.0, white) < toneMapShoulder(1.0, white));
    CHECK(toneMapFilmic(0.05, white) < 0.05);
    CHECK(toneMapFilmic(0.5, 0.2) > 0.0);
}

void testGamutMapping() {
    CHECK(nearColor(mapIntoGamut({0.2, 0.5, 0.9}), {0.2, 0.5, 0.9}, 1e-15));
    CHECK(nearColor(mapIntoGamut({0.0, 0.0, 0.0}), {0.0, 0.0, 0.0}, 0.0));
    CHECK(nearColor(mapIntoGamut({-1.0, -1.0, -1.0}), {0.0, 0.0, 0.0}, 0.0));
    CHECK(nearColor(mapIntoGamut({1.0, 1.0, 1.0}), {1.0, 1.0, 1.0}, 1e-15));
    const Color3 red2020 = applyMatrix(primariesMatrix(Primaries::Rec2020, Primaries::Rec709), {0.6, 0.02, 0.02});
    CHECK(red2020[1] < 0.0);
    const Color3 mapped = mapIntoGamut(red2020);
    const Color3 w = lumaCoefficients(Primaries::Rec709);
    const double before = w[0] * red2020[0] + w[1] * red2020[1] + w[2] * red2020[2];
    const double after = w[0] * mapped[0] + w[1] * mapped[1] + w[2] * mapped[2];
    CHECK(mapped[0] >= 0.0 && mapped[1] >= 0.0 && mapped[2] >= 0.0 && mapped[0] <= 1.0 && mapped[1] <= 1.0 && mapped[2] <= 1.0);
    CHECK(near(after, std::min(before, after), 1e-12));
    CHECK(near(after, before, 1e-9));
    const Color3 over = mapIntoGamut({2.0, 1.0, 0.5});
    CHECK(near(over[0], 1.0, 1e-12) && near(over[1], 0.5, 1e-12) && near(over[2], 0.25, 1e-12));
    std::uint32_t state = 99;
    for (int i = 0; i < 20000; ++i) {
        const Color3 c{unit(state) * 4.0 - 1.5, unit(state) * 4.0 - 1.5, unit(state) * 4.0 - 1.5};
        const Color3 m = mapIntoGamut(c);
        for (double v : m) {
            CHECK(std::isfinite(v) && v >= 0.0 && v <= 1.0 + 1e-12);
        }
    }
}

void testHdrToSdr() {
    HdrToSdr pq;
    pq.source = Transfer::Pq;
    const Color3 black = hdrToSdr({0.0, 0.0, 0.0}, pq);
    CHECK(nearColor(black, {0.0, 0.0, 0.0}, 0.0));
    const double peakSignal = nitsToPq(pq.peak);
    const Color3 peak = hdrToSdr({peakSignal, peakSignal, peakSignal}, pq);
    CHECK(nearColor(peak, {1.0, 1.0, 1.0}, 1e-9));
    const double diffuse = nitsToPq(203.0);
    const Color3 grey = hdrToSdr({diffuse, diffuse, diffuse}, pq);
    CHECK(near(grey[0], grey[1], 1e-9) && near(grey[1], grey[2], 1e-9));
    CHECK(grey[0] > 0.94 && grey[0] < 0.97);
    const Color3 beyond = hdrToSdr({1.0, 1.0, 1.0}, pq);
    CHECK(nearColor(beyond, {1.0, 1.0, 1.0}, 1e-9));
    double previous = -1.0;
    for (int i = 0; i <= 1000; ++i) {
        const double e = i / 1000.0;
        const Color3 c = hdrToSdr({e, e, e}, pq);
        CHECK(c[0] >= previous - 1e-12);
        previous = c[0];
        CHECK(near(c[0], c[1], 1e-9) && near(c[1], c[2], 1e-9));
    }
    for (double e : {0.1, 0.2, 0.3, 0.4}) {
        const Color3 c = hdrToSdr({e, e, e}, pq);
        const double expected = linearToSdr(Transfer::Srgb, std::min(pqToNits(e) / 203.0, kShoulderKnee));
        CHECK(near(c[0], expected, 1e-9));
    }
    HdrToSdr hlg;
    hlg.source = Transfer::Hlg;
    const Color3 hlgWhite = hdrToSdr({1.0, 1.0, 1.0}, hlg);
    CHECK(nearColor(hlgWhite, {1.0, 1.0, 1.0}, 1e-9));
    const Color3 hlgBlack = hdrToSdr({0.0, 0.0, 0.0}, hlg);
    CHECK(nearColor(hlgBlack, {0.0, 0.0, 0.0}, 0.0));
    const Color3 relative = hdrToLinear({0.75, 0.75, 0.75}, hlg);
    CHECK(near(relative[0], relative[1], 1e-12));
    CHECK(near(relative[0], 0.905, 0.02));
    HdrToSdr filmic = pq;
    filmic.mapper = ToneMapper::Filmic;
    const Color3 f = hdrToSdr({diffuse, diffuse, diffuse}, filmic);
    CHECK(f[0] < grey[0]);
    const Color3 fPeak = hdrToSdr({peakSignal, peakSignal, peakSignal}, filmic);
    CHECK(nearColor(fPeak, {1.0, 1.0, 1.0}, 1e-9));
    HdrToSdr bright = pq;
    bright.peak = 4000.0;
    const double s4000 = nitsToPq(4000.0);
    CHECK(nearColor(hdrToSdr({s4000, s4000, s4000}, bright), {1.0, 1.0, 1.0}, 1e-9));
    const Color3 red = hdrToSdr({diffuse, 0.0, 0.0}, pq);
    CHECK(red[0] > 0.0 && red[1] >= 0.0 && red[2] >= 0.0 && red[0] <= 1.0);
    CHECK(red[0] > red[1] && red[0] > red[2]);
    std::uint32_t state = 1234;
    for (int i = 0; i < 20000; ++i) {
        const Color3 c{unit(state), unit(state), unit(state)};
        for (const HdrToSdr& p : {pq, hlg, filmic}) {
            const Color3 out = hdrToSdr(c, p);
            for (double v : out) {
                CHECK(std::isfinite(v) && v >= 0.0 && v <= 1.0);
            }
        }
    }
    HdrToSdr p3 = pq;
    p3.primaries = Primaries::DisplayP3;
    const Color3 p3Grey = hdrToSdr({diffuse, diffuse, diffuse}, p3);
    CHECK(nearColor(p3Grey, grey, 1e-9));
    const Color3 p3Red = hdrToSdr({diffuse, 0.0, 0.0}, p3);
    CHECK(p3Red[0] > 0.0 && p3Red[0] <= 1.0 && p3Red[1] >= 0.0 && p3Red[2] >= 0.0);
    CHECK(p3Red[1] + p3Red[2] <= red[1] + red[2] + 0.2);
    HdrToSdr junkPrimaries = pq;
    junkPrimaries.primaries = Primaries::Rec709;
    CHECK(sanitized(junkPrimaries).primaries == Primaries::Rec2020);
    for (int i = 0; i < 3000; ++i) {
        const Color3 c{unit(state), unit(state), unit(state)};
        for (double v : hdrToSdr(c, p3)) {
            CHECK(std::isfinite(v) && v >= 0.0 && v <= 1.0);
        }
    }
    const HdrToSdr junk = sanitized(HdrToSdr{Transfer::Srgb, -5.0, 1e9, ToneMapper::Filmic});
    CHECK(junk.source == Transfer::Pq);
    CHECK(junk.referenceWhite == 80.0);
    CHECK(junk.peak == kPqPeakNits);
    const HdrToSdr nan = sanitized(HdrToSdr{Transfer::Hlg, std::nan(""), std::nan(""), ToneMapper::Shoulder});
    CHECK(nan.referenceWhite == kDefaultReferenceWhiteNits && nan.peak == kDefaultPeakNits);
    const HdrToSdr low = sanitized(HdrToSdr{Transfer::Pq, 400.0, 400.0, ToneMapper::Shoulder});
    CHECK(low.peak >= low.referenceWhite);
    const Color3 none = hdrToSdr({0.5, 0.5, 0.5}, HdrToSdr{Transfer::Pq, 400.0, 400.0, ToneMapper::Shoulder});
    CHECK(none[0] >= 0.0 && none[0] <= 1.0);
}

void testDisplayColor() {
    DisplayColor standard;
    CHECK(standard.isDefault());
    std::uint32_t state = 5;
    for (int i = 0; i < 200; ++i) {
        const Color3 c{unit(state), unit(state), unit(state)};
        CHECK(workingToDisplay(c, standard) == c);
    }
    for (Primaries p : {Primaries::Rec709, Primaries::DisplayP3, Primaries::Rec2020}) {
        for (int t = 0; t < kDisplayTransferCount; ++t) {
            DisplayColor d{p, displayTransferAt(t)};
            const Color3 white = workingToDisplay({1.0, 1.0, 1.0}, d);
            CHECK(nearColor(white, {1.0, 1.0, 1.0}, 1e-9));
            const Color3 black = workingToDisplay({0.0, 0.0, 0.0}, d);
            CHECK(nearColor(black, {0.0, 0.0, 0.0}, 1e-12));
            for (int i = 1; i < 10; ++i) {
                const double g = i / 10.0;
                const Color3 grey = workingToDisplay({g, g, g}, d);
                CHECK(near(grey[0], grey[1], 1e-9) && near(grey[1], grey[2], 1e-9));
                const double expected = linearToSdr(d.transfer, sdrToLinear(Transfer::Srgb, g));
                CHECK(near(grey[0], expected, 1e-9));
            }
            for (int i = 0; i < 100; ++i) {
                const Color3 c{unit(state), unit(state), unit(state)};
                const Color3 out = workingToDisplay(c, d);
                for (double v : out) {
                    CHECK(v >= 0.0 && v <= 1.0);
                }
            }
        }
    }
    const DisplayColor p3{Primaries::DisplayP3, Transfer::Srgb};
    const Color3 red = workingToDisplay({1.0, 0.0, 0.0}, p3);
    const Matrix3 toP3 = primariesMatrix(Primaries::Rec709, Primaries::DisplayP3);
    CHECK(near(red[0], linearToSdr(Transfer::Srgb, toP3[0]), 1e-9));
    CHECK(near(red[1], linearToSdr(Transfer::Srgb, toP3[3]), 1e-9));
    CHECK(near(red[2], linearToSdr(Transfer::Srgb, toP3[6]), 1e-9));
    CHECK(red[0] < 1.0);
    const DisplayColor g24{Primaries::Rec709, Transfer::Gamma24};
    const Color3 mid = workingToDisplay({0.5, 0.5, 0.5}, g24);
    CHECK(near(mid[0], std::pow(sdrToLinear(Transfer::Srgb, 0.5), 1.0 / 2.4), 1e-12));
    CHECK(mid[0] > 0.5);
    const DisplayColor weird = sanitized(DisplayColor{Primaries::Rec2020, Transfer::Pq});
    CHECK(weird.transfer == Transfer::Srgb && weird.gamut == Primaries::Rec2020);
    CHECK(!weird.isDefault());
    const Matrix3 identity = displayMatrix(standard);
    CHECK(identity[0] == 1.0 && identity[4] == 1.0 && identity[8] == 1.0 && identity[1] == 0.0);
}

std::string identityText(int n, const std::string& extra = "") {
    std::ostringstream out;
    out << "# made by the test\nTITLE \"test lut\"\n" << extra << "LUT_3D_SIZE " << n << "\n";
    for (int b = 0; b < n; ++b) {
        for (int g = 0; g < n; ++g) {
            for (int r = 0; r < n; ++r) {
                out << static_cast<double>(r) / (n - 1) << " " << static_cast<double>(g) / (n - 1) << " " << static_cast<double>(b) / (n - 1) << "\n";
            }
        }
    }
    return out.str();
}

void testCubeParsing() {
    const CubeParse good = parseCube(identityText(2));
    CHECK(good.ok);
    CHECK(good.lut.size == 2);
    CHECK(good.lut.title == "test lut");
    CHECK(good.lut.rgb.size() == 24);
    CHECK(good.lut.valid());
    CHECK(isIdentityLut(good.lut, 1e-6));
    for (int n : {3, 17, 33, 65}) {
        const CubeParse p = parseCube(identityText(n));
        CHECK(p.ok && p.lut.size == n);
        CHECK(maxIdentityError(p.lut) < 1e-6);
    }

    std::string crlf = identityText(2);
    std::string withCr;
    for (char c : crlf) {
        if (c == '\n') {
            withCr += "\r\n";
        } else {
            withCr += c;
        }
    }
    CHECK(parseCube(withCr).ok);
    CHECK(parseCube(std::string("\xEF\xBB\xBF") + identityText(2)).ok);
    CHECK(parseCube("lut_3d_size 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n").ok);

    const CubeParse formats = parseCube("LUT_3D_SIZE 2\n+0.0 0.0 0.0\n1e0 0.0 0.0\n0.0 1.0e0 0.0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    CHECK(formats.ok);
    CHECK(near(formats.lut.rgb[3], 1.0, 1e-9));

    const CubeParse domain = parseCube("DOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\nLUT_3D_SIZE 2\n0 0 0\n2 0 0\n0 2 0\n2 2 0\n0 0 2\n2 0 2\n0 2 2\n2 2 2\n");
    CHECK(domain.ok);
    CHECK(domain.lut.domainMax[0] == 2.0f);
    CHECK(isIdentityLut(domain.lut, 1e-6));
    const Color3 half = sampleCube(domain.lut, {1.0, 1.0, 1.0});
    CHECK(nearColor(half, {1.0, 1.0, 1.0}, 1e-9));
    const CubeParse range = parseCube("LUT_3D_INPUT_RANGE -1 3\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    CHECK(range.ok && range.lut.domainMin[1] == -1.0f && range.lut.domainMax[2] == 3.0f);

    CHECK(!parseCube("").ok);
    CHECK(!parseCube("# only a comment\n").ok);
    CHECK(parseCube("").error.find("LUT_3D_SIZE") != std::string::npos);
    CHECK(!parseCube("LUT_1D_SIZE 4\n0 0 0\n").ok);
    CHECK(parseCube("LUT_1D_SIZE 4\n0 0 0\n").error.find("1D") != std::string::npos);
    CHECK(!parseCube("LUT_3D_SIZE 1\n0 0 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 66\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE -3\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE abc\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2 3\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\nLUT_3D_SIZE 2\n").ok);
    CHECK(!parseCube("0 0 0\nLUT_3D_SIZE 2\n").ok);
    CHECK(parseCube("0 0 0\nLUT_3D_SIZE 2\n").line == 1);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n").ok);
    std::string tooMany = identityText(2) + "0 0 0\n";
    CHECK(!parseCube(tooMany).ok);
    CHECK(parseCube(tooMany).error.find("more table lines") != std::string::npos);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 0 x\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 0 0 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\nnan nan nan\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 inf 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 1e999 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 17 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\n0 -17 0\n").ok);
    CHECK(!parseCube("LUT_3D_SIZE 2\nBANANA 3\n").ok);
    CHECK(!parseCube("DOMAIN_MIN 0 0\nLUT_3D_SIZE 2\n").ok);
    CHECK(!parseCube("DOMAIN_MAX a b c\nLUT_3D_SIZE 2\n").ok);
    CHECK(!parseCube("LUT_3D_INPUT_RANGE 1\nLUT_3D_SIZE 2\n").ok);
    const std::string reversed = "DOMAIN_MIN 1 1 1\nDOMAIN_MAX 0 0 0\n" + identityText(2);
    CHECK(!parseCube(reversed).ok);
    const std::string equalDomain = "DOMAIN_MIN 0 0 0\nDOMAIN_MAX 0 1 1\n" + identityText(2);
    CHECK(!parseCube(equalDomain).ok);
    const CubeParse longTitle = parseCube("TITLE \"" + std::string(500, 'x') + "\"\n" + identityText(2));
    CHECK(longTitle.ok && longTitle.lut.title.size() <= kMaxCubeNameBytes);
    const CubeParse bare = parseCube("TITLE no quotes here\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    CHECK(bare.ok && bare.lut.title == "no quotes here");
    CHECK(!parseCube(std::string(200, '\0')).ok);
    CHECK(!parseCube(std::string("LUT_3D_SIZE 2\n") + std::string(1000, '9')).ok);

    CubeLut broken = makeIdentityLut(4);
    CHECK(broken.valid());
    broken.rgb.pop_back();
    CHECK(!broken.valid());
    broken = makeIdentityLut(4);
    broken.rgb[5] = std::nanf("");
    CHECK(!broken.valid());
    broken = makeIdentityLut(4);
    broken.domainMax[1] = broken.domainMin[1];
    CHECK(!broken.valid());
    broken = makeIdentityLut(4);
    broken.size = 1;
    CHECK(!broken.valid());
    CHECK(!CubeLut{}.valid());
    CHECK(makeIdentityLut(1).size == kMinCubeSize);
    CHECK(makeIdentityLut(500).size == kMaxCubeSize);
    CHECK(maxIdentityError(CubeLut{}) > 1.0);
    CHECK(!isIdentityLut(CubeLut{}, 1.0));
}

void testCubeSampling() {
    for (int n : {2, 5, 17, 33}) {
        const CubeLut identity = makeIdentityLut(n);
        std::uint32_t state = static_cast<std::uint32_t>(n);
        for (int i = 0; i < 400; ++i) {
            const Color3 c{unit(state), unit(state), unit(state)};
            CHECK(nearColor(sampleCube(identity, c), c, 1e-6));
        }
        CHECK(nearColor(sampleCube(identity, {0.0, 0.0, 0.0}), {0.0, 0.0, 0.0}, 0.0));
        CHECK(nearColor(sampleCube(identity, {1.0, 1.0, 1.0}), {1.0, 1.0, 1.0}, 1e-6));
        CHECK(nearColor(sampleCube(identity, {-3.0, 9.0, 0.5}), {0.0, 1.0, 0.5}, 1e-6));
    }

    CubeLut invert = makeIdentityLut(9);
    for (float& v : invert.rgb) {
        v = 1.0f - v;
    }
    CHECK(!isIdentityLut(invert, 0.01));
    CHECK(maxIdentityError(invert) > 0.9);
    std::uint32_t state = 21;
    for (int i = 0; i < 400; ++i) {
        const Color3 c{unit(state), unit(state), unit(state)};
        CHECK(nearColor(sampleCube(invert, c), {1.0 - c[0], 1.0 - c[1], 1.0 - c[2]}, 1e-6));
    }

    CubeLut swap = makeIdentityLut(7);
    for (size_t i = 0; i + 2 < swap.rgb.size(); i += 3) {
        std::swap(swap.rgb[i], swap.rgb[i + 2]);
    }
    for (int i = 0; i < 200; ++i) {
        const Color3 c{unit(state), unit(state), unit(state)};
        CHECK(nearColor(sampleCube(swap, c), {c[2], c[1], c[0]}, 1e-6));
    }

    CubeLut gamma = makeIdentityLut(33);
    for (float& v : gamma.rgb) {
        v = static_cast<float>(std::pow(v, 2.0));
    }
    const Color3 mid = sampleCube(gamma, {0.5, 0.5, 0.5});
    CHECK(near(mid[0], 0.25, 2e-3));
    const double node = 16.0 / 32.0;
    CHECK(near(sampleCube(gamma, {node, node, node})[0], node * node, 1e-6));

    CubeLut corner = makeIdentityLut(2);
    corner.rgb[0] = 0.5f;
    CHECK(near(sampleCube(corner, {0.0, 0.0, 0.0})[0], 0.5, 1e-9));
    CHECK(near(sampleCube(corner, {0.5, 0.0, 0.0})[0], 0.75, 1e-9));

    CubeLut domain = makeIdentityLut(2);
    domain.domainMin = {-1.0f, -1.0f, -1.0f};
    domain.domainMax = {3.0f, 3.0f, 3.0f};
    CHECK(near(sampleCube(domain, {1.0, 1.0, 1.0})[0], 0.5, 1e-9));
    CHECK(near(sampleCube(domain, {-1.0, 3.0, 1.0})[1], 1.0, 1e-9));
    CHECK(near(sampleCube(domain, {-5.0, 3.0, 1.0})[0], 0.0, 1e-9));

    CHECK(nearColor(sampleCube(CubeLut{}, {0.3, 0.4, 0.5}), {0.3, 0.4, 0.5}, 0.0));
}

void testCubeSizes() {
    CHECK(cubeGpuBytes(33) == 33ull * 33ull * 33ull * 6ull);
    CHECK(cubeGpuBytes(65) == 65ull * 65ull * 65ull * 6ull);
    CHECK(cubeGpuBytes(0) == 0);
    CHECK(cubeGpuBytes(100000) == cubeGpuBytes(kMaxCubeSize));
    CHECK(colorStageBytes(1920, 1080, 33) == 1920ull * 1080ull * 4ull + cubeGpuBytes(33));
    CHECK(colorStageBytes(0, 0, 0) == 0);
    CHECK(colorStageBytes(-5, -5, 33) == cubeGpuBytes(33));
    CHECK(colorStageBytes(7680, 4320, 65) == 7680ull * 4320ull * 4ull + cubeGpuBytes(65));
}

void testNamesAndFiles() {
    CHECK(safeCubeName("a.cube"));
    CHECK(safeCubeName("Warm Look 33.CUBE"));
    CHECK(safeCubeName("x.Cube"));
    CHECK(!safeCubeName(""));
    CHECK(!safeCubeName(".cube"));
    CHECK(!safeCubeName("a.txt"));
    CHECK(!safeCubeName("a.cube.txt"));
    CHECK(!safeCubeName("sub/a.cube"));
    CHECK(!safeCubeName("sub\\a.cube"));
    CHECK(!safeCubeName("../a.cube"));
    CHECK(!safeCubeName("..a.cube"));
    CHECK(!safeCubeName("C:a.cube"));
    CHECK(!safeCubeName(std::string("a\x01.cube")));
    CHECK(!safeCubeName(std::string(kMaxCubeNameBytes, 'a') + ".cube"));
    CHECK(safeCubeName(std::string(kMaxCubeNameBytes - 5, 'a') + ".cube"));

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("nvj_color_test_" + std::to_string(static_cast<unsigned long long>(std::rand())));
    std::filesystem::create_directories(dir);
    const auto write = [&](const std::string& name, const std::string& text) {
        std::ofstream(dir / name, std::ios::binary) << text;
    };
    write("b look.cube", identityText(3));
    write("a look.cube", identityText(2));
    write("readme.txt", "x");
    write("empty.cube", "");
    write("bad.cube", "LUT_3D_SIZE 2\n0 0\n");
    std::filesystem::create_directories(dir / "folder.cube");
    const std::vector<std::string> names = listCubeFiles(dir);
    CHECK(names.size() == 4);
    CHECK(std::is_sorted(names.begin(), names.end()));
    CHECK(std::find(names.begin(), names.end(), "readme.txt") == names.end());
    CHECK(std::find(names.begin(), names.end(), "folder.cube") == names.end());
    CHECK(loadCubeFile(dir / "a look.cube").ok);
    CHECK(loadCubeFile(dir / "b look.cube").lut.size == 3);
    CHECK(!loadCubeFile(dir / "empty.cube").ok);
    CHECK(!loadCubeFile(dir / "bad.cube").ok);
    CHECK(!loadCubeFile(dir / "missing.cube").ok);
    CHECK(!loadCubeFile(dir / "missing.cube").error.empty());
    CHECK(!loadCubeFile(dir / "folder.cube").ok);
    {
        std::ofstream big(dir / "big.cube", std::ios::binary);
        big << "LUT_3D_SIZE 2\n";
    }
    std::error_code resizeError;
    std::filesystem::resize_file(dir / "big.cube", kMaxCubeFileBytes + 1, resizeError);
    if (!resizeError) {
        const CubeParse tooBig = loadCubeFile(dir / "big.cube");
        CHECK(!tooBig.ok);
        CHECK(tooBig.error.find("64 MB") != std::string::npos);
    }
    CHECK(listCubeFiles(dir / "nothing here").empty());
    std::filesystem::remove_all(dir);
}

void setDataDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.string().c_str(), 1);
#endif
}

void testOutputJson() {
    OutputSpec plain = defaultOutputSpec(0);
    const nlohmann::json plainDoc = outputToJson(plain);
    CHECK(!plainDoc.contains("display"));

    OutputSpec spec = defaultOutputSpec(0);
    spec.display.gamut = Primaries::DisplayP3;
    spec.display.transfer = Transfer::Gamma24;
    const nlohmann::json doc = outputToJson(spec);
    CHECK(doc.contains("display"));
    CHECK(doc["display"]["gamut"] == "p3");
    CHECK(doc["display"]["transfer"] == "gamma24");
    OutputSpec back = defaultOutputSpec(0);
    CHECK(outputFromJson(doc, back));
    CHECK(back.display == spec.display);
    CHECK(outputToJson(back) == doc);

    nlohmann::json broken = doc;
    broken["display"]["gamut"] = "nonsense";
    broken["display"]["transfer"] = 7;
    OutputSpec repaired = defaultOutputSpec(0);
    CHECK(outputFromJson(broken, repaired));
    CHECK(repaired.display.gamut == Primaries::Rec709);
    CHECK(repaired.display.transfer == Transfer::Srgb);
    CHECK(!outputToJson(repaired).contains("display"));

    nlohmann::json linearName = doc;
    linearName["display"]["transfer"] = "linear";
    OutputSpec refused = defaultOutputSpec(0);
    CHECK(outputFromJson(linearName, refused));
    CHECK(isDisplayTransfer(refused.display.transfer));

    nlohmann::json wrongType = doc;
    wrongType["display"] = "p3";
    OutputSpec ignored = spec;
    CHECK(outputFromJson(wrongType, ignored));
    CHECK(ignored.display.isDefault());

    OutputSet set = defaultOutputs();
    CHECK(venueToJson("plain", set).dump().find("\"display\"") == std::string::npos);
    set[1].display = {Primaries::Rec2020, Transfer::Gamma26};
    const nlohmann::json venue = venueToJson("coloured", set);
    OutputSet loaded = defaultOutputs();
    std::string name;
    std::string error;
    CHECK(venueFromJson(venue, name, loaded, error));
    CHECK(loaded[1].display == set[1].display);
    CHECK(loaded[0].display.isDefault());
}

void testSettingsJson() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "nyrexvj_color_settings";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    setDataDirectory(dir);

    Settings defaults;
    CHECK(saveSettings(defaults));
    std::ifstream plainFile(dir / "settings.json", std::ios::binary);
    std::stringstream plainText;
    plainText << plainFile.rdbuf();
    const nlohmann::json plain = nlohmann::json::parse(plainText.str());
    for (const char* key : {"colorStage", "colorLut", "colorLutStrength", "hdrToSdr", "hdrWhiteNits", "hdrPeakNits", "hdrTone"}) {
        CHECK(!plain.contains(key));
    }

    Settings changed;
    changed.colorStage = true;
    changed.colorLut = "warm.cube";
    changed.colorLutStrength = 0.4f;
    changed.hdrToSdr = true;
    changed.hdrWhiteNits = 150;
    changed.hdrPeakNits = 1500;
    changed.hdrTone = "filmic";
    CHECK(saveSettings(changed));
    Settings loaded;
    CHECK(loadSettings(loaded));
    CHECK(loaded.colorStage && loaded.colorLut == "warm.cube");
    CHECK(near(loaded.colorLutStrength, 0.4, 1e-6));
    CHECK(loaded.hdrToSdr && loaded.hdrWhiteNits == 150 && loaded.hdrPeakNits == 1500 && loaded.hdrTone == "filmic");

    {
        std::ofstream out(dir / "settings.json", std::ios::binary | std::ios::trunc);
        out << R"({"colorStage":"yes","colorLut":"../escape.cube","colorLutStrength":9,"hdrToSdr":1,"hdrWhiteNits":5,"hdrPeakNits":99999,"hdrTone":"magic"})";
    }
    Settings repaired;
    CHECK(loadSettings(repaired));
    CHECK(!repaired.colorStage);
    CHECK(repaired.colorLut.empty());
    CHECK(near(repaired.colorLutStrength, 1.0, 1e-6));
    CHECK(!repaired.hdrToSdr);
    CHECK(repaired.hdrWhiteNits == 80);
    CHECK(repaired.hdrPeakNits == 10000);
    CHECK(repaired.hdrTone == "shoulder");

#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", "");
#else
    unsetenv("NYREXVJ_DATA_DIR");
#endif
    std::filesystem::remove_all(dir, ec);
}

}

int main() {
    testNames();
    testMatrices();
    testSdrTransfers();
    testPq();
    testHlg();
    testToneMappers();
    testGamutMapping();
    testHdrToSdr();
    testDisplayColor();
    testCubeParsing();
    testCubeSampling();
    testCubeSizes();
    testNamesAndFiles();
    testOutputJson();
    testSettingsJson();
    return testing::finish();
}

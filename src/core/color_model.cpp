#include "core/color_model.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

struct Chromaticity {
    double x;
    double y;
};

struct PrimariesDefinition {
    Chromaticity red;
    Chromaticity green;
    Chromaticity blue;
    Chromaticity white;
};

constexpr Chromaticity kD65{0.3127, 0.3290};

PrimariesDefinition definitionOf(Primaries primaries) {
    switch (primaries) {
    case Primaries::DisplayP3:
        return {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, kD65};
    case Primaries::Rec2020:
        return {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, kD65};
    default:
        return {{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}, kD65};
    }
}

Matrix3 multiply(const Matrix3& a, const Matrix3& b) {
    Matrix3 r{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a[static_cast<size_t>(row * 3 + k)] * b[static_cast<size_t>(k * 3 + column)];
            }
            r[static_cast<size_t>(row * 3 + column)] = sum;
        }
    }
    return r;
}

Matrix3 invert(const Matrix3& m) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double s = std::fabs(det) > 1e-300 ? 1.0 / det : 0.0;
    return {(e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s,
            (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s,
            (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s};
}

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

double hableCurve(double x) {
    constexpr double A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892;
constexpr double kHlgC = 0.55991073;

}

const char* primariesName(Primaries primaries) {
    switch (primaries) {
    case Primaries::DisplayP3:
        return "p3";
    case Primaries::Rec2020:
        return "rec2020";
    default:
        return "rec709";
    }
}

const char* primariesLabel(Primaries primaries) {
    switch (primaries) {
    case Primaries::DisplayP3:
        return "Display P3";
    case Primaries::Rec2020:
        return "Rec. 2020";
    default:
        return "Rec. 709";
    }
}

Primaries primariesFromName(const std::string& name, Primaries fallback) {
    if (name == "rec709") {
        return Primaries::Rec709;
    }
    if (name == "p3") {
        return Primaries::DisplayP3;
    }
    if (name == "rec2020") {
        return Primaries::Rec2020;
    }
    return fallback;
}

const char* transferName(Transfer transfer) {
    switch (transfer) {
    case Transfer::Gamma22:
        return "gamma22";
    case Transfer::Gamma24:
        return "gamma24";
    case Transfer::Gamma26:
        return "gamma26";
    case Transfer::Linear:
        return "linear";
    case Transfer::Pq:
        return "pq";
    case Transfer::Hlg:
        return "hlg";
    default:
        return "srgb";
    }
}

const char* transferLabel(Transfer transfer) {
    switch (transfer) {
    case Transfer::Gamma22:
        return "Gamma 2.2";
    case Transfer::Gamma24:
        return "Gamma 2.4";
    case Transfer::Gamma26:
        return "Gamma 2.6";
    case Transfer::Linear:
        return "Linear";
    case Transfer::Pq:
        return "PQ";
    case Transfer::Hlg:
        return "HLG";
    default:
        return "sRGB";
    }
}

Transfer transferFromName(const std::string& name, Transfer fallback) {
    for (Transfer t : {Transfer::Srgb, Transfer::Gamma22, Transfer::Gamma24, Transfer::Gamma26, Transfer::Linear, Transfer::Pq, Transfer::Hlg}) {
        if (name == transferName(t)) {
            return t;
        }
    }
    return fallback;
}

bool isDisplayTransfer(Transfer transfer) {
    return transfer == Transfer::Srgb || transfer == Transfer::Gamma22 || transfer == Transfer::Gamma24 || transfer == Transfer::Gamma26;
}

Transfer displayTransferAt(int index) {
    switch (index) {
    case 1:
        return Transfer::Gamma22;
    case 2:
        return Transfer::Gamma24;
    case 3:
        return Transfer::Gamma26;
    default:
        return Transfer::Srgb;
    }
}

int displayTransferIndex(Transfer transfer) {
    switch (transfer) {
    case Transfer::Gamma22:
        return 1;
    case Transfer::Gamma24:
        return 2;
    case Transfer::Gamma26:
        return 3;
    default:
        return 0;
    }
}

const char* toneMapperName(ToneMapper mapper) {
    return mapper == ToneMapper::Filmic ? "filmic" : "shoulder";
}

const char* toneMapperLabel(ToneMapper mapper) {
    return mapper == ToneMapper::Filmic ? "Filmic" : "Soft shoulder";
}

ToneMapper toneMapperFromName(const std::string& name, ToneMapper fallback) {
    if (name == "shoulder") {
        return ToneMapper::Shoulder;
    }
    if (name == "filmic") {
        return ToneMapper::Filmic;
    }
    return fallback;
}

Matrix3 rgbToXyz(Primaries primaries) {
    const PrimariesDefinition d = definitionOf(primaries);
    const auto column = [](const Chromaticity& c) { return Color3{c.x / c.y, 1.0, (1.0 - c.x - c.y) / c.y}; };
    const Color3 r = column(d.red);
    const Color3 g = column(d.green);
    const Color3 b = column(d.blue);
    const Matrix3 p{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const Color3 white = column(d.white);
    const Color3 s = applyMatrix(invert(p), white);
    return {r[0] * s[0], g[0] * s[1], b[0] * s[2], r[1] * s[0], g[1] * s[1], b[1] * s[2], r[2] * s[0], g[2] * s[1], b[2] * s[2]};
}

Matrix3 xyzToRgb(Primaries primaries) {
    return invert(rgbToXyz(primaries));
}

Matrix3 primariesMatrix(Primaries from, Primaries to) {
    if (from == to) {
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    return multiply(xyzToRgb(to), rgbToXyz(from));
}

Color3 lumaCoefficients(Primaries primaries) {
    const Matrix3 m = rgbToXyz(primaries);
    return {m[3], m[4], m[5]};
}

Color3 applyMatrix(const Matrix3& m, const Color3& c) {
    return {m[0] * c[0] + m[1] * c[1] + m[2] * c[2], m[3] * c[0] + m[4] * c[1] + m[5] * c[2], m[6] * c[0] + m[7] * c[1] + m[8] * c[2]};
}

double sdrToLinear(Transfer transfer, double encoded) {
    const double v = clamp01(encoded);
    switch (transfer) {
    case Transfer::Srgb:
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    case Transfer::Gamma22:
        return std::pow(v, 2.2);
    case Transfer::Gamma24:
        return std::pow(v, 2.4);
    case Transfer::Gamma26:
        return std::pow(v, 2.6);
    default:
        return v;
    }
}

double linearToSdr(Transfer transfer, double linear) {
    if (linear <= 0.0) {
        return 0.0;
    }
    const double l = std::min(linear, 1.0);
    switch (transfer) {
    case Transfer::Srgb:
        return l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
    case Transfer::Gamma22:
        return std::pow(l, 1.0 / 2.2);
    case Transfer::Gamma24:
        return std::pow(l, 1.0 / 2.4);
    case Transfer::Gamma26:
        return std::pow(l, 1.0 / 2.6);
    default:
        return l;
    }
}

double pqToNits(double signal) {
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 4096.0 * 32.0;
    constexpr double c3 = 2392.0 / 4096.0 * 32.0;
    const double e = clamp01(signal);
    if (e <= 0.0) {
        return 0.0;
    }
    const double p = std::pow(e, 1.0 / m2);
    const double numerator = std::max(p - c1, 0.0);
    const double denominator = c2 - c3 * p;
    return kPqPeakNits * std::pow(numerator / denominator, 1.0 / m1);
}

double nitsToPq(double nits) {
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 4096.0 * 32.0;
    constexpr double c3 = 2392.0 / 4096.0 * 32.0;
    const double y = std::clamp(nits / kPqPeakNits, 0.0, 1.0);
    const double p = std::pow(y, m1);
    return std::pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

double hlgOetf(double sceneLinear) {
    const double e = clamp01(sceneLinear);
    return e <= 1.0 / 12.0 ? std::sqrt(3.0 * e) : kHlgA * std::log(12.0 * e - kHlgB) + kHlgC;
}

double hlgInverseOetf(double signal) {
    const double e = clamp01(signal);
    return e <= 0.5 ? e * e / 3.0 : (std::exp((e - kHlgC) / kHlgA) + kHlgB) / 12.0;
}

double hlgSystemGamma(double peakNits) {
    return 1.2 + 0.42 * std::log10(std::clamp(peakNits, 400.0, 2000.0) / 1000.0);
}

HdrToSdr sanitized(HdrToSdr params) {
    if (params.source != Transfer::Hlg) {
        params.source = Transfer::Pq;
    }
    params.referenceWhite = std::isfinite(params.referenceWhite) ? std::clamp(params.referenceWhite, 80.0, 400.0) : kDefaultReferenceWhiteNits;
    params.peak = std::isfinite(params.peak) ? std::clamp(params.peak, 400.0, kPqPeakNits) : kDefaultPeakNits;
    params.peak = std::max(params.peak, params.referenceWhite);
    if (params.primaries != Primaries::DisplayP3) {
        params.primaries = Primaries::Rec2020;
    }
    return params;
}

double toneMapShoulder(double value, double whitePoint) {
    if (!(value > 0.0)) {
        return 0.0;
    }
    if (value <= kShoulderKnee) {
        return value;
    }
    if (whitePoint <= kShoulderKnee + 1e-9) {
        return std::min(value, 1.0);
    }
    const double span = 1.0 - kShoulderKnee;
    const double xw = (whitePoint - kShoulderKnee) / span;
    const double scale = (1.0 + xw) / xw;
    const double x = (value - kShoulderKnee) / span;
    return std::min(1.0, kShoulderKnee + span * scale * x / (1.0 + x));
}

double toneMapFilmic(double value, double whitePoint) {
    if (!(value > 0.0)) {
        return 0.0;
    }
    const double white = std::max(whitePoint, 1.0);
    return std::min(1.0, hableCurve(value * kFilmicExposure) / hableCurve(white * kFilmicExposure));
}

double toneMap(ToneMapper mapper, double value, double whitePoint) {
    return mapper == ToneMapper::Filmic ? toneMapFilmic(value, whitePoint) : toneMapShoulder(value, whitePoint);
}

Color3 mapIntoGamut(const Color3& linear709) {
    const Color3 w = lumaCoefficients(Primaries::Rec709);
    Color3 c = linear709;
    const double y = w[0] * c[0] + w[1] * c[1] + w[2] * c[2];
    if (!(y > 0.0)) {
        return {0.0, 0.0, 0.0};
    }
    double t = 1.0;
    for (double v : c) {
        if (v < 0.0) {
            t = std::min(t, y / (y - v));
        }
    }
    if (t < 1.0) {
        for (double& v : c) {
            v = y + t * (v - y);
        }
    }
    double peak = 0.0;
    for (double& v : c) {
        v = std::max(v, 0.0);
        peak = std::max(peak, v);
    }
    if (peak > 1.0) {
        for (double& v : c) {
            v /= peak;
        }
    }
    return c;
}

Color3 hdrToLinear(const Color3& encoded, const HdrToSdr& input) {
    const HdrToSdr params = sanitized(input);
    const Color3 w = lumaCoefficients(params.primaries);
    Color3 relative{};
    if (params.source == Transfer::Hlg) {
        Color3 scene{};
        for (size_t i = 0; i < 3; ++i) {
            scene[i] = hlgInverseOetf(encoded[i]);
        }
        const double ys = w[0] * scene[0] + w[1] * scene[1] + w[2] * scene[2];
        const double gain = ys > 0.0 ? std::pow(ys, hlgSystemGamma(params.peak) - 1.0) : 0.0;
        for (size_t i = 0; i < 3; ++i) {
            relative[i] = params.peak * gain * scene[i] / params.referenceWhite;
        }
    } else {
        for (size_t i = 0; i < 3; ++i) {
            relative[i] = pqToNits(encoded[i]) / params.referenceWhite;
        }
    }
    const double luminance = w[0] * relative[0] + w[1] * relative[1] + w[2] * relative[2];
    if (!(luminance > 0.0)) {
        return {0.0, 0.0, 0.0};
    }
    const double mapped = toneMap(params.mapper, luminance, params.peak / params.referenceWhite);
    const double factor = mapped / luminance;
    return {relative[0] * factor, relative[1] * factor, relative[2] * factor};
}

Color3 hdrToSdr(const Color3& encoded, const HdrToSdr& params) {
    const HdrToSdr clean = sanitized(params);
    const Color3 linearSource = hdrToLinear(encoded, clean);
    const Color3 linear709 = mapIntoGamut(applyMatrix(primariesMatrix(clean.primaries, Primaries::Rec709), linearSource));
    return {linearToSdr(Transfer::Srgb, linear709[0]), linearToSdr(Transfer::Srgb, linear709[1]), linearToSdr(Transfer::Srgb, linear709[2])};
}

DisplayColor sanitized(DisplayColor display) {
    if (!isDisplayTransfer(display.transfer)) {
        display.transfer = Transfer::Srgb;
    }
    return display;
}

Matrix3 displayMatrix(const DisplayColor& display) {
    return primariesMatrix(Primaries::Rec709, display.gamut);
}

Color3 workingToDisplay(const Color3& encoded, const DisplayColor& input) {
    const DisplayColor display = sanitized(input);
    if (display.isDefault()) {
        return encoded;
    }
    Color3 linear{};
    for (size_t i = 0; i < 3; ++i) {
        linear[i] = sdrToLinear(Transfer::Srgb, encoded[i]);
    }
    const Color3 converted = applyMatrix(displayMatrix(display), linear);
    return {linearToSdr(display.transfer, converted[0]), linearToSdr(display.transfer, converted[1]), linearToSdr(display.transfer, converted[2])};
}

}

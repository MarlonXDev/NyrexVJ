#pragma once

#include <array>
#include <string>

namespace nvj {

using Color3 = std::array<double, 3>;
using Matrix3 = std::array<double, 9>;

enum class Primaries { Rec709, DisplayP3, Rec2020 };
enum class Transfer { Srgb, Gamma22, Gamma24, Gamma26, Linear, Pq, Hlg };
enum class ToneMapper { Shoulder, Filmic };

constexpr int kPrimariesCount = 3;
constexpr int kDisplayTransferCount = 4;
constexpr int kToneMapperCount = 2;

constexpr double kPqPeakNits = 10000.0;
constexpr double kDefaultReferenceWhiteNits = 203.0;
constexpr double kDefaultPeakNits = 1000.0;
constexpr double kShoulderKnee = 0.8;
constexpr double kFilmicExposure = 2.0;

const char* primariesName(Primaries primaries);
const char* primariesLabel(Primaries primaries);
Primaries primariesFromName(const std::string& name, Primaries fallback);

const char* transferName(Transfer transfer);
const char* transferLabel(Transfer transfer);
Transfer transferFromName(const std::string& name, Transfer fallback);
bool isDisplayTransfer(Transfer transfer);
Transfer displayTransferAt(int index);
int displayTransferIndex(Transfer transfer);

const char* toneMapperName(ToneMapper mapper);
const char* toneMapperLabel(ToneMapper mapper);
ToneMapper toneMapperFromName(const std::string& name, ToneMapper fallback);

Matrix3 rgbToXyz(Primaries primaries);
Matrix3 xyzToRgb(Primaries primaries);
Matrix3 primariesMatrix(Primaries from, Primaries to);
Color3 lumaCoefficients(Primaries primaries);
Color3 applyMatrix(const Matrix3& matrix, const Color3& color);

double sdrToLinear(Transfer transfer, double encoded);
double linearToSdr(Transfer transfer, double linear);

double pqToNits(double signal);
double nitsToPq(double nits);
double hlgOetf(double sceneLinear);
double hlgInverseOetf(double signal);
double hlgSystemGamma(double peakNits);

struct HdrToSdr {
    Transfer source = Transfer::Pq;
    double referenceWhite = kDefaultReferenceWhiteNits;
    double peak = kDefaultPeakNits;
    ToneMapper mapper = ToneMapper::Shoulder;
    Primaries primaries = Primaries::Rec2020;

    bool operator==(const HdrToSdr&) const = default;
};

HdrToSdr sanitized(HdrToSdr params);
double toneMapShoulder(double value, double whitePoint);
double toneMapFilmic(double value, double whitePoint);
double toneMap(ToneMapper mapper, double value, double whitePoint);
Color3 mapIntoGamut(const Color3& linear709);
Color3 hdrToLinear(const Color3& encoded, const HdrToSdr& params);
Color3 hdrToSdr(const Color3& encoded, const HdrToSdr& params);

struct DisplayColor {
    Primaries gamut = Primaries::Rec709;
    Transfer transfer = Transfer::Srgb;

    bool isDefault() const { return gamut == Primaries::Rec709 && transfer == Transfer::Srgb; }
    bool operator==(const DisplayColor&) const = default;
};

DisplayColor sanitized(DisplayColor display);
Matrix3 displayMatrix(const DisplayColor& display);
Color3 workingToDisplay(const Color3& encoded, const DisplayColor& display);

}

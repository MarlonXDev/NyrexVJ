#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace nvj::hapconv {

enum class Variant { Unknown, Dxt1, Dxt5, Ycg6, Yg10 };

enum class TargetFormat { Auto, Hap, HapAlpha, HapQ };

struct SourceInfo {
    bool ok = false;
    bool isDxv = false;
    std::string error;
    Variant variant = Variant::Unknown;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int frames = 0;
    double duration = 0.0;
};

struct Options {
    TargetFormat preference = TargetFormat::Auto;
    bool snappy = true;
    int threads = 2;
    bool allowAnyCodec = false;
    bool verify = true;
};

struct Result {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    Variant variant = Variant::Unknown;
    TargetFormat format = TargetFormat::Hap;
    bool alphaKept = false;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int frames = 0;
    unsigned long long bytes = 0;
    unsigned long long sourceBytes = 0;
    double seconds = 0.0;
    double decodeSeconds = 0.0;
    double encodeSeconds = 0.0;
    bool snappy = false;
};

using ProgressFn = std::function<bool(int framesDone, int framesTotal)>;

Variant variantFromTag(std::uint32_t tag);
Variant variantFromPacket(const std::uint8_t* data, std::size_t size);
const char* variantName(Variant variant);
bool variantHasAlpha(Variant variant);
bool variantIsHighQuality(Variant variant);
TargetFormat chooseTarget(Variant variant, TargetFormat preference, bool& alphaKept);
const char* targetName(TargetFormat format);
const char* encoderFormat(TargetFormat format);
TargetFormat targetFromName(const std::string& name);
std::string targetKey(TargetFormat format);

SourceInfo inspectSource(const std::string& pathUtf8, bool allowAnyCodec = false);
Result convertFile(const std::string& sourceUtf8, const std::string& outputUtf8, const Options& options, const ProgressFn& progress);
bool verifyOutput(const std::string& pathUtf8, const Result& expected, std::string& error);

}

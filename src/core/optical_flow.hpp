#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace nvj::flow {

enum class Mode { Off = 0, Interpolate = 1, MotionBlur = 2, Vectors = 3 };
constexpr int kModeCount = 4;

enum class Tier { Fast = 0, Balanced = 1, Fine = 2 };
constexpr int kTierCount = 3;

constexpr int kMinBudgetMb = 32;
constexpr int kMaxBudgetMb = 2048;
constexpr int kDefaultBudgetMb = 256;
constexpr float kCutCost = 16.0f;
constexpr float kCutConfident = 0.35f;
constexpr float kCutMatched = 0.30f;
constexpr float kCutTextured = 0.25f;
constexpr int kMaxFlowCols = 128;
constexpr int kMaxFlowRows = 128;

const char* modeName(Mode mode);
Mode modeFromName(const std::string& name, Mode fallback);
const char* tierName(Tier tier);
Tier tierFromName(const std::string& name, Tier fallback);
Tier clampTier(int value);
int clampBudgetMb(int value);

struct Settings {
    Mode mode = Mode::Off;
    Tier tier = Tier::Balanced;
    int budgetMb = kDefaultBudgetMb;
    float shutter = 0.5f;

    bool operator==(const Settings& other) const { return mode == other.mode && tier == other.tier && budgetMb == other.budgetMb && shutter == other.shutter; }
    bool operator!=(const Settings& other) const { return !(*this == other); }
};

Settings sanitised(const Settings& settings);

struct TierSpec {
    int analysisWidth;
    int block;
    int radius;
    int levels;
    int refine;
    bool bidirectional;
};

const TierSpec& tierSpec(Tier tier);
void analysisSize(int sourceWidth, int sourceHeight, Tier tier, int& width, int& height);

struct Luma {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    bool valid() const { return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height); }
};

Luma lumaFromRgba(const std::uint8_t* rgba, int width, int height, int stride, int targetWidth, int targetHeight);
Luma lumaFromPlane(const std::uint8_t* plane, int width, int height, int stride, int bytesPerSample, int bitDepth, bool msbAligned, const std::uint8_t* alpha, int alphaStride, int alphaBytes, int targetWidth, int targetHeight);
Luma lumaFromUyvy(const std::uint8_t* data, int width, int height, int stride, int targetWidth, int targetHeight);

struct Field {
    int cols = 0;
    int rows = 0;
    int block = 8;
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<float> dx;
    std::vector<float> dy;
    std::vector<float> confidence;
    float meanMagnitude = 0.0f;
    float maxMagnitude = 0.0f;
    float matchedCost = 0.0f;
    float zeroCost = 0.0f;
    float confidentFraction = 0.0f;
    float texturedFraction = 0.0f;
    float texturedCost = 0.0f;
    float texturedMatched = 0.0f;
    bool sceneCut = false;

    bool valid() const { return cols > 0 && rows > 0 && dx.size() == static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows) && dy.size() == dx.size() && confidence.size() == dx.size(); }
    std::size_t blocks() const { return dx.size(); }
};

bool estimate(const Luma& a, const Luma& b, Tier tier, Field& field, std::string* error = nullptr);

struct PairPlan {
    bool usable = false;
    bool exact = false;
    float t = 0.0f;
    std::string reason;
};

PairPlan planPair(double seconds, double timeA, double timeB, double nominalPeriod, bool reverse);

class Governor {
public:
    enum class Decision { Keep, Lower, Raise, Disable };

    explicit Governor(Tier requested = Tier::Balanced);
    void setRequested(Tier requested);
    Tier requested() const { return requested_; }
    Tier current() const { return current_; }
    bool disabled() const { return disabled_; }
    Decision record(double analysisMs, double periodSeconds);
    double p95() const;
    double average() const;
    std::size_t samples() const { return history_.size(); }
    unsigned long long lowered() const { return lowered_; }
    void reset();

private:
    Tier requested_ = Tier::Balanced;
    Tier current_ = Tier::Balanced;
    bool disabled_ = false;
    std::vector<double> history_;
    std::vector<double> recent_;
    int calm_ = 0;
    unsigned long long lowered_ = 0;
};

std::uint64_t estimateBytes(Mode mode, int width, int height);

class Budget {
public:
    void setLimit(std::uint64_t bytes);
    std::uint64_t limit() const;
    bool reserve(const void* owner, std::uint64_t bytes);
    void release(const void* owner);
    std::uint64_t used() const;
    std::size_t owners() const;
    std::uint64_t reserved(const void* owner) const;

private:
    mutable std::mutex mutex_;
    std::uint64_t limit_ = static_cast<std::uint64_t>(kDefaultBudgetMb) * 1024ull * 1024ull;
    std::map<const void*, std::uint64_t> owners_;
};

Budget& globalBudget();

}

#pragma once

#include "core/live_source.hpp"
#include "core/output_map.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvj::person {

enum class Mode { Off = 0, Difference = 1 };
constexpr int kModeCount = 2;

enum class Tier { Fast = 0, Balanced = 1, Fine = 2 };
constexpr int kTierCount = 3;

enum class Input { Camera = 0, Ndi = 1, Stream = 2 };
constexpr int kInputCount = 3;

constexpr int kMinRate = 2;
constexpr int kMaxRate = 30;
constexpr int kDefaultRate = 15;
constexpr int kMaxTracks = 8;
constexpr int kMaxPicturePixels = 256 * 256;
constexpr double kCpuShare = 0.25;
constexpr double kLostAfterSeconds = 1.0;

const char* modeName(Mode mode);
Mode modeFromName(const std::string& name, Mode fallback);
const char* tierName(Tier tier);
Tier tierFromName(const std::string& name, Tier fallback);
Tier clampTier(int value);
int clampRate(int value);
int clampPercent(int value);
const char* inputName(Input input);
bool parseInput(const std::string& text, Input& input, int& slot);
std::string inputText(Input input, int slot);

struct Settings {
    Mode mode = Mode::Off;
    Input input = Input::Camera;
    int slot = 0;
    Tier tier = Tier::Balanced;
    int rate = kDefaultRate;
    int sensitivity = 50;
    int softness = 50;
    int stability = 50;

    bool operator==(const Settings&) const = default;
};

Settings sanitised(const Settings& settings);

struct Picture {
    int width = 0;
    int height = 0;
    unsigned long long id = 0;
    double time = 0.0;
    std::vector<std::uint8_t> rgb;

    bool valid() const { return width > 0 && height > 0 && rgb.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u; }
};

int analysisWidth(Tier tier);
void analysisSize(int sourceWidth, int sourceHeight, Tier tier, int& width, int& height);
bool reduce(const LiveFrame& frame, Tier tier, Picture& out);

struct Provenance {
    std::string id;
    std::string title;
    std::string licence;
    std::string origin;
    bool needsModel = false;
    bool usesNetwork = false;
    bool deterministic = true;
};

struct Params {
    float sensitivity = 0.5f;
    float dt = 1.0f / 15.0f;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual Provenance provenance() const = 0;
    virtual bool segment(const Picture& picture, const Params& params, std::vector<std::uint8_t>& score, std::string& problem) = 0;
    virtual void learn() = 0;
    virtual void reset() = 0;
    virtual bool learning() const = 0;
    virtual bool sceneChanged() const = 0;
};

std::unique_ptr<Backend> makeBackend(Mode mode);

struct Blob {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    float cx = 0.0f;
    float cy = 0.0f;
    int area = 0;
};

std::vector<Blob> findBlobs(const MaskBitmap& mask, std::uint8_t threshold, int minArea, int maxBlobs);

enum class TrackState { Tentative = 0, Tracked = 1, Coasting = 2 };
const char* trackStateName(TrackState state);

struct Track {
    int id = 0;
    TrackState state = TrackState::Tentative;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float area = 0.0f;
    int hits = 0;
    int missed = 0;
    double age = 0.0;
    double lostFor = 0.0;
    bool merged = false;
};

class Tracker {
public:
    void update(const std::vector<Blob>& blobs, int width, int height, double dt);
    void clear();
    const std::vector<Track>& tracks() const { return tracks_; }
    int created() const { return nextId_ - 1; }

private:
    std::vector<Track> tracks_;
    int nextId_ = 1;
};

struct Signals {
    bool present = false;
    int count = 0;
    int primary = 0;
    float presence = 0.0f;
    float x = 0.5f;
    float y = 0.5f;
    float size = 0.0f;
    float motion = 0.0f;
    float countValue = 0.0f;
};

constexpr int kSignalCount = 6;
const char* signalName(int index);
float signalValue(const Signals& signals, int index);

class SignalFilter {
public:
    void update(const std::vector<Track>& tracks, double dt);
    void clear();
    const Signals& signals() const { return signals_; }

private:
    Signals signals_;
};

class Stabiliser {
public:
    void configure(int softness, int stability);
    void process(const std::vector<std::uint8_t>& score, int width, int height, double dt, MaskBitmap& out);
    void decay(double dt, MaskBitmap& out);
    void clear();

private:
    int softness_ = 50;
    int stability_ = 50;
    int width_ = 0;
    int height_ = 0;
    std::vector<float> previous_;
    std::vector<float> scratch_;
};

enum class State { Off = 0, Learning = 1, Running = 2, SourceLost = 3, Failed = 4 };
const char* stateName(State state);

struct Result {
    unsigned long long generation = 0;
    unsigned long long pictureId = 0;
    State state = State::Off;
    MaskBitmap mask;
    std::vector<Track> tracks;
    Signals signals;
    double milliseconds = 0.0;
    Tier tier = Tier::Balanced;
    int rate = kDefaultRate;
    int foregroundPercent = 0;
    std::string note;
};

class Analyzer {
public:
    explicit Analyzer(std::unique_ptr<Backend> backend);
    void configure(const Settings& settings);
    bool process(const Picture& picture, Result& out);
    void sourceLost(double dt, Result& out);
    void learn();
    void reset();
    const Provenance& provenance() const { return provenance_; }
    State state() const { return state_; }

private:
    std::unique_ptr<Backend> backend_;
    Provenance provenance_;
    Settings settings_;
    Stabiliser stabiliser_;
    Tracker tracker_;
    SignalFilter filter_;
    std::vector<std::uint8_t> score_;
    State state_ = State::Learning;
    double lastTime_ = -1.0;
    double lostFor_ = 0.0;
    unsigned long long generation_ = 0;
    int width_ = 0;
    int height_ = 0;
};

class Governor {
public:
    enum class Decision { Keep, LowerTier, LowerRate, RaiseRate, RaiseTier, Disable };

    Governor(Tier tier = Tier::Balanced, int rate = kDefaultRate);
    void request(Tier tier, int rate);
    Decision record(double milliseconds);
    Tier tier() const { return tier_; }
    int rate() const { return rate_; }
    bool disabled() const { return disabled_; }
    double budgetMs() const;
    double average() const;
    double p95() const;
    unsigned long long lowered() const { return lowered_; }
    void reset();

private:
    Tier requestedTier_ = Tier::Balanced;
    int requestedRate_ = kDefaultRate;
    Tier tier_ = Tier::Balanced;
    int rate_ = kDefaultRate;
    bool disabled_ = false;
    std::vector<double> recent_;
    std::vector<double> history_;
    int calm_ = 0;
    unsigned long long lowered_ = 0;
};

}

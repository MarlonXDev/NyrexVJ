#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj::particles {

constexpr const char* kFormat = "nyrexvj-particles";
constexpr int kVersion = 1;
constexpr const char* kExtension = ".nyxpart";
constexpr const char* kFolder = "particles";
constexpr int kHardMaxParticles = 16384;
constexpr int kMinLimit = 64;
constexpr int kDefaultLimit = 2048;
constexpr double kStep = 1.0 / 60.0;
constexpr int kMaxStepsPerCall = 8;
constexpr int kMaxSpawnPerStep = 256;
constexpr int kMaxBurst = 500;
constexpr int kGpuMinCount = 1024;
constexpr int kGpuMaxCount = 1048576;
constexpr int kGpuDefaultCount = 65536;
constexpr float kGpuMaxRate = 200000.0f;
constexpr int kGpuMaxBurst = 100000;
constexpr float kCpuMaxRate = 2000.0f;

enum class Shape { Point, Line, Ring, Box };
enum class ColorMode { Gradient, Rainbow, White };

struct Params {
    float rate = 120.0f;
    float burst = 0.0f;
    float life = 2.0f;
    float lifeJitter = 0.3f;
    float speed = 0.4f;
    float speedJitter = 0.4f;
    float direction = 90.0f;
    float spread = 40.0f;
    float gravity = 0.0f;
    float drag = 0.2f;
    float turbulence = 0.0f;
    float size = 0.02f;
    float sizeEnd = 0.5f;
    float sizeJitter = 0.3f;
    float fadeIn = 0.1f;
    float softness = 0.6f;
    Shape shape = Shape::Point;
    float originX = 0.0f;
    float originY = -0.8f;
    float extent = 0.2f;
    ColorMode color = ColorMode::Gradient;
    bool additive = true;
    float levelRate = 0.0f;
    float levelSize = 0.0f;
    float levelSpeed = 0.0f;
    float swirl = 0.0f;
    bool gpu = false;
    int gpuCount = kGpuDefaultCount;
    std::uint32_t seed = 1;
};

struct Drive {
    double clock = 0.0;
    double beat = 0.0;
    float level = 0.0f;
};

struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float age = 0.0f;
    float life = 1.0f;
    float size = 0.01f;
    float hue = 0.0f;
};

struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float size = 0.0f;
    float alpha = 0.0f;
    float t = 0.0f;
    float hue = 0.0f;
};

void clampParams(Params& params);
int clampLimit(int limit);
std::size_t gpuBytes(int limit);

class System {
public:
    explicit System(int limit = kDefaultLimit);

    void setLimit(int limit);
    void setParams(const Params& params);
    void setAspect(float aspect);
    void reset();
    void advance(const Drive& drive);
    void collect(std::vector<Vertex>& out, const Drive& drive) const;

    int alive() const { return static_cast<int>(particles_.size()); }
    int limit() const { return limit_; }
    long long dropped() const { return dropped_; }
    long long spawned() const { return spawned_; }
    const Params& params() const { return params_; }
    const std::vector<Particle>& particles() const { return particles_; }
    std::uint64_t stateHash() const;

private:
    std::uint32_t next();
    float unit();
    float signedUnit();
    void spawn(float level);
    void stepOnce(float level);

    Params params_;
    float aspect_ = 16.0f / 9.0f;
    int limit_ = kDefaultLimit;
    std::vector<Particle> particles_;
    std::uint32_t state_ = 1;
    bool started_ = false;
    double origin_ = 0.0;
    double lastClock_ = 0.0;
    long long lastTick_ = 0;
    long long lastBeat_ = 0;
    float carry_ = 0.0f;
    long long dropped_ = 0;
    long long spawned_ = 0;
};

const char* shapeName(Shape shape);
Shape shapeFromName(const std::string& name);
const char* colorModeName(ColorMode mode);
ColorMode colorModeFromName(const std::string& name);

struct Preset {
    std::string name;
    Params params;
};

const std::vector<Preset>& builtinPresets();
const std::vector<Preset>& gpuPresets();
Params cpuEquivalent(const Params& params, int limit);
bool findBuiltin(const std::string& name, Params& params);

std::string paramsToJson(const Params& params);
bool paramsFromJson(const std::string& json, Params& params, std::string& error, std::vector<std::string>* warnings = nullptr);

std::string particlesDirectory();
std::filesystem::path particleDocPath(const std::string& name);
std::vector<std::string> listParticleDocs();
bool loadParticleDoc(const std::string& name, Params& params, std::string& error);
bool saveParticleDoc(const std::string& name, const Params& params, std::string& error);
bool deleteParticleDoc(const std::string& name);

}

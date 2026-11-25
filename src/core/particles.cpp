#include "core/particles.hpp"

#include "core/generator_files.hpp"
#include "core/recovery.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <system_error>

namespace nvj::particles {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr float kPi = 3.14159265358979323846f;

float number(const json& node, const char* key, float fallback, float low, float high) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_number()) {
        return fallback;
    }
    const double value = it->get<double>();
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(static_cast<float>(value), low, high);
}

std::string stringValue(const json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

bool boolValue(const json& node, const char* key, bool fallback) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

void mix(std::uint64_t& hash, const void* data, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

}

int clampLimit(int limit) {
    return std::clamp(limit, kMinLimit, kHardMaxParticles);
}

std::size_t gpuBytes(int limit) {
    return static_cast<std::size_t>(clampLimit(limit)) * sizeof(Vertex);
}

void clampParams(Params& p) {
    const Params defaults;
    auto fix = [](float& value, float low, float high, float fallback) {
        value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    fix(p.rate, 0.0f, p.gpu ? kGpuMaxRate : kCpuMaxRate, defaults.rate);
    fix(p.burst, 0.0f, static_cast<float>(p.gpu ? kGpuMaxBurst : kMaxBurst), defaults.burst);
    fix(p.life, 0.2f, 10.0f, defaults.life);
    fix(p.lifeJitter, 0.0f, 1.0f, defaults.lifeJitter);
    fix(p.speed, 0.0f, 3.0f, defaults.speed);
    fix(p.speedJitter, 0.0f, 1.0f, defaults.speedJitter);
    fix(p.direction, -360.0f, 360.0f, defaults.direction);
    fix(p.spread, 0.0f, 360.0f, defaults.spread);
    fix(p.gravity, -3.0f, 3.0f, defaults.gravity);
    fix(p.drag, 0.0f, 5.0f, defaults.drag);
    fix(p.turbulence, 0.0f, 1.0f, defaults.turbulence);
    fix(p.size, 0.002f, 0.25f, defaults.size);
    fix(p.sizeEnd, 0.0f, 3.0f, defaults.sizeEnd);
    fix(p.sizeJitter, 0.0f, 1.0f, defaults.sizeJitter);
    fix(p.fadeIn, 0.0f, 1.0f, defaults.fadeIn);
    fix(p.softness, 0.0f, 1.0f, defaults.softness);
    fix(p.originX, -1.0f, 1.0f, defaults.originX);
    fix(p.originY, -1.0f, 1.0f, defaults.originY);
    fix(p.extent, 0.0f, 1.0f, defaults.extent);
    fix(p.levelRate, 0.0f, 4.0f, defaults.levelRate);
    fix(p.levelSize, 0.0f, 2.0f, defaults.levelSize);
    fix(p.levelSpeed, 0.0f, 2.0f, defaults.levelSpeed);
    fix(p.swirl, -4.0f, 4.0f, defaults.swirl);
    p.gpuCount = std::clamp(p.gpuCount, kGpuMinCount, kGpuMaxCount);
}

System::System(int limit) {
    limit_ = clampLimit(limit);
    particles_.reserve(static_cast<size_t>(limit_));
    reset();
}

void System::setLimit(int limit) {
    limit_ = clampLimit(limit);
    if (static_cast<int>(particles_.size()) > limit_) {
        particles_.resize(static_cast<size_t>(limit_));
    }
    particles_.reserve(static_cast<size_t>(limit_));
}

void System::setParams(const Params& params) {
    params_ = params;
    clampParams(params_);
}

void System::setAspect(float aspect) {
    aspect_ = std::clamp(aspect, 0.25f, 8.0f);
}

void System::reset() {
    particles_.clear();
    state_ = params_.seed == 0 ? 1u : params_.seed;
    started_ = false;
    carry_ = 0.0f;
    dropped_ = 0;
    spawned_ = 0;
}

std::uint32_t System::next() {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x == 0 ? 1u : x;
    return state_;
}

float System::unit() {
    return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
}

float System::signedUnit() {
    return unit() * 2.0f - 1.0f;
}

void System::spawn(float level) {
    if (static_cast<int>(particles_.size()) >= limit_) {
        ++dropped_;
        return;
    }
    Particle p;
    const float angle = (params_.direction + signedUnit() * params_.spread * 0.5f) * kPi / 180.0f;
    const float speed = params_.speed * (1.0f + params_.levelSpeed * level) * std::max(0.0f, 1.0f + signedUnit() * params_.speedJitter);
    float x = params_.originX * aspect_;
    float y = params_.originY;
    switch (params_.shape) {
        case Shape::Point:
            break;
        case Shape::Line:
            x += signedUnit() * params_.extent * aspect_;
            break;
        case Shape::Ring: {
            const float around = unit() * 2.0f * kPi;
            x += std::cos(around) * params_.extent;
            y += std::sin(around) * params_.extent;
            break;
        }
        case Shape::Box:
            x += signedUnit() * params_.extent * aspect_;
            y += signedUnit() * params_.extent;
            break;
    }
    p.x = x;
    p.y = y;
    p.vx = std::cos(angle) * speed;
    p.vy = std::sin(angle) * speed;
    p.life = std::max(0.05f, params_.life * (1.0f + signedUnit() * params_.lifeJitter));
    p.size = params_.size * std::max(0.1f, 1.0f + signedUnit() * params_.sizeJitter);
    p.hue = unit();
    particles_.push_back(p);
    ++spawned_;
}

void System::stepOnce(float level) {
    const float dt = static_cast<float>(kStep);
    carry_ += params_.rate * (1.0f + params_.levelRate * level) * dt;
    int count = static_cast<int>(carry_);
    carry_ -= static_cast<float>(count);
    count = std::min(count, kMaxSpawnPerStep);
    for (int i = 0; i < count; ++i) {
        spawn(level);
    }
    const float dragFactor = std::max(0.0f, 1.0f - params_.drag * dt);
    const float limitX = aspect_ * 1.35f;
    size_t i = 0;
    while (i < particles_.size()) {
        Particle& p = particles_[i];
        p.vy -= params_.gravity * dt;
        if (params_.swirl != 0.0f) {
            const float rx = p.x - params_.originX * aspect_;
            const float ry = p.y - params_.originY;
            p.vx -= ry * params_.swirl * dt;
            p.vy += rx * params_.swirl * dt;
        }
        if (params_.turbulence > 0.0f) {
            p.vx += signedUnit() * params_.turbulence * dt * 4.0f;
            p.vy += signedUnit() * params_.turbulence * dt * 4.0f;
        }
        p.vx *= dragFactor;
        p.vy *= dragFactor;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.age += dt;
        if (p.age >= p.life || std::fabs(p.x) > limitX || std::fabs(p.y) > 1.35f || !std::isfinite(p.x) || !std::isfinite(p.y)) {
            p = particles_.back();
            particles_.pop_back();
            continue;
        }
        ++i;
    }
}

void System::advance(const Drive& drive) {
    if (!std::isfinite(drive.clock) || !std::isfinite(drive.beat)) {
        return;
    }
    const float level = std::isfinite(drive.level) ? std::clamp(drive.level, 0.0f, 1.0f) : 0.0f;
    if (!started_ || drive.clock < lastClock_ - 1e-6 || drive.clock - lastClock_ > 2.0) {
        reset();
        started_ = true;
        origin_ = drive.clock;
        lastTick_ = 0;
        lastBeat_ = static_cast<long long>(std::floor(drive.beat));
    }
    lastClock_ = drive.clock;
    const long long tick = static_cast<long long>(std::floor((drive.clock - origin_) * 60.0 + 1e-6));
    long long steps = tick - lastTick_;
    lastTick_ = tick;
    steps = std::clamp<long long>(steps, 0, kMaxStepsPerCall);
    const long long beat = static_cast<long long>(std::floor(drive.beat));
    if (beat > lastBeat_ && params_.burst > 0.0f) {
        const int count = std::min(static_cast<int>(params_.burst), kMaxBurst);
        for (int i = 0; i < count; ++i) {
            spawn(level);
        }
    }
    lastBeat_ = beat;
    for (long long i = 0; i < steps; ++i) {
        stepOnce(level);
    }
}

void System::collect(std::vector<Vertex>& out, const Drive& drive) const {
    out.clear();
    out.reserve(particles_.size());
    const float boost = 1.0f + params_.levelSize * (std::isfinite(drive.level) ? std::clamp(drive.level, 0.0f, 1.0f) : 0.0f);
    for (const Particle& p : particles_) {
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float scale = 1.0f + (params_.sizeEnd - 1.0f) * t;
        const float fadeIn = params_.fadeIn > 0.001f ? std::min(1.0f, t / params_.fadeIn) : 1.0f;
        Vertex v;
        v.x = p.x;
        v.y = p.y;
        v.size = p.size * scale * boost;
        v.alpha = fadeIn * (1.0f - t);
        v.t = t;
        v.hue = p.hue;
        out.push_back(v);
    }
}

std::uint64_t System::stateHash() const {
    std::uint64_t hash = 1469598103934665603ull;
    for (const Particle& p : particles_) {
        mix(hash, &p, sizeof(Particle));
    }
    mix(hash, &state_, sizeof(state_));
    return hash;
}

const char* shapeName(Shape shape) {
    switch (shape) {
        case Shape::Line: return "line";
        case Shape::Ring: return "ring";
        case Shape::Box: return "box";
        case Shape::Point: break;
    }
    return "point";
}

Shape shapeFromName(const std::string& name) {
    if (name == "line") {
        return Shape::Line;
    }
    if (name == "ring") {
        return Shape::Ring;
    }
    if (name == "box") {
        return Shape::Box;
    }
    return Shape::Point;
}

const char* colorModeName(ColorMode mode) {
    switch (mode) {
        case ColorMode::Rainbow: return "rainbow";
        case ColorMode::White: return "white";
        case ColorMode::Gradient: break;
    }
    return "gradient";
}

ColorMode colorModeFromName(const std::string& name) {
    if (name == "rainbow") {
        return ColorMode::Rainbow;
    }
    if (name == "white") {
        return ColorMode::White;
    }
    return ColorMode::Gradient;
}

const std::vector<Preset>& builtinPresets() {
    static const std::vector<Preset> presets = [] {
        std::vector<Preset> list;
        {
            Params p;
            p.rate = 180.0f;
            p.life = 1.4f;
            p.speed = 1.1f;
            p.spread = 50.0f;
            p.gravity = 1.2f;
            p.drag = 0.1f;
            p.size = 0.012f;
            p.sizeEnd = 0.2f;
            p.seed = 11;
            p.burst = 60.0f;
            p.levelRate = 1.5f;
            list.push_back({"Sparks", p});
        }
        {
            Params p;
            p.rate = 90.0f;
            p.life = 6.0f;
            p.lifeJitter = 0.4f;
            p.speed = 0.15f;
            p.direction = 270.0f;
            p.spread = 30.0f;
            p.drag = 0.3f;
            p.turbulence = 0.35f;
            p.size = 0.012f;
            p.sizeEnd = 1.0f;
            p.originY = 1.1f;
            p.shape = Shape::Line;
            p.extent = 1.0f;
            p.color = ColorMode::White;
            p.softness = 0.8f;
            p.seed = 23;
            list.push_back({"Snow", p});
        }
        {
            Params p;
            p.rate = 70.0f;
            p.life = 3.5f;
            p.speed = 0.25f;
            p.spread = 70.0f;
            p.drag = 0.4f;
            p.turbulence = 0.5f;
            p.size = 0.01f;
            p.sizeEnd = 0.1f;
            p.originY = -1.0f;
            p.shape = Shape::Line;
            p.extent = 0.8f;
            p.seed = 37;
            p.levelRate = 1.0f;
            list.push_back({"Embers", p});
        }
        {
            Params p;
            p.rate = 220.0f;
            p.life = 2.6f;
            p.speed = 1.5f;
            p.spread = 24.0f;
            p.gravity = 1.4f;
            p.drag = 0.05f;
            p.size = 0.016f;
            p.sizeEnd = 0.6f;
            p.originY = -1.0f;
            p.seed = 41;
            p.levelSpeed = 0.8f;
            p.levelRate = 1.0f;
            list.push_back({"Fountain", p});
        }
        {
            Params p;
            p.rate = 0.0f;
            p.burst = 140.0f;
            p.life = 2.8f;
            p.speed = 0.9f;
            p.spread = 360.0f;
            p.gravity = 0.8f;
            p.drag = 0.6f;
            p.size = 0.02f;
            p.sizeEnd = 0.8f;
            p.originY = 0.0f;
            p.color = ColorMode::Rainbow;
            p.additive = false;
            p.softness = 0.15f;
            p.seed = 53;
            list.push_back({"Confetti", p});
        }
        {
            Params p;
            p.rate = 30.0f;
            p.life = 5.0f;
            p.speed = 0.22f;
            p.spread = 40.0f;
            p.drag = 0.1f;
            p.turbulence = 0.3f;
            p.size = 0.05f;
            p.sizeEnd = 1.4f;
            p.sizeJitter = 0.6f;
            p.originY = -1.0f;
            p.shape = Shape::Line;
            p.extent = 0.9f;
            p.softness = 0.35f;
            p.seed = 67;
            p.levelSize = 0.8f;
            list.push_back({"Bubbles", p});
        }
        for (Preset& preset : list) {
            clampParams(preset.params);
        }
        return list;
    }();
    return presets;
}

bool findBuiltin(const std::string& name, Params& params) {
    for (const Preset& preset : builtinPresets()) {
        if (preset.name == name) {
            params = preset.params;
            return true;
        }
    }
    for (const Preset& preset : gpuPresets()) {
        if (preset.name == name) {
            params = preset.params;
            return true;
        }
    }
    return false;
}

const std::vector<Preset>& gpuPresets() {
    static const std::vector<Preset> presets = [] {
        std::vector<Preset> list;
        {
            Params p;
            p.gpu = true;
            p.gpuCount = 262144;
            p.rate = 40000.0f;
            p.life = 6.0f;
            p.lifeJitter = 0.4f;
            p.speed = 0.25f;
            p.speedJitter = 0.6f;
            p.direction = 90.0f;
            p.spread = 360.0f;
            p.drag = 0.35f;
            p.turbulence = 0.3f;
            p.swirl = 0.7f;
            p.size = 0.006f;
            p.sizeEnd = 0.4f;
            p.sizeJitter = 0.5f;
            p.originY = 0.0f;
            p.shape = Shape::Ring;
            p.extent = 0.12f;
            p.softness = 0.8f;
            p.seed = 101;
            p.levelRate = 1.2f;
            p.levelSize = 0.6f;
            list.push_back({"GPU Nebula", p});
        }
        {
            Params p;
            p.gpu = true;
            p.gpuCount = 131072;
            p.rate = 1500.0f;
            p.burst = 20000.0f;
            p.life = 2.4f;
            p.lifeJitter = 0.5f;
            p.speed = 0.9f;
            p.speedJitter = 0.7f;
            p.spread = 360.0f;
            p.gravity = 0.5f;
            p.drag = 0.9f;
            p.size = 0.008f;
            p.sizeEnd = 0.2f;
            p.originY = 0.0f;
            p.color = ColorMode::Rainbow;
            p.softness = 0.5f;
            p.seed = 113;
            p.levelSpeed = 0.9f;
            p.levelSize = 0.5f;
            list.push_back({"GPU Beat Burst", p});
        }
        {
            Params p;
            p.gpu = true;
            p.gpuCount = 98304;
            p.rate = 12000.0f;
            p.life = 3.2f;
            p.lifeJitter = 0.3f;
            p.speed = 0.7f;
            p.speedJitter = 0.3f;
            p.direction = 270.0f;
            p.spread = 6.0f;
            p.gravity = 0.2f;
            p.drag = 0.05f;
            p.size = 0.004f;
            p.sizeEnd = 1.0f;
            p.originY = 1.1f;
            p.shape = Shape::Line;
            p.extent = 1.0f;
            p.color = ColorMode::White;
            p.softness = 0.7f;
            p.seed = 127;
            p.levelRate = 1.5f;
            list.push_back({"GPU Rain", p});
        }
        for (Preset& preset : list) {
            clampParams(preset.params);
        }
        return list;
    }();
    return presets;
}

Params cpuEquivalent(const Params& source, int limit) {
    Params p = source;
    p.gpu = false;
    clampParams(p);
    const float room = static_cast<float>(clampLimit(limit)) * 0.9f;
    const float demand = p.rate * p.life;
    if (demand > room && demand > 0.0f) {
        p.rate *= room / demand;
    }
    p.burst = std::min(p.burst, static_cast<float>(kMaxBurst));
    clampParams(p);
    return p;
}

std::string paramsToJson(const Params& source) {
    Params p = source;
    clampParams(p);
    json node;
    node["format"] = kFormat;
    node["version"] = kVersion;
    node["rate"] = p.rate;
    node["burst"] = p.burst;
    node["life"] = p.life;
    node["lifeJitter"] = p.lifeJitter;
    node["speed"] = p.speed;
    node["speedJitter"] = p.speedJitter;
    node["direction"] = p.direction;
    node["spread"] = p.spread;
    node["gravity"] = p.gravity;
    node["drag"] = p.drag;
    node["turbulence"] = p.turbulence;
    node["size"] = p.size;
    node["sizeEnd"] = p.sizeEnd;
    node["sizeJitter"] = p.sizeJitter;
    node["fadeIn"] = p.fadeIn;
    node["softness"] = p.softness;
    node["shape"] = shapeName(p.shape);
    node["originX"] = p.originX;
    node["originY"] = p.originY;
    node["extent"] = p.extent;
    node["color"] = colorModeName(p.color);
    node["additive"] = p.additive;
    node["levelRate"] = p.levelRate;
    node["levelSize"] = p.levelSize;
    node["levelSpeed"] = p.levelSpeed;
    node["swirl"] = p.swirl;
    node["backend"] = p.gpu ? "gpu" : "cpu";
    node["gpuCount"] = p.gpuCount;
    node["seed"] = p.seed;
    return node.dump(2);
}

bool paramsFromJson(const std::string& text, Params& params, std::string& error, std::vector<std::string>* warnings) {
    if (text.size() > gen::kMaxDocBytes) {
        error = "The file is too large";
        return false;
    }
    json node = json::parse(text, nullptr, false, false);
    if (node.is_discarded() || !node.is_object()) {
        error = "The particle file is not valid";
        return false;
    }
    auto format = node.find("format");
    if (format == node.end() || !format->is_string() || format->get<std::string>() != kFormat) {
        error = "This is not a NyrexVJ particle file";
        return false;
    }
    auto version = node.find("version");
    if (version == node.end() || !version->is_number_integer() || version->get<int>() < 1) {
        error = "The particle file has no version";
        return false;
    }
    if (version->get<int>() > kVersion) {
        error = "The particle file was made by a newer version";
        return false;
    }
    Params r;
    r.gpu = stringValue(node, "backend") == "gpu";
    r.rate = number(node, "rate", r.rate, 0.0f, r.gpu ? kGpuMaxRate : kCpuMaxRate);
    r.burst = number(node, "burst", r.burst, 0.0f, static_cast<float>(r.gpu ? kGpuMaxBurst : kMaxBurst));
    r.life = number(node, "life", r.life, 0.2f, 10.0f);
    r.lifeJitter = number(node, "lifeJitter", r.lifeJitter, 0.0f, 1.0f);
    r.speed = number(node, "speed", r.speed, 0.0f, 3.0f);
    r.speedJitter = number(node, "speedJitter", r.speedJitter, 0.0f, 1.0f);
    r.direction = number(node, "direction", r.direction, -360.0f, 360.0f);
    r.spread = number(node, "spread", r.spread, 0.0f, 360.0f);
    r.gravity = number(node, "gravity", r.gravity, -3.0f, 3.0f);
    r.drag = number(node, "drag", r.drag, 0.0f, 5.0f);
    r.turbulence = number(node, "turbulence", r.turbulence, 0.0f, 1.0f);
    r.size = number(node, "size", r.size, 0.002f, 0.25f);
    r.sizeEnd = number(node, "sizeEnd", r.sizeEnd, 0.0f, 3.0f);
    r.sizeJitter = number(node, "sizeJitter", r.sizeJitter, 0.0f, 1.0f);
    r.fadeIn = number(node, "fadeIn", r.fadeIn, 0.0f, 1.0f);
    r.softness = number(node, "softness", r.softness, 0.0f, 1.0f);
    r.shape = shapeFromName(stringValue(node, "shape"));
    r.originX = number(node, "originX", r.originX, -1.0f, 1.0f);
    r.originY = number(node, "originY", r.originY, -1.0f, 1.0f);
    r.extent = number(node, "extent", r.extent, 0.0f, 1.0f);
    r.color = colorModeFromName(stringValue(node, "color"));
    r.additive = boolValue(node, "additive", r.additive);
    r.levelRate = number(node, "levelRate", r.levelRate, 0.0f, 4.0f);
    r.levelSize = number(node, "levelSize", r.levelSize, 0.0f, 2.0f);
    r.levelSpeed = number(node, "levelSpeed", r.levelSpeed, 0.0f, 2.0f);
    r.swirl = number(node, "swirl", r.swirl, -4.0f, 4.0f);
    auto gpuCount = node.find("gpuCount");
    if (gpuCount != node.end() && gpuCount->is_number_integer()) {
        r.gpuCount = static_cast<int>(std::clamp<long long>(gpuCount->get<long long>(), kGpuMinCount, kGpuMaxCount));
    }
    auto seed = node.find("seed");
    if (seed != node.end() && seed->is_number_unsigned()) {
        r.seed = static_cast<std::uint32_t>(std::min<std::uint64_t>(seed->get<std::uint64_t>(), 0xFFFFFFFFull));
    }
    if (warnings && !r.gpu && r.rate * r.life > static_cast<float>(kDefaultLimit)) {
        warnings->push_back("This file could use more particles than the default limit and the limit will cut them off");
    }
    clampParams(r);
    params = r;
    return true;
}

std::string particlesDirectory() {
    return gen::folderPath(kFolder).string();
}

fs::path particleDocPath(const std::string& name) {
    return gen::folderPath(kFolder) / (name + kExtension);
}

std::vector<std::string> listParticleDocs() {
    return gen::listStems(gen::folderPath(kFolder), kExtension);
}

bool loadParticleDoc(const std::string& name, Params& params, std::string& error) {
    if (!gen::nameIsUsable(name)) {
        error = "The name is not valid";
        return false;
    }
    std::string text;
    if (!gen::readLimited(particleDocPath(name), text, error)) {
        return false;
    }
    return paramsFromJson(text, params, error);
}

bool saveParticleDoc(const std::string& name, const Params& params, std::string& error) {
    if (!gen::nameIsUsable(name)) {
        error = "The name is not valid";
        return false;
    }
    std::error_code ec;
    fs::create_directories(gen::folderPath(kFolder), ec);
    if (!writeFileAtomic(particleDocPath(name), paramsToJson(params))) {
        error = "The file could not be saved";
        return false;
    }
    return true;
}

bool deleteParticleDoc(const std::string& name) {
    if (!gen::nameIsUsable(name)) {
        return false;
    }
    std::error_code ec;
    return fs::remove(particleDocPath(name), ec) && !ec;
}

}

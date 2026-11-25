#include "core/gpu_particles.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nvj::gpuparticles {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDt = static_cast<float>(particles::kStep);
constexpr std::uint32_t kSerialMul = 0x85EBCA6Bu;
constexpr std::uint32_t kSeedMul = 0x9E3779B1u;
constexpr std::uint32_t kStreamMul = 0x9E3779B9u;

std::uint32_t streamHash(std::uint32_t base, std::uint32_t index) {
    return hash(base + index * kStreamMul);
}

float centered(std::uint32_t hashed) {
    return unit(hashed) * 2.0f - 1.0f;
}

}

std::uint32_t hash(std::uint32_t value) {
    const std::uint32_t state = value * 747796405u + 2891336453u;
    const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float unit(std::uint32_t hashed) {
    return static_cast<float>(hashed >> 8u) * (1.0f / 16777216.0f);
}

int clampBudgetMb(int megabytes) {
    return std::clamp(megabytes, kMinBudgetMb, kMaxBudgetMb);
}

std::size_t bytesFor(int capacity) {
    return static_cast<std::size_t>(std::max(capacity, 0)) * kBytesPerParticle;
}

int capacityFor(int requested, std::uint64_t availableBytes) {
    int wanted = std::clamp(requested, particles::kGpuMinCount, particles::kGpuMaxCount);
    const std::uint64_t fits = availableBytes / kBytesPerParticle;
    if (fits < static_cast<std::uint64_t>(wanted)) {
        wanted = static_cast<int>(fits / kCapacityGranule) * kCapacityGranule;
    }
    return wanted < particles::kGpuMinCount ? 0 : wanted;
}

void Budget::setLimit(std::uint64_t bytes) {
    limit_ = bytes;
}

bool Budget::tryReserve(std::uint64_t bytes) {
    if (bytes > available()) {
        return false;
    }
    used_ += bytes;
    return true;
}

void Budget::release(std::uint64_t bytes) {
    used_ = bytes >= used_ ? 0 : used_ - bytes;
}

void Scheduler::configure(const particles::Params& params, int capacity) {
    params_ = params;
    particles::clampParams(params_);
    capacity_ = std::clamp(capacity, kCapacityGranule, particles::kGpuMaxCount);
}

void Scheduler::reset() {
    started_ = false;
    origin_ = 0.0;
    lastClock_ = 0.0;
    lastTick_ = 0;
    lastBeat_ = 0;
    carry_ = 0.0;
    pendingBurst_ = 0;
    head_ = 0;
    serial_ = 0;
    used_ = 0;
    index_ = 0;
    spawned_ = 0;
}

int Scheduler::advance(const particles::Drive& drive, std::vector<Step>& out) {
    out.clear();
    if (!std::isfinite(drive.clock) || !std::isfinite(drive.beat)) {
        return 0;
    }
    const float level = std::isfinite(drive.level) ? std::clamp(drive.level, 0.0f, 1.0f) : 0.0f;
    if (!started_ || drive.clock < lastClock_ - 1e-6 || drive.clock - lastClock_ > 2.0) {
        reset();
        started_ = true;
        origin_ = drive.clock;
        lastBeat_ = static_cast<long long>(std::floor(drive.beat));
    }
    lastClock_ = drive.clock;
    const long long tick = static_cast<long long>(std::floor((drive.clock - origin_) * 60.0 + 1e-6));
    long long steps = std::clamp<long long>(tick - lastTick_, 0, particles::kMaxStepsPerCall);
    lastTick_ = tick;
    const long long beat = static_cast<long long>(std::floor(drive.beat));
    if (beat > lastBeat_ && params_.burst > 0.0f) {
        const std::uint64_t add = static_cast<std::uint64_t>(std::min(params_.burst, static_cast<float>(particles::kGpuMaxBurst)));
        pendingBurst_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(pendingBurst_ + add, static_cast<std::uint64_t>(capacity_)));
    }
    lastBeat_ = beat;
    for (long long i = 0; i < steps; ++i) {
        carry_ += static_cast<double>(params_.rate) * (1.0 + static_cast<double>(params_.levelRate) * static_cast<double>(level)) * particles::kStep;
        double whole = std::floor(carry_);
        carry_ -= whole;
        whole = std::min(whole, static_cast<double>(kMaxSpawnPerStep));
        std::uint64_t count = static_cast<std::uint64_t>(whole) + pendingBurst_;
        pendingBurst_ = 0;
        count = std::min<std::uint64_t>(count, static_cast<std::uint64_t>(capacity_));
        Step step;
        step.head = head_;
        step.spawn = static_cast<std::uint32_t>(count);
        step.serial = serial_;
        step.used = static_cast<std::uint32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(used_) + count, static_cast<std::uint64_t>(capacity_)));
        step.index = index_;
        step.level = level;
        out.push_back(step);
        head_ = static_cast<std::uint32_t>((static_cast<std::uint64_t>(head_) + count) % static_cast<std::uint64_t>(capacity_));
        serial_ += static_cast<std::uint32_t>(count);
        used_ = step.used;
        spawned_ += count;
        ++index_;
    }
    return static_cast<int>(out.size());
}

Reference::Reference(const particles::Params& params, int capacity, float aspect)
    : params_(params), capacity_(std::clamp(capacity, kCapacityGranule, particles::kGpuMaxCount)), aspect_(std::clamp(aspect, 0.25f, 8.0f)), states_(static_cast<size_t>(std::clamp(capacity, kCapacityGranule, particles::kGpuMaxCount))) {
    particles::clampParams(params_);
}

void Reference::apply(const Step& step) {
    const particles::Params& p = params_;
    const float dragFactor = std::max(0.0f, 1.0f - p.drag * kDt);
    const float limitX = aspect_ * 1.35f;
    const std::uint32_t cap = static_cast<std::uint32_t>(capacity_);
    const std::uint32_t used = std::min<std::uint32_t>(step.used, cap);
    for (std::uint32_t i = 0; i < used; ++i) {
        State& s = states_[i];
        const std::uint32_t rel = (i + cap - step.head) % cap;
        if (rel < step.spawn) {
            const std::uint32_t serial = step.serial + rel;
            const std::uint32_t base = hash(p.seed * kSeedMul + serial * kSerialMul + 0x27D4EB2Fu);
            const float r0 = unit(streamHash(base, 0));
            const float r1 = unit(streamHash(base, 1));
            const float r2 = unit(streamHash(base, 2));
            const float r3 = unit(streamHash(base, 3));
            const float r4 = unit(streamHash(base, 4));
            const float r5 = unit(streamHash(base, 5));
            const float r6 = unit(streamHash(base, 6));
            const float angle = (p.direction + (r0 * 2.0f - 1.0f) * p.spread * 0.5f) * kPi / 180.0f;
            const float speed = p.speed * (1.0f + p.levelSpeed * step.level) * std::max(0.0f, 1.0f + (r1 * 2.0f - 1.0f) * p.speedJitter);
            float x = p.originX * aspect_;
            float y = p.originY;
            switch (p.shape) {
                case particles::Shape::Point:
                    break;
                case particles::Shape::Line:
                    x += (r2 * 2.0f - 1.0f) * p.extent * aspect_;
                    break;
                case particles::Shape::Ring: {
                    const float around = r2 * 2.0f * kPi;
                    x += std::cos(around) * p.extent;
                    y += std::sin(around) * p.extent;
                    break;
                }
                case particles::Shape::Box:
                    x += (r2 * 2.0f - 1.0f) * p.extent * aspect_;
                    y += (r3 * 2.0f - 1.0f) * p.extent;
                    break;
            }
            s.x = x;
            s.y = y;
            s.vx = std::cos(angle) * speed;
            s.vy = std::sin(angle) * speed;
            s.age = 0.0f;
            s.life = std::max(0.05f, p.life * (1.0f + (r4 * 2.0f - 1.0f) * p.lifeJitter));
            s.size = p.size * std::max(0.1f, 1.0f + (r5 * 2.0f - 1.0f) * p.sizeJitter);
            s.hue = r6;
            continue;
        }
        if (s.life <= 0.0f || s.age >= s.life) {
            s.life = 0.0f;
            s.age = 0.0f;
            s.size = 0.0f;
            continue;
        }
        s.vy -= p.gravity * kDt;
        if (p.swirl != 0.0f) {
            const float rx = s.x - p.originX * aspect_;
            const float ry = s.y - p.originY;
            s.vx -= ry * p.swirl * kDt;
            s.vy += rx * p.swirl * kDt;
        }
        if (p.turbulence > 0.0f) {
            std::uint32_t key;
            std::memcpy(&key, &s.hue, sizeof(key));
            const std::uint32_t k = hash(key ^ (step.index * kSeedMul));
            s.vx += centered(k) * p.turbulence * kDt * 4.0f;
            s.vy += centered(hash(k + 1u)) * p.turbulence * kDt * 4.0f;
        }
        s.vx *= dragFactor;
        s.vy *= dragFactor;
        s.x += s.vx * kDt;
        s.y += s.vy * kDt;
        s.age += kDt;
        if (s.age >= s.life || std::fabs(s.x) > limitX || std::fabs(s.y) > 1.35f || !std::isfinite(s.x) || !std::isfinite(s.y)) {
            s.life = 0.0f;
            s.age = 0.0f;
            s.size = 0.0f;
        }
    }
}

int Reference::alive(std::uint32_t used) const {
    int count = 0;
    const std::uint32_t limit = std::min<std::uint32_t>(used, static_cast<std::uint32_t>(capacity_));
    for (std::uint32_t i = 0; i < limit; ++i) {
        if (states_[i].life > 0.0f) {
            ++count;
        }
    }
    return count;
}

std::uint64_t Reference::stateHash(std::uint32_t used) const {
    std::uint64_t value = 1469598103934665603ull;
    const std::uint32_t limit = std::min<std::uint32_t>(used, static_cast<std::uint32_t>(capacity_));
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(states_.data());
    const size_t total = static_cast<size_t>(limit) * sizeof(State);
    for (size_t i = 0; i < total; ++i) {
        value ^= bytes[i];
        value *= 1099511628211ull;
    }
    return value;
}

}

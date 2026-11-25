#pragma once

#include "core/particles.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nvj::gpuparticles {

constexpr int kStateFloats = 8;
constexpr std::size_t kBytesPerParticle = static_cast<std::size_t>(kStateFloats) * sizeof(float) * 2;
constexpr int kCapacityGranule = 1024;
constexpr int kMaxSpawnPerStep = 131072;
constexpr int kMinBudgetMb = 8;
constexpr int kMaxBudgetMb = 512;
constexpr int kDefaultBudgetMb = 64;

struct State {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float age = 0.0f;
    float life = 0.0f;
    float size = 0.0f;
    float hue = 0.0f;
};

struct Step {
    std::uint32_t head = 0;
    std::uint32_t spawn = 0;
    std::uint32_t serial = 0;
    std::uint32_t used = 0;
    std::uint32_t index = 0;
    float level = 0.0f;
};

std::uint32_t hash(std::uint32_t value);
float unit(std::uint32_t hashed);
int clampBudgetMb(int megabytes);
std::size_t bytesFor(int capacity);
int capacityFor(int requested, std::uint64_t availableBytes);

class Budget {
public:
    void setLimit(std::uint64_t bytes);
    std::uint64_t limit() const { return limit_; }
    std::uint64_t used() const { return used_; }
    std::uint64_t available() const { return used_ >= limit_ ? 0 : limit_ - used_; }
    bool tryReserve(std::uint64_t bytes);
    void release(std::uint64_t bytes);

private:
    std::uint64_t limit_ = static_cast<std::uint64_t>(kDefaultBudgetMb) * 1024ull * 1024ull;
    std::uint64_t used_ = 0;
};

class Scheduler {
public:
    void configure(const particles::Params& params, int capacity);
    void reset();
    int advance(const particles::Drive& drive, std::vector<Step>& out);

    int capacity() const { return capacity_; }
    int used() const { return static_cast<int>(used_); }
    std::uint64_t spawned() const { return spawned_; }
    std::uint64_t recycled() const { return spawned_ > static_cast<std::uint64_t>(capacity_) ? spawned_ - static_cast<std::uint64_t>(capacity_) : 0; }

private:
    particles::Params params_;
    int capacity_ = kCapacityGranule;
    bool started_ = false;
    double origin_ = 0.0;
    double lastClock_ = 0.0;
    long long lastTick_ = 0;
    long long lastBeat_ = 0;
    double carry_ = 0.0;
    std::uint32_t pendingBurst_ = 0;
    std::uint32_t head_ = 0;
    std::uint32_t serial_ = 0;
    std::uint32_t used_ = 0;
    std::uint32_t index_ = 0;
    std::uint64_t spawned_ = 0;
};

class Reference {
public:
    Reference(const particles::Params& params, int capacity, float aspect);

    void apply(const Step& step);
    const std::vector<State>& states() const { return states_; }
    int alive(std::uint32_t used) const;
    std::uint64_t stateHash(std::uint32_t used) const;

private:
    particles::Params params_;
    int capacity_;
    float aspect_;
    std::vector<State> states_;
};

}

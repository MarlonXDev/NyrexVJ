#pragma once

#include "core/netsync.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace nvj::netsync {

struct LinkConfig {
    double loss = 0.0;
    double duplicate = 0.0;
    double minDelay = 0.0;
    double maxDelay = 0.0;
    double burstLoss = 0.0;
    std::uint64_t seed = 1;
};

class LossyLink {
public:
    explicit LossyLink(const LinkConfig& config = LinkConfig()) : config_(config), state_(config.seed * 2685821657736338717ull + 88172645463325252ull) {}

    void configure(const LinkConfig& config) { config_ = config; }
    void setDown(bool down) { down_ = down; }
    bool down() const { return down_; }

    void push(const Bytes& packet, double now) {
        ++pushed_;
        if (down_) {
            ++dropped_;
            return;
        }
        if (burstLeft_ > 0) {
            --burstLeft_;
            ++dropped_;
            return;
        }
        if (uniform() < config_.loss) {
            ++dropped_;
            if (config_.burstLoss > 0.0 && uniform() < config_.burstLoss) {
                burstLeft_ = 2 + static_cast<int>(uniform() * 4.0);
            }
            return;
        }
        enqueue(packet, now);
        if (uniform() < config_.duplicate) {
            enqueue(packet, now);
        }
    }

    std::vector<Bytes> pop(double now) {
        std::vector<Bytes> out;
        std::stable_sort(queue_.begin(), queue_.end(), [](const Item& a, const Item& b) { return a.due < b.due; });
        size_t taken = 0;
        while (taken < queue_.size() && queue_[taken].due <= now) {
            out.push_back(std::move(queue_[taken].data));
            ++taken;
        }
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(taken));
        return out;
    }

    std::uint64_t pushed() const { return pushed_; }
    std::uint64_t dropped() const { return dropped_; }
    size_t inFlight() const { return queue_.size(); }
    void clear() { queue_.clear(); }

private:
    struct Item {
        double due = 0.0;
        Bytes data;
    };

    double uniform() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<double>(state_ >> 11) / 9007199254740992.0;
    }

    void enqueue(const Bytes& packet, double now) {
        Item item;
        item.due = now + config_.minDelay + uniform() * std::max(config_.maxDelay - config_.minDelay, 0.0);
        item.data = packet;
        queue_.push_back(std::move(item));
    }

    LinkConfig config_;
    std::uint64_t state_;
    std::vector<Item> queue_;
    bool down_ = false;
    int burstLeft_ = 0;
    std::uint64_t pushed_ = 0;
    std::uint64_t dropped_ = 0;
};

}

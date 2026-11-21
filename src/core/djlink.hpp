#pragma once

#include "core/beat_clock.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace nvj {

constexpr int kDjLinkBeatPort = 50001;
constexpr int kDjLinkMaxPlayer = 6;
constexpr size_t kDjLinkBeatSize = 96;

struct DjBeat {
    int player = 0;
    std::string name;
    double trackBpm = 0.0;
    double pitch = 1.0;
    double bpm = 0.0;
    int beatInBar = 0;
    double nextBeatMs = -1.0;
    double nextBarMs = -1.0;
};

bool isDjLinkPacket(const unsigned char* data, size_t size);
bool parseDjBeat(const unsigned char* data, size_t size, DjBeat& out);

class DjLinkTracker {
public:
    void advance(double dt);
    void onBeat(const DjBeat& beat);
    void onMalformed();
    void onOther();
    void setPlayer(int player);
    int player() const { return selected_; }
    void reset();

    int followed() const { return followed_; }
    bool active() const;
    bool playerSeen(int player) const;
    double bpm() const { return bpm_; }
    int beatInBar() const { return beatInBar_; }
    double secondsSinceBeat() const;
    unsigned long long packets() const { return packets_; }
    unsigned long long ignored() const { return ignored_; }
    unsigned long long malformed() const { return malformed_; }
    unsigned long long other() const { return other_; }

    bool takeObservation(SyncObservation& out, bool barAlign);

private:
    bool accepts(int player) const;

    double time_ = 0.0;
    int selected_ = 0;
    int followed_ = 0;
    double bpm_ = 0.0;
    int beatInBar_ = 0;
    double lastBeat_ = -1.0e9;
    std::array<double, kDjLinkMaxPlayer + 1> seen_{};
    std::array<bool, kDjLinkMaxPlayer + 1> everSeen_{};
    bool pending_ = false;
    double pendingTime_ = 0.0;
    double pendingBpm_ = 0.0;
    int pendingBar_ = 0;
    bool pendingJump_ = false;
    unsigned long long packets_ = 0;
    unsigned long long ignored_ = 0;
    unsigned long long malformed_ = 0;
    unsigned long long other_ = 0;
};

}

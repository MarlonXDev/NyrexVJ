#include "core/djlink.hpp"

#include <algorithm>
#include <cstring>

namespace nvj {

namespace {

constexpr unsigned char kMagic[10] = {0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c};
constexpr unsigned char kBeatType = 0x28;
constexpr double kFollowTimeout = 2.0;
constexpr unsigned long kMaxPitch = 0x00400000;
constexpr double kPitchUnit = 1048576.0;

unsigned long readBe32(const unsigned char* data) {
    return (static_cast<unsigned long>(data[0]) << 24) | (static_cast<unsigned long>(data[1]) << 16) | (static_cast<unsigned long>(data[2]) << 8) | static_cast<unsigned long>(data[3]);
}

unsigned readBe16(const unsigned char* data) {
    return (static_cast<unsigned>(data[0]) << 8) | static_cast<unsigned>(data[1]);
}

}

bool isDjLinkPacket(const unsigned char* data, size_t size) {
    return data != nullptr && size > sizeof(kMagic) && std::memcmp(data, kMagic, sizeof(kMagic)) == 0;
}

bool parseDjBeat(const unsigned char* data, size_t size, DjBeat& out) {
    if (data == nullptr || size < kDjLinkBeatSize) {
        return false;
    }
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0 || data[10] != kBeatType || data[31] != 0x01 || data[32] != 0x00) {
        return false;
    }
    if (data[33] != data[95]) {
        return false;
    }
    const unsigned long pitchRaw = readBe32(data + 84);
    const unsigned bpmRaw = readBe16(data + 90);
    const int bar = data[92];
    if (pitchRaw == 0 || pitchRaw > kMaxPitch || bpmRaw == 0 || bpmRaw == 0xffff || bar > 4) {
        return false;
    }
    DjBeat beat;
    beat.player = data[33];
    for (int i = 11; i < 31 && data[i] != 0; ++i) {
        const unsigned char c = data[i];
        beat.name.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?');
    }
    beat.pitch = static_cast<double>(pitchRaw) / kPitchUnit;
    beat.trackBpm = static_cast<double>(bpmRaw) / 100.0;
    beat.bpm = beat.trackBpm * beat.pitch;
    beat.beatInBar = bar;
    const unsigned long nextBeat = readBe32(data + 36);
    const unsigned long nextBar = readBe32(data + 44);
    beat.nextBeatMs = nextBeat == 0xffffffffUL ? -1.0 : static_cast<double>(nextBeat) / beat.pitch;
    beat.nextBarMs = nextBar == 0xffffffffUL ? -1.0 : static_cast<double>(nextBar) / beat.pitch;
    out = beat;
    return true;
}

void DjLinkTracker::reset() {
    followed_ = 0;
    bpm_ = 0.0;
    beatInBar_ = 0;
    lastBeat_ = time_ - 1.0e9;
    seen_.fill(0.0);
    everSeen_.fill(false);
    pending_ = false;
    pendingJump_ = false;
}

void DjLinkTracker::advance(double dt) {
    time_ += std::max(dt, 0.0);
}

void DjLinkTracker::setPlayer(int player) {
    player = std::clamp(player, 0, kDjLinkMaxPlayer);
    if (player == selected_) {
        return;
    }
    selected_ = player;
    if (player != 0 && followed_ != player) {
        followed_ = 0;
    }
}

bool DjLinkTracker::accepts(int player) const {
    if (selected_ != 0) {
        return selected_ == player;
    }
    if (followed_ != 0 && time_ - lastBeat_ <= kFollowTimeout) {
        return followed_ == player;
    }
    return true;
}

void DjLinkTracker::onMalformed() {
    ++malformed_;
}

void DjLinkTracker::onOther() {
    ++other_;
}

void DjLinkTracker::onBeat(const DjBeat& beat) {
    if (beat.player < 1 || beat.player > kDjLinkMaxPlayer) {
        ++ignored_;
        return;
    }
    seen_[static_cast<size_t>(beat.player)] = time_;
    everSeen_[static_cast<size_t>(beat.player)] = true;
    if (!accepts(beat.player)) {
        ++ignored_;
        return;
    }
    ++packets_;
    pendingJump_ = pendingJump_ || followed_ != beat.player || time_ - lastBeat_ > kFollowTimeout;
    followed_ = beat.player;
    lastBeat_ = time_;
    bpm_ = beat.bpm;
    beatInBar_ = beat.beatInBar;
    pending_ = true;
    pendingTime_ = time_;
    pendingBpm_ = beat.bpm;
    pendingBar_ = beat.beatInBar;
}

bool DjLinkTracker::active() const {
    return followed_ != 0 && time_ - lastBeat_ <= kFollowTimeout;
}

bool DjLinkTracker::playerSeen(int player) const {
    if (player < 1 || player > kDjLinkMaxPlayer) {
        return false;
    }
    return everSeen_[static_cast<size_t>(player)] && time_ - seen_[static_cast<size_t>(player)] <= kFollowTimeout;
}

double DjLinkTracker::secondsSinceBeat() const {
    return time_ - lastBeat_;
}

bool DjLinkTracker::takeObservation(SyncObservation& out, bool barAlign) {
    if (!pending_) {
        return false;
    }
    pending_ = false;
    SyncObservation observation;
    observation.source = SyncSource::DjLink;
    observation.hasPhase = true;
    observation.modulus = barAlign && pendingBar_ >= 1 ? 4 : 1;
    observation.beat = observation.modulus == 4 ? static_cast<double>(pendingBar_ - 1) : 0.0;
    observation.hasTempo = true;
    observation.bpm = pendingBpm_;
    observation.age = std::max(time_ - pendingTime_, 0.0);
    observation.jump = pendingJump_;
    pendingJump_ = false;
    out = observation;
    return true;
}

}

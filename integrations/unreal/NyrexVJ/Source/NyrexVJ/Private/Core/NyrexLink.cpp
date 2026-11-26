#include "NyrexLink.h"

#include <algorithm>
#include <cmath>

namespace NyrexCore {

namespace {

bool usable(double now) {
    return std::isfinite(now);
}

double since(double now, double then) {
    return now > then ? now - then : 0.0;
}

}

LinkMonitor::LinkMonitor(double timeoutSeconds) : timeout_(timeoutSeconds > 0.1 && usable(timeoutSeconds) ? timeoutSeconds : kConnectionTimeoutSeconds) {}

void LinkMonitor::setListening(bool listening, double now) {
    if (!usable(now)) {
        return;
    }
    if (listening == listening_) {
        return;
    }
    listening_ = listening;
    wire_ = false;
    everConnected_ = false;
    ended_ = false;
    incompatible_ = false;
    incompatibleReported_ = false;
    havePacket_ = false;
    haveHello_ = false;
    longestGap_ = 0.0;
    windowStart_ = now;
    windowPackets_ = 0;
    rate_ = 0.0;
    packets_ = 0;
    hellos_ = 0;
    connections_ = 0;
    losses_ = 0;
    state_ = listening ? LinkState::Waiting : LinkState::Stopped;
}

void LinkMonitor::refreshRate(double now) {
    const double span = since(now, windowStart_);
    if (span >= 1.0) {
        rate_ = static_cast<double>(windowPackets_) / span;
        windowStart_ = now;
        windowPackets_ = 0;
    }
}

void LinkMonitor::onPacket(double now) {
    if (!listening_ || !usable(now)) {
        return;
    }
    if (havePacket_) {
        longestGap_ = std::max(longestGap_, since(now, lastPacket_));
    }
    havePacket_ = true;
    lastPacket_ = now;
    ++packets_;
    ++windowPackets_;
    refreshRate(now);
}

void LinkMonitor::onHello(double now, bool schemaSupported) {
    if (!listening_ || !usable(now)) {
        return;
    }
    haveHello_ = true;
    lastHello_ = now;
    ++hellos_;
    ended_ = false;
    wire_ = schemaSupported;
    if (schemaSupported) {
        incompatible_ = false;
        incompatibleReported_ = false;
    } else {
        incompatible_ = true;
    }
}

void LinkMonitor::onBye(double now) {
    if (!listening_ || !usable(now)) {
        return;
    }
    wire_ = false;
    ended_ = true;
}

LinkChange LinkMonitor::evaluate(double now) {
    LinkChange change;
    if (!usable(now)) {
        return change;
    }
    if (!listening_) {
        state_ = LinkState::Stopped;
        return change;
    }
    refreshRate(now);
    const bool fresh = haveHello_ && since(now, lastHello_) < timeout_;
    const bool alive = wire_ && fresh;
    const bool incompatibleNow = incompatible_ && fresh;
    const LinkState before = state_;
    if (alive) {
        state_ = LinkState::Connected;
    } else if (incompatibleNow) {
        state_ = LinkState::Incompatible;
    } else if (before == LinkState::Connected) {
        state_ = ended_ ? LinkState::Ended : LinkState::Lost;
    } else if (before == LinkState::Incompatible) {
        state_ = everConnected_ ? LinkState::Lost : LinkState::Waiting;
    }
    if (before != LinkState::Connected && state_ == LinkState::Connected) {
        change.connected = true;
        everConnected_ = true;
        connectedAt_ = now;
        ++connections_;
    }
    if (before == LinkState::Connected && state_ != LinkState::Connected) {
        change.disconnected = true;
        if (state_ == LinkState::Lost) {
            ++losses_;
        }
    }
    if (incompatible_ && !incompatibleReported_ && haveHello_) {
        incompatibleReported_ = true;
        change.incompatible = true;
    }
    return change;
}

LinkStats LinkMonitor::stats(double now) const {
    LinkStats out;
    out.state = state_;
    out.packets = packets_;
    out.hellos = hellos_;
    out.connections = connections_;
    out.losses = losses_;
    if (havePacket_ && usable(now)) {
        out.secondsSinceLastPacket = since(now, lastPacket_);
    }
    if (haveHello_ && usable(now)) {
        out.secondsSinceLastHello = since(now, lastHello_);
    }
    if (state_ == LinkState::Connected && usable(now)) {
        out.connectedSeconds = since(now, connectedAt_);
    }
    out.longestGapSeconds = longestGap_;
    const bool recent = havePacket_ && usable(now) && since(now, lastPacket_) <= 2.0;
    out.packetsPerSecond = recent ? rate_ : 0.0;
    return out;
}

LatencyTracker::LatencyTracker(double timeoutSeconds) : timeout_(timeoutSeconds > 0.05 && std::isfinite(timeoutSeconds) ? timeoutSeconds : kPingTimeoutSeconds) {
    window_.reserve(kLatencyWindow);
    pending_.reserve(kMaxPendingPings);
}

bool LatencyTracker::due(double now, double intervalSeconds) const {
    if (!usable(now) || !(intervalSeconds > 0.0)) {
        return false;
    }
    return !haveSent_ || since(now, lastSent_) >= intervalSeconds;
}

std::int32_t LatencyTracker::beginPing(double now) {
    if (!usable(now)) {
        return 0;
    }
    expire(now);
    if (pending_.size() >= kMaxPendingPings) {
        pending_.erase(pending_.begin());
        ++lost_;
    }
    const std::int32_t id = nextId_;
    nextId_ = nextId_ >= kMaxPingId ? 1 : nextId_ + 1;
    Pending item;
    item.id = id;
    item.sentAt = now;
    pending_.push_back(item);
    haveSent_ = true;
    lastSent_ = now;
    ++sent_;
    return id;
}

bool LatencyTracker::onPong(std::int32_t id, double now) {
    if (!usable(now)) {
        return false;
    }
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (pending_[i].id != id) {
            continue;
        }
        const double sentAt = pending_[i].sentAt;
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        if (now < sentAt || now - sentAt > timeout_) {
            ++ignored_;
            return false;
        }
        const double ms = (now - sentAt) * 1000.0;
        if (window_.size() < kLatencyWindow) {
            window_.push_back(ms);
        } else {
            window_[cursor_] = ms;
            cursor_ = (cursor_ + 1) % kLatencyWindow;
        }
        last_ = ms;
        ++received_;
        return true;
    }
    ++ignored_;
    return false;
}

void LatencyTracker::expire(double now) {
    if (!usable(now)) {
        return;
    }
    for (std::size_t i = 0; i < pending_.size();) {
        if (now - pending_[i].sentAt > timeout_) {
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
            ++lost_;
        } else {
            ++i;
        }
    }
}

void LatencyTracker::reset() {
    pending_.clear();
    window_.clear();
    cursor_ = 0;
    haveSent_ = false;
    sent_ = 0;
    received_ = 0;
    lost_ = 0;
    ignored_ = 0;
    last_ = 0.0;
}

LatencyStats LatencyTracker::stats() const {
    LatencyStats out;
    out.sent = sent_;
    out.received = received_;
    out.lost = lost_;
    out.ignored = ignored_;
    out.samples = window_.size();
    if (window_.empty()) {
        return out;
    }
    out.valid = true;
    out.lastMs = last_;
    double sum = 0.0;
    out.minMs = window_[0];
    out.maxMs = window_[0];
    for (const double value : window_) {
        sum += value;
        out.minMs = std::min(out.minMs, value);
        out.maxMs = std::max(out.maxMs, value);
    }
    out.averageMs = sum / static_cast<double>(window_.size());
    std::vector<double> sorted = window_;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1;
    out.p95Ms = sorted[std::min(index, sorted.size() - 1)];
    return out;
}

bool encodePing(std::int32_t id, std::vector<std::uint8_t>& out) {
    if (id < 0 || id > kMaxPingId) {
        return false;
    }
    OscPacketMessage message;
    message.address = "/nyrex/ping";
    message.values.push_back(OscValue::fromInteger(id));
    return encodeMessage(message, out);
}

const char* linkStateName(LinkState state) {
    switch (state) {
    case LinkState::Stopped:
        return "Stopped";
    case LinkState::Waiting:
        return "Waiting";
    case LinkState::Connected:
        return "Connected";
    case LinkState::Lost:
        return "Lost";
    case LinkState::Ended:
        return "Ended";
    case LinkState::Incompatible:
        return "Incompatible";
    }
    return "Stopped";
}

}

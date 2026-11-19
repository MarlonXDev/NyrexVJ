#include "control/stage_out.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

bool validHost(const std::string& host) {
    if (host == "localhost") {
        return true;
    }
    int parts = 0;
    int value = -1;
    for (const char c : host) {
        if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value) * 10 + (c - '0');
            if (value > 255) {
                return false;
            }
        } else if (c == '.' && value >= 0) {
            ++parts;
            value = -1;
        } else {
            return false;
        }
    }
    return parts == 3 && value >= 0;
}

}

void StageOutput::configure(const DmxConfig& dmxIn, const VrchatConfig& vrchatIn) {
    DmxConfig dmx = dmxIn;
    VrchatConfig vrchat = vrchatIn;
    normalizeDmx(dmx);
    normalizeVrchat(vrchat);
    if (configured_ && dmx == dmx_ && vrchat == vrchat_) {
        return;
    }
    const bool artnetTargetChanged = !configured_ || !artnetSocket_.isOpen() || dmx.artnetHost != dmx_.artnetHost || dmx.artnetPort != dmx_.artnetPort || dmx.artnetEnabled != dmx_.artnetEnabled;
    const bool vrchatTargetChanged = !configured_ || !vrchatSocket_.isOpen() || vrchat.host != vrchat_.host || vrchat.port != vrchat_.port || vrchat.enabled != vrchat_.enabled;
    const bool paramsChanged = !configured_ || vrchat.params != vrchat_.params;
    dmx_ = dmx;
    vrchat_ = vrchat;
    configured_ = true;

    if (!dmx_.artnetEnabled) {
        artnetSocket_.close();
        artnetError_.clear();
    } else if (artnetTargetChanged) {
        artnetSocket_.close();
        artnetError_.clear();
        if (!validHost(dmx_.artnetHost)) {
            artnetError_ = "The Art-Net address must be an IPv4 address or localhost";
        } else {
            std::string error;
            if (!artnetSocket_.connectTo(dmx_.artnetHost, dmx_.artnetPort, error)) {
                artnetError_ = error;
            }
        }
        artnetAccumulator_ = 0.0;
    }

    if (!vrchat_.enabled) {
        vrchatSocket_.close();
        vrchatError_.clear();
    } else if (vrchatTargetChanged) {
        vrchatSocket_.close();
        vrchatError_.clear();
        if (!validHost(vrchat_.host)) {
            vrchatError_ = "The VRChat address must be an IPv4 address or localhost";
        } else {
            std::string error;
            if (!vrchatSocket_.connectTo(vrchat_.host, vrchat_.port, error)) {
                vrchatError_ = error;
            }
        }
        vrchatAccumulator_ = 0.0;
        vrchatRefresh_ = 0.0;
    }
    if (paramsChanged || vrchatTargetChanged) {
        memory_.assign(vrchat_.params.size(), Memory{});
        vrchatRefresh_ = 0.0;
    }
}

bool StageOutput::universeUsed(int index) const {
    if (test_ != StageTest::Off) {
        return true;
    }
    return std::any_of(dmx_.lanes.begin(), dmx_.lanes.end(), [index](const DmxLane& lane) { return lane.universe - 1 == index; });
}

void StageOutput::computeUniverses(double dt, const SourceFunction& source) {
    switch (test_) {
    case StageTest::Full:
        fillDmxFull(universes_);
        break;
    case StageTest::Ramp:
        fillDmxRamp(universes_);
        break;
    case StageTest::Chase:
        chaseClock_ += std::max(dt, 0.0);
        fillDmxChase(universes_, static_cast<int>(chaseClock_ * 24.0), kDmxChannels);
        break;
    case StageTest::Off:
        computeDmx(dmx_, source, universes_);
        break;
    }
    renderVrslGrid(universes_, dmx_.stripPadUniverses, grid_.data());
}

void StageOutput::sendArtnet() {
    for (int u = 0; u < kDmxUniverses; ++u) {
        if (!universeUsed(u)) {
            continue;
        }
        sequence_[u] = static_cast<std::uint8_t>(sequence_[u] == 255 ? 1 : sequence_[u] + 1);
        const std::vector<std::uint8_t> packet = artnetDmxPacket(artnetUniverse(dmx_, u), sequence_[u], universes_[static_cast<size_t>(u)].data(), kDmxChannels);
        if (artnetSocket_.send(packet.data(), static_cast<int>(packet.size()))) {
            ++artnetPackets_;
        }
    }
}

std::vector<OscMessage> StageOutput::composeVrchat(const SourceFunction& source, bool refresh) {
    std::vector<OscMessage> out;
    if (memory_.size() != vrchat_.params.size()) {
        memory_.assign(vrchat_.params.size(), Memory{});
    }
    for (size_t i = 0; i < vrchat_.params.size(); ++i) {
        const VrchatParam& param = vrchat_.params[i];
        const float value = evaluateBinding(param.binding, source);
        Memory& memory = memory_[i];
        OscMessage message;
        message.address = "/avatar/parameters/" + param.name;
        bool changed = false;
        if (param.type == VrchatType::Float) {
            changed = memory.lastFloat < 0.0f || std::fabs(value - memory.lastFloat) >= 0.002f;
            message.arguments.push_back(OscArgument::fromFloat(value));
            if (changed || refresh) {
                memory.lastFloat = value;
            }
        } else if (param.type == VrchatType::Int) {
            const int whole = static_cast<int>(std::lround(value * 255.0f));
            changed = whole != memory.lastInt;
            message.arguments.push_back(OscArgument::fromInt(whole));
            if (changed || refresh) {
                memory.lastInt = whole;
            }
        } else {
            const int on = value >= 0.5f ? 1 : 0;
            changed = on != memory.lastBool;
            message.arguments.push_back(OscArgument::fromBool(on != 0));
            if (changed || refresh) {
                memory.lastBool = on;
            }
        }
        if (changed || refresh) {
            out.push_back(std::move(message));
        }
    }
    return out;
}

void StageOutput::sendVrchat(const SourceFunction& source) {
    const bool refresh = vrchatRefresh_ <= 0.0;
    if (refresh) {
        vrchatRefresh_ = 1.0;
    }
    const std::vector<OscMessage> messages = composeVrchat(source, refresh);
    if (messages.empty()) {
        return;
    }
    std::vector<OscMessage> chunk;
    size_t bytes = 16;
    auto flush = [this, &chunk, &bytes]() {
        if (!chunk.empty()) {
            const std::vector<unsigned char> packet = encodeOscBundle(chunk);
            if (vrchatSocket_.send(packet.data(), static_cast<int>(packet.size()))) {
                ++vrchatPackets_;
            }
            chunk.clear();
            bytes = 16;
        }
    };
    for (const OscMessage& item : messages) {
        const size_t size = encodeOscMessage(item).size() + 4;
        if (!chunk.empty() && bytes + size > 1400) {
            flush();
        }
        chunk.push_back(item);
        bytes += size;
    }
    flush();
}

void StageOutput::update(double dt, const SourceFunction& source) {
    if (!configured_) {
        return;
    }
    dt = std::max(dt, 0.0);
    computeUniverses(dt, source);

    if (dmx_.artnetEnabled && artnetSocket_.isOpen()) {
        const double interval = 1.0 / static_cast<double>(dmx_.artnetRate);
        artnetAccumulator_ += dt;
        if (artnetAccumulator_ >= interval) {
            artnetAccumulator_ -= interval;
            if (artnetAccumulator_ >= interval) {
                artnetAccumulator_ = 0.0;
            }
            sendArtnet();
        }
    }

    if (vrchat_.enabled && vrchatSocket_.isOpen()) {
        const double interval = 1.0 / static_cast<double>(vrchat_.rate);
        vrchatAccumulator_ += dt;
        vrchatRefresh_ -= dt;
        if (vrchatAccumulator_ >= interval) {
            vrchatAccumulator_ -= interval;
            if (vrchatAccumulator_ >= interval) {
                vrchatAccumulator_ = 0.0;
            }
            sendVrchat(source);
        }
    }
}

void StageOutput::stop() {
    artnetSocket_.close();
    vrchatSocket_.close();
}

}

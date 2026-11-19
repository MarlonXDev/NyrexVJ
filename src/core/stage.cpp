#include "core/stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nvj {

namespace {

float finite(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

void normalizeBinding(SourceBinding& binding) {
    binding.source = std::clamp(binding.source, 0, kStageSourceCount - 1);
    binding.fixed = std::clamp(finite(binding.fixed, 0.0f), 0.0f, 1.0f);
    binding.low = std::clamp(finite(binding.low, 0.0f), 0.0f, 1.0f);
    binding.high = std::clamp(finite(binding.high, 1.0f), 0.0f, 1.0f);
}

}

bool validVrchatName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

float evaluateBinding(const SourceBinding& binding, const SourceFunction& source) {
    float value = binding.source == 0 ? binding.fixed : (source ? source(binding.source) : 0.0f);
    value = std::clamp(finite(value, 0.0f), 0.0f, 1.0f);
    if (binding.invert) {
        value = 1.0f - value;
    }
    value = binding.low + (binding.high - binding.low) * value;
    return std::clamp(value, 0.0f, 1.0f);
}

void normalizeDmx(DmxConfig& config) {
    config.artnetPort = std::clamp(config.artnetPort, 1, 65535);
    config.artnetNet = std::clamp(config.artnetNet, 0, 127);
    config.artnetFirstUniverse = std::clamp(config.artnetFirstUniverse, 0, 255 - kDmxUniverses);
    config.artnetRate = std::clamp(config.artnetRate, 1, 44);
    if (config.artnetHost.empty()) {
        config.artnetHost = "127.0.0.1";
    }
    if (config.lanes.size() > kMaxDmxLanes) {
        config.lanes.resize(kMaxDmxLanes);
    }
    for (DmxLane& lane : config.lanes) {
        lane.universe = std::clamp(lane.universe, 1, kDmxUniverses);
        lane.channel = std::clamp(lane.channel, 1, kDmxChannels);
        if (lane.channel == kDmxChannels) {
            lane.fine = false;
        }
        normalizeBinding(lane.binding);
    }
}

void normalizeVrchat(VrchatConfig& config) {
    config.port = std::clamp(config.port, 1, 65535);
    config.rate = std::clamp(config.rate, 1, 60);
    if (config.host.empty()) {
        config.host = "127.0.0.1";
    }
    std::vector<VrchatParam> kept;
    for (VrchatParam& param : config.params) {
        if (kept.size() >= kMaxVrchatParams || !validVrchatName(param.name)) {
            continue;
        }
        const bool duplicate = std::any_of(kept.begin(), kept.end(), [&param](const VrchatParam& other) { return other.name == param.name; });
        if (duplicate) {
            continue;
        }
        normalizeBinding(param.binding);
        kept.push_back(param);
    }
    config.params = std::move(kept);
}

void computeDmx(const DmxConfig& config, const SourceFunction& source, DmxUniverses& out) {
    for (auto& universe : out) {
        universe.fill(0);
    }
    for (const DmxLane& lane : config.lanes) {
        const int universe = std::clamp(lane.universe, 1, kDmxUniverses) - 1;
        const int channel = std::clamp(lane.channel, 1, kDmxChannels) - 1;
        const float value = evaluateBinding(lane.binding, source);
        if (lane.fine && channel + 1 < kDmxChannels) {
            const int wide = static_cast<int>(std::lround(value * 65535.0f));
            out[static_cast<size_t>(universe)][static_cast<size_t>(channel)] = static_cast<std::uint8_t>(wide >> 8);
            out[static_cast<size_t>(universe)][static_cast<size_t>(channel + 1)] = static_cast<std::uint8_t>(wide & 0xff);
        } else {
            out[static_cast<size_t>(universe)][static_cast<size_t>(channel)] = static_cast<std::uint8_t>(std::lround(value * 255.0f));
        }
    }
}

void fillDmxChase(DmxUniverses& out, int step, int length) {
    for (auto& universe : out) {
        universe.fill(0);
    }
    length = std::clamp(length, 1, kDmxChannels);
    const int channel = ((step % length) + length) % length;
    out[0][static_cast<size_t>(channel)] = 255;
}

void fillDmxFull(DmxUniverses& out) {
    for (auto& universe : out) {
        universe.fill(255);
    }
}

void fillDmxRamp(DmxUniverses& out) {
    for (auto& universe : out) {
        for (int c = 0; c < kDmxChannels; ++c) {
            universe[static_cast<size_t>(c)] = static_cast<std::uint8_t>((c * 255 + kDmxChannels / 2) / (kDmxChannels - 1));
        }
    }
}

std::vector<std::uint8_t> artnetDmxPacket(int universe, std::uint8_t sequence, const std::uint8_t* data, int length) {
    length = std::clamp(length, 2, kDmxChannels);
    if (length % 2 != 0) {
        ++length;
    }
    universe = std::clamp(universe, 0, 32767);
    std::vector<std::uint8_t> packet(18u + static_cast<size_t>(length), 0);
    std::memcpy(packet.data(), "Art-Net", 8);
    packet[8] = 0x00;
    packet[9] = 0x50;
    packet[10] = 0;
    packet[11] = 14;
    packet[12] = sequence;
    packet[13] = 0;
    packet[14] = static_cast<std::uint8_t>(universe & 0xff);
    packet[15] = static_cast<std::uint8_t>((universe >> 8) & 0x7f);
    packet[16] = static_cast<std::uint8_t>((length >> 8) & 0xff);
    packet[17] = static_cast<std::uint8_t>(length & 0xff);
    if (data) {
        std::memcpy(packet.data() + 18, data, static_cast<size_t>(std::min(length, kDmxChannels)));
    }
    return packet;
}

int artnetUniverse(const DmxConfig& config, int universeIndex) {
    return (config.artnetNet << 8) | (config.artnetFirstUniverse + universeIndex);
}

int vrslCell(int universe, int channel, bool padUniverses) {
    if (universe < 1 || universe > kDmxUniverses || channel < 1 || channel > kDmxChannels) {
        return -1;
    }
    const int stride = padUniverses ? kVrslSectorsPerUniverse * kVrslRows : kDmxChannels;
    const int cell = (universe - 1) * stride + (channel - 1);
    return cell < kVrslCells ? cell : -1;
}

void vrslPixel(int cell, int& column, int& row) {
    column = cell / kVrslRows;
    row = cell % kVrslRows;
}

void renderVrslGrid(const DmxUniverses& universes, bool padUniverses, std::uint8_t* grid) {
    std::memset(grid, 0, static_cast<size_t>(kVrslCells));
    for (int u = 0; u < kDmxUniverses; ++u) {
        for (int c = 0; c < kDmxChannels; ++c) {
            const int cell = vrslCell(u + 1, c + 1, padUniverses);
            if (cell < 0) {
                continue;
            }
            int column = 0;
            int row = 0;
            vrslPixel(cell, column, row);
            grid[row * kVrslColumns + column] = universes[static_cast<size_t>(u)][static_cast<size_t>(c)];
        }
    }
}

std::string vrslPlace(int universe, int channel, bool padUniverses) {
    const int cell = vrslCell(universe, channel, padUniverses);
    if (cell < 0) {
        return std::string();
    }
    int column = 0;
    int row = 0;
    vrslPixel(cell, column, row);
    return std::to_string(column + 1) + "/" + std::to_string(row + 1);
}

}

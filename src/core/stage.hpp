#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nvj {

constexpr int kDmxUniverses = 4;
constexpr int kDmxChannels = 512;
constexpr size_t kMaxDmxLanes = 128;
constexpr size_t kMaxVrchatParams = 32;
constexpr int kVrslColumns = 120;
constexpr int kVrslRows = 13;
constexpr int kVrslBlock = 16;
constexpr int kVrslCells = kVrslColumns * kVrslRows;
constexpr int kVrslSectorsPerUniverse = 40;
constexpr int kStageSourceCount = 20;

struct SourceBinding {
    int source = 0;
    float fixed = 0.0f;
    float low = 0.0f;
    float high = 1.0f;
    bool invert = false;

    bool operator==(const SourceBinding& other) const = default;
};

struct DmxLane {
    std::string name;
    int universe = 1;
    int channel = 1;
    bool fine = false;
    SourceBinding binding;

    bool operator==(const DmxLane& other) const = default;
};

enum class VrchatType { Float = 0, Bool = 1, Int = 2 };

struct VrchatParam {
    std::string name;
    VrchatType type = VrchatType::Float;
    SourceBinding binding;

    bool operator==(const VrchatParam& other) const = default;
};

struct DmxConfig {
    bool artnetEnabled = false;
    std::string artnetHost = "127.0.0.1";
    int artnetPort = 6454;
    int artnetNet = 0;
    int artnetFirstUniverse = 0;
    int artnetRate = 30;
    bool stripEnabled = false;
    bool stripAtTop = true;
    bool stripPadUniverses = false;
    std::vector<DmxLane> lanes;

    bool operator==(const DmxConfig& other) const = default;
};

struct VrchatConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 9000;
    int rate = 30;
    std::vector<VrchatParam> params;

    bool operator==(const VrchatConfig& other) const = default;
};

using DmxUniverses = std::array<std::array<std::uint8_t, kDmxChannels>, kDmxUniverses>;
using SourceFunction = std::function<float(int)>;

bool validVrchatName(const std::string& name);
float evaluateBinding(const SourceBinding& binding, const SourceFunction& source);
void normalizeDmx(DmxConfig& config);
void normalizeVrchat(VrchatConfig& config);
void computeDmx(const DmxConfig& config, const SourceFunction& source, DmxUniverses& out);
void fillDmxChase(DmxUniverses& out, int step, int length);
void fillDmxFull(DmxUniverses& out);
void fillDmxRamp(DmxUniverses& out);

std::vector<std::uint8_t> artnetDmxPacket(int universe, std::uint8_t sequence, const std::uint8_t* data, int length);
int artnetUniverse(const DmxConfig& config, int universeIndex);

int vrslCell(int universe, int channel, bool padUniverses);
void vrslPixel(int cell, int& column, int& row);
void renderVrslGrid(const DmxUniverses& universes, bool padUniverses, std::uint8_t* grid);
std::string vrslPlace(int universe, int channel, bool padUniverses);

}

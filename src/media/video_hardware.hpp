#pragma once

#include <array>
#include <string>
#include <vector>

namespace nvj {

enum class ProbeState { Untested, Supported, Unsupported, Failed };

constexpr size_t kProbeCodecCount = 3;

struct HardwareProbeCell {
    ProbeState state = ProbeState::Untested;
    std::string detail;
};

struct HardwareProbeRow {
    std::string backend;
    std::string label;
    bool deviceFound = false;
    std::string detail;
    std::array<HardwareProbeCell, kProbeCodecCount> codecs;
};

struct HardwareProbeReport {
    bool ffmpegAvailable = false;
    std::string problem;
    std::vector<HardwareProbeRow> rows;
};

const char* probeCodecLabel(size_t index);
std::vector<std::string> hardwareBackendOrder();
std::string hardwareBackendLabel(const std::string& backend);
HardwareProbeReport probeHardwareDecoding();

}

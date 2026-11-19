#pragma once

#include "control/osc.hpp"
#include "control/udp_socket.hpp"
#include "core/stage.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

enum class StageTest { Off = 0, Full = 1, Ramp = 2, Chase = 3 };

class StageOutput {
public:
    void configure(const DmxConfig& dmx, const VrchatConfig& vrchat);
    void update(double dt, const SourceFunction& source);
    void stop();
    void setTest(StageTest test) { test_ = test; }
    StageTest test() const { return test_; }

    const DmxUniverses& universes() const { return universes_; }
    const std::uint8_t* vrslGrid() const { return grid_.data(); }
    bool artnetSending() const { return artnetSocket_.isOpen(); }
    bool vrchatSending() const { return vrchatSocket_.isOpen(); }
    const std::string& artnetError() const { return artnetError_; }
    const std::string& vrchatError() const { return vrchatError_; }
    unsigned long long artnetPackets() const { return artnetPackets_; }
    unsigned long long vrchatPackets() const { return vrchatPackets_; }
    std::vector<OscMessage> composeVrchat(const SourceFunction& source, bool refresh);

private:
    struct Memory {
        float lastFloat = -1.0f;
        int lastInt = -1;
        int lastBool = -1;
    };

    void computeUniverses(double dt, const SourceFunction& source);
    void sendArtnet();
    void sendVrchat(const SourceFunction& source);
    bool universeUsed(int index) const;

    DmxConfig dmx_;
    VrchatConfig vrchat_;
    bool configured_ = false;
    StageTest test_ = StageTest::Off;
    DmxUniverses universes_{};
    std::vector<std::uint8_t> grid_ = std::vector<std::uint8_t>(kVrslCells, 0);
    UdpSocket artnetSocket_;
    UdpSocket vrchatSocket_;
    std::string artnetError_;
    std::string vrchatError_;
    unsigned long long artnetPackets_ = 0;
    unsigned long long vrchatPackets_ = 0;
    double artnetAccumulator_ = 0.0;
    double vrchatAccumulator_ = 0.0;
    double vrchatRefresh_ = 0.0;
    double chaseClock_ = 0.0;
    std::uint8_t sequence_[kDmxUniverses] = {0, 0, 0, 0};
    std::vector<Memory> memory_;
};

}

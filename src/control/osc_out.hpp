#pragma once

#include "audio/audio_features.hpp"
#include "control/control_targets.hpp"
#include "control/osc.hpp"
#include "control/udp_socket.hpp"
#include "core/settings.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

constexpr int kOscSchema = 1;
constexpr int kOscSpectrumBands = 32;
constexpr size_t kOscMaxParams = 16;

struct OscOutConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 9011;
    int rate = 60;
    bool spectrum = true;
    int inputPort = 0;
    std::string version;
    std::vector<OscOutParam> params;
};

struct OscOutFrame {
    AudioFeatures audio;
    float bpm = 120.0f;
    double beat = 0.0;
};

bool validOscParamName(const std::string& name);

class OscSender {
public:
    void configure(const OscOutConfig& config);
    void update(double dt, const OscOutFrame& frame, const ControlRouter& router);
    void stop();
    std::vector<OscMessage> compose(const OscOutFrame& frame, const ControlRouter& router, bool tick);
    std::vector<OscMessage> hello() const;
    std::vector<OscMessage> bye() const;

    bool sending() const { return socket_.isOpen(); }
    const std::string& error() const { return error_; }
    unsigned long long packets() const { return packets_; }
    size_t parameterCount() const { return resolved_.size(); }

private:
    struct Resolved {
        std::string address;
        int target = -1;
    };

    void resolveParams();
    void transmit(const std::vector<OscMessage>& messages);

    OscOutConfig config_;
    bool configured_ = false;
    UdpSocket socket_;
    std::vector<Resolved> resolved_;
    std::string error_;
    unsigned long long packets_ = 0;
    double accumulator_ = 0.0;
    double nextHello_ = 0.0;
    int helloBurst_ = 0;
    bool haveBeat_ = false;
    long long lastBeat_ = 0;
    bool haveOnset_ = false;
    std::uint32_t lastOnset_ = 0;
};

}

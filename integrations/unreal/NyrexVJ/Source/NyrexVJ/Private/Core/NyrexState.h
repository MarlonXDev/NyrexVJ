#pragma once

#include "NyrexOsc.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NyrexCore {

constexpr int kSupportedSchema = 1;
constexpr int kSpectrumBands = 32;
constexpr std::size_t kMaxParameters = 64;
constexpr std::size_t kMaxParameterNameLength = 32;
constexpr std::size_t kMaxEventsPerApply = 256;
constexpr std::size_t kMaxControlAddressLength = 128;

struct NyrexState {
    bool helloSeen = false;
    bool schemaSupported = true;
    std::string appName;
    std::string appVersion;
    int schema = 0;
    int inputPort = 0;

    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float level = 0.0f;
    float kick = 0.0f;
    std::array<float, kSpectrumBands> spectrum{};

    float bpm = 0.0f;
    std::int32_t beat = 0;
    float beatPhase = 0.0f;
    std::int32_t beatInBar = 0;
    std::int32_t onsetCount = 0;

    std::unordered_map<std::string, float> parameters;
};

struct NyrexEvents {
    bool hello = false;
    bool bye = false;
    std::vector<std::pair<std::int32_t, std::int32_t>> beats;
    std::vector<std::int32_t> onsets;
};

float clampUnit(float value);
bool validParameterName(const std::string& name);
bool validControlAddress(const std::string& address);
std::string columnAddress(int column);
std::string deckAddress(int deck);
std::string layerOpacityAddress(int layer);
extern const char* const kMasterLevelAddress;
extern const char* const kTapAddress;
void applyMessage(NyrexState& state, const OscPacketMessage& message, NyrexEvents& events);
void applyPacket(NyrexState& state, const std::uint8_t* data, std::size_t size, NyrexEvents& events);

}

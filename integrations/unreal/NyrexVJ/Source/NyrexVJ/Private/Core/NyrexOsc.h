#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace NyrexCore {

struct OscValue {
    enum class Kind : std::uint8_t { Integer, Number, Text };

    Kind kind = Kind::Integer;
    std::int32_t integer = 0;
    float number = 0.0f;
    std::string text;

    static OscValue fromInteger(std::int32_t value);
    static OscValue fromNumber(float value);
    static OscValue fromText(const std::string& value);
};

struct OscPacketMessage {
    std::string address;
    std::vector<OscValue> values;
};

constexpr std::size_t kMaxPacketBytes = 65536;
constexpr int kMaxBundleDepth = 4;
constexpr std::size_t kMaxMessagesPerPacket = 256;
constexpr std::size_t kMaxArguments = 64;
constexpr std::size_t kMaxStringBytes = 512;

bool decodePacket(const std::uint8_t* data, std::size_t size, std::vector<OscPacketMessage>& out);
bool encodeMessage(const OscPacketMessage& message, std::vector<std::uint8_t>& out);

}

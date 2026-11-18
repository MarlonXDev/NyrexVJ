#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nvj {

struct OscArgument {
    enum class Type { Float, Int, True, False, Nil, Text };

    Type type = Type::Nil;
    float number = 0.0f;
    std::string text;

    static OscArgument fromFloat(float value);
    static OscArgument fromInt(int value);
    static OscArgument fromBool(bool value);
    static OscArgument fromText(const std::string& value);
};

struct OscMessage {
    std::string address;
    std::vector<OscArgument> arguments;
};

bool parseOscPacket(const unsigned char* data, size_t size, std::vector<OscMessage>& messages);
std::vector<unsigned char> encodeOscMessage(const OscMessage& message);
std::vector<unsigned char> encodeOscBundle(const std::vector<OscMessage>& messages);
std::string describeOsc(const OscMessage& message);

}

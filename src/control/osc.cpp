#include "control/osc.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace nvj {

namespace {

constexpr int kMaxBundleDepth = 4;

struct Reader {
    const unsigned char* data;
    size_t size;
    size_t position = 0;

    size_t remaining() const { return size - position; }

    bool readString(std::string& out) {
        size_t end = position;
        while (end < size && data[end] != 0) {
            ++end;
        }
        if (end >= size) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(data + position), end - position);
        const size_t padded = (end - position + 1 + 3) & ~static_cast<size_t>(3);
        if (padded > remaining()) {
            return false;
        }
        position += padded;
        return true;
    }

    bool readWord(uint32_t& out) {
        if (remaining() < 4) {
            return false;
        }
        out = (static_cast<uint32_t>(data[position]) << 24) | (static_cast<uint32_t>(data[position + 1]) << 16) | (static_cast<uint32_t>(data[position + 2]) << 8) | static_cast<uint32_t>(data[position + 3]);
        position += 4;
        return true;
    }

    bool skip(size_t count) {
        if (remaining() < count) {
            return false;
        }
        position += count;
        return true;
    }
};

bool parseMessage(const unsigned char* data, size_t size, OscMessage& message) {
    Reader reader{data, size};
    if (!reader.readString(message.address) || message.address.empty() || message.address[0] != '/') {
        return false;
    }
    if (reader.remaining() == 0) {
        return true;
    }
    std::string tags;
    if (!reader.readString(tags) || tags.empty() || tags[0] != ',') {
        return false;
    }
    for (size_t i = 1; i < tags.size(); ++i) {
        uint32_t word = 0;
        switch (tags[i]) {
        case 'i':
            if (!reader.readWord(word)) {
                return false;
            }
            message.arguments.push_back(OscArgument::fromInt(static_cast<int>(static_cast<int32_t>(word))));
            break;
        case 'f': {
            if (!reader.readWord(word)) {
                return false;
            }
            float value = 0.0f;
            std::memcpy(&value, &word, sizeof(value));
            message.arguments.push_back(OscArgument::fromFloat(value));
            break;
        }
        case 'd': {
            uint32_t high = 0;
            uint32_t low = 0;
            if (!reader.readWord(high) || !reader.readWord(low)) {
                return false;
            }
            const uint64_t bits = (static_cast<uint64_t>(high) << 32) | low;
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            message.arguments.push_back(OscArgument::fromFloat(static_cast<float>(value)));
            break;
        }
        case 'h': {
            uint32_t high = 0;
            uint32_t low = 0;
            if (!reader.readWord(high) || !reader.readWord(low)) {
                return false;
            }
            message.arguments.push_back(OscArgument::fromInt(static_cast<int>(static_cast<int32_t>(low))));
            break;
        }
        case 's':
        case 'S': {
            std::string text;
            if (!reader.readString(text)) {
                return false;
            }
            message.arguments.push_back(OscArgument::fromText(text));
            break;
        }
        case 'T':
        case 'I':
            message.arguments.push_back(OscArgument::fromBool(true));
            break;
        case 'F':
            message.arguments.push_back(OscArgument::fromBool(false));
            break;
        case 'N':
            message.arguments.push_back(OscArgument());
            break;
        case 'b': {
            if (!reader.readWord(word)) {
                return false;
            }
            const size_t padded = (static_cast<size_t>(word) + 3) & ~static_cast<size_t>(3);
            if (word > size || !reader.skip(padded)) {
                return false;
            }
            break;
        }
        case 't':
            if (!reader.skip(8)) {
                return false;
            }
            break;
        case 'c':
        case 'r':
        case 'm':
            if (!reader.skip(4)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

bool parseElement(const unsigned char* data, size_t size, std::vector<OscMessage>& messages, int depth) {
    if (size >= 8 && std::memcmp(data, "#bundle", 8) == 0) {
        if (depth >= kMaxBundleDepth || size < 16) {
            return false;
        }
        Reader reader{data, size, 16};
        bool any = false;
        while (reader.remaining() >= 4) {
            uint32_t length = 0;
            reader.readWord(length);
            if (length > reader.remaining()) {
                break;
            }
            any = parseElement(data + reader.position, length, messages, depth + 1) || any;
            reader.skip(length);
        }
        return any;
    }
    OscMessage message;
    if (!parseMessage(data, size, message)) {
        return false;
    }
    messages.push_back(std::move(message));
    return true;
}

void appendWord(std::vector<unsigned char>& out, uint32_t value) {
    out.push_back(static_cast<unsigned char>(value >> 24));
    out.push_back(static_cast<unsigned char>(value >> 16));
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value));
}

void appendString(std::vector<unsigned char>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
    out.push_back(0);
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

}

OscArgument OscArgument::fromFloat(float value) {
    OscArgument argument;
    argument.type = Type::Float;
    argument.number = value;
    return argument;
}

OscArgument OscArgument::fromInt(int value) {
    OscArgument argument;
    argument.type = Type::Int;
    argument.number = static_cast<float>(value);
    return argument;
}

OscArgument OscArgument::fromBool(bool value) {
    OscArgument argument;
    argument.type = value ? Type::True : Type::False;
    argument.number = value ? 1.0f : 0.0f;
    return argument;
}

OscArgument OscArgument::fromText(const std::string& value) {
    OscArgument argument;
    argument.type = Type::Text;
    argument.text = value;
    return argument;
}

bool parseOscPacket(const unsigned char* data, size_t size, std::vector<OscMessage>& messages) {
    if (data == nullptr || size == 0) {
        return false;
    }
    return parseElement(data, size, messages, 0);
}

std::vector<unsigned char> encodeOscMessage(const OscMessage& message) {
    std::vector<unsigned char> out;
    appendString(out, message.address);
    std::string tags = ",";
    for (const OscArgument& argument : message.arguments) {
        switch (argument.type) {
        case OscArgument::Type::Float:
            tags += 'f';
            break;
        case OscArgument::Type::Int:
            tags += 'i';
            break;
        case OscArgument::Type::True:
            tags += 'T';
            break;
        case OscArgument::Type::False:
            tags += 'F';
            break;
        case OscArgument::Type::Nil:
            tags += 'N';
            break;
        case OscArgument::Type::Text:
            tags += 's';
            break;
        }
    }
    appendString(out, tags);
    for (const OscArgument& argument : message.arguments) {
        if (argument.type == OscArgument::Type::Float) {
            uint32_t word = 0;
            std::memcpy(&word, &argument.number, sizeof(word));
            appendWord(out, word);
        } else if (argument.type == OscArgument::Type::Int) {
            appendWord(out, static_cast<uint32_t>(static_cast<int32_t>(argument.number)));
        } else if (argument.type == OscArgument::Type::Text) {
            appendString(out, argument.text);
        }
    }
    return out;
}

std::vector<unsigned char> encodeOscBundle(const std::vector<OscMessage>& messages) {
    std::vector<unsigned char> out;
    appendString(out, "#bundle");
    appendWord(out, 0);
    appendWord(out, 1);
    for (const OscMessage& message : messages) {
        const std::vector<unsigned char> encoded = encodeOscMessage(message);
        appendWord(out, static_cast<uint32_t>(encoded.size()));
        out.insert(out.end(), encoded.begin(), encoded.end());
    }
    return out;
}

std::string describeOsc(const OscMessage& message) {
    std::string text = message.address;
    for (const OscArgument& argument : message.arguments) {
        text += " ";
        switch (argument.type) {
        case OscArgument::Type::Float:
        case OscArgument::Type::Int: {
            std::string number = std::to_string(argument.number);
            const size_t dot = number.find('.');
            if (dot != std::string::npos) {
                number.erase(std::min(number.size(), dot + 3));
            }
            text += number;
            break;
        }
        case OscArgument::Type::True:
            text += "true";
            break;
        case OscArgument::Type::False:
            text += "false";
            break;
        case OscArgument::Type::Nil:
            text += "nil";
            break;
        case OscArgument::Type::Text:
            text += "\"" + argument.text + "\"";
            break;
        }
    }
    return text;
}

}

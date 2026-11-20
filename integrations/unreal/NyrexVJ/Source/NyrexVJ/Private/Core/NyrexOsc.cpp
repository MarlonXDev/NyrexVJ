#include "NyrexOsc.h"

#include <cstring>

namespace NyrexCore {

namespace {

struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t position;
};

std::size_t padded(std::size_t length) {
    return (length + 4) & ~static_cast<std::size_t>(3);
}

bool readWord(Reader& reader, std::uint32_t& word) {
    if (reader.size - reader.position < 4) {
        return false;
    }
    const std::uint8_t* p = reader.data + reader.position;
    word = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    reader.position += 4;
    return true;
}

bool readString(Reader& reader, std::string& text) {
    const std::size_t remaining = reader.size - reader.position;
    const std::uint8_t* start = reader.data + reader.position;
    std::size_t length = 0;
    while (length < remaining && start[length] != 0) {
        ++length;
    }
    if (length >= remaining || length > kMaxStringBytes) {
        return false;
    }
    const std::size_t step = padded(length);
    if (step > remaining) {
        return false;
    }
    text.assign(reinterpret_cast<const char*>(start), length);
    reader.position += step;
    return true;
}

bool skip(Reader& reader, std::size_t count) {
    if (reader.size - reader.position < count) {
        return false;
    }
    reader.position += count;
    return true;
}

bool decodeMessage(const std::uint8_t* data, std::size_t size, std::vector<OscPacketMessage>& out) {
    Reader reader{data, size, 0};
    OscPacketMessage message;
    if (!readString(reader, message.address) || message.address.empty() || message.address[0] != '/') {
        return false;
    }
    std::string tags;
    if (reader.position < reader.size) {
        if (!readString(reader, tags) || tags.empty() || tags[0] != ',') {
            return false;
        }
    }
    if (tags.size() > kMaxArguments + 1) {
        return false;
    }
    for (std::size_t i = 1; i < tags.size(); ++i) {
        switch (tags[i]) {
        case 'i': {
            std::uint32_t word = 0;
            if (!readWord(reader, word)) {
                return false;
            }
            message.values.push_back(OscValue::fromInteger(static_cast<std::int32_t>(word)));
            break;
        }
        case 'f': {
            std::uint32_t word = 0;
            if (!readWord(reader, word)) {
                return false;
            }
            float number = 0.0f;
            std::memcpy(&number, &word, sizeof(number));
            message.values.push_back(OscValue::fromNumber(number));
            break;
        }
        case 's': {
            std::string text;
            if (!readString(reader, text)) {
                return false;
            }
            message.values.push_back(OscValue::fromText(text));
            break;
        }
        case 'T':
        case 'F':
        case 'N':
        case 'I':
            break;
        case 'h':
        case 't':
        case 'd':
            if (!skip(reader, 8)) {
                return false;
            }
            break;
        case 'c':
        case 'r':
        case 'm':
            if (!skip(reader, 4)) {
                return false;
            }
            break;
        case 'b': {
            std::uint32_t length = 0;
            if (!readWord(reader, length) || length > reader.size || !skip(reader, (static_cast<std::size_t>(length) + 3) & ~static_cast<std::size_t>(3))) {
                return false;
            }
            break;
        }
        default:
            return false;
        }
    }
    out.push_back(std::move(message));
    return true;
}

bool decodeElement(const std::uint8_t* data, std::size_t size, int depth, std::vector<OscPacketMessage>& out) {
    if (size < 4 || depth > kMaxBundleDepth) {
        return false;
    }
    if (data[0] != '#') {
        return decodeMessage(data, size, out);
    }
    static const char kBundle[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};
    if (size < 16 || std::memcmp(data, kBundle, 8) != 0) {
        return false;
    }
    Reader reader{data, size, 16};
    while (reader.position < reader.size) {
        std::uint32_t length = 0;
        if (!readWord(reader, length) || length < 4 || (length & 3u) != 0 || length > reader.size - reader.position) {
            return false;
        }
        if (!decodeElement(reader.data + reader.position, length, depth + 1, out) || out.size() > kMaxMessagesPerPacket) {
            return false;
        }
        reader.position += length;
    }
    return true;
}

bool plainText(const std::string& text) {
    if (text.size() > kMaxStringBytes) {
        return false;
    }
    for (char c : text) {
        if (c == 0) {
            return false;
        }
    }
    return true;
}

void appendString(std::vector<std::uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
    out.insert(out.end(), padded(text.size()) - text.size(), 0);
}

void appendWord(std::vector<std::uint8_t>& out, std::uint32_t word) {
    out.push_back(static_cast<std::uint8_t>(word >> 24));
    out.push_back(static_cast<std::uint8_t>(word >> 16));
    out.push_back(static_cast<std::uint8_t>(word >> 8));
    out.push_back(static_cast<std::uint8_t>(word));
}

}

OscValue OscValue::fromInteger(std::int32_t value) {
    OscValue result;
    result.kind = Kind::Integer;
    result.integer = value;
    return result;
}

OscValue OscValue::fromNumber(float value) {
    OscValue result;
    result.kind = Kind::Number;
    result.number = value;
    return result;
}

OscValue OscValue::fromText(const std::string& value) {
    OscValue result;
    result.kind = Kind::Text;
    result.text = value;
    return result;
}

bool decodePacket(const std::uint8_t* data, std::size_t size, std::vector<OscPacketMessage>& out) {
    if (data == nullptr || size == 0 || size > kMaxPacketBytes) {
        return false;
    }
    const std::size_t before = out.size();
    if (!decodeElement(data, size, 0, out)) {
        out.resize(before);
        return false;
    }
    return true;
}

bool encodeMessage(const OscPacketMessage& message, std::vector<std::uint8_t>& out) {
    if (message.address.empty() || message.address[0] != '/' || !plainText(message.address) || message.values.size() > kMaxArguments) {
        return false;
    }
    std::string tags = ",";
    for (const OscValue& value : message.values) {
        switch (value.kind) {
        case OscValue::Kind::Integer:
            tags += 'i';
            break;
        case OscValue::Kind::Number:
            tags += 'f';
            break;
        case OscValue::Kind::Text:
            if (!plainText(value.text)) {
                return false;
            }
            tags += 's';
            break;
        }
    }
    std::vector<std::uint8_t> bytes;
    appendString(bytes, message.address);
    appendString(bytes, tags);
    for (const OscValue& value : message.values) {
        switch (value.kind) {
        case OscValue::Kind::Integer:
            appendWord(bytes, static_cast<std::uint32_t>(value.integer));
            break;
        case OscValue::Kind::Number: {
            std::uint32_t word = 0;
            std::memcpy(&word, &value.number, sizeof(word));
            appendWord(bytes, word);
            break;
        }
        case OscValue::Kind::Text:
            appendString(bytes, value.text);
            break;
        }
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
    return true;
}

}

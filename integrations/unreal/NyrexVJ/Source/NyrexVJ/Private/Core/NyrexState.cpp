#include "NyrexState.h"

#include <cmath>
#include <cstring>

namespace NyrexCore {

namespace {

const char kOutPrefix[] = "/nyrex/out/";
const char kInPrefix[] = "/nyrex/";
const char kParamPrefix[] = "/nyrex/out/param/";

bool startsWith(const std::string& text, const char* prefix) {
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

bool numberOf(const OscValue& value, float& out) {
    if (value.kind == OscValue::Kind::Number) {
        out = value.number;
        return true;
    }
    if (value.kind == OscValue::Kind::Integer) {
        out = static_cast<float>(value.integer);
        return true;
    }
    return false;
}

bool integerOf(const OscValue& value, std::int32_t& out) {
    if (value.kind == OscValue::Kind::Integer) {
        out = value.integer;
        return true;
    }
    if (value.kind == OscValue::Kind::Number && std::isfinite(value.number) && value.number > -2147483648.0f && value.number < 2147483648.0f) {
        out = static_cast<std::int32_t>(value.number);
        return true;
    }
    return false;
}

bool numbers(const OscPacketMessage& message, std::size_t count, float* out) {
    if (message.values.size() < count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!numberOf(message.values[i], out[i])) {
            return false;
        }
    }
    return true;
}

void applyHello(NyrexState& state, const OscPacketMessage& message, NyrexEvents& events) {
    if (message.values.size() < 2 || message.values[0].kind != OscValue::Kind::Text || message.values[0].text != "NyrexVJ") {
        return;
    }
    std::int32_t schema = 0;
    if (!integerOf(message.values[1], schema)) {
        return;
    }
    state.helloSeen = true;
    state.appName = message.values[0].text;
    state.schema = schema;
    state.schemaSupported = schema == kSupportedSchema;
    if (message.values.size() > 2 && message.values[2].kind == OscValue::Kind::Text && message.values[2].text.size() <= kMaxStringBytes) {
        state.appVersion = message.values[2].text;
    }
    std::int32_t port = 0;
    if (message.values.size() > 3 && integerOf(message.values[3], port) && port >= 0 && port <= 65535) {
        state.inputPort = port;
    }
    events.hello = true;
}

void applyData(NyrexState& state, const std::string& name, const OscPacketMessage& message, NyrexEvents& events) {
    if (name == "audio") {
        float v[5];
        if (numbers(message, 5, v)) {
            state.bass = clampUnit(v[0]);
            state.mid = clampUnit(v[1]);
            state.high = clampUnit(v[2]);
            state.level = clampUnit(v[3]);
            state.kick = clampUnit(v[4]);
        }
    } else if (name == "spectrum") {
        float v[kSpectrumBands];
        if (numbers(message, kSpectrumBands, v)) {
            for (int i = 0; i < kSpectrumBands; ++i) {
                state.spectrum[static_cast<std::size_t>(i)] = clampUnit(v[i]);
            }
        }
    } else if (name == "clock") {
        float bpm = 0.0f;
        float phase = 0.0f;
        std::int32_t beat = 0;
        if (message.values.size() >= 3 && numberOf(message.values[0], bpm) && integerOf(message.values[1], beat) && numberOf(message.values[2], phase) && std::isfinite(bpm)) {
            state.bpm = bpm < 0.0f ? 0.0f : (bpm > 1000.0f ? 1000.0f : bpm);
            state.beat = beat;
            state.beatPhase = clampUnit(phase);
        }
    } else if (name == "beat") {
        std::int32_t beat = 0;
        std::int32_t inBar = 0;
        if (message.values.size() >= 2 && integerOf(message.values[0], beat) && integerOf(message.values[1], inBar)) {
            state.beat = beat;
            state.beatInBar = inBar;
            if (events.beats.size() < kMaxEventsPerApply) {
                events.beats.emplace_back(beat, inBar);
            }
        }
    } else if (name == "onset") {
        std::int32_t count = 0;
        if (!message.values.empty() && integerOf(message.values[0], count)) {
            state.onsetCount = count;
            if (events.onsets.size() < kMaxEventsPerApply) {
                events.onsets.push_back(count);
            }
        }
    }
}

}

float clampUnit(float value) {
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

bool validParameterName(const std::string& name) {
    if (name.empty() || name.size() > kMaxParameterNameLength) {
        return false;
    }
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool validControlAddress(const std::string& address) {
    if (address.size() <= std::strlen(kInPrefix) || address.size() > kMaxControlAddressLength || !startsWith(address, kInPrefix) || startsWith(address, kOutPrefix)) {
        return false;
    }
    for (char c : address) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/';
        if (!ok) {
            return false;
        }
    }
    return address.back() != '/' && address.find("//") == std::string::npos;
}

const char* const kMasterLevelAddress = "/nyrex/master/level";
const char* const kTapAddress = "/nyrex/master/tap";

std::string columnAddress(int column) {
    return column >= 1 && column <= 8 ? "/nyrex/column/" + std::to_string(column) : std::string();
}

std::string deckAddress(int deck) {
    return deck >= 1 && deck <= 16 ? "/nyrex/deck/" + std::to_string(deck) : std::string();
}

std::string layerOpacityAddress(int layer) {
    return layer >= 1 && layer <= 4 ? "/nyrex/layer/" + std::to_string(layer) + "/opacity" : std::string();
}

void applyMessage(NyrexState& state, const OscPacketMessage& message, NyrexEvents& events) {
    if (!startsWith(message.address, kOutPrefix)) {
        return;
    }
    const std::string name = message.address.substr(std::strlen(kOutPrefix));
    if (name == "hello") {
        applyHello(state, message, events);
        return;
    }
    if (name == "bye") {
        events.bye = true;
        return;
    }
    if (!state.schemaSupported) {
        return;
    }
    if (startsWith(message.address, kParamPrefix)) {
        const std::string parameter = message.address.substr(std::strlen(kParamPrefix));
        float value = 0.0f;
        if (validParameterName(parameter) && !message.values.empty() && numberOf(message.values[0], value)) {
            auto it = state.parameters.find(parameter);
            if (it != state.parameters.end()) {
                it->second = clampUnit(value);
            } else if (state.parameters.size() < kMaxParameters) {
                state.parameters.emplace(parameter, clampUnit(value));
            }
        }
        return;
    }
    applyData(state, name, message, events);
}

void applyPacket(NyrexState& state, const std::uint8_t* data, std::size_t size, NyrexEvents& events) {
    std::vector<OscPacketMessage> messages;
    if (!decodePacket(data, size, messages)) {
        return;
    }
    for (const OscPacketMessage& message : messages) {
        applyMessage(state, message, events);
    }
}

}

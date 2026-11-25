#pragma once

#include "control/control_targets.hpp"

#include <string>
#include <vector>

namespace nvj {

struct MidiMessage {
    int status = 0;
    int data1 = 0;
    int data2 = 0;
    double time = 0.0;
};

inline bool isSystemMidi(const MidiMessage& message) {
    return message.status >= 0xF0;
}

enum class MidiKind { ControlChange, Note };

struct MidiEvent {
    bool valid = false;
    MidiKind kind = MidiKind::ControlChange;
    int channel = 1;
    int number = 0;
    float value = 0.0f;
};

struct MidiBinding {
    MidiKind kind = MidiKind::ControlChange;
    int channel = 0;
    int number = 0;
    std::string target;
    float low = 0.0f;
    float high = 1.0f;
};

MidiEvent decodeMidi(const MidiMessage& message);
std::string describeMidi(const MidiMessage& message);

bool applyMidi(const std::vector<MidiBinding>& bindings, const MidiEvent& event, ControlRouter& router);

constexpr size_t kMaxProfileBytes = 256u * 1024u;
constexpr size_t kMaxProfileBindings = 1024;

std::string sanitizeProfileName(const std::string& name);
std::string profileToJson(const std::string& name, const std::vector<MidiBinding>& bindings);
bool profileFromJson(const std::string& text, std::vector<MidiBinding>& bindings, std::string& error);

std::string bindingsToJson(const std::vector<MidiBinding>& bindings);
bool bindingsFromJson(const std::string& text, std::vector<MidiBinding>& bindings);

}

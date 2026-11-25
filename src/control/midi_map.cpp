#include "control/midi_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace nvj {

MidiEvent decodeMidi(const MidiMessage& message) {
    MidiEvent event;
    const int type = message.status & 0xF0;
    event.channel = (message.status & 0x0F) + 1;
    event.number = message.data1 & 0x7F;
    if (type == 0xB0) {
        event.valid = true;
        event.kind = MidiKind::ControlChange;
        event.value = static_cast<float>(message.data2 & 0x7F) / 127.0f;
    } else if (type == 0x90) {
        event.valid = true;
        event.kind = MidiKind::Note;
        event.value = (message.data2 & 0x7F) > 0 ? 1.0f : 0.0f;
    } else if (type == 0x80) {
        event.valid = true;
        event.kind = MidiKind::Note;
        event.value = 0.0f;
    }
    return event;
}

std::string describeMidi(const MidiMessage& message) {
    const MidiEvent event = decodeMidi(message);
    if (!event.valid) {
        return "";
    }
    const std::string kind = event.kind == MidiKind::ControlChange ? "CC" : "Note";
    return kind + " " + std::to_string(event.number) + "  ch " + std::to_string(event.channel) + "  " + std::to_string(message.data2 & 0x7F);
}

bool applyMidi(const std::vector<MidiBinding>& bindings, const MidiEvent& event, ControlRouter& router) {
    if (!event.valid) {
        return false;
    }
    bool matched = false;
    for (const MidiBinding& binding : bindings) {
        if (binding.kind != event.kind || binding.number != event.number) {
            continue;
        }
        if (binding.channel != 0 && binding.channel != event.channel) {
            continue;
        }
        const int target = controlTargetIndex(binding.target);
        if (target < 0) {
            continue;
        }
        const float value = binding.low + (binding.high - binding.low) * event.value;
        router.set(target, value, true);
        matched = true;
    }
    return matched;
}

std::string bindingsToJson(const std::vector<MidiBinding>& bindings) {
    nlohmann::json doc;
    doc["version"] = 1;
    nlohmann::json list = nlohmann::json::array();
    for (const MidiBinding& binding : bindings) {
        nlohmann::json entry;
        entry["type"] = binding.kind == MidiKind::ControlChange ? "cc" : "note";
        entry["channel"] = binding.channel;
        entry["number"] = binding.number;
        entry["target"] = binding.target;
        entry["low"] = binding.low;
        entry["high"] = binding.high;
        list.push_back(entry);
    }
    doc["bindings"] = list;
    return doc.dump(2);
}

bool bindingsFromJson(const std::string& text, std::vector<MidiBinding>& bindings) {
    nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return false;
    }
    auto list = doc.find("bindings");
    if (list == doc.end() || !list->is_array()) {
        return false;
    }
    std::vector<MidiBinding> result;
    for (const auto& entry : *list) {
        if (!entry.is_object()) {
            continue;
        }
        auto type = entry.find("type");
        auto channel = entry.find("channel");
        auto number = entry.find("number");
        auto target = entry.find("target");
        if (type == entry.end() || !type->is_string() || channel == entry.end() || !channel->is_number_integer() || number == entry.end() || !number->is_number_integer() || target == entry.end() || !target->is_string()) {
            continue;
        }
        MidiBinding binding;
        const std::string kind = type->get<std::string>();
        if (kind == "cc") {
            binding.kind = MidiKind::ControlChange;
        } else if (kind == "note") {
            binding.kind = MidiKind::Note;
        } else {
            continue;
        }
        binding.channel = channel->get<int>();
        binding.number = number->get<int>();
        binding.target = target->get<std::string>();
        if (binding.channel < 0 || binding.channel > 16 || binding.number < 0 || binding.number > 127 || controlTargetIndex(binding.target) < 0) {
            continue;
        }
        auto low = entry.find("low");
        auto high = entry.find("high");
        if (low != entry.end() && low->is_number()) {
            binding.low = std::clamp(low->get<float>(), 0.0f, 1.0f);
        }
        if (high != entry.end() && high->is_number()) {
            binding.high = std::clamp(high->get<float>(), 0.0f, 1.0f);
        }
        result.push_back(binding);
    }
    bindings = result;
    return true;
}

std::string sanitizeProfileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool allowed = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80 || c == '_' || c == '-' || c == '.' || c == ' ';
        if (allowed) {
            out.push_back(c);
        }
    }
    size_t begin = 0;
    while (begin < out.size() && (out[begin] == ' ' || out[begin] == '.')) {
        ++begin;
    }
    size_t end = out.size();
    while (end > begin && (out[end - 1] == ' ' || out[end - 1] == '.')) {
        --end;
    }
    out = out.substr(begin, end - begin);
    if (out.size() > 48) {
        size_t cut = 48;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
    }
    std::string upper = out;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const size_t dot = upper.find('.');
    const std::string stem = dot == std::string::npos ? upper : upper.substr(0, dot);
    static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* word : reserved) {
        if (stem == word) {
            return std::string();
        }
    }
    return out;
}

std::string profileToJson(const std::string& name, const std::vector<MidiBinding>& bindings) {
    nlohmann::json doc = nlohmann::json::parse(bindingsToJson(bindings));
    doc["format"] = "nyrexvj-midi-profile";
    doc["name"] = name;
    return doc.dump(2);
}

bool profileFromJson(const std::string& text, std::vector<MidiBinding>& bindings, std::string& error) {
    if (text.size() > kMaxProfileBytes) {
        error = "The profile file is too large";
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string()) != "nyrexvj-midi-profile") {
        error = "This is not a controller profile";
        return false;
    }
    const int version = doc.value("version", 0);
    if (version < 1) {
        error = "The profile has no valid version";
        return false;
    }
    if (version > 1) {
        error = "The profile was written by a newer version of NyrexVJ";
        return false;
    }
    std::vector<MidiBinding> parsed;
    if (!bindingsFromJson(text, parsed)) {
        error = "The profile has no list of mappings";
        return false;
    }
    if (parsed.size() > kMaxProfileBindings) {
        error = "The profile has too many mappings";
        return false;
    }
    bindings = parsed;
    return true;
}

}

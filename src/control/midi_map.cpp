#include "control/midi_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

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

}

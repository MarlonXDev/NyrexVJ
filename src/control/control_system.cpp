#include "control/control_system.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nvj {

namespace {

constexpr const char* kOscPrefix = "/nyrex/";
constexpr int kMaxPacketsPerUpdate = 512;
constexpr double kSaveDelay = 0.5;

std::filesystem::path bindingsPath() {
    return std::filesystem::path(userDataDir()) / "controls.json";
}

}

ControlSystem::ControlSystem(Composition& comp, ControlHost& host) : router_(comp, host), midi_(createMidiInput()) {}

ControlSystem::~ControlSystem() {
    if (dirty_) {
        saveBindings();
    }
}

void ControlSystem::refreshMidiDevices() {
    midiDevices_ = midi_->listDevices();
}

void ControlSystem::configure(const ControlConfig& config) {
    const bool socketWanted = config.oscEnabled;
    if (!configured_ || socketWanted != socketWantsOpen_ || config.oscPort != socketPort_ || config.oscAllowRemote != socketRemote_) {
        socket_.close();
        oscError_.clear();
        if (socketWanted) {
            socket_.open(config.oscPort, config.oscAllowRemote, oscError_);
        }
        socketWantsOpen_ = socketWanted;
        socketPort_ = config.oscPort;
        socketRemote_ = config.oscAllowRemote;
    }
    if (!configured_ || config.midiDevice != appliedMidiDevice_) {
        midi_->stop();
        midiError_.clear();
        if (!config.midiDevice.empty()) {
            midi_->start(config.midiDevice, midiError_);
        }
        appliedMidiDevice_ = config.midiDevice;
        refreshMidiDevices();
    }
    config_ = config;
    configured_ = true;
}

bool ControlSystem::handleOsc(const OscMessage& message) {
    ++oscCount_;
    lastOsc_ = describeOsc(message);
    const std::string prefix = kOscPrefix;
    if (message.address.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const int target = controlTargetIndex(message.address.substr(prefix.size()));
    if (target < 0) {
        return false;
    }
    const ControlTarget& info = controlTargets()[static_cast<size_t>(target)];
    const OscArgument* value = nullptr;
    for (const OscArgument& argument : message.arguments) {
        if (argument.type != OscArgument::Type::Text && argument.type != OscArgument::Type::Nil) {
            value = &argument;
            break;
        }
    }
    if (value == nullptr) {
        if (info.kind == ControlKind::Trigger) {
            return router_.set(target, 1.0f, false);
        }
        if (info.kind == ControlKind::Toggle) {
            return router_.toggle(target);
        }
        return false;
    }
    return router_.set(target, value->number, false);
}

void ControlSystem::injectMidi(const MidiMessage& message) {
    injected_.push_back(message);
}

void ControlSystem::startLearn(int bindingIndex, const std::string& target) {
    learning_ = true;
    learnIndex_ = bindingIndex;
    learnTarget_ = target;
}

void ControlSystem::cancelLearn() {
    learning_ = false;
    learnIndex_ = -1;
    learnTarget_.clear();
}

void ControlSystem::finishLearn(const MidiEvent& event) {
    if (learnIndex_ >= 0 && learnIndex_ < static_cast<int>(bindings_.size())) {
        MidiBinding& binding = bindings_[static_cast<size_t>(learnIndex_)];
        binding.kind = event.kind;
        binding.channel = event.channel;
        binding.number = event.number;
    } else {
        bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(), [&](const MidiBinding& existing) {
            return existing.kind == event.kind && existing.number == event.number && existing.channel == event.channel && existing.target == learnTarget_;
        }), bindings_.end());
        MidiBinding binding;
        binding.kind = event.kind;
        binding.channel = event.channel;
        binding.number = event.number;
        binding.target = learnTarget_;
        bindings_.push_back(binding);
    }
    cancelLearn();
    markDirty();
}

void ControlSystem::handleMidi(const MidiMessage& message) {
    const MidiEvent event = decodeMidi(message);
    if (!event.valid) {
        return;
    }
    ++midiCount_;
    lastMidi_ = describeMidi(message);
    if (learning_) {
        if (event.kind == MidiKind::ControlChange || event.value > 0.0f) {
            finishLearn(event);
        }
        return;
    }
    applyMidi(bindings_, event, router_);
}

void ControlSystem::update(double dt) {
    std::vector<MidiMessage> messages;
    messages.swap(injected_);
    midi_->drain(messages);
    for (const MidiMessage& message : messages) {
        handleMidi(message);
    }

    if (socket_.isOpen()) {
        unsigned char buffer[2048];
        std::vector<OscMessage> parsed;
        for (int packets = 0; packets < kMaxPacketsPerUpdate; ++packets) {
            const int received = socket_.receive(buffer, static_cast<int>(sizeof(buffer)));
            if (received <= 0) {
                break;
            }
            parsed.clear();
            if (parseOscPacket(buffer, static_cast<size_t>(received), parsed)) {
                for (const OscMessage& message : parsed) {
                    handleOsc(message);
                }
            }
        }
    }

    if (dirty_) {
        dirtyFor_ += dt;
        if (dirtyFor_ >= kSaveDelay) {
            saveBindings();
        }
    }
}

bool ControlSystem::loadBindings() {
    std::ifstream file(bindingsPath(), std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return bindingsFromJson(buffer.str(), bindings_);
}

bool ControlSystem::saveBindings() {
    dirty_ = false;
    dirtyFor_ = 0.0;
    std::ofstream file(bindingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << bindingsToJson(bindings_);
    return static_cast<bool>(file);
}

}

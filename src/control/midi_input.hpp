#pragma once

#include "control/midi_map.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

class MidiInput {
public:
    virtual ~MidiInput() = default;
    virtual std::vector<std::string> listDevices() = 0;
    virtual bool start(const std::string& name, std::string& error) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual void drain(std::vector<MidiMessage>& messages) = 0;
};

std::unique_ptr<MidiInput> createMidiInput();

}

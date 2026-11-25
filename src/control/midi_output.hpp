#pragma once

#include "control/midi_map.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nvj {

class MidiOutput {
public:
    virtual ~MidiOutput() = default;
    virtual std::vector<std::string> listDevices() = 0;
    virtual bool open(const std::string& name, std::string& error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool send(const MidiMessage& message) = 0;
    virtual bool supported() const { return true; }
};

std::unique_ptr<MidiOutput> createMidiOutput();

}

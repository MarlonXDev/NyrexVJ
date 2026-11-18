#include "control/midi_input.hpp"

namespace nvj {

namespace {

class NullMidiInput final : public MidiInput {
public:
    std::vector<std::string> listDevices() override { return {}; }
    bool start(const std::string&, std::string& error) override {
        error = "MIDI input is only available on Windows";
        return false;
    }
    void stop() override {}
    bool running() const override { return false; }
    void drain(std::vector<MidiMessage>&) override {}
};

}

std::unique_ptr<MidiInput> createMidiInput() {
    return std::make_unique<NullMidiInput>();
}

}

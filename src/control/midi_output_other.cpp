#include "control/midi_output.hpp"

namespace nvj {

namespace {

class NoMidiOutput final : public MidiOutput {
public:
    std::vector<std::string> listDevices() override { return {}; }

    bool open(const std::string&, std::string& error) override {
        error = "MIDI feedback is only available on Windows in this version";
        return false;
    }

    void close() override {}
    bool isOpen() const override { return false; }
    bool send(const MidiMessage&) override { return false; }
    bool supported() const override { return false; }
};

}

std::unique_ptr<MidiOutput> createMidiOutput() {
    return std::make_unique<NoMidiOutput>();
}

}

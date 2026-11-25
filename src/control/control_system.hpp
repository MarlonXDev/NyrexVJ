#pragma once

#include "control/control_targets.hpp"
#include "control/midi_feedback.hpp"
#include "control/midi_input.hpp"
#include "control/midi_output.hpp"
#include "control/midi_map.hpp"
#include "control/osc.hpp"
#include "control/udp_socket.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct ControlConfig {
    bool oscEnabled = false;
    int oscPort = 9010;
    bool oscAllowRemote = false;
    std::string midiDevice;
    bool midiFeedback = false;
    std::string midiOutDevice;
};

class ControlSystem {
public:
    ControlSystem(Composition& comp, ControlHost& host);
    ~ControlSystem();

    void configure(const ControlConfig& config);
    void update(double dt);
    void injectMidi(const MidiMessage& message);
    void takeSyncMessages(std::vector<MidiMessage>& out);
    bool fireTarget(const std::string& id, float value);
    void refreshMidiDevices();
    void refreshMidiOutDevices();
    void useMidiOutput(std::unique_ptr<MidiOutput> output);

    bool handleOsc(const OscMessage& message);
    const ControlRouter& router() const { return router_; }

    bool oscListening() const { return socket_.isOpen(); }
    int oscPort() const { return config_.oscPort; }
    const std::string& oscError() const { return oscError_; }
    unsigned long long oscCount() const { return oscCount_; }
    const std::string& lastOsc() const { return lastOsc_; }

    const std::vector<std::string>& midiDevices() const { return midiDevices_; }
    bool midiRunning() const { return midi_->running(); }
    const std::string& midiError() const { return midiError_; }
    unsigned long long midiCount() const { return midiCount_; }
    const std::string& lastMidi() const { return lastMidi_; }
    const std::vector<std::string>& midiOutDevices() const { return midiOutDevices_; }
    bool midiOutOpen() const { return output_->isOpen(); }
    bool midiOutSupported() const { return output_->supported(); }
    const std::string& midiOutError() const { return midiOutError_; }
    unsigned long long feedbackSent() const { return feedbackSent_; }

    static std::filesystem::path profileDirectory();
    std::vector<std::string> listProfiles() const;
    bool saveProfile(const std::string& name, std::string& error);
    bool loadProfile(const std::string& name, std::string& error);

    std::vector<MidiBinding>& bindings() { return bindings_; }
    const std::vector<MidiBinding>& bindings() const { return bindings_; }
    void markDirty() { dirty_ = true; dirtyFor_ = 0.0; }
    bool loadBindings();
    bool saveBindings();

    void startLearn(int bindingIndex, const std::string& target);
    void cancelLearn();
    bool learning() const { return learning_; }
    const std::string& learnTarget() const { return learnTarget_; }

private:
    void handleMidi(const MidiMessage& message);
    void finishLearn(const MidiEvent& event);

    ControlRouter router_;
    ControlConfig config_;
    UdpSocket socket_;
    std::unique_ptr<MidiInput> midi_;
    std::unique_ptr<MidiOutput> output_;
    MidiFeedback feedback_;
    std::vector<std::string> midiOutDevices_;
    std::string midiOutError_;
    std::string appliedOutDevice_;
    double quietFor_ = 1.0;
    unsigned long long feedbackSent_ = 0;
    std::vector<std::string> midiDevices_;
    std::vector<MidiBinding> bindings_;
    std::vector<MidiMessage> injected_;
    std::vector<MidiMessage> sync_;
    std::string oscError_;
    std::string midiError_;
    std::string lastOsc_;
    std::string lastMidi_;
    std::string learnTarget_;
    std::string appliedMidiDevice_;
    int learnIndex_ = -1;
    bool learning_ = false;
    bool configured_ = false;
    bool dirty_ = false;
    double dirtyFor_ = 0.0;
    unsigned long long oscCount_ = 0;
    unsigned long long midiCount_ = 0;
    bool socketWantsOpen_ = false;
    int socketPort_ = 0;
    bool socketRemote_ = false;
};

}

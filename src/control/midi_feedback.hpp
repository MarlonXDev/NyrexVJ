#pragma once

#include "control/control_targets.hpp"
#include "control/midi_map.hpp"

#include <map>
#include <vector>

namespace nvj {

constexpr size_t kMaxFeedbackPerUpdate = 64;

class MidiFeedback {
public:
    void reset();
    void compute(const std::vector<MidiBinding>& bindings, const ControlRouter& router, std::vector<MidiMessage>& out, bool allowContinuous = true);
    size_t tracked() const { return last_.size(); }

private:
    static int keyFor(const MidiBinding& binding);

    std::map<int, int> last_;
};

}

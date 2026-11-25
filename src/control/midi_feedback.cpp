#include "control/midi_feedback.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace nvj {

int MidiFeedback::keyFor(const MidiBinding& binding) {
    const int channel = binding.channel > 0 ? binding.channel : 1;
    return (binding.kind == MidiKind::Note ? 0x10000 : 0) | (channel << 8) | binding.number;
}

void MidiFeedback::reset() {
    last_.clear();
}

void MidiFeedback::compute(const std::vector<MidiBinding>& bindings, const ControlRouter& router, std::vector<MidiMessage>& out, bool allowContinuous) {
    const auto& targets = controlTargets();
    std::set<int> seen;
    size_t produced = 0;
    for (const MidiBinding& binding : bindings) {
        const int index = controlTargetIndex(binding.target);
        if (index < 0) {
            continue;
        }
        const ControlTarget& target = targets[static_cast<size_t>(index)];
        const bool switchLike = target.kind != ControlKind::Continuous;
        if (binding.kind == MidiKind::Note && !switchLike) {
            continue;
        }
        const int key = keyFor(binding);
        if (!seen.insert(key).second) {
            continue;
        }
        if (!switchLike && !allowContinuous) {
            continue;
        }
        int data = 0;
        if (switchLike) {
            data = router.lit(index) ? 127 : 0;
        } else {
            const float span = binding.high - binding.low;
            if (std::fabs(span) < 1e-4f) {
                continue;
            }
            const float position = std::clamp((router.value(index) - binding.low) / span, 0.0f, 1.0f);
            data = static_cast<int>(std::lround(position * 127.0f));
        }
        const auto found = last_.find(key);
        if (found != last_.end() && found->second == data) {
            continue;
        }
        if (produced >= kMaxFeedbackPerUpdate) {
            continue;
        }
        const int channel = (binding.channel > 0 ? binding.channel : 1) - 1;
        MidiMessage message;
        message.status = (binding.kind == MidiKind::Note ? 0x90 : 0xB0) | channel;
        message.data1 = binding.number;
        message.data2 = data;
        out.push_back(message);
        last_[key] = data;
        ++produced;
    }
    for (auto it = last_.begin(); it != last_.end();) {
        if (seen.count(it->first) == 0) {
            it = last_.erase(it);
        } else {
            ++it;
        }
    }
}

}

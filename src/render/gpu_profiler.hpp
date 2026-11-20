#pragma once

#include "core/render_policy.hpp"

#include <glad/gl.h>

#include <array>
#include <string>
#include <vector>

namespace nvj {

class GpuProfiler {
public:
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    void beginFrame();
    void mark(PassKind kind, int deck, int layer, int slot, const std::string& name);
    void endFrame();
    void flushNow();
    void destroy();
    const ProfileTable& table() const { return table_; }
    double lastFrameMs() const { return lastFrameMs_; }
    const std::vector<ProfileSample>& lastSamples() const { return lastSamples_; }

private:
    struct Segment {
        GLuint query = 0;
        ProfileSample sample;
    };

    struct Slot {
        std::vector<Segment> segments;
        size_t used = 0;
        bool pending = false;
    };

    static constexpr int kSlots = 4;

    void endSegment();
    void collect(Slot& slot, bool wait);

    std::array<Slot, kSlots> slots_;
    int current_ = 0;
    bool enabled_ = false;
    bool inFrame_ = false;
    bool active_ = false;
    ProfileTable table_;
    std::vector<ProfileSample> lastSamples_;
    double lastFrameMs_ = 0.0;
};

}

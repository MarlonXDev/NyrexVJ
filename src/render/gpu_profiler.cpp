#include "render/gpu_profiler.hpp"

namespace nvj {

void GpuProfiler::setEnabled(bool enabled) {
    if (enabled == enabled_) {
        return;
    }
    if (!enabled) {
        destroy();
    }
    enabled_ = enabled;
}

void GpuProfiler::endSegment() {
    if (active_) {
        glEndQuery(GL_TIME_ELAPSED);
        active_ = false;
    }
}

void GpuProfiler::collect(Slot& slot, bool wait) {
    if (!slot.pending) {
        return;
    }
    if (!wait && slot.used > 0) {
        GLint available = 0;
        glGetQueryObjectiv(slot.segments[slot.used - 1].query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            slot.pending = false;
            return;
        }
    }
    table_.beginFrame();
    lastSamples_.clear();
    double total = 0.0;
    for (size_t i = 0; i < slot.used; ++i) {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(slot.segments[i].query, GL_QUERY_RESULT, &nanoseconds);
        ProfileSample sample = slot.segments[i].sample;
        sample.ms = static_cast<double>(nanoseconds) / 1.0e6;
        total += sample.ms;
        table_.add(sample);
        lastSamples_.push_back(sample);
    }
    table_.endFrame();
    lastFrameMs_ = total;
    slot.pending = false;
}

void GpuProfiler::beginFrame() {
    if (!enabled_) {
        return;
    }
    current_ = (current_ + 1) % kSlots;
    Slot& slot = slots_[static_cast<size_t>(current_)];
    collect(slot, false);
    slot.used = 0;
    slot.pending = false;
    inFrame_ = true;
    active_ = false;
}

void GpuProfiler::mark(PassKind kind, int deck, int layer, int slotIndex, const std::string& name) {
    if (!enabled_ || !inFrame_) {
        return;
    }
    endSegment();
    Slot& slot = slots_[static_cast<size_t>(current_)];
    if (slot.used >= slot.segments.size()) {
        Segment segment;
        glGenQueries(1, &segment.query);
        slot.segments.push_back(segment);
    }
    Segment& segment = slot.segments[slot.used++];
    segment.sample = ProfileSample{};
    segment.sample.kind = kind;
    segment.sample.deck = deck;
    segment.sample.layer = layer;
    segment.sample.slot = slotIndex;
    segment.sample.name = name;
    glBeginQuery(GL_TIME_ELAPSED, segment.query);
    active_ = true;
}

void GpuProfiler::endFrame() {
    if (!enabled_ || !inFrame_) {
        return;
    }
    endSegment();
    slots_[static_cast<size_t>(current_)].pending = true;
    inFrame_ = false;
}

void GpuProfiler::flushNow() {
    if (!enabled_) {
        return;
    }
    Slot& slot = slots_[static_cast<size_t>(current_)];
    if (slot.pending) {
        collect(slot, true);
    }
}

void GpuProfiler::destroy() {
    if (active_) {
        glEndQuery(GL_TIME_ELAPSED);
        active_ = false;
    }
    for (Slot& slot : slots_) {
        for (Segment& segment : slot.segments) {
            if (segment.query != 0) {
                glDeleteQueries(1, &segment.query);
            }
        }
        slot.segments.clear();
        slot.used = 0;
        slot.pending = false;
    }
    table_.clear();
    lastSamples_.clear();
    lastFrameMs_ = 0.0;
    inFrame_ = false;
}

}

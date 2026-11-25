#pragma once

#include "control/control_system.hpp"
#include "control/sync_system.hpp"
#include "core/show_timeline.hpp"

namespace nvj {

class ControlTimelineHost final : public TimelineHost {
public:
    explicit ControlTimelineHost(ControlSystem& control) : control_(control) {}

    LaneKind laneKind(const std::string& target) const override;
    bool read(const std::string& target, float& value) const override;
    bool write(const std::string& target, float value) override;
    void fire(const Cue& cue) override;

private:
    ControlSystem& control_;
};

TimelineInputs timelineInputs(const SyncSystem& sync, double clock, bool running);

class TimelineSystem {
public:
    TimelineSystem();
    ~TimelineSystem();

    void configure(const TimelineConfig& config);
    void update(double dt, const TimelineInputs& inputs, SyncSystem& sync, ControlSystem& control);

    ShowTimeline& timeline() { return timeline_; }
    const ShowTimeline& timeline() const { return timeline_; }
    TimelineRunner& runner() { return runner_; }
    const TimelineRunner& runner() const { return runner_; }
    const TimelineHistory& history() const { return history_; }

    TimelineDoc capture(const SyncSystem& sync) const;
    void apply(const TimelineDoc& doc, SyncSystem& sync);
    bool commit(const TimelineDoc& before, const SyncSystem& sync);
    bool undo(SyncSystem& sync);
    bool redo(SyncSystem& sync);

    bool load();
    bool save();
    void markDirty() { dirty_ = true; dirtyFor_ = 0.0; }
    bool loaded() const { return loaded_; }

private:
    ShowTimeline timeline_;
    TimelineRunner runner_;
    TimelineHistory history_;
    bool dirty_ = false;
    double dirtyFor_ = 0.0;
    bool loaded_ = false;
};

}

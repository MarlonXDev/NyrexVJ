#include "control/timeline_system.hpp"

#include "core/log.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace nvj {

namespace {

constexpr double kTimelineSaveDelay = 0.5;

std::filesystem::path timelinePath() {
    return std::filesystem::path(userDataDir()) / "timeline.json";
}

}

LaneKind ControlTimelineHost::laneKind(const std::string& target) const {
    const int index = controlTargetIndex(target);
    if (index < 0) {
        return LaneKind::Refused;
    }
    const ControlTarget& entry = controlTargets()[static_cast<size_t>(index)];
    if (entry.kind == ControlKind::Trigger || entry.item == ControlItem::Bpm) {
        return LaneKind::Refused;
    }
    return entry.kind == ControlKind::Toggle ? LaneKind::Toggle : LaneKind::Continuous;
}

bool ControlTimelineHost::read(const std::string& target, float& value) const {
    const int index = controlTargetIndex(target);
    if (index < 0) {
        return false;
    }
    value = control_.router().value(index);
    return true;
}

bool ControlTimelineHost::write(const std::string& target, float value) {
    return control_.fireTarget(target, value);
}

void ControlTimelineHost::fire(const Cue& cue) {
    control_.fireTarget(cue.target, cue.value);
}

TimelineInputs timelineInputs(const SyncSystem& sync, double clock, bool running) {
    TimelineInputs inputs;
    inputs.timecodeValid = sync.timecodeActive();
    inputs.timecodeSeconds = sync.timecodeSeconds();
    inputs.clock = clock;
    inputs.running = running;
    return inputs;
}

TimelineSystem::TimelineSystem() = default;

TimelineSystem::~TimelineSystem() {
    if (dirty_) {
        save();
    }
}

void TimelineSystem::configure(const TimelineConfig& config) {
    runner_.configure(config);
}

void TimelineSystem::update(double dt, const TimelineInputs& inputs, SyncSystem& sync, ControlSystem& control) {
    if (runner_.config().enabled) {
        ControlTimelineHost host(control);
        runner_.update(inputs, timeline_, sync.cues(), host);
    }
    if (dirty_) {
        dirtyFor_ += dt;
        if (dirtyFor_ >= kTimelineSaveDelay) {
            save();
        }
    }
}

TimelineDoc TimelineSystem::capture(const SyncSystem& sync) const {
    TimelineDoc doc;
    doc.timeline = timeline_;
    doc.cues = sync.cues();
    return doc;
}

void TimelineSystem::apply(const TimelineDoc& doc, SyncSystem& sync) {
    timeline_ = doc.timeline;
    sync.cues() = doc.cues;
    sync.markCuesDirty();
    markDirty();
    runner_.releaseAll();
}

bool TimelineSystem::commit(const TimelineDoc& before, const SyncSystem& sync) {
    markDirty();
    return history_.commit(before, capture(sync));
}

bool TimelineSystem::undo(SyncSystem& sync) {
    TimelineDoc doc = capture(sync);
    if (!history_.undo(doc)) {
        return false;
    }
    apply(doc, sync);
    return true;
}

bool TimelineSystem::redo(SyncSystem& sync) {
    TimelineDoc doc = capture(sync);
    if (!history_.redo(doc)) {
        return false;
    }
    apply(doc, sync);
    return true;
}

bool TimelineSystem::load() {
    std::ifstream file(timelinePath(), std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();
    ShowTimeline parsed;
    if (!timelineFromJson(buffer.str(), parsed)) {
        std::error_code ec;
        std::filesystem::path damaged = timelinePath();
        damaged += ".damaged";
        std::filesystem::copy_file(timelinePath(), damaged, std::filesystem::copy_options::overwrite_existing, ec);
        logLine("The timeline file could not be read. A copy was kept as timeline.json.damaged");
        return false;
    }
    timeline_ = std::move(parsed);
    loaded_ = true;
    history_.reset();
    return true;
}

bool TimelineSystem::save() {
    dirty_ = false;
    dirtyFor_ = 0.0;
    const std::filesystem::path target = timelinePath();
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << timelineToJson(timeline_);
        file.flush();
        if (!file) {
            file.close();
            std::error_code removeEc;
            std::filesystem::remove(temporary, removeEc);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

}

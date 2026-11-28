#include "core/show_timeline.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr double kStartEpsilon = 1.0e-9;
constexpr int kMaxJsonDepth = 16;

double roundSix(double value) {
    return std::round(value * 1.0e6) / 1.0e6;
}

bool finiteIn(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

bool shallowEnough(const std::string& text) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxJsonDepth) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return true;
}

size_t keyIndexAfter(const std::vector<Keyframe>& keys, double seconds) {
    return static_cast<size_t>(std::upper_bound(keys.begin(), keys.end(), seconds, [](double t, const Keyframe& key) { return t < key.seconds; }) - keys.begin());
}

bool sameKey(const Keyframe& a, const Keyframe& b) {
    return a.seconds == b.seconds && a.value == b.value && a.shape == b.shape;
}

bool sameCue(const Cue& a, const Cue& b) {
    return a.seconds == b.seconds && a.target == b.target && a.value == b.value;
}

}

const char* keyShapeName(KeyShape shape) {
    switch (shape) {
    case KeyShape::Hold:
        return "hold";
    case KeyShape::Smooth:
        return "smooth";
    default:
        return "linear";
    }
}

KeyShape keyShapeFromName(const std::string& name) {
    if (name == "hold") {
        return KeyShape::Hold;
    }
    if (name == "smooth") {
        return KeyShape::Smooth;
    }
    return KeyShape::Linear;
}

size_t keySegment(const std::vector<Keyframe>& keys, double seconds) {
    if (!std::isfinite(seconds)) {
        return 0;
    }
    return keyIndexAfter(keys, seconds);
}

double easeAlong(KeyShape shape, double u) {
    u = std::clamp(u, 0.0, 1.0);
    if (shape == KeyShape::Hold) {
        return u >= 1.0 ? 1.0 : 0.0;
    }
    if (shape == KeyShape::Smooth) {
        return u * u * (3.0 - 2.0 * u);
    }
    return u;
}

float evaluateKeys(const std::vector<Keyframe>& keys, double seconds, bool stepsOnly) {
    if (keys.empty()) {
        return 0.0f;
    }
    const size_t after = keySegment(keys, seconds);
    if (after == 0) {
        return keys.front().value;
    }
    if (after >= keys.size()) {
        return keys.back().value;
    }
    const Keyframe& a = keys[after - 1];
    const Keyframe& b = keys[after];
    if (stepsOnly || a.shape == KeyShape::Hold) {
        return a.value;
    }
    const double span = b.seconds - a.seconds;
    const double u = easeAlong(a.shape, span > 0.0 ? (seconds - a.seconds) / span : 1.0);
    return a.value + (b.value - a.value) * static_cast<float>(u);
}

size_t timelineKeyCount(const ShowTimeline& timeline) {
    size_t total = 0;
    for (const Lane& lane : timeline.lanes) {
        total += lane.keys.size();
    }
    return total;
}

int findLane(const ShowTimeline& timeline, const std::string& target) {
    for (size_t i = 0; i < timeline.lanes.size(); ++i) {
        if (timeline.lanes[i].target == target) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int addLane(ShowTimeline& timeline, const std::string& target) {
    if (target.empty() || target.size() > kMaxTargetText) {
        return -1;
    }
    const int existing = findLane(timeline, target);
    if (existing >= 0) {
        return existing;
    }
    if (timeline.lanes.size() >= kMaxTimelineLanes) {
        return -1;
    }
    Lane lane;
    lane.target = target;
    timeline.lanes.push_back(std::move(lane));
    return static_cast<int>(timeline.lanes.size() - 1);
}

bool removeLane(ShowTimeline& timeline, size_t lane) {
    if (lane >= timeline.lanes.size()) {
        return false;
    }
    timeline.lanes.erase(timeline.lanes.begin() + static_cast<std::ptrdiff_t>(lane));
    return true;
}

int insertKey(ShowTimeline& timeline, size_t lane, const Keyframe& key) {
    if (lane >= timeline.lanes.size() || !std::isfinite(key.seconds) || !std::isfinite(key.value)) {
        return -1;
    }
    Lane& target = timeline.lanes[lane];
    if (target.keys.size() >= kMaxLaneKeys || timelineKeyCount(timeline) >= kMaxTimelineKeys) {
        return -1;
    }
    Keyframe clamped = key;
    clamped.seconds = std::clamp(key.seconds, 0.0, kMaxTimelineSeconds);
    clamped.value = std::clamp(key.value, 0.0f, 1.0f);
    const size_t at = keyIndexAfter(target.keys, clamped.seconds);
    target.keys.insert(target.keys.begin() + static_cast<std::ptrdiff_t>(at), clamped);
    return static_cast<int>(at);
}

int moveKey(ShowTimeline& timeline, size_t lane, size_t key, double seconds, float value) {
    if (lane >= timeline.lanes.size() || key >= timeline.lanes[lane].keys.size() || !std::isfinite(seconds) || !std::isfinite(value)) {
        return -1;
    }
    std::vector<Keyframe>& keys = timeline.lanes[lane].keys;
    Keyframe moved = keys[key];
    moved.seconds = std::clamp(seconds, 0.0, kMaxTimelineSeconds);
    moved.value = std::clamp(value, 0.0f, 1.0f);
    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(key));
    const size_t at = keyIndexAfter(keys, moved.seconds);
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(at), moved);
    return static_cast<int>(at);
}

bool removeKey(ShowTimeline& timeline, size_t lane, size_t key) {
    if (lane >= timeline.lanes.size() || key >= timeline.lanes[lane].keys.size()) {
        return false;
    }
    std::vector<Keyframe>& keys = timeline.lanes[lane].keys;
    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(key));
    return true;
}

bool setKeyShape(ShowTimeline& timeline, size_t lane, size_t key, KeyShape shape) {
    if (lane >= timeline.lanes.size() || key >= timeline.lanes[lane].keys.size()) {
        return false;
    }
    timeline.lanes[lane].keys[key].shape = shape;
    return true;
}

void setLoop(ShowTimeline& timeline, bool enabled, double start, double end) {
    start = std::isfinite(start) ? std::clamp(start, 0.0, kMaxTimelineSeconds) : 0.0;
    end = std::isfinite(end) ? std::clamp(end, 0.0, kMaxTimelineSeconds) : 0.0;
    if (end < start) {
        std::swap(start, end);
    }
    timeline.loop.enabled = enabled;
    timeline.loop.start = start;
    timeline.loop.end = end;
}

bool sameDoc(const TimelineDoc& a, const TimelineDoc& b) {
    if (a.timeline.loop.enabled != b.timeline.loop.enabled || a.timeline.loop.start != b.timeline.loop.start || a.timeline.loop.end != b.timeline.loop.end) {
        return false;
    }
    if (a.timeline.lanes.size() != b.timeline.lanes.size() || a.cues.size() != b.cues.size()) {
        return false;
    }
    for (size_t i = 0; i < a.timeline.lanes.size(); ++i) {
        const Lane& x = a.timeline.lanes[i];
        const Lane& y = b.timeline.lanes[i];
        if (x.target != y.target || x.muted != y.muted || x.keys.size() != y.keys.size()) {
            return false;
        }
        for (size_t k = 0; k < x.keys.size(); ++k) {
            if (!sameKey(x.keys[k], y.keys[k])) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < a.cues.size(); ++i) {
        if (!sameCue(a.cues[i], b.cues[i])) {
            return false;
        }
    }
    return true;
}

size_t docBytes(const TimelineDoc& doc) {
    size_t total = sizeof(TimelineDoc);
    for (const Lane& lane : doc.timeline.lanes) {
        total += sizeof(Lane) + lane.target.capacity() + lane.keys.size() * sizeof(Keyframe);
    }
    for (const Cue& cue : doc.cues) {
        total += sizeof(Cue) + cue.target.capacity();
    }
    return total;
}

std::string timelineToJson(const ShowTimeline& timeline) {
    nlohmann::json doc;
    doc["version"] = 1;
    nlohmann::json loop;
    loop["enabled"] = timeline.loop.enabled;
    loop["start"] = roundSix(timeline.loop.start);
    loop["end"] = roundSix(timeline.loop.end);
    doc["loop"] = loop;
    nlohmann::json lanes = nlohmann::json::array();
    for (const Lane& lane : timeline.lanes) {
        nlohmann::json item;
        item["target"] = lane.target;
        item["muted"] = lane.muted;
        nlohmann::json keys = nlohmann::json::array();
        for (const Keyframe& key : lane.keys) {
            nlohmann::json entry;
            entry["seconds"] = roundSix(key.seconds);
            entry["value"] = roundSix(static_cast<double>(key.value));
            entry["shape"] = keyShapeName(key.shape);
            keys.push_back(entry);
        }
        item["keys"] = keys;
        lanes.push_back(item);
    }
    doc["lanes"] = lanes;
    return doc.dump(2);
}

bool timelineFromJson(const std::string& text, ShowTimeline& timeline) {
    if (text.empty() || text.size() > kMaxTimelineText || !shallowEnough(text)) {
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (!doc.is_object()) {
        return false;
    }
    ShowTimeline parsed;
    const auto loop = doc.find("loop");
    if (loop != doc.end() && loop->is_object()) {
        const auto enabled = loop->find("enabled");
        const auto start = loop->find("start");
        const auto end = loop->find("end");
        if (start != loop->end() && end != loop->end() && start->is_number() && end->is_number()) {
            const double a = start->get<double>();
            const double b = end->get<double>();
            if (finiteIn(a, 0.0, kMaxTimelineSeconds) && finiteIn(b, 0.0, kMaxTimelineSeconds)) {
                setLoop(parsed, enabled != loop->end() && enabled->is_boolean() && enabled->get<bool>(), a, b);
            }
        }
    }
    const auto lanes = doc.find("lanes");
    if (lanes == doc.end() || !lanes->is_array()) {
        return false;
    }
    {
        size_t totalKeys = 0;
        for (const nlohmann::json& item : *lanes) {
            if (!item.is_object() || parsed.lanes.size() >= kMaxTimelineLanes) {
                continue;
            }
            const auto target = item.find("target");
            if (target == item.end() || !target->is_string()) {
                continue;
            }
            Lane lane;
            lane.target = target->get<std::string>();
            if (lane.target.empty() || lane.target.size() > kMaxTargetText || findLane(parsed, lane.target) >= 0) {
                continue;
            }
            const auto muted = item.find("muted");
            lane.muted = muted != item.end() && muted->is_boolean() && muted->get<bool>();
            const auto keys = item.find("keys");
            if (keys != item.end() && keys->is_array()) {
                for (const nlohmann::json& entry : *keys) {
                    if (!entry.is_object() || lane.keys.size() >= kMaxLaneKeys || totalKeys >= kMaxTimelineKeys) {
                        continue;
                    }
                    const auto seconds = entry.find("seconds");
                    const auto value = entry.find("value");
                    if (seconds == entry.end() || value == entry.end() || !seconds->is_number() || !value->is_number()) {
                        continue;
                    }
                    const double t = seconds->get<double>();
                    const double v = value->get<double>();
                    if (!finiteIn(t, 0.0, kMaxTimelineSeconds) || !std::isfinite(v)) {
                        continue;
                    }
                    Keyframe key;
                    key.seconds = t;
                    key.value = std::clamp(static_cast<float>(v), 0.0f, 1.0f);
                    const auto shape = entry.find("shape");
                    if (shape != entry.end() && shape->is_string()) {
                        key.shape = keyShapeFromName(shape->get<std::string>());
                    }
                    lane.keys.push_back(key);
                    ++totalKeys;
                }
                std::stable_sort(lane.keys.begin(), lane.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.seconds < b.seconds; });
            }
            parsed.lanes.push_back(std::move(lane));
        }
    }
    timeline = std::move(parsed);
    return true;
}

const char* timelineSourceName(TimelineSourceChoice choice) {
    switch (choice) {
    case TimelineSourceChoice::Timecode:
        return "timecode";
    case TimelineSourceChoice::Transport:
        return "transport";
    default:
        return "auto";
    }
}

TimelineSourceChoice timelineSourceFromName(const std::string& name) {
    if (name == "timecode") {
        return TimelineSourceChoice::Timecode;
    }
    if (name == "transport") {
        return TimelineSourceChoice::Transport;
    }
    return TimelineSourceChoice::Auto;
}

const char* lostPolicyName(LostPolicy policy) {
    return policy == LostPolicy::Continue ? "continue" : "hold";
}

LostPolicy lostPolicyFromName(const std::string& name) {
    return name == "continue" ? LostPolicy::Continue : LostPolicy::Hold;
}

const char* overridePolicyName(OverridePolicy policy) {
    return policy == OverridePolicy::UntilReleased ? "released" : "segment";
}

OverridePolicy overridePolicyFromName(const std::string& name) {
    return name == "released" ? OverridePolicy::UntilReleased : OverridePolicy::NextSegment;
}

void TimelineRunner::configure(const TimelineConfig& config) {
    const bool wasEnabled = config_.enabled;
    const TimelineSourceChoice oldSource = config_.source;
    config_ = config;
    if (!config_.enabled || !wasEnabled) {
        following_ = false;
        lost_ = false;
        wantPlay_ = false;
        advancing_ = false;
        engaged_ = false;
        havePosition_ = false;
        armStart_ = true;
        seekPending_ = false;
        needAnchor_ = true;
        lanes_.clear();
        return;
    }
    if (config_.source != oldSource) {
        if (config_.source == TimelineSourceChoice::Transport) {
            following_ = false;
            lost_ = false;
        }
        if (config_.source == TimelineSourceChoice::Timecode) {
            wantPlay_ = false;
        }
        needAnchor_ = true;
    }
}

void TimelineRunner::play() {
    if (!config_.enabled || following_ || config_.source == TimelineSourceChoice::Timecode) {
        return;
    }
    wantPlay_ = true;
    needAnchor_ = true;
    lost_ = false;
    engaged_ = true;
}

void TimelineRunner::pause() {
    if (!config_.enabled || following_ || config_.source == TimelineSourceChoice::Timecode) {
        return;
    }
    wantPlay_ = false;
    armStart_ = false;
    lost_ = false;
}

void TimelineRunner::toggle() {
    if (wantPlay_) {
        pause();
    } else {
        play();
    }
}

void TimelineRunner::stop() {
    if (!config_.enabled || following_ || config_.source == TimelineSourceChoice::Timecode) {
        return;
    }
    wantPlay_ = false;
    lost_ = false;
    seekPending_ = true;
    seekTo_ = 0.0;
    armStart_ = true;
    engaged_ = true;
}

void TimelineRunner::seek(double seconds) {
    if (!config_.enabled || following_ || config_.source == TimelineSourceChoice::Timecode || !std::isfinite(seconds)) {
        return;
    }
    seekPending_ = true;
    seekTo_ = std::clamp(seconds, 0.0, kMaxTimelineSeconds);
    armStart_ = true;
    needAnchor_ = true;
    engaged_ = true;
}

TimelineState TimelineRunner::state() const {
    if (!config_.enabled) {
        return TimelineState::Off;
    }
    if (following_) {
        return TimelineState::Following;
    }
    if (lost_) {
        return wantPlay_ ? TimelineState::LostRunning : TimelineState::LostHolding;
    }
    if (config_.source == TimelineSourceChoice::Timecode) {
        return TimelineState::Waiting;
    }
    if (wantPlay_) {
        return advancing_ ? TimelineState::Playing : TimelineState::Paused;
    }
    return armStart_ ? TimelineState::Stopped : TimelineState::Paused;
}

LaneStatus TimelineRunner::laneStatus(const std::string& target) const {
    const auto found = lanes_.find(target);
    return found == lanes_.end() ? LaneStatus::Idle : found->second.status;
}

bool TimelineRunner::overridden(const std::string& target) const {
    const auto found = lanes_.find(target);
    return found != lanes_.end() && found->second.overridden;
}

void TimelineRunner::release(const std::string& target) {
    const auto found = lanes_.find(target);
    if (found != lanes_.end()) {
        found->second.overridden = false;
        found->second.hasWritten = false;
    }
}

void TimelineRunner::releaseAll() {
    lanes_.clear();
}

size_t TimelineRunner::overriddenCount() const {
    size_t count = 0;
    for (const auto& entry : lanes_) {
        if (entry.second.overridden) {
            ++count;
        }
    }
    return count;
}

void TimelineRunner::anchorAt(double position, double clock) {
    anchorPosition_ = position;
    anchorClock_ = clock;
}

void TimelineRunner::fireRange(const std::vector<Cue>& cues, double from, double to, TimelineHost& host) {
    for (size_t index : dueCues(cues, from, to)) {
        host.fire(cues[index]);
        ++cuesFired_;
    }
}

size_t TimelineRunner::countCues(const std::vector<Cue>& cues, double from, double to) const {
    return dueCues(cues, from, to).size();
}

void TimelineRunner::pruneRuntime(const ShowTimeline& timeline) {
    for (auto it = lanes_.begin(); it != lanes_.end();) {
        if (findLane(timeline, it->first) < 0) {
            it = lanes_.erase(it);
        } else {
            ++it;
        }
    }
}

void TimelineRunner::driveLanes(const ShowTimeline& timeline, TimelineHost& host) {
    if (lanes_.size() > timeline.lanes.size()) {
        pruneRuntime(timeline);
    }
    for (const Lane& lane : timeline.lanes) {
        LaneRuntime& runtime = lanes_[lane.target];
        if (lane.muted) {
            runtime.status = LaneStatus::Muted;
            runtime.hasWritten = false;
            runtime.overridden = false;
            continue;
        }
        const LaneKind kind = host.laneKind(lane.target);
        if (kind == LaneKind::Refused) {
            runtime.status = LaneStatus::Unavailable;
            continue;
        }
        if (lane.keys.empty()) {
            runtime.status = LaneStatus::Idle;
            continue;
        }
        const bool toggle = kind == LaneKind::Toggle;
        float wanted = evaluateKeys(lane.keys, position_, toggle);
        if (toggle) {
            wanted = wanted >= 0.5f ? 1.0f : 0.0f;
        }
        float current = 0.0f;
        const bool haveCurrent = host.read(lane.target, current);
        if (runtime.overridden) {
            if (config_.override == OverridePolicy::NextSegment && keySegment(lane.keys, position_) != runtime.segment) {
                runtime.overridden = false;
                runtime.hasWritten = false;
            } else {
                runtime.status = LaneStatus::Overridden;
                continue;
            }
        }
        if (runtime.hasWritten && haveCurrent && std::fabs(current - runtime.lastRead) > kOverrideTolerance) {
            runtime.overridden = true;
            runtime.segment = keySegment(lane.keys, position_);
            runtime.status = LaneStatus::Overridden;
            ++overrides_;
            continue;
        }
        if (!runtime.hasWritten || std::fabs(wanted - runtime.lastWanted) > kWriteTolerance) {
            if (host.write(lane.target, wanted)) {
                float after = wanted;
                if (!host.read(lane.target, after)) {
                    after = wanted;
                }
                runtime.lastRead = after;
                runtime.lastWanted = wanted;
                runtime.hasWritten = true;
                ++writes_;
            }
        }
        runtime.status = LaneStatus::Following;
    }
}

void TimelineRunner::update(const TimelineInputs& inputs, const ShowTimeline& timeline, const std::vector<Cue>& cues, TimelineHost& host) {
    if (!config_.enabled) {
        return;
    }
    const bool hadPosition = havePosition_;
    const double previous = position_;
    bool discontinuity = false;
    bool wrapped = false;
    double wrapFrom = 0.0;
    double wrapTo = 0.0;
    const bool clockOk = std::isfinite(inputs.clock);
    const bool timecodeOk = config_.source != TimelineSourceChoice::Transport && inputs.timecodeValid && std::isfinite(inputs.timecodeSeconds) && inputs.timecodeSeconds >= 0.0;

    if (timecodeOk) {
        if (!following_) {
            following_ = true;
            lost_ = false;
            wantPlay_ = false;
            if (hadPosition) {
                ++clockChanges_;
                discontinuity = true;
            }
        }
        position_ = std::min(inputs.timecodeSeconds, kMaxTimelineSeconds);
        advancing_ = true;
        engaged_ = true;
        armStart_ = false;
        seekPending_ = false;
    } else {
        if (following_) {
            following_ = false;
            lost_ = true;
            ++timecodeLosses_;
            wantPlay_ = config_.lost == LostPolicy::Continue;
            needAnchor_ = true;
            armStart_ = false;
            advancing_ = false;
        }
        if (config_.source == TimelineSourceChoice::Timecode && !lost_) {
            advancing_ = false;
        } else {
            if (seekPending_) {
                position_ = seekTo_;
                seekPending_ = false;
                discontinuity = true;
                needAnchor_ = true;
            }
            const bool shouldAdvance = wantPlay_ && inputs.running && clockOk;
            if (shouldAdvance) {
                if (!advancing_ || needAnchor_ || inputs.clock < anchorClock_) {
                    anchorAt(position_, inputs.clock);
                }
                needAnchor_ = false;
                position_ = std::clamp(anchorPosition_ + (inputs.clock - anchorClock_), 0.0, kMaxTimelineSeconds);
                if (timeline.loop.usable() && hadPosition && !discontinuity && previous < timeline.loop.end && position_ >= timeline.loop.end) {
                    const double length = timeline.loop.end - timeline.loop.start;
                    const double over = std::fmod(position_ - timeline.loop.end, length);
                    wrapFrom = previous;
                    wrapTo = timeline.loop.start + over;
                    wrapped = true;
                    position_ = wrapTo;
                    anchorAt(position_, inputs.clock);
                    ++loopWraps_;
                }
            } else {
                needAnchor_ = true;
            }
            advancing_ = shouldAdvance;
        }
    }
    havePosition_ = true;

    if (hadPosition && !discontinuity && !wrapped) {
        const double step = position_ - previous;
        if (step < 0.0 || step > kTimelineMaxStep) {
            discontinuity = true;
        }
    }
    if (discontinuity && hadPosition) {
        ++seeks_;
        if (!following_ && config_.cues && advancing_ && position_ > previous) {
            cuesSkipped_ += countCues(cues, previous, position_);
        }
    }

    const bool ownsCues = config_.cues && advancing_ && !following_;
    if (ownsCues) {
        if (wrapped) {
            fireRange(cues, wrapFrom, timeline.loop.end, host);
            fireRange(cues, timeline.loop.start - kStartEpsilon, wrapTo, host);
        } else if (armStart_) {
            fireRange(cues, position_ - kStartEpsilon, position_, host);
        } else if (!discontinuity) {
            fireRange(cues, previous, position_, host);
        }
    }
    if (advancing_) {
        armStart_ = false;
    }

    if (engaged_) {
        driveLanes(timeline, host);
    }
}

void TimelineHistory::reset() {
    undo_.clear();
    redo_.clear();
    bytes_ = 0;
}

void TimelineHistory::trim() {
    while (undo_.size() > kLimit) {
        undo_.erase(undo_.begin());
    }
    while (redo_.size() > kLimit) {
        redo_.erase(redo_.begin());
    }
    auto total = [this]() {
        size_t sum = 0;
        for (const TimelineDoc& doc : undo_) {
            sum += docBytes(doc);
        }
        for (const TimelineDoc& doc : redo_) {
            sum += docBytes(doc);
        }
        return sum;
    };
    bytes_ = total();
    while (bytes_ > kBudgetBytes && undo_.size() + redo_.size() > 1) {
        if (!undo_.empty()) {
            undo_.erase(undo_.begin());
        } else {
            redo_.erase(redo_.begin());
        }
        bytes_ = total();
    }
}

bool TimelineHistory::commit(const TimelineDoc& before, const TimelineDoc& after) {
    if (sameDoc(before, after)) {
        return false;
    }
    undo_.push_back(before);
    redo_.clear();
    trim();
    return true;
}

bool TimelineHistory::undo(TimelineDoc& current) {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(std::move(current));
    current = std::move(undo_.back());
    undo_.pop_back();
    trim();
    return true;
}

bool TimelineHistory::redo(TimelineDoc& current) {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(std::move(current));
    current = std::move(redo_.back());
    redo_.pop_back();
    trim();
    return true;
}

}

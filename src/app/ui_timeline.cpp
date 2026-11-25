#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "app/ui_targets.hpp"
#include "core/show_timeline.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

namespace {

constexpr float kLabelWidth = 214.0f;
constexpr float kRulerHeight = 28.0f;
constexpr float kCueHeight = 36.0f;
constexpr float kLaneHeight = 62.0f;
constexpr float kLanePad = 8.0f;
constexpr float kHitRadius = 9.0f;
constexpr float kMinPixelsPerSecond = 2.0f;
constexpr float kMaxPixelsPerSecond = 4000.0f;

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

std::string lowerText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string clockText(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const long long total = static_cast<long long>(std::llround(seconds * 1000.0));
    const long long millis = total % 1000;
    const long long whole = total / 1000;
    const long long secs = whole % 60;
    const long long mins = (whole / 60) % 60;
    const long long hours = whole / 3600;
    char buffer[48];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld.%03lld", hours, mins, secs, millis);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld.%03lld", mins, secs, millis);
    }
    return buffer;
}

std::string rulerText(double seconds, double step) {
    const long long total = static_cast<long long>(std::llround(std::max(0.0, seconds) * 100.0));
    const long long centis = total % 100;
    const long long whole = total / 100;
    const long long secs = whole % 60;
    const long long mins = (whole / 60) % 60;
    const long long hours = whole / 3600;
    char buffer[48];
    if (step >= 1.0) {
        if (hours > 0) {
            std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", hours, mins, secs);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", mins, secs);
        }
    } else if (step >= 0.1) {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld.%lld", hours * 60 + mins, secs, centis / 10);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld.%02lld", hours * 60 + mins, secs, centis);
    }
    return buffer;
}

const char* stateLabel(TimelineState state) {
    switch (state) {
    case TimelineState::Waiting:
        return tr("Waiting for timecode");
    case TimelineState::Stopped:
        return tr("Stopped");
    case TimelineState::Paused:
        return tr("Paused");
    case TimelineState::Playing:
        return tr("Playing");
    case TimelineState::Following:
        return tr("Following the timecode");
    case TimelineState::LostHolding:
        return tr("Timecode lost, holding the position");
    case TimelineState::LostRunning:
        return tr("Timecode lost, running on");
    default:
        return tr("Off");
    }
}

ImVec4 stateColor(TimelineState state) {
    switch (state) {
    case TimelineState::Playing:
    case TimelineState::Following:
        return kGood;
    case TimelineState::LostHolding:
    case TimelineState::LostRunning:
        return kBad;
    case TimelineState::Waiting:
        return kWarn;
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
}

const char* shapeLabel(KeyShape shape) {
    switch (shape) {
    case KeyShape::Hold:
        return tr("Hold");
    case KeyShape::Smooth:
        return tr("Smooth");
    default:
        return tr("Linear");
    }
}

const char* sourceLabel(TimelineSourceChoice choice) {
    switch (choice) {
    case TimelineSourceChoice::Timecode:
        return tr("Timecode only");
    case TimelineSourceChoice::Transport:
        return tr("Own transport only");
    default:
        return tr("Automatic");
    }
}

const char* lostLabel(LostPolicy policy) {
    return policy == LostPolicy::Continue ? tr("Keep running on its own transport") : tr("Hold the position");
}

const char* overrideLabel(OverridePolicy policy) {
    return policy == OverridePolicy::UntilReleased ? tr("Until I release it") : tr("Until the next keyframe segment");
}

ImU32 withAlpha(const ImVec4& color, float alpha) {
    return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha));
}

struct Geometry {
    float x0 = 0.0f;
    float x1 = 0.0f;
    double start = 0.0;
    float pps = 40.0f;

    float toX(double seconds) const { return x0 + static_cast<float>((seconds - start) * pps); }
    double toTime(float x) const { return start + static_cast<double>(x - x0) / pps; }
    double visible() const { return static_cast<double>(x1 - x0) / pps; }
};

double roundMillis(double seconds) {
    return std::round(seconds * 1000.0) / 1000.0;
}

double clampSeconds(double seconds) {
    if (!std::isfinite(seconds)) {
        return 0.0;
    }
    return std::clamp(seconds, 0.0, kMaxTimelineSeconds);
}

double rulerStep(float pixelsPerSecond) {
    static const double kSteps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0};
    for (double step : kSteps) {
        if (step * pixelsPerSecond >= 84.0) {
            return step;
        }
    }
    return 3600.0;
}

int moveCue(std::vector<Cue>& cues, size_t index, double seconds) {
    if (index >= cues.size()) {
        return -1;
    }
    Cue cue = cues[index];
    cue.seconds = clampSeconds(roundMillis(seconds));
    cues.erase(cues.begin() + static_cast<std::ptrdiff_t>(index));
    const auto at = std::upper_bound(cues.begin(), cues.end(), cue.seconds, [](double value, const Cue& other) { return value < other.seconds; });
    const auto inserted = cues.insert(at, cue);
    return static_cast<int>(inserted - cues.begin());
}

}

void App::timelineBeginEdit() {
    if (timelineView_.editing) {
        return;
    }
    timelineView_.before = timeline_.capture(sync_);
    timelineView_.editing = true;
}

void App::timelineEndEdit() {
    if (!timelineView_.editing) {
        return;
    }
    timelineView_.editing = false;
    timeline_.commit(timelineView_.before, sync_);
    timelineView_.before = TimelineDoc();
}

void App::timelineUndo() {
    timelineEndEdit();
    if (timeline_.undo(sync_)) {
        toast(tr("Timeline edit undone"));
    } else {
        toast(tr("Nothing to undo"));
    }
    timelineView_.selectKey = -1;
    timelineView_.selectCue = -1;
    timelineView_.selectLane = -1;
}

void App::timelineRedo() {
    timelineEndEdit();
    if (timeline_.redo(sync_)) {
        toast(tr("Timeline edit redone"));
    } else {
        toast(tr("Nothing to redo"));
    }
    timelineView_.selectKey = -1;
    timelineView_.selectCue = -1;
    timelineView_.selectLane = -1;
}

void App::updateTimeline(double dt) {
    TimelineConfig config;
    config.enabled = settings_.timelineEnabled && !netSync_.suppressesAutomation();
    config.source = timelineSourceFromName(settings_.timelineSource);
    config.lost = lostPolicyFromName(settings_.timelineLost);
    config.override = overridePolicyFromName(settings_.timelineOverride);
    config.cues = settings_.cuesEnabled && !netSync_.suppressesAutomation();
    timeline_.configure(config);
    timeline_.update(dt, timelineInputs(sync_, engine_.beatClock().time(), !engine_.paused), sync_, control_);
}

void App::drawTimelineToolbar() {
    TimelineRunner& runner = timeline_.runner();
    TimelineView& view = timelineView_;
    const bool on = settings_.timelineEnabled;
    const TimelineState state = runner.state();
    const bool following = state == TimelineState::Following || state == TimelineState::LostHolding || state == TimelineState::LostRunning;
    const bool timecodeOnly = timelineSourceFromName(settings_.timelineSource) == TimelineSourceChoice::Timecode;
    const bool transportLocked = !on || following || timecodeOnly;

    if (ImGui::Checkbox(trId("Run the timeline", "timelineRun"), &settings_.timelineEnabled)) {
        saveSettings(settings_);
    }
    ImGui::SetItemTooltip("%s", tr("Off by default. While it is off nothing is written and no cue is fired by the timeline. Your keyframes stay as they are."));
    ImGui::SameLine();

    ImGui::BeginDisabled(transportLocked);
    const bool playing = state == TimelineState::Playing;
    if (ui::accentButton(playing ? trId("Pause", "timelinePause") : trId("Play", "timelinePlay"), playing)) {
        runner.toggle();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Stop", "timelineStop"))) {
        runner.stop();
    }
    ImGui::EndDisabled();
    if (transportLocked && on) {
        ImGui::SetItemTooltip("%s", following ? tr("The timecode sets the position. The play and stop buttons are ignored while a timecode is present.") : tr("The source is set to timecode only."));
    }
    ImGui::SameLine();

    ImGui::PushFont(nullptr, 24.0f);
    ImGui::TextColored(ui::kAccent, "%s", clockText(on ? runner.position() : 0.0).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    const ImVec4 color = stateColor(state);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(color, "%s", stateLabel(state));

    if (on && runner.overriddenCount() > 0) {
        ImGui::SameLine();
        const std::string label = std::string(tr("Release hand moves")) + " (" + std::to_string(runner.overriddenCount()) + ")";
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kWarn.x * 0.5f, kWarn.y * 0.5f, kWarn.z * 0.3f, 1.0f));
        if (ImGui::Button(label.c_str())) {
            runner.releaseAll();
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tr("You moved these controls by hand, so the timeline lets go of them. Release hands them back."));
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!timeline_.history().canUndo());
    if (ImGui::Button(trId("Undo", "timelineUndo"))) {
        timelineUndo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!timeline_.history().canRedo());
    if (ImGui::Button(trId("Redo", "timelineRedo"))) {
        timelineRedo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ShowTimeline& tl = timeline_.timeline();
    ImGui::BeginDisabled(tl.lanes.size() >= kMaxTimelineLanes);
    if (ImGui::Button(trId("Add lane", "timelineAddLane"))) {
        view.pickMode = 0;
        view.filter[0] = '\0';
        view.openPick = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr("A lane automates one control with keyframes. Triggers such as clips and columns are cues, not lanes."));
    ImGui::SameLine();
    ImGui::BeginDisabled(sync_.cues().size() >= kMaxCues);
    if (ImGui::Button(trId("Add cue", "timelineAddCue"))) {
        view.pickMode = 1;
        view.pickSeconds = clampSeconds(roundMillis(runner.position()));
        view.filter[0] = '\0';
        view.openPick = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    bool loopOn = tl.loop.enabled;
    if (ImGui::Checkbox(trId("Loop", "timelineLoop"), &loopOn)) {
        timelineBeginEdit();
        double start = tl.loop.start;
        double end = tl.loop.end;
        if (loopOn && end - start < kMinLoopLength) {
            start = clampSeconds(roundMillis(runner.position()));
            end = clampSeconds(start + 8.0);
        }
        setLoop(tl, loopOn, start, end);
        timelineEndEdit();
    }
    ImGui::SetItemTooltip("%s", tr("Repeats the region marked on the ruler. Hold Shift and drag on the ruler to mark it. It only applies to the timeline's own transport, never to a timecode."));
    ImGui::SameLine();
    if (ImGui::Button(trId("Settings", "timelineSettings"))) {
        ImGui::OpenPopup("##tlSettings");
    }
    ImGui::SameLine();
    ImGui::Checkbox(trId("Follow", "timelineFollow"), &view.followPlayhead);
    ImGui::SetItemTooltip("%s", tr("Scrolls the view along with the playhead."));
    ImGui::SameLine();
    if (ImGui::Button(trId("Fit", "timelineFit"))) {
        double end = 30.0;
        for (const Lane& lane : tl.lanes) {
            if (!lane.keys.empty()) {
                end = std::max(end, lane.keys.back().seconds);
            }
        }
        if (!sync_.cues().empty()) {
            end = std::max(end, sync_.cues().back().seconds);
        }
        const float width = std::max(200.0f, ImGui::GetContentRegionAvail().x - kLabelWidth - 24.0f);
        view.pixelsPerSecond = std::clamp(static_cast<float>(width / (end * 1.05)), kMinPixelsPerSecond, kMaxPixelsPerSecond);
        view.viewStart = 0.0;
    }
    ImGui::SetItemTooltip("%s", tr("Zooms so that all keyframes and cues are visible."));

    if (ImGui::BeginPopup("##tlSettings")) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Timeline settings"));
        ImGui::Spacing();
        const TimelineSourceChoice source = timelineSourceFromName(settings_.timelineSource);
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo(tr("Position source"), sourceLabel(source))) {
            for (TimelineSourceChoice choice : {TimelineSourceChoice::Auto, TimelineSourceChoice::Timecode, TimelineSourceChoice::Transport}) {
                if (ImGui::Selectable(sourceLabel(choice), choice == source)) {
                    settings_.timelineSource = timelineSourceName(choice);
                    saveSettings(settings_);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("Automatic follows the timecode when one is present and otherwise uses the timeline's own transport."));
        const LostPolicy lost = lostPolicyFromName(settings_.timelineLost);
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo(tr("When the timecode is lost"), lostLabel(lost))) {
            for (LostPolicy policy : {LostPolicy::Hold, LostPolicy::Continue}) {
                if (ImGui::Selectable(lostLabel(policy), policy == lost)) {
                    settings_.timelineLost = lostPolicyName(policy);
                    saveSettings(settings_);
                }
            }
            ImGui::EndCombo();
        }
        const OverridePolicy override = overridePolicyFromName(settings_.timelineOverride);
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo(tr("How long a hand move wins"), overrideLabel(override))) {
            for (OverridePolicy policy : {OverridePolicy::NextSegment, OverridePolicy::UntilReleased}) {
                if (ImGui::Selectable(overrideLabel(policy), policy == override)) {
                    settings_.timelineOverride = overridePolicyName(policy);
                    saveSettings(settings_);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("If you move a controlled slider yourself, the timeline stops writing to it until the playhead enters a new keyframe segment or you release it."));
        ImGui::Spacing();
        double loopStart = tl.loop.start;
        double loopEnd = tl.loop.end;
        ImGui::SetNextItemWidth(140.0f);
        bool loopChanged = ImGui::InputDouble(tr("Loop start (s)"), &loopStart, 0.0, 0.0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
        loopChanged = ImGui::IsItemDeactivatedAfterEdit() || loopChanged;
        ImGui::SetNextItemWidth(140.0f);
        bool endChanged = ImGui::InputDouble(tr("Loop end (s)"), &loopEnd, 0.0, 0.0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
        endChanged = ImGui::IsItemDeactivatedAfterEdit() || endChanged;
        if (loopChanged || endChanged) {
            timelineBeginEdit();
            setLoop(tl, tl.loop.enabled, loopStart, loopEnd);
            timelineEndEdit();
        }
        ImGui::Spacing();
        ImGui::Checkbox(tr("Show counters"), &view.showCounters);
        if (!settings_.cuesEnabled) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
            ImGui::TextWrapped("%s", tr("Cues are switched off in Settings, Sync. The timeline will not fire them."));
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
}

void App::drawTimelineTargetPicker() {
    TimelineView& view = timelineView_;
    ShowTimeline& tl = timeline_.timeline();
    ImGui::SetNextWindowSize(ImVec2(440.0f, 380.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tlPick")) {
        return;
    }
    ControlTimelineHost host(control_);
    const bool laneMode = view.pickMode == 0;
    ImGui::TextColored(ui::kAccent, "%s", laneMode ? tr("Choose the control for the new lane") : tr("Choose what the cue does"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##tlFilter", tr("Search"), view.filter, sizeof(view.filter));
    const std::string filter = lowerText(view.filter);
    const auto& targets = controlTargets();
    std::vector<std::pair<int, std::string>> shown;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (laneMode && host.laneKind(targets[i].id) == LaneKind::Refused) {
            continue;
        }
        std::string entry = targetLabel(targets[i]);
        if (filter.empty() || lowerText(entry).find(filter) != std::string::npos || lowerText(targets[i].id).find(filter) != std::string::npos) {
            shown.emplace_back(static_cast<int>(i), std::move(entry));
        }
    }
    ImGui::BeginChild("##tlPickList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& entry = shown[static_cast<size_t>(row)];
            const std::string& id = targets[static_cast<size_t>(entry.first)].id;
            const bool taken = laneMode && findLane(tl, id) >= 0;
            ImGui::PushID(row);
            ImGui::BeginDisabled(taken);
            if (ImGui::Selectable(entry.second.c_str())) {
                timelineBeginEdit();
                if (laneMode) {
                    const int lane = addLane(tl, id);
                    view.selectLane = lane;
                    view.selectKey = -1;
                } else if (sync_.cues().size() < kMaxCues) {
                    Cue cue;
                    cue.seconds = view.pickSeconds;
                    cue.target = id;
                    cue.value = 1.0f;
                    std::vector<Cue>& cues = sync_.cues();
                    const auto at = std::upper_bound(cues.begin(), cues.end(), cue.seconds, [](double value, const Cue& other) { return value < other.seconds; });
                    const auto inserted = cues.insert(at, cue);
                    view.selectCue = static_cast<int>(inserted - cues.begin());
                    view.selectKey = -1;
                    sync_.markCuesDirty();
                }
                timelineEndEdit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (taken) {
                ImGui::SetItemTooltip("%s", tr("This control already has a lane."));
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void App::drawTimelineCanvas() {
    TimelineView& view = timelineView_;
    TimelineRunner& runner = timeline_.runner();
    ShowTimeline& tl = timeline_.timeline();
    std::vector<Cue>& cues = sync_.cues();
    ControlTimelineHost host(control_);
    ImGuiIO& io = ImGui::GetIO();

    const bool on = settings_.timelineEnabled;
    const float reserve = 96.0f + (view.showCounters ? 24.0f : 0.0f);
    const float childHeight = std::max(160.0f, ImGui::GetContentRegionAvail().y - reserve);
    ImGui::BeginChild("##tlCanvas", ImVec2(0.0f, childHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);

    const float childWidth = ImGui::GetContentRegionAvail().x;
    const float areaWidth = std::max(120.0f, childWidth - kLabelWidth - 6.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    Geometry geo;
    geo.x0 = origin.x + kLabelWidth;
    geo.x1 = geo.x0 + areaWidth;
    geo.pps = std::clamp(view.pixelsPerSecond, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    view.pixelsPerSecond = geo.pps;

    const double position = on ? runner.position() : 0.0;
    if (view.followPlayhead && runner.advancing() && view.drag == TimelineDrag::None && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        const double visible = static_cast<double>(areaWidth) / geo.pps;
        if (position < view.viewStart || position > view.viewStart + visible * 0.94) {
            view.viewStart = std::max(0.0, position - visible * 0.06);
        }
    }
    view.viewStart = std::clamp(view.viewStart, 0.0, kMaxTimelineSeconds);
    geo.start = view.viewStart;

    const float laneCount = static_cast<float>(tl.lanes.size());
    const float cueTop = origin.y + kRulerHeight;
    const float lanesTop = cueTop + kCueHeight;
    const float totalHeight = kRulerHeight + kCueHeight + laneCount * kLaneHeight + 12.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 accent = ImGui::GetColorU32(ui::kAccent);

    draw->PushClipRect(ImVec2(geo.x0, origin.y), ImVec2(geo.x1, origin.y + totalHeight), true);
    draw->AddRectFilled(ImVec2(geo.x0, origin.y), ImVec2(geo.x1, origin.y + kRulerHeight), withAlpha(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f));
    draw->AddRectFilled(ImVec2(geo.x0, cueTop), ImVec2(geo.x1, lanesTop), withAlpha(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.04f));
    for (size_t i = 0; i < tl.lanes.size(); ++i) {
        const float top = lanesTop + static_cast<float>(i) * kLaneHeight;
        draw->AddRectFilled(ImVec2(geo.x0, top), ImVec2(geo.x1, top + kLaneHeight), withAlpha(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), i % 2 == 0 ? 0.0f : 0.03f));
        draw->AddLine(ImVec2(geo.x0, top + kLaneHeight), ImVec2(geo.x1, top + kLaneHeight), lineColor);
    }

    const double step = rulerStep(geo.pps);
    {
        const double firstMajor = std::floor(geo.start / step) * step;
        const double minorStep = step / 5.0;
        if (minorStep * geo.pps >= 7.0) {
            for (double t = std::floor(geo.start / minorStep) * minorStep; geo.toX(t) <= geo.x1; t += minorStep) {
                const float x = geo.toX(t);
                draw->AddLine(ImVec2(x, origin.y + kRulerHeight - 6.0f), ImVec2(x, origin.y + kRulerHeight), withAlpha(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.25f));
            }
        }
        for (double t = firstMajor; geo.toX(t) <= geo.x1; t += step) {
            if (t < 0.0) {
                continue;
            }
            const float x = geo.toX(t);
            draw->AddLine(ImVec2(x, origin.y + 4.0f), ImVec2(x, origin.y + totalHeight), withAlpha(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.08f));
            draw->AddLine(ImVec2(x, origin.y + kRulerHeight - 12.0f), ImVec2(x, origin.y + kRulerHeight), dimColor);
            const std::string label = rulerText(t, step);
            draw->AddText(ImVec2(x + 4.0f, origin.y + 3.0f), dimColor, label.c_str());
        }
    }

    if (tl.loop.end - tl.loop.start > 0.0) {
        const float lx0 = geo.toX(tl.loop.start);
        const float lx1 = geo.toX(tl.loop.end);
        const ImVec4 loopColor = tl.loop.usable() ? ui::kAccent : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        draw->AddRectFilled(ImVec2(lx0, origin.y + kRulerHeight - 9.0f), ImVec2(lx1, origin.y + kRulerHeight), withAlpha(loopColor, tl.loop.enabled ? 0.65f : 0.25f));
        draw->AddRectFilled(ImVec2(lx0, cueTop), ImVec2(lx1, origin.y + totalHeight), withAlpha(loopColor, tl.loop.enabled ? 0.07f : 0.03f));
        draw->AddLine(ImVec2(lx0, origin.y + 2.0f), ImVec2(lx0, origin.y + totalHeight), withAlpha(loopColor, 0.7f));
        draw->AddLine(ImVec2(lx1, origin.y + 2.0f), ImVec2(lx1, origin.y + totalHeight), withAlpha(loopColor, 0.7f));
    }

    for (size_t i = 0; i < cues.size(); ++i) {
        const float x = geo.toX(cues[i].seconds);
        if (x < geo.x0 - 12.0f || x > geo.x1 + 12.0f) {
            continue;
        }
        const float cy = cueTop + kCueHeight * 0.5f;
        const bool selected = static_cast<int>(i) == view.selectCue;
        const ImU32 fill = selected ? accent : ImGui::GetColorU32(ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
        draw->AddQuadFilled(ImVec2(x, cy - 8.0f), ImVec2(x + 7.0f, cy), ImVec2(x, cy + 8.0f), ImVec2(x - 7.0f, cy), fill);
        draw->AddQuad(ImVec2(x, cy - 8.0f), ImVec2(x + 7.0f, cy), ImVec2(x, cy + 8.0f), ImVec2(x - 7.0f, cy), withAlpha(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.8f));
        if (geo.pps * 0.5 > 40.0 || selected) {
            const std::string label = targetLabelById(cues[i].target);
            draw->AddText(ImVec2(x + 10.0f, cy - ImGui::GetFontSize() * 0.5f), selected ? textColor : dimColor, label.c_str());
        }
    }

    for (size_t li = 0; li < tl.lanes.size(); ++li) {
        const Lane& lane = tl.lanes[li];
        const float top = lanesTop + static_cast<float>(li) * kLaneHeight;
        const float bottom = top + kLaneHeight;
        const float innerTop = top + kLanePad;
        const float innerBottom = bottom - kLanePad;
        const LaneKind kind = host.laneKind(lane.target);
        const bool toggle = kind == LaneKind::Toggle;
        const LaneStatus status = on ? runner.laneStatus(lane.target) : LaneStatus::Idle;
        const bool selectedLane = static_cast<int>(li) == view.selectLane;
        ImVec4 base = ui::kAccent;
        if (lane.muted || kind == LaneKind::Refused) {
            base = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        } else if (status == LaneStatus::Overridden) {
            base = kWarn;
        }
        const ImU32 curve = withAlpha(base, lane.muted ? 0.5f : 0.95f);
        draw->AddLine(ImVec2(geo.x0, innerBottom), ImVec2(geo.x1, innerBottom), withAlpha(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.06f));
        draw->AddLine(ImVec2(geo.x0, innerTop), ImVec2(geo.x1, innerTop), withAlpha(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.06f));
        if (selectedLane) {
            draw->AddRect(ImVec2(geo.x0, top + 1.0f), ImVec2(geo.x1, bottom - 1.0f), withAlpha(ui::kAccent, 0.35f));
        }

        if (!lane.keys.empty()) {
            std::vector<ImVec2> points;
            const float stepPx = 2.0f;
            for (float x = geo.x0; x <= geo.x1 + stepPx; x += stepPx) {
                const float value = evaluateKeys(lane.keys, geo.toTime(x), toggle);
                points.emplace_back(x, innerBottom - value * (innerBottom - innerTop));
            }
            if (points.size() >= 2) {
                draw->AddPolyline(points.data(), static_cast<int>(points.size()), curve, ImDrawFlags_None, 2.0f);
            }
        } else {
            draw->AddText(ImVec2(geo.x0 + 8.0f, top + kLaneHeight * 0.5f - ImGui::GetFontSize() * 0.5f), dimColor, tr("Double click to add a keyframe"));
        }

        for (size_t ki = 0; ki < lane.keys.size(); ++ki) {
            const Keyframe& key = lane.keys[ki];
            const float x = geo.toX(key.seconds);
            if (x < geo.x0 - 12.0f || x > geo.x1 + 12.0f) {
                continue;
            }
            const float y = innerBottom - key.value * (innerBottom - innerTop);
            const bool selected = selectedLane && static_cast<int>(ki) == view.selectKey;
            const ImU32 fill = selected ? ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) : curve;
            if (key.shape == KeyShape::Hold) {
                draw->AddRectFilled(ImVec2(x - 5.0f, y - 5.0f), ImVec2(x + 5.0f, y + 5.0f), fill);
            } else if (key.shape == KeyShape::Smooth) {
                draw->AddCircleFilled(ImVec2(x, y), 5.5f, fill);
            } else {
                draw->AddQuadFilled(ImVec2(x, y - 6.0f), ImVec2(x + 6.0f, y), ImVec2(x, y + 6.0f), ImVec2(x - 6.0f, y), fill);
            }
        }
    }

    if (on) {
        const float px = geo.toX(position);
        if (px >= geo.x0 - 8.0f && px <= geo.x1 + 8.0f) {
            draw->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + totalHeight), accent, 2.0f);
            draw->AddTriangleFilled(ImVec2(px - 7.0f, origin.y), ImVec2(px + 7.0f, origin.y), ImVec2(px, origin.y + 11.0f), accent);
        }
    }
    draw->PopClipRect();

    for (size_t li = 0; li < tl.lanes.size(); ++li) {
        const Lane& lane = tl.lanes[li];
        const float top = lanesTop + static_cast<float>(li) * kLaneHeight;
        const LaneKind kind = host.laneKind(lane.target);
        const LaneStatus status = on ? runner.laneStatus(lane.target) : LaneStatus::Idle;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 2.0f, top + 4.0f));
        ImGui::PushID(static_cast<int>(li));
        bool muted = lane.muted;
        if (ImGui::Checkbox("##mute", &muted)) {
            timelineBeginEdit();
            tl.lanes[li].muted = muted;
            timelineEndEdit();
        }
        ImGui::SetItemTooltip("%s", tr("Mute this lane"));
        ImGui::SameLine();
        const std::string label = targetLabelById(lane.target);
        const float nameWidth = kLabelWidth - 66.0f;
        ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + nameWidth, top + kLaneHeight), true);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable(label.c_str(), static_cast<int>(li) == view.selectLane, ImGuiSelectableFlags_None, ImVec2(nameWidth, 0.0f))) {
            view.selectLane = static_cast<int>(li);
            view.selectKey = -1;
            view.selectCue = -1;
        }
        ImGui::PopClipRect();
        ImGui::SetItemTooltip("%s", lane.target.c_str());
        ImGui::SetCursorScreenPos(ImVec2(origin.x + kLabelWidth - 26.0f, top + 4.0f));
        if (ImGui::SmallButton("x")) {
            timelineBeginEdit();
            removeLane(tl, li);
            timelineEndEdit();
            view.selectLane = -1;
            view.selectKey = -1;
            ImGui::PopID();
            break;
        }
        ImGui::SetItemTooltip("%s", tr("Remove this lane"));

        ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0f, top + 32.0f));
        const char* text = nullptr;
        ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        if (kind == LaneKind::Refused) {
            text = tr("Not available");
            color = kBad;
        } else if (lane.muted) {
            text = tr("Muted");
        } else if (!on) {
            text = kind == LaneKind::Toggle ? tr("On / off") : tr("Value");
        } else if (status == LaneStatus::Overridden) {
            text = tr("Moved by hand");
            color = kWarn;
        } else if (status == LaneStatus::Following) {
            text = tr("Following");
            color = kGood;
        } else {
            text = tr("Waiting");
        }
        ImGui::TextColored(color, "%s", text);
        if (on && status == LaneStatus::Overridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Release"))) {
                runner.release(lane.target);
            }
        }
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(geo.x0, origin.y));
    ImGui::InvisibleButton("##tlArea", ImVec2(areaWidth, totalHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;

    struct Hit {
        int region = 0;
        int lane = -1;
        int key = -1;
        int cue = -1;
        double seconds = 0.0;
        float value = 0.0f;
    };
    auto probe = [&](const ImVec2& at) {
        Hit hit;
        hit.seconds = clampSeconds(geo.toTime(at.x));
        if (at.y < cueTop) {
            hit.region = 1;
            return hit;
        }
        if (at.y < lanesTop) {
            hit.region = 2;
            float best = kHitRadius;
            for (size_t i = 0; i < cues.size(); ++i) {
                const float distance = std::fabs(geo.toX(cues[i].seconds) - at.x);
                if (distance <= best) {
                    best = distance;
                    hit.cue = static_cast<int>(i);
                }
            }
            return hit;
        }
        const int row = static_cast<int>((at.y - lanesTop) / kLaneHeight);
        if (row < 0 || row >= static_cast<int>(tl.lanes.size())) {
            hit.region = 0;
            return hit;
        }
        hit.region = 3;
        hit.lane = row;
        const float top = lanesTop + static_cast<float>(row) * kLaneHeight;
        const float innerTop = top + kLanePad;
        const float innerBottom = top + kLaneHeight - kLanePad;
        hit.value = std::clamp((innerBottom - at.y) / (innerBottom - innerTop), 0.0f, 1.0f);
        float best = kHitRadius * kHitRadius;
        const Lane& lane = tl.lanes[static_cast<size_t>(row)];
        for (size_t k = 0; k < lane.keys.size(); ++k) {
            const float x = geo.toX(lane.keys[k].seconds);
            const float y = innerBottom - lane.keys[k].value * (innerBottom - innerTop);
            const float distance = (x - at.x) * (x - at.x) + (y - at.y) * (y - at.y);
            if (distance <= best) {
                best = distance;
                hit.key = static_cast<int>(k);
            }
        }
        return hit;
    };

    if (hovered && !io.WantTextInput) {
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            const double anchorTime = geo.toTime(mouse.x);
            const float factor = std::pow(1.18f, io.MouseWheel);
            view.pixelsPerSecond = std::clamp(view.pixelsPerSecond * factor, kMinPixelsPerSecond, kMaxPixelsPerSecond);
            view.viewStart = std::clamp(anchorTime - static_cast<double>(mouse.x - geo.x0) / view.pixelsPerSecond, 0.0, kMaxTimelineSeconds);
        } else if (io.KeyShift && io.MouseWheel != 0.0f) {
            view.viewStart = std::clamp(view.viewStart - static_cast<double>(io.MouseWheel) * 90.0 / view.pixelsPerSecond, 0.0, kMaxTimelineSeconds);
        } else if (io.MouseWheel != 0.0f) {
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * 48.0f);
        }
        if (io.MouseWheelH != 0.0f) {
            view.viewStart = std::clamp(view.viewStart - static_cast<double>(io.MouseWheelH) * 90.0 / view.pixelsPerSecond, 0.0, kMaxTimelineSeconds);
        }
    }

    if (ImGui::IsItemActivated()) {
        const Hit hit = probe(mouse);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            view.drag = TimelineDrag::Pan;
            view.dragMouseStart = mouse.x;
            view.dragViewStart = view.viewStart;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const bool doubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (hit.region == 1) {
                if (io.KeyShift) {
                    view.drag = TimelineDrag::Loop;
                    view.dragAnchor = roundMillis(hit.seconds);
                    timelineBeginEdit();
                    setLoop(tl, true, view.dragAnchor, view.dragAnchor);
                } else {
                    view.drag = TimelineDrag::Scrub;
                    runner.seek(hit.seconds);
                }
            } else if (hit.region == 2) {
                view.selectLane = -1;
                view.selectKey = -1;
                if (hit.cue >= 0) {
                    view.selectCue = hit.cue;
                    view.drag = TimelineDrag::Cue;
                    view.dragAnchor = cues[static_cast<size_t>(hit.cue)].seconds - hit.seconds;
                    timelineBeginEdit();
                } else {
                    view.selectCue = -1;
                    if (doubleClick && cues.size() < kMaxCues) {
                        view.pickMode = 1;
                        view.pickSeconds = roundMillis(hit.seconds);
                        view.filter[0] = '\0';
                        view.openPick = true;
                    }
                }
            } else if (hit.region == 3) {
                view.selectLane = hit.lane;
                view.selectCue = -1;
                Lane& lane = tl.lanes[static_cast<size_t>(hit.lane)];
                if (hit.key >= 0) {
                    view.selectKey = hit.key;
                    view.drag = TimelineDrag::Key;
                    view.dragAnchor = lane.keys[static_cast<size_t>(hit.key)].seconds - hit.seconds;
                    timelineBeginEdit();
                } else {
                    view.selectKey = -1;
                    if (doubleClick && host.laneKind(lane.target) != LaneKind::Refused) {
                        Keyframe key;
                        key.seconds = roundMillis(hit.seconds);
                        key.value = host.laneKind(lane.target) == LaneKind::Toggle ? (hit.value >= 0.5f ? 1.0f : 0.0f) : hit.value;
                        key.shape = KeyShape::Linear;
                        timelineBeginEdit();
                        view.selectKey = insertKey(tl, static_cast<size_t>(hit.lane), key);
                        timelineEndEdit();
                    }
                }
            } else {
                view.selectLane = -1;
                view.selectKey = -1;
                view.selectCue = -1;
            }
        }
    }

    if (ImGui::IsItemActive()) {
        const double now = clampSeconds(geo.toTime(mouse.x));
        switch (view.drag) {
        case TimelineDrag::Scrub:
            runner.seek(now);
            break;
        case TimelineDrag::Loop: {
            const double a = std::min(view.dragAnchor, roundMillis(now));
            const double b = std::max(view.dragAnchor, roundMillis(now));
            setLoop(tl, true, a, b);
            break;
        }
        case TimelineDrag::Key:
            if (view.selectLane >= 0 && view.selectLane < static_cast<int>(tl.lanes.size()) && view.selectKey >= 0 && view.selectKey < static_cast<int>(tl.lanes[static_cast<size_t>(view.selectLane)].keys.size())) {
                const size_t laneIndex = static_cast<size_t>(view.selectLane);
                const Keyframe current = tl.lanes[laneIndex].keys[static_cast<size_t>(view.selectKey)];
                const float top = lanesTop + static_cast<float>(laneIndex) * kLaneHeight;
                const float innerTop = top + kLanePad;
                const float innerBottom = top + kLaneHeight - kLanePad;
                float value = std::clamp((innerBottom - mouse.y) / (innerBottom - innerTop), 0.0f, 1.0f);
                if (host.laneKind(tl.lanes[laneIndex].target) == LaneKind::Toggle) {
                    value = value >= 0.5f ? 1.0f : 0.0f;
                }
                double seconds = clampSeconds(roundMillis(now + view.dragAnchor));
                if (io.KeyShift) {
                    value = current.value;
                }
                if (io.KeyCtrl) {
                    seconds = current.seconds;
                }
                view.selectKey = moveKey(tl, laneIndex, static_cast<size_t>(view.selectKey), seconds, value);
            }
            break;
        case TimelineDrag::Cue:
            if (view.selectCue >= 0 && view.selectCue < static_cast<int>(cues.size())) {
                view.selectCue = moveCue(cues, static_cast<size_t>(view.selectCue), now + view.dragAnchor);
                sync_.markCuesDirty();
            }
            break;
        case TimelineDrag::Pan:
            view.viewStart = std::clamp(view.dragViewStart - static_cast<double>(mouse.x - view.dragMouseStart) / geo.pps, 0.0, kMaxTimelineSeconds);
            break;
        default:
            break;
        }
    }
    if (ImGui::IsItemDeactivated()) {
        view.drag = TimelineDrag::None;
        timelineEndEdit();
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const Hit hit = probe(mouse);
        view.menuLane = hit.lane;
        view.menuKey = hit.key;
        view.menuCue = hit.cue;
        view.menuSeconds = roundMillis(hit.seconds);
        view.menuValue = hit.value;
        if (hit.region == 3 && hit.key >= 0) {
            view.selectLane = hit.lane;
            view.selectKey = hit.key;
            view.selectCue = -1;
            ImGui::OpenPopup("##tlKeyMenu");
        } else if (hit.region == 3) {
            view.selectLane = hit.lane;
            ImGui::OpenPopup("##tlLaneMenu");
        } else if (hit.region == 2 && hit.cue >= 0) {
            view.selectCue = hit.cue;
            view.selectKey = -1;
            ImGui::OpenPopup("##tlCueMenu");
        } else if (hit.region == 2) {
            ImGui::OpenPopup("##tlCueAddMenu");
        } else if (hit.region == 1) {
            ImGui::OpenPopup("##tlRulerMenu");
        }
    }

    if (ImGui::BeginPopup("##tlKeyMenu")) {
        if (view.menuLane >= 0 && view.menuLane < static_cast<int>(tl.lanes.size()) && view.menuKey >= 0 && view.menuKey < static_cast<int>(tl.lanes[static_cast<size_t>(view.menuLane)].keys.size())) {
            const size_t laneIndex = static_cast<size_t>(view.menuLane);
            const size_t keyIndex = static_cast<size_t>(view.menuKey);
            const KeyShape shape = tl.lanes[laneIndex].keys[keyIndex].shape;
            for (KeyShape candidate : {KeyShape::Linear, KeyShape::Smooth, KeyShape::Hold}) {
                if (ImGui::MenuItem(shapeLabel(candidate), nullptr, shape == candidate)) {
                    timelineBeginEdit();
                    setKeyShape(tl, laneIndex, keyIndex, candidate);
                    timelineEndEdit();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Delete keyframe"))) {
                timelineBeginEdit();
                removeKey(tl, laneIndex, keyIndex);
                timelineEndEdit();
                view.selectKey = -1;
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##tlLaneMenu")) {
        if (view.menuLane >= 0 && view.menuLane < static_cast<int>(tl.lanes.size())) {
            const Lane& lane = tl.lanes[static_cast<size_t>(view.menuLane)];
            const bool allowed = host.laneKind(lane.target) != LaneKind::Refused;
            if (ImGui::MenuItem(tr("Add keyframe here"), nullptr, false, allowed)) {
                Keyframe key;
                key.seconds = view.menuSeconds;
                key.value = host.laneKind(lane.target) == LaneKind::Toggle ? (view.menuValue >= 0.5f ? 1.0f : 0.0f) : view.menuValue;
                timelineBeginEdit();
                view.selectKey = insertKey(tl, static_cast<size_t>(view.menuLane), key);
                timelineEndEdit();
            }
            if (ImGui::MenuItem(tr("Add keyframe at the playhead"), nullptr, false, allowed && on)) {
                Keyframe key;
                key.seconds = clampSeconds(roundMillis(runner.position()));
                float current = 0.0f;
                host.read(lane.target, current);
                key.value = current;
                timelineBeginEdit();
                view.selectKey = insertKey(tl, static_cast<size_t>(view.menuLane), key);
                timelineEndEdit();
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##tlCueMenu")) {
        if (view.menuCue >= 0 && view.menuCue < static_cast<int>(cues.size())) {
            const Cue cue = cues[static_cast<size_t>(view.menuCue)];
            if (ImGui::MenuItem(tr("Fire this cue now"))) {
                control_.fireTarget(cue.target, cue.value);
            }
            if (ImGui::MenuItem(tr("Delete cue"))) {
                timelineBeginEdit();
                cues.erase(cues.begin() + view.menuCue);
                sync_.markCuesDirty();
                timelineEndEdit();
                view.selectCue = -1;
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##tlCueAddMenu")) {
        if (ImGui::MenuItem(tr("Add cue here"), nullptr, false, cues.size() < kMaxCues)) {
            view.pickMode = 1;
            view.pickSeconds = view.menuSeconds;
            view.filter[0] = '\0';
            view.openPick = true;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##tlRulerMenu")) {
        const bool enabledLoop = tl.loop.enabled;
        if (ImGui::MenuItem(tr("Set loop start here"))) {
            timelineBeginEdit();
            const double end = std::max(tl.loop.end, view.menuSeconds + 1.0);
            setLoop(tl, enabledLoop, view.menuSeconds, end);
            timelineEndEdit();
        }
        if (ImGui::MenuItem(tr("Set loop end here"))) {
            timelineBeginEdit();
            setLoop(tl, enabledLoop, std::min(tl.loop.start, view.menuSeconds), view.menuSeconds);
            timelineEndEdit();
        }
        if (ImGui::MenuItem(tr("Clear the loop"))) {
            timelineBeginEdit();
            setLoop(tl, false, 0.0, 0.0);
            timelineEndEdit();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Move the playhead here"), nullptr, false, on)) {
            runner.seek(view.menuSeconds);
        }
        ImGui::EndPopup();
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + totalHeight));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
}

void App::drawTimeline() {
    if (!timelineOpen_) {
        timelineHasFocus_ = false;
        if (timelineView_.editing) {
            timelineEndEdit();
        }
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(1180.0f, viewport->WorkSize.x * 0.9f), std::min(560.0f, viewport->WorkSize.y * 0.7f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.62f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (timelineFocus_) {
        ImGui::SetNextWindowFocus();
        timelineFocus_ = false;
    }
    const std::string title = std::string(tr("Show timeline")) + "###timeline";
    if (!ImGui::Begin(title.c_str(), &timelineOpen_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        timelineHasFocus_ = false;
        return;
    }
    timelineHasFocus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGuiIO& io = ImGui::GetIO();
    TimelineView& view = timelineView_;
    TimelineRunner& runner = timeline_.runner();
    ShowTimeline& tl = timeline_.timeline();

    if (timelineHasFocus_ && !io.WantTextInput && view.drag == TimelineDrag::None) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                timelineRedo();
            } else {
                timelineUndo();
            }
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            timelineRedo();
        } else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false) && settings_.timelineEnabled) {
            runner.toggle();
        } else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Home, false) && settings_.timelineEnabled) {
            runner.stop();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            if (view.selectLane >= 0 && view.selectLane < static_cast<int>(tl.lanes.size()) && view.selectKey >= 0) {
                timelineBeginEdit();
                removeKey(tl, static_cast<size_t>(view.selectLane), static_cast<size_t>(view.selectKey));
                timelineEndEdit();
                view.selectKey = -1;
            } else if (view.selectCue >= 0 && view.selectCue < static_cast<int>(sync_.cues().size())) {
                timelineBeginEdit();
                sync_.cues().erase(sync_.cues().begin() + view.selectCue);
                sync_.markCuesDirty();
                timelineEndEdit();
                view.selectCue = -1;
            }
        }
    }

    drawTimelineToolbar();
    if (!settings_.timelineEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("%s", tr("The timeline is off. You can prepare lanes and keyframes, but nothing runs until you switch it on."));
        ImGui::PopStyleColor();
    }
    drawTimelineCanvas();
    if (view.openPick) {
        view.openPick = false;
        ImGui::OpenPopup("##tlPick");
    }
    drawTimelineTargetPicker();

    if (view.selectLane >= 0 && view.selectLane < static_cast<int>(tl.lanes.size()) && view.selectKey >= 0 && view.selectKey < static_cast<int>(tl.lanes[static_cast<size_t>(view.selectLane)].keys.size())) {
        const size_t laneIndex = static_cast<size_t>(view.selectLane);
        const size_t keyIndex = static_cast<size_t>(view.selectKey);
        const Keyframe key = tl.lanes[laneIndex].keys[keyIndex];
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("Keyframe"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        double seconds = key.seconds;
        bool timeChanged = ImGui::InputDouble("##keyTime", &seconds, 0.0, 0.0, "%.3f s", ImGuiInputTextFlags_EnterReturnsTrue);
        timeChanged = ImGui::IsItemDeactivatedAfterEdit() || timeChanged;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        float value = key.value;
        const bool valueChanged = ui::sliderFloat("##keyValue", &value, 0.0f, 1.0f, "%.3f");
        const bool valueDone = ImGui::IsItemDeactivated();
        if (timeChanged || valueChanged) {
            timelineBeginEdit();
            view.selectKey = moveKey(tl, laneIndex, keyIndex, clampSeconds(seconds), std::clamp(value, 0.0f, 1.0f));
        }
        if ((timeChanged && !valueChanged) || valueDone) {
            timelineEndEdit();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("##keyShape", shapeLabel(key.shape))) {
            for (KeyShape candidate : {KeyShape::Linear, KeyShape::Smooth, KeyShape::Hold}) {
                if (ImGui::Selectable(shapeLabel(candidate), candidate == key.shape)) {
                    timelineBeginEdit();
                    setKeyShape(tl, laneIndex, keyIndex, candidate);
                    timelineEndEdit();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("The shape applies from this keyframe to the next one. On / off controls always switch at the keyframe."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete keyframe"))) {
            timelineBeginEdit();
            removeKey(tl, laneIndex, keyIndex);
            timelineEndEdit();
            view.selectKey = -1;
        }
    } else if (view.selectCue >= 0 && view.selectCue < static_cast<int>(sync_.cues().size())) {
        std::vector<Cue>& cues = sync_.cues();
        const size_t cueIndex = static_cast<size_t>(view.selectCue);
        const Cue cue = cues[cueIndex];
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("Cue"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        double seconds = cue.seconds;
        bool timeChanged = ImGui::InputDouble("##cueTime", &seconds, 0.0, 0.0, "%.3f s", ImGuiInputTextFlags_EnterReturnsTrue);
        timeChanged = ImGui::IsItemDeactivatedAfterEdit() || timeChanged;
        if (timeChanged) {
            timelineBeginEdit();
            view.selectCue = moveCue(cues, cueIndex, seconds);
            sync_.markCuesDirty();
            timelineEndEdit();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(targetLabelById(cue.target).c_str());
        const int cueTargetIndex = controlTargetIndex(cue.target);
        const bool continuous = cueTargetIndex >= 0 && controlTargets()[static_cast<size_t>(cueTargetIndex)].kind == ControlKind::Continuous;
        if (continuous && view.selectCue >= 0) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            float value = cues[static_cast<size_t>(view.selectCue)].value;
            if (ui::sliderFloat("##cueValue", &value, 0.0f, 1.0f, "%.2f")) {
                timelineBeginEdit();
                cues[static_cast<size_t>(view.selectCue)].value = value;
                sync_.markCuesDirty();
            }
            if (ImGui::IsItemDeactivated()) {
                timelineEndEdit();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Fire this cue now"))) {
            control_.fireTarget(cue.target, cue.value);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete cue"))) {
            timelineBeginEdit();
            cues.erase(cues.begin() + view.selectCue);
            sync_.markCuesDirty();
            timelineEndEdit();
            view.selectCue = -1;
        }
    } else {
        ImGui::TextDisabled("%s", tr("Click a keyframe or a cue to edit it. Ctrl + wheel zooms, Shift + wheel scrolls, Space plays and pauses."));
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    double maxStart = 60.0;
    for (const Lane& lane : tl.lanes) {
        if (!lane.keys.empty()) {
            maxStart = std::max(maxStart, lane.keys.back().seconds + 30.0);
        }
    }
    if (!sync_.cues().empty()) {
        maxStart = std::max(maxStart, sync_.cues().back().seconds + 30.0);
    }
    maxStart = std::max({maxStart, runner.position() + 30.0, view.viewStart});
    const double minStart = 0.0;
    ImGui::SliderScalar("##tlView", ImGuiDataType_Double, &view.viewStart, &minStart, &maxStart, "%.0f s");
    ImGui::SetItemTooltip("%s", tr("Scrolls the view"));

    if (view.showCounters) {
        const std::string counters = std::string(tr("Jumps")) + " " + std::to_string(runner.seeks()) + "   " + tr("Cues skipped") + " " + std::to_string(runner.cuesSkipped()) + "   " + tr("Cues fired") + " " + std::to_string(runner.cuesFired()) + "   " + tr("Loop passes") + " " + std::to_string(runner.loopWraps()) + "   " + tr("Source changes") + " " + std::to_string(runner.clockChanges()) + "   " + tr("Timecode lost") + " " + std::to_string(runner.timecodeLosses()) + "   " + tr("Hand moves") + " " + std::to_string(runner.overrides()) + "   " + tr("Writes") + " " + std::to_string(runner.writes());
        ImGui::TextDisabled("%s", counters.c_str());
    }

    if (view.editing && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        timelineEndEdit();
    }
    ImGui::End();
}

}

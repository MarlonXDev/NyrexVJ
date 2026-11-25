#pragma once

#include "core/show_timeline.hpp"

namespace nvj {

enum class TimelineDrag { None, Scrub, Loop, Key, Cue, Pan };

struct TimelineView {
    double viewStart = 0.0;
    float pixelsPerSecond = 40.0f;
    bool followPlayhead = true;
    int selectLane = -1;
    int selectKey = -1;
    int selectCue = -1;
    TimelineDrag drag = TimelineDrag::None;
    double dragAnchor = 0.0;
    float dragMouseStart = 0.0f;
    double dragViewStart = 0.0;
    bool editing = false;
    TimelineDoc before;
    int menuLane = -1;
    int menuKey = -1;
    int menuCue = -1;
    double menuSeconds = 0.0;
    float menuValue = 0.5f;
    int pickMode = 0;
    double pickSeconds = 0.0;
    char filter[64] = {};
    bool showCounters = false;
    bool openPick = false;
};

}

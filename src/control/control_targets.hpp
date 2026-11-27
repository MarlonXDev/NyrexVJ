#pragma once

#include "core/composition.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace nvj {

enum class ControlKind { Continuous, Toggle, Trigger };

enum class ControlItem {
    Level,
    Blackout,
    Bpm,
    Tap,
    Resync,
    Hue,
    Saturation,
    Brightness,
    Contrast,
    Opacity,
    Speed,
    Scale,
    Bypass,
    Solo,
    ClearLayer,
    ClipTrigger,
    ColumnTrigger,
    DeckSelect,
    DeckNext,
    DeckPrevious,
    EffectEnable,
    EffectParam,
    Macro,
    FeedbackFreeze,
    FeedbackReset
};

struct ControlTarget {
    std::string id;
    ControlKind kind = ControlKind::Continuous;
    ControlItem item = ControlItem::Level;
    int layer = -1;
    int index = 0;
    int slot = 0;
    int param = 0;
};

const std::vector<ControlTarget>& controlTargets();
int controlTargetIndex(const std::string& id);

class ControlHost {
public:
    virtual ~ControlHost() = default;
    virtual void triggerClip(int layer, int column) = 0;
    virtual void triggerColumn(int column) = 0;
    virtual void clearLayer(int layer) = 0;
    virtual void switchDeck(int index) = 0;
    virtual void tap() = 0;
    virtual void resync() = 0;
    virtual bool blackout() const = 0;
    virtual void setBlackout(bool on) = 0;
    virtual bool feedbackFrozen() const { return false; }
    virtual void setFeedbackFrozen(bool) {}
    virtual void resetFeedback() {}
};

class ControlRouter {
public:
    ControlRouter(Composition& comp, ControlHost& host);

    bool set(int target, float value, bool edgeOnly);
    bool toggle(int target);
    bool state(int target) const;
    bool lit(int target) const;
    float value(int target) const;

private:
    void setContinuous(const ControlTarget& target, float value);
    void setToggle(const ControlTarget& target, bool on);
    void fire(const ControlTarget& target);
    ColorGrade& gradeFor(const ControlTarget& target);
    EffectSlot& slotFor(const ControlTarget& target);
    const EffectSlot& slotFor(const ControlTarget& target) const;

    Composition& comp_;
    ControlHost& host_;
    std::unordered_map<int, float> previous_;
};

}

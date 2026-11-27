#pragma once

#include "control/control_targets.hpp"
#include "render/engine.hpp"

namespace nvj {

class EngineControlHost final : public ControlHost {
public:
    explicit EngineControlHost(Engine& engine) : engine_(engine) {}

    void triggerClip(int layer, int column) override { engine_.requestTrigger(layer, column); }
    void triggerColumn(int column) override { engine_.requestTriggerColumn(column); }
    void clearLayer(int layer) override { engine_.clearLayer(layer); }
    void switchDeck(int index) override { engine_.requestSwitchDeck(index); }
    void tap() override { engine_.tap(); }
    void resync() override { engine_.resync(); }
    bool blackout() const override { return engine_.blackout; }
    void setBlackout(bool on) override { engine_.blackout = on; }
    bool feedbackFrozen() const override { return engine_.feedbackFrozen(); }
    void setFeedbackFrozen(bool on) override { engine_.setFeedbackFrozen(on); }
    void resetFeedback() override { engine_.resetFeedback(); }

private:
    Engine& engine_;
};

}

#pragma once

#include "control/control_targets.hpp"
#include "render/engine.hpp"

namespace nvj {

class EngineControlHost final : public ControlHost {
public:
    explicit EngineControlHost(Engine& engine) : engine_(engine) {}

    void triggerClip(int layer, int column) override { engine_.trigger(layer, column); }
    void triggerColumn(int column) override { engine_.triggerColumn(column); }
    void clearLayer(int layer) override { engine_.clearLayer(layer); }
    void switchDeck(int index) override { engine_.switchDeck(index); }
    void tap() override { engine_.tap(); }
    void resync() override { engine_.resync(); }
    bool blackout() const override { return engine_.blackout; }
    void setBlackout(bool on) override { engine_.blackout = on; }

private:
    Engine& engine_;
};

}

#include "control/control_targets.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

std::string number(int value) {
    return std::to_string(value);
}

ControlTarget make(const std::string& id, ControlKind kind, ControlItem item, int layer = -1, int index = 0, int slot = 0, int param = 0) {
    ControlTarget target;
    target.id = id;
    target.kind = kind;
    target.item = item;
    target.layer = layer;
    target.index = index;
    target.slot = slot;
    target.param = param;
    return target;
}

void addGrade(std::vector<ControlTarget>& list, const std::string& prefix, int layer) {
    list.push_back(make(prefix + "hue", ControlKind::Continuous, ControlItem::Hue, layer));
    list.push_back(make(prefix + "saturation", ControlKind::Continuous, ControlItem::Saturation, layer));
    list.push_back(make(prefix + "brightness", ControlKind::Continuous, ControlItem::Brightness, layer));
    list.push_back(make(prefix + "contrast", ControlKind::Continuous, ControlItem::Contrast, layer));
}

void addEffects(std::vector<ControlTarget>& list, const std::string& prefix, int layer) {
    for (int slot = 0; slot < kEffectSlots; ++slot) {
        const std::string base = prefix + "fx/" + number(slot + 1) + "/";
        list.push_back(make(base + "enable", ControlKind::Toggle, ControlItem::EffectEnable, layer, 0, slot));
        for (int param = 0; param < kParamCount; ++param) {
            list.push_back(make(base + "param/" + number(param + 1), ControlKind::Continuous, ControlItem::EffectParam, layer, 0, slot, param));
        }
    }
}

std::vector<ControlTarget> buildTargets() {
    std::vector<ControlTarget> list;
    list.push_back(make("master/level", ControlKind::Continuous, ControlItem::Level));
    list.push_back(make("master/blackout", ControlKind::Toggle, ControlItem::Blackout));
    list.push_back(make("master/bpm", ControlKind::Continuous, ControlItem::Bpm));
    list.push_back(make("master/tap", ControlKind::Trigger, ControlItem::Tap));
    list.push_back(make("master/resync", ControlKind::Trigger, ControlItem::Resync));
    addGrade(list, "master/", -1);
    for (int macro = 0; macro < kMacroCount; ++macro) {
        list.push_back(make("macro/" + number(macro + 1), ControlKind::Continuous, ControlItem::Macro, -1, macro));
    }
    for (int deck = 0; deck < kDeckCount; ++deck) {
        list.push_back(make("deck/" + number(deck + 1), ControlKind::Trigger, ControlItem::DeckSelect, -1, deck));
    }
    list.push_back(make("deck/next", ControlKind::Trigger, ControlItem::DeckNext));
    list.push_back(make("deck/previous", ControlKind::Trigger, ControlItem::DeckPrevious));
    for (int column = 0; column < kColumnCount; ++column) {
        list.push_back(make("column/" + number(column + 1), ControlKind::Trigger, ControlItem::ColumnTrigger, -1, column));
    }
    for (int layer = 0; layer < kLayerCount; ++layer) {
        const std::string prefix = "layer/" + number(layer + 1) + "/";
        list.push_back(make(prefix + "opacity", ControlKind::Continuous, ControlItem::Opacity, layer));
        list.push_back(make(prefix + "speed", ControlKind::Continuous, ControlItem::Speed, layer));
        list.push_back(make(prefix + "scale", ControlKind::Continuous, ControlItem::Scale, layer));
        list.push_back(make(prefix + "camera-orbit", ControlKind::Continuous, ControlItem::CameraOrbit, layer));
        list.push_back(make(prefix + "camera-tilt", ControlKind::Continuous, ControlItem::CameraTilt, layer));
        list.push_back(make(prefix + "camera-fov", ControlKind::Continuous, ControlItem::CameraFov, layer));
        list.push_back(make(prefix + "world-x", ControlKind::Continuous, ControlItem::WorldX, layer));
        list.push_back(make(prefix + "world-y", ControlKind::Continuous, ControlItem::WorldY, layer));
        list.push_back(make(prefix + "world-z", ControlKind::Continuous, ControlItem::WorldZ, layer));
        list.push_back(make(prefix + "world-yaw", ControlKind::Continuous, ControlItem::WorldYaw, layer));
        addGrade(list, prefix, layer);
        list.push_back(make(prefix + "bypass", ControlKind::Toggle, ControlItem::Bypass, layer));
        list.push_back(make(prefix + "solo", ControlKind::Toggle, ControlItem::Solo, layer));
        list.push_back(make(prefix + "clear", ControlKind::Trigger, ControlItem::ClearLayer, layer));
        for (int column = 0; column < kColumnCount; ++column) {
            list.push_back(make(prefix + "clip/" + number(column + 1), ControlKind::Trigger, ControlItem::ClipTrigger, layer, column));
        }
        addEffects(list, prefix, layer);
    }
    addEffects(list, "master/", -1);
    list.push_back(make("master/feedback-freeze", ControlKind::Toggle, ControlItem::FeedbackFreeze));
    list.push_back(make("master/feedback-reset", ControlKind::Trigger, ControlItem::FeedbackReset));
    return list;
}

float lerp(float low, float high, float t) {
    return low + (high - low) * t;
}

float logRange(float low, float high, float t) {
    return low * std::pow(high / low, t);
}

float unlerp(float low, float high, float value) {
    return std::clamp((value - low) / (high - low), 0.0f, 1.0f);
}

float unlog(float low, float high, float value) {
    return std::clamp(std::log(std::max(value, low) / low) / std::log(high / low), 0.0f, 1.0f);
}

}

const std::vector<ControlTarget>& controlTargets() {
    static const std::vector<ControlTarget> targets = buildTargets();
    return targets;
}

int controlTargetIndex(const std::string& id) {
    static const std::unordered_map<std::string, int> lookup = [] {
        std::unordered_map<std::string, int> map;
        const auto& list = controlTargets();
        for (size_t i = 0; i < list.size(); ++i) {
            map.emplace(list[i].id, static_cast<int>(i));
        }
        return map;
    }();
    auto it = lookup.find(id);
    return it == lookup.end() ? -1 : it->second;
}

ControlRouter::ControlRouter(Composition& comp, ControlHost& host) : comp_(comp), host_(host) {}

ColorGrade& ControlRouter::gradeFor(const ControlTarget& target) {
    return target.layer >= 0 ? comp_.layers[static_cast<size_t>(target.layer)].grade : comp_.master.grade;
}

EffectSlot& ControlRouter::slotFor(const ControlTarget& target) {
    auto& slots = target.layer >= 0 ? comp_.layers[static_cast<size_t>(target.layer)].fx : comp_.master.fx;
    return slots[static_cast<size_t>(target.slot)];
}

const EffectSlot& ControlRouter::slotFor(const ControlTarget& target) const {
    const auto& slots = target.layer >= 0 ? comp_.layers[static_cast<size_t>(target.layer)].fx : comp_.master.fx;
    return slots[static_cast<size_t>(target.slot)];
}

bool ControlRouter::set(int index, float value, bool edgeOnly) {
    const auto& targets = controlTargets();
    if (index < 0 || index >= static_cast<int>(targets.size())) {
        return false;
    }
    const ControlTarget& target = targets[static_cast<size_t>(index)];
    if (std::isnan(value)) {
        return false;
    }
    value = std::clamp(value, 0.0f, 1.0f);
    switch (target.kind) {
    case ControlKind::Continuous:
        setContinuous(target, value);
        break;
    case ControlKind::Toggle:
        setToggle(target, value >= 0.5f);
        break;
    case ControlKind::Trigger: {
        float& previous = previous_[index];
        const bool rising = previous < 0.5f;
        previous = value;
        if (value >= 0.5f && (!edgeOnly || rising)) {
            fire(target);
        }
        break;
    }
    }
    return true;
}

bool ControlRouter::state(int index) const {
    const auto& targets = controlTargets();
    if (index < 0 || index >= static_cast<int>(targets.size())) {
        return false;
    }
    const ControlTarget& target = targets[static_cast<size_t>(index)];
    switch (target.item) {
    case ControlItem::Blackout:
        return host_.blackout();
    case ControlItem::FeedbackFreeze:
        return host_.feedbackFrozen();
    case ControlItem::Bypass:
        return comp_.layers[static_cast<size_t>(target.layer)].bypass;
    case ControlItem::Solo:
        return comp_.layers[static_cast<size_t>(target.layer)].solo;
    case ControlItem::EffectEnable:
        return slotFor(target).enabled;
    default:
        return false;
    }
}

bool ControlRouter::lit(int index) const {
    const auto& targets = controlTargets();
    if (index < 0 || index >= static_cast<int>(targets.size())) {
        return false;
    }
    const ControlTarget& target = targets[static_cast<size_t>(index)];
    switch (target.item) {
    case ControlItem::ClipTrigger: {
        const Layer& layer = comp_.layers[static_cast<size_t>(std::clamp(target.layer, 0, kLayerCount - 1))];
        const int column = std::clamp(target.index, 0, kColumnCount - 1);
        return layer.activeColumn == column && layer.cells[static_cast<size_t>(column)] >= 0;
    }
    case ControlItem::ColumnTrigger: {
        const int column = std::clamp(target.index, 0, kColumnCount - 1);
        for (const Layer& layer : comp_.layers) {
            if (layer.activeColumn == column && layer.cells[static_cast<size_t>(column)] >= 0) {
                return true;
            }
        }
        return false;
    }
    case ControlItem::DeckSelect:
        return comp_.activeDeck == target.index;
    default:
        return target.kind == ControlKind::Toggle && state(index);
    }
}

float ControlRouter::value(int index) const {
    const auto& targets = controlTargets();
    if (index < 0 || index >= static_cast<int>(targets.size())) {
        return 0.0f;
    }
    const ControlTarget& target = targets[static_cast<size_t>(index)];
    if (target.kind == ControlKind::Trigger) {
        return 0.0f;
    }
    if (target.kind == ControlKind::Toggle) {
        return state(index) ? 1.0f : 0.0f;
    }
    const ColorGrade& grade = target.layer >= 0 ? comp_.layers[static_cast<size_t>(target.layer)].grade : comp_.master.grade;
    switch (target.item) {
    case ControlItem::Level:
        return std::clamp(comp_.master.level, 0.0f, 1.0f);
    case ControlItem::Bpm:
        return unlerp(40.0f, 240.0f, comp_.bpm);
    case ControlItem::Hue:
        return unlerp(-0.5f, 0.5f, grade.hue);
    case ControlItem::Saturation:
        return unlerp(0.0f, 2.0f, grade.saturation);
    case ControlItem::Brightness:
        return unlerp(-0.5f, 0.5f, grade.brightness);
    case ControlItem::Contrast:
        return unlerp(0.5f, 2.0f, grade.contrast);
    case ControlItem::Opacity:
        return std::clamp(comp_.layers[static_cast<size_t>(target.layer)].opacity, 0.0f, 1.0f);
    case ControlItem::Speed:
        return unlog(0.1f, 4.0f, comp_.layers[static_cast<size_t>(target.layer)].speed);
    case ControlItem::Scale:
        return unlog(0.25f, 4.0f, comp_.layers[static_cast<size_t>(target.layer)].scale);
    case ControlItem::CameraOrbit:
        return unlerp(-180.0f, 180.0f, comp_.layers[static_cast<size_t>(target.layer)].cameraOrbit);
    case ControlItem::CameraTilt:
        return unlerp(-60.0f, 60.0f, comp_.layers[static_cast<size_t>(target.layer)].cameraTilt);
    case ControlItem::CameraFov:
        return unlerp(-30.0f, 30.0f, comp_.layers[static_cast<size_t>(target.layer)].cameraFov);
    case ControlItem::WorldX:
        return unlerp(-4.0f, 4.0f, comp_.layers[static_cast<size_t>(target.layer)].worldX);
    case ControlItem::WorldY:
        return unlerp(-4.0f, 4.0f, comp_.layers[static_cast<size_t>(target.layer)].worldY);
    case ControlItem::WorldZ:
        return unlerp(-4.0f, 4.0f, comp_.layers[static_cast<size_t>(target.layer)].worldZ);
    case ControlItem::WorldYaw:
        return unlerp(-180.0f, 180.0f, comp_.layers[static_cast<size_t>(target.layer)].worldYaw);
    case ControlItem::EffectParam:
        return std::clamp(slotFor(target).params[static_cast<size_t>(target.param)], 0.0f, 1.0f);
    case ControlItem::Macro:
        return std::clamp(comp_.modulators.macros[static_cast<size_t>(std::clamp(target.index, 0, kMacroCount - 1))], 0.0f, 1.0f);
    default:
        return 0.0f;
    }
}

bool ControlRouter::toggle(int index) {
    const auto& targets = controlTargets();
    if (index < 0 || index >= static_cast<int>(targets.size()) || targets[static_cast<size_t>(index)].kind != ControlKind::Toggle) {
        return false;
    }
    return set(index, state(index) ? 0.0f : 1.0f, false);
}

void ControlRouter::setToggle(const ControlTarget& target, bool on) {
    switch (target.item) {
    case ControlItem::Blackout:
        host_.setBlackout(on);
        break;
    case ControlItem::FeedbackFreeze:
        host_.setFeedbackFrozen(on);
        break;
    case ControlItem::Bypass:
        comp_.layers[static_cast<size_t>(target.layer)].bypass = on;
        break;
    case ControlItem::Solo:
        comp_.layers[static_cast<size_t>(target.layer)].solo = on;
        break;
    case ControlItem::EffectEnable:
        slotFor(target).enabled = on;
        break;
    default:
        break;
    }
}

void ControlRouter::setContinuous(const ControlTarget& target, float value) {
    switch (target.item) {
    case ControlItem::Level:
        comp_.master.level = value;
        break;
    case ControlItem::Bpm:
        comp_.bpm = lerp(40.0f, 240.0f, value);
        break;
    case ControlItem::Hue:
        gradeFor(target).hue = lerp(-0.5f, 0.5f, value);
        break;
    case ControlItem::Saturation:
        gradeFor(target).saturation = lerp(0.0f, 2.0f, value);
        break;
    case ControlItem::Brightness:
        gradeFor(target).brightness = lerp(-0.5f, 0.5f, value);
        break;
    case ControlItem::Contrast:
        gradeFor(target).contrast = lerp(0.5f, 2.0f, value);
        break;
    case ControlItem::Opacity:
        comp_.layers[static_cast<size_t>(target.layer)].opacity = value;
        break;
    case ControlItem::Speed:
        comp_.layers[static_cast<size_t>(target.layer)].speed = logRange(0.1f, 4.0f, value);
        break;
    case ControlItem::Scale:
        comp_.layers[static_cast<size_t>(target.layer)].scale = logRange(0.25f, 4.0f, value);
        break;
    case ControlItem::CameraOrbit:
        comp_.layers[static_cast<size_t>(target.layer)].cameraOrbit = lerp(-180.0f, 180.0f, value);
        break;
    case ControlItem::CameraTilt:
        comp_.layers[static_cast<size_t>(target.layer)].cameraTilt = lerp(-60.0f, 60.0f, value);
        break;
    case ControlItem::CameraFov:
        comp_.layers[static_cast<size_t>(target.layer)].cameraFov = lerp(-30.0f, 30.0f, value);
        break;
    case ControlItem::WorldX:
        comp_.layers[static_cast<size_t>(target.layer)].worldX = lerp(-4.0f, 4.0f, value);
        break;
    case ControlItem::WorldY:
        comp_.layers[static_cast<size_t>(target.layer)].worldY = lerp(-4.0f, 4.0f, value);
        break;
    case ControlItem::WorldZ:
        comp_.layers[static_cast<size_t>(target.layer)].worldZ = lerp(-4.0f, 4.0f, value);
        break;
    case ControlItem::WorldYaw:
        comp_.layers[static_cast<size_t>(target.layer)].worldYaw = lerp(-180.0f, 180.0f, value);
        break;
    case ControlItem::EffectParam:
        slotFor(target).params[static_cast<size_t>(target.param)] = value;
        break;
    case ControlItem::Macro:
        comp_.modulators.macros[static_cast<size_t>(std::clamp(target.index, 0, kMacroCount - 1))] = std::clamp(value, 0.0f, 1.0f);
        break;
    default:
        break;
    }
}

void ControlRouter::fire(const ControlTarget& target) {
    switch (target.item) {
    case ControlItem::Tap:
        host_.tap();
        break;
    case ControlItem::Resync:
        host_.resync();
        break;
    case ControlItem::FeedbackReset:
        host_.resetFeedback();
        break;
    case ControlItem::ClipTrigger:
        host_.triggerClip(target.layer, target.index);
        break;
    case ControlItem::ColumnTrigger:
        host_.triggerColumn(target.index);
        break;
    case ControlItem::ClearLayer:
        host_.clearLayer(target.layer);
        break;
    case ControlItem::DeckSelect:
        host_.switchDeck(target.index);
        break;
    case ControlItem::DeckNext:
        host_.switchDeck(nextUsedDeck(comp_, comp_.activeDeck, 1));
        break;
    case ControlItem::DeckPrevious:
        host_.switchDeck(nextUsedDeck(comp_, comp_.activeDeck, -1));
        break;
    default:
        break;
    }
}

}

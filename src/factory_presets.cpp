#include "preset.hpp"

#include "clip_library.hpp"
#include "effect_library.hpp"

#include <initializer_list>

namespace nvj {

namespace {

void setClips(Layer& layer, std::initializer_list<const char*> names) {
    size_t column = 0;
    for (const char* name : names) {
        if (column < static_cast<size_t>(kColumnCount)) {
            layer.cells[column++] = name ? clipIndexByName(name) : -1;
        }
    }
}

void setFx(EffectSlot& slot, const char* name, std::initializer_list<float> values = {}) {
    setEffect(slot, effectIndexByName(name));
    size_t index = 0;
    for (float value : values) {
        if (index < static_cast<size_t>(kParamCount)) {
            slot.params[index++] = value;
        }
    }
}

void setActive(Layer& layer, int column) {
    layer.activeColumn = (column >= 0 && column < kColumnCount && layer.cells[static_cast<size_t>(column)] >= 0) ? column : -1;
}

Composition deck(const char* name, float bpm) {
    Composition comp;
    comp.name = name;
    comp.bpm = bpm;
    setClips(comp.layers[0], {"Plasma", "Tunnel", "Noise Flow", "Kaleido", "Julia", "Synthwave", "Starfield", "Cube Field 3D"});
    setClips(comp.layers[1], {"Grid Pulse", "Pulse", "Bars", "Equalizer", "Laser Fan", "Sphere Grid 3D", "Torus 3D", "Strobe"});
    setClips(comp.layers[2], {"Strobe", "Pulse", "Grid Pulse", "Laser Fan", "Starfield", "Bars", "Equalizer", "Noise Flow"});
    setClips(comp.layers[3], {"Pulse", "Strobe", "Laser Fan", "Starfield", "Grid Pulse", "Equalizer", "Bars", "Julia"});
    comp.layers[1].blend = BlendAdd;
    comp.layers[1].opacity = 0.6f;
    comp.layers[2].blend = BlendScreen;
    comp.layers[2].opacity = 0.5f;
    comp.layers[3].blend = BlendAdd;
    comp.layers[3].opacity = 0.4f;
    return comp;
}

Composition neonClub() {
    Composition comp = deck("Neon Club", 128.0f);
    setActive(comp.layers[0], 1);
    setActive(comp.layers[1], 0);
    setFx(comp.master.fx[0], "Glow", {0.6f, 0.5f, 0.25f});
    setFx(comp.master.fx[1], "Vignette", {0.45f, 0.6f, 0.2f});
    setFx(comp.master.fx[2], "RGB Split", {0.2f, 0.0f, 0.6f});
    return comp;
}

Composition acidRainbow() {
    Composition comp = deck("Acid Rainbow", 132.0f);
    comp.layers[0].rainbow = true;
    comp.layers[1].rainbow = true;
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.55f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 1);
    setFx(comp.master.fx[0], "Kaleidoscope", {0.35f, 0.0f, 0.5f, 0.6f});
    setFx(comp.master.fx[1], "Hue Cycle", {0.7f, 0.0f, 0.0f});
    setFx(comp.master.fx[2], "Glow", {0.3f, 0.45f, 0.45f});
    return comp;
}

Composition deepSpace() {
    Composition comp = deck("Deep Space", 100.0f);
    for (int l = 0; l < 2; ++l) {
        comp.layers[static_cast<size_t>(l)].colorA = {0.35f, 0.2f, 1.0f};
        comp.layers[static_cast<size_t>(l)].colorB = {0.0f, 0.85f, 0.9f};
    }
    comp.layers[0].rainbow = false;
    comp.layers[1].opacity = 0.85f;
    setActive(comp.layers[0], 6);
    setActive(comp.layers[1], 6);
    setFx(comp.master.fx[0], "Trails", {0.8f, 0.58f, 0.5f});
    setFx(comp.master.fx[1], "Glow", {0.6f, 0.55f, 0.2f});
    setFx(comp.master.fx[2], "Vignette", {0.7f, 0.6f, 0.0f});
    return comp;
}

Composition synthwaveNight() {
    Composition comp = deck("Synthwave Night", 110.0f);
    comp.layers[0].colorA = {1.0f, 0.2f, 0.55f};
    comp.layers[0].colorB = {0.2f, 0.85f, 1.0f};
    comp.layers[1].colorA = {1.0f, 0.2f, 0.55f};
    comp.layers[1].colorB = {0.2f, 0.85f, 1.0f};
    comp.layers[1].opacity = 0.5f;
    setActive(comp.layers[0], 5);
    setActive(comp.layers[1], 4);
    setFx(comp.master.fx[0], "Glow", {0.7f, 0.5f, 0.2f});
    setFx(comp.master.fx[1], "Scanlines", {0.5f, 0.3f, 0.5f});
    setFx(comp.master.fx[2], "RGB Split", {0.2f, 0.1f, 0.5f});
    setFx(comp.master.fx[3], "Vignette", {0.5f, 0.6f, 0.0f});
    return comp;
}

Composition laserShow() {
    Composition comp = deck("Laser Show", 138.0f);
    for (int l = 0; l < kLayerCount; ++l) {
        comp.layers[static_cast<size_t>(l)].colorA = {0.1f, 1.0f, 0.3f};
        comp.layers[static_cast<size_t>(l)].colorB = {1.0f, 0.1f, 0.8f};
    }
    comp.layers[0].opacity = 0.25f;
    comp.layers[1].blend = BlendNormal;
    comp.layers[1].opacity = 1.0f;
    comp.layers[2].blend = BlendAdd;
    comp.layers[2].opacity = 0.6f;
    setActive(comp.layers[0], 2);
    setActive(comp.layers[1], 4);
    setActive(comp.layers[2], 4);
    setFx(comp.master.fx[0], "Glow", {0.85f, 0.55f, 0.15f});
    setFx(comp.master.fx[1], "Trails", {0.75f, 0.5f, 0.5f});
    return comp;
}

Composition minimalMono() {
    Composition comp = deck("Minimal Mono", 120.0f);
    for (int l = 0; l < kLayerCount; ++l) {
        comp.layers[static_cast<size_t>(l)].colorA = {1.0f, 1.0f, 1.0f};
        comp.layers[static_cast<size_t>(l)].colorB = {0.35f, 0.35f, 0.35f};
    }
    setActive(comp.layers[0], 2);
    setActive(comp.layers[1], 1);
    comp.master.grade.saturation = 0.0f;
    setFx(comp.master.fx[0], "Scanlines", {0.35f, 0.25f, 0.5f});
    setFx(comp.master.fx[1], "Vignette", {0.5f, 0.6f, 0.0f});
    return comp;
}

Composition showcase3d() {
    Composition comp = deck("3D Showcase", 124.0f);
    comp.layers[0].colorA = {1.0f, 0.25f, 0.0f};
    comp.layers[0].colorB = {0.0f, 0.6f, 1.0f};
    comp.layers[1].colorA = {1.0f, 0.25f, 0.0f};
    comp.layers[1].colorB = {0.0f, 0.6f, 1.0f};
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.7f;
    setActive(comp.layers[0], 7);
    setActive(comp.layers[1], 6);
    setFx(comp.master.fx[0], "Glow", {0.5f, 0.5f, 0.3f});
    setFx(comp.master.fx[1], "RGB Split", {0.25f, 0.0f, 0.7f});
    return comp;
}

Composition warpTunnel() {
    Composition comp = deck("Warp Tunnel", 130.0f);
    comp.layers[0].rainbow = true;
    setActive(comp.layers[0], 0);
    setFx(comp.master.fx[0], "Tunnel Map", {0.35f, 0.5f, 0.8f, 0.55f});
    setFx(comp.master.fx[1], "Glow", {0.5f, 0.5f, 0.3f});
    setFx(comp.master.fx[2], "Zoom Pulse", {0.35f, 0.5f});
    return comp;
}

std::vector<Composition> buildFactory() {
    return {neonClub(), acidRainbow(), deepSpace(), synthwaveNight(), laserShow(), minimalMono(), showcase3d(), warpTunnel()};
}

}

const std::vector<Composition>& factoryPresets() {
    static const std::vector<Composition> presets = buildFactory();
    return presets;
}

int factoryPresetIndex(const std::string& name) {
    const std::vector<Composition>& presets = factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Composition defaultComposition() {
    return factoryPresets().front();
}

}

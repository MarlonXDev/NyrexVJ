#include "core/preset.hpp"

#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <initializer_list>
#include <string>
#include <vector>

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

void setMod(AudioMod& mod, int band, int target, float amount) {
    mod.band = band;
    mod.target = target;
    mod.amount = amount;
}

void setReact(EffectSlot& slot, int band, int param, float amount) {
    slot.reactBand = band;
    slot.reactParam = param;
    slot.reactAmount = amount;
}

void setGate(EffectSlot& slot, int band, float amount) {
    slot.reactBand = band;
    slot.reactMode = ReactGate;
    slot.reactAmount = amount;
}

void setColors(Layer& layer, std::array<float, 3> a, std::array<float, 3> b) {
    layer.colorA = a;
    layer.colorB = b;
}

Composition customDeck(const char* name, float bpm, std::initializer_list<const char*> a, std::initializer_list<const char*> b, std::initializer_list<const char*> c, std::initializer_list<const char*> d) {
    Composition comp;
    comp.name = name;
    comp.bpm = bpm;
    setClips(comp.layers[0], a);
    setClips(comp.layers[1], b);
    setClips(comp.layers[2], c);
    setClips(comp.layers[3], d);
    comp.layers[1].blend = BlendAdd;
    comp.layers[1].opacity = 0.45f;
    comp.layers[2].blend = BlendScreen;
    comp.layers[2].opacity = 0.35f;
    comp.layers[3].blend = BlendAdd;
    comp.layers[3].opacity = 0.25f;
    return comp;
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

Composition audioReactor() {
    Composition comp = customDeck("Audio Reactor", 126.0f,
        {"Spectrum Circle", "Rings Burst", "Hex Grid", "Voronoi", "Metaballs", "Liquid Marble", "Oscilloscope", "Warp Speed"},
        {"Equalizer", "Oscilloscope", "Bars", "Sunburst", "Lightning", "Spectrum Circle", "Rings Burst", "Hex Grid"},
        {"Starfield", "Warp Speed", "Digital Rain", "Lightning", "Sunburst", "Pulse", "Strobe", "Laser Fan"},
        {"Pulse", "Strobe", "Laser Fan", "Lightning", "Sunburst", "Equalizer", "Bars", "Rings Burst"});
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.35f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.1f);
    setMod(comp.layers[1].mods[0], BandMid, TargetOpacity, 0.3f);
    setMod(comp.master.mods[0], BandKick, TargetBrightness, 0.06f);
    setFx(comp.master.fx[0], "Radial Blur", {0.1f, 0.5f, 0.5f, 0.3f});
    setReact(comp.master.fx[0], BandBass, 0, 0.3f);
    setFx(comp.master.fx[1], "Chromatic Aberration", {0.15f, 0.6f, 0.4f});
    setReact(comp.master.fx[1], BandKick, 0, 0.4f);
    setFx(comp.master.fx[2], "Vignette", {0.5f, 0.6f, 0.1f});
    return comp;
}

Composition fractalDream() {
    Composition comp = customDeck("Fractal Dream", 110.0f,
        {"Mandelbrot", "Julia", "Liquid Marble", "Kaleido", "Gyroid 3D", "Plasma", "Metaballs", "Noise Flow"},
        {"Voronoi", "Hex Grid", "Rings Burst", "Pulse", "Sunburst", "Kaleido", "Starfield", "Grid Pulse"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    for (int l = 0; l < 2; ++l) {
        setColors(comp.layers[static_cast<size_t>(l)], {0.55f, 0.2f, 1.0f}, {0.1f, 0.9f, 0.85f});
    }
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.35f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.2f);
    setFx(comp.master.fx[0], "Infinite Zoom", {0.35f, 0.6f, 0.5f, 0.4f});
    setFx(comp.master.fx[1], "Light Rays", {0.4f, 0.6f, 0.5f, 0.6f});
    setFx(comp.master.fx[2], "Vignette", {0.5f, 0.6f, 0.0f});
    return comp;
}

Composition cyberRain() {
    Composition comp = customDeck("Cyber Rain", 140.0f,
        {"Digital Rain", "Terrain 3D", "Cube Tunnel 3D", "Lightning", "Grid Pulse", "Warp Speed", "Tunnel", "Starfield"},
        {"Grid Pulse", "Hex Grid", "Bars", "Equalizer", "Laser Fan", "Oscilloscope", "Spectrum Circle", "Strobe"},
        {"Digital Rain", "Lightning", "Warp Speed", "Pulse", "Starfield", "Strobe", "Laser Fan", "Bars"},
        {"Strobe", "Pulse", "Laser Fan", "Starfield", "Grid Pulse", "Equalizer", "Bars", "Digital Rain"});
    for (int l = 0; l < kLayerCount; ++l) {
        setColors(comp.layers[static_cast<size_t>(l)], {0.1f, 1.0f, 0.35f}, {0.95f, 0.15f, 0.75f});
    }
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setFx(comp.master.fx[0], "VHS", {0.4f, 0.35f, 0.5f, 0.5f});
    setFx(comp.master.fx[1], "Chromatic Aberration", {0.25f, 0.6f, 0.5f});
    setReact(comp.master.fx[1], BandKick, 0, 0.5f);
    setFx(comp.master.fx[2], "Block Glitch", {0.2f, 0.5f, 0.5f});
    setFx(comp.master.fx[3], "CRT", {0.4f, 0.4f, 0.4f, 0.15f});
    return comp;
}

Composition inferno() {
    Composition comp = customDeck("Inferno", 150.0f,
        {"Fire", "Lightning", "Sunburst", "Metaballs", "Liquid Marble", "Plasma", "Starfield", "Noise Flow"},
        {"Sunburst", "Lightning", "Fire", "Rings Burst", "Warp Speed", "Pulse", "Equalizer", "Bars"},
        {"Starfield", "Lightning", "Warp Speed", "Pulse", "Strobe", "Sunburst", "Fire", "Rings Burst"},
        {"Pulse", "Strobe", "Lightning", "Starfield", "Sunburst", "Equalizer", "Bars", "Rings Burst"});
    for (int l = 0; l < kLayerCount; ++l) {
        setColors(comp.layers[static_cast<size_t>(l)], {1.0f, 0.18f, 0.0f}, {1.0f, 0.85f, 0.2f});
    }
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 1);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.2f);
    setFx(comp.master.fx[0], "Ripple", {0.35f, 0.3f, 0.4f, 0.4f});
    setFx(comp.master.fx[1], "Light Rays", {0.22f, 0.6f, 0.65f, 0.6f});
    setFx(comp.master.fx[2], "Film Grain", {0.3f, 0.3f, 0.2f, 0.2f});
    setFx(comp.master.fx[3], "Vignette", {0.55f, 0.6f, 0.2f});
    return comp;
}

Composition worlds3d() {
    Composition comp = customDeck("3D Worlds", 118.0f,
        {"Cube Tunnel 3D", "Gyroid 3D", "Terrain 3D", "Torus 3D", "Sphere Grid 3D", "Cube Field 3D", "Synthwave", "Mandelbrot"},
        {"Terrain 3D", "Cube Tunnel 3D", "Torus 3D", "Gyroid 3D", "Starfield", "Synthwave", "Grid Pulse", "Rings Burst"},
        {"Starfield", "Warp Speed", "Pulse", "Grid Pulse", "Sunburst", "Rings Burst", "Hex Grid", "Voronoi"},
        {"Pulse", "Strobe", "Laser Fan", "Starfield", "Grid Pulse", "Equalizer", "Bars", "Rings Burst"});
    for (int l = 0; l < 2; ++l) {
        setColors(comp.layers[static_cast<size_t>(l)], {1.0f, 0.3f, 0.05f}, {0.05f, 0.7f, 1.0f});
    }
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.45f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setFx(comp.master.fx[0], "Camera Shake", {0.2f, 0.5f, 0.6f, 0.3f});
    setReact(comp.master.fx[0], BandKick, 0, 0.5f);
    setFx(comp.master.fx[1], "Chromatic Aberration", {0.25f, 0.6f, 0.4f});
    setFx(comp.master.fx[2], "Glow", {0.5f, 0.5f, 0.3f});
    return comp;
}

Composition crystalCube() {
    Composition comp = customDeck("Crystal Cube", 112.0f,
        {"Plasma", "Kaleido", "Voronoi", "Hex Grid", "Mandelbrot", "Liquid Marble", "Julia", "Noise Flow"},
        {"Grid Pulse", "Rings Burst", "Sunburst", "Pulse", "Metaballs", "Voronoi", "Hex Grid", "Starfield"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setFx(comp.master.fx[0], "Cube Spin", {0.55f, 0.5f, 0.4f, 0.6f});
    setFx(comp.master.fx[1], "Glow", {0.55f, 0.5f, 0.3f});
    setFx(comp.master.fx[2], "Floor Reflect", {0.35f, 0.5f, 0.4f, 0.4f});
    return comp;
}

Composition popArt() {
    Composition comp = customDeck("Pop Art", 100.0f,
        {"Plasma", "Metaballs", "Voronoi", "Sunburst", "Rings Burst", "Hex Grid", "Kaleido", "Noise Flow"},
        {"Rings Burst", "Sunburst", "Metaballs", "Pulse", "Hex Grid", "Voronoi", "Kaleido", "Grid Pulse"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    for (int l = 0; l < 2; ++l) {
        setColors(comp.layers[static_cast<size_t>(l)], {1.0f, 0.1f, 0.5f}, {1.0f, 0.9f, 0.1f});
    }
    comp.layers[0].rainbow = false;
    setActive(comp.layers[0], 1);
    setActive(comp.layers[1], 0);
    setFx(comp.master.fx[0], "Halftone", {0.4f, 0.25f, 0.3f, 0.8f});
    setFx(comp.master.fx[1], "Chromatic Aberration", {0.2f, 0.6f, 0.3f});
    setFx(comp.master.fx[2], "Dither", {0.3f, 0.2f, 0.0f, 0.5f});
    return comp;
}

Composition milkyRed() {
    Composition comp = customDeck("Milky Red", 118.0f,
        {"Milky Wave", "Liquid Marble", "Noise Flow", "Plasma", "Milky Wave", "Fire", "Voronoi", "Julia"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setColors(comp.layers[0], {0.35f, 0.0f, 0.02f}, {1.0f, 0.12f, 0.1f});
    setActive(comp.layers[0], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.12f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.08f);
    setFx(comp.master.fx[0], "Glow", {0.55f, 0.5f, 0.3f});
    setFx(comp.master.fx[1], "Vignette", {0.5f, 0.6f, 0.2f});
    setFx(comp.master.fx[2], "Chromatic Aberration", {0.25f, 0.6f, 0.4f});
    setGate(comp.master.fx[2], BandKick, 0.5f);
    return comp;
}

Composition crystalCavern() {
    Composition comp = customDeck("Crystal Cavern", 124.0f,
        {"Crystal Cavern", "Crystal Lines", "Voronoi", "Hex Grid", "Kaleido", "Warp Speed", "Tunnel", "Julia"},
        {"Crystal Lines", "Rings Burst", "Sunburst", "Pulse", "Grid Pulse", "Starfield", "Hex Grid", "Voronoi"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setColors(comp.layers[0], {0.05f, 0.45f, 0.65f}, {0.55f, 0.35f, 1.0f});
    setColors(comp.layers[1], {0.6f, 0.9f, 1.0f}, {1.0f, 1.0f, 1.0f});
    comp.layers[1].opacity = 0.3f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.1f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.1f);
    setFx(comp.master.fx[0], "Glow", {0.3f, 0.3f, 0.6f});
    setFx(comp.master.fx[1], "Vignette", {0.45f, 0.6f, 0.2f});
    setFx(comp.master.fx[2], "Mirror Fold", {0.3f, 0.25f, 0.45f, 0.6f});
    setGate(comp.master.fx[2], BandKick, 0.06f);
    return comp;
}

Composition crystalLines() {
    Composition comp = customDeck("Crystal Lines", 126.0f,
        {"Crystal Lines", "Crystal Cavern", "Tunnel", "Hex Grid", "Voronoi", "Warp Speed", "Kaleido", "Julia"},
        {"Crystal Cavern", "Laser Fan", "Rings Burst", "Pulse", "Grid Pulse", "Starfield", "Hex Grid", "Voronoi"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setColors(comp.layers[0], {0.75f, 0.9f, 1.0f}, {1.0f, 1.0f, 1.0f});
    setColors(comp.layers[1], {0.1f, 0.3f, 0.5f}, {0.4f, 0.6f, 1.0f});
    comp.layers[1].opacity = 0.22f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.1f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.12f);
    setFx(comp.master.fx[0], "Glow", {0.3f, 0.3f, 0.65f});
    setFx(comp.master.fx[1], "Vignette", {0.5f, 0.6f, 0.2f});
    setFx(comp.master.fx[2], "Chromatic Aberration", {0.2f, 0.6f, 0.4f});
    setGate(comp.master.fx[2], BandKick, 0.6f);
    return comp;
}

Composition hyperdrive() {
    Composition comp = customDeck("Hyperdrive", 142.0f,
        {"Warp Speed", "Mandelbox Fly", "Tesseract", "Neon Corridor", "Tunnel", "Cube Tunnel 3D", "Laser Web", "Starfield"},
        {"Laser Fan", "Neon Lattice", "Sunburst", "Rings Burst", "Lightning", "Laser Web", "Grid Pulse", "Pulse"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Strobe", "Pulse", "Laser Fan", "Starfield", "Grid Pulse", "Equalizer", "Bars", "Rings Burst"});
    setColors(comp.layers[0], {0.2f, 0.55f, 1.0f}, {1.0f, 0.35f, 0.1f});
    setColors(comp.layers[1], {0.1f, 0.9f, 1.0f}, {1.0f, 0.2f, 0.7f});
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.25f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.12f);
    setFx(comp.master.fx[0], "Hyperspace", {0.7f, 0.6f, 0.45f, 0.55f});
    setReact(comp.master.fx[0], BandKick, 0, 0.35f);
    setFx(comp.master.fx[1], "Chromatic Aberration", {0.2f, 0.6f, 0.4f});
    setReact(comp.master.fx[1], BandKick, 0, 0.5f);
    setFx(comp.master.fx[2], "Glow", {0.5f, 0.5f, 0.3f});
    setFx(comp.master.fx[3], "Vignette", {0.5f, 0.6f, 0.2f});
    return comp;
}

Composition neonCathedral() {
    Composition comp = customDeck("Neon Cathedral", 128.0f,
        {"Neon Sigil", "Neon Lattice", "Laser Web", "Prism Bloom", "Crystal Cavern", "Neon Corridor", "Tesseract", "Kaleido"},
        {"Crystal Lines", "Neon Sigil", "Laser Fan", "Rings Burst", "Sunburst", "Pulse", "Hex Grid", "Lightning"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Strobe", "Laser Fan", "Starfield", "Grid Pulse", "Equalizer", "Bars", "Rings Burst"});
    setColors(comp.layers[0], {1.0f, 0.15f, 0.65f}, {0.15f, 0.8f, 1.0f});
    setColors(comp.layers[1], {0.5f, 0.2f, 0.9f}, {0.05f, 0.7f, 0.65f});
    comp.layers[1].opacity = 0.25f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.18f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.15f);
    setFx(comp.master.fx[0], "Kaleido Neon", {0.4f, 0.45f, 0.1f, 0.5f});
    setReact(comp.master.fx[0], BandBass, 1, 0.25f);
    setFx(comp.master.fx[1], "Glow", {0.15f, 0.45f, 0.3f});
    setFx(comp.master.fx[2], "RGB Split", {0.15f, 0.0f, 0.6f});
    setGate(comp.master.fx[2], BandKick, 0.5f);
    setFx(comp.master.fx[3], "Vignette", {0.7f, 0.6f, 0.2f});
    return comp;
}

Composition chromeMeltdown() {
    Composition comp = customDeck("Chrome Meltdown", 118.0f,
        {"Liquid Marble", "Chaos Smoke", "Metaballs", "Milky Wave", "Plasma", "Noise Flow", "Julia", "Prism Bloom"},
        {"Metaballs", "Prism Bloom", "Liquid Marble", "Chaos Smoke", "Milky Wave", "Pulse", "Sunburst", "Noise Flow"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setColors(comp.layers[0], {0.02f, 0.06f, 0.25f}, {0.45f, 0.65f, 1.0f});
    setColors(comp.layers[1], {0.6f, 0.2f, 0.05f}, {0.5f, 0.15f, 0.9f});
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.3f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.2f);
    setFx(comp.master.fx[0], "Chrome Liquid", {0.3f, 0.45f, 0.4f, 0.45f});
    setReact(comp.master.fx[0], BandBass, 0, 0.3f);
    setFx(comp.master.fx[1], "Star Burst", {0.5f, 0.6f, 0.8f, 0.5f});
    setReact(comp.master.fx[1], BandKick, 1, 0.3f);
    setFx(comp.master.fx[2], "Chromatic Aberration", {0.15f, 0.6f, 0.4f});
    setFx(comp.master.fx[3], "Vignette", {0.55f, 0.6f, 0.2f});
    return comp;
}

Composition shatterStorm() {
    Composition comp = customDeck("Shatter Storm", 150.0f,
        {"Lightning", "Chaos Smoke", "Fire", "Sunburst", "Laser Fan", "Neon Sigil", "Voronoi", "Mandelbrot"},
        {"Lightning", "Laser Web", "Sunburst", "Rings Burst", "Fire", "Pulse", "Strobe", "Laser Fan"},
        {"Starfield", "Lightning", "Warp Speed", "Pulse", "Strobe", "Sunburst", "Fire", "Rings Burst"},
        {"Strobe", "Pulse", "Laser Fan", "Lightning", "Grid Pulse", "Equalizer", "Bars", "Rings Burst"});
    setColors(comp.layers[0], {1.0f, 0.2f, 0.05f}, {1.0f, 0.85f, 0.3f});
    setColors(comp.layers[1], {1.0f, 0.95f, 0.8f}, {1.0f, 0.4f, 0.1f});
    comp.layers[1].blend = BlendAdd;
    comp.layers[1].opacity = 0.8f;
    setActive(comp.layers[0], 2);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.22f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.18f);
    setFx(comp.master.fx[0], "Shatter", {0.5f, 0.3f, 0.75f, 0.45f});
    setReact(comp.master.fx[0], BandKick, 1, 0.6f);
    setFx(comp.master.fx[1], "Light Rays", {0.25f, 0.6f, 0.65f, 0.6f});
    setFx(comp.master.fx[2], "Film Grain", {0.3f, 0.3f, 0.2f, 0.2f});
    setFx(comp.master.fx[3], "Vignette", {0.6f, 0.6f, 0.2f});
    return comp;
}

Composition mandalaMachine() {
    Composition comp = customDeck("Mandala Machine", 124.0f,
        {"Neon Sigil", "Kaleido", "Mandelbrot", "Julia", "Prism Bloom", "Crystal Lines", "Hex Grid", "Neon Lattice"},
        {"Prism Bloom", "Neon Sigil", "Rings Burst", "Sunburst", "Voronoi", "Kaleido", "Pulse", "Grid Pulse"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    comp.layers[0].rainbow = true;
    setColors(comp.layers[1], {1.0f, 0.5f, 0.1f}, {0.2f, 0.7f, 1.0f});
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.45f;
    setActive(comp.layers[0], 3);
    setActive(comp.layers[1], 6);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.15f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.12f);
    setFx(comp.master.fx[0], "Mandala Fold", {0.6f, 0.45f, 0.3f, 0.65f});
    setReact(comp.master.fx[0], BandMid, 2, 0.35f);
    setFx(comp.master.fx[1], "Glow", {0.4f, 0.5f, 0.3f});
    setFx(comp.master.fx[2], "Chromatic Aberration", {0.15f, 0.6f, 0.4f});
    setFx(comp.master.fx[3], "Vignette", {0.55f, 0.6f, 0.2f});
    return comp;
}

Composition terrainFlight() {
    Composition comp = customDeck("Terrain Flight", 122.0f,
        {"Hex Grid", "Voronoi", "Plasma", "Metaballs", "Noise Flow", "Mandelbrot", "Julia", "Liquid Marble"},
        {"Grid Pulse", "Hex Grid", "Voronoi", "Rings Burst", "Pulse", "Sunburst", "Noise Flow", "Starfield"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setColors(comp.layers[0], {1.0f, 0.55f, 0.1f}, {0.1f, 0.75f, 1.0f});
    comp.layers[1].opacity = 0.3f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.1f);
    setFx(comp.master.fx[0], "Image Terrain", {0.6f, 0.5f, 0.75f, 0.45f});
    setReact(comp.master.fx[0], BandBass, 0, 0.3f);
    setFx(comp.master.fx[1], "Glow", {0.5f, 0.5f, 0.3f});
    setFx(comp.master.fx[2], "Chromatic Aberration", {0.15f, 0.6f, 0.4f});
    setFx(comp.master.fx[3], "Vignette", {0.5f, 0.6f, 0.2f});
    return comp;
}

Composition tesseractClub() {
    Composition comp = customDeck("Tesseract Club", 130.0f,
        {"Tesseract", "Cube Tunnel 3D", "Laser Web", "Neon Lattice", "Cube Field 3D", "Neon Corridor", "Gyroid 3D", "Sphere Grid 3D"},
        {"Neon Lattice", "Laser Fan", "Grid Pulse", "Rings Burst", "Pulse", "Torus 3D", "Hex Grid", "Strobe"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Strobe", "Pulse", "Laser Fan", "Starfield", "Grid Pulse", "Equalizer", "Bars", "Rings Burst"});
    setColors(comp.layers[0], {0.1f, 0.85f, 1.0f}, {1.0f, 0.15f, 0.6f});
    setColors(comp.layers[1], {0.6f, 0.3f, 1.0f}, {0.1f, 1.0f, 0.8f});
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.2f);
    setMod(comp.layers[0].mods[1], BandKick, TargetBrightness, 0.14f);
    setFx(comp.master.fx[0], "Mirror Corridor");
    setFx(comp.master.fx[1], "Glow", {0.55f, 0.5f, 0.3f});
    setFx(comp.master.fx[2], "Chromatic Aberration", {0.2f, 0.6f, 0.4f});
    setReact(comp.master.fx[2], BandKick, 0, 0.5f);
    setFx(comp.master.fx[3], "Vignette", {0.5f, 0.6f, 0.2f});
    return comp;
}

Composition feedbackGalaxy() {
    Composition comp = customDeck("Feedback Galaxy", 96.0f,
        {"Starfield", "Chaos Smoke", "Mandelbox Fly", "Milky Wave", "Julia", "Prism Bloom", "Liquid Marble", "Plasma"},
        {"Chaos Smoke", "Starfield", "Prism Bloom", "Rings Burst", "Sunburst", "Pulse", "Noise Flow", "Milky Wave"},
        {"Starfield", "Pulse", "Rings Burst", "Grid Pulse", "Sunburst", "Noise Flow", "Hex Grid", "Voronoi"},
        {"Pulse", "Rings Burst", "Starfield", "Sunburst", "Grid Pulse", "Hex Grid", "Noise Flow", "Voronoi"});
    setColors(comp.layers[0], {0.4f, 0.2f, 1.0f}, {0.1f, 0.85f, 0.95f});
    setColors(comp.layers[1], {1.0f, 0.3f, 0.7f}, {0.3f, 0.5f, 1.0f});
    comp.layers[1].blend = BlendScreen;
    comp.layers[1].opacity = 0.4f;
    setActive(comp.layers[0], 0);
    setActive(comp.layers[1], 0);
    setMod(comp.layers[0].mods[0], BandBass, TargetScale, 0.12f);
    setFx(comp.master.fx[0], "Hue Feedback", {0.8f, 0.6f, 0.6f, 0.45f});
    setFx(comp.master.fx[1], "Star Burst", {0.45f, 0.6f, 0.45f, 0.6f});
    setReact(comp.master.fx[1], BandKick, 1, 0.3f);
    setFx(comp.master.fx[2], "Glow", {0.55f, 0.55f, 0.3f});
    setFx(comp.master.fx[3], "Vignette", {0.6f, 0.6f, 0.2f});
    return comp;
}

std::vector<Composition> buildFactory() {
    using Builder = Composition (*)();
    static constexpr Builder builders[] = {neonClub, hyperdrive, neonCathedral, chromeMeltdown, shatterStorm, mandalaMachine, terrainFlight, tesseractClub, feedbackGalaxy, milkyRed, crystalCavern, crystalLines, acidRainbow, deepSpace, synthwaveNight, laserShow, minimalMono, showcase3d, warpTunnel, audioReactor, fractalDream, cyberRain, inferno, worlds3d, crystalCube, popArt};
    std::vector<Composition> presets;
    presets.reserve(sizeof(builders) / sizeof(builders[0]));
    for (const Builder build : builders) {
        presets.push_back(build());
    }
    return presets;
}

const std::vector<std::string>& bankNames() {
    static const std::vector<std::string> names = {"Hyperdrive", "Neon Cathedral", "Chrome Meltdown", "Shatter Storm", "Mandala Machine", "Terrain Flight", "Tesseract Club", "Feedback Galaxy", "Neon Club", "Crystal Cavern", "Inferno", "Warp Tunnel", "Synthwave Night", "Laser Show", "Acid Rainbow", "Cyber Rain"};
    return names;
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

const std::vector<int>& factoryBank() {
    static const std::vector<int> bank = [] {
        std::vector<int> indices;
        for (const std::string& name : bankNames()) {
            const int index = factoryPresetIndex(name);
            if (index >= 0) {
                indices.push_back(index);
            }
        }
        return indices;
    }();
    return bank;
}

}

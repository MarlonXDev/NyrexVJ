#include "render/effect_library.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "core/node_compile.hpp"
#include "core/node_files.hpp"
#include "render/clip_library.hpp"
#include "scene3d/scene_pack.hpp"
#include "render/effect_library_extra.hpp"

namespace nvj {

namespace {

const char* kGrade = R"glsl(
vec3 hueShift(vec3 c, float h) {
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 toRgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c;
    float a = h * 6.28318;
    float cs = cos(a);
    float sn = sin(a);
    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return clamp(toRgb * yiq, 0.0, 1.0);
}

vec3 applyGrade(vec3 c, vec4 g) {
    if (abs(g.x) > 0.0001) {
        c = hueShift(c, g.x);
    }
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, g.y);
    c = (c - 0.5) * g.w + 0.5;
    c += g.z;
    return max(c, 0.0);
}
)glsl";

const char* kEffectHeader = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform sampler2D uPrev;
uniform vec4 uParams;
uniform float uTime;
uniform float uBeat;
uniform float uAspect;
uniform vec2 uRes;
uniform vec4 uAudio;
uniform float uKick;
uniform sampler2D uAudioTex;
uniform float uMix;
const float PI = 3.14159265;

float spec(float x) {
    return texture(uAudioTex, vec2(clamp(x, 0.0, 1.0), 0.25)).r;
}

float wave(float x) {
    return texture(uAudioTex, vec2(clamp(x, 0.0, 1.0), 0.75)).r * 2.0 - 1.0;
}

vec3 hc(float h) {
    return 0.5 + 0.5 * cos(6.28318 * (h + vec3(0.0, 0.33, 0.67)));
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vn(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float ss(float a, float b, float x) {
    float d = b - a;
    d = abs(d) < 0.00001 ? 0.00001 : d;
    float t = clamp((x - a) / d, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

vec2 mrep(vec2 q) {
    return 1.0 - abs(mod(q, 2.0) - 1.0);
}
)glsl";

const char* kEffectFooter = R"glsl(
vec4 effect(vec2 uv, vec4 p);
void main() {
    vec4 e = effect(vUv, uParams);
    vec4 s = texture(uTex, vUv);
    float alpha = s.a >= 0.999 ? 1.0 : clamp(max(s.a, max(e.r, max(e.g, e.b))), 0.0, 1.0);
    vec4 o = vec4(e.rgb, alpha);
    fragColor = uMix >= 0.999 ? o : mix(s, o, clamp(uMix, 0.0, 1.0));
}
)glsl";

std::vector<EffectDef> buildLibrary() {
    std::vector<EffectDef> fx;

    fx.push_back({"Mirror", {"Axis", "Center", nullptr, nullptr}, {0.0f, 0.5f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float c = mix(0.3, 0.7, p.y);
    vec2 q = uv;
    if (p.x < 0.67) q.x = c - abs(c - q.x);
    if (p.x > 0.33) q.y = c - abs(c - q.y);
    return texture(uTex, clamp(q, 0.0, 1.0));
}
)glsl", false, false});

    fx.push_back({"Kaleidoscope", {"Segments", "Rotation", "Zoom", "Spin"}, {0.3f, 0.0f, 0.5f, 0.55f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float n = floor(mix(2.0, 16.0, p.x) + 0.5);
    float seg = PI / n;
    float a = atan(c.y, c.x) + p.y * 6.28318 + uTime * (p.w - 0.5);
    float r = length(c) * pow(2.0, mix(-1.0, 1.0, p.z));
    a = abs(mod(a, seg * 2.0) - seg);
    vec2 q = vec2(cos(a), sin(a)) * r;
    q = q / vec2(uAspect, 1.0) + 0.5;
    return texture(uTex, mrep(q));
}
)glsl", false, false});

    fx.push_back({"Pixelate", {"Size", "Beat Pulse", nullptr, nullptr}, {0.7f, 0.0f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float n = mix(300.0, 12.0, p.x);
    n *= 1.0 - p.y * 0.6 * exp(-fract(uBeat) * 4.0);
    vec2 cells = vec2(n * uAspect, n);
    vec2 q = (floor(uv * cells) + 0.5) / cells;
    return texture(uTex, q);
}
)glsl", false, false});

    fx.push_back({"RGB Split", {"Amount", "Angle", "Beat Pulse", nullptr}, {0.3f, 0.0f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float a = p.y * 6.28318;
    float amt = p.x * 0.05 * (1.0 + p.z * 3.0 * exp(-fract(uBeat) * 5.0));
    vec2 d = vec2(cos(a), sin(a)) * amt;
    vec4 c = texture(uTex, uv);
    return vec4(texture(uTex, uv + d).r, c.g, texture(uTex, uv - d).b, c.a);
}
)glsl", false, false});

    fx.push_back({"Invert", {"Amount", "Beat Flash", nullptr, nullptr}, {1.0f, 0.0f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float k = max(p.x, p.y * exp(-fract(uBeat) * 6.0));
    return vec4(mix(c.rgb, 1.0 - c.rgb, k), c.a);
}
)glsl", false, false});

    fx.push_back({"Glow", {"Intensity", "Radius", "Threshold", nullptr}, {0.6f, 0.5f, 0.25f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 base = texture(uTex, uv);
    float r = mix(0.01, 0.12, p.y);
    vec2 asp = vec2(1.0 / uAspect, 1.0);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 24; ++i) {
        float fi = float(i);
        float a = fi * 2.399963;
        float rad = sqrt((fi + 0.5) / 24.0) * r;
        vec3 s = texture(uTex, uv + vec2(cos(a), sin(a)) * rad * asp).rgb;
        acc += max(s - p.z, 0.0);
    }
    acc /= 24.0;
    return vec4(base.rgb + acc * p.x * 4.0, base.a);
}
)glsl", false, false});

    fx.push_back({"Blur", {"Radius", nullptr, nullptr, nullptr}, {0.5f, 0.5f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float r = p.x * 0.03;
    vec2 asp = vec2(1.0 / uAspect, 1.0);
    vec4 acc = texture(uTex, uv) * 2.0;
    float w = 2.0;
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 0.785398;
        vec2 d = vec2(cos(a), sin(a)) * asp;
        acc += texture(uTex, uv + d * r * 0.5);
        acc += texture(uTex, uv + d * r) * 0.75;
        w += 1.75;
    }
    return acc / w;
}
)glsl", false, false});

    fx.push_back({"Zoom Pulse", {"Amount", "Decay", nullptr, nullptr}, {0.5f, 0.4f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float e = exp(-fract(uBeat) * mix(2.0, 12.0, p.y));
    float s = 1.0 - p.x * 0.5 * e;
    return texture(uTex, (uv - 0.5) * s + 0.5);
}
)glsl", false, false});

    fx.push_back({"Rotate", {"Angle", "Spin", "Zoom", nullptr}, {0.0f, 0.6f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float a = p.x * 6.28318 + uTime * (p.y - 0.5) * 2.0;
    float z = pow(2.0, mix(-1.0, 1.0, p.z));
    vec2 c = rot(a) * ((uv - 0.5) * vec2(uAspect, 1.0) * z);
    return texture(uTex, mrep(c / vec2(uAspect, 1.0) + 0.5));
}
)glsl", false, false});

    fx.push_back({"Wave Warp", {"Amount", "Frequency", "Speed", "Direction"}, {0.4f, 0.35f, 0.3f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float amt = p.x * 0.08;
    float f = mix(2.0, 30.0, p.y);
    float sp = p.z * 6.0;
    float wx = clamp(2.0 - 2.0 * p.w, 0.0, 1.0);
    float wy = clamp(2.0 * p.w, 0.0, 1.0);
    vec2 q = uv;
    q.x += sin(uv.y * f + uTime * sp) * amt * wx;
    q.y += sin(uv.x * f * 0.8 + uTime * sp * 1.1) * amt * wy;
    return texture(uTex, mrep(q));
}
)glsl", false, false});

    fx.push_back({"Swirl", {"Amount", "Radius", "Speed", nullptr}, {0.8f, 0.6f, 0.3f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float r = length(c);
    float k = ss(mix(0.15, 1.0, p.y), 0.0, r);
    float a = (p.x * 2.0 - 1.0) * 8.0 * k * k + uTime * (p.z - 0.5) * 2.0 * k;
    c = rot(a) * c;
    return texture(uTex, mrep(c / vec2(uAspect, 1.0) + 0.5));
}
)glsl", false, false});

    fx.push_back({"Glitch", {"Amount", "Block Size", "Rate", "Color Split"}, {0.35f, 0.5f, 0.5f, 0.4f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float t = floor(uTime * mix(2.0, 24.0, p.z));
    float rows = mix(6.0, 60.0, p.y);
    float row = floor(uv.y * rows);
    float h = hash21(vec2(row, t));
    float on = step(1.0 - p.x, hash21(vec2(row * 1.7, t * 3.1)));
    float shift = (h - 0.5) * 0.4 * on * (p.x + 0.2);
    float split = on * p.w * 0.03;
    vec2 q = vec2(uv.x + shift, uv.y);
    vec3 c;
    c.r = texture(uTex, mrep(vec2(q.x + split, q.y))).r;
    c.g = texture(uTex, mrep(q)).g;
    c.b = texture(uTex, mrep(vec2(q.x - split, q.y))).b;
    return vec4(c, 1.0);
}
)glsl", false, false});

    fx.push_back({"Scanlines", {"Density", "Strength", "Scroll", nullptr}, {0.4f, 0.5f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float lines = mix(80.0, 600.0, p.x);
    float s = 0.5 + 0.5 * sin((uv.y + uTime * (p.z - 0.5) * 0.4) * lines * PI);
    c.rgb *= 1.0 - p.y * (1.0 - s);
    return c;
}
)glsl", false, false});

    fx.push_back({"Vignette", {"Amount", "Softness", "Beat Pulse", nullptr}, {0.5f, 0.5f, 0.0f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float r = length((uv - 0.5) * vec2(uAspect, 1.0));
    float edge0 = mix(0.9, 0.0, p.x) * (1.0 + p.z * 0.4 * exp(-fract(uBeat) * 4.0));
    float edge1 = edge0 + mix(0.1, 0.9, p.y);
    c.rgb *= 1.0 - ss(edge0, edge1, r);
    return c;
}
)glsl", false, false});

    fx.push_back({"Edge Detect", {"Strength", "Keep Image", "Source Color", nullptr}, {0.5f, 0.0f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 px = 1.0 / uRes;
    float tl = luma(texture(uTex, uv + px * vec2(-1.0, -1.0)).rgb);
    float t = luma(texture(uTex, uv + px * vec2(0.0, -1.0)).rgb);
    float tr = luma(texture(uTex, uv + px * vec2(1.0, -1.0)).rgb);
    float l = luma(texture(uTex, uv + px * vec2(-1.0, 0.0)).rgb);
    float r = luma(texture(uTex, uv + px * vec2(1.0, 0.0)).rgb);
    float bl = luma(texture(uTex, uv + px * vec2(-1.0, 1.0)).rgb);
    float b = luma(texture(uTex, uv + px * vec2(0.0, 1.0)).rgb);
    float br = luma(texture(uTex, uv + px * vec2(1.0, 1.0)).rgb);
    float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
    float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;
    float e = clamp(length(vec2(gx, gy)) * mix(1.0, 16.0, p.x), 0.0, 1.0);
    vec3 c = texture(uTex, uv).rgb;
    vec3 col = mix(vec3(e), c * e * 2.0, p.z);
    return vec4(col + c * p.y, 1.0);
}
)glsl", false, false});

    fx.push_back({"Posterize", {"Levels", nullptr, nullptr, nullptr}, {0.3f, 0.5f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float n = floor(mix(2.0, 12.0, p.x) + 0.5);
    return vec4(floor(c.rgb * n + 0.5) / n, c.a);
}
)glsl", false, false});

    fx.push_back({"Trails", {"Feedback", "Zoom", "Rotate", nullptr}, {0.7f, 0.55f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 cur = texture(uTex, uv);
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float z = 1.0 - (p.y - 0.5) * 0.12;
    c = rot((p.z - 0.5) * 0.12) * (c * z);
    vec3 prev = texture(uPrev, c / vec2(uAspect, 1.0) + 0.5).rgb * mix(0.0, 0.985, p.x);
    return vec4(max(cur.rgb, prev), 1.0);
}
)glsl", false, true});

    fx.push_back({"Strobe", {"Rate", "Duty", "Mode", nullptr}, {0.5f, 0.3f, 0.0f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float rate = pow(2.0, floor(mix(-1.0, 3.0, p.x) + 0.5));
    float on = step(fract(uBeat * rate), mix(0.05, 0.5, p.y));
    vec3 outc = p.z < 0.5 ? c.rgb + vec3(on) : c.rgb * (1.0 - on);
    return vec4(outc, c.a);
}
)glsl", false, false});

    fx.push_back({"Perspective Plane", {"Horizon", "Zoom", "Scroll", "Fog"}, {0.5f, 0.5f, 0.7f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float d = mix(-0.15, 0.35, p.x) - c.y;
    if (d <= 0.001) return vec4(0.0, 0.0, 0.0, 1.0);
    float z = 0.25 * pow(2.0, mix(-1.0, 1.5, p.y)) / d;
    vec2 t = vec2(c.x * z + 0.5, z + uTime * (p.z - 0.5) * 2.0);
    float fog = ss(0.0, mix(0.05, 0.5, p.w), d);
    return vec4(texture(uTex, mrep(t)).rgb * fog, 1.0);
}
)glsl", true, false});

    fx.push_back({"Tunnel Map", {"Segments", "Depth", "Speed", "Twist"}, {0.35f, 0.5f, 0.7f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float r = max(length(c), 0.0005);
    float a = atan(c.y, c.x) / PI;
    float seg = 2.0 * floor(mix(1.0, 4.0, p.x) + 0.5);
    float z = 0.25 * pow(2.0, mix(-1.0, 1.0, p.y)) / r + uTime * (p.z - 0.5) * 3.0;
    vec2 t = vec2(a * 0.5 * seg + z * (p.w - 0.5) * 0.5 + 0.5, z);
    float fog = ss(0.0, 0.35, r);
    return vec4(texture(uTex, mrep(t)).rgb * fog, 1.0);
}
)glsl", true, false});

    fx.push_back({"Fisheye", {"Amount", "Zoom", "Chromatic", nullptr}, {0.75f, 0.5f, 0.2f, 0.5f}, R"glsl(
vec2 warp(vec2 c, float s, float k, float z) {
    vec2 d = c * s;
    d = d * (1.0 + k * dot(d, d)) / z;
    return d / (2.0 * vec2(uAspect, 1.0)) + 0.5;
}

float inside(vec2 q) {
    vec2 m = step(vec2(0.0), q) * step(q, vec2(1.0));
    return m.x * m.y;
}

vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0) * 2.0;
    float k = (p.x * 2.0 - 1.0) * 0.8;
    float z = pow(2.0, mix(-1.0, 1.0, p.y));
    float ch = p.z * 0.04;
    vec2 qr = warp(c, 1.0 + ch, k, z);
    vec2 qg = warp(c, 1.0, k, z);
    vec2 qb = warp(c, 1.0 - ch, k, z);
    vec3 col = vec3(texture(uTex, qr).r * inside(qr), texture(uTex, qg).g * inside(qg), texture(uTex, qb).b * inside(qb));
    return vec4(col, 1.0);
}
)glsl", true, false});

    fx.push_back({"Tile", {"Count", "Mirror", "Scroll", nullptr}, {0.4f, 1.0f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float n = floor(mix(1.0, 6.0, p.x) + 0.5);
    vec2 q = uv * n + vec2(uTime * (p.z - 0.5), 0.0);
    vec2 t = p.y > 0.5 ? mrep(q) : fract(q);
    return texture(uTex, t);
}
)glsl", false, false});

    fx.push_back({"Hue Cycle", {"Speed", "Offset", "Beat Step", nullptr}, {0.6f, 0.0f, 0.0f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float h = p.y + uTime * (p.x - 0.5) * 0.5 + floor(uBeat) * p.z * 0.125;
    return vec4(hueShift(c.rgb, h), c.a);
}
)glsl", false, false});

    appendExtraEffects(fx);
    return fx;
}

}

int EffectDef::paramCount() const {
    int count = 0;
    for (const char* name : paramNames) {
        if (name) {
            ++count;
        }
    }
    return count;
}

namespace {

std::vector<EffectDef>& mutableLibrary() {
    static std::vector<EffectDef> library = buildLibrary();
    return library;
}

std::deque<std::array<std::string, kParamCount>>& graphNames() {
    static std::deque<std::array<std::string, kParamCount>> names;
    return names;
}

void applyGraphControls(EffectDef& def, const GraphDocument& graph) {
    std::array<std::string, kParamCount>& names = graphNames()[static_cast<size_t>(def.graphSlot)];
    for (int i = 0; i < kParamCount; ++i) {
        const GraphControl& control = graph.controls[static_cast<size_t>(i)];
        names[static_cast<size_t>(i)] = control.label;
        def.paramNames[static_cast<size_t>(i)] = control.label.empty() ? nullptr : names[static_cast<size_t>(i)].c_str();
        def.defaults[static_cast<size_t>(i)] = control.value;
    }
}

void clearGraphControls(EffectDef& def) {
    std::array<std::string, kParamCount>& names = graphNames()[static_cast<size_t>(def.graphSlot)];
    for (int i = 0; i < kParamCount; ++i) {
        names[static_cast<size_t>(i)].clear();
        def.paramNames[static_cast<size_t>(i)] = nullptr;
        def.defaults[static_cast<size_t>(i)] = 0.5f;
    }
}

bool fillGraphDef(EffectDef& def, const GraphFileInfo& file) {
    const unsigned long long oldHash = def.hash;
    const std::string oldError = def.error;
    def.stamp = file.stamp;
    def.bytes = file.bytes;
    GraphDocument graph;
    std::string error;
    if (!readGraphFile(file.path, graph, error)) {
        def.error = error;
        def.body.clear();
        def.hash = 0;
        def.usesPrevious = false;
        def.usesMacros = false;
        def.textureReads = 0;
        clearGraphControls(def);
        return oldHash != 0 || oldError != def.error;
    }
    applyGraphControls(def, graph);
    const CompiledGraph compiled = compileGraph(graph);
    if (!compiled.ok) {
        def.error = compiled.firstError();
        for (const GraphError& item : compiled.errors) {
            const GraphNode* node = graph.find(item.node);
            if (node) {
                const NodeSpec* spec = findNodeSpec(node->type);
                def.error = std::string(spec ? spec->title : node->type.c_str()) + ": " + item.message;
                break;
            }
        }
        def.body.clear();
        def.hash = 0;
        def.usesPrevious = false;
        def.usesMacros = false;
        def.textureReads = 0;
        return oldHash != 0 || oldError != def.error;
    }
    def.error.clear();
    def.body = compiled.glsl;
    def.hash = compiled.hash;
    def.usesPrevious = compiled.usesPrevious;
    def.usesMacros = compiled.usesMacros;
    def.textureReads = compiled.textureReads;
    return oldHash != def.hash || !oldError.empty();
}


std::string isfUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::map<std::string, std::pair<long long, unsigned long long>>& examinedShaderFiles() {
    static std::map<std::string, std::pair<long long, unsigned long long>> files;
    return files;
}

void applyIsfControls(EffectDef& def, const ShaderPlan* plan) {
    std::array<std::string, kParamCount>& names = graphNames()[static_cast<size_t>(def.graphSlot)];
    const std::vector<int> controls = plan ? shaderFilterParams(*plan) : std::vector<int>();
    for (int i = 0; i < kParamCount; ++i) {
        if (static_cast<size_t>(i) < controls.size()) {
            const ShaderParam& param = plan->params[static_cast<size_t>(controls[static_cast<size_t>(i)])];
            names[static_cast<size_t>(i)] = param.label;
            def.paramNames[static_cast<size_t>(i)] = names[static_cast<size_t>(i)].c_str();
            def.defaults[static_cast<size_t>(i)] = shaderControlFromParam(param, param.def[0]);
        } else {
            names[static_cast<size_t>(i)].clear();
            def.paramNames[static_cast<size_t>(i)] = nullptr;
            def.defaults[static_cast<size_t>(i)] = 0.5f;
        }
    }
}

enum class IsfRead { Filter, Other, Failed };

IsfRead readIsfFile(const std::filesystem::path& file, std::shared_ptr<const ShaderPlan>& plan, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        error = "The shader file is gone";
        return IsfRead::Failed;
    }
    if (size > kMaxShaderFileBytes) {
        error = "The shader file is larger than 256 KiB";
        return IsfRead::Failed;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        error = "The shader file cannot be read";
        return IsfRead::Failed;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const ShaderSource prepared = prepareShaderSource(buffer.str());
    if (!prepared.ok) {
        error = prepared.error;
        return IsfRead::Failed;
    }
    if (!prepared.plan || !prepared.plan->filter) {
        return IsfRead::Other;
    }
    plan = prepared.plan;
    return IsfRead::Filter;
}

bool fillIsfDef(EffectDef& def, const std::filesystem::path& file) {
    std::error_code ec;
    def.bytes = std::filesystem::exists(file, ec) ? static_cast<unsigned long long>(std::filesystem::file_size(file, ec)) : 0;
    const auto time = std::filesystem::last_write_time(file, ec);
    def.stamp = ec ? 0 : static_cast<long long>(time.time_since_epoch().count());
    std::shared_ptr<const ShaderPlan> plan;
    std::string error;
    const IsfRead state = readIsfFile(file, plan, error);
    if (state == IsfRead::Other) {
        error = "This file is not an image filter any more. It is in the clip library";
    }
    if (state != IsfRead::Filter) {
        def.error = error;
        def.plan.reset();
        def.usesPrevious = false;
        applyIsfControls(def, nullptr);
        return true;
    }
    def.error.clear();
    def.plan = plan;
    applyIsfControls(def, plan.get());
    return true;
}
}

const std::vector<EffectDef>& effectLibrary() {
    return mutableLibrary();
}

NodeScan refreshNodeEffects(const std::string& forceName) {
    NodeScan scan;
    std::vector<EffectDef>& library = mutableLibrary();
    const std::vector<GraphFileInfo> files = scanGraphFiles(graphsFolder());
    for (size_t i = 0; i < library.size(); ++i) {
        EffectDef& def = library[i];
        if (!def.isGraph) {
            continue;
        }
        const GraphFileInfo* match = nullptr;
        for (const GraphFileInfo& file : files) {
            if (graphEffectName(file.stem) == def.name) {
                match = &file;
                break;
            }
        }
        if (!match) {
            if (def.error != "The effect file is gone") {
                def.error = "The effect file is gone";
                def.body.clear();
                def.hash = 0;
                def.usesPrevious = false;
                def.usesMacros = false;
                scan.changed.push_back(i);
            }
            continue;
        }
        if (match->stamp == def.stamp && match->bytes == def.bytes && def.error != "The effect file is gone" && def.name != forceName) {
            continue;
        }
        def.path = match->path.string();
        if (fillGraphDef(def, *match)) {
            scan.changed.push_back(i);
        }
    }
    size_t count = 0;
    for (const EffectDef& def : library) {
        count += def.isGraph ? 1 : 0;
    }
    for (const GraphFileInfo& file : files) {
        const std::string name = graphEffectName(file.stem);
        bool known = false;
        for (const EffectDef& def : library) {
            if (def.isGraph && def.name == name) {
                known = true;
                break;
            }
        }
        if (known || count >= kMaxNodeEffects) {
            continue;
        }
        EffectDef def;
        def.name = name;
        def.paramNames = {nullptr, nullptr, nullptr, nullptr};
        def.defaults = {0.5f, 0.5f, 0.5f, 0.5f};
        def.isGraph = true;
        def.path = file.path.string();
        graphNames().emplace_back();
        def.graphSlot = static_cast<int>(graphNames().size()) - 1;
        fillGraphDef(def, file);
        library.push_back(std::move(def));
        ++count;
        ++scan.added;
    }
    return scan;
}


std::string isfEffectName(const std::string& stem) {
    return "ISF: " + stem;
}

IsfScan refreshIsfEffects() {
    namespace fs = std::filesystem;
    IsfScan scan;
    std::vector<EffectDef>& library = mutableLibrary();
    std::error_code ec;
    std::vector<fs::path> files;
    const fs::path directory = pathFromUtf8(shaderDirectory());
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && !entryEc && isShaderFileName(isfUtf8(it->path().filename()))) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    size_t count = 0;
    for (size_t i = 0; i < library.size(); ++i) {
        EffectDef& def = library[i];
        if (!def.isIsf) {
            continue;
        }
        ++count;
        const fs::path* match = nullptr;
        for (const fs::path& file : files) {
            if (isfEffectName(isfUtf8(file.stem())) == def.name) {
                match = &file;
                break;
            }
        }
        if (!match) {
            if (def.error != "The shader file is gone") {
                def.error = "The shader file is gone";
                def.plan.reset();
                applyIsfControls(def, nullptr);
                scan.changed.push_back(i);
            }
            continue;
        }
        std::error_code statEc;
        const auto size = fs::file_size(*match, statEc);
        const auto time = fs::last_write_time(*match, statEc);
        const long long stamp = statEc ? 0 : static_cast<long long>(time.time_since_epoch().count());
        if (stamp == def.stamp && static_cast<unsigned long long>(size) == def.bytes && def.error != "The shader file is gone") {
            continue;
        }
        def.path = isfUtf8(*match);
        if (fillIsfDef(def, *match)) {
            scan.changed.push_back(i);
        }
    }
    for (const fs::path& file : files) {
        const std::string name = isfEffectName(isfUtf8(file.stem()));
        bool known = false;
        for (const EffectDef& def : library) {
            if (def.isIsf && def.name == name) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        std::error_code statEc;
        const auto size = fs::file_size(file, statEc);
        const auto time = fs::last_write_time(file, statEc);
        const std::pair<long long, unsigned long long> stamp{static_cast<long long>(time.time_since_epoch().count()), static_cast<unsigned long long>(size)};
        const std::string key = isfUtf8(file);
        const auto seen = examinedShaderFiles().find(key);
        if (seen != examinedShaderFiles().end() && seen->second == stamp) {
            continue;
        }
        std::shared_ptr<const ShaderPlan> plan;
        std::string error;
        const IsfRead state = readIsfFile(file, plan, error);
        examinedShaderFiles()[key] = stamp;
        if (state == IsfRead::Other || count >= kMaxIsfEffects) {
            continue;
        }
        if (state == IsfRead::Failed) {
            continue;
        }
        EffectDef def;
        def.name = name;
        def.paramNames = {nullptr, nullptr, nullptr, nullptr};
        def.defaults = {0.5f, 0.5f, 0.5f, 0.5f};
        def.isIsf = true;
        def.path = key;
        graphNames().emplace_back();
        def.graphSlot = static_cast<int>(graphNames().size()) - 1;
        fillIsfDef(def, file);
        library.push_back(std::move(def));
        ++count;
        ++scan.added;
        examinedShaderFiles().erase(key);
    }
    return scan;
}

int effectIndexByName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::vector<EffectDef>& library = effectLibrary();
    for (size_t i = 0; i < library.size(); ++i) {
        std::string candidate = library[i].name;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (candidate == lower) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void setEffect(EffectSlot& slot, int effectIndex) {
    const std::vector<EffectDef>& library = effectLibrary();
    if (effectIndex < 0 || effectIndex >= static_cast<int>(library.size())) {
        slot.effect = -1;
        return;
    }
    slot.effect = effectIndex;
    slot.enabled = true;
    slot.params = library[static_cast<size_t>(effectIndex)].defaults;
}

int registerEffect(const std::string& name, const std::string& body, bool usesPrevious, const std::array<float, kParamCount>& defaults, bool usesMacros) {
    std::vector<EffectDef>& library = mutableLibrary();
    EffectDef def;
    def.name = name;
    def.paramNames = {nullptr, nullptr, nullptr, nullptr};
    def.defaults = defaults;
    def.body = body;
    def.usesPrevious = usesPrevious;
    def.usesMacros = usesMacros;
    library.push_back(std::move(def));
    return static_cast<int>(library.size()) - 1;
}

std::string assembleEffectSource(const std::string& body) {
    return std::string(kEffectHeader) + kGrade + kEffectFooter + body;
}

const std::string& effectFragmentSource(int effectIndex) {
    static std::vector<std::string> cache;
    static std::vector<unsigned long long> stamps;
    const std::vector<EffectDef>& library = effectLibrary();
    if (cache.size() < library.size()) {
        cache.resize(library.size());
        stamps.resize(library.size(), 0);
    }
    const size_t index = static_cast<size_t>(effectIndex);
    const unsigned long long stamp = library[index].isGraph ? library[index].hash + 2 : 1;
    if (stamps[index] != stamp) {
        cache[index] = assembleEffectSource(library[index].body);
        stamps[index] = stamp;
    }
    return cache[index];
}

const char* gradeGlsl() {
    return kGrade;
}

}

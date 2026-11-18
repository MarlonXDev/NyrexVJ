#include "clip_library.hpp"

namespace nvj {

namespace {

const char* kVertex = R"glsl(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char* kHeader = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform float uTime;
uniform float uBeat;
uniform float uBpm;
uniform vec2 uRes;
const float PI = 3.14159265;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 pal(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67)));
}

vec3 duo(float t) {
    return mix(vec3(1.0, 0.45, 0.0), vec3(0.0, 0.8, 1.0), clamp(t, 0.0, 1.0));
}

vec2 centered() {
    return (vUv - 0.5) * vec2(uRes.x / uRes.y, 1.0) * 2.0;
}
)glsl";

std::vector<ClipDef> buildLibrary() {
    std::vector<ClipDef> clips;

    clips.push_back({"Plasma", R"glsl(
void main() {
    vec2 p = centered() * 1.6;
    float t = uTime * 0.6;
    float v = sin(p.x * 2.0 + t) + sin(p.y * 3.0 - t * 1.3) + sin((p.x + p.y) * 2.0 + t * 0.7) + sin(length(p) * 3.0 - t * 1.5);
    fragColor = vec4(pal(v * 0.25 + uBeat * 0.04), 1.0);
}
)glsl"});

    clips.push_back({"Tunnel", R"glsl(
void main() {
    vec2 p = centered();
    float r = max(length(p), 0.001);
    float a = atan(p.y, p.x);
    float z = 0.6 / r + uBeat * 0.5;
    float u = a / PI * 3.0;
    float c = abs(fract(u) - 0.5) * 2.0;
    float d = abs(fract(z) - 0.5) * 2.0;
    float g = smoothstep(0.82, 1.0, max(c, d));
    vec3 col = duo(0.5 + 0.5 * sin(z * 1.5 + uTime)) * g * clamp(r * 1.8, 0.0, 1.0);
    fragColor = vec4(col, 1.0);
}
)glsl"});

    clips.push_back({"Grid Pulse", R"glsl(
void main() {
    vec2 p = centered() * 4.0;
    vec2 g = abs(fract(p) - 0.5);
    float line = smoothstep(0.44, 0.5, max(g.x, g.y));
    float pulse = exp(-fract(uBeat) * 4.0);
    float ring = smoothstep(0.12, 0.0, abs(length(p) - fract(uBeat * 0.5) * 10.0));
    vec3 col = duo(0.15) * line * (0.35 + pulse) + duo(0.9) * ring;
    fragColor = vec4(col, 1.0);
}
)glsl"});

    clips.push_back({"Kaleido", R"glsl(
void main() {
    vec2 p = centered();
    float a = atan(p.y, p.x);
    float r = length(p);
    float seg = PI / 6.0;
    a = abs(mod(a, seg * 2.0) - seg);
    vec2 q = vec2(cos(a), sin(a)) * r;
    float v = sin(q.x * 14.0 + uTime * 2.0) * sin(q.y * 14.0 - uTime * 1.5);
    vec3 col = pal(v * 0.3 + r * 0.8 + uBeat * 0.05) * smoothstep(1.6, 0.1, r);
    fragColor = vec4(col, 1.0);
}
)glsl"});

    clips.push_back({"Bars", R"glsl(
void main() {
    float bars = step(0.5, fract(vUv.y * 12.0 + uBeat * 0.25));
    float wob = 0.5 + 0.5 * sin(vUv.x * 10.0 + uTime * 2.0);
    vec3 col = duo(vUv.x) * bars * (0.3 + 0.7 * wob);
    fragColor = vec4(col, 1.0);
}
)glsl"});

    clips.push_back({"Noise Flow", R"glsl(
void main() {
    vec2 p = centered() * 1.5;
    float n = vnoise(p + vec2(uTime * 0.3, 0.0));
    n += 0.5 * vnoise(p * 2.0 - vec2(0.0, uTime * 0.4));
    n += 0.25 * vnoise(p * 4.0 + vec2(uTime * 0.2));
    n /= 1.75;
    vec3 col = duo(smoothstep(0.3, 0.8, n)) * smoothstep(0.25, 0.9, n);
    fragColor = vec4(col, 1.0);
}
)glsl"});

    clips.push_back({"Strobe", R"glsl(
void main() {
    float f = exp(-fract(uBeat) * 8.0);
    fragColor = vec4(vec3(f), 1.0);
}
)glsl"});

    clips.push_back({"Pulse", R"glsl(
void main() {
    vec2 p = centered();
    float r = length(p);
    float rad = 0.2 + 0.3 * exp(-fract(uBeat) * 3.0);
    float disc = smoothstep(rad, rad - 0.02, r);
    float ring = smoothstep(0.015, 0.0, abs(r - rad - 0.1));
    vec3 col = pal(uBeat * 0.1) * disc + vec3(ring);
    fragColor = vec4(col, 1.0);
}
)glsl"});

    return clips;
}

}

const std::vector<ClipDef>& clipLibrary() {
    static const std::vector<ClipDef> library = buildLibrary();
    return library;
}

const std::string& clipVertexSource() {
    static const std::string source = kVertex;
    return source;
}

std::string clipFragmentSource(const ClipDef& clip) {
    return std::string(kHeader) + clip.body;
}

}

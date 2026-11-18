#include "render/clip_library.hpp"

#include <algorithm>
#include <cctype>

#include "render/clip_library_extra.hpp"

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
uniform vec3 uColorA;
uniform vec3 uColorB;
uniform float uRainbow;
uniform float uScale;
uniform vec4 uAudio;
uniform float uKick;
uniform sampler2D uAudioTex;
const float PI = 3.14159265;

float spec(float x) {
    return texture(uAudioTex, vec2(clamp(x, 0.0, 1.0), 0.25)).r;
}

float wave(float x) {
    return texture(uAudioTex, vec2(clamp(x, 0.0, 1.0), 0.75)).r * 2.0 - 1.0;
}

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

float fbm(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        s += vnoise(p) * a;
        p = p * 2.02 + 17.3;
        a *= 0.5;
    }
    return s;
}

vec2 h22(vec2 p) {
    return vec2(hash21(p), hash21(p + 17.3));
}

float ss(float a, float b, float x) {
    float d = b - a;
    d = abs(d) < 0.00001 ? 0.00001 : d;
    float t = clamp((x - a) / d, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

vec3 rainbow(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67)));
}

vec3 duo(float t) {
    return mix(uColorA, uColorB, clamp(t, 0.0, 1.0));
}

vec3 pal(float t) {
    return uRainbow > 0.5 ? rainbow(t) : duo(0.5 + 0.5 * cos(6.28318 * t));
}

vec2 centered() {
    return (vUv - 0.5) * vec2(uRes.x / uRes.y, 1.0) * 2.0 / max(uScale, 0.05);
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
)glsl", false});

    clips.push_back({"Tunnel", R"glsl(
void main() {
    vec2 p = centered();
    float r = max(length(p), 0.001);
    float a = atan(p.y, p.x);
    float z = 0.6 / r + uBeat * 0.5;
    float u = a / PI * 3.0;
    float c = abs(fract(u) - 0.5) * 2.0;
    float d = abs(fract(z) - 0.5) * 2.0;
    float g = ss(0.82, 1.0, max(c, d));
    vec3 col = duo(0.5 + 0.5 * sin(z * 1.5 + uTime)) * g * clamp(r * 1.8, 0.0, 1.0);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Grid Pulse", R"glsl(
void main() {
    vec2 p = centered() * 4.0;
    vec2 g = abs(fract(p) - 0.5);
    float line = ss(0.44, 0.5, max(g.x, g.y));
    float pulse = exp(-fract(uBeat) * 4.0);
    float ring = ss(0.12, 0.0, abs(length(p) - fract(uBeat * 0.5) * 10.0));
    vec3 col = duo(0.15) * line * (0.35 + pulse) + duo(0.9) * ring;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Kaleido", R"glsl(
void main() {
    vec2 p = centered();
    float a = atan(p.y, p.x);
    float r = length(p);
    float seg = PI / 6.0;
    a = abs(mod(a, seg * 2.0) - seg);
    vec2 q = vec2(cos(a), sin(a)) * r;
    float v = sin(q.x * 14.0 + uTime * 2.0) * sin(q.y * 14.0 - uTime * 1.5);
    vec3 col = pal(v * 0.3 + r * 0.8 + uBeat * 0.05) * ss(1.6, 0.1, r);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Bars", R"glsl(
void main() {
    float bars = step(0.5, fract(vUv.y * 12.0 + uBeat * 0.25));
    float wob = clamp(spec(vUv.x * 0.75) * 1.5, 0.0, 1.0);
    vec3 col = duo(vUv.x) * bars * (0.25 + 0.75 * wob);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Noise Flow", R"glsl(
void main() {
    vec2 p = centered() * 1.5;
    float n = vnoise(p + vec2(uTime * 0.3, 0.0));
    n += 0.5 * vnoise(p * 2.0 - vec2(0.0, uTime * 0.4));
    n += 0.25 * vnoise(p * 4.0 + vec2(uTime * 0.2));
    n /= 1.75;
    vec3 col = duo(ss(0.3, 0.8, n)) * ss(0.25, 0.9, n);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Strobe", R"glsl(
void main() {
    float f = exp(-fract(uBeat) * 8.0);
    vec3 tint = uRainbow > 0.5 ? vec3(1.0) : mix(vec3(1.0), uColorA, 0.6);
    fragColor = vec4(tint * f, 1.0);
}
)glsl", false});

    clips.push_back({"Pulse", R"glsl(
void main() {
    vec2 p = centered();
    float r = length(p);
    float rad = 0.2 + 0.3 * exp(-fract(uBeat) * 3.0);
    float disc = ss(rad, rad - 0.02, r);
    float ring = ss(0.015, 0.0, abs(r - rad - 0.1));
    vec3 col = pal(uBeat * 0.1) * disc + duo(1.0) * ring;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Cube Field 3D", R"glsl(
float cubeHash(vec3 id) {
    return hash21(id.xy * 1.37 + vec2(id.z * 7.13, id.z * 3.71));
}

float mapScene(vec3 p) {
    vec3 q = p;
    q.z += uTime * 3.0;
    vec3 id = floor((q + 2.0) / 4.0);
    q = mod(q + 2.0, 4.0) - 2.0;
    float h = cubeHash(id);
    q.xy = rot(h * 6.2831 + uTime * (h - 0.5)) * q.xy;
    q.yz = rot(h * 3.0 + uTime * 0.7) * q.yz;
    float s = 0.2 + 0.4 * h + 0.12 * exp(-fract(uBeat) * 4.0) * step(0.5, h);
    return sdBox(q, vec3(s));
}

void main() {
    vec2 p = centered();
    vec3 ro = vec3(2.0 + 0.6 * sin(uTime * 0.5), 2.0 + 0.6 * cos(uTime * 0.37), 0.0);
    vec3 rd = normalize(vec3(p, 1.6));
    rd.xy = rot(0.2 * sin(uTime * 0.3)) * rd.xy;
    float d = 0.0;
    bool hit = false;
    vec3 pos = ro;
    for (int i = 0; i < 56; ++i) {
        pos = ro + rd * d;
        float m = mapScene(pos);
        if (m < 0.003) {
            hit = true;
            break;
        }
        d += m * 0.85;
        if (d > 36.0) break;
    }
    vec3 col = duo(0.0) * 0.015;
    if (hit) {
        vec2 e = vec2(0.004, 0.0);
        vec3 n = normalize(vec3(
            mapScene(pos + e.xyy) - mapScene(pos - e.xyy),
            mapScene(pos + e.yxy) - mapScene(pos - e.yxy),
            mapScene(pos + e.yyx) - mapScene(pos - e.yyx)));
        float h = cubeHash(floor((pos + vec3(0.0, 0.0, uTime * 3.0) + 2.0) / 4.0));
        vec3 base = pal(h + pos.z * 0.01);
        float diff = 0.25 + 0.75 * max(dot(n, normalize(vec3(0.4, 0.8, -0.5))), 0.0);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col = (base * diff + duo(0.85) * rim * 0.8) * exp(-d * 0.07);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Torus 3D", R"glsl(
float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float mapScene(vec3 p) {
    float pulse = 1.0 + 0.08 * exp(-fract(uBeat) * 4.0);
    vec3 a = p;
    a.xz = rot(uTime * 0.6) * a.xz;
    a.xy = rot(uTime * 0.4) * a.xy;
    vec3 b = p;
    b.yz = rot(uTime * 0.5 + 1.0) * b.yz;
    b.xy = rot(-uTime * 0.7) * b.xy;
    vec3 c = p;
    c.xz = rot(-uTime * 0.3 + 2.0) * c.xz;
    c.yz = rot(uTime * 0.8) * c.yz;
    float d = sdTorus(a, vec2(1.3 * pulse, 0.12));
    d = min(d, sdTorus(b, vec2(0.95 * pulse, 0.1)));
    d = min(d, sdTorus(c, vec2(0.6 * pulse, 0.09)));
    return min(d, length(p) - 0.2 * pulse);
}

void main() {
    vec2 p = centered();
    vec3 ro = vec3(0.0, 0.0, -4.2);
    vec3 rd = normalize(vec3(p, 1.8));
    float d = 0.0;
    float minD = 10.0;
    bool hit = false;
    vec3 pos = ro;
    for (int i = 0; i < 64; ++i) {
        pos = ro + rd * d;
        float m = mapScene(pos);
        minD = min(minD, m);
        if (m < 0.002) {
            hit = true;
            break;
        }
        d += m;
        if (d > 12.0) break;
    }
    vec3 col = duo(0.0) * 0.02 + duo(0.7) * exp(-minD * 6.0) * 0.35;
    if (hit) {
        vec2 e = vec2(0.003, 0.0);
        vec3 n = normalize(vec3(
            mapScene(pos + e.xyy) - mapScene(pos - e.xyy),
            mapScene(pos + e.yxy) - mapScene(pos - e.yxy),
            mapScene(pos + e.yyx) - mapScene(pos - e.yyx)));
        vec3 l = normalize(vec3(0.5, 0.7, -0.6));
        float diff = 0.2 + 0.8 * max(dot(n, l), 0.0);
        float spec = pow(max(dot(reflect(-l, n), -rd), 0.0), 24.0);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        vec3 base = pal(length(pos) * 0.5 + uTime * 0.1);
        col = base * diff + vec3(spec) * 0.8 + duo(0.9) * rim * 0.7;
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Sphere Grid 3D", R"glsl(
float mapScene(vec3 p) {
    const float cell = 1.2;
    vec2 id = floor(p.xz / cell + 0.5);
    vec3 q = vec3(p.x - id.x * cell, p.y, p.z - id.y * cell);
    float w = sin(id.x * 0.55 + uTime * 1.4) * cos(id.y * 0.5 + uTime * 1.1);
    float kick = exp(-fract(uBeat) * 3.0) * hash21(id) * 0.5;
    float h = w * 0.7 + kick;
    float r = 0.28 + 0.1 * (0.5 + 0.5 * sin(id.x + id.y + uTime));
    return length(q - vec3(0.0, h, 0.0)) - r;
}

void main() {
    vec2 p = centered();
    vec3 ro = vec3(sin(uTime * 0.15) * 2.0, 2.0, uTime * 1.5);
    vec3 fwd = normalize(vec3(0.0, -0.45, 1.0));
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));
    vec3 up = cross(fwd, right);
    vec3 rd = normalize(p.x * right + p.y * up + 1.6 * fwd);
    float d = 0.0;
    bool hit = false;
    vec3 pos = ro;
    for (int i = 0; i < 64; ++i) {
        pos = ro + rd * d;
        float m = mapScene(pos);
        if (m < 0.003) {
            hit = true;
            break;
        }
        d += m * 0.7;
        if (d > 40.0) break;
    }
    vec3 col = duo(0.0) * 0.03 + duo(0.5) * exp(-abs(rd.y) * 6.0) * 0.12;
    if (hit) {
        vec2 e = vec2(0.004, 0.0);
        vec3 n = normalize(vec3(
            mapScene(pos + e.xyy) - mapScene(pos - e.xyy),
            mapScene(pos + e.yxy) - mapScene(pos - e.yxy),
            mapScene(pos + e.yyx) - mapScene(pos - e.yyx)));
        float diff = 0.3 + 0.7 * max(dot(n, normalize(vec3(0.3, 0.9, -0.4))), 0.0);
        vec3 base = pal(pos.y * 0.5 + pos.z * 0.02 + uBeat * 0.02);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 2.5);
        col = (base * diff + duo(0.9) * rim * 0.5) * exp(-d * 0.055);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Synthwave", R"glsl(
void main() {
    vec2 p = centered();
    float beat = exp(-fract(uBeat) * 3.0);
    vec3 col = vec3(0.0);
    if (p.y >= 0.0) {
        col = mix(duo(0.0) * 0.55, vec3(0.015, 0.0, 0.05), ss(0.0, 0.85, p.y));
        float sd = length(vec2(p.x, p.y - 0.42)) - 0.46;
        float stripes = step(0.5, fract(p.y * 11.0));
        float cutMask = mix(stripes, 1.0, ss(0.18, 0.42, p.y));
        vec3 sunCol = mix(duo(0.0), vec3(1.0, 0.95, 0.7), ss(0.0, 0.9, p.y));
        col = mix(col, sunCol, ss(0.012, -0.012, sd) * cutMask);
        float mh = 0.06 + 0.16 * vnoise(vec2(p.x * 2.2 + 3.0, 1.0)) + 0.09 * vnoise(vec2(p.x * 6.0, 4.0));
        col = mix(col, vec3(0.008, 0.0, 0.025), ss(0.004, -0.004, p.y - mh));
        col += duo(1.0) * ss(0.014, 0.0, abs(p.y - mh)) * 0.7;
    } else {
        float d = -p.y;
        float z = 0.55 / d;
        float zz = z + uTime * 1.2;
        vec2 g = abs(fract(vec2(p.x * z * 0.6, zz * 0.6)) - 0.5);
        float w = min(0.03 + 0.012 * z, 0.2);
        float line = max(ss(0.5 - w, 0.5, g.x), ss(0.5 - w, 0.5, g.y));
        float fog = ss(0.0, 0.45, d);
        col = vec3(0.02, 0.0, 0.06) * fog + duo(1.0) * line * (0.8 + 1.2 * beat) * fog + duo(0.0) * 0.5 * exp(-d * 9.0);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Starfield", R"glsl(
void main() {
    vec2 p = centered();
    vec3 col = duo(0.0) * 0.015;
    float kick = exp(-fract(uBeat) * 3.0);
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float z = fract(fi / 5.0 + uTime * 0.12);
        float scale = mix(22.0, 2.5, z);
        vec2 q = p * scale + vec2(fi * 17.3, fi * 9.1);
        vec2 id = floor(q);
        vec2 f = fract(q) - 0.5;
        float h = hash21(id);
        vec2 off = (vec2(h, fract(h * 37.7)) - 0.5) * 0.6;
        float d = length(f - off);
        float size = mix(0.04, 0.14, z) * (0.5 + h);
        float star = ss(size, 0.0, d) * step(0.5, h);
        float fade = ss(0.0, 0.15, z) * ss(1.0, 0.8, z);
        col += pal(h * 2.0 + fi * 0.13) * star * fade * (1.0 + kick);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Laser Fan", R"glsl(
void main() {
    vec2 p = centered();
    vec2 v = p - vec2(0.0, -1.05);
    float kick = exp(-fract(uBeat) * 2.0);
    float spread = 1.3 + 0.5 * sin(uTime * 0.6);
    vec3 col = duo(0.3) * 0.06 * exp(-length(v) * 0.8);
    for (int i = 0; i < 14; ++i) {
        float fi = float(i);
        float f = fi / 13.0 - 0.5;
        float ang = 1.5708 + f * spread + 0.25 * sin(uTime * 0.9 + fi * 0.7) * (0.4 + 0.6 * kick);
        vec2 dir = vec2(cos(ang), sin(ang));
        float along = dot(v, dir);
        float perp = abs(v.x * dir.y - v.y * dir.x);
        float fwd = ss(-0.05, 0.2, along);
        float core = ss(0.01, 0.0, perp);
        float halo = 0.004 / (perp + 0.004);
        float flick = 0.7 + 0.3 * sin(uTime * 7.0 + fi * 1.9);
        col += pal(fi * 0.07 + uBeat * 0.01) * (core + halo * halo * 0.6) * fwd * flick * exp(-along * 0.25);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Equalizer", R"glsl(
void main() {
    vec2 uv = vUv;
    float bars = 32.0;
    float bi = floor(uv.x * bars);
    float fx = fract(uv.x * bars);
    float h = clamp(0.04 + 1.1 * spec(pow((bi + 0.5) / bars, 1.3) * 0.9), 0.02, 0.95);
    float base = 0.3;
    bool up = uv.y >= base;
    float y = (up ? uv.y - base : base - uv.y) / 0.7;
    float segs = 24.0;
    float seg = floor(y * segs);
    float sf = fract(y * segs);
    float lit = step((seg + 0.5) / segs, h);
    float mask = ss(0.0, 0.1, fx) * ss(1.0, 0.9, fx) * ss(0.0, 0.12, sf) * ss(1.0, 0.85, sf);
    float peak = step(h, (seg + 1.5) / segs) * lit;
    vec3 c = mix(duo(clamp(y / 0.95, 0.0, 1.0)), vec3(1.0), peak * 0.7);
    float fade = up ? 1.0 : 0.25 * clamp(1.0 - y / 0.43, 0.0, 1.0);
    fragColor = vec4(c * lit * mask * fade + duo(0.0) * 0.02, 1.0);
}
)glsl", false});

    clips.push_back({"Julia", R"glsl(
void main() {
    vec2 z = centered() * 0.9;
    float a = uTime * 0.25 + uBeat * 0.02;
    vec2 c = 0.7885 * vec2(cos(a), sin(a));
    float it = 0.0;
    for (int i = 0; i < 72; ++i) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (dot(z, z) > 16.0) break;
        it += 1.0;
    }
    vec3 col = vec3(0.01);
    if (it < 71.0) {
        float sm = it - log2(max(log2(dot(z, z)), 0.0001)) + 4.0;
        col = pal(sm * 0.05 + 0.1 + uBeat * 0.02) * ss(0.0, 6.0, it);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    appendExtraClips(clips);
    return clips;
}

}

const std::vector<ClipDef>& clipLibrary() {
    static const std::vector<ClipDef> library = buildLibrary();
    return library;
}

int clipIndexByName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::vector<ClipDef>& library = clipLibrary();
    for (size_t i = 0; i < library.size(); ++i) {
        std::string candidate = library[i].name;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (candidate == lower) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const std::string& clipVertexSource() {
    static const std::string source = kVertex;
    return source;
}

std::string clipFragmentSource(const ClipDef& clip) {
    return std::string(kHeader) + clip.body;
}

}

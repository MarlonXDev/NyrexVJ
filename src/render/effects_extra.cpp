#include "render/effect_library_extra.hpp"

namespace nvj {

void appendExtraEffects(std::vector<EffectDef>& fx) {
    fx.push_back({"Radial Blur", {"Amount", "Center X", "Center Y", "Beat Pulse"}, {0.45f, 0.5f, 0.5f, 0.3f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = vec2(p.y, p.z);
    vec2 d = uv - c;
    float amt = p.x * 0.35 * (1.0 + p.w * 2.0 * exp(-fract(uBeat) * 5.0));
    vec3 acc = vec3(0.0);
    float w = 0.0;
    for (int i = 0; i < 20; ++i) {
        float t = float(i) / 19.0;
        float wt = 1.0 - t * 0.6;
        acc += texture(uTex, c + d * (1.0 - amt * t)).rgb * wt;
        w += wt;
    }
    return vec4(acc / w, 1.0);
}
)glsl", false, false});

    fx.push_back({"Light Rays", {"Intensity", "Length", "Threshold", "Decay"}, {0.5f, 0.6f, 0.45f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 base = texture(uTex, uv);
    vec2 d = (vec2(0.5) - uv) * mix(0.1, 0.9, p.y) / 28.0;
    vec2 q = uv;
    float decay = mix(0.9, 0.99, p.w);
    float w = 1.0;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 28; ++i) {
        q += d;
        acc += max(texture(uTex, q).rgb - p.z, 0.0) * w;
        w *= decay;
    }
    return vec4(base.rgb + acc * p.x * 0.35, base.a);
}
)glsl", false, false});

    fx.push_back({"Chromatic Aberration", {"Amount", "Falloff", "Beat Pulse", nullptr}, {0.4f, 0.6f, 0.3f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = uv - 0.5;
    float r = length(c * vec2(uAspect, 1.0));
    float amt = p.x * 0.08 * mix(1.0, r * 2.0, p.y) * (1.0 + p.z * 3.0 * exp(-fract(uBeat) * 5.0));
    vec2 d = c * amt;
    return vec4(texture(uTex, uv + d).r, texture(uTex, uv).g, texture(uTex, uv - d).b, 1.0);
}
)glsl", false, false});

    fx.push_back({"VHS", {"Distortion", "Noise", "Bleed", "Tracking"}, {0.5f, 0.4f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float t = uTime;
    float band = sin(uv.y * 3.0 + t * 0.7) * sin(uv.y * 17.0 - t * 2.3);
    float jit = (hash21(vec2(floor(uv.y * 240.0), floor(t * 30.0))) - 0.5) * 0.004 * p.x;
    float track = ss(0.96, 1.0, sin(uv.y * 1.5 - t * mix(0.2, 1.8, p.w)) * 0.5 + 0.5);
    float sh = band * 0.004 * p.x + jit + track * 0.06 * p.x;
    vec2 q = vec2(uv.x + sh, uv.y);
    float bleed = p.z * 0.012;
    vec3 c;
    c.r = texture(uTex, vec2(q.x + bleed, q.y)).r;
    c.g = texture(uTex, q).g;
    c.b = texture(uTex, vec2(q.x - bleed, q.y)).b;
    float n = hash21(uv * uRes + t);
    c = mix(c, vec3(n), p.y * 0.25 * (0.4 + track));
    float l = luma(c);
    c = mix(vec3(l), c, 0.8);
    c *= 0.9 + 0.1 * sin(uv.y * uRes.y * 1.5);
    return vec4(c, 1.0);
}
)glsl", false, false});

    fx.push_back({"CRT", {"Curvature", "Mask", "Scanlines", "Flicker"}, {0.5f, 0.5f, 0.5f, 0.2f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = uv * 2.0 - 1.0;
    c *= 1.0 + p.x * 0.25 * dot(c, c);
    vec2 q = c * 0.5 + 0.5;
    float inside = step(0.0, q.x) * step(q.x, 1.0) * step(0.0, q.y) * step(q.y, 1.0);
    vec3 col = texture(uTex, clamp(q, 0.0, 1.0)).rgb;
    float m = mod(floor(q.x * uRes.x), 3.0);
    vec3 mask = vec3(m < 1.0 ? 1.0 : 0.55, (m >= 1.0 && m < 2.0) ? 1.0 : 0.55, m >= 2.0 ? 1.0 : 0.55);
    col *= mix(vec3(1.0), mask * 1.25, p.y);
    float sl = 0.5 + 0.5 * sin(q.y * uRes.y * PI);
    col *= 1.0 - p.z * 0.6 * (1.0 - sl);
    col *= 1.0 - p.w * 0.12 * (0.5 + 0.5 * sin(uTime * 60.0));
    float vig = 1.0 - 0.35 * dot(c, c) * p.x;
    return vec4(col * vig * inside, 1.0);
}
)glsl", false, false});

    fx.push_back({"Halftone", {"Size", "Angle", "Softness", "Color"}, {0.45f, 0.25f, 0.3f, 0.7f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float n = mix(160.0, 30.0, p.x);
    float a = p.y * PI;
    vec2 g = rot(a) * ((uv - 0.5) * vec2(uAspect, 1.0)) * n;
    vec2 cell = floor(g) + 0.5;
    vec2 cuv = rot(-a) * (cell / n) / vec2(uAspect, 1.0) + 0.5;
    vec3 col = texture(uTex, clamp(cuv, 0.0, 1.0)).rgb;
    float l = luma(col);
    float d = length(fract(g) - 0.5);
    float rad = sqrt(l) * 0.72;
    float soft = mix(0.02, 0.25, p.z);
    float m = 1.0 - ss(rad - soft, rad + soft * 0.2 + 0.001, d);
    vec3 dotc = mix(vec3(1.0), clamp(col / max(l, 0.08), 0.0, 1.5), p.w);
    return vec4(dotc * m, 1.0);
}
)glsl", false, false});

    fx.push_back({"Hex Mosaic", {"Size", "Gap", "Beat Pulse", nullptr}, {0.45f, 0.3f, 0.3f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float n = mix(60.0, 8.0, p.x) * (1.0 - p.z * 0.3 * exp(-fract(uBeat) * 4.0));
    vec2 g = (uv - 0.5) * vec2(uAspect, 1.0) * n;
    vec2 s = vec2(1.0, 1.7320508);
    vec2 a = mod(g, s) - s * 0.5;
    vec2 b = mod(g - s * 0.5, s) - s * 0.5;
    vec2 gv = dot(a, a) < dot(b, b) ? a : b;
    vec2 center = g - gv;
    vec2 q = center / n / vec2(uAspect, 1.0) + 0.5;
    vec2 ag = abs(gv);
    float hd = max(dot(ag, normalize(vec2(1.0, 1.7320508))), ag.x);
    float edge = 0.5 - p.y * 0.3;
    float m = 1.0 - ss(edge - 0.03, edge, hd);
    return vec4(texture(uTex, clamp(q, 0.0, 1.0)).rgb * m, 1.0);
}
)glsl", false, false});

    fx.push_back({"Slice Shift", {"Slices", "Amount", "Speed", "Direction"}, {0.4f, 0.5f, 0.5f, 0.0f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float n = floor(mix(3.0, 40.0, p.x) + 0.5);
    bool horiz = p.w < 0.5;
    float coord = horiz ? uv.y : uv.x;
    float id = floor(coord * n);
    float rate = mix(0.5, 4.0, p.z);
    float t = floor(uBeat * rate);
    float f = fract(uBeat * rate);
    float h = hash21(vec2(id, t)) - 0.5;
    float h2 = hash21(vec2(id, t + 1.0)) - 0.5;
    float off = mix(h, h2, ss(0.6, 1.0, f)) * p.y * 0.8;
    vec2 q = horiz ? vec2(uv.x + off, uv.y) : vec2(uv.x, uv.y + off);
    return texture(uTex, mrep(q));
}
)glsl", false, false});

    fx.push_back({"Shockwave", {"Strength", "Width", "Speed", "Beat Sync"}, {0.6f, 0.4f, 0.4f, 1.0f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float r = length(c);
    float ph = p.w > 0.5 ? fract(uBeat * 0.5) : fract(uTime * mix(0.1, 1.0, p.z));
    float radius = ph * 1.4;
    float w = mix(0.03, 0.25, p.y);
    float d = r - radius;
    float k = exp(-d * d / (w * w)) * (1.0 - ph);
    vec2 dir = c / max(r, 0.0001);
    vec2 q = c - dir * k * p.x * 0.2;
    vec3 col = texture(uTex, clamp(q / vec2(uAspect, 1.0) + 0.5, 0.0, 1.0)).rgb;
    col += k * p.x * 0.15;
    return vec4(col, 1.0);
}
)glsl", false, false});

    fx.push_back({"Ripple", {"Amount", "Frequency", "Speed", "Decay"}, {0.5f, 0.4f, 0.4f, 0.3f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float r = length(c);
    float f = mix(6.0, 60.0, p.y);
    float wv = sin(r * f - uTime * mix(0.0, 8.0, p.z));
    float amp = p.x * 0.03 * exp(-r * mix(0.0, 4.0, p.w)) * (1.0 + uKick);
    vec2 dir = c / max(r, 0.0001);
    vec2 q = c + dir * wv * amp;
    return texture(uTex, mrep(q / vec2(uAspect, 1.0) + 0.5));
}
)glsl", false, false});

    fx.push_back({"Polar Coordinates", {"Mix", "Zoom", "Rotation", "Repeat"}, {1.0f, 0.5f, 0.0f, 0.2f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float r = length(c) * 2.0;
    float a = atan(c.y, c.x) / (2.0 * PI) + 0.5 + p.z;
    float rep = floor(mix(1.0, 6.0, p.w) + 0.5);
    vec2 polar = vec2(a * rep * 2.0, r * pow(2.0, mix(-1.0, 1.0, p.y)));
    return texture(uTex, mix(uv, mrep(polar), p.x));
}
)glsl", false, false});

    fx.push_back({"Infinite Zoom", {"Speed", "Layers", "Rotate", "Overlap"}, {0.5f, 0.6f, 0.5f, 0.4f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float t = uTime * mix(0.0, 0.6, p.x);
    float spin = (p.z - 0.5) * 0.6;
    int n = int(mix(2.0, 5.0, p.y) + 0.5);
    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 5; ++i) {
        if (i >= n) break;
        float ph = fract(t + float(i) / float(n));
        float s = pow(2.0, ph * 3.0);
        vec2 q = rot(spin * ph * 3.0) * c / s;
        float w = pow(sin(ph * PI), mix(0.5, 3.0, p.w));
        acc += texture(uTex, mrep(q / vec2(uAspect, 1.0) + 0.5)).rgb * w;
        wsum += w;
    }
    return vec4(acc / max(wsum, 0.001), 1.0);
}
)glsl", false, false});

    fx.push_back({"Flow Displace", {"Amount", "Scale", "Speed", "Detail"}, {0.5f, 0.4f, 0.4f, 0.5f}, R"glsl(
float fbm2(vec2 p, float detail) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        float on = float(i) < mix(1.0, 4.0, detail) ? 1.0 : 0.0;
        s += vn(p) * a * on;
        p = p * 2.03 + 11.7;
        a *= 0.5;
    }
    return s;
}

vec4 effect(vec2 uv, vec4 p) {
    float sc = mix(1.5, 8.0, p.y);
    float t = uTime * p.z;
    vec2 q = uv * sc * vec2(uAspect, 1.0);
    vec2 d = vec2(fbm2(q + vec2(t, 0.0), p.w) - 0.45, fbm2(q.yx + vec2(5.2, -t), p.w) - 0.45);
    return texture(uTex, mrep(uv + d * p.x * 0.3));
}
)glsl", false, false});

    fx.push_back({"Duotone", {"Shadows", "Highlights", "Contrast", "Mix"}, {0.75f, 0.12f, 0.6f, 1.0f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float lo = mix(0.0, 0.35, p.z);
    float l = smoothstep(lo, 1.0 - lo, luma(c.rgb));
    vec3 col = mix(hc(p.x) * 0.3, hc(p.y), l);
    return vec4(mix(c.rgb, col, p.w), c.a);
}
)glsl", false, false});

    fx.push_back({"Solarize", {"Threshold", "Mix", "Hue Shift", nullptr}, {0.5f, 1.0f, 0.0f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    vec3 s = mix(c.rgb, 1.0 - c.rgb, step(vec3(p.x), c.rgb));
    s = hueShift(s, p.z * 0.5);
    return vec4(mix(c.rgb, s, p.y), c.a);
}
)glsl", false, false});

    fx.push_back({"Neon Edge", {"Strength", "Glow", "Hue", "Rainbow"}, {0.5f, 0.5f, 0.55f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 px = mix(1.0, 3.0, p.y) / uRes;
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
    float e = clamp(length(vec2(gx, gy)) * mix(2.0, 20.0, p.x), 0.0, 1.0);
    vec3 fixedCol = hc(p.z);
    vec3 moving = hc(luma(texture(uTex, uv).rgb) * 1.5 + uTime * 0.1 + p.z);
    vec3 neon = mix(fixedCol, moving, p.w);
    vec3 base = texture(uTex, uv).rgb * 0.15;
    return vec4(base + neon * e * (1.0 + p.y * 1.5), 1.0);
}
)glsl", false, false});

    fx.push_back({"Sharpen", {"Amount", "Radius", nullptr, nullptr}, {0.5f, 0.4f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 px = mix(0.7, 2.5, p.y) / uRes;
    vec4 c = texture(uTex, uv);
    vec3 blur = (texture(uTex, uv + vec2(px.x, 0.0)).rgb + texture(uTex, uv - vec2(px.x, 0.0)).rgb + texture(uTex, uv + vec2(0.0, px.y)).rgb + texture(uTex, uv - vec2(0.0, px.y)).rgb) * 0.25;
    return vec4(max(c.rgb + (c.rgb - blur) * p.x * 4.0, 0.0), c.a);
}
)glsl", false, false});

    fx.push_back({"Camera Shake", {"Amount", "Speed", "Beat Kick", "Roll"}, {0.4f, 0.5f, 0.6f, 0.3f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float t = uTime * mix(4.0, 30.0, p.y);
    vec2 o = vec2(sin(t * 1.3) + sin(t * 2.7 + 1.0), sin(t * 1.7 + 2.0) + sin(t * 3.1)) * 0.005 * p.x;
    float kick = exp(-fract(uBeat) * 8.0) * p.z;
    o += vec2(sin(uBeat * 40.0), cos(uBeat * 33.0)) * 0.02 * kick;
    float roll = (sin(t * 0.9) * 0.01 + kick * 0.02) * p.w * 3.0;
    vec2 c = rot(roll) * ((uv - 0.5) * vec2(uAspect, 1.0));
    return texture(uTex, mrep(c / vec2(uAspect, 1.0) + 0.5 + o));
}
)glsl", false, false});

    fx.push_back({"Film Grain", {"Amount", "Size", "Color", "Flicker"}, {0.4f, 0.3f, 0.3f, 0.2f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float sz = mix(1.0, 4.0, p.y);
    vec2 g = floor(uv * uRes / sz);
    float t = floor(uTime * 24.0);
    float n = hash21(g + t * 17.0) - 0.5;
    vec3 nc = vec3(n, hash21(g + t * 31.0 + 7.0) - 0.5, hash21(g + t * 53.0 + 3.0) - 0.5);
    vec3 grain = mix(vec3(n), nc, p.z);
    float fl = 1.0 + (hash21(vec2(t, 1.0)) - 0.5) * 0.3 * p.w;
    return vec4((c.rgb + grain * p.x * 0.6) * fl, c.a);
}
)glsl", false, false});

    fx.push_back({"Threshold", {"Level", "Softness", "Invert", "Color"}, {0.2f, 0.2f, 0.0f, 0.3f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float l = luma(c.rgb);
    float s = mix(0.005, 0.3, p.y);
    float m = ss(p.x - s, p.x + s, l);
    m = mix(m, 1.0 - m, step(0.5, p.z));
    vec3 col = mix(vec3(m), c.rgb * m * 1.5, p.w);
    return vec4(col, 1.0);
}
)glsl", false, false});

    fx.push_back({"Color Echo", {"Echoes", "Distance", "Angle", "Hue Step"}, {0.5f, 0.5f, 0.1f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    int n = int(mix(2.0, 6.0, p.x) + 0.5);
    float a = p.z * 6.28318;
    vec2 dir = vec2(cos(a), sin(a)) * vec2(1.0 / uAspect, 1.0) * mix(0.005, 0.05, p.y);
    vec3 acc = texture(uTex, uv).rgb;
    for (int i = 1; i < 6; ++i) {
        if (i >= n) break;
        float f = float(i);
        vec3 s = texture(uTex, mrep(uv + dir * f)).rgb;
        s = hueShift(s, p.w * 0.25 * f);
        acc = max(acc, s * (1.0 - f / float(n)));
    }
    return vec4(acc, 1.0);
}
)glsl", false, false});

    fx.push_back({"Spectrum Warp", {"Amount", "Range", "Direction", "Mirror"}, {0.5f, 0.7f, 0.0f, 1.0f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    bool horiz = p.z < 0.5;
    float x = horiz ? uv.x : uv.y;
    float xm = p.w > 0.5 ? abs(x * 2.0 - 1.0) : x;
    float s = spec(xm * mix(0.4, 1.0, p.y));
    float amt = p.x * 0.15 * s;
    vec2 q = horiz ? vec2(uv.x, uv.y + amt) : vec2(uv.x + amt, uv.y);
    return texture(uTex, mrep(q));
}
)glsl", false, false});

    fx.push_back({"Oscilloscope", {"Height", "Thickness", "Glow", "Hue"}, {0.5f, 0.4f, 0.5f, 0.55f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float w = wave(uv.x) * mix(0.1, 0.45, p.x);
    float d = abs(uv.y - 0.5 - w);
    float th = mix(0.002, 0.012, p.y);
    float line = ss(th * 2.0, th, d);
    float glow = exp(-d * mix(80.0, 12.0, p.z)) * 0.5;
    return vec4(c.rgb + hc(p.w) * (line + glow), 1.0);
}
)glsl", false, false});

    fx.push_back({"Pixel Smear", {"Length", "Angle", "Threshold", "Beat Pulse"}, {0.5f, 0.25f, 0.4f, 0.3f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float a = p.y * 6.28318;
    vec2 dir = vec2(cos(a), sin(a)) * vec2(1.0 / uAspect, 1.0);
    float len = p.x * 0.25 * (1.0 + p.w * 1.5 * exp(-fract(uBeat) * 4.0));
    vec3 best = texture(uTex, uv).rgb;
    float bl = luma(best);
    for (int i = 1; i <= 24; ++i) {
        float t = float(i) / 24.0;
        vec3 s = texture(uTex, mrep(uv - dir * len * t)).rgb;
        float raw = luma(s);
        float l = raw * (1.0 - t);
        if (l > bl && raw > p.z) {
            best = s * (1.0 - t * 0.5);
            bl = l;
        }
    }
    return vec4(best, 1.0);
}
)glsl", false, false});

    fx.push_back({"Anamorphic Streak", {"Intensity", "Length", "Threshold", "Tint"}, {0.4f, 0.6f, 0.5f, 0.55f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 base = texture(uTex, uv);
    float len = mix(0.05, 0.5, p.y);
    vec3 acc = vec3(0.0);
    float w = 0.0;
    for (int i = -16; i <= 16; ++i) {
        float t = float(i) / 16.0;
        float k = exp(-t * t * 3.0);
        vec3 s = texture(uTex, vec2(clamp(uv.x + t * len, 0.0, 1.0), uv.y)).rgb;
        acc += max(s - p.z, 0.0) * k;
        w += k;
    }
    acc /= w;
    return vec4(base.rgb + acc * hc(p.w) * p.x * 6.0, base.a);
}
)glsl", false, false});

    fx.push_back({"Dither", {"Levels", "Size", "Mono", "Mix"}, {0.2f, 0.3f, 0.0f, 1.0f}, R"glsl(
const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

float bayer(vec2 p) {
    vec2 m = mod(floor(p), 4.0);
    return kBayer[int(m.x + m.y * 4.0)];
}

vec4 effect(vec2 uv, vec4 p) {
    vec4 c = texture(uTex, uv);
    float sz = floor(mix(1.0, 4.0, p.y) + 0.5);
    float th = bayer(uv * uRes / sz) / 16.0 - 0.5;
    float n = floor(mix(2.0, 8.0, p.x) + 0.5);
    vec3 src = p.z > 0.5 ? vec3(luma(c.rgb)) : c.rgb;
    vec3 d = clamp(floor(src * (n - 1.0) + 0.5 + th) / (n - 1.0), 0.0, 1.0);
    return vec4(mix(c.rgb, d, p.w), c.a);
}
)glsl", false, false});

    fx.push_back({"Block Glitch", {"Amount", "Block Size", "Rate", nullptr}, {0.4f, 0.5f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float n = mix(40.0, 6.0, p.y);
    vec2 cell = floor(uv * vec2(n * uAspect, n));
    float t = floor(uTime * mix(2.0, 20.0, p.z));
    float h = hash21(cell + t * 7.13);
    float on = step(1.0 - p.x * 0.6, h);
    vec2 jump = (vec2(hash21(cell + t), hash21(cell.yx + t * 3.7)) - 0.5) * 0.3 * on;
    vec3 col = texture(uTex, mrep(uv + jump)).rgb;
    float swap = on * step(0.5, hash21(cell + 9.1 + t));
    col = mix(col, col.gbr, swap * 0.6);
    return vec4(col, 1.0);
}
)glsl", false, false});

    fx.push_back({"Prism", {"Copies", "Spread", "Spin", "Beat Pulse"}, {0.4f, 0.4f, 0.4f, 0.4f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    int n = int(mix(3.0, 8.0, p.x) + 0.5);
    float r = mix(0.01, 0.08, p.y) * (1.0 + exp(-fract(uBeat) * 4.0) * p.w);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 8; ++i) {
        if (i >= n) break;
        float f = float(i) / float(n);
        float a = f * 6.28318 + uTime * (p.z - 0.5) * 2.0;
        vec2 o = vec2(cos(a), sin(a)) * r * vec2(1.0 / uAspect, 1.0);
        acc += texture(uTex, mrep(uv + o)).rgb * hc(f);
    }
    return vec4(acc * 2.0 / float(n), 1.0);
}
)glsl", false, false});

    fx.push_back({"Sphere Map", {"Radius", "Rotation", "Spin", "Light"}, {0.5f, 0.0f, 0.6f, 0.7f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0) * 2.0;
    vec3 ro = vec3(0.0, 0.0, -3.0);
    vec3 rd = normalize(vec3(c, 2.0));
    float R = mix(0.6, 1.4, p.x);
    float b = dot(ro, rd);
    float cc = dot(ro, ro) - R * R;
    float h = b * b - cc;
    if (h < 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    float t = -b - sqrt(h);
    vec3 pos = ro + rd * t;
    vec3 nn = normalize(pos);
    vec3 n = nn;
    n.xz = rot(p.y * 6.28318 + uTime * (p.z - 0.5) * 2.0) * n.xz;
    float u = atan(n.z, n.x) / (2.0 * PI) + 0.5;
    float v = asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5;
    vec3 col = texture(uTex, vec2(u, v)).rgb;
    float diff = mix(1.0, 0.25 + 0.75 * max(dot(nn, normalize(vec3(-0.5, 0.6, -0.7))), 0.0), p.w);
    float rim = pow(1.0 - max(dot(nn, -rd), 0.0), 3.0) * p.w * 0.6;
    return vec4(col * diff + rim, 1.0);
}
)glsl", true, false});

    fx.push_back({"Cube Spin", {"Size", "Spin", "Tilt", "Edges"}, {0.5f, 0.5f, 0.4f, 0.6f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0) * 2.0;
    vec3 ro = vec3(0.0, 0.0, -4.0);
    vec3 rd = normalize(vec3(c, 2.2));
    float ay = uTime * mix(0.0, 1.5, p.y);
    float ax = p.z * 1.2 - 0.3;
    ro.xz = rot(ay) * ro.xz;
    rd.xz = rot(ay) * rd.xz;
    ro.yz = rot(ax) * ro.yz;
    rd.yz = rot(ax) * rd.yz;
    float s = mix(0.6, 1.4, p.x);
    vec3 inv = 1.0 / (rd + vec3(0.00001));
    vec3 t1 = (-vec3(s) - ro) * inv;
    vec3 t2 = (vec3(s) - ro) * inv;
    vec3 tmin = min(t1, t2);
    vec3 tmax = max(t1, t2);
    float tn = max(max(tmin.x, tmin.y), tmin.z);
    float tf = min(min(tmax.x, tmax.y), tmax.z);
    if (tn > tf || tf < 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 pos = ro + rd * tn;
    vec3 ap = abs(pos) / s;
    vec2 fuv;
    float shade;
    if (ap.x > ap.y && ap.x > ap.z) {
        fuv = pos.zy;
        shade = 0.8;
    } else if (ap.y > ap.z) {
        fuv = pos.xz;
        shade = 1.0;
    } else {
        fuv = pos.xy;
        shade = 0.65;
    }
    fuv = fuv / s * 0.5 + 0.5;
    vec3 col = texture(uTex, clamp(fuv, 0.0, 1.0)).rgb * shade;
    float e = ss(0.93, 0.99, max(abs(fuv.x - 0.5), abs(fuv.y - 0.5)) * 2.0);
    col = mix(col, hc(uTime * 0.1), e * p.w * 0.8);
    return vec4(col, 1.0);
}
)glsl", true, false});

    fx.push_back({"Cylinder Wrap", {"Radius", "Spin", "Tilt", "Height"}, {0.5f, 0.6f, 0.4f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0) * 2.0;
    vec3 ro = vec3(0.0, 0.0, -3.5);
    vec3 rd = normalize(vec3(c, 2.0));
    float tilt = (p.z - 0.5) * 0.8;
    ro.yz = rot(tilt) * ro.yz;
    rd.yz = rot(tilt) * rd.yz;
    float R = mix(0.6, 1.5, p.x);
    float a = dot(rd.xz, rd.xz);
    float b = dot(ro.xz, rd.xz);
    float cc = dot(ro.xz, ro.xz) - R * R;
    float h = b * b - a * cc;
    if (h < 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    float t = (-b - sqrt(h)) / a;
    vec3 pos = ro + rd * t;
    float hgt = mix(0.9, 1.6, p.w);
    if (abs(pos.y) > hgt) return vec4(0.0, 0.0, 0.0, 1.0);
    float ang = atan(pos.x, pos.z) / (2.0 * PI) + 0.5 + uTime * (p.y - 0.5) * 0.5;
    vec2 tuv = mrep(vec2(ang * 2.0, pos.y / hgt * 0.5 + 0.5));
    vec3 nrm = normalize(vec3(pos.x, 0.0, pos.z));
    float shade = 0.35 + 0.65 * max(dot(nrm, -rd), 0.0);
    return vec4(texture(uTex, tuv).rgb * shade, 1.0);
}
)glsl", true, false});

    fx.push_back({"Floor Reflect", {"Horizon", "Fade", "Ripple", "Blur"}, {0.5f, 0.5f, 0.4f, 0.4f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    float hz = mix(0.35, 0.75, p.x);
    float k = 1.0 / max(1.0 - hz, 0.05);
    vec3 col;
    if (uv.y >= hz) {
        col = texture(uTex, clamp(vec2(uv.x, (uv.y - hz) * k), 0.0, 1.0)).rgb;
    } else {
        float d = hz - uv.y;
        float rp = sin(d * 60.0 + uTime * 2.0 + uv.x * 8.0) * 0.004 * p.z * (0.3 + d * 4.0);
        vec2 q = vec2(uv.x + rp, d * k);
        float bl = d * p.w * 0.05;
        vec3 acc = texture(uTex, clamp(q + vec2(bl, 0.0), 0.0, 1.0)).rgb;
        acc += texture(uTex, clamp(q - vec2(bl, 0.0), 0.0, 1.0)).rgb;
        acc += texture(uTex, clamp(q + vec2(0.0, bl), 0.0, 1.0)).rgb;
        acc += texture(uTex, clamp(q - vec2(0.0, bl), 0.0, 1.0)).rgb;
        col = acc * 0.25 * (1.0 - ss(0.0, mix(0.15, 1.0, p.y), d)) * 0.85;
    }
    return vec4(col, 1.0);
}
)glsl", true, false});

    fx.push_back({"Plane 3D", {"Tilt X", "Tilt Y", "Perspective", "Swing"}, {0.5f, 0.5f, 0.4f, 0.6f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0) * 2.0;
    float dist = mix(1.5, 6.0, p.z);
    vec3 ro = vec3(0.0, 0.0, -dist);
    vec3 rd = normalize(vec3(c, dist));
    float ay = (p.y - 0.5) * 2.4 + p.w * sin(uTime * 0.8) * 0.8;
    float ax = (p.x - 0.5) * 2.4 + p.w * sin(uTime * 0.6 + 1.0) * 0.4;
    ro.xz = rot(ay) * ro.xz;
    rd.xz = rot(ay) * rd.xz;
    ro.yz = rot(ax) * ro.yz;
    rd.yz = rot(ax) * rd.yz;
    if (rd.z < 0.0001) return vec4(0.0, 0.0, 0.0, 1.0);
    float t = -ro.z / rd.z;
    if (t < 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 pos = ro + rd * t;
    vec2 q = pos.xy / vec2(uAspect, 1.0) * 0.5 + 0.5;
    float inside = step(0.0, q.x) * step(q.x, 1.0) * step(0.0, q.y) * step(q.y, 1.0);
    return vec4(texture(uTex, clamp(q, 0.0, 1.0)).rgb * inside, 1.0);
}
)glsl", true, false});
}

}

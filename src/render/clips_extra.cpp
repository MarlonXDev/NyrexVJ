#include "render/clip_library_extra.hpp"

namespace nvj {

namespace {

const char* kCrystalCommon = R"glsl(
vec2 cj(vec2 c, float t) {
    vec2 o = h22(c);
    return 0.5 + 0.42 * sin(t + 6.28318 * o);
}

vec4 shards(vec2 x, float t) {
    vec2 n = floor(x);
    vec2 f = fract(x);
    vec2 mg = vec2(0.0);
    vec2 mr = vec2(0.0);
    float md = 8.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 g = vec2(float(i), float(j));
            vec2 r = g + cj(n + g, t) - f;
            float d = dot(r, r);
            if (d < md) {
                md = d;
                mr = r;
                mg = g;
            }
        }
    }
    md = 8.0;
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            vec2 g = mg + vec2(float(i), float(j));
            vec2 r = g + cj(n + g, t) - f;
            vec2 dv = r - mr;
            if (dot(dv, dv) > 0.00001) {
                md = min(md, dot(0.5 * (mr + r), normalize(dv)));
            }
        }
    }
    return vec4(md, n + mg, mr.x);
}

vec2 tunnelUv(vec2 p, float segments, float depthScale, float travel, out float radius) {
    radius = max(length(p), 0.0001);
    float a = atan(p.y, p.x);
    float seg = PI / segments;
    a = abs(mod(a + 0.35 * sin(uTime * 0.11), seg * 2.0) - seg);
    float z = depthScale / (radius + 0.12) + travel;
    return vec2(a / seg * 1.6, z);
}
)glsl";

}

void appendExtraClips(std::vector<ClipDef>& clips) {
    clips.push_back({"Mandelbrot", R"glsl(
void main() {
    vec2 p = centered();
    float zoom = exp(-3.2 * (0.5 - 0.5 * cos(uTime * 0.12)));
    vec2 target = vec2(-0.743643887037151, 0.131825904205330);
    vec2 c = target + p * 1.3 * zoom;
    vec2 z = vec2(0.0);
    float it = 0.0;
    float trap = 1e9;
    for (int i = 0; i < 180; ++i) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        trap = min(trap, abs(length(z) - 0.6 - 0.15 * sin(uBeat * 3.1416)));
        if (dot(z, z) > 256.0) break;
        it += 1.0;
    }
    vec3 col = duo(0.0) * 0.03;
    if (it < 179.0) {
        float sm = it - log2(max(log2(dot(z, z)), 0.0001)) + 4.0;
        col = pal(sm * 0.025 + uBeat * 0.02) * (0.35 + 0.65 * ss(0.0, 12.0, it));
    }
    col += duo(0.85) * exp(-trap * 7.0) * (0.25 + 0.5 * uAudio.x);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Metaballs", R"glsl(
void main() {
    vec2 p = centered();
    float field = 0.0;
    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        vec2 c = vec2(sin(uTime * (0.4 + fi * 0.13) + fi * 1.7) * 0.95, cos(uTime * (0.33 + fi * 0.11) + fi * 2.3) * 0.55);
        float r = 0.15 + 0.04 * sin(fi * 3.0 + uTime) + uAudio.x * 0.1 + 0.05 * exp(-fract(uBeat) * 4.0);
        float d = length(p - c);
        field += r * r / (d * d + 0.0008);
    }
    float inside = ss(0.95, 1.05, field);
    vec3 col = duo(0.0) * 0.04;
    col += pal(field * 0.1 + uTime * 0.04) * inside * (0.45 + 0.55 * ss(1.0, 3.5, field));
    col += duo(0.9) * exp(-abs(field - 1.0) * 5.0) * 0.55;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Voronoi", R"glsl(
void main() {
    vec2 p = centered() * 3.0;
    vec2 i = floor(p);
    vec2 f = fract(p);
    float d1 = 8.0;
    float d2 = 8.0;
    vec2 id = vec2(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = h22(i + g);
            o = 0.5 + 0.5 * sin(uTime * (0.6 + o * 0.8) + 6.2831 * o);
            vec2 r = g + o - f;
            float d = dot(r, r);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                id = i + g;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    float cell = hash21(id);
    float edge = ss(0.07 + 0.05 * uKick, 0.0, sqrt(d2) - sqrt(d1));
    vec3 col = pal(cell + uTime * 0.05) * (0.2 + 0.5 * (1.0 - sqrt(d1))) * (0.35 + 1.1 * spec(cell * 0.8));
    col += duo(0.9) * edge * 1.1;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Lightning", R"glsl(
void main() {
    vec2 p = centered();
    float tick = uBeat * 2.0;
    float flash = exp(-fract(tick) * 3.0);
    vec3 col = duo(0.0) * 0.03 + duo(0.5) * (uKick * 0.06 + flash * 0.1);
    for (int k = 0; k < 3; ++k) {
        float fk = float(k);
        float seed = floor(tick + fk * 3.0) + fk * 11.0;
        float bx = (hash21(vec2(seed, 1.3)) * 2.0 - 1.0) * 1.1;
        float y = p.y * 0.9 + fk * 1.7;
        float xx = (vnoise(vec2(y * 1.7 + seed * 7.1, seed)) - 0.5) * 0.9;
        xx += (vnoise(vec2(y * 6.5 + seed * 3.3, seed + 5.0)) - 0.5) * 0.3;
        xx += (vnoise(vec2(y * 23.0 + seed, seed + 9.0)) - 0.5) * 0.1;
        float d = abs(p.x - xx - bx);
        float fork = abs(p.x - xx - bx - (p.y + 0.2) * 0.55 * step(0.0, p.y + 0.2));
        float bolt = exp(-d * 150.0) + exp(-d * 16.0) * 0.3 + exp(-fork * 220.0) * 0.5 * step(0.4, hash21(vec2(seed, 7.7)));
        float on = 0.45 + 0.55 * step(0.2, hash21(vec2(seed, fk)));
        col += pal(fk * 0.3 + uTime * 0.05) * bolt * on * (0.5 + flash * 1.1);
        col += vec3(1.0) * exp(-d * 320.0) * on * (0.3 + flash * 0.7);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Fire", R"glsl(
void main() {
    vec2 uv = vUv;
    vec2 p = vec2((uv.x - 0.5) * uRes.x / uRes.y, uv.y);
    float t = uTime;
    vec2 q = vec2(p.x * 3.0, p.y * 2.2 - t * 1.5);
    float n = fbm(q + vec2(0.0, fbm(q * 1.3 + t * 0.3)));
    float h = clamp(1.0 - p.y * (1.15 - uAudio.x * 0.35 - uKick * 0.15), 0.0, 1.0);
    float w = clamp(1.0 - pow(abs(p.x) * 0.9, 1.5), 0.0, 1.0);
    float f = clamp(n * h * w * 2.6 - 0.1, 0.0, 1.0);
    vec3 col = mix(vec3(0.0), pal(0.0), ss(0.0, 0.35, f));
    col = mix(col, pal(0.45), ss(0.3, 0.65, f));
    col = mix(col, vec3(1.0), ss(0.65, 1.0, f));
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Rings Burst", R"glsl(
void main() {
    vec2 p = centered();
    float r = length(p);
    vec3 col = vec3(0.0);
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float ph = fract(uBeat * 0.25 + fi / 6.0);
        float rad = ph * 2.6;
        float th = 0.015 + 0.05 * (1.0 - ph) + spec(fi / 6.0) * 0.05;
        float ring = ss(th, 0.0, abs(r - rad)) * (1.0 - ph);
        col += pal(fi * 0.16 + ph * 0.3) * ring;
    }
    col += duo(0.5) * exp(-r * 4.0) * (0.15 + uKick * 0.6);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Hex Grid", R"glsl(
void main() {
    vec2 p = centered() * 3.0;
    vec2 s = vec2(1.0, 1.7320508);
    vec2 a = mod(p, s) - s * 0.5;
    vec2 b = mod(p - s * 0.5, s) - s * 0.5;
    vec2 gv = dot(a, a) < dot(b, b) ? a : b;
    vec2 id = p - gv;
    vec2 ag = abs(gv);
    float hd = max(dot(ag, normalize(vec2(1.0, 1.7320508))), ag.x);
    float d = length(id);
    float wv = 0.5 + 0.5 * sin(d * 1.6 - uBeat * 1.5708);
    float lvl = clamp(wv * 0.55 + spec(clamp(d / 6.0, 0.0, 1.0)) * 0.9, 0.05, 1.0);
    float size = 0.46 * lvl;
    float body = 1.0 - ss(size - 0.03, size, hd);
    float line = ss(0.44, 0.5, hd);
    vec3 col = pal(d * 0.1 + uTime * 0.1) * body + duo(0.2) * line * 0.3;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Warp Speed", R"glsl(
void main() {
    vec2 p = centered();
    float r = length(p);
    float a = atan(p.y, p.x) / (2.0 * PI);
    vec3 col = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float fi = float(i);
        float n = 60.0 + fi * 40.0;
        float id = floor(a * n);
        float h = hash21(vec2(id, fi));
        float speed = 0.5 + h * 1.5;
        float z = fract(h * 7.0 + uTime * speed * 0.35 + uBeat * 0.05);
        float rr = z * z * 3.0;
        float len = 0.05 + z * 0.55 * (1.0 + uKick * 0.8);
        float fa = fract(a * n) - 0.5;
        float w = abs(fa) * r * 6.2832 / n;
        float across = ss(0.005, 0.0, w);
        float along = ss(rr - len, rr, r) * ss(rr + 0.02, rr, r);
        col += pal(h + fi * 0.1) * across * along * (0.35 + z);
    }
    col += duo(0.6) * exp(-length(p) * 5.0) * 0.12;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Oscilloscope", R"glsl(
void main() {
    vec2 p = centered();
    vec3 col = duo(0.0) * 0.03;
    for (int k = 0; k < 3; ++k) {
        float fk = float(k);
        float x = vUv.x - fk * 0.012;
        float y = clamp(wave(x) * 2.2, -1.0, 1.0) * (0.3 + uAudio.w * 0.15) * (1.0 - fk * 0.25);
        float d = abs(p.y - y);
        col += pal(fk * 0.25 + vUv.x * 0.3 + uTime * 0.05) * (exp(-d * 120.0) + exp(-d * 14.0) * 0.18);
    }
    vec2 g = abs(fract(p * 4.0) - 0.5);
    col += duo(0.2) * 0.05 * ss(0.47, 0.5, max(g.x, g.y));
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Spectrum Circle", R"glsl(
void main() {
    vec2 p = centered();
    float r = length(p);
    float t = abs(atan(p.y, p.x)) / PI;
    float bars = 72.0;
    float id = floor(t * bars);
    float f = (id + 0.5) / bars;
    float s = spec(f * 0.8);
    float base = 0.45 + uKick * 0.05;
    float len = s * 0.6;
    float fa = fract(t * bars) - 0.5;
    float width = ss(0.42, 0.3, abs(fa));
    float inBar = step(base, r) * step(r, base + len);
    float k = (r - base) / max(len, 0.01);
    vec3 col = pal(f + uTime * 0.05) * inBar * width * (0.5 + 0.5 * k);
    col += duo(0.3) * ss(0.012, 0.0, abs(r - base));
    col += duo(0.8) * exp(-abs(r - base) * 9.0) * 0.18 * (0.5 + uAudio.x);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Liquid Marble", R"glsl(
void main() {
    vec2 p = centered() * 1.2;
    float t = uTime * 0.15;
    vec2 q = vec2(fbm(p + t), fbm(p + vec2(5.2, 1.3) - t));
    vec2 r = vec2(fbm(p + 4.0 * q + vec2(1.7, 9.2) + t * 1.2), fbm(p + 4.0 * q + vec2(8.3, 2.8) - t));
    float f = fbm(p + 4.0 * r + uAudio.x * 0.5);
    vec3 col = pal(f * 1.2 + r.x * 0.5 + uBeat * 0.01);
    col *= 0.25 + 1.2 * f * f + 0.5 * length(q);
    col += duo(0.9) * pow(f, 6.0) * 0.8;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Cube Tunnel 3D", R"glsl(
float mapScene(vec3 p) {
    p.xy = rot(p.z * 0.12) * p.xy;
    float z = mod(p.z + 1.0, 2.0) - 1.0;
    float n = 8.0;
    float sector = 6.28318 / n;
    float a = atan(p.y, p.x);
    float aa = mod(a + sector * 0.5, sector) - sector * 0.5;
    float r = length(p.xy);
    vec2 q = vec2(r * cos(aa) - 1.75, r * sin(aa));
    float grow = 0.16 + uAudio.x * 0.1;
    return sdBox(vec3(q, z), vec3(grow, 0.5, 0.7));
}

void main() {
    vec2 p = centered();
    vec3 ro = vec3(0.0, 0.0, uTime * 1.6 + uBeat * 0.3);
    vec3 rd = normalize(vec3(p, 1.2));
    rd.xy = rot(sin(uTime * 0.3) * 0.5) * rd.xy;
    float d = 0.0;
    float glow = 0.0;
    bool hit = false;
    vec3 pos = ro;
    for (int i = 0; i < 72; ++i) {
        pos = ro + rd * d;
        float m = mapScene(pos);
        glow += exp(-m * 4.0) * 0.02;
        if (m < 0.003) {
            hit = true;
            break;
        }
        d += m;
        if (d > 40.0) break;
    }
    vec3 col = duo(0.0) * 0.02 + duo(0.8) * glow * (0.4 + uAudio.x);
    if (hit) {
        vec2 e = vec2(0.004, 0.0);
        vec3 n = normalize(vec3(
            mapScene(pos + e.xyy) - mapScene(pos - e.xyy),
            mapScene(pos + e.yxy) - mapScene(pos - e.yxy),
            mapScene(pos + e.yyx) - mapScene(pos - e.yyx)));
        float fog = exp(-d * 0.07);
        float diff = 0.25 + 0.75 * max(dot(n, -rd), 0.0);
        col = pal(pos.z * 0.08 + atan(pos.y, pos.x) * 0.08) * diff * fog + duo(0.8) * glow;
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Gyroid 3D", R"glsl(
float mapScene(vec3 p) {
    float sc = 4.2;
    vec3 q = p * sc;
    float g = dot(sin(q), cos(q.yzx));
    float thick = 0.02 + uAudio.x * 0.025;
    return abs(g) / sc * 0.6 - thick;
}

void main() {
    vec2 p = centered();
    vec3 ro = vec3(sin(uTime * 0.2) * 0.5 + 0.4, cos(uTime * 0.17) * 0.5 + 0.3, uTime * 1.0 + uBeat * 0.15);
    vec3 rd = normalize(vec3(p, 1.1));
    rd.xy = rot(uTime * 0.1) * rd.xy;
    float side = mapScene(ro) < 0.0 ? -1.0 : 1.0;
    float d = 0.0;
    float glow = 0.0;
    float steps = 0.0;
    bool hit = false;
    vec3 pos = ro;
    for (int i = 0; i < 100; ++i) {
        pos = ro + rd * d;
        float m = mapScene(pos) * side;
        glow += exp(-abs(m) * 14.0) * 0.004;
        steps += 1.0;
        if (m < 0.002) {
            hit = true;
            break;
        }
        d += m * 0.9;
        if (d > 25.0) break;
    }
    float fog = exp(-d * 0.32);
    vec3 col = duo(0.0) * 0.02 + pal(d * 0.05 + uTime * 0.05) * glow * 0.8 * fog;
    if (hit) {
        vec2 e = vec2(0.002, 0.0);
        vec3 n = side * normalize(vec3(
            mapScene(pos + e.xyy) - mapScene(pos - e.xyy),
            mapScene(pos + e.yxy) - mapScene(pos - e.yxy),
            mapScene(pos + e.yyx) - mapScene(pos - e.yyx)));
        float facing = max(dot(n, -rd), 0.0);
        float diff = 0.1 + 0.9 * facing * facing;
        float rim = pow(1.0 - facing, 3.0);
        float ao = 1.0 - steps / 100.0;
        float line = ss(0.9, 1.0, abs(sin(pos.z * 9.0 + pos.x * 4.0 + uTime)));
        col = pal(pos.z * 0.2 + pos.x * 0.15) * diff * ao * fog;
        col += duo(0.9) * (rim * 0.6 + line * 0.5 * facing) * fog;
        col += pal(d * 0.05) * glow * 0.6 * fog;
    }
    col *= 1.0 + uKick * 0.35;
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Terrain 3D", R"glsl(
float heightAt(vec2 q) {
    float h = 0.9 * sin(q.x * 0.45 + 0.6 * sin(q.y * 0.3)) * cos(q.y * 0.4);
    h += 0.4 * sin(q.x * 1.3 + q.y * 0.9) + 0.2 * sin(q.x * 2.7 - q.y * 1.9);
    h = abs(h) * (0.8 + uAudio.x * 0.8);
    return h * ss(0.0, 3.0, abs(q.x));
}

void main() {
    vec2 p = centered();
    vec3 ro = vec3(0.0, 1.5, uTime * 1.4 + uBeat * 0.2);
    vec3 rd = normalize(vec3(p.x, p.y - 0.25, 1.4));
    float d = 0.1;
    bool hit = false;
    vec3 pos = ro;
    for (int i = 0; i < 90; ++i) {
        pos = ro + rd * d;
        float h = pos.y - heightAt(pos.xz);
        if (h < 0.003 * d) {
            hit = true;
            break;
        }
        d += h * 0.55;
        if (d > 60.0) break;
    }
    float sky = clamp(rd.y * 1.6 + 0.35, 0.0, 1.0);
    vec3 col = mix(duo(0.8) * 0.55, duo(0.0) * 0.1, sky);
    float sun = ss(0.2 + 0.03 * uKick, 0.18, length(vec2(p.x, p.y - 0.35)));
    col += duo(0.2) * sun;
    if (hit) {
        vec2 g = abs(fract(pos.xz * 1.5) - 0.5);
        float line = ss(0.46, 0.5, max(g.x, g.y));
        float hh = heightAt(pos.xz);
        float fog = exp(-d * 0.045);
        vec3 ground = pal(hh * 0.4 + 0.1) * (0.12 + 0.5 * ss(0.0, 1.8, hh));
        vec3 surf = ground + duo(0.9) * line * 0.9;
        col = mix(duo(0.8) * 0.3, surf, fog);
    }
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Sunburst", R"glsl(
void main() {
    vec2 p = centered();
    float a = atan(p.y, p.x);
    float r = length(p);
    float n = 24.0;
    float spin = floor(uBeat) * 0.1309 + fract(uBeat) * 0.1309 * ss(0.0, 0.3, fract(uBeat));
    float rays = 0.5 + 0.5 * sin((a + spin) * n + sin(r * 3.0 - uTime) * 0.8);
    rays = ss(0.4 + 0.1 * (1.0 - uKick), 0.6, rays);
    vec3 col = mix(duo(0.0) * 0.1, duo(1.0), rays) * (0.35 + 0.65 * (1.0 - ss(0.0, 2.2, r)));
    col += vec3(1.0) * ss(0.3 + 0.1 * uKick, 0.25, r);
    col += duo(0.5) * exp(-r * 3.0) * 0.3;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Digital Rain", R"glsl(
void main() {
    vec2 uv = vUv;
    float aspect = uRes.x / uRes.y;
    float cols = 56.0;
    vec2 cells = vec2(cols, cols / aspect);
    vec2 cell = floor(uv * cells);
    vec2 f = fract(uv * cells);
    float h = hash21(vec2(cell.x, 3.0));
    float speed = 0.3 + h * 0.9;
    float t = fract(uTime * speed * 0.25 + h * 10.0);
    float headY = 1.25 - t * 1.6;
    float dist = uv.y - headY;
    float trail = dist > 0.0 ? exp(-dist * 2.2) : 0.0;
    float head = ss(0.03, 0.0, abs(dist));
    float glyph = step(0.45, hash21(floor(f * vec2(3.0, 4.0)) + cell * 7.0 + floor(uTime * speed * 6.0)));
    float mask = ss(0.0, 0.12, f.x) * ss(1.0, 0.88, f.x) * ss(0.0, 0.1, f.y) * ss(1.0, 0.9, f.y);
    vec3 col = pal(h * 0.4 + 0.1) * trail * glyph * mask * (1.2 + 0.8 * uKick);
    col += vec3(1.0) * head * glyph * mask;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Milky Wave", R"glsl(
float sf(vec2 p) {
    return vnoise(p) * 0.62 + vnoise(p * 2.0 + 7.1) * 0.28 + vnoise(p * 4.0 + 3.3) * 0.1;
}

void main() {
    vec2 p = centered() * 0.5;
    float bass = uAudio.x;
    float t = uTime * (0.07 + bass * 0.05) + uBeat * 0.02;
    vec2 q = vec2(sf(p + vec2(0.0, t)), sf(p + vec2(5.2, 1.3) - t));
    vec2 r = vec2(sf(p + 1.9 * q + vec2(1.7, 9.2) + t * 0.6), sf(p + 1.9 * q + vec2(8.3, 2.8) - t * 0.5));
    float f = sf(p + 2.2 * r + uAudio.y * 0.15);
    float h = f * (16.0 + uAudio.y * 4.0) + uKick * 0.25 - t * 2.5;
    float w = max(fwidth(h), 0.0001);
    float d = abs(fract(h + 0.5) - 0.5) / w;
    float thick = 1.1 + uKick * 1.2 + bass * 0.9;
    float line = 1.0 - clamp(d - thick, 0.0, 1.0);
    vec3 lc = mix(duo(0.2 + f * 0.7), vec3(1.0), 0.1 + 0.4 * uKick);
    vec3 col = duo(0.0) * (0.05 + 0.3 * f * f);
    col += duo(0.4) * ss(0.4, 0.9, f) * 0.14;
    col += lc * line * (0.85 + 0.6 * uAudio.w);
    col += lc * exp(-d * 0.1) * 0.16 * (0.5 + bass);
    float vig = 1.0 - ss(0.8, 2.0, length(centered()));
    col *= 0.5 + 0.5 * vig;
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Crystal Cavern", std::string(kCrystalCommon) + R"glsl(
void main() {
    vec2 p = centered();
    float bass = uAudio.x;
    float travel = uTime * (0.22 + bass * 0.15) + uBeat * 0.05;
    float r;
    vec2 uv = tunnelUv(p, 6.0, 0.9, travel, r);
    vec4 s = shards(uv * vec2(1.2, 2.5), uTime * 0.25);
    float edge = s.x;
    float aaw = fwidth(edge);
    float w = 0.03 * (1.0 + uKick * 0.9 + bass * 0.5);
    float line = 1.0 - ss(w - aaw, w + aaw, edge);
    float hsh = hash21(s.yz);
    float inner = ss(0.0, 0.4, edge);
    vec3 facet = pal(0.55 + hsh * 0.25 + uv.y * 0.02) * (0.04 + 0.24 * hsh * hsh) * (0.6 + 0.4 * inner);
    vec3 edgeC = mix(pal(0.1 + hsh * 0.15), vec3(1.0), 0.35);
    vec3 col = facet + edgeC * line * (1.1 + 1.3 * uKick) + edgeC * exp(-edge * 14.0) * 0.18;
    float flash = step(0.9, hash21(s.yz + floor(uBeat))) * exp(-fract(uBeat) * 3.0);
    col += facet * 7.0 * flash;
    col *= ss(0.02, 0.55, r);
    col += duo(0.5) * exp(-r * 5.0) * (0.15 + 0.5 * uKick);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Crystal Lines", std::string(kCrystalCommon) + R"glsl(
void main() {
    vec2 p = centered();
    float bass = uAudio.x;
    float travel = uTime * (0.2 + bass * 0.12) + uBeat * 0.04;
    float r;
    vec2 uv = tunnelUv(p, 6.0, 1.0, travel, r);
    vec3 col = vec3(0.0);
    for (int k = 0; k < 2; ++k) {
        float fk = float(k);
        vec4 s = shards(uv * vec2(1.0, 2.2) * (1.5 - 0.7 * fk) + fk * 7.3, uTime * 0.2 + fk);
        float edge = s.x;
        float aaw = fwidth(edge);
        float w = mix(0.022, 0.04, fk) * (1.0 + uKick * 0.8 + bass * 0.5);
        float line = 1.0 - ss(w - aaw, w + aaw, edge);
        float front = exp(-abs(fract(uv.y * 0.12 - uBeat * 0.25 + fk * 0.37) - 0.5) * 9.0);
        vec3 c = mix(vec3(1.0), duo(0.5 + 0.5 * sin(uv.y * 0.3 + uTime * 0.2)), 0.25);
        col += c * line * mix(1.0, 0.55, fk) * (0.45 + 1.4 * front + uKick * 0.6);
        col += c * exp(-edge * mix(22.0, 14.0, fk)) * 0.1;
    }
    col *= ss(0.0, 0.5, r);
    col += vec3(0.9, 0.95, 1.0) * exp(-r * 6.0) * 0.25 * (0.5 + uKick);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Neon Sigil", R"glsl(
float segd(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    vec2 p = centered() * 0.98;
    float t = uTime * 0.11 + uBeat * 0.012;
    float bass = uAudio.x;
    float n = 6.0;
    float seg = 2.0 * PI / n;
    float a = atan(p.y, p.x) + t * 0.5;
    float r = length(p);
    a = abs(mod(a, seg) - seg * 0.5);
    vec2 q = vec2(cos(a), sin(a)) * r;
    vec3 col = vec3(0.0);
    float sc = 1.0;
    for (int i = 0; i < 7; ++i) {
        float fi = float(i);
        q = abs(q);
        q -= vec2(0.24 + 0.07 * sin(t * 2.0 + fi), 0.11 + 0.05 * cos(t * 1.7 + fi * 1.4));
        q = rot(0.6 + 0.4 * sin(t + fi * 0.9)) * q;
        q *= 1.3;
        sc *= 1.3;
        float d1 = segd(q, vec2(0.0), vec2(0.6, 0.0)) / sc;
        float d2 = segd(q, vec2(0.6, 0.0), vec2(0.4, 0.42)) / sc;
        float d3 = abs(length(q - vec2(0.3, 0.12)) - 0.2) / sc;
        float d = min(min(d1, d2), d3);
        float w = 0.0022 * (1.0 + 1.6 * uKick + bass);
        float core = 1.0 - ss(w, w * 2.4, d);
        float halo = 0.0018 / (d + 0.0018);
        vec3 c = duo(fi / 6.0 + 0.1 * sin(uTime * 0.3));
        col += c * (core * 1.6 + halo * 0.3) * (1.0 - fi * 0.11);
    }
    col *= ss(2.2, 0.3, r);
    col += duo(0.5) * exp(-r * 3.0) * 0.14 * (0.5 + uKick);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Neon Lattice", R"glsl(
float polyd(vec2 p, float n, float r) {
    float a = atan(p.y, p.x) + PI;
    float s = 2.0 * PI / n;
    return cos(floor(0.5 + a / s) * s - a) * length(p) - r;
}

void main() {
    vec2 p = centered() * 1.05;
    float t = uTime * 0.2 + uBeat * 0.03;
    float pulse = 1.0 + 0.05 * exp(-fract(uBeat) * 4.0) + uAudio.x * 0.05;
    p = abs(p);
    if (p.y > p.x) {
        p = p.yx;
    }
    vec3 col = vec3(0.0);
    for (int k = 0; k < 11; ++k) {
        float fk = float(k);
        float sides = 3.0 + mod(fk * 2.0, 5.0);
        float dir = mod(fk, 2.0) < 0.5 ? 1.0 : -1.0;
        float radius = (0.1 + fk * 0.105) * pulse;
        vec2 q = rot(t * dir * (0.4 + 0.05 * fk) + fk * 0.5) * p;
        float d = abs(polyd(q, sides, radius));
        float w = 0.0035 * (1.0 + uKick);
        float line = 1.0 - ss(w, w * 2.2, d);
        float halo = 0.003 / (d + 0.003);
        vec3 c = duo(mod(fk, 2.0));
        col += c * (line * 1.2 + halo * 0.28);
    }
    float spokes = abs(fract(atan(p.y, p.x) * 4.0 / PI + t * 0.1) - 0.5);
    col += duo(1.0) * (1.0 - ss(0.0, 0.012, spokes)) * 0.25 * ss(1.4, 0.2, length(p));
    col *= ss(1.9, 0.3, length(p));
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Neon Corridor", R"glsl(
void main() {
    vec2 p = centered() * 0.6;
    float speed = uTime * 0.9 + uBeat * 0.1;
    float m = max(abs(p.x) * 0.75, abs(p.y));
    float depth = 0.55 / max(m, 0.001);
    float z = depth + speed;
    bool side = abs(p.x) * 0.75 > abs(p.y);
    float u = side ? p.y / max(abs(p.x) * 0.75, 0.0001) : p.x * 0.75 / max(abs(p.y), 0.0001);
    float chevron = abs(fract(z * 0.8 + abs(u) * 0.9) - 0.5);
    float rib = 1.0 - ss(0.0, 0.07 + 0.018 / depth, chevron);
    float lane = abs(fract(u * 2.0 + 0.5) - 0.5);
    float rail = 1.0 - ss(0.0, 0.025, lane);
    float cross = ss(0.45, 0.5, abs(fract(z) - 0.5));
    vec3 cA = duo(0.0);
    vec3 cB = duo(1.0);
    vec3 col = cA * rib * (side ? 0.8 : 1.2) + cB * rail * 0.55 + mix(cA, cB, 0.5) * cross * 0.3;
    col *= exp(-depth * 0.05) * ss(0.0, 0.3, m) * (0.9 + uKick * 0.8);
    col += cB * exp(-m * 14.0) * 0.12;
    col += vec3(1.0) * exp(-length(p) * 12.0) * 0.45 * (0.4 + uAudio.x);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Chaos Smoke", R"glsl(
void main() {
    vec2 p = centered() * 0.55;
    float t = uTime * 0.06 + uBeat * 0.004;
    p = rot(t * 0.7) * p;
    vec2 q = vec2(fbm(p + t), fbm(p + vec2(5.2, 1.3) - t));
    vec2 r = vec2(fbm(p + 3.6 * q + vec2(1.7, 9.2) + t * 1.6), fbm(p + 3.6 * q + vec2(8.3, 2.8) - t * 1.3));
    float f = fbm(p * 1.1 + 4.2 * r + uAudio.x * 0.3);
    float ridge = 1.0 - abs(2.0 * f - 1.0);
    ridge = pow(ridge, 2.4);
    vec3 n = normalize(vec3(-dFdx(f) * 36.0, -dFdy(f) * 36.0, 1.0));
    vec3 light = normalize(vec3(0.5, 0.65, 0.6));
    float diff = max(dot(n, light), 0.0);
    float spec = pow(max(dot(reflect(-light, n), vec3(0.0, 0.0, 1.0)), 0.0), 18.0);
    float shade = clamp(diff * 0.75 + ridge * 0.9 + spec * 0.8, 0.0, 1.5);
    shade *= 0.55 + 0.45 * ss(0.15, 0.85, f);
    vec3 tint = mix(vec3(1.0), duo(0.5 + 0.5 * sin(f * 4.0 + uTime * 0.15)), 0.14);
    vec3 col = vec3(pow(shade, 1.35)) * tint * (1.0 + 0.5 * uKick);
    col *= 1.0 - 0.5 * ss(0.4, 1.7, length(p));
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Tesseract", R"glsl(
float segd(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.000001), 0.0, 1.0);
    return length(pa - ba * h);
}

vec2 proj(vec4 v, float t, float scale) {
    float c1 = cos(t * 0.7);
    float s1 = sin(t * 0.7);
    float c2 = cos(t * 0.43);
    float s2 = sin(t * 0.43);
    float c3 = cos(t * 0.31);
    float s3 = sin(t * 0.31);
    v.xw = mat2(c1, -s1, s1, c1) * v.xw;
    v.yz = mat2(c2, -s2, s2, c2) * v.yz;
    v.zw = mat2(c3, -s3, s3, c3) * v.zw;
    float k = 1.0 / (2.6 - v.w);
    vec3 w3 = v.xyz * k;
    float k2 = 1.0 / (3.2 - w3.z);
    return w3.xy * k2 * scale;
}

void main() {
    vec2 p = centered();
    float t = uTime * 0.5 + uBeat * 0.06;
    float scale = 2.5 * (1.0 + 0.06 * exp(-fract(uBeat) * 4.0) + uAudio.x * 0.08);
    vec3 col = vec3(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        float fl = float(layer);
        float s = scale * (1.0 - fl * 0.42);
        float tt = t + fl * 1.7;
        for (int i = 0; i < 16; ++i) {
            vec4 a = vec4((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0, (i & 4) == 0 ? -1.0 : 1.0, (i & 8) == 0 ? -1.0 : 1.0);
            vec2 pa = proj(a, tt, s);
            for (int k = 0; k < 4; ++k) {
                int j = i ^ (1 << k);
                if (j < i) {
                    continue;
                }
                vec4 b = vec4((j & 1) == 0 ? -1.0 : 1.0, (j & 2) == 0 ? -1.0 : 1.0, (j & 4) == 0 ? -1.0 : 1.0, (j & 8) == 0 ? -1.0 : 1.0);
                vec2 pb = proj(b, tt, s);
                float d = segd(p, pa, pb);
                float w = 0.005 * (1.0 + uKick);
                float core = 1.0 - ss(w, w * 2.0, d);
                float halo = 0.004 / (d + 0.004);
                vec3 c = duo(float(k) / 3.0 * 0.9 + fl * 0.5);
                col += c * (core * 1.4 + halo * 0.22) * (1.0 - fl * 0.35);
            }
        }
    }
    col *= ss(2.8, 0.2, length(p));
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Laser Web", R"glsl(
float segd(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.000001), 0.0, 1.0);
    return length(pa - ba * h);
}

vec2 node(int i, float t) {
    float fi = float(i);
    float a = (0.5 + 0.5 * sin(fi * 2.399963 + t * (0.35 + 0.08 * sin(fi)))) * 0.7853981;
    float r = 0.22 + 0.7 * fract(fi * 0.381966 + 0.08 * sin(t * 0.5 + fi)) + 0.1 * sin(t * 0.6 + fi * 1.7);
    return vec2(cos(a), sin(a)) * r;
}

void main() {
    vec2 p = centered() * 0.78;
    float t = uTime * 0.35 + uBeat * 0.04;
    float a = atan(p.y, p.x);
    float r = length(p);
    float seg = PI / 4.0;
    a = abs(mod(a, seg * 2.0) - seg) ;
    p = vec2(cos(a), sin(a)) * r;
    vec3 col = vec3(0.0);
    vec2 pts[11];
    for (int i = 0; i < 11; ++i) {
        pts[i] = node(i, t);
    }
    for (int i = 0; i < 11; ++i) {
        float dn = length(p - pts[i]);
        col += duo(float(i % 2)) * (0.006 / (dn + 0.006)) * 0.9;
        for (int j = i + 1; j < 11; ++j) {
            float dist = length(pts[i] - pts[j]);
            float reach = 0.7 + 0.25 * uKick + 0.2 * uAudio.y;
            if (dist < reach) {
                float d = segd(p, pts[i], pts[j]);
                float fade = 1.0 - dist / reach;
                float line = 1.0 - ss(0.0016, 0.0045, d);
                float halo = 0.0016 / (d + 0.0016);
                col += mix(duo(0.0), duo(1.0), fade) * (line * 1.1 + halo * 0.3) * (0.3 + fade * 1.3);
            }
        }
    }
    col *= ss(2.0, 0.2, r);
    fragColor = vec4(col, 1.0);
}
)glsl", false});

    clips.push_back({"Mandelbox Fly", R"glsl(
float de(vec3 p, out float trap) {
    vec3 z = p;
    float dr = 1.0;
    trap = 1e9;
    for (int i = 0; i < 7; ++i) {
        z = clamp(z, -1.0, 1.0) * 2.0 - z;
        float r2 = dot(z, z);
        if (r2 < 0.25) {
            z *= 4.0;
            dr *= 4.0;
        } else if (r2 < 1.0) {
            float k = 1.0 / r2;
            z *= k;
            dr *= k;
        }
        z = z * 2.0 + p;
        dr = dr * 2.0 + 1.0;
        trap = min(trap, dot(z, z));
    }
    return length(z) / abs(dr);
}

float deOnly(vec3 p) {
    float tr;
    return de(p, tr);
}

void main() {
    vec2 p = centered() * 0.42;
    float t = uTime * 0.1 + uBeat * 0.008;
    vec3 ro = vec3(18.0 * cos(t), 6.0 * sin(t * 0.7) + 2.0, 18.0 * sin(t));
    vec3 fwd = normalize(-ro);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));
    vec3 up = cross(fwd, right);
    vec3 rd = normalize(fwd + p.x * right + p.y * up);
    float dist = 0.0;
    float trap = 0.0;
    float steps = 0.0;
    bool hit = false;
    for (int i = 0; i < 80; ++i) {
        float tr;
        float d = de(ro + rd * dist, tr);
        trap = tr;
        steps += 1.0;
        if (d < 0.0006 * dist) {
            hit = true;
            break;
        }
        if (dist > 60.0) {
            break;
        }
        dist += d;
    }
    vec3 col = vec3(0.0);
    float glow = steps / 80.0;
    if (hit) {
        vec3 pos = ro + rd * dist;
        float e = 0.002 * dist;
        vec3 n = normalize(vec3(deOnly(pos + vec3(e, 0.0, 0.0)) - deOnly(pos - vec3(e, 0.0, 0.0)), deOnly(pos + vec3(0.0, e, 0.0)) - deOnly(pos - vec3(0.0, e, 0.0)), deOnly(pos + vec3(0.0, 0.0, e)) - deOnly(pos - vec3(0.0, 0.0, e))));
        vec3 l = normalize(vec3(0.6, 0.8, -0.4));
        float diff = max(dot(n, l), 0.0);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 2.5);
        float ao = 1.0 - glow * 1.1;
        vec3 base = mix(duo(0.0), duo(1.0), clamp(log(trap + 1.0) * 0.35, 0.0, 1.0));
        col = base * (0.15 + diff * 0.9) * clamp(ao, 0.0, 1.0) + duo(1.0) * rim * 0.7 * clamp(ao + 0.3, 0.0, 1.0);
        col *= exp(-max(dist - 12.0, 0.0) * 0.03);
    }
    col += duo(1.0) * pow(glow, 2.5) * 0.9;
    col += duo(0.0) * 0.015;
    col *= 1.0 + 0.5 * uKick;
    fragColor = vec4(col, 1.0);
}
)glsl", true});

    clips.push_back({"Prism Bloom", R"glsl(
void main() {
    vec2 p = centered() * 1.1;
    float t = uTime * 0.18 + uBeat * 0.02;
    float r = length(p);
    float a = atan(p.y, p.x);
    vec3 col = vec3(0.0);
    for (int k = 0; k < 9; ++k) {
        float fk = float(k);
        float petals = 6.0 + floor(fk * 0.5) * 2.0;
        float dir = mod(fk, 2.0) < 0.5 ? 1.0 : -1.0;
        float phase = a * petals + t * dir * (1.0 + 0.2 * fk);
        float base = 0.1 + fk * 0.1;
        float edge = base * (1.0 + 0.55 * cos(phase) * (0.6 + 0.4 * sin(t + fk)));
        edge *= 1.0 + 0.05 * exp(-fract(uBeat) * 4.0) + uAudio.x * 0.04;
        float d = abs(r - edge);
        float w = 0.0045 * (1.0 + uKick);
        float line = 1.0 - ss(w, w * 2.5, d);
        float halo = 0.0036 / (d + 0.0036);
        float fill = ss(0.0, 0.5, 0.5 + 0.5 * cos(phase)) * ss(edge + 0.02, edge - 0.18, r) * 0.1;
        vec3 c = duo(fk / 8.0);
        col += c * (line * 1.7 + halo * 0.3 + fill);
    }
    col += vec3(1.0, 0.95, 0.9) * exp(-r * 7.0) * (0.45 + uKick * 0.6);
    col *= ss(2.4, 0.2, r);
    fragColor = vec4(col, 1.0);
}
)glsl", false});
}

}

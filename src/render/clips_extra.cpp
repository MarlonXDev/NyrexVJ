#include "render/clip_library_extra.hpp"

namespace nvj {

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
}

}

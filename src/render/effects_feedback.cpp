#include "render/effect_library_extra.hpp"

namespace nvj {

void appendFeedbackEffects(std::vector<EffectDef>& fx) {
    fx.push_back({"Time Echo", {"Half-life", "Distance", "Angle", "Softness"}, {0.45f, 0.35f, 0.0f, 0.3f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 cur = texture(uTex, uv);
    float life = mix(0.04, 3.0, p.x * p.x);
    float a = p.z * 6.28318;
    vec2 shift = vec2(cos(a), sin(a)) * mix(0.0, 0.1, p.y) / vec2(uAspect, 1.0);
    vec2 q = uv - shift;
    float r = p.w * 0.012;
    vec2 rx = vec2(r / uAspect, 0.0);
    vec2 ry = vec2(0.0, r);
    vec3 prev = fbFetch(q) * 0.4 + (fbFetch(q + rx) + fbFetch(q - rx) + fbFetch(q + ry) + fbFetch(q - ry)) * 0.15;
    prev *= fbKeep(life);
    return vec4(fbRound(max(cur.rgb, prev)), cur.a);
}
)glsl", false, true});

    fx.push_back({"Motion Smear", {"Smear", "Slide", "Angle", "Detail"}, {0.4f, 0.2f, 0.25f, 0.4f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 cur = texture(uTex, uv);
    float life = mix(0.01, 1.2, p.x * p.x);
    float a = p.z * 6.28318;
    vec2 shift = vec2(cos(a), sin(a)) * mix(0.0, 0.03, p.y) * uDelta * 60.0 / vec2(uAspect, 1.0);
    vec3 prev = fbFetch(clamp(uv - shift, 0.0, 1.0));
    float k = fbKeep(life);
    vec3 acc = mix(cur.rgb, prev, k);
    acc = acc + (cur.rgb - acc) * p.w * 0.5;
    return vec4(fbRound(acc), cur.a);
}
)glsl", false, true});

    fx.push_back({"Feedback Transform", {"Half-life", "Zoom", "Rotate", "Drift"}, {0.6f, 0.55f, 0.5f, 0.5f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 cur = texture(uTex, uv);
    float rate = uDelta * 60.0;
    float life = mix(0.05, 4.0, p.x * p.x);
    vec2 c = (uv - 0.5) * vec2(uAspect, 1.0);
    float z = 1.0 - (p.y - 0.5) * 0.12 * rate - uKick * 0.012 * rate;
    float ang = (p.z - 0.5) * 0.16 * rate;
    float heading = uTime * 0.35;
    vec2 drift = vec2(cos(heading), sin(heading)) * (p.w - 0.5) * 0.02 * rate;
    c = rot(ang) * (c * z) + drift;
    vec3 prev = fbFetch(c / vec2(uAspect, 1.0) + 0.5) * fbKeep(life);
    vec3 outc = max(cur.rgb, prev);
    return vec4(fbRound(outc), cur.a);
}
)glsl", false, true});

    fx.push_back({"Accumulate", {"Exposure", "Mode", "Gain", "Kick Flush"}, {0.4f, 0.0f, 0.3f, 0.0f}, R"glsl(
vec4 effect(vec2 uv, vec4 p) {
    vec4 cur = texture(uTex, uv);
    float win = mix(0.1, 10.0, p.x * p.x);
    float k = fbKeep(win);
    float mode = floor(p.y * 2.99);
    vec3 src = cur.rgb * mix(1.0, 3.0, p.z);
    vec3 prev = fbFetch(uv) * (1.0 - clamp(uKick * p.w, 0.0, 1.0));
    vec3 acc;
    if (mode < 0.5) {
        acc = mix(src, prev, k);
    } else if (mode < 1.5) {
        acc = max(src, prev * k);
    } else {
        vec3 held = prev * k;
        acc = held + src - held * src;
    }
    return vec4(fbRound(acc), cur.a);
}
)glsl", false, true});
}

}

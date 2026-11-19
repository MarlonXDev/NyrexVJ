void main() {
    vec2 p = centered();
    float r = length(p);
    float bass = uAudio.x;
    float ring = sin(r * 10.0 - uTime * 2.0 - uBeat * 0.5) * 0.5 + 0.5;
    float glow = 0.04 / (abs(r - 0.6 - bass * 0.25) + 0.002);
    vec3 col = pal(r * 0.4 + uBeat * 0.05) * ring * (0.3 + bass);
    col += duo(0.7) * glow * (0.4 + uKick);
    fragColor = vec4(col, 1.0);
}

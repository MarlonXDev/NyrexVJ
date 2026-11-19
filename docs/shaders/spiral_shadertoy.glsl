void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float t = iTime * 0.5;
    float a = atan(uv.y, uv.x);
    float r = length(uv);
    float spokes = 0.5 + 0.5 * sin(a * 8.0 + t * 3.0 + r * 12.0);
    vec3 col = 0.5 + 0.5 * cos(6.2831853 * (r + t * 0.1 + vec3(0.0, 0.33, 0.67)));
    float fft = texture(iChannel0, vec2(r, 0.25)).x;
    fragColor = vec4(col * spokes * (0.6 + fft), 1.0);
}

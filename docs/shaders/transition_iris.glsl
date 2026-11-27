#pragma nyrexvj transition
#pragma nyrexvj param softness float "Softness" 0 0.5 0.12
#pragma nyrexvj param centerx float "Center X" 0 1 0.5
#pragma nyrexvj param centery float "Center Y" 0 1 0.5
void main() {
    vec2 p = vUv - vec2(centerx, centery);
    p.x *= uRes.x / uRes.y;
    float radius = uProgress * 1.2;
    float mask = 1.0 - ss(radius - softness, radius + softness, length(p));
    fragColor = mix(colFrom(vUv), colTo(vUv), mask);
}

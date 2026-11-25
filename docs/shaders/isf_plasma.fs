/*{
    "DESCRIPTION": "A moving plasma with a pattern switch, a tint and an invert switch. Example generator for the ISF import of NyrexVJ.",
    "CREDIT": "NyrexVJ example, MIT licence",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Generator"],
    "INPUTS": [
        {"NAME": "speed", "LABEL": "Speed", "TYPE": "float", "MIN": 0.0, "MAX": 4.0, "DEFAULT": 1.0},
        {"NAME": "density", "LABEL": "Density", "TYPE": "float", "MIN": 1.0, "MAX": 12.0, "DEFAULT": 4.0},
        {"NAME": "mode", "LABEL": "Pattern", "TYPE": "long", "VALUES": [0, 1, 2], "LABELS": ["Waves", "Rings", "Checks"], "DEFAULT": 0},
        {"NAME": "tint", "LABEL": "Tint", "TYPE": "color", "DEFAULT": [1.0, 0.45, 0.1, 1.0]},
        {"NAME": "invert", "LABEL": "Invert", "TYPE": "bool", "DEFAULT": false}
    ]
}*/

void main() {
    vec2 p = (isf_FragNormCoord - 0.5) * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0) * density;
    float t = TIME * speed;
    float v;
    if (mode == 0) {
        v = sin(p.x + t) + sin(p.y * 1.3 - t * 0.7) + sin((p.x + p.y) * 0.8 + t * 0.5);
    } else if (mode == 1) {
        v = sin(length(p) * 2.0 - t * 2.0) * 2.0;
    } else {
        v = sin(floor(p.x) + t) * sin(floor(p.y) - t) * 2.0;
    }
    float c = 0.5 + 0.5 * sin(v);
    if (invert) {
        c = 1.0 - c;
    }
    gl_FragColor = vec4(tint.rgb * c, tint.a);
}

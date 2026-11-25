/*{
    "DESCRIPTION": "Mixes the colour channels and posterises the picture. Example of a single pass ISF image filter.",
    "CREDIT": "NyrexVJ example, MIT licence",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Color Effect"],
    "INPUTS": [
        {"NAME": "inputImage", "TYPE": "image"},
        {"NAME": "amount", "LABEL": "Amount", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.6},
        {"NAME": "shift", "LABEL": "Shift", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.33},
        {"NAME": "levels", "LABEL": "Levels", "TYPE": "long", "VALUES": [2, 4, 8, 16, 255], "LABELS": ["2", "4", "8", "16", "Off"], "DEFAULT": 255}
    ]
}*/

void main() {
    vec4 c = IMG_THIS_NORM_PIXEL(inputImage);
    vec3 shifted = vec3(c.g, c.b, c.r) * (1.0 - shift) + vec3(c.b, c.r, c.g) * shift;
    vec3 rgb = mix(c.rgb, shifted, amount);
    if (levels < 255) {
        rgb = floor(rgb * float(levels) + 0.5) / float(levels);
    }
    gl_FragColor = vec4(rgb, c.a);
}

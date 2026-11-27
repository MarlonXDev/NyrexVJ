/*{
    "DESCRIPTION": "Pushes the incoming picture over the outgoing one. Example of an ISF transition.",
    "CREDIT": "NyrexVJ example, MIT licence",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Transition"],
    "INPUTS": [
        {"NAME": "startImage", "TYPE": "image"},
        {"NAME": "endImage", "TYPE": "image"},
        {"NAME": "progress", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "direction", "LABEL": "Direction", "TYPE": "long", "VALUES": [0, 1, 2, 3], "LABELS": ["Left", "Right", "Up", "Down"], "DEFAULT": 0}
    ]
}*/

void main() {
    vec2 uv = isf_FragNormCoord;
    vec2 axis = vec2(1.0, 0.0);
    if (direction == 1) {
        axis = vec2(-1.0, 0.0);
    } else if (direction == 2) {
        axis = vec2(0.0, 1.0);
    } else if (direction == 3) {
        axis = vec2(0.0, -1.0);
    }
    vec2 shifted = uv + axis * progress;
    vec2 wrapped = fract(shifted);
    bool incoming = shifted.x >= 1.0 || shifted.y >= 1.0 || shifted.x < 0.0 || shifted.y < 0.0;
    gl_FragColor = incoming ? IMG_NORM_PIXEL(endImage, wrapped) : IMG_NORM_PIXEL(startImage, shifted);
}

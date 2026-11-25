/*{
    "DESCRIPTION": "Leaves a trail behind moving picture parts. Example of an ISF image filter with a persistent buffer.",
    "CREDIT": "NyrexVJ example, MIT licence",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Feedback"],
    "INPUTS": [
        {"NAME": "inputImage", "TYPE": "image"},
        {"NAME": "decay", "LABEL": "Decay", "TYPE": "float", "MIN": 0.0, "MAX": 0.99, "DEFAULT": 0.85},
        {"NAME": "mixIn", "LABEL": "New picture", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.5}
    ],
    "PASSES": [
        {"TARGET": "trail", "PERSISTENT": true, "FLOAT": true},
        {}
    ]
}*/

void main() {
    if (PASSINDEX == 0) {
        vec4 now = IMG_THIS_NORM_PIXEL(inputImage);
        vec4 old = IMG_THIS_NORM_PIXEL(trail);
        gl_FragColor = max(now * mixIn + old * (1.0 - mixIn), old * decay);
    } else {
        gl_FragColor = IMG_THIS_NORM_PIXEL(trail);
    }
}

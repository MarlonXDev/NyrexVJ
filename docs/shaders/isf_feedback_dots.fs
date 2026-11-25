/*{
    "DESCRIPTION": "Dots that leave a fading, slowly zooming trail. Example of an ISF generator with a persistent buffer at half size.",
    "CREDIT": "NyrexVJ example, MIT licence",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Generator", "Feedback"],
    "INPUTS": [
        {"NAME": "fade", "LABEL": "Fade", "TYPE": "float", "MIN": 0.8, "MAX": 0.995, "DEFAULT": 0.96},
        {"NAME": "zoom", "LABEL": "Zoom", "TYPE": "float", "MIN": 0.0, "MAX": 0.03, "DEFAULT": 0.008},
        {"NAME": "size", "LABEL": "Dot size", "TYPE": "float", "MIN": 0.01, "MAX": 0.2, "DEFAULT": 0.05},
        {"NAME": "colour", "LABEL": "Colour", "TYPE": "color", "DEFAULT": [0.2, 0.8, 1.0, 1.0]}
    ],
    "PASSES": [
        {"TARGET": "field", "PERSISTENT": true, "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2"},
        {}
    ]
}*/

void main() {
    if (PASSINDEX == 0) {
        vec2 uv = isf_FragNormCoord;
        vec2 centre = vec2(0.5);
        vec4 old = IMG_NORM_PIXEL(field, centre + (uv - centre) * (1.0 - zoom)) * fade;
        vec2 dotPos = vec2(0.5 + 0.35 * cos(TIME * 1.3), 0.5 + 0.35 * sin(TIME * 1.7));
        float spot = smoothstep(size, size * 0.4, distance(uv * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0), dotPos * vec2(RENDERSIZE.x / RENDERSIZE.y, 1.0)));
        gl_FragColor = old + vec4(colour.rgb * spot, 0.0);
    } else {
        gl_FragColor = vec4(IMG_THIS_NORM_PIXEL(field).rgb, 1.0);
    }
}

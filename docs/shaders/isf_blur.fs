/*{
    "DESCRIPTION": "A two pass blur: the first pass blurs sideways into a half size buffer, the second pass blurs up and down. Example of a multipass ISF image filter.",
    "CREDIT": "NyrexVJ example, MIT licence",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Blur"],
    "INPUTS": [
        {"NAME": "inputImage", "TYPE": "image"},
        {"NAME": "radius", "LABEL": "Radius", "TYPE": "float", "MIN": 0.0, "MAX": 8.0, "DEFAULT": 3.0}
    ],
    "PASSES": [
        {"TARGET": "sideways", "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2"},
        {}
    ]
}*/

void main() {
    vec2 px = 1.0 / RENDERSIZE;
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int i = -6; i <= 6; ++i) {
        float w = exp(-float(i * i) / 18.0);
        float d = float(i) * radius;
        if (PASSINDEX == 0) {
            sum += IMG_NORM_PIXEL(inputImage, isf_FragNormCoord + vec2(d * px.x, 0.0)) * w;
        } else {
            sum += IMG_NORM_PIXEL(sideways, isf_FragNormCoord + vec2(0.0, d * px.y)) * w;
        }
        total += w;
    }
    gl_FragColor = sum / total;
}

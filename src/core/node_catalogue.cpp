#include "core/node_graph.hpp"

#include <algorithm>

namespace nvj {

namespace {

PortSpec fin(const char* name, float value, float minimum = -16.0f, float maximum = 16.0f) {
    PortSpec port;
    port.name = name;
    port.type = PortType::Float;
    port.value = value;
    port.minimum = minimum;
    port.maximum = maximum;
    return port;
}

PortSpec gin(const char* name, float value, float minimum = -16.0f, float maximum = 16.0f) {
    PortSpec port = fin(name, value, minimum, maximum);
    port.generic = true;
    return port;
}

PortSpec vin(const char* name, PortType type, const char* expression) {
    PortSpec port;
    port.name = name;
    port.type = type;
    port.expression = expression;
    return port;
}

PortSpec out(const char* name, PortType type) {
    PortSpec port;
    port.name = name;
    port.type = type;
    return port;
}

PortSpec gout(const char* name) {
    PortSpec port;
    port.name = name;
    port.type = PortType::Float;
    port.generic = true;
    return port;
}

ConstSpec cst(const char* name, float value, float minimum, float maximum, bool integer = false, const char* choices = nullptr) {
    ConstSpec spec;
    spec.name = name;
    spec.value = value;
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.integer = integer;
    spec.choices = choices;
    return spec;
}

NodeSpec make(const char* id, const char* title, const char* category, std::vector<PortSpec> inputs, std::vector<PortSpec> outputs, std::vector<ConstSpec> consts, const char* code) {
    NodeSpec spec;
    spec.id = id;
    spec.title = title;
    spec.category = category;
    spec.inputs = std::move(inputs);
    spec.outputs = std::move(outputs);
    spec.consts = std::move(consts);
    spec.code = code;
    return spec;
}

const char* kUv = "uv";
const char* kCentered = "(uv - 0.5) * vec2(uAspect, 1.0)";

std::vector<NodeSpec> buildCatalogue() {
    std::vector<NodeSpec> n;
    const PortType F = PortType::Float;
    const PortType V2 = PortType::Vec2;
    const PortType V3 = PortType::Vec3;
    const PortType V4 = PortType::Vec4;

    {
        NodeSpec s = make("picture", "Picture", "Input", {}, {out("Colour", V4)}, {}, "{o0} = texture(uTex, uv);");
        s.reads = 1;
        n.push_back(s);
    }
    {
        NodeSpec s = make("previous", "Previous Picture", "Input", {}, {out("Colour", V4)}, {}, "{o0} = texture(uPrev, uv);");
        s.reads = 1;
        s.usesPrevious = true;
        n.push_back(s);
    }
    n.push_back(make("uv", "UV", "Input", {}, {out("UV", V2)}, {}, "{o0} = uv;"));
    n.push_back(make("centered", "Centered UV", "Input", {}, {out("UV", V2)}, {}, "{o0} = (uv - 0.5) * vec2(uAspect, 1.0);"));
    n.push_back(make("time", "Time", "Input", {}, {out("Seconds", F)}, {}, "{o0} = uTime;"));
    n.push_back(make("beat", "Beat", "Input", {}, {out("Beats", F)}, {}, "{o0} = uBeat;"));
    n.push_back(make("audio", "Audio", "Input", {}, {out("Bass", F), out("Mid", F), out("High", F), out("Level", F), out("Kick", F)}, {},
                     "{o0} = uAudio.x;\n{o1} = uAudio.y;\n{o2} = uAudio.z;\n{o3} = uAudio.w;\n{o4} = uKick;"));
    n.push_back(make("spectrum", "Spectrum", "Input", {fin("Position", 0.5f, 0.0f, 1.0f)}, {out("Value", F)}, {}, "{o0} = spec({i0});"));
    n.push_back(make("waveform", "Waveform", "Input", {fin("Position", 0.5f, 0.0f, 1.0f)}, {out("Value", F)}, {}, "{o0} = wave({i0});"));
    n.push_back(make("control", "Control", "Input", {}, {out("Value", F)}, {cst("Control", 1.0f, 1.0f, 4.0f, true)}, "{o0} = p[{c0i} - 1];"));
    {
        NodeSpec s = make("macro", "Macro", "Input", {}, {out("Value", F)}, {cst("Macro", 1.0f, 1.0f, 8.0f, true)}, "{o0} = uMacros[{c0i} - 1];");
        s.usesMacros = true;
        n.push_back(s);
    }
    {
        NodeSpec s = make("lfo", "LFO", "Input", {}, {out("Value", F)}, {cst("Shape", 0.0f, 0.0f, 3.0f, true, "Sine,Triangle,Saw,Square"), cst("Beats per cycle", 4.0f, 0.25f, 64.0f)}, "{o0} = nfxLfo(uBeat / {c1}, {c0i});");
        s.helpers = "nfxLfo";
        n.push_back(s);
    }
    n.push_back(make("number", "Number", "Input", {}, {out("Value", F)}, {cst("Value", 1.0f, -16.0f, 16.0f)}, "{o0} = {c0};"));
    n.push_back(make("colour", "Colour", "Input", {}, {out("Colour", V3)}, {cst("Red", 1.0f, 0.0f, 1.0f), cst("Green", 1.0f, 0.0f, 1.0f), cst("Blue", 1.0f, 0.0f, 1.0f)}, "{o0} = vec3({c0}, {c1}, {c2});"));
    n.push_back(make("vector2", "Vector 2", "Input", {}, {out("Vector", V2)}, {cst("X", 0.0f, -2.0f, 2.0f), cst("Y", 0.0f, -2.0f, 2.0f)}, "{o0} = vec2({c0}, {c1});"));

    n.push_back(make("add", "Add", "Maths", {gin("A", 0.0f), gin("B", 0.0f)}, {gout("Result")}, {}, "{o0} = {i0} + {i1};"));
    n.push_back(make("subtract", "Subtract", "Maths", {gin("A", 0.0f), gin("B", 0.0f)}, {gout("Result")}, {}, "{o0} = {i0} - {i1};"));
    n.push_back(make("multiply", "Multiply", "Maths", {gin("A", 1.0f), gin("B", 1.0f)}, {gout("Result")}, {}, "{o0} = {i0} * {i1};"));
    n.push_back(make("divide", "Divide", "Maths", {gin("A", 1.0f), gin("B", 1.0f)}, {gout("Result")}, {},
                     "{T} d{id} = {i1};\n{o0} = {i0} / (d{id} + {T}(0.00001) * (1.0 - step({T}(0.00001), abs(d{id}))));"));
    n.push_back(make("min", "Minimum", "Maths", {gin("A", 0.0f), gin("B", 1.0f)}, {gout("Result")}, {}, "{o0} = min({i0}, {i1});"));
    n.push_back(make("max", "Maximum", "Maths", {gin("A", 0.0f), gin("B", 1.0f)}, {gout("Result")}, {}, "{o0} = max({i0}, {i1});"));
    n.push_back(make("mix", "Mix", "Maths", {gin("A", 0.0f), gin("B", 1.0f), fin("Amount", 0.5f, 0.0f, 1.0f)}, {gout("Result")}, {}, "{o0} = mix({i0}, {i1}, {i2});"));
    n.push_back(make("clamp", "Clamp", "Maths", {gin("Value", 0.5f), fin("Lowest", 0.0f), fin("Highest", 1.0f)}, {gout("Result")}, {},
                     "{o0} = clamp({i0}, min({i1}, {i2}), max({i1}, {i2}));"));
    n.push_back(make("abs", "Absolute", "Maths", {gin("Value", 0.0f)}, {gout("Result")}, {}, "{o0} = abs({i0});"));
    n.push_back(make("negate", "Negate", "Maths", {gin("Value", 0.0f)}, {gout("Result")}, {}, "{o0} = -{i0};"));
    n.push_back(make("fract", "Fraction", "Maths", {gin("Value", 0.0f)}, {gout("Result")}, {}, "{o0} = fract({i0});"));
    n.push_back(make("floor", "Floor", "Maths", {gin("Value", 0.0f)}, {gout("Result")}, {}, "{o0} = floor({i0});"));
    n.push_back(make("sine", "Sine", "Maths", {gin("Radians", 0.0f)}, {gout("Result")}, {}, "{o0} = sin({i0});"));
    n.push_back(make("cosine", "Cosine", "Maths", {gin("Radians", 0.0f)}, {gout("Result")}, {}, "{o0} = cos({i0});"));
    n.push_back(make("power", "Power", "Maths", {gin("Base", 1.0f), gin("Exponent", 2.0f)}, {gout("Result")}, {}, "{o0} = pow(max({i0}, {T}(0.00001)), {i1});"));
    n.push_back(make("smoothstep", "Smooth Step", "Maths", {fin("From", 0.0f), fin("To", 1.0f), gin("Value", 0.5f)}, {gout("Result")}, {},
                     "{o0} = smoothstep({i0}, max({i1}, {i0} + 0.00001), {i2});"));
    n.push_back(make("step", "Step", "Maths", {fin("Edge", 0.5f), gin("Value", 0.0f)}, {gout("Result")}, {}, "{o0} = step({i0}, {i1});"));
    n.push_back(make("length", "Length", "Maths", {gin("Value", 0.0f)}, {out("Length", F)}, {}, "{o0} = length({i0});"));
    n.push_back(make("remap", "Map Range", "Maths", {gin("Value", 0.5f), fin("From low", 0.0f), fin("From high", 1.0f), fin("To low", 0.0f), fin("To high", 1.0f)}, {gout("Result")}, {},
                     "{o0} = {i3} + clamp(({i0} - {i1}) / (abs({i2} - {i1}) < 0.00001 ? 0.00001 : ({i2} - {i1})), {T}(0.0), {T}(1.0)) * ({i4} - {i3});"));

    n.push_back(make("makevec2", "Make Vector 2", "Vector", {fin("X", 0.0f), fin("Y", 0.0f)}, {out("Vector", V2)}, {}, "{o0} = vec2({i0}, {i1});"));
    n.push_back(make("makecolour", "Make Colour", "Vector", {fin("Red", 0.0f, 0.0f, 1.0f), fin("Green", 0.0f, 0.0f, 1.0f), fin("Blue", 0.0f, 0.0f, 1.0f)}, {out("Colour", V3)}, {}, "{o0} = vec3({i0}, {i1}, {i2});"));
    n.push_back(make("makecoloura", "Add Alpha", "Vector", {vin("Colour", V3, "vec3(0.0)"), fin("Alpha", 1.0f, 0.0f, 1.0f)}, {out("Colour", V4)}, {}, "{o0} = vec4({i0}, {i1});"));
    n.push_back(make("split", "Split Colour", "Vector", {vin("Colour", V4, "vec4(0.0, 0.0, 0.0, 1.0)")}, {out("Red", F), out("Green", F), out("Blue", F), out("Alpha", F)}, {},
                     "{o0} = {i0}.r;\n{o1} = {i0}.g;\n{o2} = {i0}.b;\n{o3} = {i0}.a;"));
    n.push_back(make("splitvec2", "Split Vector 2", "Vector", {vin("Vector", V2, "vec2(0.0)")}, {out("X", F), out("Y", F)}, {}, "{o0} = {i0}.x;\n{o1} = {i0}.y;"));

    {
        NodeSpec s = make("hue", "Hue Shift", "Colour", {vin("Colour", V3, "vec3(1.0)"), fin("Amount", 0.0f, -1.0f, 1.0f)}, {out("Colour", V3)}, {}, "{o0} = nfxHue({i0}, {i1});");
        s.helpers = "nfxHue";
        n.push_back(s);
    }
    n.push_back(make("saturation", "Saturation", "Colour", {vin("Colour", V3, "vec3(1.0)"), fin("Amount", 1.0f, 0.0f, 3.0f)}, {out("Colour", V3)}, {}, "{o0} = mix(vec3(luma({i0})), {i0}, {i1});"));
    n.push_back(make("contrast", "Brightness Contrast", "Colour", {vin("Colour", V3, "vec3(1.0)"), fin("Brightness", 0.0f, -1.0f, 1.0f), fin("Contrast", 1.0f, 0.0f, 3.0f)}, {out("Colour", V3)}, {},
                     "{o0} = max(({i0} - 0.5) * {i2} + 0.5 + {i1}, 0.0);"));
    n.push_back(make("invert", "Invert", "Colour", {vin("Colour", V3, "vec3(1.0)"), fin("Amount", 1.0f, 0.0f, 1.0f)}, {out("Colour", V3)}, {}, "{o0} = mix({i0}, 1.0 - {i0}, {i1});"));
    n.push_back(make("luma", "Luminance", "Colour", {vin("Colour", V3, "vec3(1.0)")}, {out("Luminance", F)}, {}, "{o0} = luma({i0});"));
    n.push_back(make("palette", "Rainbow Palette", "Colour", {fin("Position", 0.0f)}, {out("Colour", V3)}, {}, "{o0} = hc({i0});"));
    n.push_back(make("blend", "Blend", "Colour", {vin("Base", V3, "vec3(0.0)"), vin("Top", V3, "vec3(1.0)"), fin("Amount", 1.0f, 0.0f, 1.0f)}, {out("Colour", V3)},
                     {cst("Mode", 0.0f, 0.0f, 4.0f, true, "Mix,Add,Multiply,Screen,Difference")},
                     "vec3 b{id} = {i0};\nvec3 t{id} = {i1};\nvec3 r{id} = t{id};\nif ({c0i} == 1) r{id} = b{id} + t{id};\nelse if ({c0i} == 2) r{id} = b{id} * t{id};\nelse if ({c0i} == 3) r{id} = 1.0 - (1.0 - b{id}) * (1.0 - t{id});\nelse if ({c0i} == 4) r{id} = abs(b{id} - t{id});\n{o0} = mix(b{id}, r{id}, {i2});"));

    n.push_back(make("rotate", "Rotate", "Distort", {vin("UV", V2, kUv), fin("Angle", 0.0f, -2.0f, 2.0f)}, {out("UV", V2)}, {}, "{o0} = nfxRotate({i0}, {i1});"));
    n.back().helpers = "nfxRotate";
    n.push_back(make("zoom", "Zoom", "Distort", {vin("UV", V2, kUv), fin("Amount", 1.0f, 0.05f, 8.0f)}, {out("UV", V2)}, {}, "{o0} = ({i0} - 0.5) / max({i1}, 0.01) + 0.5;"));
    n.push_back(make("move", "Move", "Distort", {vin("UV", V2, kUv), fin("X", 0.0f, -2.0f, 2.0f), fin("Y", 0.0f, -2.0f, 2.0f)}, {out("UV", V2)}, {}, "{o0} = {i0} - vec2({i1}, {i2});"));
    n.push_back(make("mirror", "Mirror", "Distort", {vin("UV", V2, kUv)}, {out("UV", V2)}, {cst("Mirror left to right", 1.0f, 0.0f, 1.0f, true), cst("Mirror top to bottom", 0.0f, 0.0f, 1.0f, true)},
                     "{o0} = {i0};\nif ({c0i} != 0) {o0}.x = 0.5 - abs(0.5 - {o0}.x);\nif ({c1i} != 0) {o0}.y = 0.5 - abs(0.5 - {o0}.y);"));
    n.push_back(make("kaleidoscope", "Kaleidoscope", "Distort", {vin("UV", V2, kUv), fin("Segments", 6.0f, 2.0f, 32.0f), fin("Rotation", 0.0f, -2.0f, 2.0f)}, {out("UV", V2)}, {},
                     "vec2 c{id} = ({i0} - 0.5) * vec2(uAspect, 1.0);\nfloat n{id} = clamp(floor({i1} + 0.5), 2.0, 32.0);\nfloat seg{id} = PI / n{id};\nfloat a{id} = atan(c{id}.y, c{id}.x + 0.000001) + {i2} * 6.2831853;\na{id} = abs(mod(a{id}, seg{id} * 2.0) - seg{id});\n{o0} = vec2(cos(a{id}), sin(a{id})) * length(c{id}) / vec2(uAspect, 1.0) + 0.5;"));
    n.push_back(make("swirl", "Swirl", "Distort", {vin("UV", V2, kUv), fin("Amount", 0.5f, -4.0f, 4.0f), fin("Radius", 0.5f, 0.01f, 2.0f)}, {out("UV", V2)}, {},
                     "vec2 c{id} = ({i0} - 0.5) * vec2(uAspect, 1.0);\nfloat k{id} = (1.0 - smoothstep(0.0, max({i2}, 0.01), length(c{id}))) * {i1} * 6.2831853;\nc{id} = rot(k{id}) * c{id};\n{o0} = c{id} / vec2(uAspect, 1.0) + 0.5;"));
    n.push_back(make("wave", "Wave", "Distort", {vin("UV", V2, kUv), fin("Amount", 0.02f, -1.0f, 1.0f), fin("Frequency", 10.0f, 0.0f, 64.0f), fin("Speed", 1.0f, -8.0f, 8.0f)}, {out("UV", V2)}, {},
                     "{o0} = {i0} + vec2(sin({i0}.y * {i2} + uTime * {i3}), cos({i0}.x * {i2} + uTime * {i3})) * {i1};"));
    n.push_back(make("pixelate", "Pixelate", "Distort", {vin("UV", V2, kUv), fin("Cells", 48.0f, 1.0f, 512.0f)}, {out("UV", V2)}, {},
                     "float n{id} = max(floor({i1}), 1.0);\n{o0} = (floor({i0} * n{id}) + 0.5) / n{id};"));
    n.push_back(make("polar", "Polar", "Distort", {vin("UV", V2, kUv)}, {out("UV", V2)}, {},
                     "vec2 c{id} = ({i0} - 0.5) * vec2(uAspect, 1.0);\n{o0} = vec2(atan(c{id}.y, c{id}.x + 0.000001) / 6.2831853 + 0.5, length(c{id}));"));
    n.push_back(make("tile", "Tile", "Distort", {vin("UV", V2, kUv), fin("Count", 2.0f, 1.0f, 32.0f)}, {out("UV", V2)}, {}, "{o0} = mrep({i0} * max({i1}, 1.0));"));
    n.push_back(make("lens", "Lens", "Distort", {vin("UV", V2, kUv), fin("Strength", 0.3f, -2.0f, 2.0f)}, {out("UV", V2)}, {},
                     "vec2 c{id} = {i0} - 0.5;\n{o0} = c{id} * (1.0 + {i1} * dot(c{id}, c{id}) * 4.0) + 0.5;"));
    {
        NodeSpec s = make("sample", "Sample Picture", "Distort", {vin("UV", V2, kUv)}, {out("Colour", V4)}, {cst("Edges", 0.0f, 0.0f, 2.0f, true, "Clamp,Repeat,Mirror")},
                          "vec2 q{id} = {i0};\nif ({c0i} == 1) q{id} = fract(q{id});\nelse if ({c0i} == 2) q{id} = mrep(q{id});\nelse q{id} = clamp(q{id}, 0.0, 1.0);\n{o0} = texture(uTex, q{id});");
        s.reads = 1;
        n.push_back(s);
    }

    {
        NodeSpec s = make("noise", "Noise", "Noise", {vin("UV", V2, kUv), fin("Scale", 6.0f, 0.0f, 64.0f), fin("Speed", 0.2f, -4.0f, 4.0f)}, {out("Value", F)}, {}, "{o0} = vn({i0} * {i1} + vec2(uTime * {i2}, 0.0));");
        n.push_back(s);
    }
    {
        NodeSpec s = make("fbm", "Fractal Noise", "Noise", {vin("UV", V2, kUv), fin("Scale", 4.0f, 0.0f, 64.0f), fin("Speed", 0.1f, -4.0f, 4.0f)}, {out("Value", F)}, {cst("Octaves", 3.0f, 1.0f, 5.0f, true)},
                          "{o0} = nfxFbm({i0} * {i1} + vec2(uTime * {i2}, 0.0), {c0i});");
        s.helpers = "nfxFbm";
        n.push_back(s);
    }
    {
        NodeSpec s = make("cells", "Cells", "Noise", {vin("UV", V2, kUv), fin("Scale", 6.0f, 0.0f, 64.0f), fin("Speed", 0.1f, -4.0f, 4.0f)}, {out("Distance", F)}, {},
                          "{o0} = nfxCells({i0} * {i1} + vec2(uTime * {i2}, 0.0));");
        s.helpers = "nfxCells";
        n.push_back(s);
    }

    n.push_back(make("circle", "Circle", "Shape", {vin("Position", V2, kCentered), fin("Radius", 0.3f, 0.0f, 2.0f), fin("Softness", 0.02f, 0.0f, 1.0f)}, {out("Mask", F)}, {},
                     "{o0} = 1.0 - smoothstep({i1} - {i2}, {i1} + {i2} + 0.00001, length({i0}));"));
    n.push_back(make("box", "Box", "Shape", {vin("Position", V2, kCentered), fin("Width", 0.4f, 0.0f, 2.0f), fin("Height", 0.25f, 0.0f, 2.0f), fin("Softness", 0.02f, 0.0f, 1.0f)}, {out("Mask", F)}, {},
                     "vec2 d{id} = abs({i0}) - vec2({i1}, {i2});\nfloat s{id} = length(max(d{id}, 0.0)) + min(max(d{id}.x, d{id}.y), 0.0);\n{o0} = 1.0 - smoothstep(-{i3}, {i3} + 0.00001, s{id});"));
    n.push_back(make("rings", "Rings", "Shape", {vin("Position", V2, kCentered), fin("Frequency", 8.0f, 0.0f, 64.0f), fin("Speed", 0.3f, -8.0f, 8.0f)}, {out("Value", F)}, {},
                     "{o0} = 0.5 + 0.5 * sin(length({i0}) * {i1} * 6.2831853 - uTime * {i2} * 6.2831853);"));
    n.push_back(make("gradient", "Gradient", "Shape", {vin("UV", V2, kUv), fin("Angle", 0.0f, -2.0f, 2.0f)}, {out("Value", F)}, {},
                     "vec2 c{id} = rot({i1} * 6.2831853) * ({i0} - 0.5);\n{o0} = clamp(c{id}.x + 0.5, 0.0, 1.0);"));

    {
        NodeSpec s = make("blur", "Blur", "Filter", {vin("UV", V2, kUv), fin("Radius", 0.01f, 0.0f, 0.2f)}, {out("Colour", V4)}, {cst("Samples", 8.0f, 2.0f, 16.0f, true)},
                          "vec4 acc{id} = vec4(0.0);\nfor (int i = 0; i < {c0i}; ++i) {\n    float t{id} = (float(i) + 0.5) / float({c0i});\n    float r{id} = sqrt(t{id}) * {i1};\n    float a{id} = float(i) * 2.39996323;\n    acc{id} += texture(uTex, clamp({i0} + vec2(cos(a{id}), sin(a{id})) * r{id} * vec2(1.0 / uAspect, 1.0), 0.0, 1.0));\n}\n{o0} = acc{id} / float({c0i});");
        s.readsConst = 0;
        n.push_back(s);
    }
    {
        NodeSpec s = make("edges", "Edges", "Filter", {vin("UV", V2, kUv), fin("Strength", 1.0f, 0.0f, 8.0f)}, {out("Value", F)}, {},
                          "vec2 px{id} = 1.0 / uRes;\nfloat l{id} = luma(texture(uTex, clamp({i0} - vec2(px{id}.x, 0.0), 0.0, 1.0)).rgb);\nfloat r{id} = luma(texture(uTex, clamp({i0} + vec2(px{id}.x, 0.0), 0.0, 1.0)).rgb);\nfloat d{id} = luma(texture(uTex, clamp({i0} - vec2(0.0, px{id}.y), 0.0, 1.0)).rgb);\nfloat u{id} = luma(texture(uTex, clamp({i0} + vec2(0.0, px{id}.y), 0.0, 1.0)).rgb);\n{o0} = clamp(length(vec2(r{id} - l{id}, u{id} - d{id})) * {i1} * 4.0, 0.0, 1.0);");
        s.reads = 4;
        n.push_back(s);
    }

    {
        NodeSpec s = make("output", "Output", "Output", {vin("Colour", V4, "vec4(0.0, 0.0, 0.0, 1.0)")}, {}, {}, "return {i0};");
        s.isOutput = true;
        n.push_back(s);
    }
    return n;
}

struct Helper {
    const char* name;
    const char* source;
};

const Helper kHelpers[] = {
    {"nfxHue", R"glsl(vec3 nfxHue(vec3 c, float h) {
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 toRgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c;
    float a = h * 6.28318;
    float cs = cos(a);
    float sn = sin(a);
    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs);
    return clamp(toRgb * yiq, 0.0, 1.0);
}
)glsl"},
    {"nfxRotate", R"glsl(vec2 nfxRotate(vec2 q, float t) {
    vec2 c = (q - 0.5) * vec2(uAspect, 1.0);
    c = rot(t * 6.2831853) * c;
    return c / vec2(uAspect, 1.0) + 0.5;
}
)glsl"},
    {"nfxLfo", R"glsl(float nfxLfo(float t, int shape) {
    float f = fract(t);
    if (shape == 1) {
        return 1.0 - abs(f * 2.0 - 1.0);
    }
    if (shape == 2) {
        return f;
    }
    if (shape == 3) {
        return f < 0.5 ? 1.0 : 0.0;
    }
    return 0.5 - 0.5 * cos(f * 6.2831853);
}
)glsl"},
    {"nfxFbm", R"glsl(float nfxFbm(vec2 p, int octaves) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        if (i >= octaves) {
            break;
        }
        s += a * vn(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s;
}
)glsl"},
    {"nfxCells", R"glsl(float nfxCells(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float d = 8.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = vec2(hash21(i + g), hash21(i + g + 19.7));
            vec2 r = g + o - f;
            d = min(d, dot(r, r));
        }
    }
    return sqrt(d);
}
)glsl"},
};

}

const std::vector<NodeSpec>& nodeCatalogue() {
    static const std::vector<NodeSpec> catalogue = buildCatalogue();
    return catalogue;
}

const NodeSpec* findNodeSpec(const std::string& id) {
    for (const NodeSpec& spec : nodeCatalogue()) {
        if (id == spec.id) {
            return &spec;
        }
    }
    return nullptr;
}

const char* nodeHelperSource(const std::string& name) {
    for (const Helper& helper : kHelpers) {
        if (name == helper.name) {
            return helper.source;
        }
    }
    return nullptr;
}

const std::vector<std::string>& nodeHelperOrder() {
    static const std::vector<std::string> order = [] {
        std::vector<std::string> list;
        for (const Helper& helper : kHelpers) {
            list.push_back(helper.name);
        }
        return list;
    }();
    return order;
}

std::vector<std::string> nodeCategories() {
    std::vector<std::string> categories;
    for (const NodeSpec& spec : nodeCatalogue()) {
        if (spec.isOutput) {
            continue;
        }
        if (std::find(categories.begin(), categories.end(), spec.category) == categories.end()) {
            categories.push_back(spec.category);
        }
    }
    return categories;
}

}

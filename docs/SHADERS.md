# Your own shader clips

Any fragment shader you write, or copy from a site like Shadertoy, can become a clip. Put the file into the shaders folder, it shows up in the Library (filter Shaders) as `Shader: <file name>` and works like every other clip: put it in a cell, change Speed, Scale, colours and effects, drive it with audio, LFOs and macros through the layer and effect controls.

## Where the files go

- Library, Open shaders folder opens the folder (it is `shaders` inside the NyrexVJ data folder, shown at the bottom of the Library). Files ending in `.glsl`, `.frag` or `.fs` are used, up to 128 of them and up to 256 KiB each.
- Dragging a shader file onto the NyrexVJ window copies it into that folder.
- Library, Create example shader writes a small working shader there as a starting point.
- NyrexVJ looks at the folder once a second while it runs. A new file appears in the Library, a saved change reloads the shader and its thumbnail, so you can edit in any text editor with the clip playing. A deleted file stays in the list with the note that the file is gone.
- Presets and sessions remember the clip by its name, so keep the file name when you move to another PC.

## Two styles, found automatically

A file that has a `mainImage` function and no `void main()` is a Shadertoy style shader, anything with a `void main()` is a NyrexVJ style shader. Lines that start with `#version` are removed, so a file can keep its own.

### NyrexVJ style

Write `void main()` and set `fragColor`. These are ready to use, nothing has to be declared:

- `vUv` the position in the picture from 0 to 1, `uRes` the size in pixels, `centered()` the position from about -1 to 1 with the right aspect ratio, which also follows the Scale slider.
- `uTime` the time in seconds (follows the layer Speed and Pause), `uBeat` the beat counter, `uBpm` the tempo.
- `uColorA`, `uColorB`, `uRainbow`: the colours of the layer. `duo(t)` mixes the two colours, `pal(t)` gives the palette (or rainbow when it is on), `rainbow(t)`.
- `uAudio` is a `vec4` with bass, mid, high and level (0 to 1), `uKick` the kick, `spec(x)` the spectrum at x from 0 to 1, `wave(x)` the waveform from -1 to 1.
- Helpers: `PI`, `hash21`, `h22`, `vnoise`, `fbm`, `ss` (smooth step that is safe for equal edges), `rot`, `sdBox`.

Do not give your own function or variable one of these names, the compiler reports a redefinition. The built-in clips use the same names, so their source in `src/render/clip_library.cpp` is a good reference. The example `shaders/audio_ripples.glsl` is a complete shader in this style.

### Shadertoy style

Paste the `mainImage(out vec4 fragColor, in vec2 fragCoord)` of a Shadertoy shader as it is. Available:

- `iResolution`, `iTime`, `iFrame`, `iTimeDelta` (fixed at 1/60), `iSampleRate`, `iMouse` (always zero), `iDate` (a fixed value).
- `iChannel0` is the audio: `texture(iChannel0, vec2(x, 0.25))` is the spectrum and `vec2(x, 0.75)` the waveform, the same convention as Shadertoy's sound input.
- The NyrexVJ names `uBeat`, `uBpm`, `uColorA`, `uColorB`, `uAudio`, `uKick` can be used as well, but not the helper functions of the NyrexVJ style.
- The Scale slider zooms around the centre of the picture. The output is opaque, the alpha you write is ignored.

Not supported: several passes (Buffer A to D), textures, cubemaps, video, webcam, `iChannel1` to `iChannel3`, keyboard. A shader that needs them compiles with an error. `shaders/spiral_shadertoy.glsl` is an example.

## Errors

If a shader does not compile, its tile stays dark and the Clip window shows the compiler message in red, with line numbers that count from the first line of your file. Fix the file and save it, the clip reloads by itself. The same text is in the log. A file that has neither `void main()` nor `mainImage`, is larger than 256 KiB or is not a text file is listed with the matching message.

## Limits and safety

- A shader runs only on the graphics card. It cannot read files or use the network.
- An endless loop or a very heavy shader can freeze the picture or make the graphics driver reset (on Windows this can also close NyrexVJ). Try new shaders with small loop counts, and watch the frame time in Settings, Performance.
- Characters outside plain ASCII are replaced by spaces before compiling, so umlauts in comments are fine.

## Tested and not tested

- Tested automatically: finding and telling apart the two styles, cleaning the text, the file scan with reload, new and removed files, the example shader, and the file import (`nvj_shader_tests`).
- Tested by hand on Linux with the Mesa software renderer: both styles compile and render, a mistake is reported with the right line number, a changed file reloads while it plays.
- Not tested: the graphics drivers of NVIDIA, AMD and Intel on Windows. Drivers differ in how strict they are, for example with `int` to `float` conversions, so a shader that works on one machine can need a small fix on another.

## Not built

Sliders for your own shader parameters, several passes, textures and video as shader inputs, and a built-in editor.

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

- `iResolution`, `iTime`, `iFrame`, `iTimeDelta`, `iSampleRate`, `iMouse` (always zero), `iDate` (a fixed value). Without a `#pragma nyrexvj` line `iFrame` is derived from the time (`int(iTime * 60.0)`) and `iTimeDelta` is fixed at 1/60. With one (see Shader 2.0 below) both are real values.
- `iChannel0` is the audio: `texture(iChannel0, vec2(x, 0.25))` is the spectrum and `vec2(x, 0.75)` the waveform, the same convention as Shadertoy's sound input.
- The NyrexVJ names `uBeat`, `uBpm`, `uColorA`, `uColorB`, `uAudio`, `uKick` can be used as well, but not the helper functions of the NyrexVJ style.
- The Scale slider zooms around the centre of the picture. Without Shader 2.0 the output is opaque and the alpha you write is ignored.

`shaders/spiral_shadertoy.glsl` is an example. Shader 2.0 (alpha, Buffer A to D, images and videos as inputs) is described below; a shader that needs something else (cubemaps, webcam, keyboard) still compiles with an error.

## Shader 2.0: alpha and several passes (v0.29.0)

A file stays exactly as it was until it contains a line that starts with `#pragma nyrexvj`. Such a line is removed before compiling (line numbers stay the same) and switches the file to Shader 2.0. Without such a line nothing changes, not a byte of the compiled source (checked by a test), so every existing shader file, preset and session looks as before. Directive words are not case sensitive. A directive inside a comment is ignored. A misspelled directive is an error with the line number, so a typo cannot silently turn a feature off.

### Alpha

```glsl
#pragma nyrexvj alpha
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(1.0, 0.2, 0.1, smoothstep(0.4, 0.3, length(fragCoord / iResolution.xy - 0.5)));
}
```

`#pragma nyrexvj alpha` makes the picture of the image pass transparent where the alpha is below 1. The colour you write is straight (not multiplied by the alpha), NyrexVJ multiplies it, clamps the alpha to 0 to 1 and then composites the layer with its blend mode and opacity over the layers below. `#pragma nyrexvj alpha premultiplied` is for shaders that already multiply. It works in both styles. Without the line the Shadertoy style output stays opaque, and a NyrexVJ style shader keeps passing its alpha through as it always did (treated as premultiplied by the compositor).

### Several passes in one file

```glsl
#pragma nyrexvj buffer a
#pragma nyrexvj channel 0 buffer a
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = texture(iChannel0, uv) * 0.97 + vec4(spark(uv), 0.0, 0.0, 0.0);
}

#pragma nyrexvj image
#pragma nyrexvj channel 0 buffer a
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(texture(iChannel0, fragCoord / iResolution.xy).rgb, 1.0);
}
```

Sections start with a directive line:

| Directive | Meaning |
|---|---|
| `#pragma nyrexvj common` | Code that is added in front of every pass (functions, constants). Code before the first section line counts as common code. |
| `#pragma nyrexvj buffer a` to `d` | A buffer pass. Optional `scale 0.25` to `1` renders it smaller (the default is the size of the picture). Each pass has its own `mainImage` (or `void main`), all sections of a file use the same style. |
| `#pragma nyrexvj image` | The picture that is shown. Required when a file has sections. |
| `#pragma nyrexvj channel N source [options]` | Inside a buffer or image section: what `iChannelN` (N from 0 to 3) reads. |
| `#pragma nyrexvj alpha [premultiplied]` | See above, anywhere in the file. |

Sources for a channel: `audio` (the default for channel 0, spectrum and waveform as above), `buffer a` to `buffer d`, `media "Image: name"` or `media "Video: name"` (an image or a video of the Library, by its clip name or just its file name), and `none` (reads transparent black). Options after the source: `nearest` or `linear` (default linear), `clamp` or `repeat` (default clamp). Mipmaps are not used.

Order of work, the same as Shadertoy: Buffer A, B, C, D, then Image, once per frame. A pass that reads a buffer which was already drawn in this frame sees the new picture, a pass that reads itself or a later buffer sees the picture of the previous frame. Buffers hold 16 bit floating point colour (RGBA16F), so values above 1 and below 0 survive between frames. The image pass is written to the normal 8 bit layer picture.

Inside a pass:

- `iResolution` is the size of that pass, `iChannelResolution[N]` the size of what channel N reads (a buffer, the picture of an image or video, the number of spectrum bins by 2 for the audio, `vec3(1)` for none). `iChannelTime[N]` is the shader time.
- `iFrame` counts the frames this shader has drawn since it started, `iTimeDelta` is the step of the shader time (follows Speed, zero when paused). A trigger, a deck switch and a layer that was hidden for a moment restart the buffers (cleared to transparent black) and `iFrame`.
- Scale zooms only the image pass, so the buffers keep their own screen space.
- In the NyrexVJ style the same names are available as `uChannel0` to `uChannel3`, `uChannelRes[4]`, `uChannelTime[4]`, `uFrame` and `uDelta`.
- A video that is used as a channel is drawn with the shader time. Do not play the same video in a layer at the same time, both would pull the video to different positions. Pictures from video are premultiplied, the same as the video clip. A thumbnail of such a shader shows it without its images and videos.

### Limits

- Up to 4 buffers, 4 channels per pass and 4 different images or videos per file, 256 KiB per file as before.
- Buffers need video memory: each buffer is two pictures (current and previous) of 8 bytes per pixel, so 4 buffers at 1920 x 1080 are about 133 MB for each layer that shows such a shader. All shader buffers together stay inside a budget of 512 MiB. When a new buffer does not fit, it runs at a smaller scale (down to a quarter of the size) and the Clip window says so. When even that does not fit, the layer stays transparent, the Clip window shows the reason, and the other layers keep running.
- Buffers are freed when the shader has not been drawn for 240 frames (about four seconds at 60 fps), on a reload, and when the picture size changes. Settings, Performance shows the memory they use in the line Shader buffers.
- Several layers showing the same shader each have their own buffers.
- An error in one pass is reported with the pass name (`Buffer B:`) and the line number of your file. All failing passes are listed. The clip stays dark until the file is fixed.

### Not supported (still)

Mouse and keyboard input, webcam, cubemaps and 3D textures, mipmapped sampling, sound shaders, live inputs (NDI, camera, Spout) as a channel, shader parameters as sliders, and an editor in the program. Importing ISF files is planned separately.

## Errors

If a shader does not compile, its tile stays dark and the Clip window shows the compiler message in red, with line numbers that count from the first line of your file. Fix the file and save it, the clip reloads by itself. The same text is in the log. A file that has neither `void main()` nor `mainImage`, is larger than 256 KiB or is not a text file is listed with the matching message.

## Limits and safety

- A shader runs only on the graphics card. It cannot read files or use the network.
- An endless loop or a very heavy shader can freeze the picture or make the graphics driver reset (on Windows this can also close NyrexVJ). Try new shaders with small loop counts, and watch the frame time in Settings, Performance.
- Characters outside plain ASCII are replaced by spaces before compiling, so umlauts in comments are fine.

## Tested and not tested

- Tested automatically: finding and telling apart the two styles, cleaning the text, the file scan with reload, new and removed files, the example shader, and the file import (`nvj_shader_tests`). Also there: the parser of the `#pragma nyrexvj` lines with 31 kinds of mistakes, the generated source of every pass, a fixed fingerprint of the source of old shaders (it must never change), and the function that lowers the buffer scale to fit the budget.
- Tested automatically with a real render, `NyrexVJ --shader-selftest` (runs in the CI on Linux with the Mesa software renderer, writes its shaders into a temporary data folder): old shaders keep alpha 1 and the old alpha convention, alpha over another layer for both styles and for premultiplied, buffer feedback over several frames, the restart on a trigger, the order of the passes, `iFrame` and `iTimeDelta`, buffer scale, channel options (nearest, linear, repeat, clamp), an image of the Library as a channel, the name and line of a compile error, a broken shader next to a working one, a hot reload from a multipass to a plain file, the release of the buffers, a deck switch with a transition (the old deck keeps its buffers while it fades out), and the budget (smaller scale, refused, recovered). `NyrexVJ --shader-selftest 60` adds a timing of a plain shader, an alpha shader and shaders with two and four buffers.
- Tested by hand on Linux with the Mesa software renderer: both styles compile and render, a mistake is reported with the right line number, a changed file reloads while it plays.
- Not tested: the graphics drivers of NVIDIA, AMD and Intel on Windows, and anything on Windows at all (the CI builds there but runs no render test). Drivers differ in how strict they are, for example with `int` to `float` conversions, so a shader that works on one machine can need a small fix on another. Not tested: a video as a channel (only an image), 4K output, and the speed on a real graphics card. The timings of the software renderer say how the cost grows (more buffers, more time), not how fast a real card is.

## Not built

Sliders for your own shader parameters, an ISF import, and a built-in editor.

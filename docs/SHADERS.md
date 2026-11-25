# Your own shader clips

Any fragment shader you write, or copy from a site like Shadertoy, can become a clip. Put the file into the shaders folder, it shows up in the Library (filter Shaders) as `Shader: <file name>` and works like every other clip: put it in a cell, change Speed, Scale, colours and effects, drive it with audio, LFOs and macros through the layer and effect controls.

## Where the files go

- Library, Open shaders folder opens the folder (it is `shaders` inside the NyrexVJ data folder, shown at the bottom of the Library). Files ending in `.glsl`, `.frag` or `.fs` are used, up to 128 of them and up to 256 KiB each. An ISF file (see [ISF files](#isf-files-v0370)) is recognised by its header and works from the same folder.
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
| `#pragma nyrexvj plain` | The file gets only the uniform declarations and not the helper functions of the NyrexVJ style (`PI`, `hash21`, `rot` and so on). For code that brings its own helpers with the same names. Anywhere in the file. |
| `#pragma nyrexvj param name kind "Label" ...` | A control for your shader, see [Controls](#controls-for-your-shader-v0370). |
| `#pragma nyrexvj filter` | The file is an image filter for the effect list and not a clip, see [Image filters](#image-filters-v0370). |

Sources for a channel: `audio` (the default for channel 0, spectrum and waveform as above), `picture` (in a filter only, see below), `buffer a` to `buffer d`, `media "Image: name"` or `media "Video: name"` (an image or a video of the Library, by its clip name or just its file name), and `none` (reads transparent black). Options after the source: `nearest` or `linear` (default linear), `clamp` or `repeat` (default clamp). Mipmaps are not used.

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

Mouse and keyboard input, webcam, cubemaps and 3D textures, mipmapped sampling, sound shaders, live inputs (NDI, camera, Spout) as a channel, and an editor in the program. What an ISF file cannot do is listed under [ISF files](#isf-files-v0370).

## Controls for your shader (v0.37.0)

A shader can ask for sliders, switches, colours and menus with `param` lines. They need no code in the shader besides using the name:

```glsl
#pragma nyrexvj param speed float "Speed" 0 4 1
#pragma nyrexvj param steps int "Steps" 1 12 4
#pragma nyrexvj param flash bool "Flash" off
#pragma nyrexvj param centre point "Centre" 0 0 1 1 0.5 0.5
#pragma nyrexvj param tint color "Tint" 1 0.5 0 1
#pragma nyrexvj param mode list "Mode" 0 "Waves" 0 "Rings" 1 "Checks" 2
```

| Kind | Arguments after the label | In the shader |
|---|---|---|
| `float` | minimum maximum default | `uniform float name` |
| `int` | minimum maximum default (rounded to whole numbers) | `uniform int name` |
| `bool` | `on` or `off` | `uniform bool name` |
| `point` | minX minY maxX maxY defaultX defaultY | `uniform vec2 name` |
| `color` | red green blue alpha, each 0 to 1 | `uniform vec4 name` |
| `list` | the number of the default entry, then pairs of `"Label" value` (1 to 16 entries) | `uniform int name`, the value of the chosen entry |

You do not declare the uniform, NyrexVJ does it. The name uses letters, digits and underscore (up to 32 characters, not a name that NyrexVJ, Shadertoy or GLSL already use, for example `iTime`, `uTime`, `texture`, `filter` or `input`), the label has 1 to 40 characters, numbers are between -1,000,000 and 1,000,000. A file can have up to 8 controls and up to 16 numbers together (a point is 2, a colour is 4). A mistake in a `param` line is reported with its line number.

Where you see and change them:

- A clip: the Clip window shows the controls under "Shader controls" for the cell that is playing. Right click on a control puts it back to the value from the file, "Reset controls" does it for all. Every cell keeps its own values, so the same shader can be in two cells with different settings. Presets and sessions store the values of a cell only when you changed one.
- An image filter: the controls are the four controls of the effect, see below.

A control you never touched uses the value from the file, so changing a default in the file moves every cell that still has the old one.

## Image filters (v0.37.0)

`#pragma nyrexvj filter` makes the file an effect and not a clip. It is then in the effect list as `ISF: <file name>` (every filter file gets this name, whatever its style) and not in the Library. A filter file that cannot be read at all (a mistake in the ISF header, an unsupported feature) cannot be told apart from a clip, so it stays in the Library with its message. The picture of the layer (everything before this effect in the rack) is the input:

- In the first channel of a pass, `#pragma nyrexvj channel 0 picture` reads that picture (`iChannel0`, or `uChannel0` in the NyrexVJ style). `picture` is allowed only in a filter, and it can be used in several passes.
- The result is mixed with the input by the strength of the effect (the gate in the effect's audio settings uses it), so a filter fades in and out like every other effect.
- The first four controls (float, int, bool or list, in the order of the file) become the four controls of the effect, with the label and the default from the file. They are saved with presets, can follow audio, LFOs and macros like any effect control, and the effect list shows the range of each as 0 to 1 over the range from the file. More controls than four keep the value from the file, and the Clip window and the effect panel say which.
- A filter can have buffers and several passes exactly like a clip. Its buffers are held per effect slot, are freed after four seconds without use and count in the 512 MiB budget.
- An error stays with the effect: the picture passes through unchanged, the effect panel shows the message in red with the line number of the file, and the other effects and layers keep running.

## ISF files (v0.37.0)

ISF ("Interactive Shader Format", the format of VIDVOX and the Syphon world) is read directly. Put a `.fs` file into the shaders folder. A file that starts with a `/*{ ... }*/` header is read as ISF, everything else works as before. NyrexVJ turns the ISF file into the same plan as a Shader 2.0 file (buffers, channels, controls) and runs it with the same code, so memory budget, release of buffers, hot reload, errors and thumbnails behave the same. Error lines count from the first line of your file.

A file with an input named `inputImage` is a filter and goes into the effect list, a file without one is a generator and goes into the Library as a clip.

| ISF | What NyrexVJ does |
|---|---|
| `DESCRIPTION`, `CREDIT`, `CATEGORIES`, `ISFVSN` | Shown in the Clip window under the type of the clip (the version reads for example `ISF 2.0`). |
| `float` input (`MIN`, `MAX`, `DEFAULT`, `LABEL`) | A slider. A default outside the range is moved into it (and the Clip window says so). |
| `long` input (`VALUES`, `LABELS`) | A menu. `VALUES` holds the numbers the shader gets as `int`. |
| `bool` input | A switch. |
| `event` input | A switch. It is never pulsed by the program, the Clip window says so. |
| `point2D` input | Two sliders. Without `MAX` the range 0 to 1920 by 0 to 1080 is used. |
| `color` input | A colour with alpha. |
| `image` input named `inputImage` | The picture that the filter works on. |
| `IMPORTED` | A picture from the Library, found by the file name without the ending (`Image: name`). Copy the picture into the media folder under that name. |
| `PASSES` with `TARGET` | The targets become buffers A to D in the order of the file. Buffers are always kept between frames and always 16 bit floating point, so `PERSISTENT` and `FLOAT` are accepted and need no setting. The last pass has no `TARGET` and is the picture that is shown. |
| `WIDTH`, `HEIGHT` of a pass | Calculated from `$WIDTH`, `$HEIGHT` and the names of float inputs with `+ - * /`, brackets, `floor`, `ceil`, `round`, `abs`, `min`, `max`. A size that is a fixed share of the picture (for example `$WIDTH/2`) becomes the scale of the buffer, from a quarter to the full size. |
| `PASSINDEX`, `TIME`, `TIMEDELTA`, `FRAMEINDEX`, `RENDERSIZE`, `isf_FragNormCoord`, `IMG_PIXEL`, `IMG_NORM_PIXEL`, `IMG_THIS_PIXEL`, `IMG_THIS_NORM_PIXEL`, `IMG_SIZE`, `texture2D` | Supported. `TIME` is the shader time (follows Speed and pause). |
| `gl_FragColor` | Supported. The alpha channel is used (straight alpha, as in Shader 2.0). |

What is not supported, and what happens:

- **Vertex shaders and old OpenGL.** NyrexVJ reads only the fragment part and uses OpenGL 3.3. A file that uses `varying`, `attribute`, `isf_vertShaderInit`, `gl_TexCoord`, `gl_Vertex`, the old matrices, `ftransform`, `sampler2DRect` or `texture2DRect` is refused with the name of the first one found. `.vs` files are not read.
- **Transitions** (`startImage` and `endImage`): refused. NyrexVJ has its own deck transitions.
- **`audio` and `audioFFT` inputs, cube map imports and other image inputs than `inputImage`:** the shader still runs, these read transparent black, and the Clip window lists each one. (The audio of NyrexVJ is available to the NyrexVJ style as `uAudio` and as channel 0, but it is not wired to ISF inputs.)
- **`DATE`** has the fixed value year 2026, month 1, day 1, 0 seconds; the real date and time of day are not available.
- **More than 8 controls or 16 numbers:** the rest are fixed to their default values (the file keeps working, the Clip window lists them). **More than four controls in a filter:** the first four are on the effect, the rest keep the defaults.
- **A buffer size that is not a fixed share** of the picture (a fixed pixel size, a different factor for width and height, an expression that is not understood) uses the full picture size, and the Clip window says so.
- **More than 4 buffers, more than 4 different pictures** (buffers, imported images and the filter picture together) in one file: refused.
- **Names:** an input name that GLSL 3.30 reserves (`filter`, `input`, `long`, `common`, `smooth` and so on), a name of NyrexVJ or of ISF is refused with the name.

Safety limits when a file is read: the header up to 64 KiB, nested at most 12 levels, at most 64 inputs, 16 passes, 8 imported pictures and 8 categories, texts up to 512 bytes, numbers between -1,000,000 and 1,000,000. A mistake in the header is reported with the line number inside the header, a mistake in the shader with the line number of the file. The header is read as data and never run. A file that fails stays in the Library (or the effect list) with its message, the picture stays dark (or passes through for a filter), and every other layer and effect runs on.

Examples for every kind are in `docs/shaders/`: `isf_plasma.fs` (a generator with sliders, a menu, a colour and a switch), `isf_feedback_dots.fs` (a generator with a buffer that fades at half size), `isf_color_shift.fs` (a one pass filter), `isf_blur.fs` (a two pass filter with a half size buffer) and `isf_trails.fs` (a filter with a buffer that is kept between frames). Copy one into the shaders folder to try it.

## The Shader editor (v0.46.0)

Creator, Shader editor... opens a window with a text editor, a live preview and a list of problems; the Clip window of a shader file has a button Edit in shader editor. Start option `--shader-editor [FILE]` opens it at start. It works on the same files as everything above and compiles with the same code, so a shader that works in the preview works as a clip. See [ADR 0039](adr/0039-shader-playground.md).

- **Text.** Line numbers, colours for keywords, types, built-in functions, the NyrexVJ uniforms (`uTime` and the others), `#pragma nyrexvj` lines, numbers, comments and preprocessor lines. Undo and redo (Ctrl+Z, Ctrl+Y), Tab and Shift+Tab indent, Ctrl+/ comments, Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V, double click selects a word, triple click a line, a context menu. Up to 256 KiB. Lines over 1,500 characters are drawn cut off but kept.
- **Errors.** The messages of the driver are turned into lines (Mesa, NVIDIA, AMD and ANGLE styles, the pass headers `Buffer B:` and the `Line N:` form of this program). The line is tinted, the gutter has a mark with the message as a tooltip, and the list below the text jumps to the line when you click it. A message that cannot be tied to a line stays in the list.
- **Live preview.** The shader is compiled 0.6 s after you stop typing, never while you type, and never on the render thread's critical path: where the driver supports parallel shader compile the engine only polls for the result, otherwise the driver compiles during the call and the window says so. F5 or Compile now does not wait. The preview has its own programs, buffers and 480 x 270 target and never touches a clip that is playing.
- **The last working version stays.** A shader that does not compile, does not link, is refused by the memory budget or times out leaves the previous picture and shows the error; Shown version says which text the picture comes from. A shader that is too slow is slowed down (every n-th frame) and, if it needs more than 120 ms, stopped until you press Resume.
- **What the preview supports.** What Shader 2.0 supports: both styles, `alpha` (drawn over a checkerboard, premultiplied), `buffer a` to `d` with their size and persistence, `image` and `channel` lines (the channels are transparent in the preview), `param` lines (at their defaults), `common`, ISF generators, and the uniforms `uTime`, `uResolution`, `iFrame`, `iTimeDelta` and the audio values the program is listening to at that moment. Filters (`#pragma nyrexvj filter`) need a picture to filter and are not previewed.
- **Files.** New starts from one of four templates (NyrexVJ style, Shadertoy style, transparent, two passes with a buffer) and is not saved until you say so. Save writes `.glsl`, `.frag` or `.fs` into the shaders folder: the name may use ASCII letters, digits, space and `- _ . ( )`, at most 64 characters, no Windows device names, no folders, no links; the file is written in one step (a crash cannot leave half a file); an existing file is replaced only after you agree. The folder is scanned once a second, so a saved file reloads in the clips that use it by the same path as before; the editor itself never pushes text into a clip.
- **A file that changed on disk.** The editor compares the content (not only the time) once a second. Without unsaved edits a changed file is loaded again. With unsaved edits you choose: keep yours and overwrite, load the disk version, or save a copy. If the file was deleted, Save writes it again. A file over 256 KiB, a folder, a link or a file that cannot be read is refused with a message.
- **Closing.** A window with unsaved text asks before it closes or before another file replaces it.

### Tested and not tested (the editor)

- Tested automatically: `nvj_shaderedit_tests` (26,163 checks): the text model (typing, deleting, lines, selections, tabs, undo and redo with the 400 step and 4 MiB limits, random edits against a plain string model), the highlighter on every kind of token and on random text, the compile log parser on real messages of four drivers and on random logs, the compile scheduler (debounce, maximum wait, minimum gap, one compile at a time), the cost governor (running, throttled, paused, probation, stale results), the file name check (including `CON`, `NUL`, `COM1`, a trailing dot or space, 64 characters, double extensions, paths), the file stamps and the save conflict (changed, deleted, unchanged, same content with a new time), the safe save and every refusal (size, link, folder, no overwrite), and the four templates.
- Tested with a real render, `NyrexVJ --shader-editor-selftest` (67 checks, runs in the CI on Linux with the Mesa software renderer): valid and invalid shaders, the last working version after each kind of failure (parser error, compile error, link error, memory budget, empty text, text over the limit), the four templates drawn, a two pass shader, the cost control with a heavy shader, that a clip that is playing is not changed by the preview, save and reload through the real folder scan, and a timing. `NyrexVJ --shader-editor-selftest 60` adds the benchmark (see [PERFORMANCE.md](PERFORMANCE.md)).
- Tested with real input events, `NyrexVJ --shader-editor-uitest` (about 50 s on the software renderer): the editor opening with a template, a mouse click that puts the caret on a line, Ctrl+A and Delete, an empty text (the last working version stays), typing, a typed mistake that is shown on its line while the preview keeps the last working version, undo and redo (Ctrl+Z, Ctrl+Y), Ctrl+/ to comment and uncomment, a selection with Shift+Right that typing replaces and undo restores, Ctrl+S on a new shader asking for a name and Enter saving it, a change by another program that is loaded when nothing is unsaved, and the conflict dialog when there are unsaved edits (nothing is written before the decision).
- Not driven by input events in a test, only by the unit tests of the model: Tab and Shift+Tab, the clipboard, double and triple click, a click on an entry of the problem list, and the name rules in the Save As dialog.
- Tested by hand with screenshots on Linux with the Mesa software renderer: the window with a broken shader (errors on the right lines, "No picture yet" and "There is no working version yet.").
- Not tested: any real graphics card. The behaviour of NVIDIA, AMD and Intel drivers (which of them accept parallel compile, how strict they are with a broken shader, how long a big shader takes to compile) is unknown here; Mesa caches compiled shaders, so the compile times in the tests are tiny. Not tested: Windows beyond the CI build (the clipboard, a save conflict with cloud sync or antivirus holding the file, a file open in another program), keyboard layouts with dead keys, input methods that compose text (only finished characters are read), a person editing for hours, and an editor open during a long show with video and projectors.
- A limit that stays: a shader that never ends cannot be cancelled once the driver has it. The cost control reacts only after a frame returns, so an endless loop can still freeze the graphics driver, as it can in a clip.

## Errors

If a shader does not compile, its tile stays dark and the Clip window shows the compiler message in red, with line numbers that count from the first line of your file. Fix the file and save it, the clip reloads by itself. The same text is in the log. A file that has neither `void main()` nor `mainImage`, is larger than 256 KiB or is not a text file is listed with the matching message.

## Limits and safety

- A shader runs only on the graphics card. It cannot read files or use the network.
- An endless loop or a very heavy shader can freeze the picture or make the graphics driver reset (on Windows this can also close NyrexVJ). Try new shaders with small loop counts, and watch the frame time in Settings, Performance.
- Characters outside plain ASCII are replaced by spaces before compiling, so umlauts in comments are fine.

## Tested and not tested

- Tested automatically: finding and telling apart the two styles, cleaning the text, the file scan with reload, new and removed files, the example shader, and the file import (`nvj_shader_tests`). Also there: the parser of the `#pragma nyrexvj` lines with 31 kinds of mistakes, the generated source of every pass, a fixed fingerprint of the source of old shaders (it must never change), and the function that lowers the buffer scale to fit the budget.
- Tested automatically with a real render, `NyrexVJ --shader-selftest` (runs in the CI on Linux with the Mesa software renderer, writes its shaders into a temporary data folder): old shaders keep alpha 1 and the old alpha convention, alpha over another layer for both styles and for premultiplied, buffer feedback over several frames, the restart on a trigger, the order of the passes, `iFrame` and `iTimeDelta`, buffer scale, channel options (nearest, linear, repeat, clamp), an image of the Library as a channel, the name and line of a compile error, a broken shader next to a working one, a hot reload from a multipass to a plain file, the release of the buffers, a deck switch with a transition (the old deck keeps its buffers while it fades out), and the budget (smaller scale, refused, recovered). `NyrexVJ --shader-selftest 60` adds a timing of a plain shader, an alpha shader and shaders with two and four buffers.
- ISF and controls (v0.37.0), tested automatically: `nvj_isf_tests` (22,970 checks): telling ISF from other files, the five example files (controls, plan, generated GLSL, line numbers), 44 kinds of damaged ISF files each with the message that is expected (unclosed header, wrong JSON with its line, nesting, a huge header, too many inputs, buffers, passes or pictures, bad or reserved or doubled names, wrong types, ranges and numbers, a transition, vertex and old OpenGL features, a missing `main`), 27 kinds of mistakes in `param` lines, buffer size expressions, every note about an unsupported feature, the conversion between the four effect controls and the parameters, the preset round trip of the cell values (with hostile numbers), and 22,699 cut, mutated and random inputs (no crash, always a result or a message, 0.75 s together).
- ISF and controls, tested with a real render (`NyrexVJ --shader-selftest`, now 94 checks, 54 of them new): an ISF generator with its defaults, a stored value per cell for a float, a colour, a bool and a list, unset values back to the defaults, a persistent buffer at half size (it keeps its content, and uses a quarter of the memory of a full size one), an imported picture, an ISF filter in the effect rack (changes the layer, first control, switched off, the strength gate, absent from the Library), a two pass blur and a persistent filter buffer with their memory released after use, a compile error with the right line, an unsupported file next to a working layer, a broken filter that passes the picture through, hot reload of a generator and of a filter, a removed filter, and the five example files.
- Frame times, from `NyrexVJ --shader-selftest 60` on the Mesa software renderer at 1280 x 720 (two runs each, average per frame in ms; the previous release is the commit before this change built the same way): plain single pass 13.4 and 12.9 before, 13.3 and 13.7 now; alpha 13.3 and 13.2 before, 13.3 and 14.5 now; two buffers 37.0 and 37.7 before, 37.7 and 36.8 now; four buffers 55.4 and 54.7 before, 58.2 and 55.7 now; the memory of the buffers is the same byte for byte (29,491,200 and 58,982,400). So the existing paths did not get slower beyond the noise of about 5 percent. New cases: an ISF generator 13.4 and 13.9 (the same as the plain shader), an ISF generator with two controls 13.5 and 13.9, an ISF generator with a half size buffer 18.2 and 18.5 (3,686,400 bytes), a one pass filter on a generator 17.2 and 18.5, the two pass blur filter 59.3 and 59.7 (3,686,400 bytes), the trails filter 30.4 and 29.4 (14,745,600 bytes). The software renderer draws on the processor, so these numbers show how the cost grows, not how fast a graphics card is.
- Tested by hand on Linux with the Mesa software renderer: both styles compile and render, a mistake is reported with the right line number, a changed file reloads while it plays.
- Not tested, ISF: any real graphics card, files made by other programs in bulk (only the five examples written for this change were rendered, so features that real files use but these do not may still fail), a comparison of the picture with VDMX, Resolume or another ISF host, Windows beyond the CI build and the unit tests, projectors and VRChat. The notes of the Clip window list what a file loses, but the picture of a file that uses an unsupported feature (audio inputs, for example) can differ from the other program.
- Not tested: the graphics drivers of NVIDIA, AMD and Intel on Windows, and anything on Windows at all (the CI builds there but runs no render test). Drivers differ in how strict they are, for example with `int` to `float` conversions, so a shader that works on one machine can need a small fix on another. Not tested: a video as a channel (only an image), 4K output, and the speed on a real graphics card. The timings of the software renderer say how the cost grows (more buffers, more time), not how fast a real card is.

## Not built

In the editor: autocompletion, search and replace, several open files at once, a shader debugger, recovery files for unsaved text, a preview of image filters, and media channels in the preview. Also not built: ISF vertex shaders, ISF transitions, ISF audio inputs, and moving the controls of a clip with audio, LFOs, MIDI or OSC (they are set by hand in the Clip window; the four controls of a filter can be moved because they are effect controls).

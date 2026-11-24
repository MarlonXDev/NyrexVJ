# ADR 0022: GLSL Shader 2.0, alpha output, Buffer A to D and several inputs for shader clips

- **Status:** Accepted (2026-11-24, requested by MarlonXDev), implemented in v0.29.0. Issue [#70](https://github.com/MarlonXDev/NyrexVJ/issues/70).
- **Date:** 2026-11-24
- **Owners:** Maintainers (pending assignment)
- **Related:** [SHADERS.md](../SHADERS.md), [PERFORMANCE.md](../PERFORMANCE.md), [INTERFACES.md](../INTERFACES.md), [ADR 0021](0021-dxv-hap-conversion-and-ffmpeg-setup.md)

## Context
Shader clips (v0.16.0) are one fragment shader per file, in the NyrexVJ style or the Shadertoy style, drawn into the opaque layer picture. The request for this change asks for transparency from a custom GLSL clip, an optional multipass setup with Buffer A to D, several texture or video inputs, Shadertoy uniforms, an unchanged old interface and old presets, a clear list of what is not supported, and checks of alpha blending, feedback, render target reuse, memory limits and error handling, with shader tests and GPU benchmarks.

## Findings (what existed before)
- **One pass, one output.** `Engine::drawLayerSource` drew the clip program into the shared RGBA8 layer target with blending off. A Shadertoy style shader had its alpha forced to 1, a NyrexVJ style shader passed its alpha through. The compositor treats the layer picture as premultiplied (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`).
- **Channels.** Shadertoy style shaders had `iChannel0` as the audio texture only. `iFrame` was derived from the time and `iTimeDelta` was fixed at 1/60.
- **Layers.** Four layers (`kLayerCount`), 16 decks, a preview, and a deck transition that renders two decks at once. A shader clip that exists in two places at once (two layers, two decks, the preview) must not share state.
- **Presets and sessions** store a clip by its name (`Shader: file name`) and its parameters; nothing about the shader source.
- **What is in the old source.** A fixed fingerprint (FNV-1a) of the generated source of the legacy files can be tested, so "unchanged" can be proven and not only believed.

## Decision

### Compatibility
1. **Opt in by a line in the file.** A file turns into a Shader 2.0 shader only when a line starts with `#pragma nyrexvj`. Without such a line the generated source is byte for byte what it was (a test compares a fixed fingerprint and the source of the example shaders), so existing files, presets and sessions behave as before. The old alpha behaviour stays: opaque for the Shadertoy style, passed through for the NyrexVJ style.
2. **The directive lines are removed, not commented**, and the line count is kept (`#line` per pass), so a compile error still names the right line of the person's file. A directive is recognised on comment-stripped text, so one inside a comment is ignored. An unknown or malformed directive is an error with its line number, never silently ignored, because a typo must not turn a feature off.

### Alpha
3. `#pragma nyrexvj alpha` makes the image pass write the alpha. By default the colour is read as straight and multiplied by the (clamped) alpha before it is stored; `alpha premultiplied` skips the multiplication. The layer is then composited with the normal blend mode, opacity and effects over the layers below, which already work on premultiplied pictures.

### Passes
4. **One file, sections.** `common`, `buffer a` to `buffer d` (optional `scale 0.25` to `1`), `image` and `channel N source options` inside a section. Code before the first section is common code. Both styles work, but a file uses one style for all its sections. A file with sections needs an image section. Evaluation order is Buffer A, B, C, D, Image once per frame, the same as Shadertoy: a pass that reads an earlier buffer sees the new picture of this frame, a pass that reads itself or a later buffer sees the previous frame.
5. **Buffers are RGBA16F ping-pong pairs** (`createFloat`), because feedback shaders keep values above 1 and below 0 and 8 bit would band and clip them. The image pass is written into the existing RGBA8 layer target, so the compositor and everything after it are unchanged. Buffers are cleared to transparent black when allocated and restarted.
6. **State belongs to an instance, not to a clip.** The key is `deck * kLayerCount + layer`, the preview has its own key (`2 * kLayerCount`), so a layer that shows the same shader as another one, the old deck during a transition and the thumbnail never share or overwrite each other's buffers. A thumbnail uses a temporary state with 8 warm-up frames and throws it away.
7. **When state restarts.** On a trigger, on a deck switch, when the shader was not drawn for more than 3 frames, when the shader clock jumps back by more than 0.25 s, on a reload, and when the picture size changes. A retrigger inside 0.25 s is caught by an explicit restart flag (`restartShaderLayer`, `restartShaders`): the self test found that the clock check alone missed it.
8. **When state is freed.** After 240 frames without drawing (about four seconds at 60 fps), on reload and on resize. The old deck keeps its buffers while a transition fades it out.

### Inputs
9. **Four channels per pass, sources:** the audio texture (default for channel 0, unchanged), `buffer a` to `d`, `media "Image: name"` or `"Video: name"` from the Library, and `none` (transparent black). Options `nearest` or `linear`, `clamp` or `repeat`, set through sampler objects so one texture can be read two ways in one frame. Texture units 3 to 6 are used so they cannot collide with the units of the other passes. No mipmaps.
10. **Media as input** is drawn with the existing media program into a RGBA8 target of its own (premultiplied, as video clips are), then read like any texture. At most 4 different media inputs per file. A video used as a channel runs on the shader time, so it should not play in a layer at the same time.
11. **Uniforms.** Shadertoy style: `iResolution` (per pass), `iTime`, `iTimeDelta` and `iFrame` (real values with Shader 2.0, the old derived values without), `iChannelResolution[4]`, `iChannelTime[4]`, `iSampleRate`, `iMouse` (zero), `iDate`. NyrexVJ style: `uChannel0` to `3`, `uChannelRes[4]`, `uChannelTime[4]`, `uFrame`, `uDelta`. Scale zooms only the image pass.

### Memory and errors
12. **Budget.** All shader buffers share 512 MiB. A new buffer that does not fit is lowered in steps (`fitShaderScale`) down to a quarter of the size; when even that does not fit the layer stays transparent, the Clip window says why and the other layers run on. `Engine::shaderBufferBytes()` is shown in Settings, Performance, Memory and in the benchmark JSON.
13. **Errors per pass.** Each pass compiles on its own. A failure is reported as `Buffer B: ...` with the line numbers of the person's file (`#line`), all failing passes are listed, and a broken clip draws nothing while the others keep running. A reload replaces the clip only when it compiled.

### Tests and measurements
14. `nvj_shader_tests` (248 checks, no GL): the directive parser with 31 kinds of mistakes, the source of every pass, a fixed fingerprint of the legacy source, and `fitShaderScale`. `NyrexVJ --shader-selftest [FRAMES]` (40 checks, real GL, runs in the CI on Linux with the Mesa software renderer, own data folder): alpha for both styles, premultiplied, feedback over frames, restart, pass order, `iFrame`, `iTimeDelta`, buffer scale, channel options, an image as input, error names and lines, broken next to working, hot reload, release, budget, and a deck switch with a transition. With `FRAMES` it also times a plain shader, an alpha shader and shaders with two and four buffers (`--bench-out FILE.json` writes the result).

## Consequences
- Old shader files are byte-identical in what the driver compiles; they cost nothing extra.
- A multipass shader costs one extra full-screen pass and 16 bytes per pixel per buffer. Measured with the Mesa software renderer at 1280 x 720 (not a graphics card): a plain shader 12.3 ms per frame, an alpha shader 11.8 ms, two buffers 27.3 ms, four buffers 49.3 ms, with 29.5 MB and 59 MB of buffers. The numbers show that the cost grows with the passes, they do not say how fast a real card is.
- Four layers each showing a four buffer shader at 1920 x 1080 would need about 530 MB; the budget lowers the scale of the last ones instead of running out of video memory.

## Alternatives
- **Detect multipass shaders without a marker** (for example from `Buffer A` comments). Rejected: Shadertoy does not export a single file with sections, and guessing would change files that work today.
- **One file per pass** (`name.bufferA.glsl`). Rejected: harder to share and to reload as one clip, and a half-copied set would fail in confusing ways. Sections in one file reload atomically.
- **Make alpha the default for all Shadertoy shaders.** Rejected: it would change every existing clip and many Shadertoy shaders write garbage into alpha.
- **8 bit buffers.** Rejected: feedback shaders band and clip.
- **Share one state per clip.** Rejected: two layers showing the same clip would draw into each other's feedback.
- **Mipmaps, mouse, keyboard, webcam, cubemaps, sound shaders, live inputs as channels.** Not in this change; the list is in [SHADERS.md](../SHADERS.md). ISF import is a separate request.

## Not verified
Nothing was run on a real graphics card, on Windows, or with the drivers of NVIDIA, AMD or Intel, and nothing next to VRChat. A video as a channel was not tested (only an image). 4K was not run (the budget logic was tested through `setShaderBufferBudget` with small budgets). The timings are from the software renderer. The media inputs read a premultiplied picture; a shader that expects straight colour needs to divide by alpha itself.

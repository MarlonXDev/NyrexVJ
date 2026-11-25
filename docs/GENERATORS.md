# Text, particles and effect chains

Added in 0.43.0 ([ADR 0036](adr/0036-text-particles-chains.md)); the particle clips can also run on the graphics card since 0.44.0, see [GPU particles](#gpu-particles-experimental). Three things that were missing for a VJ working with names, logos and sparkle: text as a clip, a small particle system as a clip, and effect chains that you can save, search, preview, export and import. Text and particles are normal clips. They sit in the clip grid, on layers and in presets, and every effect, blend mode, audio reaction and output works with them as with any other clip. There is no second effect engine: a chain is the four effect slots that the program already has, written to a file.

## What was there before

| Part | State | Where |
| --- | --- | --- |
| Generative clips as GLSL shaders, shader files, ISF | existed | `src/render/clip_library.cpp`, [SHADERS.md](SHADERS.md) |
| Effect library, four slots per layer and for the master, audio reaction per slot | existed | `src/render/effect_library.*`, `src/core/composition.hpp` |
| Node effect editor and `.nyxfx` files | existed | [NODES.md](NODES.md) |
| 3D scene engine | existed, not touched | [SCENE3D.md](SCENE3D.md) |
| Presets that contain the effect slots of every layer | existed | `src/core/preset.cpp` |
| Library search for clips and effects | existed | `src/app/ui_browser.cpp` |
| Text rendering, fonts, text clips | not found | |
| Particle system | not found | |
| Effect chains as files, search and preview of chains | not found | |

The effect slots were written to JSON inside the preset code. That code moved to `src/core/effect_json.*` so the presets and the chain files use the same lines, and a preset written before 0.43.0 reads exactly as before (tested).

## Text clips

Creator, Text and particles... opens the window on the Text tab. A text clip is a file `texts/<name>.nyxtext` in the data folder. The window edits a text, shows it live in a preview (480 x 270, drawn by the engine) and saves it. After saving, the clip appears in the Library as `Text: <name>` under the filter Generators a moment later, and a clip that is already playing changes to the new text.

| Setting | Range | Notes |
| --- | --- | --- |
| Text | up to 1000 characters and 2000 bytes, up to 40 lines | Line breaks are kept, long lines wrap to the width of Wrap |
| Font | the fonts the program finds (see below) | A name that is not installed falls back to a default font (tested) |
| Size | 0.02 to 0.9 of the picture height | |
| Align | Left, Centre, Right and Top, Middle, Bottom | |
| Line spacing, Tracking | 0.5 to 3, -0.5 to 2 | |
| Wrap | 0 to 1 of the picture width, 0 is off | |
| Position | -1 to 1 on both axes | |
| Shadow | 0 to 1 | |
| Kerning | on or off | |
| Colour | Layer colour, White, Rainbow | Layer colour uses the colours of the Clip window |
| Animation | No animation, Typewriter, Fade in, Slide up, Wave, Pulse, Follow the audio level, Bounce | speed 0.1 to 4, amount 0 to 1 |

The text is drawn premultiplied with an alpha channel and a transparent background, so a text layer above another layer shows the layer below around the letters. The animations use the clock of the engine; Audio changes the size with the music level.

**Fonts.** The program reads TrueType and OpenType (`.ttf`, `.otf`, `.ttc`) files from three places: the folder `fonts` in the data folder, the system font folder (Windows `%WINDIR%\Fonts`, Linux `/usr/share/fonts` and similar) and a few fixed CJK fonts. Sans, Sans Bold, Serif, Mono and Display are names for a system font that is found first on your computer. Each font file may be 64 MiB at most. Fonts are read with stb_truetype (the copy that comes with Dear ImGui, so there is no new library). The program checks the tables, the number of points and nested glyphs before it hands a font over, but stb_truetype itself is not written for hostile files; use fonts you trust.

**Unicode.** Text is read as UTF-8 and the program decodes it completely: umlauts, accents, symbols such as ♪, Greek, Cyrillic and CJK all reach the renderer. What you see depends on the font: a character the font does not have is drawn as `?` and the window shows the note "Some characters are not in the font and show as a question mark". Not built: shaping (ligatures, Arabic and Indic scripts), right to left text and emoji with colour. Kerning uses the old `kern` table of the font; the newer GPOS kerning is not read.

**Limits.** Up to 1024 different characters are rasterised into one 2048 x 2048 atlas of one byte per pixel (4 MiB at most), the pixel height of the glyphs is 12 to 320, and a text that does not fit is shortened and says so. The atlas is baked on a worker thread, so a long text never stops the show; the clip shows nothing until it is ready, and a text that is not used for two minutes frees its atlas.

## Particle clips

The Particles tab edits a particle clip, a file `particles/<name>.nyxpart`. Six built-in clips come with the program under the filter Generators: Sparks, Snow, Embers, Fountain, Confetti and Bubbles. A saved file shows as `Particles: <name>`.

The simulation runs on the processor in fixed steps of 1/60 s (at most 8 steps in one frame) and the result is drawn as one instanced draw call with a soft round sprite, so it needs OpenGL 3.3 and nothing more. It is deterministic: the same file, the same seed and the same clock and audio give the same particles. The settings are rate (0 to 2000 a second), burst on every beat (0 to 500), life, speed, direction, spread, gravity, drag, turbulence, size at start and end, jitter for life, speed and size, fade in, softness, emitter shape (Point, Line, Ring, Box) with position and extent, swirl (-4 to 4, since 0.44.0), colour (Gradient, Rainbow, White), additive or normal blending and a seed.

**Audio and beat.** Every beat of the engine's beat clock fires one burst, even when the steady rate is 0. The music level adds to the rate, the size and the speed with the three Level sliders. Without audio the level is 0 and nothing else changes.

**Upper limits.** No particle clip ever has more particles alive than Settings, Performance, Text and particles, Particle limit per layer: 64 to 16,384, default 2048. When the limit is reached, new particles are dropped and counted, the picture stays smooth. A particle is 24 bytes of graphics memory, so the buffer of a particle clip is at most 384 KiB (16,384 particles) and every loaded particle clip has one. A clip can play in up to 16 places at once (layers, decks and previews); each place keeps its own particles on the processor and the buffer is filled for one place after the other. A step creates at most 256 particles and a burst at most 500, so a wrong file cannot stall a frame.

## Effect chains

An effect chain is the four effect slots of a layer or of the master, written to `chains/<name>.nyxchain` with an optional description (up to 200 bytes). The window Creator, Effect chains... and the button Effect chains... under the effect slots of a layer or of the master do four things.

- **Save current as chain** stores the slots of the selected target (a layer or the master) with their effects, switches, four parameters and audio reaction.
- **Apply** replaces the slots of the target with the chain. It is one step in Undo.
- **Search and preview.** The search field looks in the name, the description and the effect names. The preview draws the selected chain, or the current slots, over a fixed clip (Plasma) in 480 x 270 using the same effect code that the layers use.
- **Export and import.** Export copies the chain file to a path you give, import reads a `.nyxchain` file from a path. An import names every effect that does not exist on this computer (for example a node effect you do not have) and keeps the rest; a chain with more than four effects is shortened with a notice; a damaged file, a file over 32 KiB or another format is refused and changes nothing.

Chains are not part of show packages or community packages yet. Copy the files by hand or ask for it in [ADR 0036](adr/0036-text-particles-chains.md). A show package does not pack text and particle files either: the plan of an export lists the text and particle clips of the show as clips that the other computer needs to have (as it does for Spout, NDI and cameras).

## GPU particles (experimental)

Added in 0.44.0 ([ADR 0037](adr/0037-gpu-particles.md)). The same particle clips, but the graphics card keeps and moves the particles. Off until Settings, Performance, Text and particles, Experimental GPU particles is turned on, because it has not been run on a real graphics card yet. Turned off, or when anything is missing, a GPU clip plays on the processor like any other particle clip.

### What was there before

| Part | State | Where |
| --- | --- | --- |
| Particle clips on the processor, limit 16,384, `.nyxpart` version 1 | existed (0.43.0) | `src/core/particles.*`, `src/render/generators.*` |
| OpenGL 3.3 core context | existed | `src/main.cpp` |
| Transform feedback functions in the loader | existed (core since OpenGL 3.0) | `third_party/glad` |
| Compute shaders | not usable: needs OpenGL 4.3, the context asks for 3.3, the function pointer is null there | |
| Simulation on the graphics card | not found | |
| What a real driver returns for a 3.3 request | not verified (the build machine's Mesa returns 4.5) | |

### How it works

- **One pass and one draw.** The state of every particle (position, velocity, age, lifetime, size, hue: 8 floats) is in a buffer on the card. Every 1/60 s step a vertex shader reads the old buffer and writes the new one through transform feedback with rasterising turned off. One instanced draw then reads the same buffer. This needs OpenGL 3.3 and nothing more.
- **A ring of slots.** Particles are born into the next free slots of a ring; when the ring wraps, the oldest slots are reused, alive or not. The processor works out from the step counter which slots a step creates, so nothing is read back from the card and nothing waits. Slots that were never used are neither updated nor drawn.
- **Deterministic.** Randomness is a stateless integer hash of seed, birth number and stream number, computed the same way in GLSL and in C++. The steps, the births, the beat bursts and the audio level come from the same fixed 1/60 s clock as the processor system (at most 8 steps in a frame). The same file, seed, clock and audio give the same state on the same computer.
- **The settings.** A GPU clip is a normal `.nyxpart` file with `"backend": "gpu"`, `gpuCount` (the pool, 1,024 to 1,048,576, default 65,536) and `swirl` (-4 to 4, also works on the processor). The other fields mean the same as for a processor clip, with a rate of up to 200,000 a second and a burst of up to 100,000. Files without the new fields read as before and a program of 0.43.0 opens a GPU file as a processor clip with the rate clamped.
- **Three built-in clips.** GPU Nebula (262,144 pool, a slow swirling ring of 40,000 a second), GPU Beat Burst (131,072, 20,000 on every beat, rainbow) and GPU Rain (98,304, a line of 12,000 a second falling). They are in the Library under Generators next to the six processor clips.

### Memory and limits

| What | Limit |
| --- | --- |
| Graphics memory per particle | 64 bytes (two buffers of 32) |
| Budget for all GPU clips together | Settings, Performance: 8 to 512 MB, default 64 MB (about a million particles) |
| One pool | 1,024 to 1,048,576 particles; cut to what is left of the budget in steps of 1,024, refused below 1,024 |
| Playing places of one clip | up to 16, each with its own pool, all in the one budget |
| Particles born in one step | at most 131,072, a burst at most 100,000, the rate at most 200,000 a second |
| The budget is returned | when a pool or a clip is released (tested) |

### The fallback

The clip plays on the processor, scaled by `particles::cpuEquivalent` so that rate times lifetime fits the processor limit, when the setting is off (the default), the driver has no transform feedback, the shaders do not compile or link, the budget is used up, the buffers cannot be created or the first frames raise an OpenGL error. The reason is shown in the window (Particles tab, below the preview) and on the clip, nothing else is affected, and the clip returns to the card when the cause is gone and the clip or the setting changes. The thumbnails of the Library always use the processor version. A GPU clip on the processor looks sparser; that is the limit working.

### Limits you should know

- **Not tried on a real graphics card.** Every number below is a software renderer. The manual test M-GP-1 asks for a real card.
- **Same picture on the same computer, not on every computer.** The card was compared with a processor reference of the same algorithm and matched exactly on Mesa, which computes on the processor. `sin`, `cos` and fused multiply-add may differ in the last digits on a real card, so two graphics cards can give slightly different pictures.
- **The update costs the pool, not the particles alive.** A pool of a million slots is a million vertices a step even when most are dead. The draw cost grows with the pool and the fill rate (size and blending).
- **A small pool recycles young particles.** When rate times lifetime is more than the pool, the oldest slots are reused before the particles fade out. The window warns and counts the reused slots.
- **No exact count alive** (nothing is read back); the window shows the slots in use and the reused ones.
- **Only the first three frames of a pool are checked for OpenGL errors.**
- **Not built:** compute shaders (OpenGL 4.3, a possible later backend behind the same interface), collisions, trails, sorting for normal blending (additive blending does not depend on the order), textured or mesh particles, particles that follow the picture.

### Measurements

`NyrexVJ --gpu-particle-selftest 8 --bench-out result.json` fills a pool with the Nebula settings, times the update step alone and the draw alone into a 1920 x 1080 and a 3840 x 2160 target (RGBA16F, 15.8 MB and 63.3 MB), with `glFinish` after every pass. **These numbers are from the build machine of this release: 2 cores, Mesa llvmpipe software OpenGL 4.5, no graphics card.** A software renderer draws every sprite on the processor, so they say how the cost grows and not how fast a graphics card is. The program offers resolutions up to 2560 x 1440 for a composition, so 4K is measured on the generator alone.

| Pool (slots, alive) | State buffers | Update, one step | Draw 1080p | Draw 4K | One frame 1080p | One frame 4K |
| --- | --- | --- | --- | --- | --- | --- |
| 16,384 (16,384) | 1 MB | 0.44 ms | 22.8 ms | 41.7 ms | 23.2 ms | 42.1 ms |
| 65,536 (65,536) | 4 MB | 1.7 ms | 87.5 ms | 128.8 ms | 89.2 ms | 130.6 ms |
| 262,144 (262,144) | 16 MB | 9.9 ms | 269 ms | 448 ms | 279 ms | 458 ms |
| 1,048,576 (952,797) | 64 MB | 32.0 ms | 856 ms | 1382 ms | 888 ms | 1414 ms |

For comparison the processor system advances and collects 11,670 alive particles in 0.18 ms and then still has to upload and draw them. Growing the pool by 4 grows the update by about 4 and the draw by about 3 to 4, so the cost is linear. What a graphics card does with a million particles is the open question of M-GP-1.

### Tests

| Test | What it covers | Where it runs |
| --- | --- | --- |
| `nvj_gpuparticles_tests` (217 checks) | The hash, the memory budget and the capacity maths, the scheduler (steps, rate, level, beat bursts, ring head and wrap, clock jumps and non-finite input), the processor reference (same seed same state, other seed, level and aspect change it, finite, bounds, the speed gain of the level), the file format (round trip, old files, wild values, the rate clamp for each backend), `cpuEquivalent`, the three built-in clips, swirl on the processor | ctest, every build, no graphics card needed |
| `--gpu-particle-selftest` (30 checks) | With OpenGL: the shaders compile, the card matches the reference, the same seed gives the same bytes, another seed another picture, audio and beats change the spawn, the ring reuses the oldest slots, the budget cuts, refuses and returns, buffers are kept when a parameter changes, particles are visible and the picture repeats, alpha blending differs, no GL state is left behind, every fallback (setting off, shader fault, budget too small, a normal clip never uses the card), return to the card after a fault, and the engine switches without a restart | CI on Linux, with Xvfb and Mesa |

### Command line

| Option | What it does |
| --- | --- |
| `--gpu-particles [0\|1]` | Turn Experimental GPU particles on (without a value) or off (0) for this start; the setting is kept like any change in Settings |
| `--generator-select NAME` | With `--generators`, open the window with that built-in clip or saved file |
| `--gpu-particle-selftest [FRAMES]` | Run the checks above and exit with code 1 on a failure; FRAMES adds the benchmark, `--bench-out FILE.json` writes the JSON |

## Files

| File | Folder | Format name | Version |
| --- | --- | --- | --- |
| `.nyxtext` | `texts` | `nyrexvj-text` | 1 |
| `.nyxpart` | `particles` | `nyrexvj-particles` | 1 |
| `.nyxchain` | `chains` | `nyrexvj-chain` | 1 |

All three are plain JSON of at most 32 KiB, at most 64 files of each kind are read, and names are at most 64 bytes. A file with a newer version, another format name or a syntax error is refused with a message and the old clip keeps playing. Unknown fields are ignored, numbers outside their range are clamped. Saving goes through a temporary file and a rename.

## Errors never stop the show

- A damaged text, particle or chain file is listed with its message and does not become a clip or a change.
- A clip whose font cannot be read or whose text cannot be rasterised shows nothing and says why on the clip; the layers below keep playing.
- If the two small shader programs of text and particles do not compile (graphics driver problem), every text and particle clip shows nothing and carries that message; nothing else is affected and the programs are tried again later. `--check-shaders` reports it as a failure.
- Settings, Performance, Text and particles can hide all text and particle clips (Show text and particle clips in the library).

## Command line

| Option | What it does |
| --- | --- |
| `--generators [0\|1]` | Open the window on the Text (0) or the Particles (1) tab |
| `--chains [0..4]` | Open the Effect chains window for layer 1 to 4 (0 to 3) or the master (4) |
| `--chain-select NAME` | Select that chain in the window |
| `--generator-selftest [FRAMES]` | Run the self test below and exit with code 1 on a failure; FRAMES (default none) adds the benchmark, `--bench-out FILE.json` writes the JSON |

## Tests

| Test | What it covers | Where it runs |
| --- | --- | --- |
| `nvj_creative_tests` (380 checks) | UTF-8 decoding, layout and wrapping, kerning, atlas packing and limits, damaged and hostile fonts, text and particle files (round trip, clamping, damaged, newer version), deterministic particles (same seed same state hash, different seed different state), beat bursts, limit and drop count, chain save, load, search, export and import, effect lines shared with presets | ctest, every build, no graphics card needed |
| `--generator-selftest` (24 checks) | In the real engine with OpenGL: a text clip draws and covers a plausible share of the picture, its background is transparent, a layer below stays visible, audio changes size and particle count, the same start gives the same picture hash, burst on beat without steady rate, the particle limit holds and follows the memory estimate, the limit is clamped, a long text stays inside the atlas limit, an unknown font falls back, a damaged file shows an error and the layer below plays on, a saved change reloads, a broken shader pair does not stop the show and recovers | CI on Linux, with Xvfb and Mesa |
| `--check-shaders` | Text and particle programs and every generator clip compile | CI |
| `python3 scripts/check_i18n.py` | Every text of the windows exists in all seven languages | CI |

## Measurements

`--generator-selftest 60` times the whole composition with one blue layer, a text layer and a particle layer at four limits. These are the numbers from the build of this release on a cloud container (2 cores, Mesa llvmpipe software OpenGL, 960 x 540, no graphics card), milliseconds per frame including the processor drawing the picture:

| Case | ms per frame |
| --- | --- |
| One flat colour layer (baseline) | 7.6 |
| Baseline and a text layer | 13.9 |
| Baseline and particles, limit 512 (full) | 13.7 |
| Baseline and particles, limit 2048 (full) | 15.6 |
| Baseline and particles, limit 8192 (full) | 24.0 |
| Baseline and particles, limit 16,384 (16,336 alive) | 33.6 |

The generator buffers grew from 6.22 MB to 6.61 MB between the first and the last row, which is the 24 bytes per particle; the 6.2 MB are mostly the text atlas of the very long text the test keeps loaded. **These numbers say how the cost grows, not how fast a graphics card is.** A software renderer draws every sprite on the processor, a real card draws 16,384 small sprites in a fraction of a millisecond. Nobody has measured text or particles on a real graphics card yet; the manual test M-GN-1 asks for it. The self test is the way to repeat the measurement on your own computer: `NyrexVJ --generator-selftest 120 --bench-out result.json`.

## What is not built

- 3D geometry nodes, and the GPU particle parts listed under [GPU particles](#gpu-particles-experimental): compute shaders, collisions, trails, sorting, textured and mesh particles.
- Text shaping, right to left text, colour emoji, text along a path, outlines and GPOS kerning.
- Particles that follow the picture, collide or are drawn as meshes or textures.
- Chains in show packages and community packages, a catalogue of chains, and chain parameters that stay free for a macro.
- Real mouse and keyboard tests of the windows, and everything on a real graphics card or on Windows beyond the CI build.

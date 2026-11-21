# Render performance

NyrexVJ often runs next to VRChat on the same computer and the same graphics card. Every millisecond and every megabyte it does not use is left to VRChat. This page describes what the renderer skips, how to see where the time goes, the optional reduced resolution, and what was measured. The decision is in [ADR 0011](adr/0011-render-pipeline-optimisation.md).

## What the renderer already skipped (unchanged)

- Effects that are switched off and layers that have no active clip, are bypassed or are not soloed.
- Gated effects (React mode Gate) while their gate is closed. A closed feedback effect still copies its input into its feedback picture.
- Feedback pictures only exist while a feedback effect needs them and are freed in the same frame when it does not.
- Shader programs are compiled once, when a clip or effect is first used, and then reused.
- The picture of the cue preview is only drawn while a clip is waiting in it.

## What is new in 0.19.0

### Hidden layers are not drawn
A layer whose effective opacity is 0.1 percent or lower (the Opacity slider plus any audio modulation of the opacity) adds nothing visible to the picture. At opacity 0 every blend mode returns the picture below bit for bit, between 0 and 0.1 percent the difference is at most one step of the 8 bit colour. Its clip, its effects and its blend pass are skipped. Three rules keep everything else as it was:

- **Previews.** The layer preview in the layer strip is still updated, but at one eighth of the rate (every 8th frame, about 7.5 times a second at 60 FPS) instead of every frame. A hidden layer therefore costs about one eighth of what it cost before, and its thumbnail stays alive.
- **Feedback.** A layer with an enabled feedback effect (Trails, Hue Feedback and the like) in its chain is never skipped, because its picture builds on the previous frame and would be reset.
- **Transitions.** The outgoing deck of a transition follows the same rule, and the result of a transition is identical to before.

A layer that fades in starts drawing in the frame in which its opacity passes the threshold. Its shader is already compiled by then, so there is no pause.

### Layer resolution (Settings, Performance, Render quality)
| Setting | Layers and their effects are drawn at | Effect |
| --- | --- | --- |
| Full quality (default) | 100 % | Nothing changes. The output is identical to 0.18.0. |
| Balanced | 75 % of the width and height | About 44 % fewer pixels in clips and layer effects |
| Performance | 50 % | 75 % fewer pixels |
| Automatic | 100 % down to 50 % in steps of 12.5 % | Lowers the resolution only while the average graphics time of a frame stays above 92 % of the frame budget for 20 frames, raises it again after 180 frames below 60 % of the budget if the predicted time stays below 85 % |

The reduced picture is scaled up with linear filtering when the layer is mixed into the composition. Master effects, the colour and level stage, the transition, the preview of the cue and every output (window, Spout, NDI, DMX strip) stay at the full output resolution. The frame budget is the target frame rate of Settings, Performance, Frame budget.

What reduced resolution changes: sharp edges soften, and effects that work with the pixel grid (Pixelate, Halftone, Scanlines, CRT, Dither, Film Grain, Hex Mosaic and similar) get coarser, because they see a smaller picture. Feedback pictures are recreated when the resolution changes, so trails restart once. With a lower setting the full-size layer pictures are freed, so video memory falls too. Automatic needs the GPU timer, which exists on every OpenGL 3.3 card; without it Automatic stays at 100 %.

### Video memory
- A render target that was created for deck transitions and never used was removed (one full-size picture, 7.9 MiB at 1080p).
- The two pictures of a transition (15.8 MiB at 1080p) are freed 300 frames (5 seconds at 60 FPS) after the last transition and created again at the next one, so a show that does not use transitions does not carry them, and one that does only creates them once per use.
- Reduced layer resolution frees the full-size layer pictures (see above).
- Preparing the next clip of every layer (Settings, Performance, Video playback) keeps one extra decoder and one picture per layer: about 14.5 MB for each prepared 1080p clip. Turn it off if video memory is tight. Backwards playback of video buffers up to 16 pictures per clip (about 96 MiB at most for 1080p, see [MEDIA.md](MEDIA.md)).

### Seeing where the time goes (Settings, Performance, Where the GPU time goes)
Tick **Measure every render pass**. Every pass (clip, each effect of each layer and of the master, preview, blend, the final stage, transition) gets its own GPU timer, and the table shows the average time, the share and a warning for a single pass that takes a quarter or more of the total. The setting is not saved and is off at start. The results come a few frames late so the program never waits for them, and while it is on the total GPU time of the status bar is the sum of the passes. It costs one timer query per pass, which is small, but leave it off during a show.

Command line:
- `--profile` with `--bench-comp FRAMES`: adds the per-pass table (`passes`) to the JSON result.
- `--bench-shaders FRAMES`: measures every clip (alone on one layer) and every effect (alone on the cheapest clip, default parameters) and prints the most expensive ten of each, with `--bench-out FILE.json` the complete lists.
- `--bench-memory`: reports the size of the render targets while idle, during and after a deck transition and for each render quality.
- `--quality N`: 0 Full, 1 Balanced, 2 Performance, 3 Automatic for this run.
- `--bench-switch SECONDS:clip1,clip2,...`: switches between video clips and counts empty frames and the time to the first picture (see [MEDIA.md](MEDIA.md)); `--no-prewarm` turns the background preparation of the next clip off for the run.
- `scripts/make_bench_presets.py DIR`: writes the five scenario presets used below (`Bench Four Visible`, `Bench Three Hidden`, `Bench Hidden Feedback`, `Bench Heavy Chain`, `Bench Fade Mix`) into a presets folder, so the same files can be run with any version through `--preset`.

## What is new in 0.22.0: live sources

NDI inputs and cameras ([LIVE.md](LIVE.md), [ADR 0014](adr/0014-live-sources.md)) are built to cost nothing when unused and little when used:

- **No cost when unused.** A source starts when a layer shows its clip and is released three seconds after the last use. An unused slot has no thread, no texture, no buffer and no open device. `--live-selftest` checks that graphics memory of the slot and the thread count return to their start values.
- **Half the bytes.** Pictures travel as packed UYVY (2 bytes per pixel) and are converted on the graphics card, so the processor converts nothing and the upload is half of an RGBA upload.
- **Newest picture only.** There is no queue; a slow render drops older pictures (counted and shown), and the picture on screen is never older than the last one received.
- **No waiting on the render thread.** Capture, discovery and closing a device run on their own threads; a device that hangs is cut off by a deadline.
- **Render loop.** The only additions are a check of eight idle timestamps per frame and, for a live clip, one textured quad like the one used for video. Layers without live clips are unchanged: the default composition (`--bench-comp 25`, 960 x 540, llvmpipe, three runs each, `main` and this version alternating) took 124.6, 126.7 and 127.8 ms per frame on `main` and 120.6, 134.8 and 121.2 ms with live sources, which is no difference outside the run to run spread of about 10 percent of this software renderer.

Measured with FFmpeg's test source and software OpenGL (see [LIVE.md](LIVE.md) for the table): a 1080p60 source needs 3.1 ms of upload per picture, 12.5 ms from arrival to the end of the upload on average (95th percentile 14.4 ms), 16.6 MB of video memory and 16.5 % of one processor core including the generation of the test picture; 720p30 needs 2.3 ms, 7.4 MB and 6.9 %. No real camera, capture card, NDI runtime or graphics card was available.

Command line: `--bench-live SECONDS:camera:SOURCE` and `SECONDS:ndi:NAME` measure one source; `--live-selftest` runs the end to end check.

## What is new in 0.23.0: advanced output

The output window ([OUTPUTS.md](OUTPUTS.md), [ADR 0015](adr/0015-advanced-output.md)) is the only part of the render loop that changed. Everything new costs nothing while it is not used:

- **One default slice is unchanged.** One plain slice is drawn with blending off, without a mask texture and with the colour block switched off by one uniform. It uses 42 KB of video memory (vertex and index buffers), the same as before, and `--output-selftest` checks that the memory returns to that value after slices and masks are removed.
- **Slices cost what they cover.** Each extra slice is one more textured mesh over the output window, with alpha blending. In a software rasteriser a full-window slice costs about 5 to 7 ms, so eight full-window slices are not a use case for a processor renderer; a graphics card does the same work in a small fraction of that. A slice that is hidden or removed releases its buffers and mask.
- **Masks are built once.** A change of the polygon builds an 8 bit coverage image on the processor (2.3 ms hard at 1024 x 576, 2.1 to 2.2 ms soft at 512 x 288) and uploads it; drawing only samples it. A hard mask holds 590 KB of video memory, a soft one 147 KB. A soft mask at 1024 x 576 would take 30 ms per change, which is why it is not used.
- **Colour is a few operations in the existing shader.** Gain, saturation, contrast, gamma and black lift cost about 2.5 to 2.8 ms more per full-window frame in the software rasteriser when switched on (a default slice with saturation 0.8 and gamma 1.2: 4.2 to 4.7 ms became 7.0 to 7.2 ms; the power function for gamma is expensive on a processor), and nothing when everything is at the default.
- **Larger warp grids cost more vertices, not more passes.** The mesh follows the grid (32 divisions up to 9 x 9 points, 4 cells per grid cell above, at most 128), and buffers are rebuilt only when the grid or the mapping changes.
- **Memory.** Eight soft masked slices with 17 x 17 warp together use 2.5 MB of video memory.

Measured with `--output-selftest` (software OpenGL, llvmpipe, 1280 x 720 window, three runs in a row; earlier runs on the same computer were up to 20 percent faster): one default slice 4.2 to 4.7 ms, hard mask 6.6 to 7.1, soft mask 6.5 to 6.9, colour 7.0 to 7.2, four masked slices 21.4 to 22.5, eight soft masked 17 x 17 slices 61 to 63 ms. The slower machine of the Linux CI job gave 14.1 ms for the default slice, 16.2 for a hard mask, 13.6 with colour, 62 for four masked slices and 155 ms for eight soft masked 17 x 17 slices, and 18 ms for a soft mask built at 1024 x 576. There the colour correction is not measurable, so the 2.5 ms above is a result of one computer and not a rule. These show the relative cost of the features within one run, not what a graphics card needs. The composition renderer (`--bench-comp`) is not touched by this change, and the default output path was not compared with 0.22.0 under identical conditions.

Command line: `--output-selftest` runs the check and prints the numbers above.

## Measured

Test machine: Linux, no graphics card, Mesa llvmpipe (OpenGL 4.5 core, a software rasteriser on the processor). 960 x 540 unless stated. Medians of three runs of 30 to 40 frames each, run to run spread about 5 percent. "Before" is the build of 0.18.0 (main at 6f19641), "after" is this version with the same preset file. The numbers are frame times of the whole frame (`--bench-comp`).

| Scenario | Before | After | Change |
| --- | --- | --- | --- |
| Four Visible: four layers, each with Glow and Blur, all opaque | 132.4 ms | 130.9 ms | none (within spread) |
| Three Hidden: the same, three layers at opacity 0 | 146.2 ms | 46.8 ms | **-68 %** |
| Fade Mix: the same with opacities 1, 0.5, 0.25 and 0 | 133.5 ms | 95.6 ms | **-28 %** |
| Hidden Feedback: three hidden layers with a Trails feedback effect | 98.1 ms | 100.4 ms | none, as intended (not skipped) |
| Heavy Chain: Star Burst, Light Rays, Glow, Anamorphic Streak, master Radial Blur | 138.5 ms | 142.8 ms | none at Full quality |

| Render quality (after) | Four Visible | Heavy Chain | Render targets (540p) |
| --- | --- | --- | --- |
| Full quality | 129.8 ms | 140.6 ms | 15.5 MiB |
| Balanced (75 %) | 80.0 ms (-38 %) | 84.2 ms (-40 %) | 13.8 MiB |
| Performance (50 %) | 44.7 ms (-66 %) | 46.8 ms (-67 %) | 12.5 MiB |

Render target memory at 1080p, from `--bench-memory` on the look Bench Four Visible:

| Moment | Before | After |
| --- | --- | --- |
| Idle | 39.6 MiB | 39.6 MiB |
| During a deck transition | 63.3 MiB | 55.4 MiB |
| 5 seconds after the transition | 63.3 MiB | 39.6 MiB |
| Quality Balanced / Performance | not available | 32.6 / 27.7 MiB |

The "before" values of the transition rows are computed from the code of 0.18.0 (the unused picture, which was always allocated together with the two transition pictures and never freed), not measured with that build.

Image checks, all with the output pictures of the old and the new build compared pixel by pixel: all 26 factory looks (20 frames each), the five scenario presets and three deck transitions (Fade, Dissolve, Zoom) at three moments each are identical at Full quality. The picture of a hidden layer does not change anything, as it does not before.

All 26 factory looks together, 1080p (every look sets its own resolution), `--bench-comp 40 --bench-all`, 0.18.0 run twice and the new build once, one after the other: 7686 ms and 7388 ms for the old build (the two runs of the same build differ by 3.9 %, which is the drift of the test machine), 7339 ms for the new one. Against the second old run that is -0.7 %, and per look the ratio lies between 0.88 and 1.08 (median 1.00), the old build against itself between 0.82 and 1.05. Factory looks have all layers visible and use Full quality, so no change is the expected and the wanted result: nothing got slower.

### Where the time goes (ranking from `--bench-shaders 8`, 960 x 540, llvmpipe)

Most expensive clips (one layer alone): Mandelbox Fly 158 ms, Tesseract 79 ms, Cube Field 3D 72 ms, Crystal Lines 59 ms, Sphere Grid 3D 59 ms, Laser Web 50 ms, Terrain 3D 50 ms, Cube Tunnel 3D 49 ms. 23 of the 43 clips cost under 6 ms.
Most expensive effects (one effect alone): Image Terrain 72 ms, Star Burst 70 ms, Pixel Smear 32 ms, Shatter 31 ms, Hyperspace 24 ms, Anamorphic Streak 19 ms, Light Rays 18 ms, Glow 14 ms. 48 of the 76 effects cost between 2 and 5 ms and 13 cost more than 10 ms.

These are times of a processor rasteriser. The order is a useful hint (the raymarched 3D clips and the effects with many texture reads at the top are the expensive kind on every graphics card), the ratios on a real graphics card will differ. Run `--bench-shaders 30 --bench-out shaders.json` on the target computer for the real ranking.

## Evaluated and not changed

- **Constant tables in the multi-tap effects.** Glow, Blur, Anamorphic Streak and Star Burst compute cosines, sines, roots and exponentials from the loop index in every pixel. Replacing them by constant arrays changed the time by -5 to +4 percent (three runs each, spread about 4 percent): the shader compiler already folds them. Reverted, the shaders are as in 0.18.0.
- **The big costs are the number of texture reads.** Star Burst reads the picture up to 113 times per pixel, Light Rays 29 times, Glow 25 times. Fewer reads change the picture, so the only lever that keeps the effect itself unchanged is fewer pixels (render quality).
- **Reducing single effects only.** Drawing one effect at lower resolution needs an extra reduce and an extra enlarge pass around it, which costs memory traffic and one more picture. The layer-wide setting avoids that.
- **Skipping the final stage, ping-pong feedback pictures, uniform caches.** The final stage is where colour, level and blackout happen and every output reads its result. A ping-pong feedback target needs a second full-size picture per feedback effect, which is more memory than the copy it saves. Every pass already sets only its program, one or two textures and a few uniforms. None of the three was measured as a problem and each adds risk, so they stay as they are.
- **Allocating the second layer and composition pictures on demand.** Every factory look uses effects, so the saving only exists in compositions without any effect (8.3 MiB per picture at 1080p).
- **Blackout skipping.** Skipping all drawing while blackout is on would freeze feedback trails and previews for a state that normally lasts seconds.

## Limits of these numbers

- No real graphics card, no VR, no VRChat: everything above was measured on a software rasteriser. The saving of skipped work (hidden layers, fewer pixels, freed pictures) carries over, the milliseconds do not.
- On llvmpipe the time of small passes (blend, final stage, thumbnails) is not attributed reliably to the pass, because the rasteriser works asynchronously; only the sum is. The per-pass numbers of effects and clips are reliable.
- The Windows build was not run, only compiled and tested by the build service. The reduced resolution, the timers and the freeing of pictures use plain OpenGL 3.3 calls.
- Automatic was tried through the log of a run (100, 88, 75, 63 and 50 percent in sequence under constant overload); how it behaves on a real show with changing load is open.
- Please run `NyrexVJ --bench-shaders 30 --bench-out shaders.json` and `NyrexVJ --preset "Bench Heavy Chain" --bench-comp 300 --profile --bench-out heavy.json` (after `scripts/make_bench_presets.py`) on a real graphics card, with VRChat running, and share the files.

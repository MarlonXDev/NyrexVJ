# Render performance

NyrexVJ often runs next to VRChat on the same computer and the same graphics card. Every millisecond and every megabyte it does not use is left to VRChat. This page describes what the renderer skips, how to see where the time goes, the optional reduced resolution, and what was measured. The decision is in [ADR 0011](adr/0011-render-pipeline-optimisation.md).

## What the renderer already skipped (unchanged)

- Effects that are switched off and layers that have no active clip, are bypassed or are not soloed.
- Gated effects (React mode Gate) while their gate is closed. A closed feedback effect still copies its input into its feedback picture.
- Feedback pictures only exist while a feedback effect needs them. Until 0.65.0 they were freed in the same frame when it did not; since 0.66.0 a released one waits up to five seconds in a small pool for reuse (see below).
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
- `--soak SECONDS[,FPS]`: a real-time run that changes looks, decks and quality on a timer and samples frame time, CPU, memory, graphics estimate, threads and handles; it reports growth over the run. See [TESTING.md](TESTING.md). The outside view (other processes, the graphics card) is `scripts/measure_live.py`.
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

## What is new in 0.30.0: advanced output 2.0

Everything new costs nothing while it is not used ([ADR 0023](adr/0023-advanced-output-2.md), [OUTPUTS.md](OUTPUTS.md)). Measured with `--output-selftest` on the same software renderer (llvmpipe, 1280 x 720 window, three runs in a row, spread up to 25 percent):

- **Colour per slice** is a second pass of the same few operations in the shader when it is not at its default: slice and output colour together 10.3 to 12.0 ms per frame, against 7.5 to 9.2 ms with the output colour alone. At default it costs one uniform check.
- **Curved masks** cost the same per frame as polygon masks (7.3 to 8.6 ms against 7.4 to 8.3), because the texture is built once when the mask changes. Building a 32 point curved mask takes 7.0 to 7.2 ms at 1024 x 576 (a polygon of 4 points 2.2 to 2.9 ms, the work grows with the number of edges crossing each row).
- **Picture masks** are decoded on a thread of their own. Turning a 1024 x 576 picture into a mask texture takes 2.2 to 3.1 ms hard and 3.0 to 3.6 ms soft at 512 x 288; the decoder reduces any picture to 1024 x 576 once and keeps that copy (590 KB of normal memory per picture, in addition to the texture).
- **NDI of an output** reads the back buffer into two pixel buffers only while a receiver is connected: 7.3 to 9.6 ms per frame with one receiver (including the flip and the copy into the sender) against 4.5 to 6.0 ms without NDI, and no measurable cost with NDI on and no receiver. The readback needs `2 x width x height x 4` bytes of video memory (7.4 MB at 1280 x 720, 66 MB at 3840 x 2160) and counts against the budget.
- **Output budget.** All of it, meshes, masks, delay ring and NDI readback, is estimated by `estimateOutputBudget` and measured by `OutputManager::gpuBytes`; the self test checks that both give the same number (2,506,880 B for eight soft masked 17 x 17 slices, 42,000 B for one plain slice). The limit is 384 MiB and only the NDI readback is refused when it does not fit; four 4K outputs with NDI and a delay of 8 frames would need about 540 MiB (a delay ring of 285 MiB and 253 MiB of readback buffers), one 1080p output with NDI and a delay of 2 needs 23.7 MiB for the delay ring and 15.8 MiB for the readback.

## What is new in 0.24.0: show sync

The sync system ([SYNC.md](SYNC.md), [ADR 0016](adr/0016-show-sync.md)) is built so that a show that uses none of it pays almost nothing, and one that uses it pays in small steps:

- **No cost when unused.** Every source is off by default. The timecode audio input is opened only while an input device is chosen, the Pro DJ Link socket (UDP 50001) only while Listen for Pro DJ Link beats is on, and both are closed again when switched off. MIDI clock uses the MIDI input of the Control page and adds no device of its own. An unused source has no socket, no audio device and no buffer.
- **One small update a frame.** The beat clock takes the observations that arrived since the last frame, merges them and advances the beat. Measured with a micro benchmark over two million frames (optimised build, one core): 0.04 microseconds a frame while no source delivers anything, 0.08 microseconds with eight MIDI clock ticks arriving every frame. That is well below a hundred-thousandth of a frame at 60 Hz.
- **Time stamps and threads.** MIDI clock ticks are time stamped in the MIDI input thread, so the frame rate adds no jitter to them. Pro DJ Link needs no thread: a non-blocking socket is read once a frame (at most 256 datagrams), and a beat is stamped when it is read, so its time is accurate to one frame. A decoded timecode frame is placed by the number of samples that followed it in the same block, not by the time of the read.
- **Timecode decoding is a small loop over the new samples.** The audio input collects samples in its own capture thread; the sync update drains them once a frame and decodes them in the same call, so the render thread does the decoding. One block of 800 samples (one frame at 60 Hz and 48 kHz) takes 4 microseconds, which is 0.25 ms of processor time for each second of audio (a 25 fps signal, 600 s measured twice, optimised build). The audio input runs only while a timecode device is chosen.
- **Cues are scanned, not indexed.** With no cue list, nothing runs. With a list (at most 512 cues, sorted when it changes) a frame compares the time window since the last frame with every cue, at most 512 comparisons, and only while a timecode source is valid.
- **Output frame lock.** With frame lock on and two or more outputs open, the outputs are drawn first and swapped together afterwards. Swapping one by one cost 4.5 to 5.1 ms of CPU time between the first and the last swap of two 1280 x 720 windows, with the frame lock it is 0.34 to 0.35 ms (software OpenGL, three runs). With a single output, or with the lock off, the path is as before.
- **Output delay costs memory only while used.** The delay ring is shared by all outputs and exists only while at least one open output has a delay above 0. At 1920 x 1080 with a delay of 2 it holds three pictures, 23.7 MiB of video memory, and costs 0.57 to 0.62 ms a frame for the copy (software OpenGL). With no delay set, nothing is allocated and nothing is copied.
- **Render loop.** Layers, effects and compositing are not touched. The default composition is unchanged.

Not measured: the cost of the sync system while VRChat runs, any real timecode source, DJ player or projector, and the swap skew on real graphics cards.

Command line: `--output-selftest` checks the frame lock, the delay and the sync bars and prints the numbers above for the output side.

## What is new in 0.25.0: node effects

A graph of nodes is compiled into one fragment shader ([NODES.md](NODES.md), [ADR 0017](adr/0017-node-effect-editor.md)) and runs like a hand written effect: one draw for the effect, the same header and footer, the same four parameters. A show that uses no node effect pays nothing for them.

- **No cost when unused.** With no `.nyxfx` file there is no node effect in the library and no program. The folder is scanned at start and when the editor saves or imports, at most 64 files, and a file is read only when its size or time stamp changed. The editor itself draws only while its window is open.
- **Compiling the text is cheap.** Turning a graph into GLSL took 8 to 24 microseconds for the four example graphs (7 to 13 nodes, medians of three runs). The driver then compiles the shader once; a graph that gives the same text reuses the cached source. Moving nodes, renaming and unused nodes change nothing in the text and cause no rebuild; typing a number into an unlinked input does, so values that move belong in Control, Macro or LFO nodes, which are uniforms.
- **Same speed as written by hand.** Four example graphs were run against hand written GLSL of the same effect (`--bench-nodes`, Mesa llvmpipe, 1920 x 1080, clip Plasma, effect time only). The ratio graph divided by hand was 0.92 to 1.17 in a run of 30 frames, 0.96 to 1.04 in a run of 60 and 0.91 to 1.07 in a run of 5, with no pair always on one side. The largest value, 1.17 for Mirror Swirl in the run of 30 frames, was 0.99 in the run of 60 and 0.96 in the run of 5, so the runs differ more from each other than graph and hand written effect differ (a short run on a software rasteriser is noisy, which is why three runs are given). The three pairs without feedback produce exactly the same picture (largest pixel difference 0); the Trails pair reads the previous picture and was timed, not compared pixel by pixel. Effect times were 12 to 21 ms for the four effects on this machine.
- **Picture reads are limited.** A graph may read the incoming picture up to 64 times a pixel. Blur counts its samples (2 to 16) and Edges counts 4. The status line of the editor shows the number. The limit protects against a graph that makes one frame take seconds; it does not make a graph cheap on a slow card.
- **The previous picture costs memory only when used.** A graph with a Previous Picture node needs a second picture buffer for that effect, the same as the built-in feedback effects. No other node allocates anything.
- **Modulation costs nothing extra.** The LFO is computed in the shader from the beat uniform, the Macro node reads a uniform array that is declared only when a Macro node is used, and Control nodes are the effect parameters that every effect has.
- **Render loop.** Layers, effects and compositing are not touched. The default composition is unchanged.

Not measured: node effects next to VRChat, on a real graphics card, the driver compile time of a large graph (96 nodes) and of any graph on Windows.

Command line: `--bench-nodes FRAMES` runs the comparison and prints the numbers (`--bench-out` writes JSON); `--check-shaders` compiles 129 node graphs with the driver as well.

## What is new in 0.26.0: plugins

Plugins ([PLUGINS.md](PLUGINS.md), [ADR 0018](adr/0018-plugin-system.md)) are native code that gives modulation values or pictures. A show that uses no plugin pays nothing for them.

- **No cost when unused.** Plugins are off until the person enables one. A plugin that is not enabled is never opened, no thread exists for it, and no code of it runs. The folder is scanned at start and on request (at most 16 manifests of at most 16 KiB).
- **The render thread never runs plugin code.** Per frame it publishes a snapshot of audio and beat (a copy under a mutex), reads one atomic value for each plugin band that is used, takes the last picture of an image source the way it does for a camera, and runs a check of the clocks. A program that calls exactly this 200,000 times (not part of the repository, Linux, release build, five runs) needed 0.05 to 0.06 microseconds a frame with plugins off, 0.07 to 0.08 with all 8 slots read, 0.06 to 0.08 with plugins allowed and nothing enabled, and 0.16 to 0.21 with the example plugin running in two slots.
- **A plugin that hangs does not stop the picture.** Every call has a time budget (modulation 20 ms, image 100 ms, create and destroy 500 ms, loading 1 s). A call that is over its budget is abandoned and the plugin is switched off for the session. The tests measure the longest `update()` plus `modulation()` call on the calling thread during a hang and during a slow load, and it stayed below 100 ms.
- **Modulation sources run only when read.** A modulation plugin runs at about 60 calls a second and stops 3 seconds after the last read. The example plugin took 0.013 to 0.05 ms a call, so about 0.1 to 0.3 % of one core.
- **Image sources are 1280 x 720 at 30 frames a second.** A running image source holds about four to five frame buffers of 3.5 MiB in system memory and one texture of the same size on the graphics card, and goes through the existing live path (upload on the graphics card, release after the idle time). The example plasma runs at the full 30 frames a second (30.5 reported by the runner under software OpenGL).
- **Nothing else changes.** Layers, effects, compositing and the default composition are not touched.

Not measured: a plugin that does real work, plugin sources next to VRChat, timings on Windows (the plugin tests pass there), and the processor time of the example image source on its own.

Command line: `--plugin-selftest FOLDER` loads a plugin through the real engine and checks both source kinds (`--bench-out` writes JSON); `--no-plugins` switches plugins off for a run.

## What is new in 0.29.0: Shader 2.0

Shader clips with a `#pragma nyrexvj` line can write alpha and use Buffer A to D ([SHADERS.md](SHADERS.md), [ADR 0022](adr/0022-glsl-shader-2.md)). A shader file without such a line is compiled exactly as before and costs nothing extra.

- **A buffer is one more full-screen pass and 16 bytes per pixel.** Each buffer is a pair of 16 bit floating point pictures (current and previous). At 1920 x 1080 that is about 33 MB for one buffer and 133 MB for four, for every layer that shows such a shader. All shader buffers together stay inside 512 MiB; a buffer that does not fit runs at a smaller scale (down to a quarter) before it is refused.
- **Buffers exist only while they are used.** They are freed after 240 frames without drawing (about four seconds at 60 fps), on a reload and when the picture size changes, so a deck that is not shown does not keep video memory. The old deck keeps its buffers while a transition fades it out.
- **Alpha costs nothing measurable.** The same single pass shader took 12.3 ms per frame without and 11.8 ms with `alpha` (software renderer, 1280 x 720, 60 frames, so the difference is noise).
- **Measured** with `NyrexVJ --shader-selftest 60` on the Mesa software renderer (llvmpipe) at 1280 x 720, 60 frames each: a plain shader 12.3 ms (worst 13.3), an alpha shader 11.8 ms (14.1), two buffers 27.3 ms (37.1) with 29.5 MB of buffers, four buffers 49.3 ms (79.4) with 59.0 MB. These numbers show how the cost grows with every pass, they do not say how fast a real graphics card is. The test shaders are simple, a real feedback shader with more maths per pass costs more.
- **Not measured:** a real graphics card, 1920 x 1080 or 4K, a video as an input, and four layers with buffers at once.

Command line: `--shader-selftest [FRAMES]` runs the checks and the timing (`--bench-out` writes JSON).

## What is new in 0.31.0: video pipeline audit

[ADR 0024](adr/0024-video-pipeline-audit-and-measurement.md). The routes of the video pipeline were not changed, so no video got cheaper or more expensive; what is new is what can be seen and how it was checked.

- **First picture.** The time from opening a decoder to its first picture (opening the file, finding the first key frame, decoding it) is shown in the video tooltips and written as `firstPictureMs` by `--bench-video`.
- **Upload spread.** The upload time has a 95th percentile and a largest value over the last 120 uploads (tooltip and `uploadP95Ms`, `uploadMaxMs` in the JSON), because a mean of 3 ms can hide one upload of 30 ms.
- **HDR.** PQ and HLG files are marked in the monitor and tooltips. They are not converted.

Measured with `scripts/bench_video.py --seconds 6` (Linux, 2 cores, FFmpeg 6.1, Mesa llvmpipe, 1920 x 1080 output, one run per scenario, **software renderer only, no hardware decoder**):

| Scenario | Decoder | First picture | Upload mean | Upload p95 | Upload max |
| --- | --- | ---: | ---: | ---: | ---: |
| 1x H.264 1080p | h264 (processor) | 35.9 ms | 3.2 ms | 8.9 ms | 32.8 ms |
| 4x H.264 1080p | h264 (processor) | 105.7 ms | 8.4 ms | 39.3 ms | 58.6 ms |
| 1x H.264 4K | h264 (processor) | 94.4 ms | 72.3 ms | 156.6 ms | 167.7 ms |
| 1x HEVC 1080p (10 bit) | hevc (processor) | 65.6 ms | 4.8 ms | 8.1 ms | 32.0 ms |
| 1x AV1 1080p | libdav1d (processor) | 12.5 ms | 4.1 ms | 7.1 ms | 29.5 ms |
| 1x HAP 1080p | HAP direct upload | 1.7 ms | 1.1 ms | 1.4 ms | 1.9 ms |
| 1x HAP Alpha 1080p | HAP direct upload | 2.4 ms | 1.4 ms | 2.6 ms | 4.3 ms |
| 1x HAP Q 1080p | HAP direct upload | 12.8 ms | 1.5 ms | 2.5 ms | 2.9 ms |
| 1x HAP 4K | HAP direct upload | 6.1 ms | 2.9 ms | 4.4 ms | 5.3 ms |
| 4x HAP 1080p | HAP direct upload | 6.8 ms | 1.1 ms | 2.4 ms | 8.9 ms |
| 2x H.264 1080p + 1x H.264 4K | h264 (processor) | 61.4 ms | 11.5 ms | 49.1 ms | 150.1 ms |

The upload times of the decoded video are the time a software rasteriser needs to take the planes, so they say how the figures behave, not what a graphics card does: they grow with the picture size and with the number of videos, the HAP uploads are an order of magnitude smaller than the plane uploads, and the largest value of the plane uploads is several times the mean. Several videos open at the same time slow each other down on two cores, which is why the first picture of the four H.264 videos is later than that of one.

Does the new measuring cost anything? The old build (without these changes) and the new one were measured alternately, three runs each, the processor time of NyrexVJ without the software rasteriser in percent of one core:

| Scenario | Before | After |
| --- | --- | --- |
| 1x HAP 1080p | 4.0 to 4.2 | 4.1 to 4.4 |
| 1x H.264 1080p | 15.5 to 16.5 | 15.9 to 16.8 |
| 1x HEVC 1080p | 30.3 to 30.9 | 30.4 to 31.0 |

No difference outside the spread. Numbers of different sessions on this machine can not be compared: a first set of the same scenarios on the old build gave 2.7, 13.8 and 37.2, the figures of 0.18.0 in [MEDIA.md](MEDIA.md) 5.7, 16.3 and 24.9, and a single run of the full set of scenarios showed 27.7 MB of video memory for HAP 4K once (7.9 MB alone, three times), probably a thumbnail that was being decoded at that moment. Only alternating runs of two builds are a comparison.

Not measured, because it needs hardware that was not available: hardware decoding (decode time, copy back, upload, processor and graphics card load), 4K at 60 frames a second on a real graphics card, the cost next to VRChat and in VR, and the video memory the driver adds. The checklist for a computer with a real graphics card is in [MEDIA.md](MEDIA.md), section Zero copy.

## What is new in 0.32.0: 3D Engine 2.0

[ADR 0025](adr/0025-3d-engine-2.md). Skinning and morph targets are done by the graphics card, so the processor only evaluates the joint matrices of the figures once per frame. Opaque models and the built-in scenes take the same path as before.

- **Old scenes are not slower.** Alternating old and new program, three runs each, `--bench-scene 2:150`, 1920 x 1080, Mesa software renderer: Torus Knot Mesh 142.4 to 149.4 ms per frame (graphics card time) in the old program and 144.3 to 152.1 ms in the new one, Chrome Orbs Mesh 214.2 to 224.1 and 216.9 to 224.4 ms. The differences are inside the spread of the runs. All seven built-in scenes also give the same pixels in both programs (21 pictures compared).
- **Cost of the new features,** one layer at 1920 x 1080, a tube of 64,000 vertices and 127,488 triangles, `--bench-scene 1:60:"Model: bench_..."`, three runs each (graphics card time per frame, software renderer): plain 164.1 to 169.5 ms, skinned with two joints 174.1 to 175.1 ms (about 5 percent more), eight morph targets 180.2 to 186.1 ms (about 9 percent), skinned and morphed 188.9 to 196.9 ms (about 14 percent), blended 167.7 to 169.4 ms (the same as plain: one extra sorted pass of one mesh). The models are made by `scripts/make_bench_models.py`.
- **Memory.** Geometry in video memory for that tube: 6.4 MB plain, 7.9 MB skinned (24 bytes per vertex), 18.7 MB with eight morph targets (24 bytes per vertex and target), 20.2 MB with both. The joint texture is 64 KiB for all figures of a frame (1024 joints at most) and is allocated once with the renderer. Morph data lives per mesh, not per layer: layers that play the same model share it.
- **Limits that protect frame times:** 128 joints per skin, 1024 joints per frame, 32 morph targets per mesh with the eight strongest weights applied per draw, 640 MiB of video memory per scene. 576 figures of a two-joint model draw 512 of them and report the 64 that were skipped.
- **Program size.** The stripped Linux Release program grew from 8,176,272 to 8,340,120 bytes (+160 KiB, with Meshopt), and a build with `-DNYREXVJ_DRACO=ON` is 8,704,664 bytes.
- **Not measured:** a real graphics card (the numbers above show how the cost grows, not how fast a card is, since a software renderer is limited by the processor), 4K, several skinned figures at once on a card, anything on Windows or next to VRChat.

Command line: `--scene-selftest FOLDER` draws the test models through the real renderer and checks the pixels (`--bench-out` writes JSON with `frameMs` per model for a 256 x 256 picture including the read back, which is only a smoke test of the speed).

## What is new in 0.34.0: long run tests

[ADR 0027](adr/0027-live-stability-and-hardware-test-matrix.md), [TESTING.md](TESTING.md). `--soak SECONDS[,FPS]` runs the program like a show for a given time and samples it; `scripts/measure_live.py` samples it from outside, together with other processes and the graphics card. The program itself got nothing new at run time: the option exists only when it is given, and nothing is sent anywhere.

- **Run:** `--soak 300` on the build server (Linux, 2 processor cores, Mesa llvmpipe, 1920 x 1080), 300.5 seconds, 940 frames, 14 look changes, 42 deck switches and 27 quality changes, no OpenGL error. Between the first and the last third of the run after the first quarter: memory of the process 302.2 to 321.6 MiB (+19.5, limit 64), the program's graphics estimate 54.7 to 53.0 MiB (-1.7, limit 16), threads 7 to 7, handles 9 to 9. No growth above the limits was found. `measure_live.py` ran next to it and saw 299 to 322 MiB (+22.7) and 196 percent of one core on average (two cores, the software renderer's threads included).
- **What this does not show.** A software renderer needs about 320 ms for a frame at 1920 x 1080 here (95th percentile 696 ms), so every one of the 940 frames was over the 16.7 ms budget and the frame time numbers say nothing about a real graphics card. The memory rose by about 20 MiB in five minutes: that is within what caches filling look like, and five minutes cannot tell it from a slow leak. A three hour run on real hardware is the open manual test `M-SOAK-1`.
- **Not measured:** a real graphics card, the memory of the graphics driver (the soak run reports the program's own estimate), latency, VRChat running next to the program, and everything on Windows.

## What is new in 0.35.0: recording

[ADR 0028](adr/0028-live-recording.md), [RECORDING.md](RECORDING.md). On the render thread a recording costs one scaled copy on the graphics card (`glBlitFramebuffer`) and the start of an asynchronous read back (`glReadPixels` into one of three pixel buffers, with a fence that is polled without waiting). The colour conversion, the encoding and the writing run on one thread at background priority. A recording that does not run costs nothing.

- **Measured (`--bench-record all:90`)**, Linux, 2 processor cores, Mesa llvmpipe, 1920 x 1080, look "Neon Club", 90 frames per format, 60 frames per second grid, quality 70. The frame time without recording is 173.8 ms on average. With recording: H.264 199.9 ms (+26 ms, no frame dropped), Motion JPEG 193.3 ms, HAP 202.9 ms, HAP Alpha 209.7 ms, HAP Q 207.2 ms, VP9 220.2 ms, FFV1 231.9 ms, ProRes 4444 252.4 ms (34 of 90 frames dropped) and ProRes 422 HQ 262.0 ms (13 of 90 dropped). The full table is in [RECORDING.md](RECORDING.md).
- **What these numbers are.** The graphics work of the copy is done by the processor in a software renderer, on the same two cores as the encoder thread, so the capture call takes about 20 to 34 ms here. On a graphics card the call only queues work. These numbers show that every format runs to the end, which formats the encoder cannot keep up with (the pool fills up and frames are dropped and counted) and that the render loop never waits; they do not show the cost on a real graphics card.
- **Independent of the graphics card (`--record-selftest`):** against an encoder that needs 250 ms per frame, 90 frames pass the render loop in about 7 ms in total, the slowest capture call is below 1 ms, and about 80 frames are dropped and counted.
- **Not measured:** a real graphics card (the cost of the copy, the fences and the read back with real drivers), the hardware encoders, VRChat running next to the program, a recording at 4K, and everything on Windows.

## What is new in 0.43.0: text and particles

Text and particle clips ([GENERATORS.md](GENERATORS.md), [ADR 0036](adr/0036-text-particles-chains.md)) are built so that a clip that is not on a layer costs nothing and one that is costs a bounded amount.

- **No cost when unused.** A text atlas is baked when its clip is first needed, on a worker thread, and freed two minutes after the last use. A particle clip holds its buffer only once it has been drawn. With no text or particle clip in the show, nothing of this exists.
- **Bounded cost when used.** A text is at most one atlas of 4 MiB and one draw call. Particles are simulated on the processor in at most 8 steps of 1/60 s a frame and drawn in one instanced call, and the number alive is capped by Settings, Performance, Text and particles, Particle limit per layer (64 to 16,384, default 2048, 24 bytes each). A step creates at most 256 particles and a burst at most 500.
- **Memory is shown.** The Memory block of the performance page lists Text and particles next to render targets and thumbnails.
- **How to measure it.** `NyrexVJ --generator-selftest 120 --bench-out result.json` times a flat layer, a text layer and particles at 512, 2048, 8192 and 16,384 and writes the milliseconds per frame, the number alive and the graphics memory. On the build machine of this release (Mesa llvmpipe, 960 x 540, no graphics card) the growth was 7.6 ms for the flat layer, 13.9 with text, 13.7 with 512 particles, 15.6 with 2048, 24.0 with 8192 and 33.6 with 16,384. A software renderer draws every sprite on the processor, so these numbers show how the cost grows with the limit and not how a graphics card behaves. A measurement on a real card is an open manual test (M-GN-1).

## What is new in 0.44.0: GPU particles

GPU particles ([GENERATORS.md](GENERATORS.md), [ADR 0037](adr/0037-gpu-particles.md)) move the simulation of a particle clip onto the graphics card. They are off by default, and when they are off nothing of this exists.

- **Bounded memory.** 64 bytes per particle. One budget for all GPU clips together (Settings, Performance, Graphics memory for GPU particles, 8 to 512 MB, default 64). A pool that does not fit is cut, and one that cannot be made at all runs on the processor. The budget is returned when a pool or clip is released, and the memory page of Settings counts the buffers under Text and particles.
- **Bounded work.** One update pass and one instanced draw per place, at most 8 steps of 1/60 s a frame, at most 131,072 births in a step. The update costs the slots in use, not the particles alive.
- **Measured here.** `NyrexVJ --gpu-particle-selftest 8 --bench-out result.json` gave, on a software renderer with no graphics card, an update of 0.44 ms for 16,384 slots up to 32 ms for 1,048,576, and a draw of 23 ms up to 856 ms at 1920 x 1080 and 42 ms up to 1382 ms at 3840 x 2160. The cost is linear in the pool. A software renderer draws every sprite on the processor, so these numbers do not predict a graphics card. A measurement on a real card is an open manual test (M-GP-1).

## What is new in 0.45.0: graphics memory, diagnostics and warnings

Graphics memory diagnostics ([ADR 0038](adr/0038-gpu-diagnostics.md)) extend the Performance page; there is no second profiler. Everything is passive until a threshold is crossed, and the memory budget is **off by default**.

- **Two kinds of numbers, kept apart.** Measured by the graphics driver: total, free and evicted video memory for the whole card, from `GL_NVX_gpu_memory_info` or `GL_ATI_meminfo`, read once a second and shown only if the driver gives usable values. Estimated by NyrexVJ: what it allocated (render targets, thumbnails, images with their mip levels, videos, 3D scenes, effect buffers, text and particles), split into what cannot be released now and what belongs to clips that are not playing. The budget is compared with the estimate only.
- **API limits.** OpenGL has no portable memory query. The two extensions exist on NVIDIA drivers, on AMD drivers and in Mesa, return kilobytes in a `GLint`, and say nothing about which program uses what. A software renderer exposes them with meaningless values (a wrapped negative number was seen on the build machine) and the program rejects them. No DXGI or Vulkan query is built because it could not be run here; it is the proposed way to get the operating system's budget for this process on Windows.
- **Frame time.** The Measured table gains the slowest 1 %. Below the memory block, a 60 second window gives P95 and P99 (histogram buckets of 0.25 ms up to 64 ms, rounded up to a bucket edge, shown after 120 frames), the late frames (more than 1.5 times the budget) and the dropped frames (estimated from the time between frames, since OpenGL gives no present statistics; pauses of two seconds or more are not counted).
- **Upload traffic.** The bytes of every video and live source frame handed to the driver are counted exactly (without row padding): bytes per second, the highest second of the last ten and the total. Images and models load once and are not counted.
- **Warnings.** Slow frames (P99 above 150 % of the budget, critical at 200 %), dropped frames (5 a minute), memory near (85 %) and over the budget, resident memory over the budget, little free driver memory (under 10 % or 256 MiB), driver evictions (any in 30 s, critical from 20) and very high upload (1.5 GB/s). They rise after 2 s and clear after 6 s. The first, second and fourth limits are settings.
- **Budget and release.** With a budget (256 MB to 64 GB) the estimate is checked every 0.25 s. Over it, loaded clips that are not playing and were idle for 5 s are released, prepared videos first, then images, then 3D scenes, longest idle and biggest first, at most 8 per pass, only those of 1 MiB or more, until 85 % of the budget is reached. A playing cell is never released. In this mode only the playing and the next cell of a layer are loaded, so a clip that was not used for a few seconds really is idle. A released image loads again when launched.
- **Pressure steps.** 3 s over the budget raises the step, 12 s under 75 % lowers it: 1 stops preparing the next clips, 2 releases after 1 s instead of 5 s, 3 draws layers at 75 % and 4 at 50 % (the last two only with the extra setting). While a step is raised, idle clips are also released down to 70 % of the budget so that it can come back down.
- **Measured here.** `NyrexVJ --gpu-diag-selftest 5 --bench-out result.json` on a software renderer (Mesa llvmpipe, 1920 x 1080, eight images of 3584 x 3584 pixels of 65 MiB each): the estimate was 570 MiB with every image loaded and 178 MiB after the release against a budget of 256 MiB (6 images released in 6.6 s, the playing and the next image kept), a released image was ready again 5 frames (253 ms) after its launch, four playing images of 300 MiB resident reached step 4 after 22 frames of half a second and layers at 50 %, and after the playing images were removed it came back to step 0 after 47 frames of one second. A frame took 53.7 ms with the budget off (8 images loaded) and 53.3 ms with it on (2 loaded), five frames each, which is within the noise of a software renderer: no speed up is claimed, and the budget is for keeping memory inside a number, not for speed. Real graphics cards are an open manual test (M-GD-1 to M-GD-3).

## What is new in 0.46.0: the shader editor and its preview

The Shader editor window ([ADR 0039](adr/0039-shader-playground.md)) draws a small live preview of the shader being edited. The engine holds nothing for it while the window is closed or Live preview is off.

- **Where the cost goes.** One extra pass per drawn frame at 480 x 270 into its own target, drawn after the main frame's GPU timer ended, so it never counts in the frame time of the Performance page. Its buffers (a shader with `buffer` lines) count in the shader memory and in the 512 MiB budget of Shader 2.0.
- **A cost control.** The preview measures its own GPU time without waiting for the result. A new working version is drawn one frame at a time until three results are in. An average over 20 ms draws only every n-th frame (up to every 15th), an average over 120 ms or one frame over 360 ms stops it until Resume or a new working version. Results of the previous version are dropped after a swap.
- **Compiles.** Typing never compiles: 0.6 s of quiet (at most 2.5 s while typing goes on, 0.35 s between compiles). With `GL_ARB_parallel_shader_compile` or `GL_KHR_parallel_shader_compile` the driver compiles and links in its own threads and the engine checks once per frame without waiting (20 s limit). Without it the driver compiles during the call and the window says so.
- **Measured here.** `NyrexVJ --shader-editor-selftest 60 --bench-out result.json` on a software renderer (Mesa llvmpipe, 960 x 540, two runs): a frame without the playground 2.9 and 3.1 ms, with a light preview 3.1 and 3.0 ms (the wall clock of the whole frame, noise of about 0.2 ms), the GPU time of a two pass preview 1.7 and 1.8 ms per drawn frame, the median compile 0.6 and 0.4 ms and the slowest 1.5 and 0.4 ms. A deliberately heavy shader took 184 and 188 ms per frame for the three frames it was allowed to draw, and after the cost control paused it the frame was back to 2.9 and 2.8 ms. Mesa has a shader cache and the test shaders are tiny, so the compile times say nothing about a real driver, and the numbers of a software renderer do not predict a graphics card.
- **Not measured.** A real graphics card, the compile time of a large shader on NVIDIA, AMD or Intel drivers, the stall of a driver that compiles on the spot, an editor open during a long show.

## What is new in 0.47.0: video routing

Route clips and sender sources ([ADR 0040](adr/0040-video-routing.md)) copy pictures on the card. Nothing is allocated, copied or drawn without a route clip or a chosen sender source, and `--routing-selftest` checks that.

- **Where the cost goes.** One `glBlitFramebuffer` into a copy for every source that is used (the layer after its effect chain, the mix after a layer, the master after its chain), at the size and format of that picture (RGBA8, or RGBA16F for a float chain); one more blit for every layer that plays a route clip; for every sender that runs with a source other than Master, one final pass at the output size into its own target. A source layer that is faded out to opacity 0 is drawn while something uses it. While the Video routing window is open, every source also gets one small downsample (160 x 90) every second frame.
- **Memory.** A copy is width x height x 4 bytes (8 for a float chain), a feedback source holds two, a sender copy is one output picture. Copies are released 90 frames after the last use and when the resolution changes, the sum is limited to 512 MiB (over it a source is not copied and the log says so once), and it counts in the memory estimate of Settings, Performance.
- **Measured here.** `NyrexVJ --routing-selftest 200 --bench-out result.json` on a software renderer (Mesa llvmpipe, 960 x 540, two cores, three runs, wall clock of the whole frame, four ramp shader layers): 24.3 to 25.5 ms per frame. A layer that plays a route clip instead of the fourth shader layer: 24.2 to 24.8 ms, which is -1.3, +0.5 and 0.0 ms against the shader layer (inside the noise). One sender with Layer 1: +2.4, +5.6 and +2.8 ms. Two senders with a group (Layers 1-3 and 1-2): +7.5, +2.1 and +6.5 ms. One routed source costs one picture of memory, 2,073,600 bytes at 960 x 540. The spread from run to run is several milliseconds on this shared container, and a software renderer pays for every full-size pass in a way that a graphics card does not, so no number here predicts a graphics card.
- **Not measured.** A real graphics card, 1080p and 4K, a show with four routed layers for hours, a recording of a routed source, an NDI or Spout receiver on the other side of a routed sender.

## What is new in 0.48.0: network sync

Off by default; with the role Off no socket is opened, no key is made and the sync system does nothing ([ADR 0041](adr/0041-multi-pc-sync.md)).

- **Where the cost goes.** Once per frame the update step of the sync system: on a master it reads 150 control values, builds the state vector of 159 floats and compares it with what each follower knows; on a follower it drains the socket, applies the changes and reports one phase observation to the beat clock. Packets are checked with an HMAC-SHA256 of at most 1200 bytes. There is no thread: the socket is non blocking and polled in the frame.
- **Bandwidth.** About 12 datagrams and 1.1 kB/s from a master to one follower while nothing changes (clock beacons every 0.1 s, a keyframe every 5 s, the manifest every 5 s), and about 38 datagrams and 2.9 kB/s while three controls move continuously (at most 30 deltas a second). A follower sends about a ping a second.
- **Measured here.** `NyrexVJ --netsync-selftest --bench-out result.json` on a software renderer (Mesa llvmpipe, two cores, repeated runs): the update step took 10 to 12 microseconds on average on a follower and 16 to 18 on a master, the worst single call on a master between 0.2 and 2.1 ms (the scheduler of a busy two core machine), and one call on a follower about 8 ms (the first one, which opens the socket and creates the key); the beat of the follower stayed 5.3 to 6.0 ms (mean) and at most 8.7 ms (worst) away from the master over about 970 frames, with 25 percent packet loss in the run. `scripts/test_netsync_two_instances.py` (two real programs at 960 x 540, about 8 to 13 frames a second each): 2 to 12 ms mean in the calm phase, 4 to 12 ms with 25 percent loss and up to 25 ms delay, 5 to 17 ms after a 6 s outage, 31 ms worst of all runs (three runs, three seeds). The frame time of these programs is large, so the error is dominated by it.
- **Not measured.** A real network, 60 frames a second with a full show, three or more PCs, a graphics card, the cost on Windows, and the cost of the sync next to VRChat.

## What is new in 0.49.0: hot standby

Off by default; with Use hot standby off the failover system does nothing, and a PC that is not a follower never holds its output ([ADR 0042](adr/0042-hot-standby.md)).

- **Where the cost goes.** Once per frame the update step of the failover system: it reads the decoder faults of the playing layers, the number of open output windows and the state of the sync follower, builds the readiness report from eleven small checks and moves the state machine. Once a second a follower adds 10 bytes to its Ping and a master adds 13 bytes to each of its ten Clock beacons a second (counted from the encoders). While a PC is a standby its master output is multiplied by zero in the last pass; that is no extra pass.
- **Measured here.** `NyrexVJ --failover-selftest --bench-out result.json` on a software renderer (Mesa llvmpipe, two cores, two runs after the last change): the update step took 12.6 and 13.1 microseconds on average, the worst single call 647 and 131 microseconds. Detection of a lost primary 3.02 s with the lost-after time set to 3 s; the leader answered a probe follower 0.035 s after the decision in the self test and 0.08 to 0.13 s in real programs. These are the numbers of a loopback network and software rendering at 8 to 13 frames per second.
- **Not measured.** A real network, two real PCs, 60 frames a second with a full show, a graphics card, Windows, the cost next to VRChat and the cost of the readiness report with a very large show (the fingerprint walks the used decks and their cells once a second).

## What is new in 0.50.0: release gates and the media churn run

[ADR 0043](adr/0043-release-quality-gates.md), [RELEASE.md](RELEASE.md). The limits for frame times that the soak run never had are written down in `tests/hardware/gates.json` and judged by `scripts/release_gate.py`. Nothing here changes the render loop.

- **Where the cost goes.** A normal run builds the `conditions` block once at the start and once at the end (a few string reads and a call for the system information). A media run (`--soak-media`) imports the files of its folder before the run, launches and clears clips on a timer, and once a second counts the layers whose clip has failed. Nothing is added to a frame.
- **Measured here.** A 30 s media run on a software renderer (Mesa llvmpipe, two cores, 1920 x 1080, six files: two 3D models, an image and three videos): 417 frames, 335 of them over the budget of one frame period, which is what a software renderer does and says nothing about a card; 14 launches, 3 clears, no clip failed, no OpenGL error. A 20 s media run with an image and two 3D models also passed.
- **Limits proposed for a real card** (60 fps target, period 16.7 ms): P95 of the work time within one period, P99 within 1.5 periods, longest frame 100 ms, at most 2 percent of frames over a period, at most 0.5 percent missed frames, at least 97 percent of the target frame rate. They are starting points, not results.
- **Not measured.** Any graphics card, any frame time against these limits, VRAM growth against the card (`scripts/measure_live.py` against `nvidia-smi` or the Windows counters), a run on Windows, a long media run with video decoding through a hardware decoder.

## What is new in 0.66.0: feedback buffers

[ADR 0060](adr/0060-gpu-feedback-lab-budget-pool-freeze-reset-and-feedback-effects.md). The pictures that feedback effects keep (Trails, Hue Feedback, Time Echo, Motion Smear, Feedback Transform, Accumulate) get a memory budget, a pool for reuse, Freeze and Reset. Nothing here changes a show that has no feedback effect.

- **Where the cost goes.** Each feedback effect keeps one 8 bit picture of the size of its layer (3.7 MB at 1280 x 720, 8.3 MB at 1920 x 1080, 33.2 MB at 3840 x 2160) and copies its output into it once per frame. A buffer is made when the effect starts (one clear), reused from the pool when one of the right size is waiting, and released when the effect stops. The pool holds at most 8 buffers and drops one after 300 frames without use; its memory is part of the estimate under Settings, Performance, Memory (new line Feedback buffers). The budget (Settings, Performance, Feedback, 256 MB at the start, 0 = off) makes a new buffer three quarters, half or a quarter of the picture when the full size does not fit, and refuses it when even that does not fit.
- **Measured here.** Mesa llvmpipe, 1280 x 720, one layer with a grey clip, milliseconds per frame, 60 frames after 6 warm-up frames, final run (the other two runs differ by up to 3 ms): no effect 14.4, Trails 23.5, Time Echo 31.5, Motion Smear 26.8, Feedback Transform 26.3, Accumulate 25.8, Time Echo frozen 30.8; four layers 43.0 without an effect and 110.4 with a Time Echo on each. Feedback memory: 3,686,400 bytes for one picture, 14,745,600 for four. A 1,200 frame run with random changes of resolution, deck, quality, budget, freeze, reset and effects peaked at 12.4 MB of feedback memory and ended at 0 bytes after every effect was switched off.
- **What it costs to be under the budget.** A buffer that runs smaller is filled by taking every n-th pixel and read back with bilinear filtering, so trails get softer and a little rougher. With a half size buffer and a half-life of one second, 8 % of the pixels were still one step above black 15 seconds after the picture went black, where the full size buffer was fully black.
- **Freeze is not a saving.** A frozen effect still reads the old picture; only the copy into the buffer is skipped (30.8 ms frozen against 31.5 ms running).
- **Not measured.** A graphics card, Windows, the cost next to VRChat, a run of hours, the first frame of a new buffer on a real driver, and what the budget does on a 4K output with many layers.

## What is new in 0.67.0: passes that only copy

[ADR 0061](adr/0061-gpu-effect-graph-optimizer-skip-passes-that-only-copy.md). When the master chain is empty and the colour grade, the level, Blackout and the hold change nothing, the last pass of a deck only copies its input, so the last layer is now drawn straight into the picture that pass would have written. With a colour look (LUT) the look reads that picture and writes the output, so the scratch picture and the copy back are gone. During a deck transition the two decks do the same. The switch is Settings, Performance, Render graph, Skip passes that only copy (on at the start, written to `settings.json` only when it is off); the line under it counts the passes and copies that were skipped.

- **The picture is the same.** Every render test draws the same frame with the switch off and on and compares every byte of the output; 0 bytes differ in all of them and in the five benchmark cases. Where a master effect, a grade, a level below full, Blackout or the hold is on, the old path runs, because a master effect can leave an alpha below 1 that the final pass forces to 1.
- **Measured here.** Mesa llvmpipe, 1280 x 720, milliseconds per frame, four alternating rounds with the switch off and on and 200 frames in all per side, average with the median in brackets: one layer 17.05 (16.06) to 10.66 (10.32), -37.5 %; four layers 50.61 (49.16) to 44.50 (43.03), -12.1 %; four layers with a colour look 65.21 (62.58) to 56.74 (54.51), -13.0 %; four layers with a master effect 57.09 (56.02) to 56.86 (54.72), no change, as intended; a fade between two decks 85.47 (83.28) to 71.19 (69.81), -16.7 %. The 99th percentile and the worst frame are in the ADR; they move by -24 % to +14 % in both directions and, because the master effect case (same work in both modes) differs by +14 % at the 99th percentile, they are noise.
- **Memory.** With a colour look the estimate drops by one picture of the output size (3,686,400 bytes at 1280 x 720, 8,294,400 at 1920 x 1080, 33,177,600 at 3840 x 2160): 32,382,246 to 28,695,846 bytes in the benchmark. Without a colour look nothing is allocated in either mode.
- **What carries over to a graphics card.** The number of passes does. The milliseconds do not: a pass costs 6 to 7 ms here because a processor draws every pixel, and on a graphics card a copy of this size is a fraction of a millisecond. Expect a smaller saving in time and the same saving in memory.
- **Not measured.** A graphics card, Windows, 4K output, the cost next to VRChat and a run of hours (`M-GRF-1`).

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
- **Skipping the final stage, ping-pong feedback pictures, uniform caches.** Skipping the final stage when it changes nothing is done since 0.67.0 (see above). The final stage is where colour, level and blackout happen and every output reads its result, so it still runs whenever one of them does something, and behind a master effect. A ping-pong feedback target needs a second full-size picture per feedback effect, which is more memory than the copy it saves. Every pass already sets only its program, one or two textures and a few uniforms. None of the three was measured as a problem and each adds risk, so they stay as they are.
- **Allocating the second layer and composition pictures on demand.** Every factory look uses effects, so the saving only exists in compositions without any effect (8.3 MiB per picture at 1080p).
- **Fusing effects, removing the copy of a feedback effect and not drawing covered layers.** Looked at for 0.67.0 and not built: a merged shader changes the rounding between effects, the copy of a feedback effect is the one thing the 8 bit tests of 0.66.0 cannot see go wrong, and a covered layer cannot be told apart from a see-through one on the processor. See [ADR 0061](adr/0061-gpu-effect-graph-optimizer-skip-passes-that-only-copy.md).
- **Blackout skipping.** Skipping all drawing while blackout is on would freeze feedback trails and previews for a state that normally lasts seconds.

## Limits of these numbers

- No real graphics card, no VR, no VRChat: everything above was measured on a software rasteriser. The saving of skipped work (hidden layers, fewer pixels, freed pictures) carries over, the milliseconds do not.
- On llvmpipe the time of small passes (blend, final stage, thumbnails) is not attributed reliably to the pass, because the rasteriser works asynchronously; only the sum is. The per-pass numbers of effects and clips are reliable.
- The Windows build was not run, only compiled and tested by the build service. The reduced resolution, the timers and the freeing of pictures use plain OpenGL 3.3 calls.
- Automatic was tried through the log of a run (100, 88, 75, 63 and 50 percent in sequence under constant overload); how it behaves on a real show with changing load is open.
- Please run `NyrexVJ --bench-shaders 30 --bench-out shaders.json` and `NyrexVJ --preset "Bench Heavy Chain" --bench-comp 300 --profile --bench-out heavy.json` (after `scripts/make_bench_presets.py`) on a real graphics card, with VRChat running, and share the files.

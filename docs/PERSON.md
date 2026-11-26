# Person detection: background removal and 2D tracking of people in a camera picture

Version 0.56.0. [ADR 0049](adr/0049-person-segmentation.md) has the reasons, [KNOWN_ISSUES.md](KNOWN_ISSUES.md) the open problems (KI-24 to KI-27), [TESTING.md](TESTING.md) the tests, [MODULATION.md](MODULATION.md) the sources.

## What this version gives you

Nothing changes until you switch it on. With **Person detection** on **Off** (the default) no session, no thread, no texture and no uniform binding exists, the clip and effect paths are those of 0.55.0, and a live show is drawn the same way. The setting is under Settings, Performance, Person detection and is marked experimental.

**There is no AI model in this version.** The only method is **Background difference**: the program learns a picture of the empty scene (the first 12 analysed pictures), compares every later picture with it, and calls everything that differs a person. It does not know what a person is. A box that is carried in, a door that opens and a shadow count as well. A model for real person segmentation needs a decision by the maintainer (which model, which licence, which runtime, where it comes from); the module is built so that such a method can be added without changing the rest (see "Where a model would plug in").

| Part | What it does |
| --- | --- |
| Mask | A soft picture (white is a person, black is the background) from the picture of the chosen camera, NDI or stream input. It is the same in size as the analysis picture (96, 160 or 256 pixels wide) and is stretched over the picture when it is drawn. |
| Effect **Person Key** | An ordinary effect for a layer or the master. Its **Mode** slider has four steps: the picture only where the person is, the picture only where the person is not, the person as a flat colour, and the outline of the person on the picture. **Backdrop** chooses black or a dark version of the colour for the area outside. |
| Six modulation sources | **Person present**, **Person X**, **Person Y**, **Person size**, **Person motion**, **Person count**, usable everywhere a source can be chosen (layer, master, effect reaction). They are appended to the list; old presets keep their meaning. |
| Tracking | Up to 8 people get a number. A person who is partly covered keeps the number, one who is fully covered is held for a moment and searched at the predicted place, and two people who cross keep their numbers (tested). The sources follow the biggest tracked person. |

What is **not** here: a model, a download, a network connection, transparency (see below), an input from Spout, recognition of faces or poses, or any promise that it works well with a real camera. See "Not built and not claimed".

## How it works

1. **Reduction** (`src/core/person_mask.cpp`, `reduce`, line 392). The live picture (UYVY or RGBA) is reduced to an RGB picture of 96, 160 or 256 pixels width (Fast, Balanced, Fine; the height follows the aspect, at most 65,536 pixels in all). UYVY is read with the Rec. 709 matrix from 720 lines up and Rec. 601 below. It takes about 0.2 ms for a 1280 x 720 picture and 0.4 to 0.6 ms for 1920 x 1080.
2. **Backend** (`Backend`, `src/core/person_mask.hpp`; `DifferenceBackend`, `person_mask.cpp:61`). Background difference: a per pixel average and spread of the empty scene, a gain estimate that follows a change of the whole light, a threshold from the spread and the **Sensitivity** setting, and a slow adaptation (the background follows changes with a time constant of 20 seconds, a pixel that is held as person with 300 seconds). A scene that changes completely (85 percent of the pixels for three pictures) starts learning again.
3. **Stabiliser** (`Stabiliser::process`, line 820). Blends the new score with the last mask (**Stability**) and softens the edge (**Softness**, 0 to 16 pixels in the analysis picture).
4. **Blobs and tracks** (`findBlobs`, line 488; `Tracker::update`, line 621). Connected areas become blobs, small ones are dropped, blobs are matched to the tracks from the last pictures (position and size, predicted from the speed). A track needs 3 hits before it counts as a person and coasts for 1.5 seconds after the last hit (4 seconds when it was merged into another blob, for example when two people overlap); after that it is dropped and a person who appears again gets a new number.
5. **Signals** (`SignalFilter::update`, line 760). Smoothed values of the biggest tracked person; when nobody is visible **Person present** falls to 0 and x, y and size keep their last value (use **Person present** to gate them).
6. **Worker thread** (`src/media/person_worker.cpp`). Runs at background priority, keeps only the newest picture (an older waiting picture is replaced, never queued), measures its own time and has a **governor** (`Governor::record`, `person_mask.cpp:1074`) that lowers the quality tier, then the rate, when the average of the last 6 analyses is over 25 percent of one analysis interval, raises them again after 60 calm analyses (below 35 percent of the budget) and switches the module off when even the lowest setting does not fit.
7. **Session** (`src/render/person_session.cpp`). Takes the pictures from the live input (`LiveFrameObserver::offer`, called by `LiveInput::update` right after a frame is taken, `src/render/live_input.cpp:131`), cuts the rate (**Analysis rate**), uploads the newest mask to one `R8` texture and gives the signals to the engine. The live picture itself is not changed.
8. **Engine and effect** (`Engine::setPersonSettings`, `servicePerson`, `sourceValue`; effect `Person Key` in `src/render/effects_extra.cpp:1161`). The mask is bound to texture unit 3 for every effect pass and `personMask(uv)` in `src/render/effect_library.cpp` reads it with the aspect fit of the camera picture on the output. Without a ready mask `uPersonOn` is 0 and the effect returns the picture unchanged.

## Before and after: what really keeps what

"Before" cites the code of 0.55.0 (commit `ee1881b`), "after" the code of this version. "Measured" means `NyrexVJ --person-selftest` or `nvj_person_tests` on Linux with Mesa llvmpipe; nothing was measured on a graphics card, with a real camera, on Windows or next to VRChat.

| Path | Before (0.55.0) | After (0.56.0) | Evidence |
| --- | --- | --- | --- |
| Live pictures (camera, NDI, stream) | `LiveInput::update` (`src/render/live_input.cpp:75`) takes a frame with `source_->take` (`:128`) and uploads it with `present` (`:144`) | the same; one more call, `observer_->offer(scratch_)` (`:131`), only when an observer is set (person detection on and the input chosen) | measured: with detection off no observer is set and no session exists; the mask path does not change the picture of the clip |
| Existing masks | `MaskBitmap` (`src/core/output_map.hpp:63`) and `MaskImages` (`src/core/mask_images.cpp:84`) are used for output masks only | the person mask is a `MaskBitmap` as well, built by the same type and used by the same blob code; it is a new texture and does not touch the output masks | `nvj_person_tests` (mask type, edges), the output mask tests were not changed |
| Effects | `kEffectHeader` / `kEffectFooter` (`src/render/effect_library.cpp:47`, `:114`) have no mask input; the footer keeps the alpha of the picture (an opaque picture stays opaque) | the header adds `uPerson`, `uPersonOn`, `uPersonFit` and `personMask`; the footer is not changed; the effect library has one more effect (`Person Key`) | measured: with detection off `Person Key` equals the plain picture (largest channel difference 0 of 255); `--check-shaders` compiles all effects |
| Mask orientation and fit | not applicable | the mask has the row order of the camera picture (row 0 at the top); `personMask` flips it once; a 4:3 camera on a 16:9 output gets the pillar boxes, as the clip does | measured: person at the top left of the camera picture is at the top left of the output; a vertical or horizontal flip fails four checks (a deliberate flip of the shader was tried and caught) |
| Modulation sources | `Engine::sourceValue` (`src/render/engine.cpp:2106`) and `modulationSource` (`src/core/modulation.cpp:86`) know audio, LFOs, macros, beat, bar and the plugin sources `BandPlugin1` to `BandPlugin8` (`src/core/composition.hpp:57`, `:64`) | six sources appended after the plugin sources (`BandPersonPresent` to `BandPersonCount`); the numbers of the old sources do not change and presets store the name | `nvj_person_tests`, the existing modulation tests and the preset tests passed unchanged (59 of 59 tests) |
| Interface for sources | the combo lists all sources | the six person sources are shown only while person detection is on or when a slot already uses one (`pluginBandVisible` in `src/app/ui_panels.cpp`) | read from the code; a preset with a person source and detection off keeps its source and reads 0 |
| Graphics memory report | `Engine::gpuMemory` (`src/render/engine.cpp:1564`) adds live inputs (`:1587`) | adds the size of the mask texture (`PersonSession::gpuBytes`) to the live inputs | measured: 14,400 bytes while on, 0 after switching off |
| Camera, NDI and stream capture | `CameraCapture`, the NDI receiver and `StreamCapture` own their threads and reconnect rules | unchanged; the session only reads the frames that `LiveInput` hands on and reports "no picture from the source" when none arrives for 1 second | measured: the signals fall to zero 3.4 s after the last picture, the state says "source lost", and everything comes back when pictures arrive again |
| Frame pacing and output frame rate | `FramePacer` and the VSync setting | unchanged | the person path runs on its own thread; the render thread only takes the newest result |
| Settings | no key | `personMode`, `personInput`, `personQuality`, `personRate`, `personSensitivity`, `personSoftness`, `personStability`, each written only when it differs from the default | `nvj_person_tests` (default left out of the file, values round trip, wrong values and wrong types read as defaults or brought into range) |

## Quality, rate and cost

| Tier | Analysis width | Measured analysis time (median, p95, one core, one picture, 16:9) | Share of one core at 15 analyses per second |
| --- | --- | --- | --- |
| Fast | 96 pixels | 0.16 ms, 0.20 ms | 0.25 percent |
| Balanced | 160 pixels | 0.55 ms, 0.60 ms | 0.83 percent |
| Fine | 256 pixels | 1.96 ms, 2.65 ms | 2.9 percent |

- **Budget.** One analysis may take at most 25 percent of the interval between two analyses (8.3 ms at 30 per second, 16.7 ms at 15). When the average of the last 6 analyses is over it, the governor lowers the tier, then the rate (halved, at least 2 per second), and switches the module off at the lowest setting ("the analysis is too slow for this computer and was switched off"). It raises rate and tier again after 60 analyses below 35 percent of the budget.
- **Rate.** 2 to 30 analyses per second, 15 by default. Between two analyses the last mask and the last values stay.
- **Reduction on the render thread.** The live picture is reduced where the frame arrives, 0.2 ms (measured at 1280 x 720) to 0.6 ms (1920 x 1080). Only the analysis happens on the worker.
- **Graphics memory.** One `R8` texture of the analysis size: 5,184 bytes (Fast, 96 x 54), 14,400 (Balanced, 160 x 90) or 36,864 bytes (Fine, 256 x 144) for a 16:9 camera, and nothing else. Counted in the memory report.
- **Whole step in the real engine** (960 x 540 output, 1280 x 720 camera, software OpenGL, 40 steps): 14.2 ms per step with detection off, 22.7 ms with detection on and a picture handed in every step; analysis 0.92 ms on average and 3.7 ms at the 95th percentile, budget 8.3 ms. On a graphics card these numbers mean little; they show that the person path stays far under its own budget on this machine.

## Quality that was measured (synthetic pictures only)

All of these are from `nvj_person_tests` with computer-made pictures (a moving person-shaped area on a textured background with noise). They compare against the true mask. Real cameras have noise, automatic exposure, compression and clothes that look like the wall; none of this was tried.

| What | Result |
| --- | --- |
| Overlap with the true mask (IoU) at 96, 160 and 256 pixels | 0.93, 0.97, 0.99; false positives 0.36, 0.15 and 0.03 percent of the picture |
| A person whose colour is close to the background | IoU 0.71 at sensitivity 0 and 0.96 at 100 |
| Edge softness 0, 50, 100 | a transition of 0, 8 and 16 pixels on each side of the edge (in the analysis picture), IoU 0.97 to 0.99 |
| Flicker (mean change per pixel and frame) at stability 0, 50, 100 | 0.00027, 0.00008, 0.00004 |
| Two persons crossing | numbers kept, 2 tracks, left and right assigned correctly after the crossing |
| Light changes (brighter, darker, warmer, cooler) | false positives 0.2 percent after four pictures, IoU 0.96 to 0.99 |
| Slow drift of the light by 60 percent | worst false positives 0.21 percent, lowest IoU 0.97 |
| Strobe (light doubled every second picture) | worst false positives 2.2 percent; the person was found in 30 of 30 pictures |
| Camera lost | the mask is empty after 3 pictures, the signals are quiet after 40 pictures, no track is left |
| A person partly or fully covered by an object | partly covered: same number, smaller area; fully covered for 10 pictures: held as coasting and found again with the same number; covered for 75 pictures (5 seconds at 15 per second): the number is dropped and the person comes back with a new one |
| The scene changes completely | learning starts again; a person who is in the picture at that moment becomes part of the new background (see KI-24) |
| Worker | 200 pictures submitted, 200 analysed, none replaced while busy at 30 per second; destroyed with 50 pictures waiting in about 100 ms |
| Pictures of the wrong size, too short buffers, random pictures, pictures only 1 to 64 pixels wide, extreme settings | the analysis refuses what it cannot read and reports "failed"; no crash (part of the 3,341 checks) |

## Fallbacks

| Situation | What happens |
| --- | --- |
| The module is off | nothing is created; `Person Key` returns the picture unchanged |
| No source is set for the chosen input | the module waits; the page says "no source is set for this input"; the mask stays empty |
| The camera or stream stops delivering | after 1 second the state is "source lost"; the mask and the signals fall to zero (about 3 seconds in the test); everything comes back by itself |
| The picture size changes | the background is learned again |
| The whole picture changes (a cut, a big light change over many pixels) | the background is learned again, shown as "the picture changed completely, learning the background again" |
| The analysis is too slow | quality tier down, then rate down, then switched off with a reason on the page |
| An analysis fails or the backend reports a problem | the state is "failed", the mask is not used and the effect shows the plain picture; the reason is on the page |
| Settings with unknown or damaged values | read as the defaults (`off`, `camera1`, `balanced`, 15, 50, 50, 50) |
| The mask texture cannot be made | the effect shows the plain picture |

## Where a model would plug in

`person::Backend` (`src/core/person_mask.hpp`) is the only place that decides what counts as a person: it gets a small RGB picture and returns a score per pixel, plus `Provenance` (id, title, licence, origin, whether it needs a model, whether it uses the network, whether it is deterministic). A model backend would be another class behind `makeBackend`. Everything after it (stabiliser, blobs, tracker, signals, worker, texture, effect, sources, settings page, governor, tests) is the same for every backend. **Nothing in this version picks, bundles or downloads a model.** What the maintainer has to decide before one is added: the model (and where the file comes from), its licence and the licence of its runtime, the runtime itself (a dependency in [DEPENDENCIES.md](DEPENDENCIES.md)), how the file gets onto the machine without an automatic download, the budget on a real graphics card or processor, and what the page must show about its origin. The `Provenance` fields and the line "Method: ..." on the settings page exist for that.

Privacy: the module reads pictures of people. In this version the pictures stay in memory (a copy of 96 to 256 pixels width for a moment), nothing is written to disk, nothing is sent anywhere and there is no network code in the path. The page says so; the code can be read at the files named above.

## Reproduce

```
cd build
ctest -R nvj_person_tests
xvfb-run -a -s "-screen 0 1280x720x24" env LIBGL_ALWAYS_SOFTWARE=1 \
  ./NyrexVJ --fresh --no-update-check --no-welcome --person-selftest --bench-out person.json
```

`--person-selftest` needs no camera and no files. It feeds computer-made pictures into the session of the real engine (a feed switch, `Engine::setPersonFeedExternal`, replaces the live input) and reads the output of the real effect. It changes no files and restores the composition at the end.

## Not built and not claimed

- **A model.** No segmentation network, no pose or face model, no runtime. Real person segmentation (hair, hands, a person in front of a busy background, a moving camera) is not possible with this method.
- **Real cameras.** All tests use computer-made pictures. How it behaves with a real webcam, a capture card, auto exposure, compression artefacts, flicker of stage lights and projections on the wall behind the person was not tried. A camera that sees the projected show is expected to produce false foreground; that was not tested (KI-25).
- **Transparency.** The effect draws the person's picture over a backdrop colour inside the effect chain; it does not make the area outside transparent, because the compositing in this program keeps opaque pictures opaque. Use the Add or Screen blend of the layer, or a black backdrop, to lay the person over other layers.
- **Graphics card, Windows, VRChat.** Not measured. The analysis runs on the processor only; the numbers above are for one 2 core machine with software OpenGL.
- **Spout input.** Not supported; Spout pictures never reach `LiveInput::update`.
- **Layer scale and position.** The mask is aligned to the camera picture when the clip is shown unscaled and unmoved; if a layer scales or moves the camera clip, the mask does not follow; this was read from the code and not tried (KI-26).
- **Audio, telemetry and a network.** None. No covert telemetry exists in this module.
- **The manual tests** `M-PERSON-1` to `M-PERSON-4` in [TESTING.md](TESTING.md) describe how to try it with real cameras and a real graphics card.

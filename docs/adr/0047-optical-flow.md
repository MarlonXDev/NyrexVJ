# ADR 0047: Optional optical flow: motion estimation on the processor, frame interpolation, motion blur and a vector view for video clips

- **Status:** Accepted for the automated part (requested by MarlonXDev as prompt 27). Issue [#122](https://github.com/MarlonXDev/NyrexVJ/issues/122). Nothing was run on a real graphics card, on Windows, with real footage or next to VRChat.
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [FLOW.md](../FLOW.md), [MEDIA.md](../MEDIA.md), [INTERFACES.md](../INTERFACES.md), [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), [TESTING.md](../TESTING.md), [ADR 0010](0010-video-decode-pipeline.md), [ADR 0024](0024-video-pipeline-audit-and-measurement.md), [ADR 0046](0046-color-management.md)

## Context
A video of 30 frames per second on a 60 Hz output shows every frame twice, and slow motion shows each frame for many output frames. The prompt asked for an optional module that estimates the motion between frames and builds effects on it (new pictures between two frames, motion blur), with a clear line between the vectors, the synthesis and the interface, a baseline that can be understood before any graphics card acceleration, tiers with a time and memory budget, fallbacks for variable frame rates, scene cuts, alpha, loops and unsuitable sources, no proprietary driver features, no AI models and no new graphics API, and a show that stays exactly as it was while the feature is off.

## Findings (what existed before)
- **Existed:** a clock driven playhead with speed (`Playhead`, `PlaybackDriver`), a decoder that returns the newest decoded frame not later than the wanted time and keeps a queue of five frames, reverse playback with its own buffer, one texture per video in `VideoUploader`, a frame pacer and a frame rate limit, and a memory count per video.
- **Not found:** any motion estimation, any pair of frames given to a worker, any way to look at the next frame without taking it, any synthesis of a picture between two frames, any scene cut detection.
- **Not verified before this version:** how a 30 frames per second source behaves on a 60 Hz output on a real graphics card (it is shown twice per frame by design; nobody measured judder).
- The table with code references is in [FLOW.md](../FLOW.md).

## Decision
1. **Three parts that talk through small data types.** `src/core/optical_flow.cpp` estimates a field of vectors with a confidence and a scene cut flag from two brightness pictures and has no OpenGL; `src/render/flow_interp.cpp` turns two frames, a field and a time into a picture with shaders (the same formulas run on the processor in `src/core/flow_synth.cpp` for the tests); `src/render/flow_session.cpp`, `src/media/flow_worker.cpp` and `src/app/ui_flow.cpp` do the scheduling, the fallbacks and the interface. The estimate can be tested on any machine; the shaders and the session only in the engine.
2. **A plain, understandable baseline: block matching on a pyramid, on the processor.** 8 x 8 blocks on a small brightness picture, coarse to fine, refined to a fraction of a pixel, smoothed and marked with a confidence, in three tiers (160, 240, 360 pixels wide). No AI model, no vendor motion engine, no new graphics API. A graphics card path was **not** built: nothing showed the processor path to be too slow (about 1 ms per pair at a small size here), and without a graphics card it could not be measured.
3. **Off by default and invisible when off.** `present()` calls the flow only when the mode is not Off, the graphics path is in use and the safe upload path is not. With the mode Off no session, thread, texture or shader is created (measured: 0 pairs, 0 bytes). The same file path is used for every video as before.
4. **Never delay the picture and never invent where it is not sure.** Exact frames are returned unchanged. A time between two frames uses the frame being shown (A) and the next decoded frame (B) taken from the front of the decoder queue without removing it (`VideoDecoder::peekAhead`). If B or the analysis is not ready, the current picture is held. A scene cut, a pair with times out of order or a gap above 2.5 periods holds the current picture. A block with low confidence or where the two readings disagree falls back to the nearer real picture.
5. **A worker thread at background priority with a governor.** One analysis thread per video, at the existing background priority (`setCurrentThreadBackground`). It measures its own time and lowers the tier when 4 of the last 6 pairs take more than 0.75 of the frame period, switches the feature off for the video at the lowest tier and raises the tier after 90 calm pairs.
6. **A memory budget with a refusal.** A reservation (`estimateBytes`, a ceiling) is taken from a global budget (32 to 2048 MB, default 256) before anything is created; a video that does not fit is drawn the old way and says why and how much it would need.
7. **Three modes in one setting.** Interpolate frames, Motion blur (with a shutter from 0 to 100 percent) and Show motion vectors (for checking); the settings are `flowMode`, `flowQuality`, `flowBudgetMb`, `flowShutterPercent`, written only when they differ from the default and read as the default when damaged.
8. **Honest limits in the interface.** The setting is marked experimental, the tooltip says that backwards playback and sources that cannot be analysed are shown without it, and every video shows its state and the reason when it falls back.
9. **Measure it.** `nvj_flow_tests` checks the estimate, the synthesis, the planning, the governor and the budget without OpenGL; `--flow-selftest` drives the real player with four generated clips (30 and 60 frames per second, a hard cut, a variable frame rate) and compares the result with the real 60 frames per second clip.

## Alternatives considered
- **Estimation on the graphics card first.** Rejected for now: it needs a real graphics card to measure, and a first version that the tests can follow line by line is more useful. The processor tiers are cheap enough on this machine. A graphics card path can replace the estimate later behind the same field type.
- **A vendor optical flow engine (NVIDIA, AMD).** Rejected by the request (no proprietary drivers) and not testable here.
- **A learned model.** Rejected by the request.
- **Delaying the picture by a frame so that the next frame is always ready.** Rejected: it would add latency to every video for a feature most shows do not use. Holding the current picture when B is late costs nothing.
- **Interpolating backwards playback.** Not built: the reverse buffer has a different order and its own pacing; the session says "reverse playback is not analysed".
- **One flow for the whole master picture.** Not built: effects, layers and transitions are not video frames; the flow works on the frames of a video before they enter the layer.

## Consequences
- A show that does not use the feature is unchanged: same pictures, same memory, same threads (tested for the feature off).
- With Interpolate on, a 30 frames per second video on a 60 Hz output shows a different picture for every output frame; in the synthetic test the picture between two frames is 7.3 dB closer to the real 60 frames per second picture than the held frame, and again 7.3 dB closer at half speed.
- It costs one analysis thread per video at background priority, graphics memory as listed in [FLOW.md](../FLOW.md) and about one millisecond of processor time per pair at a small size.
- Object borders over a different background can show a soft halo, and a fast fade to black can be read as a scene cut (the picture is then held, as without the feature).

## Validation / open questions
- **Run (Linux, Mesa llvmpipe, Xvfb, no graphics card):** `nvj_flow_tests` (2905 checks), `--flow-selftest` (22 checks), the existing colour, output, video and interface selftests, the whole `ctest` suite, `scripts/check_i18n.py`, `scripts/check_test_matrix.py`, `scripts/check_release.py` and `scripts/build_manual_site.py --check`. The selftest also ran once with all cores busy as a stand-in for another program.
- **Not done:** a real graphics card (speed at 1080p and 4K, behaviour of the drivers), Windows at run time, real footage (faces, water, smoke, particles, strobing light), VRChat running next to NyrexVJ, long soak runs with flow on, and a review by someone else.
- **Open for the maintainer:** whether a graphics card path is wanted once a real measurement says the processor path is too slow at 1080p or 4K; whether the defaults (Balanced, 256 MB, shutter 50 percent) are right; whether interpolation should also work for backwards playback; whether a per clip setting is wanted in addition to the global one.

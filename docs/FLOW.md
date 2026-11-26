# Optical flow: motion estimation, frame interpolation and motion blur

Version 0.54.0. [ADR 0047](adr/0047-optical-flow.md) has the reasons, [KNOWN_ISSUES.md](KNOWN_ISSUES.md) the open problems (KI-17, KI-18), [TESTING.md](TESTING.md) the tests.

## What this version gives you

Nothing changes until you switch it on. With **Optical flow mode** on **Off** (the default) no flow object, no thread, no texture and no shader exists, and every video is drawn by the same path as in 0.53.0. The setting is under Settings, Performance, Optical flow and is marked experimental.

| Mode | What it does |
| --- | --- |
| Interpolate frames | Creates the pictures between two real video frames. A 30 frames per second video on a 60 Hz output gets a new picture for every output frame, and slow motion (speed below 1) no longer shows each frame for several output frames. |
| Motion blur | Smears the picture along the measured motion. **Shutter (%)** says how long the virtual shutter is open. |
| Show motion vectors | Draws the measured motion as colour (red and green for the direction, blue for how sure the estimate is). It is meant for checking what the analysis sees. |

Three quality tiers (**Fast**, **Balanced**, **Fine**) and a graphics memory limit (**32 to 2048 MB**, 256 by default) belong to it. The video list on the same page shows for every playing video its state, its tier, the analysis time, how many pictures were made or held and its memory.

What is **not** here: a second source of motion (no AI model, no driver feature such as NVIDIA Optical Flow or AMD Fluid Motion), analysis on the graphics card, interpolation of backwards playback, a flow of HAP or other compressed sources, or a promise that real footage looks as good as the synthetic test clips. See "Not built and not claimed".

## How it works

Three parts that do not know each other's insides:

1. **Motion vectors** (`src/core/optical_flow.cpp`, no OpenGL). A video frame is reduced to a small brightness picture (160, 240 or 360 pixels wide for Fast, Balanced and Fine), weighted by alpha. A pyramid of smaller copies is searched block by block (8 x 8 blocks) from coarse to fine, refined to a fraction of a pixel, smoothed, and every block gets a **confidence** from how well the match fits. The Fine tier also checks the match backwards. A **scene cut** is declared when most textured blocks have no good match at all.
2. **Frame synthesis** (`src/render/flow_interp.cpp`, shaders; `src/core/flow_synth.cpp`, the same formulas on the processor for the tests). For a time `t` between frame A and frame B the picture is read from A at `uv - t * flow` and from B at `uv + (1 - t) * flow`, mixed in premultiplied colour, and the mix is weighed by the confidence and by how well the two readings agree. Where the vectors are not trustworthy the result falls back to the nearer real picture. A time that sits exactly on a real frame returns that frame unchanged (bit for bit, tested).
3. **Interface and control** (`src/app/ui_flow.cpp`, `src/render/flow_session.cpp`, `src/media/flow_worker.cpp`). The session sits between the decoder and the texture of a video: it asks the decoder for the next frame (`VideoDecoder::peekAhead`), gives a pair to a **worker thread** that runs at background priority, uses the result when it is ready and otherwise shows the current picture and counts it. The worker measures its own time; a **governor** lowers the tier when the analysis does not fit into 0.75 of a frame period (4 of 6 recent pairs too slow) and switches the feature off for that video at the lowest tier. It raises the tier again after 90 calm pairs.

Each source frame is converted once for the analysis and uploaded once for the picture. The decoder, the upload and the pacing of the output are not changed.

## Before and after: what really keeps what

"Before" cites the code of 0.53.0 (commit `6c8909e`), "after" the code of this version. "Measured" means `NyrexVJ --flow-selftest` or `nvj_flow_tests` on Linux with Mesa llvmpipe; nothing was measured on a graphics card.

| Path | Before (0.53.0) | After (0.54.0) | Evidence |
| --- | --- | --- | --- |
| Playback clock and speed | `Playhead::advance` (`src/core/playhead.cpp:170`) moves the position by clock delta times speed; `PlaybackDriver::drive` (`src/media/playback_driver.cpp:38`) turns it into a time in the file; `periodOf` (`:5`) is the frame period | the same, untouched | not changed |
| Choosing the frame | `VideoDecoder::frameAt` (`src/media/video_decoder.cpp:1236`) returns the newest decoded frame not later than the time (`pickForward`, `:892`); at 30 frames per second on a 60 Hz output every frame is shown twice | the same call; the flow session receives that frame as picture A and looks at the frames behind it | measured: with the feature off no picture goes through the flow path, 0 pairs, 0 bytes |
| Backwards playback | its own buffer (`pickReverse`, `:932`; `reversing()`, `:1313`) | the same; the session switches itself off for the video and shows the normal picture | measured: state "plain playback", reason "reverse playback is not analysed" |
| Decoder queue | `kQueueLimit = 5` (`:86`), pool `kQueueLimit + 4` | the same; `peekAhead` only reads the front of the queue and never takes a frame out | measured and read from the code |
| Upload | `MediaPlayer::present` (`src/render/media_player.cpp:218`) gives the frame to the single texture of `VideoUploader::present` (`src/render/video_gpu.cpp:433`) | with the feature on, `present` hands the frame to `presentFlow`, which uploads A and B into the same uploader once each and draws into its own target; `texture()` returns that target while the flow is engaged | measured: exact frames equal normal playback 20 of 20 |
| Fallback of the player | a failed GPU upload switches the clip to the safe CPU path | the same; with the safe path the flow is not used | read from the code |
| Output frame rate limit and pacing | `FramePacer` (`src/core/frame_pacer.cpp:31`, `:45`) and the VSync setting | the same, untouched | measured: 16.7 against 15.2 ms per step with the feature on and off |
| Frame skipping (the output is slow) | the clock jumps, the next frame is chosen by time | the same; a gap between two frames of more than 2.5 periods is not interpolated | measured: with three steps at once every fifth step the clip keeps playing, 0 upload failures, 58 pictures made and 7 held |
| Scene cuts | none | detected from the match, the current picture is held across the cut (equal to normal playback) | measured: 4 cuts found in the cut clip, the picture at the cut equals normal playback |
| Variable frame rate | the time of every frame is its own | the pair uses the real times of A and B; a gap above 2.5 periods falls back | measured: the clip with a 0.2 s gap interpolates around the gap and holds in it |
| Loop boundary | the position wraps, the decoder seeks | the frame after the wrap is earlier than A, so the pair is refused and the picture is held; the frame ids restart with a new decoder, so the session forgets all frames when the decoder is replaced (`forgetFrames`, `:18`) | unit tests of the loop case; the GL path was not run across a loop |
| Alpha | RGBA and planar frames with an alpha plane are drawn with their alpha | the analysis weighs by alpha, the synthesis mixes premultiplied colour and alpha alike | measured: a ProRes 4444 clip runs through the flow and its exact frames equal normal playback |
| Graphics memory | the player counts its textures (`MediaPlayer::gpuBytes`) | adds the flow targets; reserved against the limit before anything is created | measured: reported memory, reservation and refusal |
| Settings | no key | `flowMode`, `flowQuality`, `flowBudgetMb`, `flowShutterPercent`, written only when they differ from the default | `nvj_flow_tests` (default left out of the file, values round trip, wrong values and wrong types read as the default or brought into range) |

## Quality tiers, time and memory

| Tier | Analysis width | Pyramid | Search | Check backwards | Measured analysis time of one pair on a 320 x 176 picture (one core, two runs) |
| --- | --- | --- | --- | --- | --- |
| Fast | 160 | 3 levels | 4 | no | 0.7 ms |
| Balanced | 240 | 3 levels | 5 | no | 0.75 ms |
| Fine | 360 | 4 levels | 5 | yes | 1.3 to 1.8 ms |

- **Latency.** The shown picture is never delayed: the picture for a time between A and B is made from A and the already decoded B, and exact frames are the unchanged frames. If the analysis of the pair is not ready, the current picture is shown and the counter "pending" goes up. The budget for one analysis is 0.75 of the frame period of the video.
- **Graphics memory (the reservation, per video).** `estimateBytes` in `src/core/optical_flow.cpp` counts the picture of the video three times for Interpolate (two stored frames and the result) and twice for Motion blur and Vectors, plus the largest flow texture (131 072 bytes). It is a ceiling; the real use is a little lower (the 320 x 180 test clip: 822 272 bytes reserved, 695 280 bytes used).

| Picture | Interpolate | Motion blur or vectors |
| --- | --- | --- |
| 1280 x 720 | 10.7 MB | 7.2 MB |
| 1920 x 1080 | 23.9 MB | 15.9 MB |
| 3840 x 2160 | 95.0 MB | 63.4 MB |

  With the default limit of 256 MB that is about ten 1080p videos or two 4K videos at once in Interpolate mode. A video that does not fit is shown without the flow and says so (**the video memory budget is too small**, and how much it would need).
- **Processor.** One analysis thread per video at background priority (`setCurrentThreadBackground`: below normal on Windows, nice 5 on Linux). It leaves the cores to the program and to other programs; a late analysis lowers the tier by itself. How the load of another program such as VRChat changes this was only simulated (see "What was measured").

## Fallbacks

| Situation | What happens |
| --- | --- |
| The feature is off | nothing is created, the video is drawn as before |
| Backwards playback | the normal picture, reason shown |
| Compressed frames (for example HAP uploaded directly), an unsupported sample size or layout, no picture data | the normal picture, reason shown |
| Safe CPU upload path (after a failed GPU upload) | the flow is not used |
| The flow shaders cannot be built, or the textures cannot be made | the video is drawn as before and the reason is shown |
| The memory limit is too small | the video is drawn as before, the needed size is shown |
| The analysis is too slow even at Fast | the video is drawn as before for the rest of its run ("switched off, too slow") |
| The analysis of a pair is not ready | the current picture is held, "pending" is counted |
| The frame times are out of order, a gap above 2.5 periods, or no next frame | the current picture is held |
| A scene cut | the current picture is held, so the cut stays a cut |
| The vectors of a block are not trustworthy | that block falls back to the nearer real picture |
| Settings with unknown or damaged values | read as the defaults (`Off`, `balanced`, 256 MB, 50 percent) |

## What was measured

All numbers are from one run on Linux, Mesa llvmpipe (software OpenGL 4.5), a virtual screen, 2 cores and no graphics card. They are an example, not a promise for other machines.

| What | Result |
| --- | --- |
| `nvj_flow_tests` | 2905 checks, 0 failed, no OpenGL: pictures to luma for every frame layout, translations of 3.5 to 20 pixels with errors below 0.3 pixel (mean), static and flat pictures, hard cuts (to black, to a dark copy, between two textured shots) at all tiers and no cut in normal motion, pair planning, governor, budget, sampling, synthesis against a known 60 fps frame, motion blur, vectors, layers moving apart, frame drops, loop boundary, fades and dissolves, determinism, and the settings keys |
| 30 to 60 on a synthetic translation (unit test) | interpolated 48.8 dB, plain blend 23.2 dB, held frame 18.1 dB against the real frame in between |
| Several layers moving differently (unit test) | 28.3 dB, plain blend 26.5 dB, held frame 23.3 dB, with halos at the object borders |
| `--flow-selftest` | 22 checks, all passed: library clips, feature off, 30 to 60 against a real 60 fps clip, exact frames, half speed, hard cut, variable frame rate, backwards playback, memory limit, frame skipping, motion blur, vectors, an alpha clip, cost and state of OpenGL |
| 30 to 60 in the real engine (19 frames in between) | interpolated 31.9 dB, held 24.6 dB against the 60 fps clip; exact frames identical 20 of 20 |
| Half speed in the real engine (21 frames) | interpolated 31.9 dB, held 24.5 dB; 86 different pictures against 23 |
| Hard cut | 4 cuts found, the picture at the cut equals normal playback |
| Variable frame rate with a gap of 0.2 s | 72 pictures made, 24 pairs refused at the gap |
| Frame time at 960 x 540, 40 steps without sleep | 15.2 ms per step off, 16.7 ms on; analysis 1.1 ms on average and 2.1 ms at the 95th percentile |
| The same run with two busy processes on the 2 cores (a stand-in for another program, not VRChat) | all checks passed; analysis 3.5 ms on average and 12.9 ms at the 95th percentile, no tier drop |

## Reproduce

```
cd build
ctest -R nvj_flow_tests
xvfb-run -a -s "-screen 0 1280x720x24" env LIBGL_ALWAYS_SOFTWARE=1 \
  ./NyrexVJ --fresh --no-update-check --no-welcome --flow-selftest ../tests/data --bench-out flow.json
python3 ../scripts/make_flow_test_clips.py ../tests/data
```

`--flow-selftest [FOLDER]` reads `test_flow_30.mp4`, `test_flow_60.mp4`, `test_flow_cut.mp4` and `test_flow_vfr.mp4` from the folder (default `tests/data`) and works in a scratch data folder under the temp folder. It needs FFmpeg; without it the checks are skipped, or fail when `NYREXVJ_REQUIRE_VIDEO` is set. The clips are made by `scripts/make_flow_test_clips.py` from a moving synthetic picture (320 x 180, H.264).

## Not built and not claimed

- **Real footage.** The test clips are synthetic. How the interpolation looks on faces, water, smoke, fine particles, repeating patterns, strobing lights and fast flicker was not judged, and such material can show halos, wobble or smearing. A fast fade to black can be read as a scene cut (measured: a fade over 4 frames in all pairs, not a fade over 15 frames and not a dissolve between two textured shots over 8 frames); the picture is then held, as without the feature.
- **Graphics card and Windows.** Speed and behaviour on a real graphics card, with real drivers and on Windows were not measured; the draw time in the report is the processor time to submit the pass, not graphics time. The analysis runs on the processor only. A graphics card path was not built because no measurement showed the processor path to be too slow (about 1 ms per pair on this machine at a small size); the cost at 1080p and 4K on real hardware is not known.
- **VRChat or another program in parallel.** Not run. The analysis thread runs at background priority and adapts by itself, and a run with busy cores passed, but nobody has watched NyrexVJ next to VRChat.
- **Backwards playback, HAP and other compressed sources, 10 bit and higher pictures** are shown without the flow.
- **Hardware motion engines of the graphics card, AI models, new graphics APIs.** None is used.
- **Output at a fixed higher frame rate.** The flow makes pictures for the times at which the program draws; it does not change the frame rate of the output, the pacer or the recorder.
- **The manual tests** `M-FLOW-1` to `M-FLOW-4` in [TESTING.md](TESTING.md) describe how to try it on real hardware.

# ADR 0028: Live recording of the final picture: a read back without waiting, one encoder thread, a bounded frame pool, a timeline that follows the wall clock

- **Status:** Accepted (requested by MarlonXDev, Prompt 05). Issue [#82](https://github.com/MarlonXDev/NyrexVJ/issues/82). Transparency in the recording, stereo sound, recording a single output with its mapping and the hardware encoder presets are **Proposed** and wait for the maintainer.
- **Date:** 2026-11-25
- **Owners:** Maintainers (pending assignment)
- **Related:** [RECORDING.md](../RECORDING.md), [FFMPEG.md](../FFMPEG.md), [OUTPUTS.md](../OUTPUTS.md), [PERFORMANCE.md](../PERFORMANCE.md), [TESTING.md](../TESTING.md), [ADR 0024](0024-video-pipeline-audit-and-measurement.md), [ADR 0027](0027-live-stability-and-hardware-test-matrix.md)

## Context
The request: record the final picture of a show while it plays, with resolution, frame rate, codec, quality and, where supported, alpha; use the FFmpeg setup that exists; never block the render thread (asynchronous encoder queues with back pressure); say what happens on dropped frames, sound and picture sync, file errors and cancelling; tests and benchmarks; low load on the processor, the graphics card and video memory so that the frame times stay stable next to VRChat. Offline rendering and render queues are a separate task (Prompt 36) that may only reuse the shared encoder. A large change of the architecture needs a maintainer decision.

## Findings (what existed before)
- **FFmpeg.** The libraries are loaded at run time (`ffmpegApi(problem)`), with a setup that downloads, checks and installs an LGPL build (five libraries), `videoSupport()` and `retryVideoSupport()` (`src/media/`). The loader already had the encoding calls (`avcodec_send_frame`, `avcodec_receive_packet`, the output format calls), used for the HAP copies of DXV videos in `hap_convert.cpp`: HAP only, offline, from a decoded file. Nothing encoded in real time or with another codec, and the loader needed no new function for this change.
- **Colour conversion.** `RgbaConverter` (swscale, `src/media/frame_convert.hpp`) existed and is reused.
- **Read back.** The NDI output (`src/render/ndi_output.cpp`) reads the picture back with two pixel buffers that are mapped one frame later, under the output memory budget. There was no read back of the composition for a file.
- **Audio.** `AudioSystem` hands the analysis a mono signal. It had no way to give the raw samples to anything else. In demo mode a built-in synthetic source feeds the analysis; that is not a device signal and is not given to the recorder.
- **Final picture.** The last step of the engine writes `vec4(rgb, 1.0)`. The composition has no meaningful alpha.
- **Gaps.** No recording, no encoder settings, no menu entry, no status in the status bar, no test that writes a video.

## Decision
1. **The render thread does one blit and one read back request, nothing else.** `Recorder::capture` blits the composition, flipped and scaled, into a recording target (`glBlitFramebuffer`, linear filter) and starts `glReadPixels` into one of three pixel buffers with a fence. The fences are polled with a timeout of zero in later frames. Only at stop the recorder waits for the buffers, at most 200 ms each. The program state that creation and the copy touch (read and draw framebuffer, texture binding, pack buffer, pack alignment, scissor) is saved and put back, because `RenderTarget::create` unbinds the framebuffer and the texture. A self test compares the state before and after every capture.
2. **One encoder thread at background priority** converts the colours (swscale), encodes, muxes, checks the free disk space once a second and handles the sound. It lives in `src/media/video_recorder.*`, which does not know OpenGL, takes numbered frames and can therefore be reused by the offline renderer of Prompt 36.
3. **A bounded frame pool is the back pressure.** The pool holds 5 to 10 pictures, from a budget of 256 MiB. A full pool, or three busy read buffers, means the new frame is dropped and counted; the render thread never waits. The queue cannot grow beyond the pool, which keeps the memory use fixed.
4. **The timeline follows the wall clock.** `FrameSchedule` gives every frame a slot of the frame rate grid from the time it was rendered. A second frame in one slot is skipped. A stall of the show leaves empty slots that are counted as repeated, and the file keeps the previous picture across the gap (a variable frame rate timeline) instead of squeezing the time away or inventing frames. Dropped frames (encoder too slow) and repeated slots (show too slow) are shown separately, because they have different causes.
5. **Sound follows the same clock.** `AudioSink` is a one method interface, `AudioSystem` pushes the mono samples of a real input or system audio to it, and `AudioSync` compares the samples written with the time passed: silence is inserted when the sound is more than 40 ms behind, samples are dropped when it is more than 40 ms ahead. The demo signal is not recorded. A format that cannot hold the sample rate leaves the sound out with a message.
6. **Failures end in a defined state.** A disk with less than 256 MiB left ends the recording cleanly (`RecordStop::DiskFull`, the file is finished). A write error or an encoder error ends it as failed with the partial file kept. Cancel deletes the file. MP4 and MOV are written fragmented with flushed packets by default (`crash safe`), so that a file survives a crash of the program or the computer up to a moment before; MKV and FFV1 cannot do that, and the page says so. Stopping or closing the program waits up to 10 seconds for the encoder.
7. **Nine formats and one quality scale.** H.264 (`libopenh264`, AAC), H.264 and HEVC on NVENC, Quick Sync or AMF (only probed, AAC), ProRes 422 HQ and 4444, HAP, HAP Alpha, HAP Q, Motion JPEG, FFV1 and VP9. The page probes each with a small test picture on a worker thread and lists only what opens. Quality sets the bit rate (width x height x frame rate x (0.02 + 0.20 x quality / 100), times 0.7 for HEVC and 0.8 for VP9, limited to 1 to 400 Mbit/s) or the quantiser of Motion JPEG. Colours are tagged BT.709 (Motion JPEG: BT.601, as the format needs).
8. **Limits.** At most 3840 x 2160 pixels and 7680 on one side, at least 16; HAP needs a multiple of 4. The frame rates are 24, 25, 30, 48, 50, 60, 90 and 120. If the output size changes while recording, the file keeps its size and the picture is scaled; the changes are counted.
9. **Alpha is not offered.** The encoders for ProRes 4444, HAP Alpha, FFV1 and VP9 take alpha and are tested with it, but the composition is always opaque, so the settings do not offer it. Making the engine hand out a composition with a meaningful alpha changes what Spout, NDI and the output windows receive (`kFinalBody`), which is a maintainer decision.
10. **Settings and shortcuts.** Settings, Recording (format, size, frame rate, quality, sound, folder, crash safe), menu Output, `Ctrl+Shift+R`, a `REC` indicator with the time and the dropped frames in the status bar. The stored values are checked and clamped when they are read (`findRecordProfile`, `checkRecordSettings`). 83 new texts in all seven languages.
11. **Two options for the program.** `--record-selftest` records a test picture through the real recorder and checks the frames at the encoder, `--bench-record [LIST[:FRAMES]]` records the engine output in each working format and reports capture time, encoder time, drops and bit rate. `RecorderTestHooks` (an encoder delay, a failing write after N bytes, a free-space threshold, a tap that sees every frame before the conversion) exist for the tests and are off in the program.

## Alternatives considered
- **Piping to an `ffmpeg` process.** It needs an executable that NyrexVJ does not ship and cannot control (errors, priority, cancelling), and the set-up page installs libraries, not a program.
- **A synchronous `glReadPixels` or `glGetTexImage`.** It stalls the render thread until the graphics card has finished, which is the one thing the request forbids.
- **Reading back at composition size and scaling on the processor.** More bytes through the bus and more processor time on a machine that VRChat is also using. The graphics card scales for free in the blit.
- **A constant frame rate by repeating pictures.** The file would claim pictures that were never rendered and hide that the show stalled. The held frame of a variable frame rate timeline is honest and has the right length.
- **Letting the queue grow and write it later.** Memory use would depend on the length of a stall. A fixed pool turns it into a counted drop.
- **More than one encoder thread.** Most formats do not parallelise per frame in a way that is worth the memory of more frames in flight, and the render thread of the show needs the cores. Left open until real hardware numbers exist.
- **An alpha path in the engine now.** See decision 9.
- **Recording after mapping (a single output).** The NDI output of an output already sends that picture; a second path with its own budget is not needed to meet this request.

## Consequences
- A show can be recorded without a second program, with a frame time cost that is one blit and one copy on the render thread; everything else moves to a thread that gives way to the show.
- A slow encoder costs frames in the file, not frames in the show. The numbers on the page tell which of the two happened.
- `src/media/video_recorder.*` is the part Prompt 36 can reuse. `src/render/recorder.*` is the OpenGL side and stays specific to the live recording.
- The memory for a recording is fixed by the pool and the three read buffers; it is shown in Settings, Recording.
- Windows has one more place where the build can differ (swscale and encoder names); CI builds it and runs `nvj_record_tests`, but never records on Windows.

## Tests
- `nvj_record_tests`: 82,219 checks: format table and settings check, pool size, file names, frame schedule (steady, stalls, jitter, surplus, prediction against decision), audio sync (steady, start offset, drift, finish), stored settings, and the encoder with real files in every format that opens here (frame count and order, colours after decoding, timestamps, sound length and sync, crash safe and normal files, cancel, full disk, write error, slow encoder, hardware encoders refused cleanly).
- `NyrexVJ --record-selftest`: 29 checks through a real OpenGL context (llvmpipe): orientation and colours exact, scaling down and up, state before and after every capture, a slow encoder against the render loop, a size change, a stall, cancel.
- `NyrexVJ --bench-record all:90`: every format that opens here ran to the end.
- AddressSanitizer and UndefinedBehaviorSanitizer on Linux: `nvj_record_tests` and `--record-selftest` without a finding. ThreadSanitizer: `nvj_record_tests` without a warning (the self test was not run under it). A timing check in the test that included the test's own picture fill failed once under ThreadSanitizer; it now times only the calls into the recorder. LeakSanitizer reports 43 KB at exit from the existing ALSA MIDI listing (`midi_input_linux.cpp`), also without a recording.
- The whole suite (31 tests) and `scripts/check_i18n.py` (1,641 keys) pass; the test matrix has the new area `recording` with three automatic and five manual tests.

## Not tested
A real graphics card (fences, read back and blit on real drivers), NVENC, Quick Sync and AMF (refused cleanly without a card), Windows at run time, an audio device over a long time, a drive that is unplugged while recording, a recording next to VRChat, 4K on real hardware, the Settings page and the menu by hand in a window, and the files in other players and editors (decoded with FFmpeg only). The measured times are from a software renderer. The manual tests are in the matrix and open.

## Validation / open questions
- Should the engine be able to hand out an alpha composition (and what do Spout, NDI and the outputs receive then)?
- Stereo sound needs a second path from the audio input.
- Which hardware encoder presets (low latency, B-frames) does a show want? It needs real cards.
- Is 256 MiB the right threshold for a full disk, and is the 256 MiB pool budget right for 4K?
- The pass criteria of the manual tests are proposals.

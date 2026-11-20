# ADR 0013: Media control: play head, reverse playback, exact seeking, in and out points, loop modes, beat sync and pre-rolled switching

- **Status:** Accepted (2026-11-20) as milestone 2 of [ADR 0012](0012-open-source-vj-platform-plan.md), implemented in v0.21.0. Issue [#48](https://github.com/MarlonXDev/NyrexVJ/issues/48).
- **Date:** 2026-11-20
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0010](0010-video-decode-pipeline.md), [ADR 0011](0011-render-pipeline-optimisation.md), [MEDIA.md](../MEDIA.md), [PERFORMANCE.md](../PERFORMANCE.md)

## Context
Before this change a video clip had one behaviour: it ran forward from its first frame and looped without a seam. The position followed the layer clock, the decoder thread had a short queue and a forward seek window, and every frame logic decision lived inside the decoder.

A VJ program needs more: clips that play backwards, a position that can be set by hand and by tempo, a part of a clip instead of the whole file, loop modes, and a switch from one clip to the next that never shows an empty frame. All of it has to stay cheap enough to run next to VRChat, work with the existing hardware, HAP and software routes, and must not change presets that were saved before.

Constraints that shaped the design:
- the render thread never waits for the decoder (ADR 0010),
- a clip that is not on a layer is released after a few seconds (ADR 0011), so keeping every clip warm is not an option,
- `media_` belongs to a clip (an index into the library), not to a cell, so a clip used in several layers shares one decoder,
- the code must compile with GCC 11 and the Windows toolchain, and logic should be testable without a graphics card or FFmpeg.

## Decision
1. **A pure play head** (`src/core/playhead.{hpp,cpp}`). `ClipPlayback` is the saved state of one cell: in point, out point, loop mode (loop, ping-pong, once and hold, once and stop), speed from -4 to +4, and an optional length in beats. `Playhead` turns a clock value (and the beat) into a position inside the range and says whether the clip is still visible. It has no dependency on FFmpeg or OpenGL and is covered by its own tests (`nvj_playhead_tests`, 101 checks): range edges, every loop mode forwards and backwards, speed changes, beat sync, and hostile values.
2. **One driver between clock and decoder** (`src/media/playback_driver.{hpp,cpp}`). It decides, per frame, which time to ask the decoder for and in which direction, and offers `scrubTo` and `stepFrames` for the clip panel. `MediaPlayer` only forwards to it.
3. **Decoder changes** (`VideoDecoder`):
   - `setRange(in, out)` limits looping to the range; positions are range relative.
   - Exact seeking on demand: seek to the key frame before the target, decode and drop up to it. The forward window logic of ADR 0010 stays for normal playback.
   - Reverse playback: the decoder decodes a short segment forward (it seeks to a key frame before the wanted time, decodes up to it), keeps the last N pictures in a ring and serves them in descending order. N is between 6 and 16 pictures and is chosen from the picture size so that the ring and the picture pool (three times N pictures) stay near 96 MiB; very large pictures keep the minimum of 6 and can exceed that figure. For the next segment it seeks again. The video monitor shows that a clip plays backwards and how many pictures are buffered.
   - HAP and other intra frame codecs have a key frame at every picture, so a reverse segment is one picture; long GOP codecs (typical H.264 and HEVC) cost more because a whole group of pictures has to be decoded for each segment.
   - If the decoder cannot keep up while reversing, the picture that is nearest and not newer than the wanted time stays on screen. It never blocks.
4. **Saving.** `Layer::playback[column]` holds one `ClipPlayback` per cell. A preset writes a sparse `"playback"` array only for cells that differ from the default, so old presets load unchanged and new presets without playback settings look like old ones. Values are clamped on load. `Layer::setCell` resets the playback state when a different clip is put into a cell, so settings never move to another clip.
5. **Clip panel.** The panel of the selected cell shows time and frame, a scrub bar, single frame steps, Play and Pause, In and Out with Set here, the loop mode, the speed with a Reverse button, and the clip length in beats. Everything goes through `tr()`.
6. **Switching without drops.** While no clip of a layer is changing, the video of the next non-empty column of every layer is opened and primed (first picture at the in point, or at the out point when the clip plays backwards) and kept for the normal release time. The setting is **Settings, Performance, Video playback, Prepare the next clip of every layer** (`prewarmNext`, default on). `--no-prewarm` turns it off for one run and `--bench-switch SECONDS:clip1,clip2,...` measures switches (blank frames per switch, time to the first picture, memory).

## Alternatives
- **Keep the logic in the decoder thread.** Rejected: untestable without FFmpeg and hard to reason about across reverse, ping-pong and beat sync.
- **Decode a whole clip to RAM for reverse and scrubbing.** Rejected: 1080p frames are 8 MB each, so a minute of video is far beyond any sensible budget next to VRChat. A bounded ring has a fixed ceiling.
- **Transcode to an intra frame format on import.** Rejected for now: it is a large change (disk, time, quality) and HAP already covers it for users who want it. It stays a documented recommendation for heavy reverse use.
- **Playback state per layer instead of per cell.** Rejected: a cue per cell is what a VJ expects (the same clip with a different loop on another column), and the state is small.
- **Keep next clips warm for every column.** Rejected: it would cost video memory for clips that may never play. Only one clip per layer, the one that most likely comes next, is prepared.
- **A separate decoder per use of a clip.** Rejected for this milestone: it doubles decoder threads and memory for the sake of a case that is rare in practice. The shared decoder is a documented limit.

## Consequences
- Presets and saved settings are backward compatible. Playback state of existing presets is the default (forward, whole clip, loop).
- The ring and its picture pool aim at about 96 MiB while a clip plays backwards (more for 4K, where the minimum of 6 pictures applies). Long GOP video costs noticeably more processor time in reverse.
- Preparing the next clip costs about 14.5 MB of video memory per prepared 1080p clip, at most one per layer, plus its decoder thread. The setting can turn it off.
- A clip that is used in several layers shares one decoder. Each layer asks it in turn with its own clock and settings, so different positions or directions of the same clip in two layers are not supported (the pictures can jump). A copy of the file in the library is the workaround.
- Ping-pong and reverse loops restart the reverse segment at the turning point. On a slow decoder a short stutter can show there.
- Timecode locked sync (milestone 4) can drive the same driver, because position comes from a clock value, not from the decoder.

## Validation
Measured with `--bench-switch 1:big1,...,big8` (eight 1080p H.264 clips, 16 switches, Xvfb with llvmpipe, FFmpeg 6.1, software decoding):

| | Switches with an empty frame | Empty frames per switch (average, maximum) | Time to the first picture (average, maximum) | Estimated video memory |
| --- | ---: | ---: | ---: | ---: |
| Prepare next clip off | 11 of 16 | 1.06, 2 | 47.9 ms, 111.2 ms | 103.8 MB |
| Prepare next clip on | 0 of 16 | 0, 0 | in the first frame | 118.3 MB |

Other checks: the unit tests of the play head and the persistence, `nvj_media_tests` with the index fixtures (`tests/data/test_index_h264.mp4`, `tests/data/test_index_mjpeg.avi`, each picture carries its number in its brightness, so a test can tell exactly which picture was shown: forward, reverse, exact seek, range, ping-pong, once and hold, once and stop, a seek that is one picture off), an end to end run with software OpenGL (hold and stop behave as set, the clip panel renders), `--video-selftest tests/data`, and compiling the changed files with GCC 11 and MinGW.

Not validated, because the hardware was not available: hardware decoding in reverse, real graphics card timings, the effect on VRChat frame rates, Windows at run time, and long GOP clips of several minutes in reverse. Please report measurements from real computers.

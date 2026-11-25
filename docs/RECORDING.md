# Recording the final picture

NyrexVJ can record the final picture of the show into a video file while it plays. This page says what is recorded, how to start it, which formats exist, how the recording is kept from slowing the show down, what happens when something goes wrong, what was measured and what was not tested.

Rendering a show offline, frame by frame and faster or slower than real time, and a queue of such renders are not part of this page. They belong to a separate task (Prompt 36) and are not built. What is built here is the live recording, and its encoder part (`src/media/video_recorder.*`) does not touch OpenGL, so that task can reuse it.

## What is recorded

The picture that the engine renders for the composition: the same picture that goes to the preview, to Spout, to NDI and, before slices, masks, warping and edge blending, to the output windows. It is the picture at the resolution of the composition (Settings, Output, Quality), scaled to the size you choose for the file. What a single output window shows after mapping is not recorded. Use the NDI of an output or capture the window if you need that.

The final picture is always opaque (the last step of the engine writes alpha 1), so the recording has no transparency. The encoders for transparent formats are built and tested, but the settings do not offer transparency, because there is nothing to put into it. Whether the engine should be able to hand out a transparent composition is open (see For the maintainer).

## Start and stop

- Menu Output, Start recording, or `Ctrl+Shift+R`. The same keys stop it again.
- Settings, Recording has the format, the size, the frame rate, the quality, the sound, the folder and the buttons Start recording, Stop recording and Cancel and delete, and shows live numbers while it records.
- The status bar shows `REC`, the recorded time and, in orange, the number of dropped frames.
- Stop saves the file. Closing the program while it records saves the file first and waits up to 10 seconds for the encoder to finish.
- Cancel and delete stops at once and deletes the file.

Files are named `NyrexVJ_2026-11-25_20-15-00.mp4` (date and time of the start, extension of the format). A file name that exists gets `_2`, `_3` and so on. The default folder is `Recordings` inside the NyrexVJ data folder. The folder is created if it does not exist; Open folder opens it and Default goes back to the standard folder.

## Formats

FFmpeg does the encoding (see [FFMPEG.md](FFMPEG.md)). The program tries every format with a small test picture on a worker thread when the page is opened and lists only those that work on your computer. Check again repeats that after you installed or changed FFmpeg.

| Format | File | Sound | Quality setting | Good for |
| --- | --- | --- | --- | --- |
| H.264 (`libopenh264`) | `.mp4` | AAC | yes | Sharing, small files, the processor does the work |
| H.264 on the graphics card (`h264_nvenc`, `h264_qsv`, `h264_amf`) | `.mp4` | AAC | yes | Like H.264, the graphics card does the work |
| HEVC on the graphics card (`hevc_nvenc`, `hevc_qsv`, `hevc_amf`) | `.mp4` | AAC | yes | Smaller files than H.264 |
| ProRes 422 HQ (`prores_ks`) | `.mov` | PCM | fixed | Editing programs |
| ProRes 4444 (`prores_ks`) | `.mov` | PCM | fixed | The best ProRes quality |
| HAP, HAP Alpha, HAP Q (`hap`) | `.mov` | PCM | fixed | VJ programs, very light to play back |
| Motion JPEG (`mjpeg`) | `.mov` | PCM | yes | Simple and robust |
| FFV1 (`ffv1`) | `.mkv` | PCM | lossless | Archives |
| VP9 (`libvpx-vp9`) | `.mkv` | AAC | yes | The web, slow to encode |

Quality sets the bit rate of H.264, HEVC and VP9: width x height x frame rate x (0.02 + 0.20 x quality / 100), times 0.7 for HEVC and 0.8 for VP9, between 1 and 400 Mbit/s. For Motion JPEG it sets the quantiser. The other formats have a fixed quality.

The colours are tagged as BT.709 (Motion JPEG: BT.601, as the JFIF format requires), so a player shows what the preview shows. HAP needs a picture size that is a multiple of 4, every other format a multiple of 2; the size is rounded down.

The hardware encoders could not be tried here (no graphics card). They are probed like the others and listed only if they open. See Not tested.

## Size, frame rate and limits

- **Size:** same as the output, 1280 x 720, 1920 x 1080, 2560 x 1440, 3840 x 2160 or a custom size. At most 3840 x 2160 pixels in total and 7680 on one side, at least 16. The picture is scaled on the graphics card with linear filtering; a size equal to the composition is copied one to one. If the output size changes during the recording (Settings, Output, Quality), the file keeps its size and the picture is scaled, and the number of changes is shown.
- **Frame rate:** 24, 25, 30, 48, 50, 60, 90 or 120. This is the grid of the timeline in the file. The show itself keeps its own speed.
- **Memory:** one recording needs a pool of 5 to 10 pictures of the file size in main memory (as many as fit into 256 MiB), plus on the graphics side one picture for the scaled copy and three read buffers. Settings, Recording shows the total for the chosen size (pool plus four pictures): at 1920 x 1080 about 110 MiB (a pool of 10), at 3840 x 2160 about 380 MiB (a pool of 8). The recording does not count against the output memory budget of 384 MiB (see [OUTPUTS.md](OUTPUTS.md)).

## How the recording stays out of the way of the show

1. **On the render thread only a copy is made.** After the engine has rendered, one blit scales and flips the picture into a recording target, and `glReadPixels` copies that into one of three pixel buffers. A fence marks the copy. Nothing waits for the graphics card; the fence is polled with a timeout of zero in the next frames. The program state that it touches (framebuffer bindings, pack buffer, pack alignment, scissor) is put back, and the self test checks that.
2. **Everything slow runs on another thread.** The finished buffer is copied into a frame from a bounded pool and handed to one encoder thread that runs at background priority. That thread converts the colours (swscale), encodes, writes the file and checks the free disk space once a second.
3. **No waiting, only dropping.** If the pool is full because the encoder is slower than the show, the new frame is dropped and counted, and the render thread does not wait. The queue can never be longer than the pool. The same holds when all three read buffers are still busy.
4. **The file keeps real time.** Every frame gets a slot of the frame rate grid from the time it was rendered. A second frame in a slot is skipped. If the show stalls, the slots in between stay empty and the file shows the previous picture during the gap (a variable frame rate timeline, not duplicated frames), so the video is as long as the real time and sound stays in sync. The numbers Repeated frames (slots without a new picture) and Dropped frames (pictures lost because the encoder or the read buffers could not keep up) are shown separately, because the first is a slow show and the second a slow encoder.

Stop waits for the three read buffers (at most 200 ms each), lets the encoder empty its queue and writes the end of the file.

## Sound

The sound comes from the audio source in Settings, Audio, as the mono signal that the beat analysis also uses, and only when that source is a real input or system audio. The demo signal is not recorded, and a recording without a real source has no sound track (a note says so). The sample rate of the source is used for the file; if a format cannot hold that rate, the sound is left out and the note says why.

Sound and picture follow the wall clock. The encoder thread compares the number of samples it wrote with the time that has passed, inserts silence when the sound is more than 40 ms behind and drops samples when it is more than 40 ms ahead, so a stalled or restarted audio device does not make the file drift. The numbers Sound recorded and Sound corrected show how much was added or dropped. The audio buffer holds about 11 seconds; if the encoder falls behind further, samples are lost and counted.

## When something goes wrong

| Situation | What happens |
| --- | --- |
| Less than 256 MiB free on the disk | The recording ends cleanly, the file is finished and plays, and the message says that the disk is almost full |
| The file cannot be written (disk error, drive removed) | The recording stops with the message and the partial file stays |
| The encoder or the colour conversion fails | The same, with the text of FFmpeg in the log |
| The folder cannot be created or no file name is free | Nothing starts, and the message says why |
| FFmpeg is missing or no encoder opens | Nothing starts. Settings, Recording points to the FFmpeg setup |
| The program or the computer stops during the recording | With Keep the file readable after a crash (on by default, for MP4 and MOV), the file is written in fragments that are flushed to the disk, so what was recorded up to a moment before can be played. MKV and the option switched off leave a file that many players cannot open |
| Cancel and delete | The file is deleted |

The show is never stopped by a recording problem, and the render thread never waits for the encoder.

## Measurements

All numbers come from the software renderer (Mesa llvmpipe) of a Linux container with two cores, so they say nothing about a graphics card and only show the relative cost. `NyrexVJ --bench-record all:90` records 90 frames of the look "Neon Club" at 1920 x 1080 in every format that works, 60 frames per second grid, quality 70, and compares the frame time with a run without recording (173.8 ms per frame, the software renderer manages about 6 frames per second here).

| Format | Frame time with recording | Capture call on the render thread (avg / max) | Encoder per frame (avg / max) | Dropped | Bit rate |
| --- | --- | --- | --- | --- | --- |
| H.264 (`libopenh264`) | 199.9 ms | 32.1 / 61.6 ms | 53 / 123 ms | 0 | 1.7 Mbit/s |
| ProRes 422 HQ | 262.0 ms | 27.9 / 75.4 ms | 300 / 579 ms | 13 of 90 | 22.2 Mbit/s |
| ProRes 4444 | 252.4 ms | 20.7 / 45.3 ms | 414 / 790 ms | 34 of 90 | 23.8 Mbit/s |
| HAP | 202.9 ms | 32.7 / 54.3 ms | 82 / 190 ms | 0 | 24.2 Mbit/s |
| HAP Alpha | 209.7 ms | 33.7 / 53.9 ms | 77 / 198 ms | 0 | 28.3 Mbit/s |
| HAP Q | 207.2 ms | 32.3 / 54.7 ms | 115 / 250 ms | 0 | 49.1 Mbit/s |
| Motion JPEG | 193.3 ms | 30.4 / 43.5 ms | 38 / 84 ms | 0 | 7.1 Mbit/s |
| FFV1 | 231.9 ms | 29.5 / 52.2 ms | 197 / 604 ms | 0 | 27.6 Mbit/s |
| VP9 | 220.2 ms | 31.7 / 54.2 ms | 149 / 425 ms | 0 | 8.0 Mbit/s |

How to read this:

- The capture call takes about 30 ms here because a software renderer does the blit and the copy itself, on the same two cores. On a graphics card the call only queues the work. Nothing in these numbers shows the cost on real hardware.
- The two processor formats that are slower than the show (ProRes) show the backpressure at work: frames are dropped and counted, the queue stays at the pool size, the show keeps rendering. The frame time rises more than for the other formats because the encoder thread and the software renderer share two cores.
- The bit rates are those of a nearly static look at six frames per second with held frames in between; they are not what a real show produces.

The self test measures the part that does not depend on the graphics card: 90 frames against an encoder that needs 250 ms each pass the render loop in about 7 ms in total, the slowest capture call is below 1 ms, and about 80 frames are dropped and counted.

## What is tested, and what is not

Tested automatically:

- `nvj_record_tests` (about 82,000 checks): the format table and the settings check, the pool size, the file names, the frame schedule (steady rates, stalls, jitter, surplus frames, and that the question whether a frame will be taken always agrees with the decision), the audio sync (steady, start offset, drift, finish), the stored settings, and the encoder itself with real files in every format that opens here: frame count and order, picture colours after decoding, timestamps, sound length and sync, crash-safe and normal files, cancel, a full disk, a write error, a slow encoder and the refusal of the hardware encoders without a card.
- `NyrexVJ --record-selftest` (runs in CI on Linux with software OpenGL, 29 checks): the recording on the graphics side. A picture with four coloured quadrants and a marker per frame is recorded; the frames that reach the encoder are checked pixel by pixel for orientation, order and colour, at the same size and scaled down and up, the OpenGL state is compared before and after every capture, a slow encoder must not block the render loop and drops must be counted, a change of the output size and a stall of the show are recorded, and a cancelled recording deletes its file.
- Once by hand, with the `ffmpeg` command line tool on a busy 1080p, 60 frames per second test picture: the H.264 encoder reaches the asked bit rate (19.9 Mbit/s asked, 20.1 delivered). This is not part of the test suite.
- The record tests and the self test were also run under AddressSanitizer and UndefinedBehaviorSanitizer, and the record tests under ThreadSanitizer, without a finding. The self test was not run under ThreadSanitizer.

Not tested: any real graphics card (so the fences, the read buffers and the cost of the blit on real drivers), NVENC, Quick Sync and AMF (they only fail cleanly here), Windows at run time (CI builds it and runs `nvj_record_tests`, but the recording was never started on Windows), an audio device that runs for a long time (the sync was tested with generated sound, not with a real device for an hour), drives that are removed while recording, a recording next to VRChat, 4K recordings on real hardware, the settings page and the menu by hand in a window (compiled and run through the code paths of the tests, not clicked through), and the files in other players and editors (they were decoded with FFmpeg only). The manual tests are listed in `tests/hardware/matrix.json` (`M-RECORD-1` to `M-RECORD-5`) and are open.

## For the maintainer

- **Transparency:** to record with alpha, the engine would have to render the composition with an alpha that means something and not write 1 at the end (`kFinalBody`). That changes what Spout, NDI and the output windows receive and is not done. The recording code is ready for it (ProRes 4444, HAP Alpha, FFV1 and VP9 handle alpha and are tested with it).
- **Offline rendering and render queues:** not built, separate task. `VideoRecorder` takes numbered frames and has no OpenGL, which is the part such a task can share.
- **Stereo sound:** the analysis hands out a mono signal. A stereo recording needs a second path from the audio input.
- **Recording one output with its mapping:** not built. NDI of an output already sends that picture.
- **Hardware encoders:** no settings beyond quality. Which presets (low latency, B-frames) a show wants has to be decided with real cards.
- **Disk space threshold:** 256 MiB is a proposal.

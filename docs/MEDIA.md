# Image and video clips

Pictures and videos are clips like any other: they sit in the Library, go into any cell, take effects, blend modes, the grade, speed and scale, and react to the audio.

## Where the files go

Put the files into the `media` folder (Library, Files, Media folder opens it) or drag them onto the NyrexVJ window. Dragging copies the files into the media folder in the background, up to 256 MiB each, and the Library finds them at once. A second file with the same name but other content gets a number, `name (2).png`; the same file twice is only added once.

| Kind | File types | Clip name |
|------|------------|-----------|
| Image | `.png` `.jpg` `.jpeg` `.bmp` `.tga` `.gif` (first frame) | `Image: <file name without the ending>` |
| Video | `.mp4` `.mov` `.m4v` `.mkv` `.avi` `.webm` | `Video: <file name without the ending>` |

At most 128 media clips are listed. Presets store the clip name, so a preset keeps working when the file is still in the media folder under the same name. A missing file shows an empty clip and a message in the Clip window; nothing else is disturbed.

## Transparency

Images with an alpha channel (PNG, TGA) keep it. Every layer is composited with premultiplied alpha, so a logo with soft edges lies over the layers below it without a halo. The same holds for HAP Alpha and other videos that carry alpha. All older clips, scenes and effects produce a fully opaque picture and render exactly as before: all 18 factory looks were compared pixel by pixel against v0.12.0 and are identical.

The thumbnails in the Library show transparent areas as a checkerboard.

The picture is fitted into the layer with its own aspect ratio (nothing is stretched, empty areas are transparent), like a Spout input. The Scale slider of the Clip window zooms it.

## Video

Video is decoded with FFmpeg on a worker thread. The decoder never blocks the render loop: if a frame is late, the previous one stays on screen.

- Codecs: whatever your FFmpeg build can decode. Tested: H.264 and HEVC (also 10 bit) in `.mp4`, AV1, ProRes with alpha, HAP, HAP Alpha and HAP Q in `.mov`.
- The video loops without dropping frames at the seam. The position follows the layer clock, so Speed, Tap, Pause and the BPM move it like everything else.
- The picture that reaches the effects is at most 1920 pixels in its longer side. Larger videos are decoded in full and scaled down, on the graphics card when the colour conversion runs there.
- A video that is not on a layer for a few seconds is released; an image after two minutes. Both load again when needed.
- Sound in videos is ignored.

### How a video gets to the screen

NyrexVJ picks the cheapest route that works and falls back by itself, so a video always plays:

| Video | Route | What the processor does |
| --- | --- | --- |
| HAP, HAP Alpha, HAP Q (and chunked or uncompressed HAP) | The DXT blocks are read from the file, unpacked from Snappy where needed and uploaded to the graphics card as a compressed texture. HAP Q is converted from YCoCg in the media shader. | Almost nothing: file read, Snappy and one copy into the upload buffer. |
| H.264, HEVC, AV1, VP9, ProRes and other formats, hardware decoding available | The graphics card decodes, the picture is copied back as NV12 or P010 and converted to colours on the graphics card. | Almost nothing. |
| The same, no hardware decoder | The processor decodes. The raw colour planes (8 to 16 bit, with an alpha plane when the video has one) go to the graphics card, which converts them with the right matrix (601, 709 or 2020, limited or full range). | Decoding only. No colour conversion, no scaling. |
| Anything the shaders do not know, or when you turn the conversion off | FFmpeg converts to RGBA on the processor and the picture is uploaded as before. | Decoding, conversion and scaling. |

HAP needs a graphics card with S3TC (DXT) textures. NyrexVJ checks this at start with a real test upload. Without it, or when a HAP file uses something unusual, FFmpeg's own HAP decoder takes over on the processor and the monitor says so.

### Hardware decoding

The program asks FFmpeg for the graphics card's decoder and tests every interface once with a tiny built-in clip for H.264, HEVC and AV1. The order is Windows: Direct3D 11 (D3D11VA), DXVA2, NVIDIA CUDA, Intel QSV. Linux: NVIDIA CUDA (NVDEC), VA-API, Intel QSV, VDPAU. Settings, Performance, Video playback shows the result as a table (H.264, HEVC and AV1 per interface: works, not supported or failed, with the reason as a tooltip; VP9 has no test clip and is only tried when a VP9 video plays) and a Test hardware decoding button.

Settings, Performance, Decoder:

- **Automatic**: large videos (H.264, HEVC, AV1, VP9 at 1280 x 720 or more) use the graphics card, small ones stay on the processor because the copy back costs more than it saves. This is the default.
- **Prefer hardware**: always try the graphics card first.
- **Force software**: never use the graphics card for decoding, for comparing or when a driver misbehaves.

If a hardware decoder fails for a video (unsupported profile, driver error, no free decoder session), the processor takes over and the reason is shown. This also holds when the decoder stops in the middle of playback: the video restarts on the processor after a short gap. That last case could not be tried without a real graphics card. Which graphics cards decode which codec is decided by the card and the driver, so there is no fixed list: the table on the Performance page is the compatibility matrix of your computer. As a rule of thumb, H.264 and HEVC work on most NVIDIA, AMD and Intel cards of the last ten years, VP9 on newer ones, and AV1 on NVIDIA RTX 30 and newer, AMD RX 6000 and newer and Intel Arc and 11th generation and newer. This comes from the vendors' feature lists, it was not tested here.

The decoded picture is copied back to normal memory and uploaded again. A direct hand-over of the decoded picture to OpenGL (zero copy) is not implemented, see [ADR 0010](adr/0010-video-decode-pipeline.md) for why.

### Monitoring

Settings, Performance, Videos lists every playing video with:

- the codec, size and picture format, the decoder in use (processor or the hardware interface) and the number of decoder threads,
- the route to the graphics card (compressed HAP upload, colour planes converted on the graphics card, or plain RGBA) and, when a faster route was not possible, the reason, for example "no hardware decoder worked on this computer" or "this graphics card cannot take compressed textures",
- the decode time per picture, the upload time per picture, dropped pictures (skipped to keep up) and late pictures (not ready in time),
- the video memory, split into pictures, colour planes, compressed HAP textures and upload buffers.

The Clip window of a video and the tooltip of its Library entry show the same decoder and route for that clip. Settings, Performance, Memory explains each memory number.

Video memory is an estimate computed from the texture sizes NyrexVJ creates. It does not include what the driver adds. The upload buffers are counted as video memory although drivers often keep them in normal memory. The decoder's own pictures inside the graphics card (hardware decoding) are not counted.

### Frame rate limit

Settings, Performance, Frame rate limit: 30, 60, 90, 120, 144 or Unlimited, independent of VSync. The default is 60. After each frame the limiter sleeps until shortly before the next deadline and finishes with a very short precise wait, so the processor is idle in between and the frame times stay even. A frame that took too long does not make the next ones shorter. It is the biggest saving next to VRChat: a limit of 60 on a 144 Hz monitor stops NyrexVJ from drawing 144 frames a second that nobody needs. With VSync on, the limiter only acts when the monitor is faster than the limit.

## Measured effect

Measured in the test environment: Linux, Xvfb with the software rasteriser (llvmpipe) and two processor cores, FFmpeg 6.1, 1920 x 1080 output, eight seconds per scenario, no graphics card. Because the rasteriser runs on the processor too, the column "without rasteriser" is the one that shows what NyrexVJ's own video work costs: it is the processor time of everything except the software OpenGL driver, in percent of one core.

| Scenario | Processor before | Processor now | Video memory before | Video memory now |
| --- | ---: | ---: | ---: | ---: |
| 1x H.264 1080p | 23.9 % | 16.3 % | 7.9 MB | 13.8 MB |
| 4x H.264 1080p | 76.2 % | 52.0 % | 31.6 MB | 55.4 MB |
| 1x H.264 4K | 56.7 % | 43.4 % | 7.9 MB | 31.6 MB |
| 1x HEVC 1080p | 29.6 % | 24.9 % | 7.9 MB | 13.8 MB |
| 1x AV1 1080p | 25.8 % | 18.8 % | 7.9 MB | 13.8 MB |
| 1x HAP 1080p | 26.3 % | 5.7 % | 7.9 MB | 2.0 MB |
| 1x HAP Alpha 1080p | 24.7 % | 7.0 % | 7.9 MB | 4.0 MB |
| 1x HAP Q 1080p | 52.5 % | 7.1 % | 7.9 MB | 4.0 MB |
| 1x HAP 4K | 104.9 % | 9.9 % | 7.9 MB | 7.9 MB |
| 4x HAP 1080p | 97.5 % | 9.5 % | 31.6 MB | 7.9 MB |
| 2x H.264 1080p + 1x H.264 4K | 93.0 % | 51.4 % | 23.7 MB | 59.3 MB |

What this shows and what it does not:

- HAP got much cheaper on the processor and uses less video memory, because the compressed blocks stay compressed up to the graphics card.
- Software decoded video costs less processor time because the colour conversion and scaling moved to the graphics card, but it needs more video memory, because the colour planes and one upload buffer exist next to the picture. If video memory is tight, turn off Colours converted on the graphics card or use HAP.
- Hardware decoding could not be measured here, there was no graphics card. It moves the decoding itself off the processor, but the copy back and the upload remain.
- Nothing was measured in VR or next to VRChat. The numbers say how much processor time the video part needs, not what it does to your frame rate in VRChat. Measure that on your computer with the monitor on the Performance page.

Repeat the measurement with `python3 scripts/bench_video.py --exe <NyrexVJ> --videos <folder with test videos> --seconds 8`, which writes the table above and can write JSON with `--json`. The scenarios look for files named like `h264_1080.mp4`, `h264_4k.mp4`, `hevc_1080.mp4`, `av1_1080.mp4`, `hap_1080.mov`, `hapalpha_1080.mov`, `hapq_1080.mov` and `hap_4k.mov`.

### FFmpeg is not part of NyrexVJ

NyrexVJ does not contain FFmpeg and does not link it. It loads the FFmpeg libraries at run time, so the program starts and works without them and only the video clips are unavailable. This keeps NyrexVJ's MIT licence untouched, because the libraries stay separate files that you control.

On Windows get a **shared LGPL build of FFmpeg 8.1** (for example the `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` asset of the BtbN FFmpeg-Builds project on GitHub) and copy these files from its `bin` folder into the `ffmpeg` folder inside the NyrexVJ data folder (the Clip window has an Open FFmpeg folder button that shows the exact folder and the exact file names), or next to `NyrexVJ.exe`:

```
avutil-60.dll  swscale-9.dll  avcodec-62.dll  avformat-62.dll  swresample-6.dll
```

The library version in each name must match the headers NyrexVJ was built with, which is why the FFmpeg release is fixed. The Clip window always shows the exact names this NyrexVJ was built for.

The Clip window of a video clip lists the files and has a Check again button, so no restart is needed. On Linux the files are searched in `NYREXVJ_FFMPEG_DIR` (when set), the `ffmpeg` folder of the user data folder, an `ffmpeg` folder next to the program, the folder of the program and finally the system library path. Either copy the `.so` files of an FFmpeg 8.x LGPL shared build into one of those folders, or install the FFmpeg libraries of your distribution when its version numbers match the list in the Clip window (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`); the program uses the library versions it was built against.

Whoever distributes NyrexVJ together with the FFmpeg libraries has to follow the LGPL for them (notice, source offer, replaceable libraries). The official NyrexVJ downloads do not contain them.

## For developers

- Building with video support needs only the FFmpeg **headers** (`-DNYREXVJ_FFMPEG_ROOT=<folder with include/>`, or the system headers on Linux). Without them the build still works and the Clip window says that video support was left out. `-DNYREXVJ_VIDEO=OFF` leaves it out on purpose. The Windows and Linux build workflows download the 8.1 headers and libraries; if that fails the build continues without video.
- `src/media/ffmpeg_api.hpp` lists the 40 FFmpeg functions that are used (the hardware decoding ones included). `src/media/ffmpeg_dyn.cpp` loads them (`dlopen` / `LoadLibraryExW`) and refuses to run when one is missing.
- `NYREXVJ_NO_FFMPEG=1` makes the libraries count as missing (to try the error path). `NYREXVJ_REQUIRE_VIDEO=1` makes `nvj_media_tests` fail instead of skipping when video decoding is unavailable.
- Pipeline code: `src/media/video_decoder.cpp` (decoder thread, hardware probe and fallback), `src/media/video_hardware.cpp` (probe of the hardware interfaces), `src/media/hap.cpp` (HAP container parser), `src/render/video_gpu.cpp` (upload and colour conversion on the graphics card), `src/core/frame_pacer.cpp` (frame rate limit), `src/app/ui_video.cpp` (settings and monitor).
- Command line for tests and measurements: `--bench-video SECONDS:clip1,clip2` measures the processor cost of videos in real time, `--video-selftest DIR` uploads every clip of a folder through the graphics card path and compares it with the processor path (exit code 1 on a difference), `--video-mode auto|hardware|software`, `--no-hap-direct`, `--no-gpu-convert` and `--frame-report SECONDS` (frame time and sleep time statistics with the limiter).
- `tests/test_media.cpp` covers the file name rules, premultiplication, image loading with alpha, errors, the library scan, the import of dropped files and, when the libraries are present, H.264 and HAP decoding, seeking, looping without lost frames, alpha, opening and closing many decoders, the HAP parser (Snappy, chunks, uncompressed, HAP Q, damaged files), the choice of decoder and the fallback reasons.

## What was and was not tested

Tested on Linux with FFmpeg 6.1 and software OpenGL (llvmpipe): everything above except hardware decoding, including the thumbnails, the transparency over other layers, the real OpenGL upload of every test clip (HAP, HAP Alpha, HAP Q, chunked and uncompressed HAP, 8 bit and 10 bit H.264 and HEVC, ProRes with alpha) compared with the processor path (`NyrexVJ --video-selftest tests/data`), looping and seeking, and several videos at once in 1080p and 4K. On Windows (MSVC, the build workflow) with the FFmpeg 8.1 headers and DLLs: the build and the decode tests. The Windows-only code was also compiled with MinGW as a check.

Not tested, because it needs hardware that was not available: hardware decoding on a real graphics card (D3D11VA, DXVA2, NVDEC/CUDA, VA-API, QSV), real graphics card frame times, starting NyrexVJ itself on Windows with video clips, and anything in VR or next to VRChat. The code for these paths is written against the FFmpeg interfaces and falls back to the processor when something fails, but expect to need corrections after the first run on real hardware, and please report them.

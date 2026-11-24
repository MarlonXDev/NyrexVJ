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

### Colour spaces, bit depth and HDR

The shader that converts the colour planes on the graphics card reads the matrix and the range from the stream and handles these cases:

| Property | Handled | How |
| --- | --- | --- |
| Matrix | BT.601 (SMPTE 170M, BT.470 BG), BT.709, BT.2020 (non constant and constant luminance, both as non constant), YCoCg | From the colour space tag. Without a tag a picture of 720 lines or more is 709 and a smaller one 601. SMPTE 240M has no matrix of its own and uses that height rule. |
| Range | Limited (16 to 235) and full | From the range tag of the stream. |
| Bit depth | 8 to 16 bit, also most significant bit aligned samples (P010) | Planes of 8 or 16 bit are uploaded as they are and scaled in the shader. |
| Chroma | 4:2:0, 4:2:2, 4:4:4, packed UYVY | The sampler interpolates the chroma planes bilinearly. Chroma siting (left, centre, top left) is not read. |
| Alpha | A fourth plane (for example ProRes 4444, yuva420p) and HAP Alpha | Kept and composited with premultiplied alpha like every layer, see Transparency. |
| HDR (PQ, HLG) | **Detected, not converted** | The picture is shown with the matrix and range of the file as an ordinary picture, without tone mapping, so it looks flat. The monitor, the Clip window and the Library tooltip say `HDR video (PQ)` or `HDR video (HLG)` with that note. Tone mapping belongs to colour management, which is a request of its own. |

How well this matches FFmpeg's own conversion was measured with test clips made for each case (mean difference per colour channel of 255 over the picture, one run of `nvj_media_tests` with software OpenGL): BT.601 limited 0.75 to 0.77, BT.709 full 0.60, 4:2:2 10 bit 1.0 to 1.07, and 3.2 to 3.6 for the BT.2020 10 bit clips in 4:2:0 (the PQ and HLG test clips are BT.2020 10 bit and give the same numbers). The 4:4:4 clips (8 bit BT.709 and 10 bit BT.2020) differ by 0.002, so matrix and range are exact; the 3 of 255 for 10 bit 4:2:0 is the chroma filter (bilinear on the graphics card against FFmpeg's), not the matrix. Every test also requires that the wrong matrix gives a difference at least 1.5 times larger, so a clip that cannot tell the matrices apart fails instead of passing.

### Hardware decoding

The program asks FFmpeg for the graphics card's decoder and tests every interface once with a tiny built-in clip for H.264, HEVC and AV1. The order is Windows: Direct3D 11 (D3D11VA), DXVA2, NVIDIA CUDA, Intel QSV. Linux: NVIDIA CUDA (NVDEC), VA-API, Intel QSV, VDPAU. Settings, Performance, Video playback shows the result as a table (H.264, HEVC and AV1 per interface: works, not supported or failed, with the reason as a tooltip; VP9 has no test clip and is only tried when a VP9 video plays) and a Test hardware decoding button.

Settings, Performance, Decoder:

- **Automatic**: large videos (H.264, HEVC, AV1, VP9 at 1280 x 720 or more) use the graphics card, small ones stay on the processor because the copy back costs more than it saves. This is the default.
- **Prefer hardware**: always try the graphics card first.
- **Force software**: never use the graphics card for decoding, for comparing or when a driver misbehaves.

If a hardware decoder fails for a video (unsupported profile, driver error, no free decoder session), the processor takes over and the reason is shown. This also holds when the decoder stops in the middle of playback: the video restarts on the processor after a short gap. That last case could not be tried without a real graphics card. Which graphics cards decode which codec is decided by the card and the driver, so there is no fixed list: the table on the Performance page is the compatibility matrix of your computer. As a rule of thumb, H.264 and HEVC work on most NVIDIA, AMD and Intel cards of the last ten years, VP9 on newer ones, and AV1 on NVIDIA RTX 30 and newer, AMD RX 6000 and newer and Intel Arc and 11th generation and newer. This comes from the vendors' feature lists, it was not tested here.

Codecs that none of these interfaces decode (DXV, ProRes, DNxHD/DNxHR and CineForm) are never tried on the graphics card, not even with Prefer hardware. They are decoded on the processor, and with Prefer hardware the clip says so ("this is normal") instead of listing a failure for every interface.

### DXV clips (Resolume)

DXV is decoded by FFmpeg's own DXV decoder on the processor. It is not decoded by the graphics card and it is not uploaded as a compressed texture like HAP: the format has no public specification, the only open code is FFmpeg's (LGPL) and cannot be copied into this MIT licensed program, and the processor path is cheap (see [ADR 0019](adr/0019-dxv-software-codecs.md) for the reasoning and the sources). Variants: Normal Quality (DXT1, and DXT5 with alpha) arrives as RGBA, High Quality (and High Quality with alpha) arrives as planar YUV in the YCoCg colour space and is converted on the graphics card with its own matrix. To get the lowest processor load NyrexVJ can make a HAP copy of a DXV video itself, see the next section. This was tried with one real DXV file (Normal Quality, no alpha); please report wrong colours or a missing alpha channel for the other variants.

#### Converting DXV videos to HAP

DXV is decoded on the processor. HAP is not: its compressed blocks go to the graphics card as they are. A HAP copy of a DXV video therefore costs much less processor time and, in video memory, 0.5 byte per pixel (HAP) or 1 byte (HAP Alpha, HAP Q) instead of 4. NyrexVJ can make that copy in the background and play it instead of the original; the reasoning and the limits are in [ADR 0021](adr/0021-dxv-hap-conversion-and-ffmpeg-setup.md).

- **Import mode** (Settings, Performance, Convert DXV videos to HAP): **Off** never converts. **Ask** (the default) shows a notice card with the DXV videos it found in your library and converts them after you answer; Not now is remembered. **Automatic** converts every DXV video it finds. Only `.mov` files are looked at. Convert library now looks at the whole library again.
- **Which HAP.** The variant in the file decides: Normal Quality becomes HAP, Normal Quality with alpha becomes HAP Alpha, High Quality becomes HAP Q, High Quality with alpha becomes HAP Alpha. Under HAP format you can pick HAP, HAP Alpha or HAP Q yourself, but that format is only used when it keeps the picture as it is (a video with transparency is never turned into HAP or HAP Q). **HAP Q with transparency (HAP Q Alpha) is not supported**, neither by NyrexVJ nor by FFmpeg's HAP encoder, so such videos become HAP Alpha.
- **What is kept.** Width and height, every frame and its time (so the frame rate, also a changing one), the alpha channel, and the colours (High Quality uses YCoCg, converted with the full range as on the graphics card). The copy is read back and compared with the source (HAP codec, size, number of frames) before it is used. The copy is a second lossy step: for the Normal Quality test file the mean difference was 0.19 of 255 (HAP) and 1.05 (HAP Q).
- **Background, with progress and Cancel.** One video at a time on one thread with a lowered priority. Notice cards and Settings, Performance show the progress and a Cancel button. The video plays as it is until the copy is ready and then switches over without a gap (the new decoder is opened first and swapped in). If the copy cannot be opened or played, it is marked bad and the DXV original plays on; a conversion that fails leaves no partial file.
- **Originals are never changed.** They are only opened for reading. The copies are in the cache folder (`cache/hap` in the NyrexVJ data folder, Open cache folder opens it). The name of a copy comes from the size and the beginning and end of the file and the settings, so a moved or renamed file finds its copy again and a changed file does not. The cache size limit (default 4 GB) deletes the copies that were used longest ago first and never one that is playing. Clear cache deletes all copies.
- **FFmpeg.** The conversion needs FFmpeg with a DXV decoder, a HAP encoder and the MOV writer. NyrexVJ checks that at start (silently, on another thread), shows the result in Settings, Performance and can download a ready-made LGPL build on request, see [FFMPEG.md](FFMPEG.md). Without it everything works as before.

The decoded picture is copied back to normal memory and uploaded again. A direct hand-over of the decoded picture to OpenGL (zero copy) is not implemented, see [ADR 0010](adr/0010-video-decode-pipeline.md) for why.

### Zero copy: what was checked and what stays open

[ADR 0024](adr/0024-video-pipeline-audit-and-measurement.md) examined the three possible ways to hand a decoded picture to OpenGL without the copy through normal memory. None was built, because none can be tried without the matching hardware, and a path written blind for a graphics driver can crash a live show. What runs today is the cheapest path that does not need such a hand-over: with a hardware decoder one copy of the NV12 or P010 picture (about 3 MB for 1080p, 12 MB for 4K at 8 bit) and one upload through an orphaned pixel buffer, a conversion on the graphics card and nothing read back from it; HAP goes to the graphics card compressed; without a hardware decoder the processor decodes and the graphics card converts.

| Route | Needs | Status |
| --- | --- | --- |
| Direct3D 11 (D3D11VA) into OpenGL | `WGL_NV_DX_interop2`, an OpenGL context on the same adapter, texture array slices of the decoder | Not implemented, not tested |
| NVDEC through CUDA into OpenGL | A CUDA context shared with OpenGL, `cudaGraphicsGLRegisterImage` | Not implemented, not tested |
| VA-API through DMA-BUF into OpenGL | EGL (GLFW gives a GLX or WGL context here), `EGL_EXT_image_dma_buf_import`, tiling modifiers | Not implemented, not tested |
| Hardware decode, copy to memory, upload, convert on the graphics card | FFmpeg with a hardware interface | Implemented, probed with a built-in clip, **not run on a real card** |
| Processor decode, upload planes, convert on the graphics card | Nothing | Implemented, tested with software OpenGL |
| HAP, HAP Alpha, HAP Q | `S3TC` textures | Implemented, tested with software OpenGL |

What a person with a real graphics card should run and send back (nothing here was measured without one):

1. Settings, Performance, Video playback, Test hardware decoding. The table is the real compatibility matrix of that computer; send a screenshot with the card and driver version.
2. `NyrexVJ --bench-video 20:h264_1080,h264_4k,hevc_1080,av1_1080,hap_1080 --video-mode hardware --bench-out hw.json` and the same with `--video-mode software` (clips in the media folder). The JSON has the decoder, the route, `firstPictureMs`, `decodeMs`, `uploadMs`, `uploadP95Ms` and `uploadMaxMs` per video, and the processor and memory figures. Check that the hardware run says `hardware` as the decoder, that dropped and late pictures stay at zero, and that the upload p95 is small against the frame time.
3. The same next to VRChat in VR with the frame rate limit at 60, looking at the VRChat frame time with and without the video playing.
4. One HDR file and one 10 bit file from the real source of the videos, to see whether the HDR note appears and whether the colours are what the file should have.



### Monitoring

Settings, Performance, Videos lists every playing video with:

- the codec, size and picture format, the decoder in use (processor or the hardware interface) and the number of decoder threads,
- the route to the graphics card (compressed HAP upload, colour planes converted on the graphics card, or plain RGBA) and, when a faster route was not possible, the reason, for example "no hardware decoder worked on this computer" or "this graphics card cannot take compressed textures",
- the decode time per picture, the upload time per picture (hover for the 95th percentile and the largest of the last 120), the time from opening the video to its first picture, dropped pictures (skipped to keep up) and late pictures (not ready in time),
- a note `HDR video (PQ)` or `HDR video (HLG)` when the file is HDR (it is shown without tone mapping),
- the video memory, split into pictures, colour planes, compressed HAP textures and upload buffers.

The Clip window of a video and the tooltip of its Library entry show the same decoder and route for that clip. Settings, Performance, Memory explains each memory number.

Video memory is an estimate computed from the texture sizes NyrexVJ creates. It does not include what the driver adds. The upload buffers are counted as video memory although drivers often keep them in normal memory. The decoder's own pictures inside the graphics card (hardware decoding) are not counted.

### Frame rate limit

Settings, Performance, Frame rate limit: 30, 60, 90, 120, 144 or Unlimited, independent of VSync. The default is 60. After each frame the limiter sleeps until shortly before the next deadline and finishes with a very short precise wait, so the processor is idle in between and the frame times stay even. A frame that took too long does not make the next ones shorter. It is the biggest saving next to VRChat: a limit of 60 on a 144 Hz monitor stops NyrexVJ from drawing 144 frames a second that nobody needs. With VSync on, the limiter only acts when the monitor is faster than the limit.

### Playback control

The Clip panel of a video clip has a Playback section. The settings belong to the cell (the clip in one column of one layer) and are saved in the preset, so the same video can run forwards in one column and backwards in another. A preset without playback settings plays as before: forward, the whole video, loop.

| Control | What it does |
| --- | --- |
| Time and frame, scrub bar | Shows the position as time and frame number. Dragging the bar jumps to that picture exactly (the decoder seeks to the key frame before it and decodes up to it, so the picture is the right one, not the nearest key frame). |
| Step buttons | One picture back or forward. |
| Play, Pause | Pause sets the speed to 0 and keeps the picture, Play sets it to 1. |
| In point, Out point, Set here | The part of the video that plays. Drag the values or take the current position. An Out point at the end of the video means "to the end". |
| Loop mode | Loop, Ping-pong (forwards, then backwards), Once and hold the last frame, Once and then disappear (the layer shows nothing after the end). |
| Clip speed, Reverse | From -4x to +4x. Negative speeds play backwards, Reverse flips the sign. |
| Clip length in beats | Off, or 0.25 to 128 beats. The range from In to Out then plays in exactly this many beats and follows the tempo of the deck, so the video stays on the beat when you change the BPM, tap or nudge. |
| Reset playback | Back to the defaults. Only shown when something was changed. |

The layer clock still decides when a clip starts (a clip restarts at the In point when it is triggered) and the layer Speed, Tap and Pause still move it. The clip speed is multiplied with the layer speed.

**Reverse playback.** HAP and other formats with a key frame at every picture go backwards one picture at a time. Other codecs (H.264, HEVC, AV1, VP9) only store a full picture at their key frames, so for reverse the decoder seeks to a key frame before the wanted time, decodes forward into a ring of 6 to 16 pictures (sized so that the ring and its picture pool stay near 96 MiB, 4K videos keep the minimum of 6) and hands the pictures out in descending order. The video monitor shows when a clip plays backwards and how many pictures are buffered, and counts the seeks. The cost is real: for a video with a key frame every 10 seconds, up to 10 seconds of pictures have to be decoded again for every segment of 6 to 16 pictures, so it only keeps up when your computer decodes much faster than real time. If it cannot keep up, the nearest picture that is not newer than the wanted time stays on screen, the render loop never waits. For heavy use of backwards playback and scrubbing, use HAP, or encode with a short key frame interval (for example `ffmpeg -i in.mp4 -c:v libx264 -g 12 out.mp4`, a little bigger file, a much cheaper reverse).

**Switching without empty frames.** A video is opened and decoded up to its first picture before it is needed: the next filled column of every layer is prepared in the background while nothing is changing (the first picture at the In point, or at the Out point when the clip plays backwards). When you trigger it, the first picture is already there. Settings, Performance, Video playback, Prepare the next clip of every layer turns this on or off (on by default, `--no-prewarm` turns it off for one run). It costs one extra decoder and about 14.5 MB of video memory per prepared 1080p clip, at most one per layer. Measured with eight 1080p H.264 clips and 16 switches (`NyrexVJ --bench-switch 1:big1,big2,...`, software decoding, llvmpipe):

| | Switches with an empty frame | Time to the first picture (average, maximum) | Estimated video memory |
| --- | ---: | ---: | ---: |
| Prepare next clip off | 11 of 16 | 47.9 ms, 111.2 ms | 103.8 MB |
| Prepare next clip on | 0 of 16 | in the first frame | 118.3 MB |

**Limits.**
- A clip that runs in several layers at the same time shares one decoder. Each layer asks it in turn with its own clock and its own playback settings. With the same settings and the same trigger time that is harmless, with different positions or directions the picture of the layers can jump, because there is only one decoder. For two different positions of the same video at once, put two copies of the file (with different names) into the library.
- Ping-pong and backwards loops restart the reverse segment at the turning point. A slow decoder can show a short stutter there.
- Timecode locked playback comes with the show sync milestone.
- Only video clips have these controls. Images, shader clips and 3D scenes have no play head.

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

### DXV against its HAP copy

Measured on the one real DXV file available (`tests/data/test_dxv_dxt1.mov`, Normal Quality, 128 x 72, 12 frames, looped), headless on Linux with software OpenGL, FFmpeg 8.1, ten seconds per scenario, with `python3 scripts/bench_video.py --dxv tests/data/test_dxv_dxt1.mov --only dxv`:

| | DXV original | HAP copy |
| --- | ---: | ---: |
| Decoder path | processor (`dxv`), RGBA | `HAP direct upload` (compressed blocks to the graphics card) |
| Decode time per frame | 0.101 ms | 0.036 ms |
| Upload per frame | 0.689 ms (RGBA) | 0.689 ms (compressed; software OpenGL, so not meaningful) |
| Video memory of the clip | 73,728 bytes | 9,216 bytes |
| NyrexVJ processor time without the rasteriser | 2.9 % of one core | 2.8 % of one core |

Conversion of that file with `NyrexVJ --bench-convert 5:tests/data/test_dxv_dxt1.mov` (five runs, median): 12 frames in 2.1 ms (decode 0.2 ms, encode 0.9 ms, the rest is writing and checking the copy), about 6,000 frames per second, 1.09 times the size of the source for HAP, 1.33 for HAP Alpha and 1.96 for HAP Q.

What this shows and what it does not:

- The video memory ratio (8 to 1 here) follows from the formats: RGBA is 4 bytes per pixel, HAP DXT1 0.5 and HAP Alpha and HAP Q 1. At 1920 x 1080 that is 8.3 MB against 1.0 MB (HAP) or 2.1 MB (HAP Alpha, HAP Q) per picture before the buffers; this is arithmetic, not a measurement.
- The clip is tiny, so the processor figures are only a few percent and the two scenarios are not different within the noise. **The processor effect at 1080p or 4K was not measured**, because no real DXV file of that size was available and FFmpeg cannot make one. Measure your own files with `--bench-convert` and `scripts/bench_video.py --dxv <your file>`, preferably on the computer you play on.
- Nothing was measured on a real graphics card, in VR or next to VRChat. The conversion itself costs processor time once per video, on a worker with a lowered priority, so convert before the show, not during it.

Repeat the measurement with `python3 scripts/bench_video.py --exe <NyrexVJ> --videos <folder with test videos> --seconds 8`, which writes the table above and can write JSON with `--json`. The scenarios look for files named like `h264_1080.mp4`, `h264_4k.mp4`, `hevc_1080.mp4`, `av1_1080.mp4`, `hap_1080.mov`, `hapalpha_1080.mov`, `hapq_1080.mov` and `hap_4k.mov`.

### FFmpeg is not part of NyrexVJ

NyrexVJ does not contain FFmpeg and does not link it. It loads the FFmpeg libraries at run time, so the program starts and works without them and only the video clips are unavailable. This keeps NyrexVJ's MIT licence untouched, because the libraries stay separate files that you control.

You do not have to do the steps below by hand: Settings, Performance, FFmpeg has a button that downloads a ready-made LGPL build, checks it and puts it into the NyrexVJ folder, only when you press it and without touching any FFmpeg that is already installed ([FFMPEG.md](FFMPEG.md)). The manual way stays available.

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
- Playback control: `src/core/playhead.{hpp,cpp}` (pure play head, no FFmpeg or OpenGL), `src/media/playback_driver.{hpp,cpp}` (clock to decoder), the range, direction and exact seek parts of `src/media/video_decoder.cpp`, `src/render/media_player.cpp` (forwarding, scrub, step), `Engine::prewarmMedia` (next clip) and `drawPlaybackControls` in `src/app/ui_browser.cpp`.
- Conversion: `src/media/hap_convert.cpp` (the converter), `src/media/convert_cache.cpp` (the cache), `src/media/convert_manager.cpp` (queue, worker thread, modes), `src/media/video_redirect.cpp` (hooks that let `MediaPlayer` ask for a copy without knowing the manager), `src/media/ffmpeg_setup.cpp` (capability check, package choice, download, verification, install), `src/core/archive.cpp` (zip and tar.xz reader with limits), the download functions in `src/platform/` and `src/app/ui_convert.cpp` (settings page and notice cards). `NyrexVJ --bench-convert RUNS:file1,file2` (with `--convert-format auto|hap|hap_alpha|hap_q` and `--convert-keep DIR`, no window) measures the conversion; `tests/test_convert.cpp` (`nvj_convert_tests`) covers the archive reader, the package choice, the checksum list, the licence decision, the install, the cache, the redirect hooks and the manager.
- Command line for tests and measurements: `--bench-switch SECONDS:clip1,clip2,...` (at least two clips, counts empty frames and the time to the first picture over repeated switches in real time, `--bench-out` writes JSON), `--no-prewarm`, `--bench-video SECONDS:clip1,clip2` measures the processor cost of videos in real time, `--video-selftest DIR` uploads every clip of a folder through the graphics card path and compares it with the processor path (exit code 1 on a difference), `--video-mode auto|hardware|software`, `--no-hap-direct`, `--no-gpu-convert` and `--frame-report SECONDS` (frame time and sleep time statistics with the limiter).
- Colour space test clips in `tests/data` (`test_cs_601_limited`, `test_cs_709_full`, `test_cs_2020_limited10`, `test_cs_2020_full10`, `test_cs_2020_444_10`, `test_hdr_pq`, `test_hdr_hlg`, `test_h264_422_10`, `test_h264_444`, each 10 to 13 kB, made with FFmpeg's encoder with the colour tags set) are compared with FFmpeg's own conversion in `nvj_media_tests` (`testGpuLayouts`) and on a real OpenGL context in `--video-selftest`. The `--bench-video` JSON also has `firstPictureMs`, `transfer`, `uploadP95Ms` and `uploadMaxMs` per video.
- `tests/test_playhead.cpp` (target `nvj_playhead_tests`) tests the play head alone: every loop mode in both directions, ranges, speed, beat sync and invalid values. `tests/test_media.cpp` also uses two index videos (`tests/data/test_index_h264.mp4` and `tests/data/test_index_mjpeg.avi`, every picture carries its number in its brightness) to check exactly which picture is shown after forward play, reverse play, exact seeks, ranges, ping-pong, once and hold, once and stop and a prepared clip.
- `tests/test_media.cpp` covers the file name rules, premultiplication, image loading with alpha, errors, the library scan, the import of dropped files and, when the libraries are present, H.264 and HAP decoding, seeking, looping without lost frames, alpha, opening and closing many decoders, the HAP parser (Snappy, chunks, uncompressed, HAP Q, damaged files), the choice of decoder and the fallback reasons, the codecs that skip the graphics card and the YCoCg conversion (a small test video tagged YCoCg, on the processor path, in the reference conversion and in the real OpenGL shader).

## What was and was not tested

The conversion of DXV videos to HAP and the FFmpeg setup (v0.28.0): see [ADR 0021](adr/0021-dxv-hap-conversion-and-ffmpeg-setup.md) and [FFMPEG.md](FFMPEG.md) for the full list. In short, tested on Linux: the converter on the one real DXV file (Normal Quality) and on stand-in files for High Quality and alpha (the YCoCg path, alpha kept with ProRes), the cache, the manager with its modes, queue, cancel and eviction (`nvj_convert_tests`, also under ThreadSanitizer without a warning), the archive reader, the install with real archives and a real download, and playback of an original and its copy in the benchmark. Not tested: real DXV High Quality or alpha files, DXV files of 1080p or 4K (so no processor figure for them), the switch from original to copy in the interactive program by hand, the notice cards and the settings page in use, a real graphics card, and everything that runs only on Windows (the download with WinHTTP, loading the installed DLLs).

The GPU video pipeline audit (v0.31.0): see [ADR 0024](adr/0024-video-pipeline-audit-and-measurement.md). Tested on Linux with software OpenGL: nine colour space test clips (601, 709, 2020, limited, full, 8 and 10 bit, 4:2:2, 4:4:4, PQ and HLG) against FFmpeg's conversion, the HDR detection, the first picture time and the upload percentiles, and `--video-selftest tests/data` (now also in the CI). Not tested: hardware decoding, zero copy, HDR files from real sources, chroma siting, anything on a real graphics card, on Windows at run time or next to VRChat.

Tested on Linux with FFmpeg 6.1 and software OpenGL (llvmpipe): everything above except hardware decoding, including the thumbnails, the transparency over other layers, the real OpenGL upload of every test clip (HAP, HAP Alpha, HAP Q, chunked and uncompressed HAP, 8 bit and 10 bit H.264 and HEVC, ProRes with alpha) compared with the processor path (`NyrexVJ --video-selftest tests/data`), looping and seeking, and several videos at once in 1080p and 4K. On Windows (MSVC, the build workflow) with the FFmpeg 8.1 headers and DLLs: the build and the decode tests. The Windows-only code was also compiled with MinGW as a check.

The playback control (play head, reverse, exact seek, range, loop modes, beat sync, prepared clips) was tested with the unit tests, the index videos, an end to end run with software OpenGL on a virtual display and the switching benchmark above, all with software decoding. Not tested: backwards playback with hardware decoding, long videos with long key frame intervals in reverse for minutes, and everything on Windows at run time.

Not tested, because it needs hardware that was not available: hardware decoding on a real graphics card (D3D11VA, DXVA2, NVDEC/CUDA, VA-API, QSV), real graphics card frame times, starting NyrexVJ itself on Windows with video clips, and anything in VR or next to VRChat. The code for these paths is written against the FFmpeg interfaces and falls back to the processor when something fails, but expect to need corrections after the first run on real hardware, and please report them.

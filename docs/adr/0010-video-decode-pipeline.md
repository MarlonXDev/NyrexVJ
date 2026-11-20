# ADR 0010: Video decode pipeline with hardware decoding, HAP direct upload and GPU colour conversion

- **Status:** Accepted (2026-11-20, requested by MarlonXDev: video must cost VRChat as little processor and graphics time as possible)
- **Date:** 2026-11-20
- **Owners:** Maintainers (pending assignment)
- **Related:** [MEDIA.md](../MEDIA.md), issue #43

## Context
NyrexVJ runs next to VRChat, often in VR, on the same computer and the same graphics card. Before this decision every video was decoded by FFmpeg on the processor, converted to RGBA and scaled by swscale on the processor and uploaded as an RGBA texture. HAP was expanded from its DXT blocks to RGBA by FFmpeg before the upload. That cost processor time in the colour conversion and in the expansion, moved far more data to the graphics card than necessary, and had no frame rate limit independent of VSync.

The constraints: C++20, OpenGL 3.3 core, one OpenGL context, FFmpeg loaded at run time (nothing linked, see [MEDIA.md](../MEDIA.md)), Windows and Linux, no new dependencies, and an author with no NVIDIA, AMD or Intel graphics card available for tests, only a processor-only Linux machine with a software OpenGL driver.

## Decision
1. **Decoder choice.** Settings, Performance, Decoder: Automatic, Prefer hardware, Force software, stored as `videoDecoder` (`auto`, `hardware`, `software`). Automatic uses the graphics card for H.264, HEVC, AV1 and VP9 from 1280 x 720 on and the processor below that. Every hardware interface is probed once with a tiny built-in clip for H.264, HEVC and AV1 (VP9 is only tried when a VP9 video plays), and the result and the failure reasons are shown. A hardware decoder that fails to open, fails the probe decode or stops during playback (24 failed pictures in a row) switches that video to the processor with a recorded reason. The switch during playback restarts the video, which is visible as a short gap, and it was written but could not be triggered without a real graphics card.
2. **Hardware interfaces through FFmpeg's hwaccel API.** Windows: D3D11VA, DXVA2, CUDA, QSV. Linux: CUDA, VA-API, QSV, VDPAU. The decoded surface is copied to normal memory with `av_hwframe_transfer_data` as NV12 or P010.
3. **Colour conversion on the graphics card.** Planar and semi-planar YUV (8 to 16 bit, with an alpha plane when present) is uploaded as plane textures (R8, RG8, R16, RG16) through one orphaned pixel buffer and converted by a shader into the RGBA8 clip texture, with the matrix (601, 709, 2020) and range taken from the stream. A source more than 1.5 times larger than the output is supersampled with four taps. This replaces swscale on the processor. Setting `videoGpuConvert`.
4. **HAP direct upload.** HAP, HAP Alpha and HAP Q are parsed natively (section headers, Snappy, chunks, uncompressed) and uploaded as compressed S3TC textures. HAP Q is converted from YCoCg in the media shader. Support is checked at run time with the extension and a real 4 x 4 trial upload. A variant that is not supported, a damaged file or a missing extension uses FFmpeg's HAP decoder on the processor and says why. Setting `hapDirect`.
5. **Frame rate limit.** 30, 60, 90, 120, 144 or Unlimited, default 60, in the main loop after the buffer swap, independent of VSync, with a coarse sleep and a short precise wait. Setting `fpsLimit`. This is a change of behaviour for everyone who runs on a monitor above 60 Hz, noted in the changelog.
6. **Monitoring.** `VideoStatus` per video (decoder, backend, route, reason, threads, decode time, upload time, drops, late pictures) and `VideoMemory` (pictures, colour planes, compressed textures, upload buffers), shown on the Performance page, in the Clip window and in the Library tooltip.
7. **Measurement and tests.** `--bench-video`, `--video-selftest`, `--video-mode`, `--no-hap-direct`, `--no-gpu-convert`, `--frame-report`, and `scripts/bench_video.py`. The upload paths are compared with the processor path on a real OpenGL context in the self test, the parser, the settings and the fallback logic are unit tests.

## Not decided: zero copy
A direct hand-over of the decoded surface to OpenGL without the copy to normal memory is **not implemented**. The options are `WGL_NV_DX_interop2` for D3D11VA, CUDA-OpenGL interop for NVDEC and EGL image import of DMA-BUF for VA-API. Reasons:
- Each of the three is separate code per vendor and operating system, with its own synchronisation and format rules (array textures, NV12 planes, tiling modifiers), and OpenGL 3.3 core with GLFW does not give a shared device or an EGL context to work with.
- None of it can be built and run without the matching graphics card and driver. Code written blind for a graphics driver is the kind of code that works on the author's machine and crashes on someone else's, and a crash in a live show is worse than a copy.
- The copy back and upload of one 1080p NV12 picture is about 3 MB per frame. It is real, but small next to what was removed (RGBA expansion and swscale, which is 8 MB per frame and a full colour conversion on the processor).
It stays open as an idea, to be tried with real hardware, and the current design leaves room for it: the decoder already delivers a `VideoFrame` with a layout, and a zero-copy frame would be another layout.

## Alternatives
1. **Hardware decoding only for NVIDIA through CUDA.** Simple, but excludes AMD and Intel users, and the request asked for all three.
2. **Linking against a vendor SDK (NVDEC, AMF, oneVPL).** Faster paths, but new dependencies, licence work and no way to test them here.
3. **Keep processor conversion and add only the limiter.** Smallest change, but leaves the colour conversion and the HAP expansion, the two largest processor costs, in place.
4. **Always use the hardware decoder in Automatic mode.** Simpler rule, but small videos then cost more (copy back, decoder start) than the processor decoder.
5. **A ring of several upload buffers.** Fewer stalls on real graphics cards, but each extra buffer is one more copy of every video's colour planes in memory. One orphaned buffer was chosen after measuring the memory, and it can be raised later.

## Consequences
- Processor time for HAP drops sharply and video memory for 1080p HAP falls, see the table in [MEDIA.md](../MEDIA.md). Processor time for software decoded video drops, its video memory rises (planes and an upload buffer next to the picture). Users with little video memory can switch the conversion off or use HAP.
- The hardware paths are written against FFmpeg's documented interfaces, probed at run time and fall back by themselves, but they were never run on a real graphics card. The first people to try them are the real validators and the first fixes are likely.
- The memory figures are estimates from texture sizes and do not include driver overhead or the decoder's own surfaces.
- No measurement in VR or next to VRChat exists. The measured claim is limited to processor time of NyrexVJ's video part on the test machine.
- The default frame limit of 60 changes behaviour on faster monitors. Users who want the old behaviour select Unlimited.

# ADR 0024: GPU video pipeline audit, colour space coverage, HDR detection, new measurements, zero copy stays open

- **Status:** Accepted (2026-11-24, requested by MarlonXDev), implemented in v0.31.0. Issue [#74](https://github.com/MarlonXDev/NyrexVJ/issues/74).
- **Date:** 2026-11-24
- **Owners:** Maintainers (pending assignment)
- **Related:** [MEDIA.md](../MEDIA.md), [PERFORMANCE.md](../PERFORMANCE.md), [ADR 0010](0010-video-decode-pipeline.md), [ADR 0019](0019-dxv-software-codecs.md), [ADR 0021](0021-dxv-hap-conversion-and-ffmpeg-setup.md)

## Context
The request asks to examine the whole video path (FFmpeg, D3D11VA, DXVA2, NVDEC, QSV, VAAPI, HAP), to use zero copy only where it really works and otherwise transfer efficiently without needless readbacks, to keep HAP upload and DXV to HAP, to check alpha, 8 and 10 bit, colour spaces, synchronisation and fallbacks, and to measure processor, graphics card, video memory, frame times and decoder latency with 1080p and 4K and next to VRChat. The development machine has no graphics card, no hardware decoder, no Windows and no VRChat. Colour management (a request of its own), monitoring of the whole program and the hardware test matrix are other requests and are not part of this change.

## Findings (what existed before)
- **Decoder choice and fallback.** Automatic, Prefer hardware and Force software; Windows tries D3D11VA, DXVA2, CUDA, QSV, Linux CUDA, VA-API, QSV, VDPAU; every interface is probed with a tiny clip; a failing decoder falls back to the processor with a reason (ADR 0010).
- **Transfer.** A hardware picture is copied to normal memory with `av_hwframe_transfer_data` as NV12 or P010 and uploaded as plane textures through one orphaned pixel buffer; the planes are converted to RGBA by a shader on the graphics card. HAP, HAP Alpha and HAP Q are uploaded as compressed textures without any conversion on the processor. During playback no picture is read back from the graphics card to the processor on any path (the DXV conversion reads a finished file back once to verify it, which is not playback).
- **Colour.** The shader knows BT.601, BT.709, BT.2020 and YCoCg, limited and full range, 8 to 16 bit (also most significant bit aligned), planar, semi-planar, with alpha and packed UYVY. The matrix comes from the colour space tag of the stream, and without a tag from the height (720 and more is 709).
- **Zero copy.** Not implemented, ADR 0010 explains why.

## Decision
1. **Zero copy stays unimplemented.** A hand-over of the decoded surface to OpenGL (`WGL_NV_DX_interop2` for D3D11VA, CUDA-OpenGL interop for NVDEC, EGL image import of DMA-BUF for VA-API) needs a shared device or context that OpenGL 3.3 through GLFW does not provide, differs by vendor and operating system, and cannot be built or run here at all. Code written blind for a graphics driver can crash in a live show, so no such path is added. Where the hardware decoder works the transfer is one NV12 or P010 copy and one upload, with no readback from the graphics card and no processor colour conversion; where hardware decoding does not apply the processor decodes and the graphics card converts. This is the "efficient transfer" the request asks for, and ADR 0010 is unchanged.
2. **HDR is detected, not converted.** The transfer function of the stream (`color_trc`) is read; PQ (SMPTE 2084) and HLG (ARIB STD-B67) are shown as `HDR video (PQ)` or `HDR video (HLG)` in the monitor, the Clip window and the Library tooltip with the line that the picture is shown without tone mapping, so colours look flat. The picture itself is not changed: the matrix and range are applied as before, the result is treated as an ordinary SDR picture. Tone mapping and colour management are a request of their own.
3. **Two new measurements.** `VideoStatus::firstPictureMs` is the time from opening a decoder to the first picture that was decoded (decoder latency, includes opening the file and the first key frame). `UploadStats::p95Ms` and `maxMs` are the 95th percentile and the largest of the last 120 uploads, beside the existing mean, so a stutter that the mean hides becomes visible. Both are in the monitor tooltips and in the `--bench-video` JSON (`firstPictureMs`, `transfer`, `uploadP95Ms`, `uploadMaxMs`). Nothing is added to the per-frame path beyond one stored number and a sort of at most 120 values.
4. **Colour space coverage as a test.** Nine new test clips (BT.601 limited, BT.709 full, BT.2020 limited and full at 10 bit, BT.2020 4:4:4 at 10 bit, PQ, HLG, 4:2:2 at 10 bit, 4:4:4 at 8 bit) go through the real OpenGL path in `--video-selftest` and through the reference conversion in `nvj_media_tests`. Each clip is compared with the output of FFmpeg's own conversion, and the test also requires that the wrong matrix gives a clearly larger difference (at least 1.5 times), so a test clip that cannot tell the matrices apart fails instead of passing.
5. **The self test learns the DXV route.** `--video-selftest` expected the route `gpu convert` for every non-HAP clip, but a DXV Normal Quality clip arrives from FFmpeg's DXV decoder as RGBA and correctly takes `cpu rgba`. The expectation was stale (the command exited with 1 on `tests/data`) and now accepts the processor route for the DXV decoder only. The CI runs `--video-selftest tests/data` on Linux when the FFmpeg libraries are there.

## Not changed, and why
- **Chroma siting.** Chroma samples are interpolated by the sampler (bilinear), as in 0.18.0. For 10 bit 4:2:0 this differs from FFmpeg's own conversion by about 3 of 255 on the test pictures (the 4:4:4 test clips differ by 0.002, so matrix and range are exact). The difference is the chroma filter, not the matrix, and was not changed.
- **BT.2020 constant luminance (`AVCOL_SPC_BT2020_CL`)** is treated like non constant luminance, and SMPTE 240M has no matrix of its own and falls back to the height rule. Both are rare in files for VJ use and were left as they are.
- **Frame ownership of the hardware download.** Whether the picture that `av_hwframe_transfer_data` writes into is allocated again for each frame was not verified (it needs a real hardware decoder), so there is no claim about per-frame allocation.

## Alternatives considered
- **A zero-copy path for NVIDIA only (CUDA interop).** It is the best documented of the three, but still needs a CUDA context shared with OpenGL, a real card to test and a second code path to keep alive. Excluding AMD and Intel contradicts the request for all interfaces.
- **Tone mapping HDR in the media shader.** A guess at a tone curve without a colour management design would be wrong for some files and hide the problem; showing a clear note is honest, and tone mapping is its own request.
- **Reading GPU time of the conversion with timer queries.** Possible, but on llvmpipe the numbers are not attributed reliably (see PERFORMANCE.md), and on a real card it needs verification there. The upload time as seen from the render thread stays the figure.

## Consequences
- No change of behaviour for any existing video: same routes, same shaders, same memory. The new fields are additive in the benchmark JSON and the tooltips.
- A user with an HDR file now sees a warning instead of a silently flat picture.
- Zero copy remains the largest open item for processor and graphics load of 4K hardware-decoded video, and it can only be started with a real card, driver and Windows or Linux machine to test on.

## Validation
- Run on Linux with software OpenGL: `nvj_media_tests` (1,294 checks including the nine new clips, the transfer names and the first picture time), `--video-selftest tests/data` (real OpenGL upload of every clip compared with the processor path, exit 0), all 29 tests, `--live-selftest`, `--shader-selftest`, `--plugin-selftest`, `--output-selftest`, `--check-shaders` and `scripts/check_i18n.py`.
- Measured with `scripts/bench_video.py` (software rasteriser, two cores): the new build against the old build, alternating, three runs each, shows no difference outside the spread (see [PERFORMANCE.md](../PERFORMANCE.md)).
- Not validated: hardware decoding of any kind, zero copy, real graphics card timings, Windows at run time, 4K at 60 frames a second, HDR files from real cameras or encoders other than the test clips made with the FFmpeg encoder, and anything next to VRChat.

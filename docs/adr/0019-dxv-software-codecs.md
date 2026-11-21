# ADR 0019: DXV and other software-only codecs: keep the processor decoder, skip the graphics card, convert YCoCg correctly

- **Status:** Accepted (2026-11-21, requested by MarlonXDev), implemented in v0.26.1. Issue [#62](https://github.com/MarlonXDev/NyrexVJ/issues/62).
- **Date:** 2026-11-21
- **Owners:** Maintainers (pending assignment)
- **Related:** [MEDIA.md](../MEDIA.md), [ADR 0010](0010-video-decode-pipeline.md), [PERFORMANCE.md](../PERFORMANCE.md)

## Context
A DXV clip (the codec of Resolume) played with Settings, Performance, Decoder set to Prefer hardware showed, for D3D11VA, DXVA2 and NVDEC (CUDA), the text "dxv is not supported by ... in this FFmpeg". The graphics card in question decodes H.264 and AV1, so the text looked like a decoder fault. The report asked five things: why DXV runs through the hardware detection, a clear message, whether DXV can be uploaded as a compressed OpenGL texture like HAP, what the variants, alpha and the licence situation mean, and to keep the processor decoder if a direct upload is not sensible or not allowed. HAP and the existing hardware decoders must not change.

## Findings

**Why DXV went through the hardware detection.** `worthHardware()` in `src/media/video_decoder.cpp` only guards the Automatic mode (large H.264, HEVC, AV1 and VP9). In Prefer hardware every codec is tried on every interface, and `prepareHardware()` in `src/media/video_hardware.cpp` words a missing hardware configuration the same way for all codecs. After the last interface the processor decoder took over, so playback worked; only the message was misleading. A graphics card has no DXV decoder, so nothing was wrong with the card or the driver.

**What DXV is.**
- Resolume describes DXV as a codec for which the graphics card does the hard work, with Normal Quality and High Quality and an option to store alpha ([Rendering to DXV](https://resolume.com/article/67)). DXV3 Normal Quality has the same picture quality as DXV2, High Quality roughly doubles the file size ([DXV 3 upgrade guide](https://resolume.com/blog/11923)).
- FFmpeg's decoder (`libavcodec/dxv.c`, [source](https://ffmpeg.org/doxygen/8.0/dxv_8c_source.html), LGPL 2.1 or later) knows four texture formats: DXT1 (Normal Quality, no alpha), DXT5 (Normal Quality with alpha), YCG6 (High Quality) and YG10 (High Quality with alpha), plus older headers where version 1 is always DXT1. On top of the texture blocks there is a layer of custom compression (raw, LZF and table coded opcodes).
- That decoder is software only: it supports frame and slice threading and has no hardware acceleration. DXT1 and DXT5 come out as RGBA. YCG6 and YG10 come out as planar YUV 4:2:0 (with alpha for YG10) tagged with the YCoCg colour space: luma in the first plane, Cg in the second, Co in the third, alpha in the fourth, chroma at half width and half height, and no colour range or chroma offset set.
- FFmpeg has an encoder (`dxvenc.c`, [source](https://ffmpeg.org/doxygen/8.0/dxvenc_8c_source.html)) since version 7.0, but it writes only DXT1 without alpha.

**Licence and openness.**
- There is no public specification. Resolume staff say on their forum that the DXV code base is private ([thread](https://resolume.com/forum/viewtopic.php?p=106605)), and another thread discusses why it is not open source ([thread](https://resolume.com/forum/viewtopic.php?p=65805)). The format in FFmpeg was reverse engineered by contributors.
- The only open implementation is FFmpeg's, under the LGPL. NyrexVJ is MIT licensed and does not ship FFmpeg; it loads the FFmpeg libraries at run time through their public interface (see [MEDIA.md](../MEDIA.md)). Using FFmpeg's DXV decoder that way is the same arrangement as for every other codec.
- Copying or translating FFmpeg's DXV code into this repository would put LGPL code into MIT code. A rewrite from memory of that code is a derivative work in practice, and a clean room rewrite needs a specification that does not exist and people who have not read FFmpeg's code. Whether decoding the format for interoperability is allowed in a given country, and whether anyone holds patents on it, are legal questions that were not clarified. This is not legal advice.

**Can DXV be uploaded as a compressed texture like HAP?**
- Technically yes for the Normal Quality variants: the data under the custom compression layer are DXT1 or DXT5 blocks. The Open Source project ossia score does this for DXT1 and DXT5 (its [DXV decoder](https://ossia.io/score-api-docs/structscore_1_1gfx_1_1_d_x_v_decoder.html) bypasses FFmpeg's processor decompression and uploads the blocks). That project is GPL licensed.
- It would need NyrexVJ's own decoder for the compression layer, because FFmpeg's public interface does not hand out the blocks. High Quality is not one texture: luma and chroma are separate BC4 style planes with half resolution chroma and would need their own shader path.
- The gain is small. The Performance page in the report showed about 0.70 ms decode and 0.61 ms upload per frame at 1080p60 on the processor path. A direct upload would remove the DXT to RGBA step and shrink the upload from 8 MB to 1 MB per frame, which only matters for many 4K layers.

## Decision
1. **Keep the processor decoder for DXV.** No direct upload and no own DXV decoder, because of the missing specification, the licence situation and the small gain. This is documented in [MEDIA.md](../MEDIA.md). It can be reopened if Resolume publishes a specification or permission, if FFmpeg hands out the blocks through its public interface, or if a measurement shows a real need.
2. **Software-only codecs never try the graphics card.** A fixed list of codec names that none of NyrexVJ's interfaces decode (DXV, ProRes, DNxHD/DNxHR, CineForm) skips the hardware attempt in every mode (`softwareOnlyCodecLabel()` in `src/media/video_hardware.cpp`). H.264, HEVC, AV1, VP9 and everything else go the same way as before, and HAP is untouched. A fixed list was chosen over asking FFmpeg whether a decoder has a hardware configuration, because for H.264 on an FFmpeg without that interface the old, precise per interface message is the right one.
3. **A clear message.** With Prefer hardware, the clip shows "DXV is decoded by the processor, graphics cards cannot decode this codec (this is normal)" (generic text for the other codecs) in the normal colour instead of the warning colour, in German, Spanish and French as well. In Automatic mode nothing changes (the clip was never meant for the graphics card). `VideoStatus` has a new field `softwareOnly`.
4. **YCoCg is converted correctly.** A frame tagged YCoCg gets a new matrix (3) that uses full range and `R = Y + Co - Cg`, `G = Y + Cg`, `B = Y - Cg - Co` with chroma centred on 128, with Cg from the second and Co from the third plane. The conversion shader got a second pair of coefficients (`uCross`, zero for every other matrix) so that red can depend on the first chroma plane and blue on the second. The processor path (`convertYcocgToRgba`, used when GPU conversion is off or fails) does the same arithmetic and then scales like every other picture. Before this, High Quality clips were converted as BT.709 with a limited range.

## Alternatives
- **Own DXV decoder with a direct DXT1/DXT5 upload.** Rejected: no specification, the only open code is LGPL, a rewrite from it is a derivative, the processor path is already cheap, and High Quality would still need the processor path or an extra shader path.
- **Clean room decoder.** Rejected: needs a specification and people who have not read FFmpeg's code; neither exists here.
- **Patching FFmpeg to hand out the blocks.** Rejected: NyrexVJ does not build FFmpeg, it loads whatever LGPL build the person has.
- **Asking FFmpeg whether a decoder has any hardware configuration** instead of a fixed list. Rejected for H.264, HEVC, AV1 and VP9: on an FFmpeg without a given interface the old per interface message is the more useful one, and those codecs must not change.

## Consequences
- DXV clips behave as before and show an honest message; ProRes, DNxHD/DNxHR and CineForm get the same treatment.
- DXV High Quality (with and without alpha) should show the right colours. The alpha of DXV Normal Quality with alpha arrives in an RGBA frame and of High Quality with alpha in the fourth plane; both reach the layer as before. The clip browser's alpha label for DXV may be missing because the container does not state the pixel format before the first frame is decoded.
- A DXV clip is decoded on the processor with FFmpeg's threads. Anyone who needs minimal processor load can convert clips to HAP (`ffmpeg -i clip.mov -c:v hap -format hap out.mov`; `hap_alpha` and `hap_q` for alpha and quality), which costs a little quality because DXV is already lossy.

## Validation / open questions
- No real DXV file was available. The processor path was tested with a software-only codec that exists as a test file (ProRes), and the YCoCg conversion with a small test video tagged YCoCg (Y = 128, Cg = 100, Co = 160, expected colour 188, 100, 124) on the processor path, in the reference conversion of the test and in the real OpenGL shader (`--video-selftest`, software OpenGL, difference 0). Whether FFmpeg 8.x delivers real DXV High Quality frames exactly as the source reading above says was not checked with a real file; please report wrong colours.
- A static FFmpeg with the DXV encoder that could have produced a test file could not be run in this environment.

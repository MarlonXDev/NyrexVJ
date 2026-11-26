# Colour management, LUTs and HDR

Version 0.53.0. [ADR 0046](adr/0046-color-management.md) has the reasons, [KNOWN_ISSUES.md](KNOWN_ISSUES.md) the open problems (KI-8, KI-15, KI-16), [TESTING.md](TESTING.md) the tests.

## What this version gives you

Nothing changes until you switch something on. The show, the layers, the mixing, the master picture, every output window, NDI, Spout and the recorder look and behave exactly as before. Three optional things were added:

| Feature | Where | Default | What it does |
| --- | --- | --- | --- |
| Colour stage with a LUT (experimental) | Settings, Performance, Colour | off | One extra pass over the finished master picture with a 3D `.cube` look-up table and a strength from 0 to 1. |
| Tone map HDR videos to SDR | Settings, Performance, Colour | off | Turns videos that are marked PQ or HLG into the normal SDR picture inside the graphics card conversion of the video. |
| Display of an output | Settings, Output, below the colour of an output | Rec. 709 and sRGB (changes nothing) | Converts the picture of one output window for a screen or projector with another gamut (Display P3, Rec. 2020) or another curve (gamma 2.2, 2.4, 2.6). |

What is **not** here: a 10 bit picture, a 10 bit output to a screen or SDI card, HDR output to a screen, HDR metadata, a floating point master picture, automatic detection of the screen. These are explained in the matrix below and in "Not built and not claimed".

## The colour model

The picture inside NyrexVJ is written down like this, so that every conversion has a defined meaning:

- **Working picture:** 8 bit RGBA, display-referred, encoded with the sRGB curve, Rec. 709 primaries (the same as sRGB), white is 1.0. Layers, the composition and the master picture are made like this, and the blend modes work on these encoded values. This was always so; it is now written down, and `--color-selftest` measures it (8/8/8/8 bit integer, 256 distinct levels on a full width ramp).
- **Primaries** are defined by their chromaticities in `src/core/color_model.cpp` (white point D65 0.3127 / 0.3290; Rec. 709 R 0.640 0.330, G 0.300 0.600, B 0.150 0.060; Display P3 R 0.680 0.320, G 0.265 0.690, B 0.150 0.060; Rec. 2020 R 0.708 0.292, G 0.170 0.797, B 0.131 0.046). The conversion matrices are computed from them, not typed in; the tests check that white stays white, that the matrices are inverse to each other and that they match the published Rec. 709 values to four decimals.
- **Transfer curves:** sRGB, gamma 2.2, 2.4, 2.6, linear, PQ (SMPTE ST 2084, 10000 nits peak) and HLG (ARIB STD-B67). The tests check the round trip and published reference points (100 nits is the PQ value 0.5081, 10000 nits is 1.0).
- **Linear light** is only used inside a conversion: the converted value is decoded, the matrix applied in linear light and the result encoded again.

## Before and after: what really keeps what

"Before" cites the code of 0.52.0 (commit `ab3b34e`), "after" the code of this version. "Measured" means `NyrexVJ --color-selftest` or `--output-selftest` on Linux with Mesa llvmpipe; nothing was measured on a graphics card.

| Path | Before (0.52.0) | After (0.53.0) | Evidence |
| --- | --- | --- | --- |
| Layer, composition and master targets | RGBA8: `RenderTarget::create` is `GL_RGBA8`, `GL_UNSIGNED_BYTE` (`src/render/gl_util.cpp:93`), used for the layer, composition and master targets (`src/render/engine.cpp:1878`) | the same | measured 8/8/8/8 bit integer (`masterDepth` in the report) |
| Floating point targets | RGBA16F only where `createFloat` is called: the feedback buffers of node effects (`src/render/engine.cpp:2761`) and float routing captures (`src/render/routing_engine.cpp:19`); never the master | the same | measured 16/16/16/16 bit float for a `createFloat` target (`floatTargetDepth`) |
| Blending and transitions | on the encoded 8 bit values, in the layer shaders and in `transitionPass` (`src/render/engine.cpp:3434`) | the same; the colour stage comes after the transition | measured: with a LUT on, a deck transition fades first and the LUT is applied to the faded picture (`the colour stage is applied after the deck transition`) |
| Master grade | `finalPass` (`src/render/engine.cpp:3421`) | the same | not changed |
| Colour stage with LUT | none | one pass from the master to a scratch target and back (`Engine::applyColorStage`, `src/render/engine.cpp:862`, called after the master or transition pass, `:3564`), shader in `src/render/color_stage.cpp`; alpha is copied unchanged | measured against the processor model of the same `.cube` file (largest difference 1 level for a 33 point table with crossing channels) |
| Colour stage off | the pass does not exist | the pass does not exist: no program, no texture, no scratch target (`Engine::setColorStage(false,...)` destroys them) | measured: after switching on and off, or with no LUT, or with strength 0, the whole master picture is bit identical to the picture before (0 differing values), and the memory is 0 bytes |
| Output window frame buffer | `glfwCreateWindow` with no colour or depth hint (`src/render/output_window.cpp:231`; the only hints in the program are the context version in `src/main.cpp:495`), so the system default frame buffer | the same | measured 8/8/8/8 for the frame buffer of the main window on the virtual screen (`windowDepth`); output windows are made with the same defaults, but their own frame buffer was not measured and no real screen was used |
| Per output colour correction (Prompt 02) | gain, saturation, contrast, gamma, black lift in the output shader | the same, untouched | not changed |
| Output display conversion | none | in the output shader, after the colour correction and before edge blending, brightness and black lift, flat and spatial program alike (`flatFragmentSource`, `src/render/output_window.cpp:181`); skipped while the display is Rec. 709 with sRGB; not applied to the test pattern or sync bars | measured in the real window against the processor model: 0 levels difference for 8 display settings on 5 source colours, order after the colour correction, mask respected, spatial program converts too |
| NDI output | RGBX, 8 bit, read back from the output pass (`src/core/ndi_sender.cpp:111`) | the same, so it contains the display conversion | measured with the fake NDI library: the NDI frame equals the window picture with a display conversion on and differs from the picture without it |
| Spout output | `SendTexture` without a format of its own (`src/render/spout_output_win.cpp:49`), Windows only | the same | **not run**: Windows only, no Spout here |
| Master NDI, Spout, recorder source | the master picture `out_` (8 bit) | the master picture after the colour stage | the master is bit identical with the stage off |
| Recorder | 8 bit RGBA read back (`src/render/recorder.cpp:257`), converted to the pixel format of the encoder; ProRes uses 10 bit pixel formats (`src/media/video_recorder.cpp:65`) but its source is 8 bit; tagged Rec. 709 (`src/media/video_recorder.cpp:94`) | the same | read from the code; a ProRes 10 bit file holds an 8 bit picture and is not a 10 bit recording |
| Video decode 8 to 16 bit | planes keep their depth up to the conversion (`src/media/video_decoder.cpp:208`, `src/render/video_gpu.cpp:254`), the converted picture is RGBA8 (`:571`) | the same | `--video-selftest` (GPU conversion against FFmpeg's own, all clips pass); a 10 bit video is reduced to 8 bit by the conversion, there is no 10 bit picture path |
| Video matrix and range | 601, 709, 2020, YCoCg, limited and full range | the same | `nvj_media_tests`, `--video-selftest` |
| HDR video (PQ, HLG) | detected only (`videoTransferName`, `src/media/video_decoder.cpp:30`), converted with the matrix and range of the file, looks flat (KI-8) | optional tone mapping in the same conversion shader (`src/render/video_gpu.cpp:66`, `:245`), on the graphics card path only; processor conversion (`sws`) and HAP are not changed | measured: the shader matches the processor model to 0.0005 for six parameter sets; both test clips match the model on all locally flat samples (mean error 0.19 and 0.13 levels); an SDR clip stays bit identical; on the processor path the option changes nothing (difference 0) |
| 3D LUT import | none | `.cube` files in the folder `luts` of the data folder | `nvj_color_tests`, 289 672 checks: the colour model, the curves, tone mapping, gamut mapping, the `.cube` parser and sampler, damaged files and limits, and the JSON of settings and venues |
| 10 bit display, 10 bit SDI, HDR output, HDR metadata | not present | not present | **not built**, see below |

## The colour stage and LUTs

Put `.cube` files into the folder `luts` of the data folder (Windows `%APPDATA%\NyrexVJ\luts`, elsewhere `~/.config/NyrexVJ/luts`; `NYREXVJ_DATA_DIR` replaces the data folder; the button **Open LUT folder** creates and opens it). Switch on **Colour stage with LUT (experimental)** in Settings, Performance, Colour, choose the file and the strength.

- **Format.** Only 3D tables. `LUT_3D_SIZE` from 2 to 65, `DOMAIN_MIN`, `DOMAIN_MAX` and `LUT_3D_INPUT_RANGE` are read, `TITLE` is kept, comments with `#` and a byte order mark are accepted. 1D tables (`LUT_1D_SIZE`) are refused with a message. The first table line is red = 0, green = 0, blue = 0 and red changes fastest, as in the Adobe and Resolve files. A file larger than 64 MB, a table with the wrong number of lines, a value outside -16 to 16, an unknown line or a damaged number is refused with the line number; nothing is applied, and the picture is untouched.
- **Names.** A file name with `/`, `\`, `:` or `..`, or longer than 120 bytes, is never opened.
- **Sampling.** The table is a 3D texture (RGB, 16 bit float) read with trilinear filtering at texel centres. This is the usual way; a table with few points gives a coarser curve than one with 33 points. The processor model in the tests samples the same way.
- **Strength.** 0 is the original picture, 1 the table; in between the two are mixed. At strength 0 the pass is skipped.
- **Alpha** is copied through.
- **Cost.** One more picture of graphics memory (width x height x 4 bytes) plus the table (size cubed x 6 bytes; 33 points are 216 kB, 65 points 1.6 MB). The settings show it; `--color-selftest` checks that the reported memory equals the planned memory and that it is freed again.
- **Where it sits.** After the master and deck transition, before the outputs, NDI, Spout, the recorder and the previews. It is therefore also in what the recorder and the master senders see.

## HDR videos to SDR

Switch on **Tone map HDR videos to SDR** in the same section; **Tone mapping** chooses between Soft shoulder and Filmic. It acts only on videos whose file says PQ (SMPTE ST 2084) or HLG (ARIB STD-B67) and only while **Convert video colours on the graphics card** is on. Videos that the processor converts (the setting off, or a video the graphics card path cannot take) and HAP videos are not changed; the settings text says so.

The steps, in the shader of the video conversion and in `hdrToSdr` of `src/core/color_model.cpp`, which the tests compare against each other:

1. The video is converted from YCbCr to RGB with its own matrix and range, as before. The result is the non-linear signal of the file.
2. **PQ:** decoded to nits with the PQ curve. **HLG:** decoded with the inverse HLG curve to scene light and shaped with the HLG system gamma 1.2 + 0.42 log10(peak / 1000) (peak limited to 400 to 2000 for this), as BT.2100 describes.
3. Divided by the **reference white** (**Reference white (nits)**, 203 by default, 80 to 400), so that normal white is 1.0.
4. The **luminance** is tone mapped and the colour is scaled by the same factor, so hue and saturation are kept. *Soft shoulder* (default): identity up to 0.8 and then a smooth roll-off that reaches exactly 1.0 at the video peak (**Video peak (nits)**, 1000 by default, 400 to 4000 on the settings page, up to 10000 in `settings.json`). *Filmic*: the Hable curve with exposure 2, scaled to reach 1.0 at the peak.
5. The primaries are converted from Rec. 2020 (or Display P3 if the file says so) to Rec. 709 in linear light.
6. **Gamut mapping:** a colour that falls outside Rec. 709 is moved towards its own luminance until no channel is negative, and scaled down if a channel is above 1. No channel is simply clipped.
7. Encoded with the sRGB curve into the normal picture.

This is a sensible, documented conversion; it is not the only one and it is not calibrated against a reference monitor. The value of the two settings (reference white and peak) depends on how the video was mastered; `--bench-video` and the video monitor show the transfer name of a video.

## The display of an output

For every output, **Display of this output** has a colour gamut (Rec. 709, Display P3, Rec. 2020) and a transfer curve (sRGB, gamma 2.2, 2.4, 2.6). The default is Rec. 709 with sRGB and changes nothing: the shader does not run the conversion. For anything else the picture is decoded with the sRGB curve, converted from Rec. 709 to the chosen primaries in linear light and encoded with the chosen curve. The signal stays 8 bit and SDR: this is not an HDR output. It is meant for a projector or screen that you have set to a wide gamut or to a gamma of 2.4, and it assumes the picture is correct on a Rec. 709 sRGB screen. It is saved in the venue file under `display` and only when it differs from the default, so a venue without it is the same file as before.

## What was measured

All numbers are from one run of `--color-selftest` (37 checks; 28 when FFmpeg is not available, because the three video checks are then skipped, or a failure when `NYREXVJ_REQUIRE_VIDEO` is set) and `--output-selftest` (632 checks with the fake NDI library) on Linux, Mesa llvmpipe (software OpenGL 4.5), a virtual screen and no graphics card. They are an example, not a promise for other machines.

| What | Result |
| --- | --- |
| Window frame buffer, master target, float target | 8/8/8/8 bit, 8/8/8/8 bit integer, 16/16/16/16 bit float |
| Distinct levels of a full width ramp in the master picture | 256 (at 960 pixels wide) |
| Identity LUT (17 points) | no pixel differs from the picture without LUT, 256 levels kept |
| Inverting LUT | the whole ramp is inverted within 0 levels |
| 33 point LUT with crossing channels against the processor model | largest difference 1 level, mean 0.02 over 192 pixels |
| LUT with a smaller input domain against the model | 0 levels |
| Strength 0.5 with an inverting LUT | 128 +- 1 everywhere |
| Display conversion in the output window against the model | 0 levels (8 settings x 5 colours) |
| HDR shader against the model, 6 parameter sets, 35 colours each | largest difference 0.00049 (half float precision of the test target) |
| HDR test clips `test_hdr_pq.mp4`, `test_hdr_hlg.mp4`, flat samples | 100 % inside the quantisation band, mean error 0.19 and 0.13 levels, 3894 flat samples of 9216 |
| Frame time with four ramp layers at 960x540, software OpenGL, 40 frames per state | 24.9 ms without the colour stage, 29.3 ms with a 33 point LUT (+5.2 ms), 23.3 ms without again. The check only requires that the cost stays bounded; the numbers move by a few milliseconds from run to run. They are software numbers; no graphics card was used |
| Memory of the stage with a 33 point LUT at 960x540 | 2 289 222 bytes, equal to the plan |

The report of `--color-selftest --bench-out report.json` contains all of these as `checks`, `summary` and `benchmark`.

## Fallbacks

| Situation | What happens |
| --- | --- |
| The colour stage shader or the LUT texture cannot be made | the stage stays off, the picture is untouched, the settings show the reason |
| The `.cube` file is damaged, too big, 1D or has an unsafe name | refused with the reason and the line number; the stage has no LUT and does not change the picture |
| Strength 0 or no LUT chosen | the pass is skipped |
| The scratch target cannot be made (out of graphics memory) | the stage switches itself off and shows the reason |
| An unknown or damaged value in `settings.json` or a venue (`colorStage`, `colorLut`, `hdrTone`, `display`) | read as the default; a `colorLut` that is not a safe file name is dropped |
| The video goes through the processor conversion or is a HAP video | no tone mapping, the picture is as before |
| The video is SDR | the option changes nothing (bit identical, tested) |
| The display conversion is not wanted | choose Rec. 709 and sRGB, or press Reset display |

## Reproduce

```
cd build
ctest -R nvj_color_tests
xvfb-run -a -s "-screen 0 1280x720x24" env LIBGL_ALWAYS_SOFTWARE=1 \
  ./NyrexVJ --fresh --no-update-check --no-welcome --color-selftest ../tests/data --bench-out color.json
xvfb-run -a -s "-screen 0 1280x720x24" env LIBGL_ALWAYS_SOFTWARE=1 \
  NYREXVJ_NDI_LIB=./fake_ndi.so ./NyrexVJ --fresh --no-update-check --no-welcome --output-selftest
```

`--color-selftest [FOLDER]` reads `test_hdr_pq.mp4`, `test_hdr_hlg.mp4` and `test_h264.mp4` from the folder (default `tests/data`). The JSON has the measured depths, the LUT and shader differences, the clip agreement and a `benchmark` block that says it was measured on software OpenGL.

## Not built and not claimed

- **10 bit output.** The master picture is 8 bit. A 10 bit video is converted to 8 bit; a 10 bit pixel format of the recorder holds an 8 bit picture. NDI is sent as 8 bit RGBX, Spout as the Spout library does it by default. A graphics card that offers a 10 bit window or an SDI card that takes 10 bit was not used and the program does not ask for it.
- **HDR output and HDR metadata.** Nothing is sent as PQ or HLG to a screen, NDI, Spout, the recorder or an SDI card, and no static or dynamic HDR metadata is written. HDR input is turned into SDR (if switched on) and shown as SDR.
- **A floating point master.** Only node feedback and float routing use RGBA16F.
- **Colour management of the screen.** NyrexVJ does not read ICC profiles or the EDID of a screen; the display setting is chosen by hand.
- **Colour management of layers.** Layers are blended as before in the encoded sRGB values; a linear-light blend would change every existing look and is not offered.
- **LUTs per layer, per output or per slice, 1D tables, `.3dl` and `.look` files.** Per slice colour correction stays with the output colour controls.
- **Real hardware.** A real screen or projector with another gamut, a 10 bit or HDR chain, a capture card and a real graphics card were not used. The manual tests `M-COLOR-1` to `M-COLOR-4` in [TESTING.md](TESTING.md) describe how to try them.

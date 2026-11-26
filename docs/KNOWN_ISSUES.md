# Known issues and open risks

Problems that are known today, how to see each one, and how it is known. This page lists what was found by reading the code, by a test, or by an earlier run. It does not list wishes (see [ROADMAP.md](ROADMAP.md)) and it does not list hardware that was never tried; that is in [COMPATIBILITY.md](COMPATIBILITY.md) and [TESTING.md](TESTING.md).

Checked at version: 0.50.0

KI-9 to KI-11 were added at 0.51.0, and KI-1 to KI-8 were not walked through again at that version; the release did not change the areas they name. KI-15 and KI-16 were added and KI-8 was changed at 0.53.0; KI-17 and KI-18 were added at 0.54.0; KI-19 to KI-22 were added at 0.55.0; the other entries were not walked through again.

How to read an entry: **Known by** says whether it was seen running, found in a test, or read from the source. A problem that was only read from the source says so, because nobody has watched it happen.

Before a release, walk through the list ([RELEASE.md](RELEASE.md), step 12): try the steps of every entry in an area the release touched, correct the entry, and set the version line above.

## Open

### KI-1 A video cut short stays silent without a message

- **Area:** video clips
- **Known by:** a test that ran (`nvj_media_tests`, `testMediaOutage`, see [TESTING.md](TESTING.md))
- **What happens:** a video file cut to a tenth of its size opens, shows no pictures and gives no error message within a second. Half of the file plays. An empty file is refused with a message.
- **Steps:** `head -c 1819 tests/data/test_h264.mp4 > cut.mp4`, put `cut.mp4` into the media folder, drop it into a cell and trigger it. (The size is a tenth of the 18,194 bytes of the test file. The test cuts its own files; this exact command was not run.)
- **Impact:** a show with a damaged file shows an empty layer and the person is not told why.
- **Workaround:** none in the program. Check files before the show; `NyrexVJ --video-selftest FOLDER` decodes the clips of a folder.

### KI-2 No reaction to a monitor being unplugged

- **Area:** outputs and projectors
- **Known by:** read from the source. `src/render/output_window.cpp` asks GLFW for the monitor list when it needs it and registers no monitor callback. Nobody has unplugged a monitor during a run.
- **What happens:** unknown. The list is read again only when the program needs it, so an output window on a monitor that went away is not handled at the moment of unplugging.
- **Steps:** open an output window on a second display, unplug the display and watch the output window, the status bar and the log. This is the manual test `M-DISP-1`.
- **Impact:** a projector that loses power or a cable that falls out may leave a window on a monitor that no longer exists.
- **Workaround:** none known.

### KI-3 A reset of the graphics driver is not handled

- **Area:** rendering
- **Known by:** read from the source. A search of `src` for a robust context, `glGetGraphicsResetStatus` and a lost-context error finds nothing. Out of memory is checked only when shader buffers and 3D meshes are created (`src/render/engine.cpp`, `src/render/scene_player.cpp`). Nobody has made a driver reset happen during a run.
- **What happens:** unknown: the program may crash, freeze or go on with a black picture. [SHADERS.md](SHADERS.md) already says that a very heavy shader can make the driver reset and on Windows can close the program.
- **Steps:** on Windows start a show with an output window and press Win+Ctrl+Shift+B once, which makes Windows restart the graphics driver; watch the picture and the log for a minute. This is the manual test `M-RQ-3`.
- **Impact:** a driver timeout during a show is the usual way a Windows show ends without warning.
- **Workaround:** keep shaders light, keep the driver up to date, keep a hot standby PC ([SYNC.md](SYNC.md)).

### KI-4 The program has never been run on Windows by a test

- **Area:** everything that is Windows only
- **Known by:** the workflow. The `windows` job builds the program, runs `ctest` and the Python checks, and never starts `NyrexVJ.exe`.
- **What happens:** the Windows code of the system information in the run reports (`src/platform/platform_win.cpp`: processor name from the registry, memory, system version through `RtlGetVersion`) compiles but has never run. The same holds for Spout, the Windows updater, hot plugging and the driver counters of `scripts/measure_live.py`.
- **Steps:** on a Windows PC run `NyrexVJ.exe --fresh --no-update-check --no-welcome --soak 10 --bench-out soak.json` and open `soak.json`: `conditions.system` should name the system, the processor, the cores and the memory, `conditions.graphics` the card and the driver.
- **Impact:** a hardware gate report from Windows could carry empty or wrong system information. The gate refuses an empty operating system, but nothing in the build would notice a wrong processor name or memory size.
- **Workaround:** fill the system information by hand in the measurement template ([templates/measurement-report.json](templates/measurement-report.json)).

### KI-5 `--soak-media` copies files into the data folder

- **Area:** test tooling
- **Known by:** read from the source and run. `importAsset` copies each file into the media or model folder of the data folder.
- **What happens:** the media of the folder you give stay in that data folder after the run. To protect a real show, the option refuses to run unless `NYREXVJ_DATA_DIR` is set to a scratch folder.
- **Steps:** run `NyrexVJ --fresh --soak 5 --soak-media FOLDER` without `NYREXVJ_DATA_DIR`: exit code 1 and a message. With the variable set it runs and the files are in `$NYREXVJ_DATA_DIR/media` and `models`.
- **Impact:** none when the variable is set. Shader files in the folder are imported too and are not used by the run.
- **Workaround:** always use a scratch data folder for soak runs.

### KI-6 Presets and settings have no checked version

- **Area:** file formats
- **Known by:** a test that ran (`nvj_compat_tests`) and [INTERFACES.md](INTERFACES.md)
- **What happens:** a preset or a settings file written by a newer version loads in an older one. What the older version does not know is ignored, without a message, and is gone when the file is saved again. The same holds for cue lists and timelines: their version number is written, not checked. Venue setups and controller profiles from a newer version are refused with a message.
- **Steps:** add a key to a copy of `tests/data/compat/composition_v0.50.0.json` and load it as a preset: it loads, and the key is not in the file when the preset is saved again (the load is tested in `nvj_compat_tests`; the save was read from the code).
- **Impact:** going back to an older release with a data folder that a newer release has written can lose settings and the newer parts of presets.
- **Workaround:** keep a copy of the data folder before updating ([RELEASE.md](RELEASE.md), rollback).

### KI-7 `--library-kind 11` selects Favorites instead of Routing

- **Area:** a test option
- **Known by:** read from the source, not run. `src/app/app.cpp` limits the value to 0 to 10 while the library has twelve kinds (0 to 11); kind 11 is Routing.
- **Steps:** `NyrexVJ --library-kind 11` and look at the library buttons.
- **Impact:** only screenshots and tests that want the Routing list at start. The option is internal.
- **Workaround:** click the button.

### KI-8 HDR video is shown without tone mapping unless you switch it on

- **Area:** video clips, colour
- **Known by:** a test that ran: `--color-selftest` plays `test_hdr_pq.mp4` and `test_hdr_hlg.mp4` with the option off and on ([COLOR.md](COLOR.md))
- **What happens:** since 0.53.0 PQ and HLG files can be tone mapped to SDR (Settings, Performance, Colour, **Tone map HDR videos to SDR**), but the option is off by default, so a default show still shows them flat. It acts only while the graphics card converts the colours; videos that the processor converts and HAP videos stay flat even with the option on.
- **Steps:** play `tests/data/test_hdr_pq.mp4`, then switch the option on.
- **Workaround:** switch the option on and keep **Convert video colours on the graphics card** on, or convert HDR material to SDR before the show.

### KI-9 Screen readers do not work with the interface

- **Area:** accessibility
- **Known by:** the documentation of the interface library says that accessibility features are not supported; nothing was run with a screen reader
- **What happens:** the library draws every control itself and offers no accessibility tree, so a screen reader sees one window and no controls.
- **Steps:** start a screen reader and open NyrexVJ.
- **Impact:** people who rely on a screen reader cannot use the program. Keyboard navigation, high contrast and scaling help people who see the screen; they do not replace this.
- **Workaround:** none. A different toolkit would be a rewrite and is not proposed.

### KI-10 About 80 sizes stay fixed in pixels

- **Area:** interface scaling
- **Known by:** counted in the source (82 places that write `ImVec2(width, height)` with numbers outside the two helpers and the theme, `grep`, 0.51.0); the self test checks that the seven main panels, the Settings window and the manual window fit at 100, 125, 150 and 200 percent on windows from 1024x600 to 2560x1440, and it has not looked at every other window
- **What happens:** most spacing follows the Interface size through the style, and windows that open at a fixed size are scaled and kept inside the main window. Some small paddings and button widths are written as pixels and stay the same. At 150 or 200 percent such a button can look small next to the text.
- **Steps:** set Interface size to 2.00 and open the windows one after another.
- **Impact:** looks, not function; a very small or very large window may not look balanced.
- **Workaround:** none needed for use.

### KI-11 The keys inside the editors cannot be changed

- **Area:** shortcuts
- **Known by:** read from the source: the Clip Creator, node editor, timeline and shader editor read `Ctrl+Z`, `Ctrl+Y`, `Delete`, `Space` and similar keys directly
- **What happens:** Settings, Shortcuts changes the thirteen global actions only. The keys of the editors and the number keys 1 to 8 stay as they are.
- **Steps:** try to give the node editor another undo key.
- **Impact:** a person who needs different keys in an editor cannot have them.
- **Workaround:** none.

### KI-12 Two Fullscreen buttons on the output page share one ID

- **Area:** interface, outputs
- **Known by:** seen running. Dear ImGui reports an ID conflict for the two buttons with the text Fullscreen or Windowed: the one next to Open all (`drawOutputControls`) and the one of the selected output. The code is the same in the 0.51.0 release; it was found while the 3D mapping block was checked on screen and was not changed by it.
- **What happens:** both buttons have the same ID because neither is inside its own ID scope, so the library shows its ID conflict warning when the mouse is over either button, and a click may reach the other button.
- **Steps:** open Settings, Output, hold the mouse over one of the two Fullscreen buttons and read the ID conflict warning of the library; click the lower one with an output window open and see which window changes.
- **Impact:** a click on one of the two buttons may act on the other. Nobody has seen it do so; it was read from the library's warning.
- **Workaround:** use the window of the output itself (`F11` in an output window, or the shortcut list) instead of the two buttons.

### KI-13 3D mapping has only been tested against a computer model

- **Area:** 3D mapping
- **Known by:** tests that ran (`nvj_spatial_tests`, `--output-selftest`, [ADR 0045](adr/0045-spatial-mapping.md)). The pixels of the real output window agree with a CPU model of the same camera, and the calibration finds a pose that was used to make the points. No real projector, room or measuring tape was involved.
- **What happens:** unknown on real hardware. The lens model is an ideal pinhole with a lens shift; it has no lens distortion, no focus and no keystone from the projector's own correction. Points measured by hand carry errors that the test points do not, and the calibration reports the error it finds, but it cannot tell a wrong measurement from a wrong lens.
- **Steps:** this is the manual test `M-SPATIAL-1` and `M-SPATIAL-2` in [TESTING.md](TESTING.md): measure four to six points in a real room, calibrate and look at the picture on the wall.
- **Impact:** a person may need more points, a better measurement or a projector without its own keystone correction before the picture lands within a few pixels.
- **Workaround:** use the error per point to find the point that is wrong, and switch off the projector's own keystone correction.

### KI-14 Without a depth buffer surfaces do not hide each other

- **Area:** 3D mapping, outputs
- **Known by:** read from the source and covered by a test with a forced setting. The output window asks for its depth bits when it is opened; a window without them draws the surfaces in list order and the interface says so. No real driver was seen to give a window without a depth buffer.
- **What happens:** a pillar in front of a wall does not hide the wall unless the pillar comes later in the list.
- **Steps:** open an output with a wall and a pillar behind it in the list and check the orange line in the 3D mapping block.
- **Impact:** a wrong picture where surfaces overlap in the view of the projector, on a driver that gives no depth buffer.
- **Workaround:** put the nearer surface later in the list.

### KI-15 There is no 10 bit or HDR output

- **Area:** colour, outputs, recording
- **Known by:** read from the source and measured with software OpenGL ([COLOR.md](COLOR.md)). The master picture and the layers are 8 bit integer; the output window is made without a colour depth hint and was measured as 8/8/8/8 on a virtual screen; NDI is sent as 8 bit RGBX; the recorder reads an 8 bit picture. A real graphics card, a 10 bit screen and an HDR chain were not used.
- **What happens:** a 10 bit video is reduced to 8 bit when it is converted, HDR is tone mapped to SDR (if switched on) and never sent as PQ or HLG, no HDR metadata is written anywhere, and a 10 bit pixel format of the recorder (ProRes) holds an 8 bit picture. Gradients can show banding on a screen that could show more.
- **Steps:** run `NyrexVJ --color-selftest` and read the first three checks (frame buffer, master and float target depth); the settings page shows the master depth the graphics card reports.
- **Impact:** an event that needs a true 10 bit or HDR chain cannot be done with this program.
- **Workaround:** none. Dither or add a little grain to gradients that band.

### KI-16 Tone mapping and display conversion were compared with a computer model only

- **Area:** colour
- **Known by:** tests that ran (`nvj_color_tests`, `--color-selftest`, `--output-selftest`). The shaders match the model, the model matches the published curves and matrices, but nothing was compared with a reference player, a measuring device or a real wide gamut screen.
- **What happens:** unknown: the HDR picture is a sensible conversion, not a calibrated one; the display setting assumes that the screen really is in the gamut and curve you choose.
- **Steps:** manual tests `M-COLOR-1` to `M-COLOR-4` in [TESTING.md](TESTING.md).
- **Impact:** an HDR file may look too dark or too bright against another player; a wrong display setting makes colours dull or too strong.
- **Workaround:** set **Reference white (nits)** and **Video peak (nits)** by eye, and leave the display of an output at Rec. 709 and sRGB unless a screen is known to be set differently.

### KI-17 Optical flow was tested on generated clips and software OpenGL only

- **Area:** video, optical flow
- **Known by:** tests that ran (`nvj_flow_tests`, `--flow-selftest`; [FLOW.md](FLOW.md)). The four test clips are made from a moving synthetic picture; nothing was run on a real graphics card, on Windows, with real footage or next to VRChat.
- **What happens:** unknown for real material: faces, water, smoke, fine particles, repeating patterns and strobing light may show halos, wobble or smearing; the analysis cost at 1080p and 4K on real hardware and the effect of another program using the processor are not measured.
- **Steps:** manual tests `M-FLOW-1` to `M-FLOW-4` in [TESTING.md](TESTING.md).
- **Impact:** with **Optical flow mode** on, a video may look worse than the plain picture, or the analysis may fall back to the plain picture more often than in the tests.
- **Workaround:** leave **Optical flow mode** on Off (the default), or use Motion blur or the vector view only to check a clip; lower the quality tier or the memory limit if the video monitor shows late or dropped frames.

### KI-18 A fast fade to black can be read as a scene cut, and borders can show a halo

- **Area:** video, optical flow
- **Known by:** a test that ran (`nvj_flow_tests`: layers moving apart, hard cuts) and read from the source. A scene cut is declared when most textured blocks have no good match. In `nvj_flow_tests` a fade to black over 4 frames (a quarter of the brightness per frame) was flagged in all 4 pairs at all three tiers, a fade over 15 frames and a dissolve between two textured shots over 8 frames were not flagged. Where the flow is not trustworthy the picture falls back to the nearer real picture, which leaves a soft halo at the border of a moving object over another background (28.3 dB against 26.5 dB for a plain blend in the layers test).
- **What happens:** during such a fast fade the current picture is held (the same as without the feature), and the border of a moving object can look soft.
- **Steps:** play a clip with a quick fade to black in the Interpolate mode and look at the video list on the Performance page: the picture counts show held pictures during the fade, and the tooltip of the Pictures column shows the hard cuts found.
- **Impact:** no wrong picture, only less smoothness at those places.
- **Workaround:** none needed; switch the mode off for a clip that is made of quick fades.

### KI-19 Network streams were tested with generated senders on one machine only

- **Area:** live inputs, network streams
- **Known by:** tests that ran (`nvj_stream_tests`, `scripts/test_stream_input.py`, `--stream-selftest`; [STREAMS.md](STREAMS.md)). Every sender was `ffmpeg` or a small RTSP server written for the test, on the loopback address of the same computer, with software OpenGL; nothing ran with a real RTSP camera, a real network, Windows, H.264 or H.265 over RTSP, multicast, SRT encryption or next to VRChat.
- **What happens:** unknown for real conditions: lost and reordered packets, Wi-Fi, a camera that sends B-frames or odd time stamps, a server that asks for a login in a way the test server did not, many streams at once, and the processor and graphics memory of four 1080p or 4K streams.
- **Steps:** manual tests `M-STREAM-1` to `M-STREAM-5` in [TESTING.md](TESTING.md).
- **Impact:** with **Allow network stream inputs** on, a stream may start slower, stutter, jump to the next key frame more often than in the tests, or not open at all; the details of the clip show the reason from FFmpeg.
- **Workaround:** leave the setting off (the default) when the show must not depend on it; use the **Smooth** profile on a bad network; send from a source that you tested before the show.

### KI-20 A stream address is saved with its password in settings.json

- **Area:** live inputs, network streams, settings
- **Known by:** read from the code (`src/core/settings.cpp`, key `streamInputs`) and a test that ran (`nvj_stream_tests`: the saved address is the typed one). The interface and the log show the address with the password and secret option values hidden (`maskUrl`); the file does not.
- **What happens:** anyone who can read `settings.json` can read the password of an RTSP camera or an SRT `passphrase` that was typed into the address.
- **Steps:** type an address with a password, open `settings.json` in the user data folder.
- **Impact:** a password in a file of the user's own profile, the same place as the other settings.
- **Workaround:** use a camera account that only has the right to view, do not reuse a password, and do not share the settings file or a screenshot of the folder.

### KI-21 A listener opens a port on all network addresses

- **Area:** live inputs, network streams, security
- **Known by:** read from the code (`makeOpenPlan` in `src/core/stream_url.cpp`) and the scenarios that ran: a `udp://` address and an `srt://` address with `mode=listener` open a port on `0.0.0.0` unless the address names one. The page shows the port in orange.
- **What happens:** anyone who can reach this computer on that port can send a picture, and the program decodes it. The decoding is limited (codecs, size, number of streams, time, see [STREAMS.md](STREAMS.md)) but is done by FFmpeg's demuxers and decoders, which were not fuzzed in this project.
- **Steps:** enter `udp://@:5000` in a Stream Input clip and look at the page.
- **Impact:** an unwanted sender on the same network can show a picture in the show, or send damaged data to the decoders.
- **Workaround:** use the listener only on a network you trust, allow the port in the firewall for that network only, name the address of the network card in the address (`udp://@192.168.1.5:5000`), or let the program connect to the sender instead (`srt://host:9000` without `mode=listener`, RTSP).

### KI-22 The real NDI runtime was never loaded, and NDI 6 is not proven

- **Area:** live inputs, NDI
- **Known by:** tests that ran (`nvj_ndi_receive_tests`, `nvj_ndi_v3_tests`, `--output-selftest`, all against the stand-in library `tests/fake_ndi.cpp`) and read from the code ([STREAMS.md](STREAMS.md)). The program looks for `libndi.so.6` before `libndi.so.5` and reads the version from the runtime that it finds; nothing was run with a vendor library.
- **What happens:** unknown: whether the six functions the program calls behave like the stand-in on a real runtime, whether `NDIlib_version` returns text on every version, whether a real source with alpha, a changing size or a high frame rate is received correctly, and what NDI 6 adds that the program does not use.
- **Steps:** manual tests `M-STREAM-4` and `M-STREAM-5` in [TESTING.md](TESTING.md); the line "NDI runtime:" in the NDI output settings shows what the runtime reports.
- **Impact:** an NDI problem on a real machine would be a first finding, not a regression.
- **Workaround:** check NDI with the free NDI Tools before the show; use Spout on Windows for a program on the same computer.

## Limits of the evidence, not of the program

These are not bugs. They are what the numbers cannot tell you.

- **Software OpenGL says nothing about speed.** The continuous build renders at a few frames per second on llvmpipe. Its frame times and "over budget" counts are not judged and mean nothing for a graphics card. The `hardware-release` profile of the release gate is the only place with limits for frame times, and it has never been run on a graphics card.
- **The limits of the gates are proposals.** The numbers in `tests/hardware/gates.json` were chosen as reasonable starting points, not measured on any card. The maintainer decides.
- **The estimate of graphics memory counts what the program allocates.** The driver can use more. `scripts/measure_live.py` reads the card itself and is the check against that; it has never been run against a card in this project.
- **The renderer class is a guess from a name.** A software renderer whose name is not in the list of `src/core/run_conditions.cpp` would be called `gpu-reported`. A hardware result also needs a named tester and machine for that reason.
- **The old-format fixtures were written by hand.** They show what the loaders accept today, not what an older release wrote. No file of an older release has been collected.
- **Codec runs are on a software decoder.** The 20 clips passed on FFmpeg's decoders in software. No hardware decoder was exercised.
- **The display scale was tested with injected values.** `--interface-uitest` sets 1.25, 1.5 and 2.0 by hand; the value that the operating system reports for a real monitor was never read from a real 125 to 200 percent display, and the high contrast and focus colours were judged by calculation and by screenshots, not by people.
- **Two real PCs have not been used for sync or hot standby.** The tests use two processes on one PC ([SYNC.md](SYNC.md)).
- **The edge blend of two projectors in space is the flat model.** Each output blends in its own picture; it does not know which surface the light hits. The test shows that two overlapping projectors add up to one picture in the model; no two real projectors were overlapped.

# Testing live stability and hardware

This page says what is tested automatically, what needs real hardware and what has been run on real hardware so far. Design and reasons: [ADR 0027](adr/0027-live-stability-and-hardware-test-matrix.md).

The short version: **no hardware result has been recorded yet.** Everything below that says "open" is open. Nothing in this project was run on a real NVIDIA, AMD or Intel graphics card, with a real projector, camera, MIDI controller, Art-Net node, DJ player or VRChat.

## The four states

| State | Meaning |
| --- | --- |
| n/a | The feature does not exist on that platform (Spout on Linux). |
| implemented | The feature exists. No run is recorded in `tests/hardware/results.json`. |
| automated | An automatic test ran and is recorded, without real hardware (software OpenGL, fake libraries, test sources). |
| hardware | A manual test ran on real hardware and is recorded with date, build, tester, environment and evidence. |

The continuous build runs the unit tests on Windows and on Linux for every change. Those runs are not copied into the results file: it records only runs that somebody documented on purpose. A cell therefore says "implemented" when nothing was recorded, even if a unit test covers part of it.

## The matrix

The table is generated from `tests/hardware/matrix.json` and `tests/hardware/results.json` by `python3 scripts/check_test_matrix.py --write`. The script checks that every test it names exists (unit tests in `CMakeLists.txt`, options in `src/main.cpp`, scripts, documents, sources), that results are complete, and that the table below is current. A result marked `hardware` is refused when its environment names a software renderer or a virtual machine.

<!-- matrix:start -->
| Area | Windows | Linux | Automatic checks | Manual tests |
| --- | --- | --- | --- | --- |
| Rendering, shaders and 3D scenes on graphics card vendors | implemented | automated 4/5 | 5 | M-RENDER-1 open |
| Spout output to OBS, Unreal or VRChat capture (Windows) | implemented | n/a | 2 | M-SPOUT-1 open |
| Spout input from Unreal or Unity (Windows) | implemented | n/a | 1 | M-SPOUTIN-1 open |
| NDI output | implemented | automated 2/2 | 2 | M-NDIOUT-1 open |
| NDI input | implemented | automated 3/3 | 3 | M-NDIIN-1 open |
| Recording the final picture to a video file | implemented | automated 3/3 | 3 | M-RECORD-1 open; M-RECORD-2 open; M-RECORD-3 open; M-RECORD-4 open; M-RECORD-5 open |
| Cameras and capture cards | implemented | automated 2/3 | 3 | M-CAM-1 open |
| Several displays and projectors, hot plugging | implemented | automated 3/3 | 3 | M-DISP-1 open |
| MIDI controllers and MIDI clock | implemented | automated 2/3 | 3 | M-MIDI-1 open |
| Art-Net / DMX output | implemented | automated 2/2 | 2 | M-ART-1 open |
| DJ sync: Pro DJ Link, MIDI clock, timecode | implemented | automated 4/4 | 4 | M-SYNC-1 open |
| FFmpeg, hardware decoders and the fallback to the processor | implemented | automated 3/4 | 4 | M-FF-1 open |
| Plugin errors, limits and recovery | implemented | automated 3/3 | 3 | none |
| Long shows: frame times, memory growth, threads and handles over time | implemented | automated 4/5 | 5 | M-SOAK-1 open |
| Restart and recovery after a crash | implemented | automated 2/3 | 3 | M-REC-1 open |
| Running next to VRChat (CPU, GPU and VRAM shared) | implemented | n/a | 0 | M-VRC-1 open |
| Media outages: file removed or truncated while it plays | implemented | automated 1/1 | 1 | M-OUT-1 open |
| Web interface and REST API | implemented | automated 3/3 | 3 | M-API-1 open; M-API-2 open; M-API-3 open |
| ISF shader import: generators, image filters, several passes | implemented | automated 4/4 | 4 | M-ISF-1 open; M-ISF-2 open; M-ISF-3 open; M-ISF-4 open |
| Show timeline: keyframe lanes, cues on a ruler, timecode following, hand moves | implemented | automated 4/4 | 4 | M-TL-1 open; M-TL-2 open; M-TL-3 open; M-TL-4 open |
| User manual and website: the manual window, the generated website, the pictures, English and German text | implemented | automated 4/4 | 4 | M-MN-1 open; M-MN-2 open; M-MN-3 open; M-MN-4 open |
| Live workflow: launch quantize, favorites and tags, replace everywhere, MIDI feedback, controller profiles | implemented | automated 2/2 | 2 | M-LW-1 open; M-LW-2 open; M-LW-3 open |
| Show packages: export, import, dry run, path repair, rollback, the Show package window | implemented | automated 4/4 | 4 | M-SP-1 open; M-SP-2 open; M-SP-3 open |
| Community packages: .nyxpack, versioned manifest, dependencies, conflicts, plugin consent, update, removal and rollback, the Community packages window | implemented | automated 5/5 | 5 | M-CP-1 open; M-CP-2 open; M-CP-3 open |
| Text clips, limited particle clips and effect chains: fonts and Unicode, transparency, audio and beat modulation, particle limits, damaged files, chain save, search, preview, export and import | implemented | automated 6/6 | 6 | M-GN-1 open; M-GN-2 open; M-GN-3 open |
| GPU particles with transform feedback and a processor fallback: card against processor reference, repeatability, ring reuse, memory budget, every fallback, audio and beat modulation, state left clean | implemented | automated 4/4 | 4 | M-GP-1 open; M-GP-2 open; M-GP-3 open |
| Graphics memory diagnostics: driver readout kept apart from the estimate, P95 and P99, dropped frames, upload traffic, eight warnings with hysteresis, an optional memory budget that releases only idle clips by priority and never a playing one, the pressure ladder and its recovery | implemented | automated 4/4 | 4 | M-GD-1 open; M-GD-2 open; M-GD-3 open |
| Shader editor with a live preview: text editing with undo and redo, highlighting, compiler messages on their lines, a compile that waits for typing to stop and never blocks, the last working version kept on every failure, an isolated preview that never touches a playing clip, a cost control, templates, safe saving as a shader asset with a conflict check and hot reload | implemented | automated 5/5 | 5 | M-SE-1 open; M-SE-2 open; M-SE-3 open; M-SE-4 open |
| Video routing: route clips that play a layer, a group of layers or the master (same frame, or the previous frame as feedback), loops refused, GPU only copies with a memory limit and release, copies for each deck of a transition, sender sources for Spout, NDI and the recording, and the Video routing window | implemented | automated 4/4 | 4 | M-VR-1 open; M-VR-2 open; M-VR-3 open; M-VR-4 open |
| Network sync: one master and followers over UDP with a signed protocol, a state snapshot with incremental updates, sequence numbers, packet loss and reconnect, a clock estimate for the beat and the timecode, missing media shown instead of sent, version negotiation, and the Network sync block on the Sync page | implemented | automated 6/6 | 6 | M-MS-1 open; M-MS-2 open; M-MS-3 open; M-MS-4 open; M-MS-5 open |
| Hot standby: a primary and a standby PC with readiness checks, health reports, a takeover by hand or automatic, a leader epoch against two leaders, a held output on the standby, and the Hot standby window | implemented | automated 6/6 | 6 | M-HS-1 open; M-HS-2 open; M-HS-3 open; M-HS-4 open |
| Release quality: gates for crash freedom, growth and frame times, the compatibility matrix, old and new file formats, and the release checklist | implemented | automated 8/8 | 8 | M-RQ-1 open; M-RQ-2 open; M-RQ-3 open; M-RQ-4 open |
| Interface and accessibility: workspaces and the export and import of layouts, your own shortcuts, interface size and display scale, high contrast, keyboard navigation, zoom of the node canvas, and recovery from damaged layout, settings and shortcut files | implemented | automated 4/4 | 4 | M-UI-1 open; M-UI-2 open; M-UI-3 open; M-UI-4 open; M-UI-5 open; M-UI-6 open; M-UI-7 open; M-UI-8 open; M-UI-9 open; M-UI-10 open; M-UI-11 open |
| 3D mapping onto real surfaces: a virtual projector and viewer, surfaces, perspective, occlusion, calibration points and the manual calibration | implemented | automated 3/3 | 3 | M-SPATIAL-1 open; M-SPATIAL-2 open; M-SPATIAL-3 open; M-SPATIAL-4 open |
| Colour management: the colour model, the .cube LUT stage, HDR to SDR for videos, the display of an output, and what depth the real window, master and outputs keep | implemented | automated 3/3 | 3 | M-COLOR-1 open; M-COLOR-2 open; M-COLOR-3 open; M-COLOR-4 open |
| Optical flow: motion estimation, frame interpolation, motion blur and the vector view for video clips, and what they do on real footage, real graphics cards and next to other programs | implemented | automated 2/2 | 2 | M-FLOW-1 open; M-FLOW-2 open; M-FLOW-3 open; M-FLOW-4 open |
| Network stream inputs (RTSP, SRT, UDP) and the NDI runtime: real cameras and servers, real networks, Windows, SDI capture cards, the real NDI runtime and VRChat on the same PC | implemented | automated 3/4 | 4 | M-STREAM-1 open; M-STREAM-2 open; M-STREAM-3 open; M-STREAM-4 open; M-STREAM-5 open |
| Person detection (mask and 2D tracking from a camera, NDI or stream picture, no model): real cameras, real stage light, a real graphics card, Windows and VRChat on the same PC | implemented | automated 2/2 | 2 | M-PERSON-1 open; M-PERSON-2 open; M-PERSON-3 open; M-PERSON-4 open |
| Hardware frame lock and genlock status: the swap group and genlock extension names a real driver lists, what a consumer card, a professional card and a card with a sync board show, the software frame lock next to it, Windows and Linux | implemented | automated 4/4 | 4 | M-GENLOCK-1 open; M-GENLOCK-2 open; M-GENLOCK-3 open; M-GENLOCK-4 open |
| Unreal Engine link: the plugin in a real editor and a packaged game, the connection state, the ping and pong round trip, the Python example client, Windows and Linux | implemented | automated 4/4 | 4 | M-UNREAL-1 open; M-UNREAL-2 open; M-UNREAL-3 open; M-UNREAL-4 open; M-UNREAL-5 open |
| ResoPort (Resolume import), translucent 3D meshes, output while the window is minimized or moved, the 15 transitions and the glass effects: real Resolume compositions, real Windows, real graphics cards | implemented | implemented | 5 | M-RP-1 open; M-RP-2 open; M-RP-3 open; M-WIN-1 open |
| Audio analysis 2: the optional beat tracker (tempo, phase, confidence, grid beats), the audio latency setting, the adjustable band split and the left, right or mixed channel choice | implemented | implemented | 2 | M-AUD-1 open |
| Transition designer: deck transitions of your own as GLSL or ISF files (outgoing and incoming picture, progress, controls, preview, fallback to the built-in transition) | implemented | implemented | 5 | M-TRN-1 open |
| GPU feedback lab: a memory budget for feedback buffers, buffer reuse, freeze and reset, the frame time as uDelta, and the new effects Time Echo, Motion Smear, Feedback Transform and Accumulate | implemented | implemented | 6 | M-FBK-1 open |
| GPU effect graph optimizer: the last layer is drawn straight into the output, the colour look needs no extra picture, and a transition draws its two decks without a copy, with the switch Skip passes that only copy | implemented | implemented | 6 | M-GRF-1 open |
| Music on 3D materials: the beat and eight spectrum bands as sources, Colour shift, Roughness, Texture shift X and Y, Inflate and Ripple as targets, four rows, scene version 2 only when needed, notes for unknown keys, and the switch Music moves 3D materials | implemented | implemented | 4 | M-MBD-1 open |
<!-- matrix:end -->

Each area in `matrix.json` lists its automatic tests (`A-...`) and its manual tests (`M-...`) with the steps, what to measure and a proposed pass criterion. The pass criteria are proposals: the maintainer decides the real limits.

## Recording a result

1. Run the manual test from `tests/hardware/matrix.json` on the real machine.
2. Add one entry to `tests/hardware/results.json`:

```json
{
  "area": "displays",
  "test": "M-DISP-1",
  "platform": "windows",
  "gpu": "nvidia",
  "state": "hardware",
  "outcome": "passed",
  "date": "2026-12-01",
  "build": "0.50.0",
  "environment": "Windows 11, GeForce RTX 3060, driver 560.94, two 1080p displays",
  "conditions": {
    "system": "Windows 11 23H2, Ryzen 7 5800X, 32 GB",
    "driver": "NVIDIA 560.94",
    "resolution": "1920x1080",
    "clips": ["Plasma", "Tunnel", "Video: show-intro"],
    "effects": ["Glow", "RGB Split"],
    "durationSeconds": 1800
  },
  "tester": "name",
  "evidence": "link or path to the measurement files",
  "notes": "what was seen, including problems"
}
```

3. Run `python3 scripts/check_test_matrix.py --write` and commit the file, `docs/TESTING.md` and `docs/COMPATIBILITY.md`.

A hardware result needs the `conditions` block: system, driver, resolution, clips, effects and a duration above zero (since 0.50.0). The values in the example above are made up to show the shape; they are not a result. The templates `docs/templates/measurement-report.json` and `docs/templates/test-log.md` hold the numbers and the log that belong to such an entry. The checker refuses a block whose system or driver names a software renderer or a virtual machine.

A failed run is recorded with `"outcome": "failed"`. A failure stays visible in the table.

## Long runs: `--soak`

```
NyrexVJ --fresh --no-update-check --no-welcome --soak SECONDS[,FPS] --bench-out soak.json
```

The soak run renders in real time (60 frames per second unless FPS is given: 30, 60, 90, 120 or 144, the same steps as the frame limit in Settings) for 5 to 86400 seconds. It does what a show does to the program: it changes the look every 20 seconds, switches between two decks every 7 seconds (a half second transition) and cycles the render quality every 11 seconds. Every few seconds (a thirtieth of the run, 0.5 to 60 seconds) it records a sample.

What a sample holds: the render time of the frames of that window (average, 50th, 95th and 99th percentile, maximum), the frames over the budget of one frame period, the CPU use of the whole process in percent of one core (more than 100 means more than one core, and with software OpenGL the rasteriser threads count), the resident memory of the process, the program's own estimate of graphics memory (render targets, media, scenes, shader buffers; not what the driver really holds), the number of threads and the number of handles (open files and sockets on Linux, handles on Windows).

At the end it writes the totals (frames, frames over the budget of one period, frames longer than two periods, missed frames, which are gaps of more than one and a half periods between two frame starts, an estimate of the frames dropped in them, and the render time and frame interval statistics of the whole run) and a **growth verdict** for the memory, graphics estimate, thread and handle series. The first quarter of the run is ignored (loading, caches). The median of the first third of the rest is compared with the median of the last third. It counts as growth when the difference is larger than 64 MiB and 10 percent (memory), 16 MiB and 5 percent (graphics estimate), 2 (threads) or 16 (handles). The medians make a single spike harmless and a steady climb visible. The verdict is only given for runs of at least 60 seconds; a shorter run reports the numbers and "too short to judge". The exit code is 0 when no growth and no OpenGL error was found, 2 otherwise, 1 when the arguments are wrong. The report is rewritten after every sample, so a run that crashes leaves its last samples behind.

The limits are proposals for catching steady leaks, not performance targets. The run does not fail because frames were slow: whether 16.7 ms is achieved depends on the machine. Since 0.50.0 the judgement lives outside the program, in `scripts/release_gate.py` and `tests/hardware/gates.json` (see [RELEASE.md](RELEASE.md)), and frame times are judged only for a run on a real graphics card.

Since 0.50.0 the report names where it came from (`conditions`: system, build, graphics card, driver, resolution, clips, effects, duration) and which kind of renderer produced it (`software-gl`, `virtual-gpu` or `gpu-reported`).

`--soak-media FOLDER` replaces the look changes by a media churn: it imports the files of FOLDER, launches a clip on a layer every second second, clears a layer every ninth, and fails the run when a clip that should be playing has failed. Because the import copies the files into the data folder, it refuses to run unless `NYREXVJ_DATA_DIR` points at a scratch folder.

```
NYREXVJ_DATA_DIR=/tmp/soak-data NyrexVJ --fresh --no-update-check --no-welcome --soak 1800 --soak-media /path/to/clips --bench-out soak.json
python3 scripts/release_gate.py soak.json --profile hardware-release --exit-code 0
```

The continuous build runs a 6 s soak and a 20 s media soak through the `smoke` gate and writes the verdicts into the summary of the run, under a line that says the run used a software renderer and says nothing about a graphics card, a projector or a driver. The manual workflow `soak` runs a longer one by hand. Device loss, a driver reset, video memory almost full and a monitor that goes away have no automatic test; they are the manual tests `M-RQ-3`, `M-RQ-4` and `M-DISP-1` and the entries KI-2 and KI-3 of [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Measuring the machine: `scripts/measure_live.py`

The soak run sees the process from inside. `scripts/measure_live.py` sees it from outside, together with other programs and the graphics card:

```
python3 scripts/measure_live.py --seconds 1800 --interval 1 --process NyrexVJ --process VRChat --out parallel.json
```

It needs only Python 3. For each named process it records CPU use (percent of one core) and resident memory every interval, and the summary has minimum, average, 95th percentile and maximum plus a growth check of the memory. For the graphics card it uses what the system offers:

| Source | Used when | What it reads |
| --- | --- | --- |
| `nvidia-smi` | `--gpu nvidia`, or automatic when `nvidia-smi -L` works | load and memory use of each card |
| amdgpu files in `/sys/class/drm` | `--gpu amd`, or automatic on Linux with such files | load and memory use of each card |
| `typeperf` counters | `--gpu windows`, automatic on Windows | graphics memory and 3D load of the processes (`GPU Process Memory`, `GPU Engine`), any vendor |
| none | `--gpu none` | nothing |

Intel on Linux has no source here (`intel_gpu_top` usually needs elevated rights and prints a stream, which this script does not parse); on Windows the counters cover Intel.

## Faults tested without hardware

| Fault | How it is tested | Result |
| --- | --- | --- |
| Video file cut to half, a tenth or nothing before opening | `nvj_media_tests` (`testMediaOutage`) | Half: opens and plays. A tenth: opens, no pictures and **no error message** within a second. Nothing: refused with a message. No crash or hang in any case. |
| Video file cut to a third while it plays | same | Playback continues (pictures already read and the shorter file), no crash. |
| Second half of the file overwritten while it plays | same | Playback continues, no crash. |
| Video file deleted while it plays | same (Linux; on Windows the delete is refused while the file is open and the check is skipped) | Playback continues. |
| File gone, then back, opened again | same | Opens again and plays. |
| Program ended with `kill -9`, then started again | `scripts/test_restart.sh` | The second run starts, draws, writes its picture, keeps the log of the killed run and ends its own session cleanly. |
| A plugin that fails, hangs, leaks or sends wrong data | `nvj_plugin_tests` ([PLUGINS.md](PLUGINS.md)) | See that page. |
| Recording: disk almost full, write error, slow encoder, cancel, a copy of the file taken while it is written | `nvj_record_tests` and `NyrexVJ --record-selftest` ([RECORDING.md](RECORDING.md)) | A full disk ends the recording cleanly with a playable file, a write error ends it as failed with the partial file, a slow encoder drops and counts frames and never blocks the render loop, cancel deletes the file, a copy of an MP4 or MOV taken while it is written plays up to a moment before. No crash or hang. Not tested: a drive that is unplugged during a recording (manual test `M-RECORD-4`). |
| A camera or NDI sender that goes away | `nvj_live_tests`, `nvj_ndi_receive_tests`, `nvj_camera_tests` ([LIVE.md](LIVE.md)) | See that page. |
| Damaged or extreme 3D mapping data in a venue file (text where a number or a flag belongs, a projector that is a string, surfaces that are numbers, text or have too few corners, points with two coordinates or a text output position, 40 surfaces with 5000 divisions each, a position of 1e30, a field of view and a yaw of 1e9) | `nvj_spatial_tests` | Wrong entries are skipped or read as the defaults, the surfaces are cut to 16 and the divisions to 32, coordinates and angles are limited, nothing crashes, and a venue without the block is read as before. |
| A damaged, truncated, empty, 1D, oversized (more than 64 MB), badly numbered or unsafely named `.cube` file, text where a number belongs in `colorStage`, `colorLut`, `colorLutStrength`, `hdrToSdr`, `hdrWhiteNits`, `hdrPeakNits`, `hdrTone` or in the `display` of an output, a LUT that cannot be uploaded | `nvj_color_tests`, and `--color-selftest` for a broken table in the real engine | The file is refused with the reason and the line number, nothing is applied and the picture stays bit identical; wrong settings values are read as the defaults or brought into range; a venue without the new key is the same file as before. Not tested: a graphics card that is out of memory for the scratch target of the stage. |
| Hard cuts, quick fades, dissolves, a gap in the frame times, a frame time that goes backwards at a loop, backwards playback, a full graphics memory limit, a clip with alpha, frames skipped because the output is slow, and text, wrong types or out-of-range numbers in `flowMode`, `flowQuality`, `flowBudgetMb` or `flowShutterPercent` | `nvj_flow_tests`, and `--flow-selftest` for the same cases in the real player | A hard cut is never blended and equals normal playback; a gap, backwards playback and a full memory limit fall back to the plain picture with a reason; skipped frames do not stop the clip and cause no upload failure; wrong settings values are read as the defaults or brought into range. A quick fade to black (4 frames) is read as a cut and held ([KI-18](KNOWN_ISSUES.md)). Not tested: real footage, a real graphics card, Windows, VRChat running next to NyrexVJ. |
| A person-shaped area in a camera picture: an empty scene, the area at different sizes, a faint area, several people, two people who cross, an area that is partly or fully covered, light that changes (brighter, darker, warmer, cooler, a slow drift, a strobe), a camera that goes away and comes back, a scene that changes completely, pictures of the wrong size or too short, random pictures, a worker that is destroyed with pictures waiting, and text, wrong types or out-of-range numbers in `personMode`, `personInput`, `personQuality`, `personRate`, `personSensitivity`, `personSoftness` or `personStability` | `nvj_person_tests` (3,341 checks), and `--person-selftest` for the same module inside the real engine | An empty scene reports nobody; the mask overlaps the true area (IoU 0.93 to 0.99 at the three sizes); edges follow the softness; the mask flickers less with more stability; crossing people keep their numbers; light changes cause 0.2 percent false foreground or less (a strobe 2.2 percent); a lost camera empties the mask and the signals; a scene that changes completely is learned again (and a person in the picture at that moment becomes background, [KI-24](KNOWN_ISSUES.md)); wrong input is refused without a crash; wrong settings values are read as the defaults. In the real engine the mask lies upright and not mirrored, also for a 4:3 camera on a 16:9 output, the effect is a pass-through while the module is off, and the module costs 0.9 ms of analysis against a budget of 8.3 ms on this machine. Not tested: a real camera, a real graphics card, Windows, VRChat, a scaled camera layer, a projection in the camera picture ([KI-25](KNOWN_ISSUES.md), [KI-26](KNOWN_ISSUES.md), [KI-27](KNOWN_ISSUES.md)). |
| A damaged, empty, binary, wild (positions far off any screen), newer or out-of-range `layout_v8.ini`, `settings.json` or `shortcuts.json`, a damaged or oversized workspace file, `--reset-layout` and `--reset-interface` | `scripts/test_interface_recovery.py`, which starts the real program 15 times (24 checks), and `nvj_interface_tests` for the file checks | The program starts every time. A damaged file is renamed to `.bad` with its content intact, a newer shortcut file stays untouched and is copied before the first change, out-of-range values are brought into range, workspace files are never changed by the start. Not tested: a file that becomes unreadable while the program runs, a disk that is full while a workspace is saved. |
| A Resolume composition that is empty, cut at every third byte, changed in four random bytes (3,000 times), nested too deep, with too many nodes, with a document type declaration that has definitions, with a name that is a path, with more layers or decks than NyrexVJ has, with missing media, with a file type that cannot be played, or with an unknown blend name | `nvj_resoport_tests` (173 checks) and the ResoPort steps of `--interface-uitest` | The file is refused with a reason or read as far as it is sound; nothing is guessed, nothing is changed before Import, and the report counts what was skipped. |

The case "a tenth of the file" is a finding, not a pass: the clip stays silent without telling the user why. It was observed and recorded; the decoder was not changed in this step.

**Monitor hot plugging** (a display or projector unplugged during a show) has no automatic test. A virtual display cannot be unplugged, and the code asks GLFW for the monitor list when it needs it and registers no callback for changes ([OUTPUTS.md](OUTPUTS.md)). It is a manual test (`M-DISP-1`) and open.

## What was run and what was not

What was run is in `tests/hardware/results.json` and in the pull request of the version. In short: the unit tests, the selftests, the soak run and the restart test ran on Linux with software OpenGL (llvmpipe), 2 processor cores, 8 GB of memory. That machine renders the factory looks at about 5 frames per second at 1080p, so its frame times and its "over budget" counts say nothing about a real graphics card.

The interface checks (`--interface-uitest`, `nvj_theme_tests`) ran on a virtual screen with window sizes from 1024x600 to 2560x1440 and injected display scales; no real 125 to 200 percent display, 4K screen or keyboard-only show was involved (`M-UI-1` to `M-UI-5`). Since 0.59.0 the same holds for the new default layout, the sliders with the name on the left and the docking timeline (`M-UI-6` and `M-UI-7`). Since 0.60.0 it holds for the new arrangement after Resolume (grid on top, tabs on the right) and the default theme Arena (including the system font, which was only loaded from the Linux font files) and for the ResoPort dialog as well, and the look was compared with a Resolume screenshot by eye only (`M-UI-8`), and ResoPort was never run on a real Resolume show composition (`M-RP-1` to `M-RP-3`); the minimized and moving window on Windows ran only as compiled code (`M-WIN-1`).

The 3D mapping checks (`nvj_spatial_tests` and the spatial cases of `--output-selftest`) compare the real output window with a CPU model of the same projector and the same surfaces; they ran with software OpenGL only. The calibration was tested on points that a known pose produced, not on points that somebody measured; no real projector, room, pair of projectors or graphics card was involved (`M-SPATIAL-1` to `M-SPATIAL-4`).

The colour checks (`nvj_color_tests`, `--color-selftest` and the display cases of `--output-selftest`) compare the real shaders with a processor model of the same curves, matrices, tone mapping and LUT; the HDR test clips are the two short files in `tests/data` made with FFmpeg's encoder. They ran with software OpenGL only, so the measured depths (8/8/8/8 for the window and the master) are those of a virtual screen, and the frame times say nothing about a graphics card. No real HDR file, wide gamut screen, projector, capture card or 10 bit chain was involved (`M-COLOR-1` to `M-COLOR-4`).

The optical flow checks (`nvj_flow_tests` and `--flow-selftest`) compare the estimate and the synthesis with pictures whose motion is known, and the real player with four clips that were generated from a moving synthetic picture (`scripts/make_flow_test_clips.py`); one run was repeated with all cores of the machine busy as a stand-in for another program. They ran with software OpenGL only, so the frame times and analysis times say nothing about a graphics card, and no real footage, no real graphics card, no Windows run and no VRChat were involved (`M-FLOW-1` to `M-FLOW-4`).

The network stream checks (`nvj_stream_tests`, `nvj_ndi_v3_tests`, `nvj_picture_convert_tests` and `scripts/test_stream_input.py` with `--stream-selftest`) test the address rules, the lag and jitter measures and the settings without any network, and the receiver with real SRT, UDP and RTSP senders (`ffmpeg` and a small RTSP server written for the test) on the loopback address of one computer, with a stand-in NDI runtime for the NDI part. They ran with software OpenGL only, so the delay from decoding to upload is a number of this container and not of a graphics card, and no real camera, no real network with loss, no Windows run, no real NDI runtime, no SDI card and no VRChat were involved (`M-STREAM-1` to `M-STREAM-5`).

The person detection checks (`nvj_person_tests` and `--person-selftest`) use pictures that the tests draw themselves (a textured background with noise and a person-shaped area with known edges), so every result can be compared with the true mask. The selftest feeds such pictures into the real session of the engine through a feed switch instead of a camera and reads the output of the real `Person Key` effect; as a control, the vertical flip of the mask in the shader was tried on purpose and made four checks fail. They ran with software OpenGL on a machine with 2 processor cores and no graphics card, so the times say nothing about a graphics card, and no real camera, no Windows run and no VRChat were involved (`M-PERSON-1` to `M-PERSON-4`).

Not run, because the hardware was not available: every manual test in the matrix. In particular a real NVIDIA, AMD or Intel graphics card on Windows or Linux, real hardware video decoders, Spout with a real receiver, a real NDI network, cameras and capture cards, several displays and projectors (including unplugging one), MIDI controllers, Art-Net nodes, DJ players, VRChat running next to NyrexVJ, long runs of hours, and the Windows side of `measure_live.py` and of `processThreadCount` and `processHandleCount` (they compile in the Windows build; they were not run on Windows).

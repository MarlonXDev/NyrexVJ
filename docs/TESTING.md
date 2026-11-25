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
  "build": "0.34.0",
  "environment": "Windows 11, GeForce RTX 3060, driver 560.94, two 1080p displays",
  "tester": "name",
  "evidence": "link or path to the measurement files",
  "notes": "what was seen, including problems"
}
```

3. Run `python3 scripts/check_test_matrix.py --write` and commit the file and `docs/TESTING.md`.

A failed run is recorded with `"outcome": "failed"`. A failure stays visible in the table.

## Long runs: `--soak`

```
NyrexVJ --fresh --no-update-check --no-welcome --soak SECONDS[,FPS] --bench-out soak.json
```

The soak run renders in real time (60 frames per second unless FPS is given: 30, 60, 90, 120 or 144, the same steps as the frame limit in Settings) for 5 to 86400 seconds. It does what a show does to the program: it changes the look every 20 seconds, switches between two decks every 7 seconds (a half second transition) and cycles the render quality every 11 seconds. Every few seconds (a thirtieth of the run, 0.5 to 60 seconds) it records a sample.

What a sample holds: the render time of the frames of that window (average, 50th, 95th and 99th percentile, maximum), the frames over the budget of one frame period, the CPU use of the whole process in percent of one core (more than 100 means more than one core, and with software OpenGL the rasteriser threads count), the resident memory of the process, the program's own estimate of graphics memory (render targets, media, scenes, shader buffers; not what the driver really holds), the number of threads and the number of handles (open files and sockets on Linux, handles on Windows).

At the end it writes the totals (frames, frames over the budget of one period, frames longer than two periods, missed frames, which are gaps of more than one and a half periods between two frame starts, an estimate of the frames dropped in them, and the render time and frame interval statistics of the whole run) and a **growth verdict** for the memory, graphics estimate, thread and handle series. The first quarter of the run is ignored (loading, caches). The median of the first third of the rest is compared with the median of the last third. It counts as growth when the difference is larger than 64 MiB and 10 percent (memory), 16 MiB and 5 percent (graphics estimate), 2 (threads) or 16 (handles). The medians make a single spike harmless and a steady climb visible. The verdict is only given for runs of at least 60 seconds; a shorter run reports the numbers and "too short to judge". The exit code is 0 when no growth and no OpenGL error was found, 2 otherwise, 1 when the arguments are wrong. The report is rewritten after every sample, so a run that crashes leaves its last samples behind.

The limits are proposals for catching steady leaks, not performance targets. The run does not fail because frames were slow: whether 16.7 ms is achieved depends on the machine, and a gate on that is a separate task (release checks), not part of this one.

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

The case "a tenth of the file" is a finding, not a pass: the clip stays silent without telling the user why. It was observed and recorded; the decoder was not changed in this step.

**Monitor hot plugging** (a display or projector unplugged during a show) has no automatic test. A virtual display cannot be unplugged, and the code asks GLFW for the monitor list when it needs it and registers no callback for changes ([OUTPUTS.md](OUTPUTS.md)). It is a manual test (`M-DISP-1`) and open.

## What was run and what was not

What was run is in `tests/hardware/results.json` and in the pull request of the version. In short: the unit tests, the selftests, the soak run and the restart test ran on Linux with software OpenGL (llvmpipe), 2 processor cores, 8 GB of memory. That machine renders the factory looks at about 5 frames per second at 1080p, so its frame times and its "over budget" counts say nothing about a real graphics card.

Not run, because the hardware was not available: every manual test in the matrix. In particular a real NVIDIA, AMD or Intel graphics card on Windows or Linux, real hardware video decoders, Spout with a real receiver, a real NDI network, cameras and capture cards, several displays and projectors (including unplugging one), MIDI controllers, Art-Net nodes, DJ players, VRChat running next to NyrexVJ, long runs of hours, and the Windows side of `measure_live.py` and of `processThreadCount` and `processHandleCount` (they compile in the Windows build; they were not run on Windows).

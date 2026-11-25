# Releasing NyrexVJ

This page is the release checklist: what has to be true before a version is called a release, how that is checked, what a machine can check and what only a person on real hardware can. The limits below are **proposals**. The maintainer decides the real ones, and `tests/hardware/gates.json` is the one place they are written down.

Nothing on this page has been measured on a real graphics card yet. Where a number is named, it is a limit to be checked, not a result.

## What counts as evidence

Every report a run writes says which kind of machine produced it. The program reads the renderer string of OpenGL and puts one of three classes into the report (`conditions.graphics.validationClass`):

| Class | Meaning | Counts as |
| --- | --- | --- |
| `software-gl` | The vendor or renderer string contains llvmpipe, softpipe, swrast, lavapipe, software rasterizer, basic render, swiftshader or gdi generic | Automated evidence. Never hardware. |
| `virtual-gpu` | The string contains virgl, vmware, svga3d, virtualbox, vboxvideo, parallels, qxl or microsoft hyper-v | Automated evidence. Never hardware. |
| `gpu-reported` | Anything else. The program saw a renderer that is not a known software or virtual one | Possible hardware evidence. A person still has to say what machine it was, and that goes into `tests/hardware/results.json`. |

The class is a guess from a string. A software or virtual renderer whose name is not in those lists would be called `gpu-reported`; that is why a hardware result also needs a named tester, the driver and the machine, and why `scripts/check_test_matrix.py` refuses a hardware result whose system or driver names a software renderer or a virtual machine.

The continuous build only ever produces `software-gl` runs. Its summary says so at the top of every run.

## The gates

`scripts/release_gate.py` reads the report of a run (`--bench-out file.json` of `--soak`) and judges it against `tests/hardware/gates.json`. It has three profiles:

| Profile | Where it runs | Minimum length | Judges |
| --- | --- | --- | --- |
| `smoke` | Every change in the continuous build, software OpenGL | 5 s | Crash freedom, a finished report, no OpenGL error, no failed clip, a frame that is not stuck. No growth, no frame time limits. |
| `soak` | The manual workflow `soak`, any renderer | 600 s | The same, plus growth of memory, graphics estimate, threads and handles. A stall check on frame times, but no performance limits. |
| `hardware-release` | A real graphics card, by hand | 1800 s | The same, plus the frame time limits below. Refused when the run is not `gpu-reported`. |

### What each gate means

| Gate | Meaning | Limit |
| --- | --- | --- |
| G-FORMAT | The report names the system, build, graphics driver, resolution, clips, effects and duration | all present, clips not empty |
| G-EXIT | The program ended with exit code 0. A crash gives another code. | 0 |
| G-FINISHED | The report is the final one. A run that was cut off or crashed leaves `finished` false. | true |
| G-CLASS | The class of the renderer. `hardware-release` needs `gpu-reported`. | profile |
| G-DURATION | The run was long enough for the profile | see table above |
| G-FRAMES | At least one frame was drawn | 1 |
| G-GLERROR | No OpenGL error was seen | 0 |
| G-FAULTS | No clip that was supposed to play failed (media runs) | 0 |
| G-GROWTH | Memory of the process, the estimate of graphics memory, threads and handles did not grow above their limits between the first and the last third of the run, after the first quarter | proposals below |
| G-STALL, G-SPREAD, G-PROGRESS | Software and virtual runs: the longest gap between two frames, P99 against the median, frames per second | 10 s, 20 times, 0.5 fps. These only catch a hang. |
| G-FRAMETIME | Real graphics card: the percentiles of the work time of a frame against the frame period | see below |

Growth limits (the same ones the soak run uses): process memory 64 MiB or 10 percent of the starting value, whichever is larger; graphics memory estimate 16 MiB or 5 percent; threads 2; handles 16. They are judged only for runs of at least 60 seconds and only on the median of the first and the last third of the samples after the first 25 percent of the run. The estimate counts what the program allocates itself; the driver can use more. `scripts/measure_live.py` is the outside check, and `--live` lets the gate judge its report as well.

Frame time limits for `hardware-release` (proposals, with the frame period of the target frame rate, 16.7 ms at 60 fps):

| Measure | Limit |
| --- | --- |
| P95 of the work time | at most one period |
| P99 of the work time | at most 1.5 periods |
| Longest work time | at most 100 ms |
| Frames over one period | at most 2 percent |
| Missed frames (a gap of more than 1.5 periods between two frame starts) | at most 0.5 percent of the frames |
| Achieved frame rate | at least 97 percent of the target |

A value that is missing counts as a failure, not as a pass.

### Running a gate

```
NyrexVJ --fresh --no-update-check --no-welcome --soak 1800 --soak-media FOLDER --bench-out soak.json
python3 scripts/release_gate.py soak.json --profile hardware-release --exit-code 0
```

Use the real exit code of the program for `--exit-code`. `--live device.json` adds the report of `scripts/measure_live.py` (give it `--system`, `--driver`, `--build`, `--resolution`, `--clips` and `--effects` so it names its conditions too). `--json file` writes the verdicts, `--summary file` appends a Markdown table.

`--soak-media FOLDER` imports the files in FOLDER (images, videos and 3D models, at most what the media limits allow), launches a clip on a layer every second second and clears a layer every ninth, and counts a clip that fails while it is supposed to play. It replaces the look changes of an ordinary soak. At least two usable files are needed.

## What the continuous build proves and what it does not

| Job | Runs | Proves | Does not prove |
| --- | --- | --- | --- |
| windows | Build, `ctest` (unit tests, including the file format tests), the consistency checks | The program compiles and the unit tests pass on Windows | Nothing about the program running on Windows: no window, no soak, no OpenGL |
| linux | Build, `ctest`, the self tests, a 6 s soak and a 20 s media soak through the `smoke` gate, restart test | The program runs on Linux with Mesa llvmpipe on a virtual display, starts, changes clips, ends without a crash or an OpenGL error | A real graphics card, a driver, a projector, audio devices, MIDI, NDI or Spout, VRChat |
| linux scene variants | The 3D loader and renderer with and without Meshopt and Draco | The loader builds in both configurations | Real graphics cards |
| soak (manual only) | `workflow_dispatch` with a length in seconds, up to 7200 | A long software run with changing media: crash freedom and growth | The same as above, and no video (the workflow builds without FFmpeg) |

The `windows` job is the only evidence for Windows. Anything that depends on Windows at run time (Spout, the Windows system information in the report, the driver, hot plugging) is manual.

## The checklist

Work through this from top to bottom. Record the answers in the release issue or the pull request, with the file or the link that shows it.

### 1. The change

1. The work has an issue and its own branch, and a pull request that says what was tested and what was not.
2. `python3 scripts/check_release.py` passes: the version in `CMakeLists.txt` is the newest entry of `CHANGELOG.md` and `CHANGELOG.de.md`, both READMEs name it, every decision file has its row in `docs/DECISIONS.md`, every command line option of `src/main.cpp` is named in a document, every script a workflow names exists, the release documents exist.
3. `python3 scripts/check_i18n.py`, `python3 scripts/check_test_matrix.py`, `python3 scripts/build_manual_site.py --check`, `python3 scripts/test_release_gate.py`, `python3 scripts/test_check_release.py` and `python3 scripts/test_measure_live.py` pass.

### 2. The continuous build

4. The `windows`, `linux` and both `linux scene variant` jobs are green on the commit that will be tagged. (`publish` is skipped on a pull request.)
5. The summary of the `linux` job shows the two smoke gates passing and says "Software OpenGL only". The artifact `soak-software-gl` holds both reports.
6. For a release, run the workflow `soak` once with 1800 seconds or more and keep the artifact. It judges growth on software OpenGL; it does not replace step 8.

### 3. File formats

7. Presets, settings, venues, cues, timelines and controller profiles still load: `nvj_compat_tests` passes. If a format changed on purpose, write the fixtures of the new version (`nvj_compat_tests --write-fixtures tests/data/compat`, then change `kNewest` in `tests/test_compat.cpp` and keep the older files), name the change under "Changed" in the changelog and say in the pull request what an older program does with the new file. See [COMPATIBILITY.md](COMPATIBILITY.md).

### 4. Real hardware

8. On a PC with a real graphics card, run test `M-RQ-1` (the soak with changing media and the `hardware-release` gate) and record it in `tests/hardware/results.json` with the `conditions` block (system, driver, resolution, clips, effects, duration). Use `docs/templates/measurement-report.json` for the numbers and `docs/templates/test-log.md` for the log.
9. Repeat the compatibility pass `M-RQ-2` for every combination the release notes call supported (graphics card vendor and operating system). A combination without a recorded pass is "untested" in [COMPATIBILITY.md](COMPATIBILITY.md); do not call it supported.
10. Run the manual tests of the areas the release changed. The table in [TESTING.md](TESTING.md) lists which are open.
11. For a show release: run `M-VRC-1` (next to VRChat), `M-DISP-1` (displays and projectors) and `M-SOAK-1` (three hours). They are the tests that decide whether the program survives an evening.

### 5. Known problems

12. Walk through [KNOWN_ISSUES.md](KNOWN_ISSUES.md). For each entry, try the reproduction steps if the release touched that area, update the entry, and set `Checked at version:` to the version being released. `python3 scripts/check_release.py --release` fails until that line matches. Add every problem found on the way with its steps.

### 6. Tag and publish

13. The maintainer pushes the tag `vX.Y.Z`. The build refuses a tag that does not match the version in `CMakeLists.txt` and runs `check_release.py --release`.
14. After `publish` ran, download the files from the release page and check them against `SHA256SUMS.txt`. Start the downloaded program on a clean machine with `--fresh` once.

### 7. Sign-off

Copy this table into the release issue. Every row needs a name or a link; "n/a" needs a reason.

| Item | Done by | Evidence |
| --- | --- | --- |
| Checks of step 1 | | |
| Jobs of step 2 green | | |
| Fixtures of step 3 | | |
| `hardware-release` gate on a real card (graphics card and driver named) | | |
| Compatibility pass per supported combination | | |
| Manual tests of the changed areas | | |
| Known issues walked through | | |
| Tag, release files and checksums | | |

## Rollback

There is no automatic rollback. The updater swaps the program file and removes its `.old` leftovers on the next start, so the program does not keep the previous version for you. To go back, install the previous release again from the releases page (the files and `SHA256SUMS.txt` stay there).

Files in the user data folder are not replaced by an update. Presets, settings, venues, cues, timelines and controller profiles written by a newer version load in an older one only as far as that older version knows them, see [COMPATIBILITY.md](COMPATIBILITY.md). Keep a copy of the data folder before a release that changed a format, and say so in the release notes.

## What this page does not do

It does not decide that a release is good. A green gate says that the limits held in one run on one machine. A person signs the release.

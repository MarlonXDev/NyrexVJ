# ADR 0027: Live stability and hardware tests: a soak run in the program, a test matrix as data, an outside measuring script, two fault tests

- **Status:** Accepted (requested by MarlonXDev, Prompt 10). Issue [#80](https://github.com/MarlonXDev/NyrexVJ/issues/80). The pass criteria in the matrix are **Proposed** and wait for the maintainer.
- **Date:** 2026-11-25
- **Owners:** Maintainers (pending assignment)
- **Related:** [TESTING.md](../TESTING.md), [PERFORMANCE.md](../PERFORMANCE.md), [LIVE.md](../LIVE.md), [OUTPUTS.md](../OUTPUTS.md), [MEDIA.md](../MEDIA.md), [ADR 0014](0014-live-sources.md), [ADR 0024](0024-video-pipeline-audit-and-measurement.md), [ADR 0026](0026-plugin-system-2.md)

## Context
The request: a reproducible test matrix (Windows and Linux; NVIDIA, AMD, Intel; Spout, NDI, cameras, several displays and projectors, MIDI, Art-Net, DJ sync, FFmpeg), tests for long shows, media outages, hot plugging, decoder fallbacks, plugin errors, video memory leaks, dropped frames and restarts, measurements of CPU, GPU, video memory, render time and latency also next to VRChat, automation of everything that works without hardware, and a clear mark on every manual test that is still open. Results must be documented instead of untested functions being called finished. Release checks (Prompt 18) and telemetry (Prompt 23) are separate tasks, and no second performance infrastructure may be built.

## Findings (what existed before)
- **Benchmarks and selftests.** `--bench-comp`, `--bench-all`, `--bench-memory`, `--bench-switch`, `--bench-live`, `--bench-video`, `--live-selftest`, `--output-selftest`, `--video-selftest`, `--scene-selftest`, `--shader-selftest`, `--plugin-selftest`, `--crash-test` (`src/main.cpp`, `src/app/bench.cpp`). All of them run seconds to a few minutes. None of them changes looks, decks and quality over time for a long run, and none looks at threads or handles.
- **Measurement code.** `FrameStats` (percentiles), `Engine::gpuMemory()` (the program's own estimate), `platform::processMemoryBytes()` and `processCpuSeconds()`. Nothing sampled them over time, and nothing could see another process or the graphics card.
- **Unit tests.** Live state machine and reconnecting, fake NDI, camera test sources, MIDI clock, DJ link simulator, frame lock, outputs, plugins, recovery data. Documented as not run on hardware in LIVE.md, OUTPUTS.md, MEDIA.md, DMX.md, SYNC.md, UNREAL.md and FFMPEG.md, each in its own list.
- **Gaps.** No test for a video that is cut, overwritten or deleted while it plays. No end to end test that ends the program hard and starts it again (the recovery files had unit tests only). No overview that says which test covers which feature on which platform, and no place where a run on real hardware is recorded. Monitor hot plugging has no code that reacts to it (`src/render/output_window.cpp` asks GLFW for the monitor list when it needs it and registers no callback).

## Decision
1. **The soak run lives in the program** (`--soak SECONDS[,FPS]`, `src/app/soak.cpp`), next to the other benchmarks, because the events it needs (change the look, switch the deck, change the quality) are engine calls and the render time is best measured at the call. It follows `--bench-memory`: same step, same engine calls, JSON through `--bench-out`. It is not an always-on feature: it exists only when the option is given, writes one local file and sends nothing anywhere (telemetry is another task).
2. **Growth is judged by medians, not by a fit.** The first quarter is ignored, the median of the first third of the rest is compared with the median of the last third, and the limit is an absolute and a relative value per series (`src/core/growth_check.*`, with tests that include spikes, steps, warm-up and noise). A run shorter than 60 seconds reports numbers only. The limits are proposals for catching steady leaks. The run does not fail on slow frames: that would be a release gate, and the numbers of a software renderer say nothing about a real card.
3. **Outside measurement is a script** (`scripts/measure_live.py`, Python 3 standard library only) because the outside view needs other processes and the graphics card, which the program cannot see. It reads what the system already offers (`/proc`, PowerShell `Get-Process`, `nvidia-smi`, amdgpu files, Windows `typeperf` counters) and installs nothing and switches nothing on. The parsers are separate functions with sample texts as tests; the Linux path is also tested live against the test's own process.
4. **The test matrix is data** (`tests/hardware/matrix.json`, `tests/hardware/results.json`) with a checker (`scripts/check_test_matrix.py`) and a table in TESTING.md that the checker generates and compares. Four states: n/a, implemented, automated, hardware. A result in state hardware needs date, build, tester and evidence, and is refused when its environment names a software renderer or a virtual machine. The matrix names unit tests, options and scripts that exist; the checker finds a name that does not.
5. **Two fault tests for the two real gaps:** `testMediaOutage` in `nvj_media_tests` and `scripts/test_restart.sh`. Other fault cases (plugins, lost cameras and NDI senders, decoder fallbacks) already have tests and are only listed in the matrix.
6. **Monitor hot plugging is not automated and not changed.** It is a manual test. Registering a monitor callback would be a behaviour change that belongs to a maintainer decision about what an output window should do when its display disappears.
7. **Two small platform functions** (`platform::processThreadCount()`, `processHandleCount()`) for the soak run. Linux: `/proc/self/task` and `/proc/self/fd`. Windows: Toolhelp snapshot of the threads of the own process and `GetProcessHandleCount`. Other systems return 0.

## Alternatives considered
- **Only an external script.** It cannot change looks or decks and sees only the process as a whole.
- **ETW, PresentMon, GPU-Z or vendor SDKs.** They would measure more, but they are new dependencies and need Windows and real hardware to be tried at all. The matrix names the manual steps where such a tool is the right instrument.
- **psutil or other packages for the script.** An install the user did not ask for; the standard library is enough for what is read.
- **OpenGL memory extensions for the graphics card's own memory figure** (`GL_NVX_gpu_memory_info`, `GL_ATI_meminfo`). They are vendor specific and not available on every driver. Not built; `measure_live.py` reads the card from outside instead. The soak run's graphics figure is the program's own estimate and says so.
- **A frame time gate in the continuous build.** Not done: it is Prompt 18's job, and on hosted runners with a software renderer the numbers are not a measure of anything.
- **A fixed pass limit per manual test.** The limits are marked Proposed, because they are decisions about what the show needs.

## Consequences
- A long show can be rehearsed on any machine with `--soak`, and a growth in memory, graphics estimate, threads or handles is found without hardware. The same run on real hardware, together with `measure_live.py`, is the manual test `M-SOAK-1`.
- Nothing about real hardware is claimed. The table in TESTING.md shows "implemented" where nothing is recorded, and every manual test shows "open" until a result is added.
- `soak.cpp` and the two platform functions add a little code to the program; nothing runs unless the option is given.
- The checker has to be run when a test is renamed (CI runs it).

## Tests
- `nvj_soak_tests`: growth check (median, too few samples, flat and noisy series over 200 seeds, steady leak over 100 seeds, warm-up, late step, single spike, shrinking, limits, bad warm-up values), the `--soak` argument (valid forms, 27 invalid ones), sample interval and the event schedule.
- `nvj_media_tests`: `testMediaOutage`.
- `scripts/test_measure_live.py`: 13 tests, parsers with sample texts and a live check on Linux.
- `scripts/check_test_matrix.py`: checks matrix, results and table.
- `scripts/test_restart.sh`: kill and restart. Checked against a run where the first process was still alive, which leaves the session file in place and would make the script fail.
- `--soak 12` and `--soak 300` on Linux with software OpenGL; numbers are in [PERFORMANCE.md](../PERFORMANCE.md).

## Not tested
Everything that needs hardware: see the matrix. The Windows code of `processThreadCount`, `processHandleCount` and of the PowerShell and `typeperf` parts of `measure_live.py` compiles or parses sample text in the continuous build but was never run on Windows. The `typeperf` output format was written from documentation and knowledge, not from a real machine. `nvidia-smi` and the amdgpu files were not read on real cards.

## Validation / open questions
- The maintainer decides the pass criteria of the manual tests and the limits of the growth check.
- A hot plug reaction of the output windows (callback, what to do with the window) needs a decision.
- A video that opens but delivers no pictures and no error (a file cut to about a tenth) keeps the clip silent without a message. Whether the player should time out and report it is open.

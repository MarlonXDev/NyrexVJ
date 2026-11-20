# ADR 0012: Plan towards a full open source VJ platform (live sources, media control, outputs, sync, nodes, plugins, Linux)

- **Status:** Accepted (2026-11-20, requested by MarlonXDev as "PROMPT_3"). Each milestone is its own issue, branch and pull request. The plan is the approved order, not a promise of dates.
- **Date:** 2026-11-20
- **Owners:** Maintainers (pending assignment)
- **Related:** [ROADMAP.md](../ROADMAP.md), [ARCHITECTURE.md](../ARCHITECTURE.md), [PERFORMANCE.md](../PERFORMANCE.md), [ADR 0007](0007-spout-receiver.md), [ADR 0010](0010-video-decode-pipeline.md), [ADR 0011](0011-render-pipeline-optimisation.md)

## Context
The request is to grow NyrexVJ step by step towards an open source alternative to Resolume Avenue and Arena in seven areas, without a rewrite, with low CPU, GPU and video memory use (also next to VRChat), no needless dependencies, respected licences, and tests and documentation for every extension.

What the code already has (checked on `main` at `5edb8d2`, version 0.19.0):

| Area | Already there | Missing |
| --- | --- | --- |
| Live sources | NDI sender (runtime loaded with `dlopen`/`LoadLibrary`, own struct declarations, tested against a fake library), four Spout input slots with lifecycle and placeholder (Windows only), FFmpeg loaded at run time | NDI input and source discovery, webcams and capture cards, reconnect logic outside Spout |
| Media control | Time driven decoder (`frameAt(seconds)`), background thread with queue and seek window, endless loop, HAP direct upload, hardware decode, frame rate limit | Reverse playback, scrubbing UI, exact seek on demand, in/out points, loop modes, speed, beat sync, pre-rolled switching |
| Outputs | Up to four outputs, one rectangular slice each, corner pin, warp grid 2 to 9 points, edge blend, brightness, flips, test pattern, NDI of the main picture | Several slices and masks per output, colour correction per output, saved venue setups, smoother warp, black level in blends |
| Show sync | Audio beat and BPM, tap tempo, resync, nudge, half/double, beat grid, OSC, MIDI input (Windows) | SMPTE (LTC, MTC), Pro DJ Link, StageLinq, one beat clock that merges sources, output frame lock |
| Nodes | GLSL effects and clips from files (NyrexVJ and Shadertoy style), modulation sources, macros, LFOs | A graph document, a graph to GLSL compiler, an editor |
| Plugins | `.nyxclip` packs, shader files, Unreal plugin (separate), OSC in and out | A native plugin interface, versioning, isolation |
| Platforms | Windows build with CI, packaging, updater. Linux builds and passes ctest (the author develops on it) | Linux CI, Linux audio capture, Linux MIDI, Linux updater and releases, dependency list |

Constraints from [AGENTS.md](../../AGENTS.md) and [CONTRIBUTING.md](../../CONTRIBUTING.md): C++20, OpenGL 3.3 core, Dear ImGui, CMake; platform code isolated behind a header with a fallback; nothing blocks the render thread; every user visible text goes through `tr()`; both changelogs and docs are updated; no new dependency without a look at licence, size and upkeep.

## Decision
Seven milestones, each one issue, one branch, one pull request, one minor version, in this order. The order puts the safety net first, then the work that every show needs, then the larger features.

1. **v0.20.0 Platforms and releases** (issue "Platforms")
   - Linux CI job (build, ctest, shader check, a short headless benchmark as smoke test), kept next to the Windows job. The release publishing moves to its own job that needs both builds and keeps the existing asset names (`NyrexVJ.exe`, `SHA256SUMS.txt`, `NyrexVJ-windows-x64.zip`) so the updater of every installed copy keeps working.
   - Linux portable archive (`NyrexVJ-linux-x64.tar.gz`) with the executable, licences, `DEPENDENCIES.md` and checksums; `libstdc++` and `libgcc` linked statically so the file runs on older distributions; the remaining run time libraries are listed from `ldd` in the archive and in the docs.
   - Linux audio capture (PulseAudio simple API, also served by PipeWire, loaded with `dlopen`, no build dependency) and Linux MIDI input (ALSA sequencer, loaded with `dlopen`). Both stay behind the existing `AudioInput` and `MidiInput` interfaces; without the library the app says so and continues.
   - Linux updater on top of the existing logic (replace the executable file, restart). The platform functions stay in `src/platform`.
   - `docs/DEPENDENCIES.md` and `THIRD_PARTY_LICENSES.md`: every library, version, licence, whether it is bundled or loaded at run time.
2. **v0.21.0 Media control** (issue "Media control")
   - A pure core type for the play head (`PlayheadState`, advance, seek, loop mode, speed, in and out points) with unit tests, so no frame logic lives only in the decoder thread.
   - Reverse playback: HAP and other intra frame codecs seek per frame; long GOP codecs decode a group of pictures forward into a bounded ring (memory capped, shown in the video monitor) and serve it backwards. The cap and the fallback (stepped playback with a note) are documented.
   - Exact seek on demand (seek to the key frame before the target, decode and drop up to it), scrubbing from the clip panel, frame step, timecode display.
   - In and out points, loop modes (loop, ping pong, once and hold, once and stop), speed from -4 to +4, beat sync (restart on beat, length in beats), sync modes (free, beat, timecode locked once milestone 4 exists).
   - Switching without drops: the clip in the cue preview and the clips of the next column are pre-rolled (decoder open, queue filled, first frame uploaded) and released by the existing idle rule. A benchmark counts frames without a ready picture across a switch.
3. **v0.22.0 Live sources** (issue "Live sources")
   - Same slot model as Spout: fixed clips `NDI Input 1` to `4` and `Camera 1` to `4`, a state machine (searching, connecting, live, signal lost, reconnecting), the same placeholder, release after three idle seconds, one GPU upload path.
   - NDI input: the runtime is loaded like the sender; a discovery thread lists sources once a second; empty name means the first source that is not our own output; the receiver asks for the format the sender produces natively (UYVY is uploaded as half width RG and converted in the shader, so a frame costs half of BGRA); only the newest frame is kept so the latency stays one frame; no frame for three seconds starts a reconnect with growing waits.
   - Cameras and capture cards through the FFmpeg device layer (`avdevice`, `v4l2` on Linux, `dshow` on Windows), loaded at run time like the other FFmpeg libraries, so there is no new dependency and MJPEG decoding and format conversion are done by code that exists. Device lists come from the same layer. Small buffers and no frame queue for low latency; the camera format is chosen by size, rate and pixel format with uncompressed or MJPEG preferred over anything that needs a heavy decode.
   - Tests: a fake NDI library for the receiver (like `tests/fake_ndi.cpp` for the sender), a virtual `lavfi` source for the camera path, state machine tests for the reconnect rules. Real NDI senders, webcams and capture cards need hardware; their first run belongs to the maintainer.
4. **v0.23.0 Advanced output** (issue "Advanced output")
   - Several slices per output (input rectangle to output quad or warp mesh) and polygon masks with feather and invert, rendered once into a small texture and multiplied in the output shader; the old single slice is the default and old files load unchanged.
   - Colour correction per output (gain, lift, gamma, contrast, saturation, per channel) in the same output shader, no extra pass.
   - Venue setups as named JSON files (all outputs, slices, masks, warps, blends, colour; monitor name and size as hints, index as fallback), save, load, duplicate, delete, import, export.
   - Warp and blend: smooth interpolation (Catmull-Rom) for the warp grid and up to 17 by 17 points, black level compensation in the blend zones.
5. **v0.24.0 Show sync** (issue "Show sync")
   - One beat clock (`BeatClock`, core, no OpenGL) that merges tap, audio, OSC, MIDI clock and the sources below by priority, keeps the phase continuous (slews instead of jumping), has a latency offset and a lock indicator. It replaces the scattered beat state in small steps, with tests.
   - SMPTE timecode: LTC decoded from an audio input (biphase mark decoder, own code from the public specification, tested with a generated signal), MTC from MIDI quarter frames. The timecode drives the beat clock position, the timecode locked playback of milestone 2, a modulation source, and a simple cue list that triggers columns and decks.
   - Pro DJ Link: a passive listener for the beat packets that players broadcast (UDP 50001) giving BPM and beat position; the player to follow is chosen in the interface. Written from the publicly documented packet layout, tested with a packet simulator. Not tested against real players.
   - Denon StageLinq: evaluation only in this milestone (feasibility, protocol size, licence of existing implementations, risk). It is built only if the evaluation says a small, safe subset is possible; the result is written down either way.
   - Several outputs: every output shows the same composition frame; a per output frame delay for projectors with different latency; swap skew measured and shown; sync bars in the test pattern.
6. **v0.25.0 Node effect editor** (issue "Node editor")
   - A versioned graph document in core (nodes, ports, links, parameters, limits) with validation, no OpenGL, unit tested.
   - A compiler from graph to one GLSL fragment shader per effect (all nodes in one pass, no intermediate pictures unless a node needs a texture of its own), cached by graph hash, with the compile error shown on the node that caused it.
   - A catalogue of small nodes: inputs (UV, time, beat, audio bands, picture in, parameter), maths, colour, distortion, noise, a bounded blur. LFO and macro nodes feed parameters through the existing modulation sources; parameters become the effect's controls.
   - Files (`.nyxfx`, JSON, versioned, with a checksum) in a `nodes` folder; they appear in the library as effects or clips and can be copied to another computer.
   - The editor is drawn with `ImDrawList` (pan, zoom, drag, wires) so there is no new dependency.
7. **v0.26.0 Plugin system and API** (issue "Plugins")
   - A C header `nyrexvj_plugin.h` with a stable ABI: API version (major, minor), size-tagged structs, a host function table (log, audio features, beat, parameters), and plugin kinds that do not touch the OpenGL context: modulation sources (floats) and frame sources (RGBA frames that appear as clips). Effect plugins stay declarative (manifest plus shader files, or a node file), which cannot crash the program.
   - Compatibility check before any plugin code runs: the major version must match, the minor version must be at most the host's; otherwise the plugin is listed as incompatible and not loaded.
   - Isolation: native code runs on its own worker thread with a time budget per call; a plugin that exceeds it or reports an error is disabled for the session; before each call the plugin id is written to a marker so the crash report names it and the next start disables it ("safe mode" list). Frame data is copied and size checked. Running plugins in a separate process is documented as the next step if the in-process rules prove not enough.
   - `docs/PLUGINS.md` with the full API reference, an example plugin that is built and loaded by a test, and tests with deliberately broken plugins (wrong version, slow, failing).

Every milestone ends the same way: tests added to ctest, `docs/` page, README and both changelogs, translations for all texts, a measured effect on frame time and memory where the feature touches the render loop, a pull request with the CI result, the merge, and the Vault note.

## Principles that apply to all milestones
- Extend, do not rewrite: new code attaches to the Spout slot model, the `AudioInput`/`MidiInput` interfaces, `OutputMapping`, the modulation sources and the shader file loader.
- Cost only when used: a source, a decoder, a mask or a plugin costs nothing until it is on a layer or switched on, and is released after it stops being used (as in ADR 0007 and ADR 0011).
- The render thread never waits for disk, network, a decoder, a device or a plugin.
- Optional libraries are loaded at run time and their absence is a message, not a failure.
- No dependency is added without a line in `docs/DEPENDENCIES.md` with its licence. Proprietary runtimes (NDI) are never bundled.

## Alternatives
1. **Start with the node editor or the plugin API** because they are the most visible. Rejected: they are the largest and least testable without the base (CI on both systems, media timing, sources) under them.
2. **Use an existing node editor library** (imnodes, imgui-node-editor, both MIT). Rejected for now: a small canvas on `ImDrawList` is enough for the planned catalogue and adds no dependency. Revisit if the editor needs more than pan, zoom, drag and wires.
3. **Webcams through Media Foundation and V4L2 directly.** Rejected: two code paths that cannot both be tested here, and MJPEG decoding to write. The FFmpeg device layer is one path over code already loaded at run time.
4. **Plugins as an out-of-process host from the start.** Safer, but needs shared memory frames, a protocol and a process supervisor before the first plugin exists. The in-process rules with a budget, a marker and a safe mode list come first and the interface is shaped so a process boundary can be added without changing plugins.
5. **Ableton Link and other sync protocols.** Not requested here. The beat clock of milestone 5 is the place where they would attach.

## Consequences
- Seven versions of work. Each is useful alone and none depends on a later one; the sync and media milestones meet only at "timecode locked playback", which is switched on when both exist.
- The CI gets longer (a second job) and the publishing job is new. The asset names the updater reads do not change.
- NDI input, cameras, capture cards, Pro DJ Link, StageLinq, projectors and every Windows only path can be compiled and unit tested here but not run against the real thing. Each milestone says so in its docs and asks the maintainer for the first real run.
- Reverse playback of long GOP video costs memory for the decoded group of pictures. The cap is small by default and visible in the video monitor.
- A public plugin ABI is a promise. Version 1.0 of the header will only be declared when the example plugin and the broken plugin tests have been used for one release.

## Validation
Per milestone: ctest on Linux and Windows CI, `scripts/check_i18n.py`, `NyrexVJ --check-shaders`, benchmarks before and after for anything in the render loop (`--bench-comp`, `--bench-video`, `--bench-memory`), and the open items for real hardware listed in the pull request.

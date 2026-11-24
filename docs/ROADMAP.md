# NyrexVJ roadmap

> **Proposal — not a committed release schedule.** Version groupings below are discussion placeholders, subject to maintainer approval and technical discovery. Do not implement an item solely because it appears here.

## Existing baseline: v0.4.0 (documented in README)
- Native C++20/OpenGL/Dear ImGui application.
- Generative clips, effects, presets, 16 decks, transitions.
- Audio capture/analysis, BPM estimate, audio modulation.
- Spout **output** and fullscreen output window.
- English/German UI and application updates.

The current README explicitly says video clips are not implemented. MIDI mapping and OSC input shipped in v0.5.0. Shader-based 3D visuals already exist; this does **not** imply a scene editor or imported 3D models.

## Proposed milestones

### Foundation (candidate v0.5)
- Unified parameter and modulation model: every audio band, LFO, macro and beat ramp is one numbered source with one lookup (`modulationSource`), see [MODULATION.md](MODULATION.md). It landed in v0.12.0; parameters themselves are still addressed by the string ids of the control targets.
- Blend modes (14), undo/redo in the main window and a separate preview channel landed in v0.11.0. Eight macros landed in v0.12.0.
- Render performance budgets and profiling: frame time budget, CPU/GPU timings, `--bench-comp` and tests landed in v0.11.0. A per-pass GPU profiler, hidden-layer skipping, an optional reduced layer resolution and freeing of unused render targets landed in v0.19.0 ([ADR 0011](adr/0011-render-pipeline-optimisation.md), [PERFORMANCE.md](PERFORMANCE.md)). A run on a real GPU, next to VRChat, is still open.
- Define stable clip/source/effect/output interfaces.

### Media and 3D runtime (candidate v0.6)
- Video playback via FFmpeg (H.264 and HAP), image clips and alpha handling landed in v0.13.0, see [MEDIA.md](MEDIA.md). FFmpeg is loaded at run time and is not shipped; a run on Windows with real DLLs and a real GPU is still open. Hardware decoding, HAP direct upload, colour conversion on the graphics card, a frame rate limit and the video monitor landed in v0.18.0 ([ADR 0010](adr/0010-video-decode-pipeline.md)). Hardware decoding on a real graphics card, measurements next to VRChat and zero-copy hand-over to OpenGL are still open. The audit of v0.31.0 ([ADR 0024](adr/0024-video-pipeline-audit-and-measurement.md)) added HDR detection (shown, not tone mapped), first picture time, upload percentiles, nine colour space test clips and the video self test in the CI, and left zero copy open on purpose; the checklist for a real graphics card is in [MEDIA.md](MEDIA.md).
- Spout **receiver** support. A receiver with four input slots landed in v0.10.0 ([ADR 0007](adr/0007-spout-receiver.md)); it was only tested with a test pattern on Linux and still needs a run with a real sender on Windows.
- Prototype glTF/GLB model import and a lightweight native 3D scene renderer. A first prototype landed in v0.6.0 (see [SCENE3D.md](SCENE3D.md)); it still needs frame time measurements on real GPUs and maintainer decisions on scope. The Spout receiver landed separately in v0.10.0.

### Live control (candidate v0.7)
- OSC output and a documented, versioned OSC protocol (MIDI mapping and OSC input already exist since v0.5.0). OSC output, schema 1, landed in v0.10.0 ([ADR 0008](adr/0008-osc-output-protocol.md), [UNREAL.md](UNREAL.md)).
- Beatgrid/tempo synchronization improvements and reusable modulation sources. Tap tempo, resync, nudge, /2, x2 and Sync BPM to audio already existed; v0.12.0 adds the beats per bar, grid shifting, the Beat and Bar sources, four LFOs and eight macros.
- Unreal integration proof of concept: Spout video into NyrexVJ, control/audio data to Unreal via OSC. Both channels exist since v0.10.0 (see [ADR 0002](adr/0002-unreal-integration.md) and [UNREAL.md](UNREAL.md)); a test with a real Unreal Engine and latency measurements are still open.

### Integrated 3D Clip Creator (candidate v0.8)
- **Status:** a first Clip Creator and the `.nyxclip` format landed in v0.9.0 (see [ADR 0006](adr/0006-clip-creator.md) and [SCENE3D.md](SCENE3D.md)). The list below stays as the longer term wish list; what exists is described in the ADRs.
- Dedicated editor for simple scenes, cameras, primitives, imported models, materials, and looping animation. Up to eight cameras per scene with beat cuts landed in v0.11.0.
- Audio-reactive 3D parameters.
- Reuse the native 3D runtime and render scenes to offscreen textures usable as ordinary clips.
- Explore a versioned, portable community clip-pack format (working name `.nyxclip`).

### Professional output (candidate v0.9)
- Mapped NDI per output, curved, freehand and picture masks and colour per slice (Advanced Output 2.0) landed in v0.30.0, see [OUTPUTS.md](OUTPUTS.md) and [ADR 0023](adr/0023-advanced-output-2.md). Tested with software OpenGL and a fake NDI library only.
- Multi-display output, projection mapping, warping, edge blending and NDI output landed in v0.14.0, see [OUTPUTS.md](OUTPUTS.md). Real projectors, real multi-monitor fullscreen and the real NDI runtime are untested. DMX over Art-Net, the VRSL strip in the video and VRChat OSC parameters landed in v0.15.0, see [DMX.md](DMX.md). The strip's channel order is unverified against a real VRSL world, and no real Art-Net node or VRChat was tried.
- Extend Unreal SDK with Blueprint-accessible controls and optional discovery if the prototype succeeds. An experimental plugin with Blueprint access to the OSC data and to the controls landed in v0.17.0 ([ADR 0009](adr/0009-unreal-plugin.md), `integrations/unreal/NyrexVJ`). Its protocol core is tested on every build, the Unreal-facing code has never been compiled with Unreal Engine, so it is not a supported SDK yet. Discovery is not started.

### Production readiness (candidate v1.0)
- Reliability, performance regression tests, crash recovery, project compatibility, documentation, and release quality. Crash recovery (session marker, rotating autosaves, crash report, recovery dialog) and the performance benchmark landed in v0.11.0.

## Plan towards a full open source VJ platform (ADR 0012, approved 2026-11-20)
Seven milestones, one issue and one pull request each, in this order. Status is tracked in the issues, not here.

| Version | Milestone | Content |
| --- | --- | --- |
| 0.20.0 | Platforms and releases | Linux CI, portable Linux archive, Linux audio capture and MIDI, Linux updater, dependency and licence list |
| 0.21.0 | Media control | Reverse playback, scrubbing, exact seek, in and out points, loop modes, speed, beat sync, switching without drops |
| 0.22.0 | Live sources | NDI input with discovery, webcams and capture cards, reconnect, low latency upload |
| 0.23.0 | Advanced output | Several slices and masks per output, colour correction per output, venue setups, smoother warp, black level in blends |
| 0.24.0 | Show sync | One beat clock, SMPTE (LTC, MTC), Pro DJ Link, StageLinq evaluation, output frame lock |
| 0.25.0 | Node effect editor | Graph document, graph to GLSL compiler, node editor, shareable effect files |
| 0.26.0 | Plugin system and API | C ABI header, versioning, compatibility check, isolation of faulty plugins |

## Already listed in the README roadmap
- Video clips via FFmpeg (H.264/HAP): landed in v0.13.0, see [MEDIA.md](MEDIA.md).
- DXV videos converted to HAP in the background and the FFmpeg setup by button: landed in v0.28.0, see [MEDIA.md](MEDIA.md), [FFMPEG.md](FFMPEG.md) and [ADR 0021](adr/0021-dxv-hap-conversion-and-ffmpeg-setup.md).
- DMX strip output for VRSL: landed in v0.15.0, see [DMX.md](DMX.md).
- Custom shader clips loaded from files: landed in v0.16.0, see [SHADERS.md](SHADERS.md). Alpha output, Buffer A to D and several image and video inputs (Shader 2.0): landed in v0.29.0, see [ADR 0022](adr/0022-glsl-shader-2.md).
- More languages: Spanish and French plus your own language files landed in v0.16.0, Italian, Portuguese, Dutch and Japanese in v0.27.0, see [LANGUAGES.md](LANGUAGES.md). All of them except German are machine assisted and not reviewed by native speakers. Japanese uses a system font that is merged in at run time; your own language files are still limited to Latin-1.

These existing commitments/ideas should be reconciled with the proposed milestones by maintainers, not silently replaced.

## Tracking rules
- One GitHub Issue per scoped feature or research task, with acceptance criteria and dependencies.
- Use labels/statuses such as `proposal`, `approved`, `in progress`, and `done` if maintainers adopt them.
- Do not mark features as done until verified against code and tests.
- Track unresolved design decisions in Issues or decision records.

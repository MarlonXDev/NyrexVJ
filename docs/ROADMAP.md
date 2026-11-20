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
- Render performance budgets and profiling: frame time budget, CPU/GPU timings, `--bench-comp` and tests landed in v0.11.0; a run on a real GPU is still open.
- Define stable clip/source/effect/output interfaces.

### Media and 3D runtime (candidate v0.6)
- Video playback via FFmpeg (H.264 and HAP), image clips and alpha handling landed in v0.13.0, see [MEDIA.md](MEDIA.md). FFmpeg is loaded at run time and is not shipped; a run on Windows with real DLLs and a real GPU is still open.
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
- Multi-display output, projection mapping, warping, edge blending and NDI output landed in v0.14.0, see [OUTPUTS.md](OUTPUTS.md). Real projectors, real multi-monitor fullscreen and the real NDI runtime are untested. DMX over Art-Net, the VRSL strip in the video and VRChat OSC parameters landed in v0.15.0, see [DMX.md](DMX.md). The strip's channel order is unverified against a real VRSL world, and no real Art-Net node or VRChat was tried.
- Extend Unreal SDK with Blueprint-accessible controls and optional discovery if the prototype succeeds. Not started: no Unreal plugin exists and no public SDK is promised without maintainer approval.

### Production readiness (candidate v1.0)
- Reliability, performance regression tests, crash recovery, project compatibility, documentation, and release quality. Crash recovery (session marker, rotating autosaves, crash report, recovery dialog) and the performance benchmark landed in v0.11.0.

## Already listed in the README roadmap
- Video clips via FFmpeg (H.264/HAP): landed in v0.13.0, see [MEDIA.md](MEDIA.md).
- DMX strip output for VRSL: landed in v0.15.0, see [DMX.md](DMX.md).
- Custom shader clips loaded from files: landed in v0.16.0, see [SHADERS.md](SHADERS.md).
- More languages: Spanish and French plus your own language files landed in v0.16.0, see [LANGUAGES.md](LANGUAGES.md). The Spanish and French texts are machine assisted and not reviewed by native speakers, and scripts beyond Latin-1 need a font that is not built yet.

These existing commitments/ideas should be reconciled with the proposed milestones by maintainers, not silently replaced.

## Tracking rules
- One GitHub Issue per scoped feature or research task, with acceptance criteria and dependencies.
- Use the labels `proposal`, `approved`, `in progress` and `done` ([ADR 0003](adr/0003-shared-ai-collaboration.md)).
- A verified status check of every item above is tracked in issue [#25](https://github.com/MarlonXDev/NyrexVJ/issues/25).
- Do not mark features as done until verified against code and tests.
- Track unresolved design decisions in Issues or decision records.

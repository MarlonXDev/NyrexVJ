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
- Review unified parameter and modulation model.
- Evaluate blend modes, macros, undo/redo, and separate preview/live workflows.
- Define render performance budgets and profiling strategy.
- Define stable clip/source/effect/output interfaces.

### Media and 3D runtime (candidate v0.6)
- Video playback via FFmpeg; evaluate H.264 and HAP support, decoding, seeking, synchronization, and packaging.
- Image clips and alpha handling.
- Spout **receiver** support.
- Prototype glTF/GLB model import and a lightweight native 3D scene renderer. A first prototype landed in v0.6.0 (see [SCENE3D.md](SCENE3D.md)); it still needs frame time measurements on real GPUs and maintainer decisions on scope. Video, image clips and the Spout receiver are untouched.

### Live control (candidate v0.7)
- OSC output and a documented, versioned OSC protocol (MIDI mapping and OSC input already exist since v0.5.0).
- Beatgrid/tempo synchronization improvements and reusable modulation sources.
- Unreal integration proof of concept: Spout video into NyrexVJ, control/audio data to Unreal via OSC.

### Integrated 3D Clip Creator (candidate v0.8)
- **Status:** a first Clip Creator and the `.nyxclip` format landed in v0.9.0 (see [ADR 0006](adr/0006-clip-creator.md) and [SCENE3D.md](SCENE3D.md)). The list below stays as the longer term wish list; what exists is described in the ADRs.
- Dedicated editor for simple scenes, cameras, primitives, imported models, materials, and looping animation.
- Audio-reactive 3D parameters.
- Reuse the native 3D runtime and render scenes to offscreen textures usable as ordinary clips.
- Explore a versioned, portable community clip-pack format (working name `.nyxclip`).

### Professional output (candidate v0.9)
- Evaluate multi-display output, projection mapping, warping, edge blending, NDI, and stage/VRChat control integration.
- Extend Unreal SDK with Blueprint-accessible controls and optional discovery if the prototype succeeds.

### Production readiness (candidate v1.0)
- Reliability, performance regression tests, crash recovery, project compatibility, documentation, and release quality.

## Already listed in the README roadmap
- Video clips via FFmpeg (H.264/HAP).
- DMX strip output for VRSL.
- Custom shader clips loaded from files.
- More languages.

These existing commitments/ideas should be reconciled with the proposed milestones by maintainers, not silently replaced.

## Tracking rules
- One GitHub Issue per scoped feature or research task, with acceptance criteria and dependencies.
- Use labels/statuses such as `proposal`, `approved`, `in progress`, and `done` if maintainers adopt them.
- Do not mark features as done until verified against code and tests.
- Track unresolved design decisions in Issues or decision records.

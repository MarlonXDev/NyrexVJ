# Architecture overview and proposals

> **Working document.** Current facts below come from the project's README; proposed components are not yet implemented or approved. Verify all interfaces against source before coding.

## Documented current architecture
- C++20 native Windows application, CMake build.
- OpenGL 3.3 renderer, GLSL clips/effects, Dear ImGui UI.
- `src/app`: user interface and application orchestration.
- `src/audio`: WASAPI capture, spectrum/beat analysis.
- `src/core`: shared application logic and state.
- `src/render`: rendering, clips, and effects, including the 3D scene player (`scene_player.*`).
- `src/scene3d`: glTF/GLB loader with safety limits, scene model, built-in 3D scenes (no OpenGL, unit tested).
- `src/platform`: platform-specific integration.
- `src/update`: updater.
- Spout sender, a Spout receiver with four input slots (v0.10.0, see [ADR 0007](adr/0007-spout-receiver.md)) and the fullscreen output are documented.

Inspect source and tests before claiming exact internal ownership, threading, or data flow.

## Proposed modular direction

```text
Sources (generative / video / images / native 3D / Spout / future NDI)
    -> Clip and layer composition
    -> Effects + unified parameters + audio/tempo modulation
    -> Master composition
    -> Preview / fullscreen / Spout / future mapping & other outputs
```

The diagram is a target abstraction, not a description of existing code.

## Native 3D runtime and Clip Creator

**Implemented as a prototype (v0.6.0, Issue #6):** glTF/GLB loading on a worker thread, node animation, base colour textures, fixed lighting, rendering into the layer texture with a shared depth buffer, built-in 3D clips and user models from a folder. Details, limits and measurements: [SCENE3D.md](SCENE3D.md). Everything below that is not listed there is still a proposal.

- Prefer a focused native renderer integrated with the existing graphics backend, not an embedded game engine.
- Investigate glTF/GLB loading, scene graph, cameras, materials, simple lights, reusable GPU resources, and shader caching.
- Render 3D clips into offscreen framebuffers/textures for the existing compositor.
- Keep the Creator UI separate from the scene runtime so saved scenes play without the editor.
- Plan for versioned scene serialization and safe handling of missing assets.
- Benchmark before choosing more complex rendering technology.

## Unreal Engine interoperability
- Keep Unreal as a separate process.
- First validate a Windows proof of concept for Unreal-rendered video -> Spout -> NyrexVJ Spout receiver.
- Investigate interoperability between Unreal's graphics backend and NyrexVJ's OpenGL rendering, including GPU sharing, synchronization, and fallback behavior.
- Use OSC for parameter control, tempo/beat and audio analysis values; define a versioned protocol before building a public SDK.
- Since v0.10.0 the Spout receiver and a versioned OSC output (schema 1, [ADR 0008](adr/0008-osc-output-protocol.md)) exist and are described in [UNREAL.md](UNREAL.md). They were tested without Unreal Engine and without Windows hardware, so the proof of concept is not finished.
- A future Unreal plugin could expose Blueprint nodes and controllable parameters, but these are proposals, not existing APIs.
- Consider a reusable NyrexVJ control protocol for other creative tools.

## Performance and reliability requirements to validate
- What the renderer skips, the optional reduced layer resolution, the pass profiler and the measurements are in [PERFORMANCE.md](PERFORMANCE.md) and [ADR 0011](adr/0011-render-pipeline-optimisation.md).
- Never block the render loop on video decoding, file I/O, or network operations.
- Specify GPU/CPU memory budgets and test under multi-layer load.
- Handle lost Spout senders, missing media, GPU resource failures, and app restart gracefully.
- What is tested automatically, what needs real hardware, the long run test (`--soak`) and the measuring script are in [TESTING.md](TESTING.md) and [ADR 0027](adr/0027-live-stability-and-hardware-test-matrix.md).
- Preserve preset/session compatibility when evolving clip types and parameters.
- Validate third-party licenses and binary distribution constraints before adding dependencies.

## Open design questions
1. Existing layer/clip API boundaries and compatibility requirements.
2. Texture format, color space, alpha, and synchronization across sources.
3. Media decoding strategy and HAP availability.
4. Stable scene/clip file format and asset packaging.
5. OSC schema, transport, security, and discovery behavior.
6. Scope and performance budgets for mapping and multi-output.

Resolve these through focused issues and maintainer-reviewed decisions.

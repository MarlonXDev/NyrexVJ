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
- Live recording (v0.35.0, see [ADR 0028](adr/0028-live-recording.md) and [RECORDING.md](RECORDING.md)): `src/render/recorder.*` does the OpenGL part on the render thread (one scaled copy and an asynchronous read back with a fence), `src/media/video_recorder.*` runs the colour conversion, the encoding and the writing on one thread at background priority and has no OpenGL, `src/core/record_plan.*` holds the formats, the checks, the frame schedule and the audio sync as plain code that is unit tested, and `AudioSystem` gives the samples of a real input to the recorder through `AudioSink`.
- Video routing (v0.47.0, see [ADR 0040](adr/0040-video-routing.md) and [OUTPUTS.md](OUTPUTS.md)): `src/core/routing.*` holds the sources, the rules, the plan per frame, the surface checks and the cache tracker (no OpenGL, unit tested), `src/render/routing_engine.cpp` copies pictures with `glBlitFramebuffer` at fixed points in `Engine::renderDeck` and draws them for the clips of kind `RouteSource` and for the senders, and `src/app/ui_routing.cpp` is the window.
- Network sync (v0.48.0, see [ADR 0041](adr/0041-multi-pc-sync.md) and [SYNC.md](SYNC.md)): `src/core/netsync.*` is the protocol (packets, signature, negotiation, the master and follower state machines, the clock estimator) with an injected clock and random numbers and no sockets, tested through `netsync_link.hpp`; `src/control/netsync_show.*` maps the show onto a state vector and applies it, `netsync_transport.*` runs it over a UDP socket, `netsync_key.*` keeps the key, and `src/app/netsync_system.*` ties it to the engine, the controls, the beat clock and the timecode once per frame.
- Hot standby (v0.49.0, see [ADR 0042](adr/0042-hot-standby.md) and [SYNC.md](SYNC.md)): `src/core/failover.*` holds the eleven readiness checks, the state machine (`failover::Machine`) and the measures of a takeover with a clock that is passed in and no sockets or OpenGL; `src/core/netsync.*` got protocol version 2 (epoch, health, readiness and show fingerprint tails, the `Takeover` message, a Bye reason). `src/app/failover_system.*` gives the machine its facts once per frame (the engine's decoder faults, open outputs, the age of the last session snapshot, the follower state of `NetSyncSystem`), holds the master output black while a standby follows (`Engine::holdOutput`) and carries out a takeover through `NetSyncSystem::becomeLeader` and `stepDown`. `src/app/ui_failover.cpp` is the window Output, Hot standby..., `failover_selftest.*` the self test and `scripts/test_failover_two_instances.py` the test with real programs.
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

**Implemented as a prototype (v0.6.0, Issue #6):** glTF/GLB loading on a worker thread, node animation, base colour textures, fixed lighting, rendering into the layer texture (with one depth buffer per shared world since v0.70.0), geometry built from an optional node graph (since v0.71.0), built-in 3D clips and user models from a folder. Details, limits and measurements: [SCENE3D.md](SCENE3D.md). Everything below that is not listed there is still a proposal.

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

# ADR 0001: Native 3D runtime and separate Clip Creator

- **Status:** Accepted (2026-11-18, approved by MarlonXDev)
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [Architecture](../ARCHITECTURE.md), [Roadmap](../ROADMAP.md)

## Context
NyrexVJ already renders GLSL-based generative visuals, including 3D-looking shaders, using its native C++20/OpenGL renderer. A future 3D Clip Creator would need imported assets, cameras, scene objects, animation, and audio-reactive controls without turning the live renderer into a heavyweight editor.

## Proposed decision
- Keep the current native rendering architecture as the initial integration target.
- Investigate a lightweight scene runtime supporting glTF/GLB assets, cameras, simple lighting/materials, animation, and offscreen rendering.
- Separate scene playback/runtime from Creator editing UI. A saved scene must render as a clip without opening the editor.
- Feed scene output into the existing layer/effect pipeline using GPU textures/framebuffers, subject to validation of current compositor interfaces.
- Benchmark before committing to a scene graph, asset library, shading model, or clip file format.

This is a proposed direction, **not** approval to build a new renderer.

## Alternatives
1. **Embed Unreal Engine:** powerful tooling, but substantial size, deployment, integration, and runtime complexity.
2. **Embed a general-purpose 3D editor/engine:** faster feature access, but larger dependency and maintenance surface.
3. **Pre-render 3D content to video only:** simpler playback, but no native real-time parameter and audio interaction.
4. **Keep shader-only 3D:** minimal architectural change, but cannot offer a scene-oriented Creator.

## Consequences
- Potentially lower overhead and tighter integration with NyrexVJ's clip and audio systems.
- Additional responsibility for asset management, GPU resource lifetimes, material/shader compatibility, serialization, and testing.
- OpenGL 3.3 compatibility and target hardware may constrain materials and effects.
- Imported assets and clip packs require licensing, path handling, and safe loading policies.

## Validation / open questions
- Inspect existing renderer/clip abstractions and determine the least disruptive integration point.
- Prototype one glTF/GLB scene rendered into a compositor-compatible texture.
- Measure frame times and memory with multiple simultaneous 3D clips.
- Decide scene serialization and asset packaging only after testing.
- Confirm supported material, animation, and lighting scope with maintainers.

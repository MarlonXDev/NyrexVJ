# ADR 0001: Native 3D runtime and separate Clip Creator

- **Status:** Accepted (2026-11-18, approved by MarlonXDev)
- **Implementation:** Prototype implemented in v0.6.0 ([#6](https://github.com/MarlonXDev/NyrexVJ/issues/6), [#7](https://github.com/MarlonXDev/NyrexVJ/pull/7)). Open follow-ups are listed at the end.
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [Architecture](../ARCHITECTURE.md), [Roadmap](../ROADMAP.md), [SCENE3D.md](../SCENE3D.md), [#3](https://github.com/MarlonXDev/NyrexVJ/pull/3)

## Context
NyrexVJ already renders GLSL-based generative visuals, including 3D-looking shaders, using its native C++20/OpenGL renderer. A future 3D Clip Creator would need imported assets, cameras, scene objects, animation, and audio-reactive controls without turning the live renderer into a heavyweight editor.

## Decision
- Keep the current native rendering architecture as the integration target. No second renderer and no embedded engine.
- Add a lightweight scene runtime for glTF/GLB assets with node animation, simple lighting and materials, and offscreen rendering.
- Keep scene playback separate from any Creator editing UI. A scene renders as a clip without opening an editor.
- Feed scene output into the existing layer and effect pipeline as a GPU texture, so effects, blend modes, decks and presets work on 3D clips unchanged.
- Measure before committing to a scene graph of our own, an asset library, a physically based shading model, or a clip file format. None of these is decided by this ADR.

This ADR approves the direction and the prototype. It does **not** approve a Clip Creator, an editor UI, a new file format, or extra dependencies beyond the PNG/JPEG decoder already in use.

## Alternatives
1. **Embed Unreal Engine:** powerful tooling, but substantial size, deployment, integration, and runtime complexity.
2. **Embed a general-purpose 3D editor/engine:** faster feature access, but larger dependency and maintenance surface.
3. **Pre-render 3D content to video only:** simpler playback, but no native real-time parameter and audio interaction.
4. **Keep shader-only 3D:** minimal architectural change, but cannot offer a scene-oriented Creator.

## Consequences
- Scene clips share layers, effects, decks and audio reactivity with every other clip. The cost is one shared depth buffer (about 8 MiB at 1920x1080) and the geometry of each loaded model, which is shared between layers.
- The project now owns asset loading, GPU resource lifetimes and the safety of untrusted model files. Loading is limited and tested (see [SCENE3D.md](../SCENE3D.md)) and never runs on the render thread.
- OpenGL 3.3 and a fixed lighting model keep the prototype cheap; richer materials are a later, measured decision.
- Imported assets and future clip packs still need licensing, path handling and packaging rules before any file format is defined.

## Validation results
| Question | Result |
| --- | --- |
| Least disruptive integration point | Done. A scene clip renders into the layer texture through its own framebuffer and the unchanged effect chain and compositor follow. |
| One glTF/GLB scene in a compositor-compatible texture | Done, with textures, node hierarchy and looping animation. Verified on Linux (headless) and by unit tests; the Windows build passes CI. |
| Memory with several simultaneous 3D clips | Measured. Render targets about 40 MiB and the shared depth buffer about 8 MiB at 1080p with four layers; geometry is paid once per model. |
| Frame times with several simultaneous 3D clips | **Open.** Measured only on a software renderer, which proves the benchmark but not the performance. Needs `NyrexVJ --bench-scene` on a real GPU. |
| Scene serialization and asset packaging | Deliberately not decided. Models are plain `.glb`/`.gltf` files in a folder; presets reference them by name. |
| Material, animation and lighting scope | Prototype scope: base colour and texture, node animation, fixed lights. **Open:** maintainers confirm or change it. |
| Safe loading policy | Done. Size, count, depth, path and range limits with tests. **Open:** cancelling a very slow load. |

## Open follow-ups
- Run `NyrexVJ.exe --bench-scene 4:300:"Torus Knot Mesh" --bench-out result.json` on a real GPU and record the result here.
- Maintainers confirm the prototype scope (materials, animation, lighting) or request changes.
- Cancellation and a time limit for slow model loads.
- A separate ADR before any scene file format, clip pack format (`.nyxclip`), asset packaging, PBR shading, or Creator UI is started.

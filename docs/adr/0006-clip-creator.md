# ADR 0006: Clip Creator

- **Status:** Accepted (2026-11-18, approved by MarlonXDev)
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0001](0001-native-3d-runtime.md), [ADR 0004](0004-scene-document-and-clip-pack.md), [ADR 0005](0005-pbr-shading.md), [Roadmap](../ROADMAP.md)

## Context
ADR 0001 kept playback separate from any editor and did not approve an editor UI. MarlonXDev asked for ADR 0001 to be finished completely. The roadmap describes a dedicated editor for simple scenes, cameras, primitives, imported models, materials and looping animation, with audio-reactive parameters, that renders scenes to ordinary clips.

## Decision
- **Separate window.** The Creator is its own window (Creator in the main menu, and a button in the Library). Playing a scene clip never opens it and never needs it.
- **Same runtime.** The Creator edits a scene document ([ADR 0004](0004-scene-document-and-clip-pack.md)), converts it to a runtime scene and draws it with the same `SceneRenderer` and the same shading ([ADR 0005](0005-pbr-shading.md)) into its own preview texture. What the preview shows is what the clip plays.
- **Scope.** Add, duplicate, rename and delete objects (primitives: sphere, cube, cylinder, cone, plane, torus, torus knot; imported models from the asset library), lights (directional, point) and edit the camera (orbit speed, distance, height, field of view). Per object: position, rotation, scale, repeat (a ring or a line of copies), material (colour, metallic, roughness, emissive, texture, follow layer colours), up to two loop animations (spin, bob, orbit, pulse, sway) and up to two audio bindings (Bass, Mid, High, Level or Kick to scale, emissive, spin speed, height or light intensity). Undo and redo keep a bounded list of earlier documents.
- **Output.** Save writes a `.nyxclip` into the `clips/` folder, Use as clip saves, rescans and puts the clip into the selected cell. The preview camera can be orbited with the mouse; this camera is for editing only and is not saved unless Set as clip camera is pressed.
- **Not in scope.** No timeline or keyframes, no gizmos in the viewport, no skeletal animation, no boolean modelling, no sculpting, no physics, no scripting. These need their own decision.
- **Live loop.** Editing changes the document, the document is converted and uploaded again only when it changed. Scene clips already in decks are reloaded when their pack is saved again.

## Alternatives
1. **No editor, only hand-written JSON:** cheapest, but not usable for a performer in the middle of an event.
2. **Import from a general 3D tool only:** the tool has to be owned and learned, and the result loses live parameters and audio bindings.
3. **Dock the editor into the main layout:** convenient, but the Creator needs space and the live layout must stay clear.

## Consequences
- The Creator is UI code only. It uses the document, the converter and the renderer, so the runtime has no knowledge of it.
- Every Creator action is a plain document edit, which is why undo is a list of documents and why tests can run without a window.
- Anything the Creator offers must exist in the document format; adding new object types later means a new document version or an ignored-field extension.

## Validation
Unit tests cover document edits, conversion and round trips. The window was exercised on Linux with simulated input and screenshots; a real GPU and Windows are not covered.

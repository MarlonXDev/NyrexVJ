# ADR 0004: Scene document, `.nyxclip` pack format and asset library

- **Status:** Accepted (2026-11-18, approved by MarlonXDev)
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0001](0001-native-3d-runtime.md), [ADR 0005](0005-pbr-shading.md), [ADR 0006](0006-clip-creator.md), [SCENE3D.md](../SCENE3D.md)

## Context
ADR 0001 approved a native 3D scene player and said that a scene file format, a clip pack format (`.nyxclip`), asset packaging and an asset library need their own decision first. MarlonXDev asked for ADR 0001 to be finished completely, which includes these points. The prototype plays `.glb`/`.gltf` files and three built-in scenes made by code. Presets reference a clip by name only.

## Decision
- **Scene document.** One authoring model, the scene document (`SceneDoc`), describes an authored scene: objects (a primitive or an imported model, with position, rotation, scale, an optional repeat), one material per object, lights, one camera, up to two loop animations per object and up to two audio bindings per object, light and camera. It is stored as JSON with `"format": "nyxscene"` and an integer `"version"` (1 now). It is converted to the existing runtime `Scene` before drawing, so the player, renderer, decks, effects and presets stay untouched.
- **Pack file.** A `.nyxclip` file is a single binary container written by NyrexVJ itself, not a zip. This keeps the rule of ADR 0001 that no dependency beyond the PNG/JPEG decoder is added.
  - Header: 8 bytes magic `NYXCLIP1`, `u32` format version (1), `u32` entry count. All integers are little endian.
  - Directory: per entry a `u16` name length, the name (UTF-8, forward slashes, no `..`, no drive letter, no leading slash, at most 128 bytes), a `u64` size, a `u64` offset from the start of the file and the SHA-256 of the data.
  - Data: the entries follow the directory. `scene.json` is required. Other entries live under `assets/` (models as `.glb`, images as `.png` or `.jpg`).
  - Reading verifies the magic, the version, every range with overflow safe arithmetic, the name rules, duplicate names, the SHA-256 of each entry, and the limits (file 256 MiB, 256 entries, `scene.json` 4 MiB, one asset 128 MiB). A newer version than the program knows is refused with a message. Unknown JSON fields are ignored, so small additions do not need a new version.
- **Portability.** Saving a pack copies every model and texture the scene uses into the pack, so a pack is self-contained and can be shared as one file. Models are embedded as `.glb` and loaded through the existing safe glTF loader with its limits and its cancellation.
- **Asset library.** The user data folder holds `models/` (existing, `.glb`/`.gltf`), `textures/` (`.png`/`.jpg`) and `clips/` (saved `.nyxclip` packs). The Library lists the packs as clips named `Scene: file name`, and lists models and textures in an Assets view. A rescan picks up new files without a restart. A scene that references an asset which is in neither the pack nor the folders loads without that object and with a warning, the same way a missing texture already behaves.
- **Scene graph.** NyrexVJ keeps one scene graph: the runtime `Scene` node tree that the glTF loader and the document converter both fill. The document itself is a flat list of objects (an object may repeat itself but has no parent), so there is no second graph and no parenting editor.
- **Unloading.** A scene that no layer has used for two minutes releases its geometry and textures from the GPU. It loads again on the next use, in the background, like the first load.
- **Presets** keep referencing clips by name. A missing pack is an empty cell, as for a missing model.

## Alternatives
1. **Zip container:** well known, but needs a zip and inflate dependency.
2. **Folder with loose files:** easy to edit, but not shareable as one file and easy to break.
3. **JSON with base64 assets:** one file and no new code, but 33 percent larger and slow to parse for big models.
4. **A glTF extension:** reuses glTF tooling, but ties authoring data to a format that cannot describe lights, bindings or loop animations the way the Creator needs.

## Consequences
- Format version 1 is a promise: later versions must still read it.
- The program owns a small container reader that handles untrusted input, covered by tests (truncation, lying sizes, overlapping and out-of-range entries, bad names, bad hashes, huge counts).
- Authoring data and runtime data stay separate, so the player does not change when the Creator changes.

## Validation
Unit tests cover write and read round trips, every rejection above, the JSON conversion and the conversion to a runtime scene. Packs and assets were exercised through the Creator and through headless runs on Linux; a real GPU and Windows paths are not covered.

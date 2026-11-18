# ADR 0005: Physically based shading for scene clips

- **Status:** Accepted (2026-11-18, approved by MarlonXDev)
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0001](0001-native-3d-runtime.md), [ADR 0004](0004-scene-document-and-clip-pack.md), [SCENE3D.md](../SCENE3D.md)

## Context
ADR 0001 left a physically based shading model open because the prototype used a fixed, cheap model and nothing was measured. MarlonXDev asked for ADR 0001 to be finished completely.

## Decision
- **Model.** Metallic-roughness as in glTF 2.0: base colour, metallic, roughness, emissive colour and strength, with a GGX normal distribution, Smith visibility and Schlick Fresnel, and a Lambert diffuse term that is reduced by the metallic value.
- **Textures.** Base colour, metallic-roughness (green roughness, blue metallic), normal and emissive textures, using texture units 0 to 3. Normal maps use a tangent frame built from screen space derivatives, so no tangent attribute is added to the vertex format.
- **Lights.** Up to four lights per scene, directional or point, with colour and intensity. A scene without lights gets a default key, fill and rim rig. No shadows.
- **Environment.** A small analytic sky (gradient, horizon band, sun) gives diffuse ambient light and the reflections of metals. There are no HDR files, no environment maps and no pre-filtered cube maps, so no new asset type and no new dependency. Roughness blends the reflection toward the average sky.
- **glTF breadth.** Supported: metallic-roughness materials, base colour, metallic-roughness, normal and emissive textures, emissive factor, `KHR_materials_emissive_strength`, alpha mode OPAQUE and MASK (cut-out), double sided. Not supported and ignored with a warning: skins, morph targets, glTF lights and cameras, other extensions, blended transparency. A model that needs them still loads, it just shows without them.
- **Output.** Tone mapping is a simple filmic curve before the existing layer pipeline, so effects and blend modes keep working on an ordinary colour texture.
- **Legacy mode.** The three built-in scenes of v0.6.0 keep the classic fixed model and the layer palette tint, so existing presets look the same. Scenes with `shading: "pbr"` (loaded models, packs and new built-in scenes) use the model above.
- **Colour follow.** A material can opt in to follow the layer palette (Color A to Color B, rainbow), so authored scenes still respond to the layer colours like every other clip.

## Alternatives
1. **Keep only the fixed model:** cheapest, but metals and glass look flat and models do not look like their previews elsewhere.
2. **Image based lighting with HDR files:** the best looking reflections, but needs an HDR decoder, an asset type and pre-filtering.
3. **Shadow maps:** better depth, but a second pass per light and a larger memory cost for every layer.

## Consequences
- The fragment shader is more expensive than the classic one. The cost is per pixel covered by geometry and independent of the number of layers that show other clips.
- Without environment maps, reflections are a stylised sky, not the surroundings of a real place.
- Frame times were only measured on a software renderer. `NyrexVJ --bench-scene` still has to be run on a real GPU; that is a task for the maintainer and is recorded in ADR 0001.

## Validation
`--check-shaders` compiles the shader on every start of CI. Headless renders of spheres with a metallic and a roughness sweep, a lit model and an emissive object were checked by eye on Linux.

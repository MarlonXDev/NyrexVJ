# Native 3D scene player (prototype)

This is the 3D scene player that [ADR 0001](adr/0001-native-3d-runtime.md) asked for. It plays 3D scenes as ordinary clips inside the existing layer and effect pipeline. Since v0.9.0 the scenes can also come from the Clip Creator and from `.nyxclip` files ([ADR 0004](adr/0004-scene-document-and-clip-pack.md), [ADR 0005](adr/0005-pbr-shading.md), [ADR 0006](adr/0006-clip-creator.md)); see "Decisions" at the end.

## How it fits into the renderer

```text
.glb / .gltf file or built-in scene
    -> gltf.cpp / procedural.cpp          (src/scene3d, no OpenGL, loads on a worker thread)
    -> Scene (nodes, meshes, materials, images, animations)
    -> ScenePlayer                         (src/render/scene_player.*, uploads to the GPU on the render thread)
    -> SceneRenderer                       (draws into the layer's own texture with a shared depth buffer)
    -> layer effect chain -> composite     (unchanged)
```

- A scene clip is a `ClipDef` with `kind` `BuiltinScene`, `ModelFile` or `ScenePack` (a `.nyxclip` file) instead of a fragment shader body. Presets store it by name like any other clip, an unknown name stays an empty cell.
- `SceneRenderer` attaches the layer texture (`layerA_`) to its own framebuffer together with one depth renderbuffer. There is no extra colour target, no blit and no per-clip framebuffer. The depth buffer is shared by every scene clip.
- `Engine::renderDeck` draws a scene clip and a shader clip through the same code path afterwards: effects, blend mode, opacity and master chain are untouched.
- Scene data is loaded by `std::async`, so file reading, parsing and image decoding never run on the render thread. The upload to the GPU is done on the render thread the first time the clip is in a deck cell. Until it is ready the layer is simply empty.
- After the upload the CPU copy of the geometry and images is released. Several layers (and several decks) that play the same scene share one set of GPU buffers.

## What is supported

| Area | Supported | Not supported (yet) |
| --- | --- | --- |
| Container | `.glb` and `.gltf` (external `.bin`, `data:` URIs, relative image files) | Draco, Meshopt, any other required extension |
| Geometry | triangles, triangle strips, triangle fans, indexed or not, `POSITION`, `NORMAL` (computed when missing), `TEXCOORD_0`, `COLOR_0`, normalized and quantized integer attributes | points, lines, sparse accessors, second UV set, tangents |
| Materials | base colour factor and texture (PNG and JPEG), metallic-roughness factors and texture, normal texture, emissive factor, texture and `KHR_materials_emissive_strength`, alpha mode opaque and mask, double sided | occlusion, alpha blend (drawn opaque, with a warning), texture transforms, other material extensions |
| Hierarchy | node tree with translation, rotation, scale or matrix, mesh sharing between nodes | skins, morph targets, cameras and lights from the file |
| Animation | translation, rotation and scale channels, linear and step, cubic spline approximated as linear, looping | weights, skeletal animation |

Shading is a fixed, cheap model: two directional lights, a little ambient light and a rim light. Built-in scenes are drawn in the layer palette (Color A to Color B, or the rainbow switch); models keep their own colours. The camera orbits the scene automatically, is fitted to its bounding sphere and zooms with the layer Scale. Layer Speed drives both the orbit and the animation, Bass pulses the scale and Kick adds a small punch and a stronger rim.

## Loading safety

Files come from the user's disk, but model packs come from the internet, so the loader treats them as untrusted. All limits are in `GltfLimits` and can be changed in code.

| Check | Limit |
| --- | --- |
| File size, buffers together | 192 MiB |
| JSON text and nesting depth | 16 MiB, 64 levels |
| Vertices, indices | 2 000 000, 6 000 000 per scene |
| Nodes, meshes, animation keys | 10 000, 4 096, 200 000 |
| Node hierarchy depth, one parent per node | 64, no cycles |
| Image size, decoded image memory | 4096 x 4096, 192 MiB in total |
| Accessors | every range is checked against its buffer view and buffer with overflow safe arithmetic, indices must be in range, values must be finite |
| External files | relative paths only, no drive letters, no `..`, no symlink escape from the model folder, in-memory data may not reference files |
| Required extensions | anything outside a short allow list is refused with its name |

A refused file produces an error text (in the log and in the clip tooltip) and never reaches the GPU. A texture that cannot be read only produces a warning, the model still loads without it. `tests/test_scene3d.cpp` covers these cases (truncated and tampered files, lying chunk lengths, out of range indices, cycles, shared children, huge counts, deep JSON, path tricks, bad base64, broken images, limits).

Not done yet: a time limit or cancellation for a very slow load. Closing the program while a huge model is still loading waits for that load to finish.

## Built-in scenes

| Clip | Vertices | Triangles | GPU geometry |
| --- | --- | --- | --- |
| Torus Knot Mesh | 5 061 | 9 600 | 349 KiB |
| Crystal Cluster Mesh (16 instances of one crystal) | 72 | 24 | 3 KiB |
| Orbit Rings Mesh (three animated rings and a pulsing core) | 5 744 | 10 560 | 393 KiB |

They are generated by code (`src/scene3d/procedural.cpp`) and use the same `Scene` type and the same renderer as loaded files. `writeGlb` turns any `Scene` back into a `.glb`, which the tests use for round trips.

## Measuring

```
NyrexVJ --bench-scene LAYERS:FRAMES[:CLIP] [--bench-out result.json]
```

It puts CLIP (default `Torus Knot Mesh`, a shader clip such as `Plasma` works too, which gives a baseline) on 1 to 4 layers, waits for the load, warms up for 30 frames and then measures FRAMES frames: CPU submit time, GPU time (`GL_TIME_ELAPSED`), scene statistics, the estimated GPU memory split into render targets, depth and geometry, and the resident memory of the process.

Memory is deterministic and was measured with 4 layers at 1920 x 1080:

| Part | Size |
| --- | --- |
| Render targets (5 full size textures, no scene needed) | 39.5 MiB |
| Shared depth buffer, only once a scene clip has drawn | 8.0 MiB |
| Geometry of Orbit Rings Mesh, shared by all 4 layers | 0.7 MiB |
| Clip thumbnails and layer previews | 2.4 MiB |

So the fixed cost of the first 3D clip is about 8 MiB of depth buffer, every further layer and every further instance of the same scene costs almost nothing, and the geometry of a model is paid once per model.

**Frame times are not measured yet on real hardware.** The numbers collected so far come from a software OpenGL renderer (llvmpipe) on a build server: 4 layers of Torus Knot Mesh took 190 ms per frame, 4 layers of the Plasma shader 209 ms. They show that the mesh path is not slower than a full screen shader there and that the benchmark works, they say nothing about a real GPU. The benchmark has to be run on the target PC (and on a weaker one) before anything is decided from it.

## Decisions

ADR 0001 said to benchmark before committing. The decisions that followed are recorded in the ADRs and implemented in v0.9.0:

- A scene graph of our own: the scene runtime evaluates node motions, repeats, framing and audio bindings itself ([ADR 0004](adr/0004-scene-document-and-clip-pack.md)).
- Scene file and asset packaging: a JSON scene document inside a `.nyxclip` container with embedded `.glb` models and textures; a missing asset gives a warning and the rest of the scene still plays.
- Physically based shading with an analytic sky and up to four lights, next to the unchanged classic shading ([ADR 0005](adr/0005-pbr-shading.md)).
- The Clip Creator as a floating window with a live preview ([ADR 0006](adr/0006-clip-creator.md)).
- Unloading: a scene that is in no cell and was not used for 120 seconds releases its GPU memory and loads again on demand; the models folder is rescanned without a restart.
- glTF breadth: materials and textures as listed above. Skins, morph targets, glTF lights and cameras and blended transparency stay unsupported and are ignored with a warning.

Still open: frame times on a real GPU. Run `NyrexVJ --bench-scene 4:300:"Torus Knot Mesh" --bench-out result.json` and the same with `Chrome Orbs Mesh` on the target PC and on a weaker one.

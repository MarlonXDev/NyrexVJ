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
| Container | `.glb` and `.gltf` (external `.bin`, `data:` URIs, relative image files), `EXT_meshopt_compression` (the decoder of meshoptimizer, on by default), `KHR_draco_mesh_compression` (Draco, off by default, see "Compressed files") | any other required extension |
| Geometry | triangles, triangle strips, triangle fans, indexed or not, `POSITION`, `NORMAL` (computed when missing), `TEXCOORD_0`, `COLOR_0`, normalized and quantized integer attributes, sparse accessors, `JOINTS_0` and `WEIGHTS_0`, morph targets with `POSITION` and `NORMAL` deltas | points, lines, second UV set, tangents, morph target tangents and colours |
| Materials | base colour factor and texture (PNG and JPEG), metallic-roughness factors and texture, normal texture, emissive factor, texture and `KHR_materials_emissive_strength`, alpha mode opaque, mask and blend, double sided | occlusion, texture transforms, other material extensions |
| Hierarchy | node tree with translation, rotation, scale or matrix, mesh sharing between nodes, skins (joints with inverse bind matrices), default morph weights per mesh and per node | cameras and lights from the file are read and counted but **not applied** (see "Lights and cameras in a file") |
| Animation | translation, rotation and scale channels, weights channels (morph targets), skeletal animation through the joints of a skin, linear and step, cubic spline approximated as linear, looping | none that was asked for |

Shading is a fixed, cheap model: two directional lights, a little ambient light and a rim light. Built-in scenes are drawn in the layer palette (Color A to Color B, or the rainbow switch); models keep their own colours. The camera orbits the scene automatically, is fitted to its bounding sphere and zooms with the layer Scale. Layer Speed drives both the orbit and the animation, Bass pulses the scale and Kick adds a small punch and a stronger rim.

## Skins, morph targets and blended transparency (v0.32.0)

[ADR 0025](adr/0025-3d-engine-2.md) has the reasons. The short version of what a file with these features does:

- **Skinning** runs on the graphics card. Each skin's joint matrices (`world of the joint node x inverse bind matrix`) are evaluated once per frame on the processor, written into one floating point texture (four texels per joint) and read by the vertex shader with four influences per vertex (`JOINTS_0`, `WEIGHTS_0`). A skinned mesh is drawn with an identity model matrix, as the glTF specification says, so the transform of its own node is ignored. All meshes of one node share one set of joint matrices. Several figures made from the same model each get their own joints.
- **Morph targets** are stored as one floating point texture of position and normal deltas per mesh and are blended in the vertex shader. A mesh may have up to 32 targets and the shader applies the **eight strongest weights** of each draw (the others are dropped, the strongest first, so a file with more active targets looks nearly right instead of failing). Default weights come from the mesh and the node, animated weights from a `weights` channel; a skinned and morphed mesh applies the morph first and the skin second, as the specification says.
- **Blended transparency** (`alphaMode` `BLEND`) is drawn in a second pass after all opaque and masked meshes, with depth testing on and depth writing off, sorted back to front **per mesh instance** (by the distance of the centre of the instance, for a skinned mesh the average of its joints). It is not sorted per triangle, so two blended triangles of the same mesh can overlap in the wrong order, the usual limit of this method. A double sided blended mesh draws its back faces first and then its front faces. Draw order in the file does not matter (a test renders the same two planes in both orders and compares the pixels). Opaque and masked materials and the built-in scenes are drawn exactly as before.
- **Limits** protect the video memory next to VRChat: 128 joints per skin, 64 skins, 32 morph targets per mesh, 1024 joints per frame for all skinned figures together (a figure that would exceed it is skipped, counted, and the first time logged as `N animated figures were skipped`; the other figures keep playing), 128 MiB of morph data when loading and 640 MiB of graphics memory per scene (`kSceneMaxGpuBytes`, checked when loading, in the Clip Creator and again before the upload). A skin is about 24 bytes per vertex, a morph target 24 bytes per vertex and target.
- The Clip Creator accepts a model with skins and morph targets as a model object. A model that would take the scene over the skin limit is left out with a warning in the clip window, and a scene that would need more than the memory limit is refused with a message there. The details in the library (hover over a clip) list skins, joints, morph targets and blended materials.

## Compressed files

| Extension | Where it is decoded | Build option | Default |
| --- | --- | --- | --- |
| `EXT_meshopt_compression` | on the buffer views, before any accessor is read (modes attributes, triangles and indices, filters octahedral, quaternion and exponential) | `NYREXVJ_MESHOPT` | on |
| `KHR_draco_mesh_compression` | on the primitive, instead of its accessors (the number of decoded points must match the accessors) | `NYREXVJ_DRACO` | **off** |

The meshoptimizer decoder (v1.3, MIT) is a few small C++ files that are compiled into the program. Draco 1.5.7 (Apache 2.0) is large, so it is only built when you ask for it with `-DNYREXVJ_DRACO=ON`; it is **not** fetched or built by default. Without a decoder a file that needs it is refused before anything is read, and the message names the CMake option (for example `the file needs KHR_draco_mesh_compression, which this build of NyrexVJ was made without (CMake option NYREXVJ_DRACO)`). A file that lists Meshopt only as used and brings a fallback buffer (`gltfpack -cf`) loads in a build without the decoder from the fallback buffer (tested with a model and its `.fallback.bin`; the fallback file is not read when the decoder is there); a Draco primitive that also has plain accessors takes them when there is no Draco decoder (in the code, no test file for it). Both decoders get the same limits as plain data (vertex, index, memory and morph limits), and truncated or tampered compressed data fails with a message and does not reach the GPU (tests cut and flip bytes of every test file).

## Lights and cameras in a file

Lights (`KHR_lights_punctual`) and cameras of a `.glb` are read and counted (up to 16 each; the library details show the numbers), a spot light is stored as a point light with a note, but **they are not applied automatically**. The scene keeps its own lights, sky and automatic or authored camera, so a model that brings its own lights looks like it did before, and a scene made in the Clip Creator is not changed by a model that is added to it. The data is in `Scene::fileLights` and `Scene::fileCameras` so a later step can offer them as a choice. A test renders a model with lights and cameras and compares it with the same model without them.

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
| Required extensions | anything outside a short allow list is refused with its name; Draco and Meshopt are allowed only when their decoder is built in |
| Skins, morph targets | 64 skins, 128 joints per skin, 32 morph targets per mesh, 128 MiB of morph data, joint indices inside the skin, weights finite and not negative, 640 MiB of graphics memory per scene |

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

Since v0.32.0 the benchmark JSON of `--bench-scene` also has `skins`, `joints`, `morphTargets` and `blendMaterials` under `scene`. Models for it can be made with `scripts/make_bench_models.py OUTPUT_FOLDER` (a tube of 64,000 vertices, plain, skinned, morphed, both and blended; copy them to the `models` folder of the data folder and use `--bench-scene 1:60:"Model: bench_skin"`). Measurements and the comparison with the previous version are in [PERFORMANCE.md](PERFORMANCE.md). `NyrexVJ --scene-selftest tests/data/models` renders the test models and checks the pixels.

**Frame times are not measured yet on real hardware.** The numbers collected so far come from a software OpenGL renderer (llvmpipe) on a build server: 4 layers of Torus Knot Mesh took 190 ms per frame, 4 layers of the Plasma shader 209 ms. They show that the mesh path is not slower than a full screen shader there and that the benchmark works, they say nothing about a real GPU. The benchmark has to be run on the target PC (and on a weaker one) before anything is decided from it.

## Music on materials (0.68.0)

[ADR 0062](adr/0062-audio-reactive-3d-materials-sources-and-targets-for-scene-bindings.md). Every object, light and camera of a scene has up to four audio bindings (it was two): a source, a target and an amount. Rows on the same target add up and a child node takes the sum of its parent.

| Sources | What they are |
| --- | --- |
| Bass, Mid, High, Level, Kick | the five values the scene player always had |
| Beat | a pulse `exp(-5 * fract(beat))` from the beat position, so it follows tap tempo, MIDI clock or Link and works without sound |
| Band 1 to 8 | the mean of 16 neighbouring bins of the 128 logarithmic bins of the spectrum, about 30, 66, 144, 316, 690, 1,500, 3,300, 7,300 and 16,000 Hz between the edges; no new analysis |

| Targets on an object | Effect | Limit of the sum |
| --- | --- | --- |
| Size, Glow, Twist, Lift | as before | as before |
| Colour shift | turns the hue of the base colour and of the glow colour, 1.0 is one whole turn | plus and minus 8 turns |
| Roughness | added to the roughness before the shader's own clamp (0.045 to 1) | plus and minus 1 |
| Texture shift X, Y | added to the texture coordinates of every lookup of the material; the textures repeat, so a whole number comes back to the start | plus and minus 8 |
| Inflate | moves each vertex along its normal by 0.25 * amount * source times the half diagonal of the mesh bounding box | plus and minus 1 |
| Ripple | a travelling wave along the normal, 0.12 * amount * source * radius * sin(dot(p / radius, (3.1, 4.3, 2.7)) + 5 * time) | plus and minus 1 |

Lights keep Light intensity and the camera keeps Zoom. Inflate and Ripple run in the vertex shader after the morph targets and before the skin, in the local space of the mesh. They keep the old normals, so a deep ripple on a sharp shape is lit as if it were flat. Colour shift, Roughness and Texture shift live in the PBR fragment shader, which every scene from the Clip Creator and every glTF file uses; Inflate and Ripple also work in the classic shading of the built-in scenes, which have no material bindings of their own.

A scene document is written as `"version": 1` while it uses only the old sources, the old targets and two rows, and as `"version": 2` otherwise; programs older than 0.68.0 refuse version 2 with their message about a newer program. A binding with an unknown source or target, a binding that is not an object and a binding after the fourth are dropped, the scene still plays, and each one adds a line to the warnings (at most 16) in the Clip Creator and the log. Settings, Performance, **Music moves 3D materials** (on at the start, written to `settings.json` only when off) leaves the six material targets out and draws the material as authored. The preview in the Clip Creator uses `virtualAudio(beat, time)` when Simulate audio is ticked, a fixed function, so the same moment always gives the same picture.

## Decisions

ADR 0001 said to benchmark before committing. The decisions that followed are recorded in the ADRs and implemented in v0.9.0:

- A scene graph of our own: the scene runtime evaluates node motions, repeats, framing and audio bindings itself ([ADR 0004](adr/0004-scene-document-and-clip-pack.md)).
- Scene file and asset packaging: a JSON scene document inside a `.nyxclip` container with embedded `.glb` models and textures; a missing asset gives a warning and the rest of the scene still plays.
- Physically based shading with an analytic sky and up to four lights, next to the unchanged classic shading ([ADR 0005](adr/0005-pbr-shading.md)).
- The Clip Creator as a floating window with a live preview ([ADR 0006](adr/0006-clip-creator.md)).
- Unloading: a scene that is in no cell and was not used for 120 seconds releases its GPU memory and loads again on demand; the models folder is rescanned without a restart.
- glTF breadth (v0.9.0): materials and textures as listed above.
- 3D Engine 2.0 (v0.32.0): skinning, morph targets, blended transparency, Meshopt and Draco import and the limits above, see [ADR 0025](adr/0025-3d-engine-2.md). glTF lights and cameras are read but not applied.
- Music on materials (v0.68.0): eight spectrum bands and the beat as new sources, six material targets, four rows, see [ADR 0062](adr/0062-audio-reactive-3d-materials-sources-and-targets-for-scene-bindings.md).

Still open: frame times on a real GPU. Run `NyrexVJ --bench-scene 4:300:"Torus Knot Mesh" --bench-out result.json` and the same with `Chrome Orbs Mesh` on the target PC and on a weaker one.
